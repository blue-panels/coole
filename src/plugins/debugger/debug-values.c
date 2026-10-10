/*
   The debugger plugin: the values of the program, the variables, the registers, the watches, the
   call stack, a value as a tree, and those in the source.

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
#ifdef ENABLE_MCTERM
#include <sys/ioctl.h>
#ifdef __linux__
#include <sys/timerfd.h>
#endif
#include <termios.h>
#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#endif

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

static void debug_reply_watch (void *ui, const debug_reply_t *reply, void *data);
static mc_ep_result_t debug_watch_add (debugger_t *debug, const char *expression);

/*** end of forward declarations */

/*** file scope functions *********************************************************************/

void
debug_watch_free (gpointer data)
{
    debug_watch_t *watch = (debug_watch_t *) data;

    g_free (watch->expression);
    g_free (watch->value);
    g_free (watch);
}

static void
debug_watches_show (debugger_t *debug)
{
    // the panel shows them
    debug_session_refresh (debug);
}

void
debug_watches_refresh (debugger_t *debug)
{
    guint i;

    if (debug->state != DEBUG_STOPPED)
        return;
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        g_free (watch->value);
        watch->value = g_strdup (_ ("<evaluating>"));
        watch->pending_token = debug->backend->ops->evaluate (debug->backend, watch->expression,
                                                              debug_reply_watch, NULL, NULL);
        if (watch->pending_token == 0)
        {
            g_free (watch->value);
            watch->value = g_strdup (_ ("<GDB unavailable>"));
        }
    }
    debug_watches_show (debug);
}

void
debug_watches_clear_values (debugger_t *debug)
{
    guint i;

    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        watch->pending_token = 0;
        g_clear_pointer (&watch->value, g_free);
    }
    debug_watches_show (debug);
}

/* Whether @name stands in @line as a word of its own */
static gboolean
debug_line_names (const char *line, const char *name)
{
    const gsize len = strlen (name);
    const char *p;

    for (p = strstr (line, name); p != NULL; p = strstr (p + 1, name))
    {
        const gboolean before = p == line || !(g_ascii_isalnum (p[-1]) || p[-1] == '_');
        const gboolean after = !(g_ascii_isalnum (p[len]) || p[len] == '_');

        if (before && after)
            return TRUE;
    }
    return FALSE;
}

/* Whether a register stands in a line of assembler, by its name or by that of a part of it:
   rax as eax, ax, al; r8 as r8d, r8w, r8b.  What a comment says does not count. */
static gboolean
debug_line_names_register (const char *line, const char *reg)
{
    char *code = g_strndup (line, strcspn (line, "#;"));
    const gsize len = strlen (reg);
    gboolean named = debug_line_names (code, reg);

    // the registers of x86-64 that have parts of other names
    if (!named && len == 3 && reg[0] == 'r' && !g_ascii_isdigit (reg[1]))
    {
        const char e[] = { 'e', reg[1], reg[2], '\0' };
        const char w[] = { reg[1], reg[2], '\0' };
        const char low[] = { reg[1], reg[2] == 'x' ? 'l' : reg[2], reg[2] == 'x' ? '\0' : 'l',
                             '\0' };
        const char high[] = { reg[1], 'h', '\0' };

        named = debug_line_names (code, e) || debug_line_names (code, w)
            || debug_line_names (code, low) || (reg[2] == 'x' && debug_line_names (code, high));
    }
    else if (!named && reg[0] == 'r' && g_ascii_isdigit (reg[1]) && len <= 3)
    {
        const char *parts[] = { "d", "w", "b" };
        guint i;

        for (i = 0; i < G_N_ELEMENTS (parts) && !named; i++)
        {
            char *part = g_strconcat (reg, parts[i], (char *) NULL);

            named = debug_line_names (code, part);
            g_free (part);
        }
    }
    g_free (code);
    return named;
}

/* Whether a line of assembler is a label, where a run of instructions starts */
static gboolean
debug_line_is_label (const char *line)
{
    const char *p = line + strspn (line, " \t");

    if (!(g_ascii_isalpha (*p) || *p == '_' || *p == '.'))
        return FALSE;
    while (g_ascii_isalnum (*p) || *p == '_' || *p == '.' || *p == '$')
        p++;
    return *p == ':';
}

