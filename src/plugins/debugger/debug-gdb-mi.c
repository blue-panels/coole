/*
   The debugger plugin: GDB with its machine interface as a backend.

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

#include <stdlib.h>
#include <string.h>

#include "lib/global.h"

#include "debug-backend.h"
#include "gdb-mi.h"

/* How the results of a reply are read */
typedef enum
{
    MI_REPLY_PLAIN,
    MI_REPLY_STARTUP,
    MI_REPLY_BREAKPOINT,
    MI_REPLY_STACK,
    MI_REPLY_VARIABLES,
    MI_REPLY_VALUE
} mi_reply_kind_t;

typedef struct
{
    unsigned int token;
    mi_reply_kind_t kind;
    debug_reply_cb cb;
    void *data;
    GDestroyNotify free_data;
} mi_request_t;

typedef struct
{
    debug_backend_t base;
    gdb_mi_session_t *gdb;
    unsigned int next_token;
    // the replies GDB owes: mi_request_t
    GPtrArray *requests;
    // the commands that load the program, one after the other
    GQueue *startup;
} mi_backend_t;

#define MI(b)     ((mi_backend_t *) (b))
#define EVENTS(m) ((m)->base.events)
#define UI(m)     ((m)->base.ui)

static void mi_startup_next (mi_backend_t *mi);

/* --------------------------------------------------------------------------------------------- */

static void
mi_request_free (gpointer p)
{
    mi_request_t *request = (mi_request_t *) p;

    if (request->free_data != NULL)
        request->free_data (request->data);
    g_free (request);
}

/* --------------------------------------------------------------------------------------------- */

/* Send a command to GDB, its reply to go to @cb.  Gives the token of the command, 0 when it could
   not be sent; @data is freed either way. */
static unsigned int
mi_request (mi_backend_t *mi, mi_reply_kind_t kind, debug_reply_cb cb, void *data,
            GDestroyNotify free_data, const char *command)
{
    mi_request_t *request;
    char *line;
    gboolean sent;

    request = g_new0 (mi_request_t, 1);
    request->token = ++mi->next_token;
    request->kind = kind;
    request->cb = cb;
    request->data = data;
    request->free_data = free_data;
    line = g_strdup_printf ("%u%s", request->token, command);
    sent = gdb_mi_session_send (mi->gdb, line);
    g_free (line);
    if (!sent)
    {
        mi_request_free (request);
        EVENTS (mi)->error (UI (mi), _ ("Could not send a command to GDB."), FALSE);
        return 0;
    }
    g_ptr_array_add (mi->requests, request);
    return request->token;
}

/* --------------------------------------------------------------------------------------------- */

static unsigned int
mi_send (mi_backend_t *mi, const char *command)
{
    return mi_request (mi, MI_REPLY_PLAIN, NULL, NULL, NULL, command);
}

/* --------------------------------------------------------------------------------------------- */

/* A command with one argument quoted for GDB */
static char *
mi_command_quoted (const char *command, const char *argument)
{
    char *quoted = gdb_mi_quote (argument);
    char *line = g_strconcat (command, " ", quoted, NULL);

    g_free (quoted);
    return line;
}

/* --------------------------------------------------------------------------------------------- */

/* The call stack, from the reply to -stack-list-frames */
static GPtrArray *
mi_frames (const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *stack = gdb_mi_get (record->results, "stack");
    GPtrArray *frames = g_ptr_array_new_with_free_func (debug_frame_free);
    guint i;

    for (i = 0; stack != NULL && stack->items != NULL && i < stack->items->len; i++)
    {
        const gdb_mi_value_t *frame = g_ptr_array_index (stack->items, i);
        const char *level = gdb_mi_get_string (frame, "level");
        const char *line = gdb_mi_get_string (frame, "line");
        debug_frame_t *entry = g_new0 (debug_frame_t, 1);

        entry->level = level != NULL ? atol (level) : -1;
        entry->line = line != NULL ? atol (line) : 0;
        entry->func = g_strdup (gdb_mi_get_string (frame, "func"));
        entry->file = g_strdup (gdb_mi_get_string (frame, "fullname"));
        entry->from = g_strdup (gdb_mi_get_string (frame, "from"));
        entry->address = g_strdup (gdb_mi_get_string (frame, "addr"));
        g_ptr_array_add (frames, entry);
    }
    return frames;
}

