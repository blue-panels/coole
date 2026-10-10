/*
   The debugger plugin: the breakpoints, their marks in the gutter, their conditions, and the
   functions of the project to put them on.

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"

#include "src/keymap.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"

#include "lib/editor-plugin.h"
#include "src/plugins/project/project-core.h"  // project_find_root (), without the project plugin
#include "debug-backend.h"
#include "debugger-int.h"

/*** forward declarations (file scope functions) */

static void debug_breakpoints_dedup (debugger_t *debug);

/*** end of forward declarations */

/*** file scope functions *********************************************************************/

/* The functions of a file, NULL when the plugin ctags is not there to give them */
void
debug_functions_free (gpointer p)
{
    if (p != NULL)
        g_ptr_array_unref ((GPtrArray *) p);
}

void
debug_breakpoint_free (gpointer data)
{
    debug_breakpoint_t *bp = (debug_breakpoint_t *) data;

    g_free (bp->file);
    g_free (bp->gdb_number);
    g_free (bp->condition);
    g_free (bp);
}

/* Whether two names name the same file: GDB gives the real path, the editor the name the file
   was opened by */
static gboolean
debug_same_file (const char *a, const char *b)
{
    char *ra, *rb;
    gboolean same;

    if (a == NULL || b == NULL)
        return FALSE;
    if (strcmp (a, b) == 0)
        return TRUE;
    ra = realpath (a, NULL);
    rb = realpath (b, NULL);
    same = ra != NULL && rb != NULL && strcmp (ra, rb) == 0;
    free (ra);
    free (rb);
    return same;
}

int
debug_breakpoint_mark (const debugger_t *debug, const debug_breakpoint_t *bp)
{
    if (bp->disabled)
        return DEBUG_MARK_DISABLED;
    if (debug_session_live (debug) && (bp->gdb_number == NULL || bp->unverified))
        return DEBUG_MARK_PENDING;
    return bp->condition != NULL ? DEBUG_MARK_CONDITION : DEBUG_MARK_BREAKPOINT;
}

static gboolean
debug_is_breakpoint_mark (int mark)
{
    return mark == DEBUG_MARK_BREAKPOINT || mark == DEBUG_MARK_PENDING
        || mark == DEBUG_MARK_DISABLED || mark == DEBUG_MARK_CONDITION;
}

static debug_breakpoint_t *
debug_breakpoint_at (const debugger_t *debug, const char *file, long line, guint *index)
{
    guint i;

    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (bp->line == line && debug_same_file (bp->file, file))
        {
            if (index != NULL)
                *index = i;
            return bp;
        }
    }
    return NULL;
}

/* Put the marks of the breakpoints and of the current line in the gutter: of one file, or of all
   (file NULL).  The marks are taken off first, so call debug_breakpoints_sync() before, to keep
   what the editing of the text has moved. */
