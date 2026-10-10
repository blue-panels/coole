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
    MI_REPLY_VALUE,
    MI_REPLY_REGISTER_NAMES,
    MI_REPLY_REGISTERS,
    MI_REPLY_DISASSEMBLE_FUNCTION,
    MI_REPLY_DISASSEMBLE,
    MI_REPLY_VAROBJ,
    MI_REPLY_CHILDREN
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
    // the names of the registers by their numbers, and the numbers of those the panel shows
    GPtrArray *register_names;
    GArray *general;
} mi_backend_t;

/* A request that waits for another first: the registers for their names, the instructions near
   an address for those of its function */
typedef struct
{
    debug_reply_cb cb;
    void *data;
    GDestroyNotify free_data;
    char *address;
} mi_pending_t;

#define MI(b)     ((mi_backend_t *) (b))
#define EVENTS(m) ((m)->base.events)
#define UI(m)     ((m)->base.ui)

static void mi_startup_next (mi_backend_t *mi);
static void mi_release (debug_backend_t *b, const char *ref);
static guint mi_register_values (mi_backend_t *mi, debug_reply_cb cb, void *data,
                                 GDestroyNotify free_data);

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

/* A variable object of GDB: its value, and its name as the reference of its members when it has
   some (a pretty-printer may give them while it says none) */
static debug_variable_t *
mi_varobj (const gdb_mi_value_t *tuple, const char *name)
{
    const char *numchild = gdb_mi_get_string (tuple, "numchild");
    const char *value = gdb_mi_get_string (tuple, "value");
    const gboolean members = (numchild != NULL && atol (numchild) > 0)
        || g_strcmp0 (gdb_mi_get_string (tuple, "dynamic"), "1") == 0
        || g_strcmp0 (gdb_mi_get_string (tuple, "has_more"), "1") == 0;
    debug_variable_t *variable = g_new0 (debug_variable_t, 1);

    variable->name = g_strdup (name);
    variable->value = g_strdup (value != NULL ? value : "{...}");
    variable->ref = members ? g_strdup (gdb_mi_get_string (tuple, "name")) : NULL;
    variable->type = g_strdup (gdb_mi_get_string (tuple, "type"));
    return variable;
}

/* --------------------------------------------------------------------------------------------- */

/* The members of a variable object, from the reply to -var-list-children */
static GPtrArray *
mi_children (const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *list = gdb_mi_get (record->results, "children");
    GPtrArray *children = g_ptr_array_new_with_free_func (debug_variable_free);
    guint i;

    for (i = 0; list != NULL && list->items != NULL && i < list->items->len; i++)
    {
        const gdb_mi_value_t *child = g_ptr_array_index (list->items, i);

        g_ptr_array_add (children, mi_varobj (child, gdb_mi_get_string (child, "exp")));
    }
    return children;
}

/* --------------------------------------------------------------------------------------------- */

/* The values of the registers, from the reply to -data-list-register-values */
static GPtrArray *
mi_registers (const mi_backend_t *mi, const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *list = gdb_mi_get (record->results, "register-values");
    GPtrArray *registers = g_ptr_array_new_with_free_func (debug_variable_free);
    guint i;

    for (i = 0; list != NULL && list->items != NULL && i < list->items->len; i++)
    {
        const gdb_mi_value_t *item = g_ptr_array_index (list->items, i);
        const char *number = gdb_mi_get_string (item, "number");
        const char *value = gdb_mi_get_string (item, "value");
        const long n = number != NULL ? atol (number) : -1;
        debug_variable_t *entry;

        if (n < 0 || (guint) n >= mi->register_names->len || value == NULL)
            continue;
        entry = g_new0 (debug_variable_t, 1);
        entry->name = g_strdup (g_ptr_array_index (mi->register_names, n));
        entry->value = g_strdup (value);
        g_ptr_array_add (registers, entry);
    }
    return registers;
}

/* --------------------------------------------------------------------------------------------- */