void
debug_notes_clear (debugger_t *debug)
{
    if (debug->host->clear_line_notes != NULL)
    {
        debug->host->clear_line_notes (debug->host, NULL);
        debug->host->redraw (debug->host);
    }
}

/* The values of the local variables after the lines of the function that name them, from its
   start down to the line the program stopped on */
static void
debug_notes_show (debugger_t *debug)
{
    // in assembler the registers stand for the variables
    const gboolean asm_source = debug_source_is_asm (debug->current_file);
    const GPtrArray *values = asm_source ? debug->registers : debug->locals;
    char *text = NULL;
    char **lines;
    long line, first;

    debug_notes_clear (debug);
    if (debug->host->set_line_note == NULL || debug->state != DEBUG_STOPPED
        || debug->current_file == NULL || values->len == 0
        || !g_file_get_contents (debug->current_file, &text, NULL, NULL))
    {
        g_free (text);
        return;
    }
    lines = g_strsplit (text, "\n", -1);
    g_free (text);
    if (debug->current_line > (long) g_strv_length (lines))
    {
        g_strfreev (lines);
        return;
    }

    /* up to the start of the function: a line that opens with a brace, a label in assembler,
       40 lines at most */
    for (first = debug->current_line; first > 1 && first > debug->current_line - 40; first--)
        if (asm_source ? debug_line_is_label (lines[first - 1]) : lines[first - 1][0] == '{')
            break;

    for (line = first; line <= debug->current_line; line++)
    {
        GString *note = g_string_new (NULL);
        guint i;

        for (i = 0; i < values->len; i++)
        {
            const debug_variable_t *local = g_ptr_array_index (values, i);

            if (asm_source ? !debug_line_names_register (lines[line - 1], local->name)
                           : !debug_line_names (lines[line - 1], local->name))
                continue;
            if (note->len > 0)
                g_string_append (note, ", ");
            g_string_append_printf (note, "%s = %.40s", local->name, local->value);
        }
        if (note->len > 0)
            debug->host->set_line_note (debug->host, debug->current_file, line, note->str);
        g_string_free (note, TRUE);
    }
    g_strfreev (lines);
    debug->host->redraw (debug->host);
}

/* The call stack, the frames taken from the reply */
void
debug_reply_stack (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    // the frames are the panel's now
    g_ptr_array_unref (debug->frames);
    debug->frames = reply->frames != NULL ? g_ptr_array_ref (reply->frames)
                                          : g_ptr_array_new_with_free_func (debug_frame_free);
    /* under _start of a program with no libc GDB has no frames to go on to, and makes some up:
       the stack ends at the first one with neither a function nor a source */
    for (i = 1; i < debug->frames->len; i++)
    {
        const debug_frame_t *frame = g_ptr_array_index (debug->frames, i);

        if (frame->file == NULL && frame->from == NULL
            && (frame->func == NULL || strcmp (frame->func, "??") == 0))
        {
            g_ptr_array_set_size (debug->frames, i);
            break;
        }
    }
    for (i = 0; i < debug->frames->len; i++)
    {
        debug_frame_t *frame = g_ptr_array_index (debug->frames, i);
        const char *func = frame->func != NULL ? frame->func : "?";

        if (frame->file != NULL)
            frame->label =
                g_strdup_printf ("#%ld %s  %s:%ld", frame->level, func, frame->file, frame->line);
        else
            frame->label = g_strdup_printf ("#%ld %s  %s", frame->level, func,
                                            frame->from != NULL ? frame->from : _ ("<no source>"));
    }
    debug_session_refresh (debug);
}

/* The local variables of the frame */
void
debug_reply_variables (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;

    (void) data;
    g_ptr_array_unref (debug->locals);
    debug->locals = reply->variables != NULL ? g_ptr_array_ref (reply->variables)
                                             : g_ptr_array_new_with_free_func (debug_variable_free);
    debug_notes_show (debug);
    debug_session_refresh (debug);
}