void
debug_marks_show (debugger_t *debug, const char *file)
{
    guint i;

    if (debug->host->set_marker == NULL)
        return;
    for (i = 0; i < DEBUG_MARK_COUNT; i++)
        debug->host->clear_markers (debug->host, file, debug->marks[i]);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (file == NULL || debug_same_file (bp->file, file))
            debug->host->set_marker (debug->host, bp->file, bp->line,
                                     debug->marks[debug_breakpoint_mark (debug, bp)], TRUE);
    }
    if (debug->current_file != NULL
        && (file == NULL || debug_same_file (debug->current_file, file)))
        debug->host->set_marker (
            debug->host, debug->current_file, debug->current_line,
            debug->marks[debug_breakpoint_at (debug, debug->current_file, debug->current_line, NULL)
                                 != NULL
                             ? DEBUG_MARK_EXEC_BREAKPOINT
                             : DEBUG_MARK_EXEC],
            TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/* A value looked into: a structure, an object or an array, its members a tree */
/* --------------------------------------------------------------------------------------------- */

/* The debugger has taken a breakpoint, or has refused it */
static void
debug_reply_breakpoint (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    // a breakpoint removed while the debugger was putting it: it goes from the debugger too
    for (i = 0; i < debug->dropped_tokens->len; i++)
        if (g_array_index (debug->dropped_tokens, unsigned int, i) == reply->request)
        {
            g_array_remove_index_fast (debug->dropped_tokens, i);
            if (reply->ok && reply->id != NULL)
                (void) debug->backend->ops->break_delete (debug->backend, reply->id);
            return;
        }
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (bp->pending_token != reply->request)
            continue;
        bp->pending_token = 0;
        if (!reply->ok)
        {
            // the mark stays the one of a breakpoint the debugger has not taken
            if (reply->msg != NULL)
                debug_output_console (debug, reply->msg, TRUE);
            debug_marks_show (debug, NULL);
            return;
        }
        g_free (bp->gdb_number);
        bp->gdb_number = g_strdup (reply->id);
        bp->unverified = reply->pending;
        if (reply->pending && reply->msg != NULL)
            debug_output_console (debug, reply->msg, TRUE);
        // GDB stops on the next line with code: the breakpoint goes there
        if (reply->line > 0)
        {
            bp->gdb_line = reply->line;
            if (bp->gdb_line != bp->line)
                debug_breakpoint_move (debug, bp->gdb_number, bp->gdb_line);
        }
        debug_marks_show (debug, NULL);
        return;
    }
}

/* The breakpoint of GDB @number to @line, the marks of the text taken first: the sync may merge
   two breakpoints of one line, so the breakpoint is looked for after it */
void
debug_breakpoint_move (debugger_t *debug, const char *number, long line)
{
    guint i;

    debug_breakpoints_sync (debug);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (g_strcmp0 (bp->gdb_number, number) == 0)
        {
            bp->line = line;
            debug_breakpoints_dedup (debug);
            debug_config_save (debug);
            return;
        }
    }
}

gboolean
debug_breakpoint_install (debugger_t *debug, debug_breakpoint_t *bp)
{
    bp->pending_token =
        debug->backend->ops->break_insert (debug->backend, bp->file, bp->line, bp->disabled,
                                           bp->condition, debug_reply_breakpoint, NULL, NULL);
    return bp->pending_token != 0;
}

void
debug_breakpoint_remove (debugger_t *debug, guint index)
{
    debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, index);

    if (bp->gdb_number != NULL && debug_alive (debug))
        (void) debug->backend->ops->break_delete (debug->backend, bp->gdb_number);
    else if (bp->pending_token != 0)
        g_array_append_val (debug->dropped_tokens, bp->pending_token);
    g_ptr_array_remove_index (debug->breakpoints, index);
}

/* A breakpoint kept but not stopped on, or stopped on again */
void
debug_breakpoint_toggle_enabled (debugger_t *debug, guint index)
{
    debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, index);

    bp->disabled = !bp->disabled;
    if (bp->gdb_number != NULL && debug_alive (debug))
        (void) debug->backend->ops->break_enable (debug->backend, bp->gdb_number, !bp->disabled);
    debug_breakpoints_sync (debug);
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    debug_session_refresh (debug);
}

/* Two breakpoints on one line are one: the later goes */
static void
debug_breakpoints_dedup (debugger_t *debug)
{
    guint i, j;

    for (i = 0; i < debug->breakpoints->len; i++)
        for (j = i + 1; j < debug->breakpoints->len;)
        {
            const debug_breakpoint_t *a = g_ptr_array_index (debug->breakpoints, i);
            const debug_breakpoint_t *b = g_ptr_array_index (debug->breakpoints, j);

            if (a->line == b->line && debug_same_file (a->file, b->file))
                debug_breakpoint_remove (debug, j);
            else
                j++;
        }
}

static gint
debug_long_compare (gconstpointer a, gconstpointer b)
{
    const long x = *(const long *) a, y = *(const long *) b;

    return x < y ? -1 : x > y ? 1 : 0;
}