/* An instruction of the reply to -data-disassemble, of the source line @file:@line */
static debug_instruction_t *
mi_instruction (const gdb_mi_value_t *item, const char *file, long line)
{
    const char *address = gdb_mi_get_string (item, "address");
    const char *offset = gdb_mi_get_string (item, "offset");
    debug_instruction_t *instruction;

    if (address == NULL)
        return NULL;
    instruction = g_new0 (debug_instruction_t, 1);
    instruction->address = g_strdup (address);
    instruction->func = g_strdup (gdb_mi_get_string (item, "func-name"));
    instruction->offset = offset != NULL ? atol (offset) : 0;
    instruction->text = g_strdup (gdb_mi_get_string (item, "inst"));
    instruction->file = g_strdup (file);
    instruction->line = line;
    return instruction;
}

/* --------------------------------------------------------------------------------------------- */

/* The instructions, from the reply to -data-disassemble: with their source lines (mode 4) where
   there are some, by themselves where there are none */
static GPtrArray *
mi_instructions (const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *list = gdb_mi_get (record->results, "asm_insns");
    GPtrArray *instructions = g_ptr_array_new_with_free_func (debug_instruction_free);
    guint i, j;

    for (i = 0; list != NULL && list->items != NULL && i < list->items->len; i++)
    {
        const gdb_mi_value_t *item = g_ptr_array_index (list->items, i);
        debug_instruction_t *instruction;

        if (g_strcmp0 (item->name, "src_and_asm_line") == 0)
        {
            const gdb_mi_value_t *of_line = gdb_mi_get (item, "line_asm_insn");
            const char *file = gdb_mi_get_string (item, "fullname");
            const char *line = gdb_mi_get_string (item, "line");

            for (j = 0; of_line != NULL && of_line->items != NULL && j < of_line->items->len; j++)
            {
                instruction = mi_instruction (g_ptr_array_index (of_line->items, j), file,
                                              line != NULL ? atol (line) : 0);
                if (instruction != NULL)
                    g_ptr_array_add (instructions, instruction);
            }
        }
        else if ((instruction = mi_instruction (item, NULL, 0)) != NULL)
            g_ptr_array_add (instructions, instruction);
    }
    return instructions;
}

/* --------------------------------------------------------------------------------------------- */

/* The function around the address has been disassembled, or GDB knows of none there: then the
   instructions that follow the address */
