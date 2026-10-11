/*
   The debugger plugin: its panel, the state, the variables, the watches, the call stack and the
   breakpoints, and its button bars.

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
/*** end of forward declarations */

/*** file scope functions *********************************************************************/

static const char *
debug_state_name (debug_state_t state)
{
    switch (state)
    {
    case DEBUG_STARTING:
        return _ ("Starting");
    case DEBUG_RUNNING:
        return _ ("Running");
    case DEBUG_STOPPED:
        return _ ("Stopped");
    case DEBUG_FINISHED:
        return _ ("Finished");
    case DEBUG_OFF:
    default:
        return _ ("Not started");
    }
}

static const char *
debug_session_button_label (int cmd, debug_state_t state)
{
    switch (cmd)
    {
    case DEBUG_CMD_HELP:
        return _ ("Help");
    case DEBUG_CMD_START_CONTINUE:
        return state == DEBUG_STOPPED                       ? _ ("Continue")
            : state == DEBUG_OFF || state == DEBUG_FINISHED ? _ ("Start")
                                                            : NULL;
    case DEBUG_CMD_PAUSE:
        return state == DEBUG_RUNNING ? _ ("Pause") : NULL;
    case DEBUG_CMD_STEP_INTO:
        return state == DEBUG_STOPPED ? _ ("Into") : NULL;
    case DEBUG_CMD_STEP_OVER:
        return state == DEBUG_STOPPED ? _ ("Over") : NULL;
    case DEBUG_CMD_STEP_OUT:
        return state == DEBUG_STOPPED ? _ ("Out") : NULL;
    case DEBUG_CMD_STOP:
        return state == DEBUG_STARTING || state == DEBUG_RUNNING || state == DEBUG_STOPPED
            ? _ ("Stop")
            : NULL;
    case DEBUG_CMD_TOGGLE_BREAKPOINT:
        return _ ("Break");
    case DEBUG_CMD_RUN_TO_CURSOR:
        return state == DEBUG_STOPPED ? _ ("ToCurs") : NULL;
    case DEBUG_CMD_CLOSE:
        return _ ("Close");
    case DEBUG_CMD_INDEX:
        return _ ("Index");
    default:
        return NULL;
    }
}

void
debug_window_buttonbar (const debugger_t *debug, Widget *w)
{
    WButtonBar *bb = buttonbar_find (DIALOG (w->owner));
    int i;

    if (bb == NULL || debug == NULL)
        return;
    for (i = 1; i <= 10; i++)
    {
        const int cmd = debug_command_of_key (debug, KEY_F (i));
        const char *label = debug_session_button_label (cmd, debug->state);

        // a window of the debugger is no file: its cursor is on no line
        if (cmd == DEBUG_CMD_RUN_TO_CURSOR && w != WIDGET (debug->session_window))
            label = NULL;
        if (label == NULL)
            buttonbar_clear_label (bb, i, NULL);
        else
            buttonbar_set_label_command (bb, i, label, debug->commands[cmd],
                                         cmd == DEBUG_CMD_HELP ? NULL : w);
    }
    widget_draw (WIDGET (bb));
}

/* The button bar of the panel: the commands go to the panel */
static void
debug_session_buttonbar (debug_session_window_t *session)
{
    debug_window_buttonbar (session->debug, WIDGET (session));
}

/* The button bar of a file window: the editor's own, with the debugger's buttons over it in step
   mode */
void
debug_editor_buttonbar (debugger_t *debug, Widget *edit)
{
    WButtonBar *bb = buttonbar_find (DIALOG (edit->owner));
    int i;

    if (bb == NULL)
        return;
    edit_set_buttonbar (EDIT (edit), bb);
    if (debug_stepping (debug) || debug->debug_mode)
        for (i = 1; i <= 10; i++)
        {
            const int cmd = debug_command_of_key (debug, KEY_F (i));
            const gboolean editors =
                cmd == DEBUG_CMD_NONE || cmd == DEBUG_CMD_HELP || cmd == DEBUG_CMD_CLOSE;

            // the bar sends the command to the editor, which gives it to handle_action()
            if (!editors && debug_session_button_label (cmd, debug->state) != NULL)
                buttonbar_set_label_command (bb, i, debug_session_button_label (cmd, debug->state),
                                             debug->commands[cmd], NULL);
            else if (!editors
                     || (debug_stepping (debug)
                         && !debug_step_passes (
                             keybind_lookup_keymap_command (edit->keymap, KEY_F (i)))))
                buttonbar_clear_label (bb, i, NULL);
        }
    widget_draw (WIDGET (bb));
}