static gint
debug_breakpoint_line_compare (gconstpointer a, gconstpointer b)
{
    const debug_breakpoint_t *x = *(debug_breakpoint_t *const *) a;
    const debug_breakpoint_t *y = *(debug_breakpoint_t *const *) b;

    return x->line < y->line ? -1 : x->line > y->line ? 1 : 0;
}

/* The marks of the breakpoints move with their lines when the text is edited: the breakpoints of
   every open file take the lines of their marks */
void
debug_breakpoints_sync (debugger_t *debug)
{
    GPtrArray *done = g_ptr_array_new ();
    gboolean moved = FALSE;
    guint i, j;

    if (debug->host->marker_lines == NULL)
    {
        g_ptr_array_free (done, TRUE);
        return;
    }
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        const debug_breakpoint_t *first = g_ptr_array_index (debug->breakpoints, i);
        GArray *lines = NULL;
        GPtrArray *mine;
        int mark;

        for (j = 0; j < done->len && !debug_same_file (g_ptr_array_index (done, j), first->file);
             j++)
            ;
        if (j < done->len)
            continue;
        g_ptr_array_add (done, first->file);

        for (mark = 0; mark < DEBUG_MARK_COUNT; mark++)
        {
            GArray *some;

            if (!debug_is_breakpoint_mark (mark))
                continue;
            some = debug->host->marker_lines (debug->host, first->file, debug->marks[mark]);
            if (some == NULL)
                continue;
            if (lines == NULL)
                lines = some;
            else
            {
                g_array_append_vals (lines, some->data, some->len);
                g_array_free (some, TRUE);
            }
        }
        if (lines == NULL)
            continue;  // no window has the file

        mine = g_ptr_array_new ();
        for (j = i; j < debug->breakpoints->len; j++)
        {
            debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, j);

            if (debug_same_file (bp->file, first->file))
                g_ptr_array_add (mine, bp);
        }
        /* a mark is never lost by editing: other counts mean the marks of the file are not
           there yet */
        if (mine->len == lines->len)
        {
            g_array_sort (lines, debug_long_compare);
            g_ptr_array_sort (mine, debug_breakpoint_line_compare);
            for (j = 0; j < mine->len; j++)
            {
                debug_breakpoint_t *bp = g_ptr_array_index (mine, j);
                const long line = g_array_index (lines, long, j);

                /* the program running was built from the text as it was: GDB keeps the
                   breakpoint on the old line, the next start puts it on the new one */
                if (bp->line != line)
                {
                    bp->line = line;
                    moved = TRUE;
                }
            }
        }
        g_ptr_array_free (mine, TRUE);
        g_array_free (lines, TRUE);
    }
    g_ptr_array_free (done, TRUE);
    if (moved)
        debug_breakpoints_dedup (debug);
}

/* A breakpoint on a line of a file, sent to GDB when it runs */
static void
debug_breakpoint_add (debugger_t *debug, const char *file, long line, const char *condition)
{
    char *real = realpath (file, NULL);
    debug_breakpoint_t *bp = g_new0 (debug_breakpoint_t, 1);

    bp->file = real != NULL ? g_strdup (real) : g_strdup (file);
    bp->line = line;
    bp->condition = condition != NULL && *condition != '\0' ? g_strdup (condition) : NULL;
    free (real);
    g_ptr_array_add (debug->breakpoints, bp);
    // the breakpoints of the start are sent already: this one is sent by itself
    if (debug_session_live (debug) && debug->breakpoints_installed)
        (void) debug_breakpoint_install (debug, bp);
}

static WInput *fn_input = NULL;

static WListbox *fn_list = NULL;

static debugger_t *fn_debug = NULL;

static GPtrArray *fn_found = NULL;  // debug_function_t
static char *fn_last = NULL;

static void
debug_function_free (gpointer p)
{
    debug_function_t *f = (debug_function_t *) p;

    g_free (f->name);
    g_free (f->file);
    g_free (f);
}

