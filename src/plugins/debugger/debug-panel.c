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
#include "src/editor/edittree.h"
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

/* What a node of the panel of the debugger is */
typedef enum
{
    PANEL_TEXT,   // a line to read
    PANEL_TITLE,  // a part: Locals, Watches, Call stack, Breakpoints
    PANEL_LOCAL,
    PANEL_WATCH,
    PANEL_MEMBER,  // a member of a variable, of a watch, of a member
    PANEL_FRAME,
    PANEL_BREAKPOINT,
    PANEL_REGISTERS,  // the part of the registers, read when it is opened
    PANEL_REGISTER,
    PANEL_ADDRESS_BREAKPOINT
} debug_panel_kind_t;

/* The data of a node of the panel */
typedef struct
{
    debug_panel_kind_t kind;
    guint index;       // of the variable, the watch, the frame, the breakpoint or the register
    char *expression;  // of a variable, a watch, a member: what gives it, NULL for none
    char *type;        // and its type, for the expressions of its members
    char *ref;         // of its members, that the debugger gives with it; NULL for none
    gboolean changed;  // a register the last steps have changed
} debug_panel_item_t;

/* The members of a node read, by the path of the node, till the program goes on: a node open,
   the panel made again, has them at once */
typedef struct
{
    char *ref;        // that they are of
    gboolean own;     // a reference of inspect (), the varobj of GDB, to release
    GPtrArray *vars;  // debug_variable_t; NULL while they are asked for
} debug_panel_members_t;

/* What an answer of the members is for: the node of the path, at the stop it was asked at */
typedef struct
{
    char *path;
    guint stop;
} debug_panel_asked_t;

#define DEBUG_PANEL_ITEM(n) ((debug_panel_item_t *) (n)->data)