/* A row of the panel of the debugger */
typedef enum
{
    PANEL_TITLE,  // the name of a part
    PANEL_TEXT,   // a line to read
    PANEL_LOCAL,
    PANEL_WATCH,
    PANEL_FRAME,
    PANEL_BREAKPOINT,
    PANEL_REGISTERS,  // the title of the registers, Enter shows and hides them
    PANEL_REGISTER,
    PANEL_ADDRESS_BREAKPOINT
} debug_panel_kind_t;

typedef struct
{
    debug_panel_kind_t kind;
    guint index;  // of the variable, the watch, the frame, the breakpoint or the register
    char *text;
    gboolean changed;  // a register the last steps have changed
} debug_panel_row_t;

static void
debug_panel_row_free (gpointer p)
{
    debug_panel_row_t *row = (debug_panel_row_t *) p;

    g_free (row->text);
    g_free (row);
}

static debug_panel_row_t *
debug_panel_add (GPtrArray *rows, debug_panel_kind_t kind, guint index, char *text)
{
    debug_panel_row_t *row = g_new0 (debug_panel_row_t, 1);

    row->kind = kind;
    row->index = index;
    row->text = text;
    g_ptr_array_add (rows, row);
    return row;
}

static gboolean
debug_panel_selectable (const debug_panel_row_t *row)
{
    return row->kind != PANEL_TITLE && row->kind != PANEL_TEXT;
}

/* What the panel shows: the state, the variables of the frame, the watches, the call stack and
   the breakpoints */