/* The functions of the project whose names have the letters of @query, best first: NULL when
   the plugin ctags is not there */
GPtrArray *
debug_functions (const debugger_t *debug, const char *root, const char *file, const char *query)
{
    GVariantDict args;
    GVariant *reply, *symbols;
    GVariantIter iter;
    const char *name, *kind, *path;
    gint32 line;
    GPtrArray *found;

    g_variant_dict_init (&args, NULL);
    if (root != NULL)
        g_variant_dict_insert (&args, "root", "s", root);
    if (file != NULL)
        g_variant_dict_insert (&args, "file", "s", file);
    g_variant_dict_insert (&args, "query", "s", query);
    g_variant_dict_insert (&args, "max", "i", (gint32) (file != NULL ? 0 : 500));
    reply = debug->host->service_call (debug->host, "ctags", "symbols", g_variant_dict_end (&args),
                                       NULL);
    if (reply == NULL)
        return NULL;
    found = g_ptr_array_new_with_free_func (debug_function_free);
    symbols = g_variant_lookup_value (reply, "symbols", G_VARIANT_TYPE ("a(sssi)"));
    if (symbols != NULL)
    {
        g_variant_iter_init (&iter, symbols);
        while (g_variant_iter_next (&iter, "(&s&s&si)", &name, &kind, &path, &line))
            if (strcmp (kind, "func") == 0)
            {
                debug_function_t *f = g_new (debug_function_t, 1);

                f->name = g_strdup (name);
                f->file = g_strdup (path);
                f->line = line;
                g_ptr_array_add (found, f);
            }
        g_variant_unref (symbols);
    }
    g_variant_unref (reply);
    return found;
}

static void
debug_function_refilter (void)
{
    const char *text = input_get_ctext (fn_input);
    guint i;

    if (fn_last != NULL && strcmp (fn_last, text) == 0)
        return;
    g_free (fn_last);
    fn_last = g_strdup (text);
    listbox_remove_list (fn_list);
    if (fn_found != NULL)
        g_ptr_array_unref (fn_found);
    fn_found = *text != '\0' ? debug_functions (fn_debug, fn_debug->project_dir, NULL, text) : NULL;
    for (i = 0; fn_found != NULL && i < fn_found->len; i++)
    {
        const debug_function_t *f = g_ptr_array_index (fn_found, i);
        const char *shown = f->file;
        char *label;

        if (g_str_has_prefix (shown, fn_debug->project_dir)
            && shown[strlen (fn_debug->project_dir)] == '/')
            shown += strlen (fn_debug->project_dir) + 1;
        label = g_strdup_printf ("%-40s %s:%ld", f->name, shown, f->line);
        listbox_add_item (fn_list, LISTBOX_APPEND_AT_END, 0, label, (void *) f, FALSE);
        g_free (label);
    }
    listbox_select_first (fn_list);
    widget_draw (WIDGET (fn_list));
}

static cb_ret_t
debug_function_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
    {
        cb_ret_t ret = dlg_default_callback (w, sender, msg, parm, data);

        widget_select (WIDGET (fn_input));
        return ret;
    }
    case MSG_KEY:
        if (parm == KEY_UP || parm == KEY_DOWN || parm == KEY_PPAGE || parm == KEY_NPAGE)
        {
            send_message (WIDGET (fn_list), w, MSG_KEY, parm, data);
            return MSG_HANDLED;
        }
        return dlg_default_callback (w, sender, msg, parm, data);
    case MSG_POST_KEY:
        debug_function_refilter ();
        return MSG_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

static lcback_ret_t
debug_function_activate (WListbox *l)
{
    (void) l;
    return LISTBOX_DONE;
}

/* Choose a function of the project by the letters of its name; NULL when none is chosen, and
 *@name with what was typed when the plugin ctags is not there to list them */