static void
debug_panel_item_free (gpointer p)
{
    debug_panel_item_t *item = (debug_panel_item_t *) p;

    g_free (item->expression);
    g_free (item->type);
    g_free (item->ref);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

static void
debug_panel_members_free (gpointer p)
{
    debug_panel_members_t *m = (debug_panel_members_t *) p;

    g_free (m->ref);
    if (m->vars != NULL)
        g_ptr_array_free (m->vars, TRUE);
    g_free (m);
}

/* --------------------------------------------------------------------------------------------- */

static debug_panel_asked_t *
debug_panel_asked_new (const debugger_t *debug, const char *path)
{
    debug_panel_asked_t *asked = g_new0 (debug_panel_asked_t, 1);

    asked->path = g_strdup (path);
    asked->stop = debug->panel_stop;
    return asked;
}

static void
debug_panel_asked_free (gpointer p)
{
    debug_panel_asked_t *asked = (debug_panel_asked_t *) p;

    g_free (asked->path);
    g_free (asked);
}

/* The members an answer is for, NULL when it is of another stop or they are no more asked for */
static debug_panel_members_t *
debug_panel_members_of (const debugger_t *debug, const debug_panel_asked_t *asked)
{
    if (asked->stop != debug->panel_stop || debug->panel_members == NULL)
        return NULL;
    return g_hash_table_lookup (debug->panel_members, asked->path);
}

/* --------------------------------------------------------------------------------------------- */

static debug_variable_t *
debug_variable_copy (const debug_variable_t *var)
{
    debug_variable_t *copy = g_new0 (debug_variable_t, 1);

    copy->name = g_strdup (var->name);
    copy->value = g_strdup (var->value);
    copy->ref = g_strdup (var->ref);
    copy->expression = g_strdup (var->expression);
    copy->type = g_strdup (var->type);
    return copy;
}

/* --------------------------------------------------------------------------------------------- */

/* A node of @kind under @parent, its key @key and its value @value at the right */
static mctree_node_t *
debug_panel_add (mctree_model_t *model, mctree_node_t *parent, debug_panel_kind_t kind, guint index,
                 const char *key, const char *value)
{
    mctree_node_t *node = mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, value);
    debug_panel_item_t *item = g_new0 (debug_panel_item_t, 1);

    item->kind = kind;
    item->index = index;
    mctree_node_set_data (node, item, debug_panel_item_free);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* A part of the panel: open but when the user has closed it */
static mctree_node_t *
debug_panel_part (const debugger_t *debug, mctree_model_t *model, debug_panel_kind_t kind,
                  const char *name)
{
    mctree_node_t *node = debug_panel_add (model, model->root, kind, 0, name, NULL);
    char *path = mctree_node_path (node);

    node->expanded =
        debug->panel_closed == NULL || !g_hash_table_contains (debug->panel_closed, path);
    g_free (path);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* A variable, a watch or a member: it opens when its members may be read, open when it was */
static mctree_node_t *
debug_panel_value (const debugger_t *debug, mctree_model_t *model, mctree_node_t *parent,
                   debug_panel_kind_t kind, guint index, const char *name, const char *value,
                   const char *expression, const char *type, const char *ref, gboolean may_open)
{
    mctree_node_t *node = debug_panel_add (model, parent, kind, index, name, value);
    debug_panel_item_t *item = DEBUG_PANEL_ITEM (node);
    char *path;

    item->expression = g_strdup (expression);
    item->type = g_strdup (type);
    item->ref = g_strdup (ref);
    if (!may_open)
        return node;
    mctree_node_set_lazy (node, TRUE);
    path = mctree_node_path (node);
    if (debug->panel_open != NULL && g_hash_table_contains (debug->panel_open, path))
        mctree_node_expand (model, node);
    g_free (path);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a value of C looks like what has members: a structure, an array, a pointer */
static gboolean
debug_panel_may_open (const char *value, const char *type)
{
    gsize len;

    if (value != NULL && (value[0] == '{' || value[0] == '['))
        return TRUE;
    if (type == NULL)
        return FALSE;
    len = strlen (type);
    while (len > 0 && g_ascii_isspace (type[len - 1]))
        len--;
    return len > 0 && (type[len - 1] == '*' || type[len - 1] == ']');
}

/* --------------------------------------------------------------------------------------------- */

static void debug_panel_ask (debugger_t *debug, mctree_model_t *model, mctree_node_t *node);
static void debug_panel_reply_members (void *ui, const debug_reply_t *reply, void *data);
static void debug_panel_reply_inspect (void *ui, const debug_reply_t *reply, void *data);

/* A node opened that is read when it is: its members, the registers */
static void
debug_panel_children (mctree_model_t *model, mctree_node_t *node, void *user_data)
{
    debugger_t *debug = (debugger_t *) user_data;
    debug_panel_item_t *item = DEBUG_PANEL_ITEM (node);

    if (item == NULL)
        return;
    if (item->kind == PANEL_REGISTERS)
    {
        /* asked for, the panel made again with them when they come: not now, in the middle of
           the tree opening the node */
        mctree_node_set_lazy (node, FALSE);
        debug->registers_shown = TRUE;
        debug_registers_refresh (debug);
        return;
    }
    debug_panel_ask (debug, model, node);
}

/* --------------------------------------------------------------------------------------------- */

/* The members of @node as they are read, under it; asked for when they are not */
static void
debug_panel_ask (debugger_t *debug, mctree_model_t *model, mctree_node_t *node)
{
    debug_panel_item_t *item = DEBUG_PANEL_ITEM (node);
    char *path = mctree_node_path (node);
    debug_panel_members_t *m;
    guint i;

    if (debug->panel_members == NULL)
        debug->panel_members =
            g_hash_table_new_full (g_str_hash, g_str_equal, g_free, debug_panel_members_free);
    m = g_hash_table_lookup (debug->panel_members, path);
    if (m != NULL && m->vars != NULL)
    {
        mctree_node_set_lazy (node, FALSE);
        for (i = 0; i < m->vars->len; i++)
        {
            const debug_variable_t *var = g_ptr_array_index (m->vars, i);
            char *expression = var->expression != NULL ? g_strdup (var->expression)
                : item->expression != NULL
                ? debug_member_expression (item->expression, item->type, var->name)
                : NULL;

            (void) debug_panel_value (debug, model, node, PANEL_MEMBER, i, var->name, var->value,
                                      expression, var->type, var->ref, var->ref != NULL);
            g_free (expression);
        }
        g_free (path);
        return;
    }
    if (m != NULL || debug->backend == NULL || debug->state != DEBUG_STOPPED)
    {
        // asked for already, or nothing to ask now
        g_free (path);
        return;
    }

    m = g_new0 (debug_panel_members_t, 1);
    g_hash_table_insert (debug->panel_members, g_strdup (path), m);
    if (item->ref != NULL && debug->backend->ops->children != NULL)
    {
        m->ref = g_strdup (item->ref);
        (void) debug->backend->ops->children (debug->backend, item->ref, debug_panel_reply_members,
                                              debug_panel_asked_new (debug, path),
                                              debug_panel_asked_free);
    }
    // GDB gives the variables with no reference: a varobj of the expression gives one
    else if (item->expression != NULL && debug->backend->ops->inspect != NULL)
        (void) debug->backend->ops->inspect (
            debug->backend, item->expression, debug_panel_reply_inspect,
            debug_panel_asked_new (debug, path), debug_panel_asked_free);
    else
    {
        m->vars = g_ptr_array_new_with_free_func (debug_variable_free);
        mctree_node_set_lazy (node, FALSE);
    }
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

/* The members of the node of the path @data have come: the panel made again with them */
static void
debug_panel_reply_members (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    debug_panel_members_t *m = debug_panel_members_of (debug, (const debug_panel_asked_t *) data);
    guint i;

    // the program went on meanwhile, another frame was chosen: they are of before
    if (m == NULL || m->vars != NULL)
        return;
    m->vars = g_ptr_array_new_with_free_func (debug_variable_free);
    for (i = 0; reply->variables != NULL && i < reply->variables->len; i++)
        g_ptr_array_add (m->vars, debug_variable_copy (g_ptr_array_index (reply->variables, i)));
    if (!reply->ok)
    {
        debug_variable_t why = { 0 };

        why.name = (char *) _ ("<error>");
        why.value = (char *) (reply->msg != NULL ? reply->msg : "");
        g_ptr_array_add (m->vars, debug_variable_copy (&why));
    }
    debug_session_refresh (debug);
}

/* --------------------------------------------------------------------------------------------- */

/* The varobj of the expression of the node of the path @data: its members asked for */
static void
debug_panel_reply_inspect (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const debug_variable_t *root =
        reply->ok && reply->variables != NULL && reply->variables->len > 0
        ? g_ptr_array_index (reply->variables, 0)
        : NULL;
    const debug_panel_asked_t *asked = (const debug_panel_asked_t *) data;
    debug_panel_members_t *m = debug_panel_members_of (debug, asked);

    if (m == NULL || m->vars != NULL || m->ref != NULL)
    {
        // the program went on meanwhile: the varobj is of none
        if (root != NULL && root->ref != NULL && debug->backend != NULL
            && debug->backend->ops->release != NULL)
            debug->backend->ops->release (debug->backend, root->ref);
        return;
    }
    if (root == NULL || root->ref == NULL)
    {
        // it has no members
        m->vars = g_ptr_array_new_with_free_func (debug_variable_free);
        debug_session_refresh (debug);
        return;
    }
    m->ref = g_strdup (root->ref);
    m->own = TRUE;
    (void) debug->backend->ops->children (debug->backend, m->ref, debug_panel_reply_members,
                                          debug_panel_asked_new (debug, asked->path),
                                          debug_panel_asked_free);
}

/* --------------------------------------------------------------------------------------------- */

/* The panel made from what the debugger says now: the state, the variables of the frame, the
   watches, the call stack and the breakpoints; what was open open again */
static mctree_model_t *
debug_panel_model (debugger_t *debug)
{
    mctree_model_t *model = mctree_model_new (G_MAXSIZE);
    const debug_launch_t *launch = debug_active_launch (debug);
    mctree_node_t *part;
    char *text;
    guint i;

    (void) mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "", NULL);
    model->root->expanded = TRUE;
    model->children = debug_panel_children;
    model->children_data = debug;

    text = g_strdup_printf (
        "%s  %s", debug->start_after_build ? _ ("Building") : debug_state_name (debug->state),
        launch != NULL ? launch->name : _ ("<no configuration>"));
    (void) debug_panel_add (model, model->root, PANEL_TEXT, 0, text, NULL);
    g_free (text);
    text = NULL;
    if (debug->current_file != NULL)
        text = g_strdup_printf ("%s %s:%ld", debug->current_func != NULL ? debug->current_func : "",
                                x_basename (debug->current_file), debug->current_line);
    else if (debug->state == DEBUG_STOPPED && debug->current_func != NULL)
        text = g_strdup_printf (_ ("%s %s, with no source"), debug->current_func,
                                debug->current_address != NULL ? debug->current_address : "");
    if (text != NULL)
        (void) debug_panel_add (model, model->root, PANEL_TEXT, 0, text, NULL);
    g_free (text);

    // how long the step or the continue took, a slow line to be seen at once
    if (debug->state == DEBUG_STOPPED && debug->run_time > 0)
    {
        const double seconds = (double) debug->run_time / G_USEC_PER_SEC;

        text = seconds < 10.0 ? g_strdup_printf (_ ("ran %.3f s"), seconds)
                              : g_strdup_printf (_ ("ran %.1f s"), seconds);
        (void) debug_panel_add (model, model->root, PANEL_TEXT, 0, text, NULL);
        g_free (text);
    }

    /* a program that runs does not stop by itself to read: the user is to type its answer in its
       terminal, which nothing would tell else */
    if (debug->state == DEBUG_RUNNING && debug->program_terminal)
    {
        (void) debug_panel_add (model, model->root, PANEL_TEXT, 0,
                                _ ("output and input: tab Program"), NULL);
        (void) debug_panel_add (model, model->root, PANEL_TEXT, 0,
                                _ ("(a click on it; Alt-F5 back)"), NULL);
    }

    // what to do next, while nothing runs
    if ((debug->state == DEBUG_OFF || debug->state == DEBUG_FINISHED) && !debug->start_after_build)
    {
        part = debug_panel_part (debug, model, PANEL_TITLE, _ ("Next"));
        if (debug->breakpoints->len == 0)
            (void) debug_panel_add (model, part, PANEL_TEXT, 0,
                                    debug->debug_mode ? _ ("F6 on a line: breakpoint")
                                                      : _ ("Ctrl-F8 on a line: breakpoint"),
                                    NULL);
        (void) debug_panel_add (model, part, PANEL_TEXT, 0,
                                debug->debug_mode ? _ ("F5: build and run")
                                                  : _ ("F5 here, Shift-F9 anywhere: run"),
                                NULL);
        (void) debug_panel_add (model, part, PANEL_TEXT, 0, _ ("Alt-F5: here and back"), NULL);
        if (debug_active_launch (debug) == NULL)
            (void) debug_panel_add (model, part, PANEL_TEXT, 0, _ ("the first F5 asks what to run"),
                                    NULL);
    }

    if (debug->state == DEBUG_STOPPED)
    {
        part = debug_panel_part (debug, model, PANEL_TITLE, _ ("Locals"));
        for (i = 0; i < debug->locals->len; i++)
        {
            const debug_variable_t *local = g_ptr_array_index (debug->locals, i);

            (void) debug_panel_value (
                debug, model, part, PANEL_LOCAL, i, local->name, local->value,
                local->expression != NULL ? local->expression : local->name, local->type,
                local->ref, local->ref != NULL || debug_panel_may_open (local->value, local->type));
        }
        // a program of Python has none; read when the part is opened
        if (debug->backend == NULL || debug->backend->ops->has_registers == NULL
            || debug->backend->ops->has_registers (debug->backend))
        {
            char *path;

            part = debug_panel_add (model, model->root, PANEL_REGISTERS, 0, _ ("Registers"), NULL);
            path = mctree_node_path (part);
            part->expanded = debug->registers_shown
                && (debug->panel_closed == NULL
                    || !g_hash_table_contains (debug->panel_closed, path));
            g_free (path);
            mctree_node_set_lazy (part, !debug->registers_shown);
            for (i = 0; debug->registers_shown && i < debug->registers->len; i++)
            {
                const debug_variable_t *reg = g_ptr_array_index (debug->registers, i);
                const char *before = debug->registers_before != NULL
                    ? g_hash_table_lookup (debug->registers_before, reg->name)
                    : NULL;
                mctree_node_t *node =
                    debug_panel_add (model, part, PANEL_REGISTER, i, reg->name, reg->value);

                DEBUG_PANEL_ITEM (node)->changed =
                    before != NULL && strcmp (before, reg->value) != 0;
            }
        }
    }

    part = debug_panel_part (debug, model, PANEL_TITLE, _ ("Watches"));
    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        /* its members are looked for by the debugger, while the program stops; open, it stays so
           while it runs */
        (void) debug_panel_value (debug, model, part, PANEL_WATCH, i, watch->expression,
                                  watch->value != NULL ? watch->value : "?", watch->expression,
                                  NULL, NULL, TRUE);
    }
    if (debug->watches->len == 0)
        (void) debug_panel_add (model, part, PANEL_TEXT, 0, _ ("Ins adds one"), NULL);

    if (debug->state == DEBUG_STOPPED)
    {
        part = debug_panel_part (debug, model, PANEL_TITLE, _ ("Call stack"));
        for (i = 0; i < debug->frames->len; i++)
        {
            const debug_frame_t *frame = g_ptr_array_index (debug->frames, i);
            const char *base = frame->file != NULL ? x_basename (frame->file) : NULL;
            const char *label = strchr (frame->label, ' ');

            // "#0 func  file:line" with the file without its directories
            if (base != NULL)
            {
                text = g_strdup_printf ("#%ld %.*s %s:%ld", frame->level,
                                        label != NULL ? (int) strcspn (label + 1, " ") : 0,
                                        label != NULL ? label + 1 : "", base, frame->line);
                (void) debug_panel_add (model, part, PANEL_FRAME, i, text, NULL);
                g_free (text);
            }
            else
                (void) debug_panel_add (model, part, PANEL_FRAME, i, frame->label, NULL);
        }
    }

    part = debug_panel_part (debug, model, PANEL_TITLE, _ ("Breakpoints"));
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
            text = g_strdup_printf (
                "%s %s:%ld%s%s%s%s", debug->glyphs[debug_breakpoint_mark (debug, bp)],
                x_basename (bp->file), bp->line, function != NULL ? "  " : "",
                function != NULL ? function : "", bp->condition != NULL ? "  if " : "",
                bp->condition != NULL ? bp->condition : "");
            (void) debug_panel_add (model, part, PANEL_BREAKPOINT, i, text, NULL);
            g_free (text);
        }
        g_hash_table_destroy (functions);
    }
    for (i = 0; i < debug->address_breakpoints->len; i++)
    {
        const debug_address_breakpoint_t *bp = g_ptr_array_index (debug->address_breakpoints, i);

        text = g_strdup_printf ("%s *%s",
                                debug->glyphs[bp->gdb_number == NULL && debug_session_live (debug)
                                                  ? DEBUG_MARK_PENDING
                                                  : DEBUG_MARK_BREAKPOINT],
                                bp->address);
        (void) debug_panel_add (model, part, PANEL_ADDRESS_BREAKPOINT, i, text, NULL);
        g_free (text);
    }
    if (debug->breakpoints->len == 0 && debug->address_breakpoints->len == 0)
        (void) debug_panel_add (model, part, PANEL_TEXT, 0,
                                debug->debug_mode ? _ ("F6 on a line puts one")
                                                  : _ ("Ctrl-B on a line puts one"),
                                NULL);
    return model;
}