static GPtrArray *
debug_panel_rows (const debugger_t *debug)
{
    GPtrArray *rows = g_ptr_array_new_with_free_func (debug_panel_row_free);
    const debug_launch_t *launch = debug_active_launch (debug);
    guint i;

    debug_panel_add (rows, PANEL_TEXT, 0,
                     g_strdup_printf ("%s  %s",
                                      debug->start_after_build ? _ ("Building")
                                                               : debug_state_name (debug->state),
                                      launch != NULL ? launch->name : _ ("<no configuration>")));
    if (debug->current_file != NULL)
        debug_panel_add (rows, PANEL_TEXT, 0,
                         g_strdup_printf ("%s %s:%ld",
                                          debug->current_func != NULL ? debug->current_func : "",
                                          x_basename (debug->current_file), debug->current_line));
    else if (debug->state == DEBUG_STOPPED && debug->current_func != NULL)
        debug_panel_add (
            rows, PANEL_TEXT, 0,
            g_strdup_printf (_ ("%s %s, with no source"), debug->current_func,
                             debug->current_address != NULL ? debug->current_address : ""));

    // how long the step or the continue took, a slow line to be seen at once
    if (debug->state == DEBUG_STOPPED && debug->run_time > 0)
    {
        const double seconds = (double) debug->run_time / G_USEC_PER_SEC;

        debug_panel_add (rows, PANEL_TEXT, 0,
                         seconds < 10.0 ? g_strdup_printf (_ ("  ran %.3f s"), seconds)
                                        : g_strdup_printf (_ ("  ran %.1f s"), seconds));
    }

    /* a program that runs does not stop by itself to read: the user is to type its answer in its
       terminal, which nothing would tell else */
    if (debug->state == DEBUG_RUNNING && debug->program_terminal)
    {
        debug_panel_add (rows, PANEL_TEXT, 0, g_strdup (_ ("  output and input: tab Program")));
        debug_panel_add (rows, PANEL_TEXT, 0, g_strdup (_ ("  (a click on it; Alt-F5 back)")));
    }

    // what to do next, while nothing runs
    if ((debug->state == DEBUG_OFF || debug->state == DEBUG_FINISHED) && !debug->start_after_build)
    {
        debug_panel_add (rows, PANEL_TITLE, 0, g_strdup (_ ("Next")));
        if (debug->breakpoints->len == 0)
            debug_panel_add (rows, PANEL_TEXT, 0,
                             g_strdup (debug->debug_mode ? _ ("  F6 on a line: breakpoint")
                                                         : _ ("  Ctrl-F8 on a line: breakpoint")));
        debug_panel_add (rows, PANEL_TEXT, 0,
                         g_strdup (debug->debug_mode ? _ ("  F5: build and run")
                                                     : _ ("  F5 here, Shift-F9 anywhere: run")));
        debug_panel_add (rows, PANEL_TEXT, 0, g_strdup (_ ("  Alt-F5: here and back")));
        if (debug_active_launch (debug) == NULL)
            debug_panel_add (rows, PANEL_TEXT, 0, g_strdup (_ ("  the first F5 asks what to run")));
    }

    if (debug->state == DEBUG_STOPPED)
    {
        debug_panel_add (rows, PANEL_TITLE, 0, g_strdup (_ ("Locals")));
        for (i = 0; i < debug->locals->len; i++)
        {
            const debug_variable_t *local = g_ptr_array_index (debug->locals, i);

            debug_panel_add (rows, PANEL_LOCAL, i,
                             g_strdup_printf ("%s = %s", local->name, local->value));
        }
        // a program of Python has none
        if (debug->backend == NULL || debug->backend->ops->has_registers == NULL
            || debug->backend->ops->has_registers (debug->backend))
            debug_panel_add (rows, PANEL_REGISTERS, 0,
                             g_strdup (debug->registers_shown
                                           ? _ ("Registers")
                                           : _ ("Registers  (Enter shows them)")));
        for (i = 0; debug->registers_shown && i < debug->registers->len; i++)
        {
            const debug_variable_t *reg = g_ptr_array_index (debug->registers, i);
            const char *before = debug->registers_before != NULL
                ? g_hash_table_lookup (debug->registers_before, reg->name)
                : NULL;
            debug_panel_row_t *row = debug_panel_add (
                rows, PANEL_REGISTER, i, g_strdup_printf ("%s = %s", reg->name, reg->value));

            row->changed = before != NULL && strcmp (before, reg->value) != 0;
        }
    }

    debug_panel_add (rows, PANEL_TITLE, 0, g_strdup (_ ("Watches")));
    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        debug_panel_add (rows, PANEL_WATCH, i,
                         g_strdup_printf ("%s = %s", watch->expression,
                                          watch->value != NULL ? watch->value : "?"));
    }
    if (debug->watches->len == 0)
        debug_panel_add (rows, PANEL_TEXT, 0, g_strdup (_ ("  Ins adds one")));

    if (debug->state == DEBUG_STOPPED)
    {
        debug_panel_add (rows, PANEL_TITLE, 0, g_strdup (_ ("Call stack")));
        for (i = 0; i < debug->frames->len; i++)
        {
            const debug_frame_t *frame = g_ptr_array_index (debug->frames, i);
            const char *base = frame->file != NULL ? x_basename (frame->file) : NULL;
            const char *label = strchr (frame->label, ' ');

            // "#0 func  file:line" with the file without its directories
            if (base != NULL)
                debug_panel_add (
                    rows, PANEL_FRAME, i,
                    g_strdup_printf ("#%ld %.*s %s:%ld", frame->level,
                                     label != NULL ? (int) strcspn (label + 1, " ") : 0,
                                     label != NULL ? label + 1 : "", base, frame->line));
            else
                debug_panel_add (rows, PANEL_FRAME, i, g_strdup (frame->label));
        }
    }

    debug_panel_add (rows, PANEL_TITLE, 0, g_strdup (_ ("Breakpoints")));
    {
        // the functions of the files, in their order: the one a breakpoint is in is named
        GHashTable *functions =
            g_hash_table_new_full (g_str_hash, g_str_equal, g_free, debug_functions_free);

        for (i = 0; i < debug->breakpoints->len; i++)
        {
            const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);
            GPtrArray *in_file;
            const char *function = NULL;
            long best;
            guint j;

            if (!g_hash_table_lookup_extended (functions, bp->file, NULL, (gpointer *) &in_file))
            {
                in_file = debug_functions (debug, debug->project_dir, bp->file, "");
                g_hash_table_insert (functions, g_strdup (bp->file), in_file);
            }
            // the nearest function that starts above it, in whatever order they come
            for (j = 0, best = 0; in_file != NULL && j < in_file->len; j++)
            {
                const debug_function_t *f = g_ptr_array_index (in_file, j);

                if (f->line <= bp->line && f->line > best)
                {
                    best = f->line;
                    function = f->name;
                }
            }
            debug_panel_add (
                rows, PANEL_BREAKPOINT, i,
                g_strdup_printf (
                    "%s %s:%ld%s%s%s%s", debug->glyphs[debug_breakpoint_mark (debug, bp)],
                    x_basename (bp->file), bp->line, function != NULL ? "  " : "",
                    function != NULL ? function : "", bp->condition != NULL ? "  if " : "",
                    bp->condition != NULL ? bp->condition : ""));
        }
        g_hash_table_destroy (functions);
    }
    for (i = 0; i < debug->address_breakpoints->len; i++)
    {
        const debug_address_breakpoint_t *bp = g_ptr_array_index (debug->address_breakpoints, i);

        debug_panel_add (
            rows, PANEL_ADDRESS_BREAKPOINT, i,
            g_strdup_printf ("%s *%s",
                             debug->glyphs[bp->gdb_number == NULL && debug_session_live (debug)
                                               ? DEBUG_MARK_PENDING
                                               : DEBUG_MARK_BREAKPOINT],
                             bp->address));
    }
    if (debug->breakpoints->len == 0 && debug->address_breakpoints->len == 0)
        debug_panel_add (rows, PANEL_TEXT, 0,
                         g_strdup (debug->debug_mode ? _ ("  F6 on a line puts one")
                                                     : _ ("  Ctrl-B on a line puts one")));
    return rows;
}