static void
mi_disassemble_function_reply (mi_backend_t *mi, const gdb_mi_record_t *record,
                               mi_pending_t *pending)
{
    debug_reply_t reply = { 0 };
    char *command;

    if (g_strcmp0 (record->klass, "done") == 0)
    {
        reply.ok = TRUE;
        reply.instructions = mi_instructions (record);
        if (pending->cb != NULL)
            pending->cb (UI (mi), &reply, pending->data);
        g_ptr_array_unref (reply.instructions);
        return;
    }
    command = g_strdup_printf ("-data-disassemble -s %s -e %s+256 -- 4", pending->address,
                               pending->address);
    // the request is the other one's now
    (void) mi_request (mi, MI_REPLY_DISASSEMBLE, pending->cb, pending->data, pending->free_data,
                       command);
    pending->free_data = NULL;
    g_free (command);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a register is the first of those that are not general: the floating point and the
   vector ones, of x86, ARM, AArch64, RISC-V and PowerPC */
static gboolean
mi_register_ends_general (const char *name)
{
    static const char *const first[] = {
        "st0", "xmm0", "ymm0", "k0", "f0", "ft0", "d0", "s0", "v0", "q0", "vr0", "fpscr", "fpsr",
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (first); i++)
        if (strcmp (name, first[i]) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The names of the registers have come: the values are asked for */
static void
mi_register_names_reply (mi_backend_t *mi, const gdb_mi_record_t *record, mi_pending_t *pending)
{
    const gdb_mi_value_t *list = gdb_mi_get (record->results, "register-names");
    guint i;

    if (g_strcmp0 (record->klass, "done") != 0 || list == NULL || list->items == NULL)
    {
        debug_reply_t reply = { 0 };

        reply.msg = gdb_mi_record_string (record, "msg");
        if (pending->cb != NULL)
            pending->cb (UI (mi), &reply, pending->data);
        return;
    }
    g_ptr_array_set_size (mi->register_names, 0);
    g_array_set_size (mi->general, 0);
    for (i = 0; i < list->items->len; i++)
    {
        const gdb_mi_value_t *item = g_ptr_array_index (list->items, i);
        const char *name = item->string != NULL ? item->string : "";

        g_ptr_array_add (mi->register_names, g_strdup (name));
    }
    // the general ones come first; a name may be empty, for a number GDB does not use
    for (i = 0; i < mi->register_names->len && mi->general->len < 48; i++)
    {
        const char *name = g_ptr_array_index (mi->register_names, i);
        const int n = (int) i;

        if (mi_register_ends_general (name))
            break;
        if (*name != '\0')
            g_array_append_val (mi->general, n);
    }
    // the request is the values' now
    (void) mi_register_values (mi, pending->cb, pending->data, pending->free_data);
    pending->free_data = NULL;
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
    if (request->kind == MI_REPLY_REGISTER_NAMES)
    {
        mi_register_names_reply (mi, record, request->data);
        mi_request_free (request);
        return TRUE;
    }
    if (request->kind == MI_REPLY_DISASSEMBLE_FUNCTION)
    {
        mi_disassemble_function_reply (mi, record, request->data);
        mi_request_free (request);
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
    case MI_REPLY_REGISTERS:
        if (reply.ok)
            reply.registers = mi_registers (mi, record);
        break;
    case MI_REPLY_DISASSEMBLE:
        if (reply.ok)
            reply.instructions = mi_instructions (record);
        break;
    case MI_REPLY_VAROBJ:
        if (reply.ok)
        {
            debug_variable_t *root = mi_varobj (record->results, NULL);

            reply.variables = g_ptr_array_new_with_free_func (debug_variable_free);
            g_ptr_array_add (reply.variables, root);
            // with no members to look into, it is of no use any more
            if (root->ref == NULL)
                mi_release (&mi->base, gdb_mi_record_string (record, "name"));
        }
        break;
    case MI_REPLY_CHILDREN:
        if (reply.ok)
            reply.variables = mi_children (record);
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
    if (reply.registers != NULL)
        g_ptr_array_unref (reply.registers);
    if (reply.instructions != NULL)
        g_ptr_array_unref (reply.instructions);
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
    // another program, another machine perhaps
    g_ptr_array_set_size (mi->register_names, 0);
    g_array_set_size (mi->general, 0);
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
        [DEBUG_EXEC_RUN] = "-exec-run",
        [DEBUG_EXEC_CONTINUE] = "-exec-continue",
        [DEBUG_EXEC_PAUSE] = "-exec-interrupt",
        [DEBUG_EXEC_NEXT] = "-exec-next",
        [DEBUG_EXEC_STEP] = "-exec-step",
        [DEBUG_EXEC_FINISH] = "-exec-finish",
        [DEBUG_EXEC_STEP_INSTRUCTION] = "-exec-step-instruction",
        [DEBUG_EXEC_NEXT_INSTRUCTION] = "-exec-next-instruction",
    };

    return mi_request (MI (b), MI_REPLY_PLAIN, cb, data, free_data, commands[what]);
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_break_insert (debug_backend_t *b, const char *file, long line, gboolean disabled,
                 const char *condition, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    char *location = g_strdup_printf ("%s:%ld", file, line);
    // -f: a breakpoint in a library not loaded yet waits for it
    GString *command = g_string_new (disabled ? "-break-insert -f -d" : "-break-insert -f");
    char *quoted;
    guint token;

    if (condition != NULL && *condition != '\0')
    {
        quoted = gdb_mi_quote (condition);
        g_string_append_printf (command, " -c %s", quoted);
        g_free (quoted);
    }
    quoted = gdb_mi_quote (location);
    g_string_append_printf (command, " %s", quoted);
    g_free (quoted);
    token = mi_request (MI (b), MI_REPLY_BREAKPOINT, cb, data, free_data, command->str);
    g_string_free (command, TRUE);
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

/* The condition goes as it is: GDB takes the rest of the line for it, none taking it off */
static guint
mi_break_condition (debug_backend_t *b, const char *id, const char *condition)
{
    char *command =
        g_strconcat ("-break-condition ", id, condition != NULL && *condition != '\0' ? " " : "",
                     condition != NULL ? condition : "", NULL);
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

static void
mi_pending_free (gpointer p)
{
    mi_pending_t *pending = (mi_pending_t *) p;

    if (pending->free_data != NULL)
        pending->free_data (pending->data);
    g_free (pending->address);
    g_free (pending);
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_register_values (mi_backend_t *mi, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    GString *command = g_string_new ("-data-list-register-values --skip-unavailable x");
    guint i, token;

    for (i = 0; i < mi->general->len; i++)
        g_string_append_printf (command, " %d", g_array_index (mi->general, int, i));
    token = mi_request (mi, MI_REPLY_REGISTERS, cb, data, free_data, command->str);
    g_string_free (command, TRUE);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

/* The general registers: their names are asked for once, the values at every stop */
static guint
mi_registers_request (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    mi_backend_t *mi = MI (b);
    mi_pending_t *pending;

    if (mi->general->len > 0)
        return mi_register_values (mi, cb, data, free_data);
    pending = g_new0 (mi_pending_t, 1);
    pending->cb = cb;
    pending->data = data;
    pending->free_data = free_data;
    return mi_request (mi, MI_REPLY_REGISTER_NAMES, NULL, pending, mi_pending_free,
                       "-data-list-register-names");
}

/* --------------------------------------------------------------------------------------------- */

/* The instructions of the function around an address */
static guint
mi_disassemble (debug_backend_t *b, const char *address, debug_reply_cb cb, void *data,
                GDestroyNotify free_data)
{
    mi_pending_t *pending;
    char *command;
    guint token;

    // an address goes into the command as it is: only what an address is made of
    if (address == NULL || strspn (address, "0123456789abcdefABCDEFx$pc") != strlen (address))
    {
        if (free_data != NULL)
            free_data (data);
        return 0;
    }
    pending = g_new0 (mi_pending_t, 1);
    pending->cb = cb;
    pending->data = data;
    pending->free_data = free_data;
    pending->address = g_strdup (address);
    command = g_strdup_printf ("-data-disassemble -a %s -- 4", address);
    token =
        mi_request (MI (b), MI_REPLY_DISASSEMBLE_FUNCTION, NULL, pending, mi_pending_free, command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_break_address (debug_backend_t *b, const char *address, debug_reply_cb cb, void *data,
                  GDestroyNotify free_data)
{
    char *location = g_strconcat ("*", address, NULL);
    char *command = mi_command_quoted ("-break-insert", location);
    guint token;

    token = mi_request (MI (b), MI_REPLY_BREAKPOINT, cb, data, free_data, command);
    g_free (command);
    g_free (location);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

/* An expression looked into: a variable object of GDB, in the frame chosen */
static guint
mi_inspect (debug_backend_t *b, const char *expression, debug_reply_cb cb, void *data,
            GDestroyNotify free_data)
{
    char *command = mi_command_quoted ("-var-create - *", expression);
    guint token;

    token = mi_request (MI (b), MI_REPLY_VAROBJ, cb, data, free_data, command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mi_children_request (debug_backend_t *b, const char *ref, debug_reply_cb cb, void *data,
                     GDestroyNotify free_data)
{
    char *command = mi_command_quoted ("-var-list-children --all-values", ref);
    guint token;

    token = mi_request (MI (b), MI_REPLY_CHILDREN, cb, data, free_data, command);
    g_free (command);
    return token;
}

/* --------------------------------------------------------------------------------------------- */

/* An answer that matters to no one: a variable object gone with the GDB before, say */
static void
mi_ignore (void *ui, const debug_reply_t *reply, void *data)
{
    (void) ui;
    (void) reply;
    (void) data;
}

/* --------------------------------------------------------------------------------------------- */

/* The variable object of an expression, its members with it */
static void
mi_release (debug_backend_t *b, const char *ref)
{
    char *command;

    if (ref == NULL || !gdb_mi_session_alive (MI (b)->gdb))
        return;
    command = mi_command_quoted ("-var-delete", ref);
    (void) mi_request (MI (b), MI_REPLY_PLAIN, mi_ignore, NULL, NULL, command);
    g_free (command);
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
    g_ptr_array_free (mi->register_names, TRUE);
    g_array_free (mi->general, TRUE);
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
    .break_condition = mi_break_condition,
    .run_to = mi_run_to,
    .select_frame = mi_select_frame,
    .stack = mi_stack,
    .variables = mi_variables_request,
    .evaluate = mi_evaluate,
    .break_address = mi_break_address,
    .disassemble = mi_disassemble,
    .registers = mi_registers_request,
    .inspect = mi_inspect,
    .children = mi_children_request,
    .release = mi_release,
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
    mi->register_names = g_ptr_array_new_with_free_func (g_free);
    mi->general = g_array_new (FALSE, FALSE, sizeof (int));
    return &mi->base;
}