/* --------------------------------------------------------------------------------------------- */

/* The parts the user has closed, kept by their paths: those of @model, and those not in it */
static GHashTable *
debug_panel_closed (const debugger_t *debug, const mctree_model_t *model)
{
    GHashTable *closed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    guint i;

    for (i = 0; model != NULL && i < model->nodes->len; i++)
    {
        const mctree_node_t *node = g_ptr_array_index (model->nodes, i);
        const debug_panel_item_t *item = DEBUG_PANEL_ITEM (node);

        if (item == NULL || (item->kind != PANEL_TITLE && item->kind != PANEL_REGISTERS))
            continue;
        if (!node->expanded)
            g_hash_table_add (closed, mctree_node_path (node));
        g_hash_table_add (seen, mctree_node_path (node));
    }
    if (debug->panel_closed != NULL)
    {
        GHashTableIter iter;
        gpointer path;

        g_hash_table_iter_init (&iter, debug->panel_closed);
        while (g_hash_table_iter_next (&iter, &path, NULL))
            if (!g_hash_table_contains (seen, path))
                g_hash_table_add (closed, g_strdup ((const char *) path));
    }
    g_hash_table_destroy (seen);
    return closed;
}

/* --------------------------------------------------------------------------------------------- */

/* The panel made again from what the debugger says now: what was open and closed stays so, the
   cursor on its node */