/* The cursor on a row that can be chosen, the nearest one in that direction */
static void
debug_panel_cursor (debug_session_window_t *session, const GPtrArray *rows, int to, int step)
{
    const int last = (int) rows->len - 1;
    int i;

    to = CLAMP (to, 0, MAX (last, 0));
    for (i = to; i >= 0 && i <= last; i += step)
        if (debug_panel_selectable (g_ptr_array_index (rows, i)))
        {
            session->cursor = i;
            return;
        }
    for (i = to; i >= 0 && i <= last; i -= step)
        if (debug_panel_selectable (g_ptr_array_index (rows, i)))
        {
            session->cursor = i;
            return;
        }
    session->cursor = 0;
}

static void
debug_session_draw (debug_session_window_t *session)
{
    WEditWindow *win = &session->window;
    Widget *w = WIDGET (session);
    debugger_t *debug = session->debug;
    const gboolean focused = widget_get_state (w, WST_FOCUSED);
    const int color = edit_window_frame_color (win, focused);
    const int lines = MAX (1, w->rect.lines - 2);
    const int width = MAX (0, w->rect.cols - 2);
    GPtrArray *rows;
    int row;

    edit_window_draw_frame (win, color, focused);
    tty_setcolor (color);
    widget_gotoyx (w, 0, 2);
    tty_print_string (str_term_trim (_ ("[Debugger]"), MAX (0, w->rect.cols - 10)));
    edit_window_draw_icons (win, color);

    if (debug == NULL)
    {
        widget_gotoyx (w, 1, 1);
        tty_print_string (_ ("Debug session ended."));
        return;
    }

    rows = debug_panel_rows (debug);
    if (session->cursor >= (int) rows->len
        || !debug_panel_selectable (g_ptr_array_index (rows, session->cursor)))
        debug_panel_cursor (session, rows, session->cursor, 1);
    if (session->cursor < session->top)
        session->top = session->cursor;
    else if (session->cursor >= session->top + lines)
        session->top = session->cursor - lines + 1;

    for (row = 0; row < lines; row++)
    {
        const int index = session->top + row;
        int c = EDITOR_NORMAL_COLOR;
        const char *text = "";
        char *indented = NULL;

        if (index < (int) rows->len)
        {
            const debug_panel_row_t *r = g_ptr_array_index (rows, index);

            if (index == session->cursor && focused && debug_panel_selectable (r))
                c = EDITOR_MARKED_COLOR;
            else if (r->kind == PANEL_TITLE || r->kind == PANEL_REGISTERS || r->changed)
                c = EDITOR_BOLD_COLOR;
            text = r->text;
            // the title of the registers is a title, that Enter opens
            if (debug_panel_selectable (r) && r->kind != PANEL_REGISTERS)
                text = indented = g_strconcat ("  ", r->text, (char *) NULL);
        }
        tty_setcolor (EDITOR_NORMAL_COLOR);
        tty_draw_hline (w->rect.y + row + 1, w->rect.x + 1, ' ', width);
        tty_setcolor (c);
        widget_gotoyx (w, row + 1, 1);
        tty_print_string (str_fit_to_term (text, width, J_LEFT_FIT));
        g_free (indented);
    }
    g_ptr_array_free (rows, TRUE);
}