/* The registers: the values of the stop before are kept, to show which have changed */
static void
debug_reply_registers (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    if (debug->registers_before != NULL)
        g_hash_table_destroy (debug->registers_before);
    debug->registers_before = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    for (i = 0; i < debug->registers->len; i++)
    {
        const debug_variable_t *reg = g_ptr_array_index (debug->registers, i);

        g_hash_table_insert (debug->registers_before, g_strdup (reg->name), g_strdup (reg->value));
    }
    g_ptr_array_unref (debug->registers);
    debug->registers = reply->registers != NULL
        ? g_ptr_array_ref (reply->registers)
        : g_ptr_array_new_with_free_func (debug_variable_free);
    if (!reply->ok && reply->msg != NULL)
        debug_output_console (debug, reply->msg, TRUE);
    if (debug_source_is_asm (debug->current_file))
        debug_notes_show (debug);
    debug_session_refresh (debug);
}

/* The registers of the frame, when the panel shows them */
void
debug_registers_refresh (debugger_t *debug)
{
    if (debug->state != DEBUG_STOPPED || !debug->registers_shown || debug->backend == NULL
        || debug->backend->ops->registers == NULL)
        return;
    (void) debug->backend->ops->registers (debug->backend, debug_reply_registers, NULL, NULL);
}

/* The registers shown in the panel, or hidden: they are asked for only when shown */
void
debug_registers_toggle (debugger_t *debug)
{
    debug->registers_shown = !debug->registers_shown;
    debug_registers_refresh (debug);
    debug_session_refresh (debug);
}

/* The registers are of another program now: none changed */
void
debug_registers_clear (debugger_t *debug)
{
    g_ptr_array_set_size (debug->registers, 0);
    g_clear_pointer (&debug->registers_before, g_hash_table_destroy);
}

/* The value of a watch */
static void
debug_reply_watch (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const gboolean failed = !reply->ok;
    guint i;

    (void) data;
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);
        const char *value;

        if (watch->pending_token != reply->request)
            continue;
        watch->pending_token = 0;
        value = failed ? reply->msg : reply->value;
        g_free (watch->value);
        watch->value = g_strdup (value != NULL ? value : _ ("<unavailable>"));
        debug_watches_show (debug);
        debug_session_refresh (debug);
        return;
    }
}

/* The value of an expression asked for with Enter */
static void
debug_reply_evaluate (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const char *expression = (const char *) data;
    const gboolean failed = !reply->ok;
    const char *value = failed ? reply->msg : reply->value;
    char *text_value;

    debug->eval_pending = FALSE;
    text_value =
        g_strdup_printf ("%s = %s", expression, value != NULL ? value : _ ("<unavailable>"));
    if (failed)
        debug_error (debug, text_value);
    else if (query_dialog (_ ("Evaluate"), text_value, D_NORMAL, 2, _ ("&OK"), _ ("Add &watch"))
             == 1)
        (void) debug_watch_add (debug, expression);
    g_free (text_value);
}

typedef struct debug_node_t debug_node_t;

struct debug_node_t
{
    debug_variable_t *var;
    debug_node_t *parent;
    GPtrArray *members;  // debug_node_t, NULL until they are asked for
    gboolean open;
    gboolean asking;
    guint id;
    int depth;
};

/* The tree of the dialog open, NULL when none is */
typedef struct
{
    debugger_t *debug;
    debug_node_t *root;
    WListbox *list;
    GPtrArray *shown;  // debug_node_t in the order of the list
} debug_tree_t;

static debug_tree_t *debug_tree = NULL;

static guint debug_node_ids = 0;

static void
debug_node_free (gpointer p)
{
    debug_node_t *node = (debug_node_t *) p;

    debug_variable_free (node->var);
    if (node->members != NULL)
        g_ptr_array_free (node->members, TRUE);
    g_free (node);
}

static debug_node_t *
debug_node_new (const debug_variable_t *var, debug_node_t *parent)
{
    debug_node_t *node = g_new0 (debug_node_t, 1);

    node->var = g_new0 (debug_variable_t, 1);
    node->var->name = g_strdup (var->name);
    node->var->value = g_strdup (var->value);
    node->var->ref = g_strdup (var->ref);
    node->var->expression = g_strdup (var->expression);
    node->var->type = g_strdup (var->type);
    node->parent = parent;
    node->depth = parent != NULL ? parent->depth + 1 : 0;
    node->id = ++debug_node_ids;
    return node;
}