static void
debug_panel_rebuild (debugger_t *debug)
{
    edit_tree_t *tree = debug->session_window;
    mctree_model_t *was = debug->panel_model;
    const mctree_node_t *current = tree != NULL ? edit_tree_current (tree) : NULL;
    char *cursor = current != NULL ? mctree_node_path (current) : NULL;
    GHashTable *open, *closed;
    int top;

    if (tree == NULL)
    {
        g_free (cursor);
        return;
    }
    open = was != NULL ? mctree_model_kept_paths (was, debug->panel_open) : NULL;
    closed = was != NULL ? debug_panel_closed (debug, was) : NULL;
    if (debug->panel_open != NULL)
        g_hash_table_destroy (debug->panel_open);
    if (debug->panel_closed != NULL)
        g_hash_table_destroy (debug->panel_closed);
    debug->panel_open = open;
    debug->panel_closed = closed;

    debug->panel_model = debug_panel_model (debug);
    // the rows where they were, the cursor on its node, then drawn
    top = tree->view->top;
    mctree_view_set_model (tree->view, debug->panel_model);
    tree->view->top = top;
    if (cursor != NULL)
        (void) mctree_view_goto_path (tree->view, cursor);
    widget_draw (WIDGET (tree));
    mctree_model_free (was);
    g_free (cursor);
}