/* --------------------------------------------------------------------------------------------- */

/* The local variables of the frame, from the reply to -stack-list-variables */
static GPtrArray *
mi_variables (const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *list = gdb_mi_get (record->results, "variables");
    GPtrArray *variables = g_ptr_array_new_with_free_func (debug_variable_free);
    guint i;

    for (i = 0; list != NULL && list->items != NULL && i < list->items->len; i++)
    {
        const gdb_mi_value_t *variable = g_ptr_array_index (list->items, i);
        const char *name = gdb_mi_get_string (variable, "name");
        const char *value = gdb_mi_get_string (variable, "value");
        const char *type = gdb_mi_get_string (variable, "type");
        debug_variable_t *entry;

        if (name == NULL)
            continue;
        entry = g_new0 (debug_variable_t, 1);
        entry->name = g_strdup (name);
        // --simple-values gives no value of a struct or an array: its type stands for it
        entry->value = value != NULL ? g_strdup (value)
            : type != NULL           ? g_strdup_printf ("{%s}", type)
                                     : g_strdup (_ ("<unavailable>"));
        g_ptr_array_add (variables, entry);
    }
    return variables;
}

/* --------------------------------------------------------------------------------------------- */

/* A command of the start is done: the next one goes */
static void
mi_startup_reply (mi_backend_t *mi, const gdb_mi_record_t *record)
{
    const char *msg;

    if (g_strcmp0 (record->klass, "done") == 0)
    {
        mi_startup_next (mi);
        return;
    }
    msg = gdb_mi_record_string (record, "msg");
    g_queue_clear_full (mi->startup, g_free);
    EVENTS (mi)->start_failed (UI (mi), msg != NULL ? msg : _ ("GDB rejected a startup command."));
}

/* --------------------------------------------------------------------------------------------- */