/* Enter, Del, Ins, Space and the moves in the panel; FALSE for a key that is none of them */
static gboolean
debug_panel_key (debug_session_window_t *session, int key)
{
    debugger_t *debug = session->debug;
    GPtrArray *rows = debug_panel_rows (debug);
    const int lines = MAX (1, WIDGET (session)->rect.lines - 2);
    const debug_panel_row_t *row =
        session->cursor < (int) rows->len ? g_ptr_array_index (rows, session->cursor) : NULL;
    gboolean handled = TRUE;

    /* the condition of the breakpoint of the row, Alt-F6 as in a file; on another row nothing,
       not that of the line of a file out of sight */
    if (debug_command_of_key (debug, key) == DEBUG_CMD_CONDITION)
    {
        if (row != NULL && row->kind == PANEL_BREAKPOINT)
            debug_breakpoint_condition (debug, g_ptr_array_index (debug->breakpoints, row->index));
        else
            tty_beep ();
        g_ptr_array_free (rows, TRUE);
        if (session->debug != NULL)
            widget_draw (WIDGET (session));
        return TRUE;
    }

    switch (key)
    {
    case KEY_UP:
        debug_panel_cursor (session, rows, session->cursor - 1, -1);
        break;
    case KEY_DOWN:
        debug_panel_cursor (session, rows, session->cursor + 1, 1);
        break;
    case KEY_PPAGE:
        debug_panel_cursor (session, rows, session->cursor - lines, -1);
        break;
    case KEY_NPAGE:
        debug_panel_cursor (session, rows, session->cursor + lines, 1);
        break;
    case KEY_HOME:
        debug_panel_cursor (session, rows, 0, 1);
        break;
    case KEY_END:
        debug_panel_cursor (session, rows, (int) rows->len - 1, -1);
        break;
    case KEY_IC:
        (void) debug_add_watch (debug, NULL);
        break;
    case ':':
        debug_gdb_command (debug);
        break;
    case '\n':
    case KEY_ENTER:
        if (row == NULL)
            break;
        if (row->kind == PANEL_LOCAL)
            (void) debug_evaluate_text (
                debug,
                g_strdup (
                    ((debug_variable_t *) g_ptr_array_index (debug->locals, row->index))->name));
        else if (row->kind == PANEL_WATCH)
            (void) debug_evaluate_text (
                debug,
                g_strdup (((debug_watch_t *) g_ptr_array_index (debug->watches, row->index))
                              ->expression));
        else if (row->kind == PANEL_FRAME)
            debug_select_frame (debug, g_ptr_array_index (debug->frames, row->index));
        else if (row->kind == PANEL_REGISTERS)
            debug_registers_toggle (debug);
        else if (row->kind == PANEL_ADDRESS_BREAKPOINT)
            debug_disasm_show (debug, TRUE);
        else if (row->kind == PANEL_REGISTER)
            (void) debug_evaluate_text (
                debug,
                g_strconcat (
                    "$",
                    ((debug_variable_t *) g_ptr_array_index (debug->registers, row->index))->name,
                    (char *) NULL));
        else if (row->kind == PANEL_BREAKPOINT)
        {
            const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, row->index);

            (void) debug->host->show_location (debug->host, bp->file, bp->line);
        }
        else
            handled = FALSE;
        break;
    case KEY_DC:
        if (row != NULL && row->kind == PANEL_WATCH)
        {
            g_ptr_array_remove_index (debug->watches, row->index);
            debug_config_save (debug);
        }
        else if (row != NULL && row->kind == PANEL_BREAKPOINT)
        {
            debug_breakpoint_remove (debug, row->index);
            debug_marks_show (debug, NULL);
            debug_config_save (debug);
        }
        else if (row != NULL && row->kind == PANEL_ADDRESS_BREAKPOINT)
            debug_address_breakpoint_toggle (debug,
                                             ((debug_address_breakpoint_t *) g_ptr_array_index (
                                                  debug->address_breakpoints, row->index))
                                                 ->address);
        break;
    case ' ':
        if (row != NULL && row->kind == PANEL_BREAKPOINT)
            debug_breakpoint_toggle_enabled (debug, row->index);
        break;
    default:
        handled = FALSE;
        break;
    }
    g_ptr_array_free (rows, TRUE);
    if (handled && session->debug != NULL)
        widget_draw (WIDGET (session));
    return handled;
}

static void
debug_panel_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    debug_session_window_t *session = (debug_session_window_t *) w;
    const int index = session->top + event->y - 1;

    if (session->debug == NULL)
        return;
    switch (msg)
    {
    case MSG_MOUSE_DOWN:
    {
        GPtrArray *rows = debug_panel_rows (session->debug);

        widget_select (w);
        if (index >= 0 && index < (int) rows->len
            && debug_panel_selectable (g_ptr_array_index (rows, index)))
            session->cursor = index;
        g_ptr_array_free (rows, TRUE);
        widget_draw (w);
        break;
    }
    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE && index == session->cursor)
            (void) debug_panel_key (session, '\n');
        break;
    case MSG_MOUSE_SCROLL_UP:
        session->top = MAX (0, session->top - 3);
        widget_draw (w);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        session->top += 3;
        widget_draw (w);
        break;
    default:
        break;
    }
}