/* --------------------------------------------------------------------------------------------- */

/* The row of a node: its name, its value at the right */
static void
debug_panel_row (edit_tree_t *tree, const mctree_node_t *node, GString *name, GString *info)
{
    (void) tree;
    g_string_append (name, node->key != NULL ? node->key : "");
    if (node->value != NULL)
        g_string_append (info, node->value);
}

/* --------------------------------------------------------------------------------------------- */

/* The parts stand out, and the registers the last steps have changed */
static gboolean
debug_panel_bold (edit_tree_t *tree, const mctree_node_t *node)
{
    const debug_panel_item_t *item = DEBUG_PANEL_ITEM (node);

    (void) tree;
    return item != NULL
        && (item->kind == PANEL_TITLE || item->kind == PANEL_REGISTERS || item->changed);
}

/* --------------------------------------------------------------------------------------------- */

/* Enter, a double click: a frame chosen, a breakpoint shown, a value of no members evaluated;
   a node with members opens or closes */
static gboolean
debug_panel_activate (edit_tree_t *tree, mctree_node_t *node)
{
    debugger_t *debug = (debugger_t *) tree->data;
    const debug_panel_item_t *item = DEBUG_PANEL_ITEM (node);

    if (debug == NULL || item == NULL)
        return FALSE;
    switch (item->kind)
    {
    case PANEL_LOCAL:
    case PANEL_WATCH:
    case PANEL_MEMBER:
        if (mctree_node_expandable (node) || item->expression == NULL)
            return FALSE;
        (void) debug_evaluate_text (debug, g_strdup (item->expression));
        return TRUE;
    case PANEL_FRAME:
        debug_select_frame (debug, g_ptr_array_index (debug->frames, item->index));
        return TRUE;
    case PANEL_REGISTERS:
        // the part opened and the registers asked for, or closed and no more asked for
        node->expanded = !node->expanded;
        mctree_node_set_lazy (node, FALSE);
        debug->registers_shown = node->expanded;
        debug_registers_refresh (debug);
        debug_session_refresh (debug);
        return TRUE;
    case PANEL_REGISTER:
        (void) debug_evaluate_text (
            debug,
            g_strconcat (
                "$", ((debug_variable_t *) g_ptr_array_index (debug->registers, item->index))->name,
                (char *) NULL));
        return TRUE;
    case PANEL_BREAKPOINT:
    {
        const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, item->index);

        (void) debug->host->show_location (debug->host, bp->file, bp->line);
        return TRUE;
    }
    case PANEL_ADDRESS_BREAKPOINT:
        debug_disasm_show (debug, TRUE);
        return TRUE;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Del, Ins, Space, ":", the condition of a breakpoint and the keys of the debugger on @node */
static gboolean
debug_panel_key (edit_tree_t *tree, mctree_node_t *node, int key)
{
    debugger_t *debug = (debugger_t *) tree->data;
    const debug_panel_item_t *item = node != NULL ? DEBUG_PANEL_ITEM (node) : NULL;
    const debug_panel_kind_t kind = item != NULL ? item->kind : PANEL_TEXT;

    if (debug == NULL)
        return FALSE;

    /* the condition of the breakpoint of the row, Alt-F6 as in a file; on another row nothing,
       not that of the line of a file out of sight */
    if (debug_command_of_key (debug, key) == DEBUG_CMD_CONDITION)
    {
        if (kind == PANEL_BREAKPOINT)
            debug_breakpoint_condition (debug, g_ptr_array_index (debug->breakpoints, item->index));
        else
            tty_beep ();
        debug_session_refresh (debug);
        return TRUE;
    }

    switch (key)
    {
    case KEY_IC:
        // a variable or a member watched as it is, else the expression asked for
        if ((kind == PANEL_LOCAL || kind == PANEL_MEMBER) && item->expression != NULL)
            (void) debug_watch_add (debug, item->expression);
        else
            (void) debug_add_watch (debug, NULL);
        return TRUE;
    case ':':
        debug_gdb_command (debug);
        return TRUE;
    case KEY_DC:
        if (kind == PANEL_WATCH)
        {
            g_ptr_array_remove_index (debug->watches, item->index);
            debug_config_save (debug);
        }
        else if (kind == PANEL_BREAKPOINT)
        {
            debug_breakpoint_remove (debug, item->index);
            debug_marks_show (debug, NULL);
            debug_config_save (debug);
        }
        else if (kind == PANEL_ADDRESS_BREAKPOINT)
            debug_address_breakpoint_toggle (debug,
                                             ((debug_address_breakpoint_t *) g_ptr_array_index (
                                                  debug->address_breakpoints, item->index))
                                                 ->address);
        debug_session_refresh (debug);
        return TRUE;
    case ' ':
        if (kind == PANEL_BREAKPOINT)
            debug_breakpoint_toggle_enabled (debug, item->index);
        debug_session_refresh (debug);
        return TRUE;
    default:
        break;
    }
    if (debug_run_command (debug, debug_command_of_key (debug, key), NULL))
        return TRUE;
    // the F keys of the editor mean nothing here
    return debug_command_of_key (debug, key) == DEBUG_CMD_NONE && key >= KEY_F (1)
        && key <= KEY_F (10);
}

/* --------------------------------------------------------------------------------------------- */

/* A command of the button bar of the panel */
static gboolean
debug_panel_action (edit_tree_t *tree, mctree_node_t *node, long command)
{
    debugger_t *debug = (debugger_t *) tree->data;

    (void) node;
    return debug != NULL && debug_run_command (debug, debug_command (debug, command), NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* The button bar of the panel: the commands go to the panel */
static void
debug_panel_buttonbar (edit_tree_t *tree, WButtonBar *bb)
{
    (void) bb;
    if (tree->data != NULL)
        debug_window_buttonbar ((debugger_t *) tree->data, WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

static void
debug_panel_closed_window (edit_tree_t *tree)
{
    debugger_t *debug = (debugger_t *) tree->data;

    if (debug == NULL)
        return;
    debug->session_window = NULL;
    g_clear_pointer (&debug->panel_model, mctree_model_free);
}

/* --------------------------------------------------------------------------------------------- */

static const edit_tree_ops_t debug_panel_ops = {
    .row = debug_panel_row,
    .activate = debug_panel_activate,
    .bold = debug_panel_bold,
    .key = debug_panel_key,
    .action = debug_panel_action,
    .buttonbar = debug_panel_buttonbar,
    .closed = debug_panel_closed_window,
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
debug_session_close_window (WEditWindow *win)
{
    WDialog *dialog = DIALOG (WIDGET (win)->owner);

    edit_window_give_room_back (win);
    edit_window_destroy (win);
    widget_draw (WIDGET (dialog));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
debug_panel_forget_members (debugger_t *debug)
{
    GHashTableIter iter;
    gpointer value;

    // the answers still to come are of before
    debug->panel_stop++;
    if (debug->panel_members == NULL)
        return;
    // the varobjs of GDB made for them go with them
    g_hash_table_iter_init (&iter, debug->panel_members);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        const debug_panel_members_t *m = (const debug_panel_members_t *) value;

        if (m->own && m->ref != NULL && debug->backend != NULL
            && debug->backend->ops->release != NULL)
            debug->backend->ops->release (debug->backend, m->ref);
    }
    g_clear_pointer (&debug->panel_members, g_hash_table_destroy);
}

/* --------------------------------------------------------------------------------------------- */

void
debug_panel_ended (debugger_t *debug)
{
    if (debug->session_window != NULL)
    {
        debug->session_window->data = NULL;
        edit_tree_set_message (debug->session_window, _ ("Debug session ended."));
    }
    g_clear_pointer (&debug->panel_members, g_hash_table_destroy);
    g_clear_pointer (&debug->panel_open, g_hash_table_destroy);
    g_clear_pointer (&debug->panel_closed, g_hash_table_destroy);
}

/* --------------------------------------------------------------------------------------------- */

void
debug_session_refresh (debugger_t *debug)
{
    Widget *current = WIDGET (debug->host->window_current (debug->host));

    if (debug->session_window != NULL)
        debug_panel_rebuild (debug);
    if (current == NULL)
        return;
    if (current == WIDGET (debug->session_window))
        debug_window_buttonbar (debug, current);
    else if (current == WIDGET (debug->disasm_window))
        debug_window_buttonbar (debug, current);
    else if (edit_widget_is_editor (current))
        debug_editor_buttonbar (debug, current);
}

/* --------------------------------------------------------------------------------------------- */

mc_ep_result_t
debug_session_show (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    WRect area;

    (void) edit;
    if (debug->session_window != NULL)
    {
        debug->host->window_show (debug->host, debug->session_window);
        return MC_EPR_OK;
    }
    debug->host->window_area (debug->host, &area);
    debug->session_window = edit_tree_new (_ ("Debugger"), &debug_panel_ops, debug);
    debug->host->window_add (debug->host, debug->session_window);
    // the column at the right: all of it, or under the tree of the project
    debug->host->window_dock_right (debug->host, debug->session_window,
                                    CLAMP (area.cols * 30 / 100, 30, 60));
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

void
debug_panel_show (void *data)
{
    (void) debug_session_show (data, NULL);
}

/* --------------------------------------------------------------------------------------------- */

void
debug_panel_close (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->session_window != NULL)
        (void) debug->host->window_close (debug->host, debug->session_window);
}

/* --------------------------------------------------------------------------------------------- */

void *
debug_panel_window (void *data)
{
    return ((debugger_t *) data)->session_window;
}