static debug_function_t *
debug_function_pick (debugger_t *debug, const char *title, char **name)
{
    WDialog *dlg;
    debug_function_t *chosen = NULL;
    int dlg_h, dlg_w, list_h;
    GPtrArray *probe;

    *name = NULL;
    if (debug->project_dir == NULL)
        return NULL;
    // without ctags: a name to type
    probe = debug_functions (debug, debug->project_dir, NULL, "");
    if (probe == NULL)
    {
        *name = input_dialog (title, _ ("Function:"), "debug-function", "", INPUT_COMPLETE_NONE);
        return NULL;
    }
    g_ptr_array_unref (probe);

    dlg_w = MIN (COLS - 4, 90);
    list_h = MAX (5, MIN (LINES - 10, 16));
    dlg_h = list_h + 6;
    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, debug_function_callback, NULL, "[Debugger]", title);
    dlg->help_file = "debugger.md";
    fn_input = input_new (1, 1, input_colors, dlg_w - 2, "", "debug-function", INPUT_COMPLETE_NONE);
    group_add_widget (GROUP (dlg), fn_input);
    fn_list = listbox_new (3, 1, list_h, dlg_w - 2, FALSE, debug_function_activate);
    group_add_widget (GROUP (dlg), fn_list);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 - 10, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 + 1, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));
    fn_debug = debug;
    g_clear_pointer (&fn_last, g_free);
    debug_function_refilter ();

    if (dlg_run (dlg) == B_ENTER)
    {
        char *text = NULL;
        const debug_function_t *f = NULL;

        listbox_get_current (fn_list, &text, (void **) &f);
        if (f != NULL)
        {
            chosen = g_new (debug_function_t, 1);
            chosen->name = g_strdup (f->name);
            chosen->file = g_strdup (f->file);
            chosen->line = f->line;
        }
        else if (*input_get_ctext (fn_input) != '\0')
            *name = g_strdup (input_get_ctext (fn_input));
    }
    widget_destroy (WIDGET (dlg));
    fn_input = NULL;
    fn_list = NULL;
    fn_debug = NULL;
    g_clear_pointer (&fn_last, g_free);
    g_clear_pointer (&fn_found, g_ptr_array_unref);
    return chosen;
}

/* --------------------------------------------------------------------------------------------- */

/* Debug > Breakpoint on function: on the line of the function the index has */
mc_ep_result_t
debug_act_function_breakpoint (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_function_t *f;
    char *name;
    guint i;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    f = debug_function_pick (debug, _ ("Breakpoint on function"), &name);
    if (f == NULL)
    {
        if (name != NULL)
            debug_error (debug,
                         _ ("The functions of the project come from the index of the "
                            "plugin ctags: put the breakpoint on a line of the function."));
        g_free (name);
        return MC_EPR_FAILED;
    }
    debug_breakpoints_sync (debug);
    if (debug_breakpoint_at (debug, f->file, f->line, &i) == NULL)
    {
        debug_breakpoint_add (debug, f->file, f->line, NULL);
        debug_config_save (debug);
    }
    (void) debug->host->show_location (debug->host, f->file, f->line);
    debug_marks_show (debug, NULL);
    debug_session_refresh (debug);
    debug_function_free (f);
    return MC_EPR_OK;
}