gboolean
debug_session_close_window (WEditWindow *win)
{
    WDialog *dialog = DIALOG (WIDGET (win)->owner);

    edit_window_give_room_back (win);
    edit_window_destroy (win);
    widget_draw (WIDGET (dialog));
    return TRUE;
}

static cb_ret_t
debug_session_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    debug_session_window_t *session = (debug_session_window_t *) w;

    switch (msg)
    {
    case MSG_DRAW:
        debug_session_draw (session);
        return MSG_HANDLED;
    case MSG_FOCUS:
        debug_session_buttonbar (session);
        debug_session_draw (session);
        return MSG_HANDLED;
    case MSG_UNFOCUS:
        debug_session_draw (session);
        return MSG_HANDLED;
    case MSG_KEY:
        if (session->debug == NULL)
            return MSG_NOT_HANDLED;
        if (debug_panel_key (session, parm))
            return MSG_HANDLED;
        if (debug_run_command (session->debug, debug_command_of_key (session->debug, parm), NULL))
            return MSG_HANDLED;
        // the F keys of the editor mean nothing here
        if (debug_command_of_key (session->debug, parm) == DEBUG_CMD_NONE && parm >= KEY_F (1)
            && parm <= KEY_F (10))
            return MSG_HANDLED;
        return MSG_NOT_HANDLED;
    case MSG_ACTION:
        if (session->debug != NULL
            && debug_run_command (session->debug, debug_command (session->debug, parm), NULL))
            return MSG_HANDLED;
        return MSG_NOT_HANDLED;
    case MSG_CURSOR:
        widget_gotoyx (w, 1 + session->cursor - session->top, 1);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (session->debug != NULL)
            session->debug->session_window = NULL;
        return group_default_callback (w, sender, msg, parm, data);
    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

static char *
debug_session_title (const WEditWindow *win)
{
    (void) win;
    return g_strdup (_ ("Debugger"));
}

static const edit_window_class_t debug_session_class = {
    .callback = debug_session_callback,
    .mouse_callback = debug_panel_mouse,
    .get_title = debug_session_title,
    .close = debug_session_close_window,
    .min_lines = 6,
    .min_cols = 24,
};

void
debug_session_refresh (debugger_t *debug)
{
    Widget *current = WIDGET (debug->host->window_current (debug->host));

    if (debug->session_window != NULL)
        widget_draw (WIDGET (debug->session_window));
    if (current == NULL)
        return;
    if (current == WIDGET (debug->session_window))
        debug_session_buttonbar (debug->session_window);
    else if (current == WIDGET (debug->disasm_window))
        debug_window_buttonbar (debug, current);
    else if (edit_widget_is_editor (current))
        debug_editor_buttonbar (debug, current);
}

mc_ep_result_t
debug_session_show (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_session_window_t *session;
    WRect area, rect;

    (void) edit;
    if (debug->session_window != NULL)
    {
        debug->host->window_show (debug->host, debug->session_window);
        return MC_EPR_OK;
    }
    debug->host->window_area (debug->host, &area);
    rect = area;
    session = g_new0 (debug_session_window_t, 1);
    edit_window_init (&session->window, &rect, &debug_session_class);
    session->window.fullscreen = 0;
    session->debug = debug;
    debug->session_window = session;
    debug->host->window_add (debug->host, session);
    // the column at the right: all of it, or under the tree of the project
    debug->host->window_dock_right (debug->host, session, CLAMP (area.cols * 30 / 100, 30, 60));
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* The panel and the console in the menu Window */
mc_ep_window_state_t
debug_panel_state (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->session_window == NULL)
        return MC_EP_WINDOW_CLOSED;
    return debug->host->window_current (debug->host) == (void *) debug->session_window
        ? MC_EP_WINDOW_FOCUSED
        : MC_EP_WINDOW_OPEN;
}

void
debug_panel_show (void *data)
{
    (void) debug_session_show (data, NULL);
}

void
debug_panel_close (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->session_window != NULL)
        (void) debug->host->window_close (debug->host, debug->session_window);
}

void *
debug_panel_window (void *data)
{
    return ((debugger_t *) data)->session_window;
}