/* Whether an expression is a name, or ends as one: it takes a member without parentheses */
static gboolean
debug_expression_simple (const char *expression)
{
    const char *p;

    for (p = expression; *p != '\0'; p++)
        if (!(g_ascii_isalnum (*p) || *p == '_' || *p == '.' || *p == '[' || *p == ']'
              || (*p == '-' && p[1] == '>') || (*p == '>' && p > expression && p[-1] == '-')))
            return FALSE;
    return TRUE;
}

/* Whether a type of C is a pointer: "struct pt *", spaces after the star or not */
static gboolean
debug_type_is_pointer (const char *type)
{
    gsize len = strlen (type);

    while (len > 0 && g_ascii_isspace (type[len - 1]))
        len--;
    return len > 0 && type[len - 1] == '*';
}

/* The expression of a member, for a watch: the one the debugger gives, else made from that of
   the structure as C has it, s.x, p->x, a[3] */
static char *
debug_node_expression (const debug_node_t *node)
{
    char *parent, *expression;
    const char *type;

    if (node->var->expression != NULL)
        return g_strdup (node->var->expression);
    if (node->parent == NULL)
        return g_strdup (node->var->name);
    parent = debug_node_expression (node->parent);
    if (parent == NULL)
        return NULL;
    type = node->parent->var->type;
    if (node->var->name[0] != '\0'
        && strspn (node->var->name, "0123456789") == strlen (node->var->name))
        expression = g_strdup_printf (debug_expression_simple (parent) ? "%s[%s]" : "(%s)[%s]",
                                      parent, node->var->name);
    else if (type != NULL && debug_type_is_pointer (type))
        expression = g_strdup_printf (debug_expression_simple (parent) ? "%s->%s" : "(%s)->%s",
                                      parent, node->var->name);
    else
        expression = g_strdup_printf (debug_expression_simple (parent) ? "%s.%s" : "(%s).%s",
                                      parent, node->var->name);
    g_free (parent);
    return expression;
}

/* The nodes the list shows, from @node down */
static void
debug_tree_collect (GPtrArray *shown, debug_node_t *node)
{
    guint i;

    g_ptr_array_add (shown, node);
    if (!node->open || node->members == NULL)
        return;
    for (i = 0; i < node->members->len; i++)
        debug_tree_collect (shown, g_ptr_array_index (node->members, i));
}

/* The list made again from the tree, the cursor on the node it was on */
static void
debug_tree_fill (debug_tree_t *tree, const debug_node_t *current)
{
    guint i;

    g_ptr_array_set_size (tree->shown, 0);
    debug_tree_collect (tree->shown, tree->root);
    listbox_remove_list (tree->list);
    for (i = 0; i < tree->shown->len; i++)
    {
        const debug_node_t *node = g_ptr_array_index (tree->shown, i);
        const char *mark = node->var->ref == NULL ? " "
            : node->asking                        ? "~"
            : node->open                          ? "-"
                                                  : "+";
        char *text = g_strdup_printf ("%*s%s %s = %s", node->depth * 2, "", mark, node->var->name,
                                      node->var->value);

        listbox_add_item_take (tree->list, LISTBOX_APPEND_AT_END, 0, text, (void *) node, FALSE);
    }
    for (i = 0; i < tree->shown->len; i++)
        if (g_ptr_array_index (tree->shown, i) == current)
            listbox_set_current (tree->list, (int) i);
    widget_draw (WIDGET (tree->list));
}

static debug_node_t *
debug_tree_current (const debug_tree_t *tree)
{
    const int i = tree->list->current;

    return i >= 0 && i < (int) tree->shown->len ? g_ptr_array_index (tree->shown, i) : NULL;
}

static debug_node_t *
debug_node_find (debug_node_t *node, guint id)
{
    guint i;

    if (node->id == id)
        return node;
    for (i = 0; node->members != NULL && i < node->members->len; i++)
    {
        debug_node_t *found = debug_node_find (g_ptr_array_index (node->members, i), id);

        if (found != NULL)
            return found;
    }
    return NULL;
}