/* Debug > Run to function: a breakpoint the debugger takes off when it stops there */
mc_ep_result_t
debug_act_run_to_function (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_function_t *f;
    char *name;
    gboolean sent;

    (void) edit;
    if (debug->state != DEBUG_STOPPED)
    {
        debug_error (debug, _ ("The program runs to a function from where it is stopped."));
        return MC_EPR_FAILED;
    }
    f = debug_function_pick (debug, _ ("Run to function"), &name);
    if (f != NULL)
    {
        const debug_launch_t *launch = debug_active_launch (debug);

        g_free (name);
        // a debug adapter takes the name only, what a function breakpoint of the protocol is
        if (launch != NULL && debug_launch_is_dap (launch))
            name = g_strdup (f->name);
        else
        {
            // GDB the function by its file, as two of them may have the name (static ones)
            char *base = g_path_get_basename (f->file);

            name = g_strdup_printf ("%s:%s", base, f->name);
            g_free (base);
        }
        debug_function_free (f);
    }
    if (name == NULL || *name == '\0')
    {
        g_free (name);
        return MC_EPR_FAILED;
    }
    sent = debug->backend->ops->break_function (debug->backend, name, TRUE) != 0
        && debug->backend->ops->exec (debug->backend, DEBUG_EXEC_CONTINUE, NULL, NULL, NULL) != 0;
    g_free (name);
    return sent ? MC_EPR_OK : MC_EPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

/* The condition of @bp asked for, the one it has to be changed: an empty one takes it off */
void
debug_breakpoint_condition (debugger_t *debug, debug_breakpoint_t *bp)
{
    char *condition;

    if (bp->pending_token != 0)
    {
        debug_error (debug, _ ("Wait for the debugger to confirm this breakpoint."));
        return;
    }
    condition = input_dialog (
        _ ("Breakpoint condition"), _ ("Stop there only when this is true (empty: each time):"),
        "debug-condition", bp->condition != NULL ? bp->condition : "", INPUT_COMPLETE_NONE);
    if (condition == NULL)
        return;
    if (*g_strstrip (condition) == '\0')
        g_clear_pointer (&condition, g_free);
    g_free (bp->condition);
    bp->condition = condition;
    // the debugger has it already: changed there too
    if (debug_session_live (debug) && bp->gdb_number != NULL
        && debug->backend->ops->break_condition != NULL)
        (void) debug->backend->ops->break_condition (debug->backend, bp->gdb_number,
                                                     condition != NULL ? condition : "");
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    debug_session_refresh (debug);
}

/* Alt-F6: the condition of the breakpoint of the line of the cursor, a breakpoint put there
   with it when there is none */
mc_ep_result_t
debug_condition_at_cursor (debugger_t *debug, void *edit)
{
    debug_breakpoint_t *bp;
    char *file, *condition;
    long line;

    if (edit == NULL || !debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    file = debug->host->get_current_file (debug->host, edit);
    line = debug->host->get_cursor_line (debug->host, edit);
    if (file == NULL || line <= 0)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    debug_breakpoints_sync (debug);
    bp = debug_breakpoint_at (debug, file, line, NULL);
    if (bp != NULL)
    {
        g_free (file);
        debug_breakpoint_condition (debug, bp);
        return MC_EPR_OK;
    }
    condition = input_dialog (_ ("Breakpoint condition"),
                              _ ("Stop there only when this is true (empty: each time):"),
                              "debug-condition", "", INPUT_COMPLETE_NONE);
    if (condition == NULL)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    debug_breakpoint_add (debug, file, line, g_strstrip (condition));
    g_free (condition);
    g_free (file);
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* Debug > Breakpoint condition */
mc_ep_result_t
debug_act_condition (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    return debug_condition_at_cursor (
        debug, edit != NULL ? edit : debug->host->window_top_file (debug->host));
}

mc_ep_result_t
debug_toggle_breakpoint (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_breakpoint_t *bp;
    char *file;
    long line;
    guint i = 0;

    if (edit == NULL)
        return MC_EPR_FAILED;
    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    file = debug->host->get_current_file (debug->host, edit);
    line = debug->host->get_cursor_line (debug->host, edit);
    if (file == NULL || line <= 0)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    debug_breakpoints_sync (debug);
    bp = debug_breakpoint_at (debug, file, line, &i);
    if (bp != NULL)
    {
        if (bp->pending_token != 0)
        {
            debug_error (debug, _ ("Wait for GDB to confirm this breakpoint."));
            g_free (file);
            return MC_EPR_FAILED;
        }
        debug_breakpoint_remove (debug, i);
        g_free (file);
    }
    else
    {
        debug_breakpoint_add (debug, file, line, NULL);
        g_free (file);
    }
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    return MC_EPR_OK;
}