/* The reply to a request: taken off the list, then handled.  FALSE when nothing asked for it. */
static gboolean
mi_request_reply (mi_backend_t *mi, const gdb_mi_record_t *record)
{
    mi_request_t *request = NULL;
    debug_reply_t reply = { 0 };
    const gdb_mi_value_t *bkpt = NULL;
    guint i;

    if (!record->has_token)
        return FALSE;
    for (i = 0; i < mi->requests->len && request == NULL; i++)
        if (((mi_request_t *) g_ptr_array_index (mi->requests, i))->token == record->token)
            request = g_ptr_array_steal_index (mi->requests, i);
    if (request == NULL)
        return FALSE;

    if (request->kind == MI_REPLY_STARTUP)
    {
        mi_request_free (request);
        mi_startup_reply (mi, record);
        return TRUE;
    }

    reply.request = request->token;
    reply.ok = g_strcmp0 (record->klass, "error") != 0;
    reply.msg = gdb_mi_record_string (record, "msg");
    switch (request->kind)
    {
    case MI_REPLY_BREAKPOINT:
    {
        const char *line;

        bkpt = gdb_mi_get (record->results, "bkpt");
        reply.ok = g_strcmp0 (record->klass, "done") == 0 && bkpt != NULL;
        reply.id = gdb_mi_get_string (bkpt, "number");
        line = gdb_mi_get_string (bkpt, "line");
        reply.line = line != NULL ? atol (line) : 0;
        break;
    }
    case MI_REPLY_STACK:
        if (reply.ok)
            reply.frames = mi_frames (record);
        break;
    case MI_REPLY_VARIABLES:
        if (reply.ok)
            reply.variables = mi_variables (record);
        break;
    case MI_REPLY_VALUE:
        reply.value = gdb_mi_record_string (record, "value");
        break;
    default:
        break;
    }

    if (request->cb != NULL)
        request->cb (UI (mi), &reply, request->data);
    else if (!reply.ok)
        EVENTS (mi)->error (UI (mi), reply.msg != NULL ? reply.msg : _ ("GDB refused a command."),
                            FALSE);
    // the plugin keeps them with a reference of its own
    if (reply.frames != NULL)
        g_ptr_array_unref (reply.frames);
    if (reply.variables != NULL)
        g_ptr_array_unref (reply.variables);
    mi_request_free (request);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The program has stopped: at a breakpoint, after a step, on a signal, or for good */
static void
mi_stopped (mi_backend_t *mi, const gdb_mi_record_t *record)
{
    const char *reason = gdb_mi_record_string (record, "reason");
    const gdb_mi_value_t *frame = gdb_mi_get (record->results, "frame");
    const char *line = gdb_mi_get_string (frame, "line");
    debug_stop_t stop = { 0 };

    if (reason == NULL)
        stop.reason = DEBUG_STOP_OTHER;
    else if (strcmp (reason, "breakpoint-hit") == 0)
        stop.reason = DEBUG_STOP_BREAKPOINT;
    else if (strcmp (reason, "end-stepping-range") == 0)
        stop.reason = DEBUG_STOP_STEP;
    else if (strcmp (reason, "function-finished") == 0)
        stop.reason = DEBUG_STOP_FINISH;
    else if (strcmp (reason, "signal-received") == 0)
        stop.reason = DEBUG_STOP_SIGNAL;
    else if (g_str_has_prefix (reason, "exited"))
        stop.reason = DEBUG_STOP_EXITED;
    else
        stop.reason = DEBUG_STOP_OTHER;
    stop.has_frame = frame != NULL;
    stop.func = gdb_mi_get_string (frame, "func");
    stop.file = gdb_mi_get_string (frame, "fullname");
    stop.line = line != NULL ? atol (line) : 0;
    stop.address = gdb_mi_get_string (frame, "addr");
    stop.exit_code = gdb_mi_record_string (record, "exit-code");
    stop.signal_name = gdb_mi_record_string (record, "signal-name");
    stop.signal_meaning = gdb_mi_record_string (record, "signal-meaning");
    EVENTS (mi)->stopped (UI (mi), &stop);
}

/* --------------------------------------------------------------------------------------------- */

/* GDB has moved a breakpoint: one waiting for a library is now in it */
static void
mi_breakpoint_modified (mi_backend_t *mi, const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *bkpt = gdb_mi_get (record->results, "bkpt");
    const char *number = gdb_mi_get_string (bkpt, "number");
    const char *line = gdb_mi_get_string (bkpt, "line");

    if (number != NULL && line != NULL && atol (line) > 0)
        EVENTS (mi)->breakpoint_moved (UI (mi), number, atol (line));
}

/* --------------------------------------------------------------------------------------------- */

static void
mi_record (const char *line, void *data)
{
    mi_backend_t *mi = (mi_backend_t *) data;
    gdb_mi_record_t *record = gdb_mi_parse (line);

    switch (record->kind)
    {
    case GDB_MI_RECORD_RESULT:
        if (!mi_request_reply (mi, record) && g_strcmp0 (record->klass, "error") == 0)
        {
            const char *msg = gdb_mi_record_string (record, "msg");

            EVENTS (mi)->error (UI (mi), msg != NULL ? msg : line, TRUE);
        }
        break;
    case GDB_MI_RECORD_EXEC:
        if (g_strcmp0 (record->klass, "running") == 0)
            EVENTS (mi)->running (UI (mi));
        else if (g_strcmp0 (record->klass, "stopped") == 0)
            mi_stopped (mi, record);
        break;
    case GDB_MI_RECORD_NOTIFY:
        if (g_strcmp0 (record->klass, "gdb-exited") == 0)
        {
            g_ptr_array_set_size (mi->requests, 0);
            g_queue_clear_full (mi->startup, g_free);
            EVENTS (mi)->exited (UI (mi), TRUE);
        }
        else if (g_strcmp0 (record->klass, "thread-group-exited") == 0)
            EVENTS (mi)->exited (UI (mi), FALSE);
        else if (g_strcmp0 (record->klass, "breakpoint-modified") == 0)
            mi_breakpoint_modified (mi, record);
        break;
    case GDB_MI_RECORD_TARGET:
        EVENTS (mi)->output (UI (mi), record->text, DEBUG_OUTPUT_PROGRAM);
        break;
    case GDB_MI_RECORD_CONSOLE:
    case GDB_MI_RECORD_LOG:
        EVENTS (mi)->output (UI (mi), record->text, DEBUG_OUTPUT_CONSOLE);
        break;
    case GDB_MI_RECORD_OTHER:
        // what GDB writes to stderr
        EVENTS (mi)->output (UI (mi), record->text, DEBUG_OUTPUT_ERROR);
        break;
    default:
        break;
    }
    gdb_mi_record_free (record);
    EVENTS (mi)->flush (UI (mi));
}

/* --------------------------------------------------------------------------------------------- */

static void
mi_startup_next (mi_backend_t *mi)
{
    char *command = g_queue_pop_head (mi->startup);

    if (command == NULL)
    {
        EVENTS (mi)->ready (UI (mi));
        return;
    }
    if (mi_request (mi, MI_REPLY_STARTUP, NULL, NULL, NULL, command) == 0)
    {
        g_queue_clear_full (mi->startup, g_free);
        EVENTS (mi)->start_failed (UI (mi), NULL);
    }
    g_free (command);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mi_start (debug_backend_t *b, const debug_start_t *spec, GError **error)
{
    mi_backend_t *mi = MI (b);
    int i;

    if (gdb_mi_session_alive (mi->gdb))
        gdb_mi_session_stop (mi->gdb);
    g_ptr_array_set_size (mi->requests, 0);
    g_queue_clear_full (mi->startup, g_free);
    if (!gdb_mi_session_start (mi->gdb, spec->debugger, error))
        return FALSE;

    g_queue_push_tail (mi->startup, g_strdup ("-gdb-set mi-async on"));
    // the arguments go through sh, as they are split: quotes and spaces as typed
    g_queue_push_tail (mi->startup, g_strdup ("-gdb-set startup-with-shell on"));
    if (g_getenv ("SHELL") != NULL && strchr (g_getenv ("SHELL"), '\n') == NULL)
        g_queue_push_tail (mi->startup,
                           g_strconcat ("-gdb-set environment SHELL=", g_getenv ("SHELL"), NULL));
    g_queue_push_tail (mi->startup, mi_command_quoted ("-file-exec-and-symbols", spec->program));
    g_queue_push_tail (mi->startup, mi_command_quoted ("-environment-cd", spec->directory));
    for (i = 0; spec->environment != NULL && spec->environment[i] != NULL; i++)
        g_queue_push_tail (mi->startup,
                           g_strconcat ("-gdb-set environment ", spec->environment[i], NULL));
    if (spec->tty != NULL)
        g_queue_push_tail (mi->startup, mi_command_quoted ("-inferior-tty-set", spec->tty));
    if (spec->argv != NULL && spec->argv[0] != NULL)
    {
        /* -exec-arguments is "set args": the line as it is, which sh splits; MI quotes would
           reach the program */
        GString *args = g_string_new ("-exec-arguments");

        for (i = 0; spec->argv[i] != NULL; i++)
        {
            char *quoted = g_shell_quote (spec->argv[i]);

            g_string_append_c (args, ' ');
            g_string_append (args, quoted);
            g_free (quoted);
        }
        g_queue_push_tail (mi->startup, g_string_free (args, FALSE));
    }
    mi_startup_next (mi);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mi_stop (debug_backend_t *b)
{
    mi_backend_t *mi = MI (b);

    gdb_mi_session_stop (mi->gdb);
    g_queue_clear_full (mi->startup, g_free);
    g_ptr_array_set_size (mi->requests, 0);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mi_alive (const debug_backend_t *b)
{
    return gdb_mi_session_alive (MI (b)->gdb);
}

/* --------------------------------------------------------------------------------------------- */

static void
mi_cancel (debug_backend_t *b)
{
    g_ptr_array_set_size (MI (b)->requests, 0);
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_exec (debug_backend_t *b, debug_exec_t what, debug_reply_cb cb, void *data,
         GDestroyNotify free_data)
{
    static const char *const commands[] = {
        [DEBUG_EXEC_RUN] = "-exec-run",         [DEBUG_EXEC_CONTINUE] = "-exec-continue",
        [DEBUG_EXEC_PAUSE] = "-exec-interrupt", [DEBUG_EXEC_NEXT] = "-exec-next",
        [DEBUG_EXEC_STEP] = "-exec-step",       [DEBUG_EXEC_FINISH] = "-exec-finish",
    };

    return mi_request (MI (b), MI_REPLY_PLAIN, cb, data, free_data, commands[what]);
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_break_insert (debug_backend_t *b, const char *file, long line, gboolean disabled,
                 debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    char *location = g_strdup_printf ("%s:%ld", file, line);
    // -f: a breakpoint in a library not loaded yet waits for it
    char *command =
        mi_command_quoted (disabled ? "-break-insert -f -d" : "-break-insert -f", location);
    guint token;

    token = mi_request (MI (b), MI_REPLY_BREAKPOINT, cb, data, free_data, command);
    g_free (command);
    g_free (location);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_break_function (debug_backend_t *b, const char *func, gboolean temporary)
{
    char *command = mi_command_quoted (temporary ? "-break-insert -t" : "-break-insert -f", func);
    guint token;

    token = mi_send (MI (b), command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_break_delete (debug_backend_t *b, const char *id)
{
    char *command = g_strconcat ("-break-delete ", id, NULL);
    guint token;

    token = mi_send (MI (b), command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_break_enable (debug_backend_t *b, const char *id, gboolean enable)
{
    char *command = g_strconcat (enable ? "-break-enable " : "-break-disable ", id, NULL);
    guint token;

    token = mi_send (MI (b), command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_run_to (debug_backend_t *b, const char *file, long line)
{
    char *location = g_strdup_printf ("%s:%ld", file, line);
    char *command = mi_command_quoted ("-exec-until", location);
    guint token;

    token = mi_send (MI (b), command);
    g_free (command);
    g_free (location);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_select_frame (debug_backend_t *b, long level)
{
    char *command = g_strdup_printf ("-stack-select-frame %ld", level);
    guint token;

    token = mi_send (MI (b), command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_stack (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    return mi_request (MI (b), MI_REPLY_STACK, cb, data, free_data, "-stack-list-frames");
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_variables_request (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    return mi_request (MI (b), MI_REPLY_VARIABLES, cb, data, free_data,
                       "-stack-list-variables --simple-values");
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_evaluate (debug_backend_t *b, const char *expression, debug_reply_cb cb, void *data,
             GDestroyNotify free_data)
{
    char *command = mi_command_quoted ("-data-evaluate-expression", expression);
    guint token;

    token = mi_request (MI (b), MI_REPLY_VALUE, cb, data, free_data, command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

/* A command of GDB itself: its words come on the console stream */
static guint
mi_console (debug_backend_t *b, const char *line, debug_reply_cb cb, void *data,
            GDestroyNotify free_data)
{
    char *command = mi_command_quoted ("-interpreter-exec console", line);
    guint token;

    token = mi_request (MI (b), MI_REPLY_PLAIN, cb, data, free_data, command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static void
mi_free (debug_backend_t *b)
{
    mi_backend_t *mi = MI (b);

    gdb_mi_session_free (mi->gdb);
    g_ptr_array_free (mi->requests, TRUE);
    g_queue_free_full (mi->startup, g_free);
    g_free (mi);
}

/* --------------------------------------------------------------------------------------------- */

static const debug_backend_ops_t mi_ops = {
    .name = "gdb-mi",
    .start = mi_start,
    .stop = mi_stop,
    .alive = mi_alive,
    .cancel = mi_cancel,
    .exec = mi_exec,
    .break_insert = mi_break_insert,
    .break_function = mi_break_function,
    .break_delete = mi_break_delete,
    .break_enable = mi_break_enable,
    .run_to = mi_run_to,
    .select_frame = mi_select_frame,
    .stack = mi_stack,
    .variables = mi_variables_request,
    .evaluate = mi_evaluate,
    .console = mi_console,
    .free = mi_free,
};

/* --------------------------------------------------------------------------------------------- */

debug_backend_t *
debug_gdb_mi_new (const debug_backend_events_t *events, void *ui)
{
    mi_backend_t *mi = g_new0 (mi_backend_t, 1);

    mi->base.ops = &mi_ops;
    mi->base.events = events;
    mi->base.ui = ui;
    mi->gdb = gdb_mi_session_new (mi_record, mi);
    mi->requests = g_ptr_array_new_with_free_func (mi_request_free);
    mi->startup = g_queue_new ();
    return &mi->base;
}