/* The members of a node have come: of a tree still open, or of none */
static void
debug_reply_members (void *ui, const debug_reply_t *reply, void *data)
{
    debug_node_t *node;
    guint i;

    (void) ui;
    if (debug_tree == NULL
        || (node = debug_node_find (debug_tree->root, GPOINTER_TO_UINT (data))) == NULL)
        return;
    node->asking = FALSE;
    node->members = g_ptr_array_new_with_free_func (debug_node_free);
    for (i = 0; reply->variables != NULL && i < reply->variables->len; i++)
        g_ptr_array_add (node->members,
                         debug_node_new (g_ptr_array_index (reply->variables, i), node));
    if (!reply->ok)
    {
        debug_variable_t why = { 0 };

        why.name = (char *) _ ("<error>");
        why.value = (char *) (reply->msg != NULL ? reply->msg : "");
        g_ptr_array_add (node->members, debug_node_new (&why, node));
    }
    debug_tree_fill (debug_tree, debug_tree_current (debug_tree));
}

/* A node opened, its members asked for the first time, or closed */
static void
debug_tree_toggle (debug_tree_t *tree, debug_node_t *node)
{
    if (node == NULL || node->var->ref == NULL)
        return;
    node->open = !node->open;
    if (node->open && node->members == NULL && !node->asking)
    {
        node->asking = tree->debug->backend->ops->children (tree->debug->backend, node->var->ref,
                                                            debug_reply_members,
                                                            GUINT_TO_POINTER (node->id), NULL)
            != 0;
        if (!node->asking)
            node->open = FALSE;
    }
    debug_tree_fill (tree, node);
}

static lcback_ret_t
debug_tree_activate (WListbox *l)
{
    (void) l;
    if (debug_tree != NULL)
        debug_tree_toggle (debug_tree, debug_tree_current (debug_tree));
    return LISTBOX_CONT;
}

static cb_ret_t
debug_tree_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_KEY && debug_tree != NULL && (parm == KEY_RIGHT || parm == KEY_LEFT))
    {
        debug_node_t *node = debug_tree_current (debug_tree);

        if (node == NULL)
            return MSG_HANDLED;
        if (parm == KEY_RIGHT && !node->open)
            debug_tree_toggle (debug_tree, node);
        else if (parm == KEY_LEFT && node->open)
            debug_tree_toggle (debug_tree, node);
        else if (parm == KEY_LEFT && node->parent != NULL)
            debug_tree_fill (debug_tree, node->parent);
        return MSG_HANDLED;
    }
    return dlg_default_callback (w, sender, msg, parm, data);
}

/* The value of an expression with members, a tree to open: Enter or Right opens a member, Left
   closes it; Add watch watches the member of the cursor */
static void
debug_tree_show (debugger_t *debug, const char *expression, const debug_variable_t *root)
{
    debug_tree_t tree = { 0 };
    const int dlg_w = MIN (COLS - 4, 90);
    const int list_h = MAX (5, MIN (LINES - 10, 20));
    const int dlg_h = list_h + 5;
    WDialog *dlg;
    debug_variable_t named = *root;
    debug_node_t *chosen;
    char *watch = NULL;

    named.name = (char *) expression;
    named.expression = root->expression != NULL ? root->expression : (char *) expression;
    tree.debug = debug;
    tree.root = debug_node_new (&named, NULL);
    tree.shown = g_ptr_array_new ();
    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, debug_tree_callback, NULL, "[Debugger]", _ ("Evaluate"));
    dlg->help_file = "debugger.md";
    tree.list = listbox_new (1, 1, list_h, dlg_w - 2, FALSE, debug_tree_activate);
    group_add_widget (GROUP (dlg), tree.list);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 - 14, B_USER, NORMAL_BUTTON, _ ("Add &watch"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 + 2, B_CANCEL, DEFPUSH_BUTTON, _ ("&Close"), NULL));
    debug_tree = &tree;
    // the first level open at once
    debug_tree_toggle (&tree, tree.root);

    if (dlg_run (dlg) == B_USER && (chosen = debug_tree_current (&tree)) != NULL)
        watch = debug_node_expression (chosen);
    debug_tree = NULL;
    widget_destroy (WIDGET (dlg));
    if (watch != NULL)
        (void) debug_watch_add (debug, watch);
    g_free (watch);
    // the variable object of GDB
    if (debug->backend != NULL && debug->backend->ops->release != NULL)
        debug->backend->ops->release (debug->backend, root->ref);
    debug_node_free (tree.root);
    g_ptr_array_free (tree.shown, TRUE);
}

/* The value of an expression in a dialog that adds it to the watches: a tree when it has
   members */
static void
debug_reply_inspect (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const char *expression = (const char *) data;
    const debug_variable_t *root =
        reply->ok && reply->variables != NULL && reply->variables->len > 0
        ? g_ptr_array_index (reply->variables, 0)
        : NULL;
    char *text_value;

    debug->eval_pending = FALSE;
    if (root != NULL && root->ref != NULL)
    {
        debug_tree_show (debug, expression, root);
        return;
    }
    text_value = g_strdup_printf ("%s = %s", expression,
                                  root != NULL             ? root->value
                                      : reply->msg != NULL ? reply->msg
                                                           : _ ("<unavailable>"));
    if (root == NULL)
        debug_error (debug, text_value);
    else if (query_dialog (_ ("Evaluate"), text_value, D_NORMAL, 2, _ ("&OK"), _ ("Add &watch"))
             == 1)
        (void) debug_watch_add (debug, expression);
    g_free (text_value);
}

/* The selection when it is on one line, else the word under the cursor */
static char *
debug_expression_at_cursor (debugger_t *debug, void *edit)
{
    WEdit *e = (WEdit *) edit;
    off_t start, end;

    if (e == NULL)
        return NULL;
    if (eval_marks (e, &start, &end) && end > start && end - start <= 256)
    {
        GString *text = g_string_sized_new ((gsize) (end - start));
        off_t i;

        for (i = start; i < end; i++)
        {
            const int c = edit_buffer_get_byte (&e->buffer, i);

            if (c == '\n')
                break;
            g_string_append_c (text, (char) c);
        }
        if (i == end)
        {
            g_strstrip (text->str);
            if (text->str[0] != '\0')
                return g_string_free (text, FALSE);
        }
        g_string_free (text, TRUE);
    }
    return debug->host->get_cursor_word (debug->host, edit);
}

/* The value of an expression, in a dialog that can add it to the watches; takes @expression */
mc_ep_result_t
debug_evaluate_text (debugger_t *debug, char *expression)
{
    gboolean sent;

    if (debug->state != DEBUG_STOPPED || debug->eval_pending)
    {
        g_free (expression);
        return MC_EPR_FAILED;
    }
    // looked into when the debugger can, its members a tree
    if (debug->backend->ops->inspect != NULL)
        sent = debug->backend->ops->inspect (debug->backend, expression, debug_reply_inspect,
                                             expression, g_free)
            != 0;
    else
        sent = debug->backend->ops->evaluate (debug->backend, expression, debug_reply_evaluate,
                                              expression, g_free)
            != 0;
    debug->eval_pending = sent;
    return sent ? MC_EPR_OK : MC_EPR_FAILED;
}

mc_ep_result_t
debug_evaluate (debugger_t *debug, void *edit)
{
    char *expression;

    if (debug->state != DEBUG_STOPPED || debug->eval_pending)
        return MC_EPR_FAILED;
    expression = debug_expression_at_cursor (debug, edit);
    if (expression == NULL)
    {
        expression =
            input_dialog (_ ("Evaluate"), _ ("Expression:"), NULL, "", INPUT_COMPLETE_NONE);
        if (expression == NULL)
            return MC_EPR_FAILED;
        g_strstrip (expression);
        if (*expression == '\0')
        {
            g_free (expression);
            return MC_EPR_FAILED;
        }
    }
    return debug_evaluate_text (debug, expression);
}

mc_ep_result_t
debug_show_stack (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    Listbox *selector;
    debug_frame_t *frame;
    guint i;

    (void) edit;
    if (debug->state != DEBUG_STOPPED || debug->frames->len == 0)
        return MC_EPR_FAILED;
    selector = listbox_window_new (MIN ((int) debug->frames->len, 16), 70, _ ("Call stack"), NULL);
    for (i = 0; i < debug->frames->len; i++)
    {
        frame = g_ptr_array_index (debug->frames, i);
        LISTBOX_APPEND_TEXT (selector, 0, frame->label, frame, FALSE);
    }
    frame = listbox_run_with_data (selector, NULL);
    if (frame == NULL)
        return MC_EPR_FAILED;
    debug_select_frame (debug, frame);
    return MC_EPR_OK;
}

/* A frame of the call stack chosen: its source, its variables, and the mark of the line */
void
debug_select_frame (debugger_t *debug, const debug_frame_t *frame)
{
    if (debug_source_here (frame->file) && frame->line > 0 && debug->host->show_location != NULL)
        (void) debug->host->show_location (debug->host, frame->file, frame->line);
    (void) debug->backend->ops->select_frame (debug->backend, frame->level);
    (void) debug->backend->ops->variables (debug->backend, debug_reply_variables, NULL, NULL);
    debug_registers_refresh (debug);
    g_free (debug->disasm_address);
    debug->disasm_address = g_strdup (frame->address);
    debug_disasm_refresh (debug);
    debug_watches_refresh (debug);
    // the mark goes with the frame: the steps go on from there
    if (debug_source_here (frame->file) && frame->line > 0)
    {
        debug_clear_current (debug);
        debug->current_file = g_strdup (frame->file);
        debug->current_line = frame->line;
        debug_breakpoints_sync (debug);
        debug_marks_show (debug, frame->file);
    }
    debug_session_refresh (debug);
}

static mc_ep_result_t
debug_watch_add (debugger_t *debug, const char *expression)
{
    debug_watch_t *watch;
    guint i;

    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *existing = g_ptr_array_index (debug->watches, i);

        if (g_strcmp0 (existing->expression, expression) == 0)
        {
            debug_error (debug, _ ("This expression is already watched."));
            return MC_EPR_FAILED;
        }
    }
    watch = g_new0 (debug_watch_t, 1);
    watch->expression = g_strdup (expression);
    g_ptr_array_add (debug->watches, watch);
    debug_config_save (debug);
    if (debug->state == DEBUG_STOPPED)
        debug_watches_refresh (debug);
    else
        debug_watches_show (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* An expression asked for, the selection or the word under the cursor of the file there to be
   taken as it is or changed; NULL when none is given */
static char *
debug_ask_expression (debugger_t *debug, void *edit, const char *title)
{
    void *file = edit != NULL ? edit : debug->host->window_top_file (debug->host);
    char *guess = file != NULL ? debug_expression_at_cursor (debug, file) : NULL;
    char *expression;

    expression = input_dialog (title, _ ("Expression:"), "debug-expression",
                               guess != NULL ? guess : "", INPUT_COMPLETE_NONE);
    g_free (guess);
    if (expression != NULL && *g_strstrip (expression) == '\0')
        g_clear_pointer (&expression, g_free);
    return expression;
}

/* Debug > Evaluate expression: its value, in the dialog that can add it to the watches */
mc_ep_result_t
debug_act_evaluate (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *expression;

    if (debug->state != DEBUG_STOPPED)
    {
        debug_error (debug, _ ("An expression has a value while the program is stopped."));
        return MC_EPR_FAILED;
    }
    expression = debug_ask_expression (debug, edit, _ ("Evaluate"));
    return expression != NULL ? debug_evaluate_text (debug, expression) : MC_EPR_FAILED;
}

mc_ep_result_t
debug_add_watch (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *expression;
    mc_ep_result_t result = MC_EPR_FAILED;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    expression = debug_ask_expression (debug, edit, _ ("Add watch"));
    if (expression == NULL)
        return MC_EPR_FAILED;
    g_strstrip (expression);
    if (*expression != '\0')
        result = debug_watch_add (debug, expression);
    g_free (expression);
    return result;
}

mc_ep_result_t
debug_remove_watch (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    Listbox *selector;
    debug_watch_t *chosen;
    guint i;

    (void) edit;
    if (debug->watches->len == 0)
        return MC_EPR_FAILED;
    selector =
        listbox_window_new (MIN ((int) debug->watches->len, 16), 70, _ ("Remove watch"), NULL);
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        LISTBOX_APPEND_TEXT (selector, 0, watch->expression, watch, FALSE);
    }
    chosen = listbox_run_with_data (selector, NULL);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    for (i = 0; i < debug->watches->len; i++)
        if (g_ptr_array_index (debug->watches, i) == chosen)
        {
            g_ptr_array_remove_index (debug->watches, i);
            debug_config_save (debug);
            debug_watches_show (debug);
            debug_session_refresh (debug);
            return MC_EPR_OK;
        }
    return MC_EPR_FAILED;
}

mc_ep_result_t
debug_show_watches (void *data, void *edit)
{
    // the watches are in the panel
    return debug_session_show (data, edit);
}
