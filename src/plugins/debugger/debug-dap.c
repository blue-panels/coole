/*
   The debugger plugin: a debug adapter of the Debug Adapter Protocol as a backend.

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

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "lib/global.h"

#include "debug-backend.h"
#include "dap.h"

/* The adapter of a breakpoint: a line, a function, an instruction */
typedef enum
{
    DAP_BP_LINE,
    DAP_BP_FUNCTION,
    DAP_BP_ADDRESS
} dap_bp_kind_t;

/* A breakpoint of the plugin as the adapter has it: the adapter takes the breakpoints of a file,
   the functions or the instructions all at once, so each change sends them all again */
typedef struct
{
    dap_bp_kind_t kind;
    char *id;  // the plugin's name of it
    char *file;
    long line;
    char *name;  // of the function, or the address of the instruction
    gboolean enabled;
    gboolean temporary;  // taken off at the next stop: run to the cursor, to a function
    gint64 dap_id;       // the adapter's, -1 when it has given none
    gboolean verified;
    // the plugin waits for this answer
    guint token;
    debug_reply_cb cb;
    void *data;
    GDestroyNotify free_data;
} dap_bp_t;

typedef struct
{
    gint64 id;
    char *name;
    char *file;
    long line;
    char *address;
} dap_frame_t;

typedef struct
{
    debug_backend_t base;
    dap_client_t *client;
    char **argv;  // of the adapter
    char *program;
    char **args;
    char *directory;
    char **environment;
    char *tty;
    char *launch_extra;
    JsonObject *capabilities;
    // the configuration goes before the launch: the adapter has said "initialized" first (GDB)
    gboolean configure_first;
    gboolean initialized;
    gboolean launched;
    gboolean configured;
    gint64 thread;
    GPtrArray *frames;  // dap_frame_t of the stop, the top first
    guint selected;     // the frame the plugin has chosen
    GPtrArray *breakpoints;
    guint next_token;
    guint next_bp;
    // the scopes of the last frame had registers: those of machine code do, those of Python not
    gboolean registers_scope;
    /* a session the adapter has started for the program, on a socket of its own (startDebugging
       of js-debug): @client is it, @parent the first one, which stays for the end; the request
       that starts the program there and its configuration */
    dap_client_t *parent;
    char *child_request;
    JsonObject *child_configuration;
    GPtrArray *gone;  // clients closed in a callback of theirs, freed later
} dap_backend_t;

#define DAP(b)    ((dap_backend_t *) (b))
#define EVENTS(d) ((d)->base.events)
#define UI(d)     ((d)->base.ui)

/* --------------------------------------------------------------------------------------------- */

static void
dap_bp_free (gpointer p)
{
    dap_bp_t *bp = (dap_bp_t *) p;

    if (bp->free_data != NULL)
        bp->free_data (bp->data);
    g_free (bp->id);
    g_free (bp->file);
    g_free (bp->name);
    g_free (bp);
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_frame_free (gpointer p)
{
    dap_frame_t *frame = (dap_frame_t *) p;

    g_free (frame->name);
    g_free (frame->file);
    g_free (frame->address);
    g_free (frame);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
dap_can (const dap_backend_t *dap, const char *capability)
{
    return dap_bool (dap->capabilities, capability, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* A JSON object node, to fill with the arguments of a request */
static JsonNode *
dap_args (JsonObject **object)
{
    JsonNode *node = json_node_new (JSON_NODE_OBJECT);

    *object = json_object_new ();
    json_node_take_object (node, *object);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* The arguments of a request about the thread of the stop */
static JsonNode *
dap_thread_args (const dap_backend_t *dap, JsonObject **object)
{
    JsonNode *node = dap_args (object);

    json_object_set_int_member (*object, "threadId", dap->thread);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* The id of the frame the plugin has chosen, -1 when there is none */
static gint64
dap_frame_id (const dap_backend_t *dap)
{
    return dap->selected < dap->frames->len
        ? ((dap_frame_t *) g_ptr_array_index (dap->frames, dap->selected))->id
        : -1;
}

/* --------------------------------------------------------------------------------------------- */

/* A request: 0 when it could not be sent, and the plugin is told */
static guint
dap_request (dap_backend_t *dap, const char *command, JsonNode *args, dap_response_fn fn,
             void *data, GDestroyNotify free_data)
{
    guint seq = dap_client_request (dap->client, command, args, fn, data, free_data);

    if (seq == 0)
        EVENTS (dap)->error (UI (dap), _ ("Could not send a request to the debug adapter."), FALSE);
    return seq;
}

/* --------------------------------------------------------------------------------------------- */

/* What waits for the answer of a request to the adapter: the callback of the plugin */
typedef struct
{
    dap_backend_t *dap;
    guint token;
    debug_reply_cb cb;
    void *data;
    GDestroyNotify free_data;
} dap_wait_t;

static dap_wait_t *
dap_wait_new (dap_backend_t *dap, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    dap_wait_t *wait = g_new0 (dap_wait_t, 1);

    wait->dap = dap;
    wait->token = ++dap->next_token;
    wait->cb = cb;
    wait->data = data;
    wait->free_data = free_data;
    return wait;
}

static void
dap_wait_free (gpointer p)
{
    dap_wait_t *wait = (dap_wait_t *) p;

    if (wait->free_data != NULL)
        wait->free_data (wait->data);
    g_free (wait);
}

/* The answer to the plugin; @reply has its request number put in */
static void
dap_wait_answer (dap_wait_t *wait, debug_reply_t *reply)
{
    reply->request = wait->token;
    if (wait->cb != NULL)
        wait->cb (UI (wait->dap), reply, wait->data);
    else if (!reply->ok)
        EVENTS (wait->dap)->error (
            UI (wait->dap),
            reply->msg != NULL ? reply->msg : _ ("The debug adapter refused a request."), FALSE);
}

/* A plain answer: done, or why not */
static void
dap_response_plain (void *owner, gboolean success, const char *message, JsonObject *body,
                    void *data)
{
    debug_reply_t reply = { 0 };

    (void) owner;
    (void) body;
    reply.ok = success;
    reply.msg = message;
    dap_wait_answer ((dap_wait_t *) data, &reply);
}

/* An answer no one waits for: only a failure is told, in the console */
static void
dap_response_plain_log (void *owner, gboolean success, const char *message, JsonObject *body,
                        void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    (void) body;
    (void) data;
    if (!success && message != NULL)
        EVENTS (dap)->output (UI (dap), message, DEBUG_OUTPUT_ERROR);
}

/* --------------------------------------------------------------------------------------------- */
/* The breakpoints */
/* --------------------------------------------------------------------------------------------- */

/* The breakpoints of a set as they were sent, the plugin's ids in the order of the request */
typedef struct
{
    dap_backend_t *dap;
    GPtrArray *ids;
    // those of the set the plugin waits for that are disabled: answered with the set
    GPtrArray *disabled;
} dap_sent_t;

static void
dap_sent_free (gpointer p)
{
    dap_sent_t *sent = (dap_sent_t *) p;

    g_ptr_array_free (sent->ids, TRUE);
    g_ptr_array_free (sent->disabled, TRUE);
    g_free (sent);
}

static dap_bp_t *
dap_bp_by_id (const dap_backend_t *dap, const char *id)
{
    guint i;

    for (i = 0; i < dap->breakpoints->len; i++)
    {
        dap_bp_t *bp = g_ptr_array_index (dap->breakpoints, i);

        if (g_strcmp0 (bp->id, id) == 0)
            return bp;
    }
    return NULL;
}

/* The answer to the plugin for a breakpoint it waits for */
static void
dap_bp_answer (dap_backend_t *dap, dap_bp_t *bp, gboolean ok, const char *msg)
{
    debug_reply_t reply = { 0 };
    debug_reply_cb cb = bp->cb;
    void *data = bp->data;
    GDestroyNotify free_data = bp->free_data;

    if (bp->token == 0)
        return;
    reply.request = bp->token;
    reply.ok = ok;
    reply.msg = msg;
    reply.id = bp->id;
    reply.line = bp->line;
    reply.pending = !bp->verified;
    bp->token = 0;
    bp->cb = NULL;
    bp->data = NULL;
    bp->free_data = NULL;
    if (cb != NULL)
        cb (UI (dap), &reply, data);
    else if (!ok && msg != NULL)
        EVENTS (dap)->output (UI (dap), msg, DEBUG_OUTPUT_ERROR);
    if (free_data != NULL)
        free_data (data);
}

/* The adapter has the breakpoints of a set */
static void
dap_response_breakpoints (void *owner, gboolean success, const char *message, JsonObject *body,
                          void *data)
{
    dap_sent_t *sent = (dap_sent_t *) data;
    dap_backend_t *dap = sent->dap;
    JsonArray *list = dap_array (body, "breakpoints");
    guint i;

    (void) owner;
    for (i = 0; i < sent->ids->len; i++)
    {
        dap_bp_t *bp = dap_bp_by_id (dap, g_ptr_array_index (sent->ids, i));
        JsonObject *answer;

        if (bp == NULL)
            continue;  // taken off since
        answer = success && list != NULL && i < json_array_get_length (list)
            ? json_array_get_object_element (list, i)
            : NULL;
        if (answer == NULL)
        {
            dap_bp_answer (dap, bp, FALSE,
                           message != NULL ? message
                                           : _ ("The debug adapter refused a breakpoint."));
            continue;
        }
        bp->dap_id = dap_int (answer, "id", -1);
        bp->verified = dap_bool (answer, "verified", FALSE);
        if (dap_int (answer, "line", 0) > 0)
            bp->line = (long) dap_int (answer, "line", 0);
        dap_bp_answer (dap, bp, TRUE, dap_string (answer, "message"));
    }
    for (i = 0; i < sent->disabled->len; i++)
    {
        dap_bp_t *bp = dap_bp_by_id (dap, g_ptr_array_index (sent->disabled, i));

        if (bp == NULL)
            continue;
        // kept by the plugin, the adapter has none to take
        bp->verified = TRUE;
        dap_bp_answer (dap, bp, TRUE, NULL);
    }
}

/* The breakpoints of a set sent again: those of a file, the functions, or the instructions */
static void
dap_breakpoints_send (dap_backend_t *dap, dap_bp_kind_t kind, const char *file)
{
    static const char *const commands[] = {
        [DAP_BP_LINE] = "setBreakpoints",
        [DAP_BP_FUNCTION] = "setFunctionBreakpoints",
        [DAP_BP_ADDRESS] = "setInstructionBreakpoints",
    };
    JsonObject *args;
    JsonNode *node;
    JsonArray *list = json_array_new ();
    dap_sent_t *sent;
    guint i;

    if (!dap_client_alive (dap->client))
    {
        json_array_unref (list);
        return;
    }
    node = dap_args (&args);
    sent = g_new0 (dap_sent_t, 1);
    sent->dap = dap;
    sent->ids = g_ptr_array_new_with_free_func (g_free);
    sent->disabled = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < dap->breakpoints->len; i++)
    {
        const dap_bp_t *bp = g_ptr_array_index (dap->breakpoints, i);
        JsonObject *item;

        if (bp->kind != kind || (kind == DAP_BP_LINE && g_strcmp0 (bp->file, file) != 0))
            continue;
        if (!bp->enabled)
        {
            if (bp->token != 0)
                g_ptr_array_add (sent->disabled, g_strdup (bp->id));
            continue;
        }
        item = json_object_new ();
        if (kind == DAP_BP_LINE)
            json_object_set_int_member (item, "line", bp->line);
        else if (kind == DAP_BP_FUNCTION)
            json_object_set_string_member (item, "name", bp->name);
        else
            json_object_set_string_member (item, "instructionReference", bp->name);
        json_array_add_object_element (list, item);
        g_ptr_array_add (sent->ids, g_strdup (bp->id));
    }
    if (kind == DAP_BP_LINE)
    {
        JsonObject *source = json_object_new ();

        json_object_set_string_member (source, "path", file);
        json_object_set_object_member (args, "source", source);
    }
    json_object_set_array_member (args, "breakpoints", list);
    (void) dap_request (dap, commands[kind], node, dap_response_breakpoints, sent, dap_sent_free);
}

/* The number of the request of a breakpoint, which the plugin keeps to know its answer */
static guint
bp_token_of (const dap_backend_t *dap, const dap_bp_t *bp)
{
    return bp->token != 0 ? bp->token : dap->next_token;
}

/* A breakpoint of the plugin, or one of its own: sent with the others of its set */
static guint
dap_bp_add (dap_backend_t *dap, dap_bp_kind_t kind, const char *file, long line, const char *name,
            gboolean enabled, gboolean temporary, debug_reply_cb cb, void *data,
            GDestroyNotify free_data)
{
    dap_bp_t *bp = g_new0 (dap_bp_t, 1);

    bp->kind = kind;
    bp->id = g_strdup_printf ("%u", ++dap->next_bp);
    bp->file = g_strdup (file);
    bp->line = line;
    bp->name = g_strdup (name);
    bp->enabled = enabled;
    bp->temporary = temporary;
    bp->dap_id = -1;
    bp->token = ++dap->next_token;
    bp->cb = cb;
    bp->data = data;
    bp->free_data = free_data;
    g_ptr_array_add (dap->breakpoints, bp);
    // a disabled one too: its answer comes with that of the set, never before this returns
    dap_breakpoints_send (dap, kind, file);
    return bp_token_of (dap, bp);
}

/* The breakpoints that went with a stop are taken off: run to the cursor, to a function */
static void
dap_breakpoints_temporary_off (dap_backend_t *dap)
{
    gboolean sets[3] = { FALSE, FALSE, FALSE };
    GPtrArray *files = g_ptr_array_new_with_free_func (g_free);
    guint i;

    for (i = 0; i < dap->breakpoints->len;)
    {
        dap_bp_t *bp = g_ptr_array_index (dap->breakpoints, i);

        if (!bp->temporary)
        {
            i++;
            continue;
        }
        sets[bp->kind] = TRUE;
        if (bp->kind == DAP_BP_LINE)
            g_ptr_array_add (files, g_strdup (bp->file));
        g_ptr_array_remove_index (dap->breakpoints, i);
    }
    for (i = 0; i < files->len; i++)
        dap_breakpoints_send (dap, DAP_BP_LINE, g_ptr_array_index (files, i));
    if (sets[DAP_BP_FUNCTION])
        dap_breakpoints_send (dap, DAP_BP_FUNCTION, NULL);
    if (sets[DAP_BP_ADDRESS])
        dap_breakpoints_send (dap, DAP_BP_ADDRESS, NULL);
    g_ptr_array_free (files, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/* The program runs, stops and ends */
/* --------------------------------------------------------------------------------------------- */

/* The frames of the stop are known: the plugin is told where the program is */
static void
dap_response_stop_frames (void *owner, gboolean success, const char *message, JsonObject *body,
                          void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;
    JsonObject *event = (JsonObject *) data;
    JsonArray *list = dap_array (body, "stackFrames");
    const char *reason = dap_string (event, "reason");
    debug_stop_t stop = { 0 };
    guint i;

    (void) message;
    g_ptr_array_set_size (dap->frames, 0);
    dap->selected = 0;
    for (i = 0; success && list != NULL && i < json_array_get_length (list); i++)
    {
        JsonObject *item = json_array_get_object_element (list, i);
        dap_frame_t *frame;

        if (item == NULL)
            continue;
        frame = g_new0 (dap_frame_t, 1);
        frame->id = dap_int (item, "id", 0);
        frame->name = g_strdup (dap_string (item, "name"));
        frame->file = g_strdup (dap_string (dap_object (item, "source"), "path"));
        frame->line = (long) dap_int (item, "line", 0);
        frame->address = g_strdup (dap_string (item, "instructionPointerReference"));
        g_ptr_array_add (dap->frames, frame);
    }
    if (reason == NULL)
        stop.reason = DEBUG_STOP_OTHER;
    else if (strstr (reason, "breakpoint") != NULL)
        stop.reason = DEBUG_STOP_BREAKPOINT;
    else if (strcmp (reason, "step") == 0)
        stop.reason = DEBUG_STOP_STEP;
    else if (strcmp (reason, "exception") == 0 || strcmp (reason, "signal") == 0)
        stop.reason = DEBUG_STOP_SIGNAL;
    else
        stop.reason = DEBUG_STOP_OTHER;
    if (stop.reason == DEBUG_STOP_SIGNAL)
    {
        stop.signal_name =
            dap_string (event, "description") != NULL ? dap_string (event, "description") : reason;
        stop.signal_meaning = dap_string (event, "text");
    }
    if (dap->frames->len > 0)
    {
        const dap_frame_t *top = g_ptr_array_index (dap->frames, 0);

        stop.has_frame = TRUE;
        stop.func = top->name;
        stop.file = top->file;
        stop.line = top->line;
        stop.address = top->address;
    }
    dap_breakpoints_temporary_off (dap);
    EVENTS (dap)->stopped (UI (dap), &stop);
}

static void
dap_event_stopped (dap_backend_t *dap, JsonObject *body)
{
    JsonObject *args;
    JsonNode *node;

    if (dap_int (body, "threadId", -1) >= 0)
        dap->thread = dap_int (body, "threadId", 1);
    node = dap_thread_args (dap, &args);
    json_object_set_int_member (args, "levels", 64);
    (void) dap_request (dap, "stackTrace", node, dap_response_stop_frames,
                        body != NULL ? json_object_ref (body) : json_object_new (),
                        (GDestroyNotify) json_object_unref);
}

/* --------------------------------------------------------------------------------------------- */

/* The output of the adapter or of the program */
static void
dap_event_output (dap_backend_t *dap, JsonObject *body)
{
    const char *category = dap_string (body, "category");
    const char *text = dap_string (body, "output");

    if (text == NULL || g_strcmp0 (category, "telemetry") == 0)
        return;
    EVENTS (dap)->output (UI (dap), text,
                          g_strcmp0 (category, "stdout") == 0 || g_strcmp0 (category, "stderr") == 0
                              ? DEBUG_OUTPUT_PROGRAM
                              : DEBUG_OUTPUT_CONSOLE);
}

/* --------------------------------------------------------------------------------------------- */

/* The adapter has moved a breakpoint, or put a pending one on code */
static void
dap_event_breakpoint (dap_backend_t *dap, JsonObject *body)
{
    JsonObject *changed = dap_object (body, "breakpoint");
    const gint64 id = dap_int (changed, "id", -1);
    const long line = (long) dap_int (changed, "line", 0);
    guint i;

    if (id < 0 || g_strcmp0 (dap_string (body, "reason"), "removed") == 0)
        return;
    for (i = 0; i < dap->breakpoints->len; i++)
    {
        dap_bp_t *bp = g_ptr_array_index (dap->breakpoints, i);

        if (bp->dap_id != id || bp->temporary)
            continue;
        if (!bp->verified && dap_bool (changed, "verified", FALSE))
        {
            bp->verified = TRUE;
            if (line > 0)
                bp->line = line;
            EVENTS (dap)->breakpoint_verified (UI (dap), bp->id, line);
        }
        else if (line > 0 && line != bp->line && bp->kind == DAP_BP_LINE)
        {
            bp->line = line;
            EVENTS (dap)->breakpoint_moved (UI (dap), bp->id, line);
        }
        return;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void dap_configure (dap_backend_t *dap);
static void dap_launch (dap_backend_t *dap);
static void dap_child_configure (dap_backend_t *dap);

static void
dap_event (void *owner, dap_client_t *client, const char *event, JsonObject *body)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    if (strcmp (event, "initialized") == 0 && dap->parent != NULL && client == dap->client)
        dap_child_configure (dap);
    else if (strcmp (event, "initialized") == 0)
    {
        /* the breakpoints go now; the launch went before (most adapters say "initialized" after
           it), or goes after configurationDone (GDB, which runs the program at the launch) */
        dap->initialized = TRUE;
        dap_configure (dap);
    }
    else if (strcmp (event, "stopped") == 0)
        dap_event_stopped (dap, body);
    else if (strcmp (event, "continued") == 0)
        EVENTS (dap)->running (UI (dap));
    else if (strcmp (event, "output") == 0)
        dap_event_output (dap, body);
    else if (strcmp (event, "breakpoint") == 0)
        dap_event_breakpoint (dap, body);
    else if (strcmp (event, "exited") == 0)
    {
        debug_stop_t stop = { 0 };
        char *code = g_strdup_printf ("%" G_GINT64_FORMAT, dap_int (body, "exitCode", 0));

        stop.reason = DEBUG_STOP_EXITED;
        stop.exit_code = code;
        EVENTS (dap)->stopped (UI (dap), &stop);
        g_free (code);
    }
    else if (strcmp (event, "terminated") == 0)
        EVENTS (dap)->exited (UI (dap), FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The child of runInTerminal: the terminal of the program is its controlling one */
static void
dap_terminal_child (gpointer data)
{
    const char *tty = (const char *) data;
    int fd;

    (void) setsid ();
    fd = open (tty, O_RDWR);
    if (fd < 0)
        return;
#ifdef TIOCSCTTY
    (void) ioctl (fd, TIOCSCTTY, 0);
#endif
    (void) dup2 (fd, STDIN_FILENO);
    (void) dup2 (fd, STDOUT_FILENO);
    (void) dup2 (fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close (fd);
}

/* The adapter asks for the program to run in a terminal: that of the program, the tab Program */
static void
dap_run_in_terminal (dap_backend_t *dap, dap_client_t *client, gint64 seq, JsonObject *arguments)
{
    JsonArray *list = dap_array (arguments, "args");
    JsonObject *env = dap_object (arguments, "env");
    char **argv, **envp = g_get_environ ();
    GPid pid = 0;
    GError *error = NULL;
    guint i, n;

    if (dap->tty == NULL || list == NULL || json_array_get_length (list) == 0)
    {
        g_strfreev (envp);
        (void) dap_client_respond (client, seq, "runInTerminal", FALSE,
                                   _ ("The program has no terminal: check Run in a terminal "
                                      "window in the configuration."),
                                   NULL);
        return;
    }
    /* under a shell of its own, which runs it and waits: the parent of the program is that shell.
       Spawned by itself it would have init for its parent once GLib let it go, and an adapter
       ends a program with the children of its parent: bash-debug-adapter runs
       "pkill -KILL -P <the parent of bashdb>", which would kill all that init has of the user,
       the systemd of the session among them */
    n = json_array_get_length (list);
    argv = g_new0 (char *, n + 5);
    argv[0] = g_strdup ("/bin/sh");
    argv[1] = g_strdup ("-c");
    argv[2] = g_strdup ("\"$@\"; exit $?");
    argv[3] = g_strdup ("sh");
    for (i = 0; i < n; i++)
        argv[i + 4] = g_strdup (json_array_get_string_element (list, i));
    if (env != NULL)
    {
        GList *names = json_object_get_members (env), *l;

        for (l = names; l != NULL; l = l->next)
        {
            const char *value = dap_string (env, l->data);

            envp = value != NULL ? g_environ_setenv (envp, l->data, value, TRUE)
                                 : g_environ_unsetenv (envp, l->data);
        }
        g_list_free (names);
    }
    if (g_spawn_async (dap_string (arguments, "cwd"), argv, envp, G_SPAWN_SEARCH_PATH,
                       dap_terminal_child, dap->tty, &pid, &error))
    {
        JsonObject *body;
        JsonNode *node = dap_args (&body);

        json_object_set_int_member (body, "processId", (gint64) pid);
        (void) dap_client_respond (client, seq, "runInTerminal", TRUE, NULL, node);
        g_spawn_close_pid (pid);
    }
    else
    {
        (void) dap_client_respond (client, seq, "runInTerminal", FALSE, error->message, NULL);
        g_error_free (error);
    }
    g_strfreev (argv);
    g_strfreev (envp);
}

static JsonNode *dap_initialize_args (const dap_backend_t *dap);
static const dap_handlers_t dap_handlers;

/* The program of the child session started there, its configuration from the adapter */
static void
dap_response_child_initialize (void *owner, gboolean success, const char *message, JsonObject *body,
                               void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;
    JsonNode *node;

    (void) data;
    if (!success)
    {
        EVENTS (dap)->error (
            UI (dap), message != NULL ? message : _ ("The session of the program would not start."),
            FALSE);
        return;
    }
    if (body != NULL)
    {
        if (dap->capabilities != NULL)
            json_object_unref (dap->capabilities);
        dap->capabilities = json_object_ref (body);
    }
    node = json_node_new (JSON_NODE_OBJECT);
    json_node_set_object (node, dap->child_configuration);
    (void) dap_request (dap, dap->child_request, node, dap_response_plain_log, NULL, NULL);
}

/* The child session is ready: it takes all the breakpoints, then the program goes */
static void
dap_child_configure (dap_backend_t *dap)
{
    GPtrArray *files = g_ptr_array_new_with_free_func (g_free);
    gboolean functions = FALSE, addresses = FALSE;
    guint i, j;

    for (i = 0; i < dap->breakpoints->len; i++)
    {
        const dap_bp_t *bp = g_ptr_array_index (dap->breakpoints, i);

        if (bp->kind == DAP_BP_FUNCTION)
            functions = TRUE;
        else if (bp->kind == DAP_BP_ADDRESS)
            addresses = TRUE;
        else
        {
            for (j = 0; j < files->len && g_strcmp0 (g_ptr_array_index (files, j), bp->file) != 0;
                 j++)
                ;
            if (j == files->len)
                g_ptr_array_add (files, g_strdup (bp->file));
        }
    }
    for (i = 0; i < files->len; i++)
        dap_breakpoints_send (dap, DAP_BP_LINE, g_ptr_array_index (files, i));
    if (functions)
        dap_breakpoints_send (dap, DAP_BP_FUNCTION, NULL);
    if (addresses)
        dap_breakpoints_send (dap, DAP_BP_ADDRESS, NULL);
    g_ptr_array_free (files, TRUE);
    (void) dap_request (dap, "configurationDone", NULL, dap_response_plain_log, NULL, NULL);
}

/* The adapter starts a session of its own for the program (js-debug): it is on another
   connection to the same socket, and the steps and the stops are there */
static void
dap_start_child (dap_backend_t *dap, dap_client_t *client, gint64 seq, JsonObject *arguments)
{
    JsonObject *configuration = dap_object (arguments, "configuration");
    dap_client_t *child;
    GError *error = NULL;

    if (dap->parent != NULL || dap_client_port (client) == 0 || configuration == NULL)
    {
        (void) dap_client_respond (client, seq, "startDebugging", FALSE,
                                   _ ("One session of a program at a time, on a socket."), NULL);
        return;
    }
    child = dap_client_new (&dap_handlers, dap);
    if (!dap_client_connect (child, dap_client_host (client), dap_client_port (client), &error))
    {
        (void) dap_client_respond (client, seq, "startDebugging", FALSE, error->message, NULL);
        g_error_free (error);
        dap_client_free (child);
        return;
    }
    (void) dap_client_respond (client, seq, "startDebugging", TRUE, NULL, NULL);
    dap->parent = dap->client;
    dap->client = child;
    g_free (dap->child_request);
    dap->child_request = g_strdup (
        g_strcmp0 (dap_string (arguments, "request"), "attach") == 0 ? "attach" : "launch");
    if (dap->child_configuration != NULL)
        json_object_unref (dap->child_configuration);
    dap->child_configuration = json_object_ref (configuration);
    (void) dap_request (dap, "initialize", dap_initialize_args (dap), dap_response_child_initialize,
                        NULL, NULL);
}

static void
dap_reverse_request (void *owner, dap_client_t *client, gint64 seq, const char *command,
                     JsonObject *arguments)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    if (strcmp (command, "runInTerminal") == 0)
        dap_run_in_terminal (dap, client, seq, arguments);
    else if (strcmp (command, "startDebugging") == 0)
        dap_start_child (dap, client, seq, arguments);
    else
        (void) dap_client_respond (client, seq, command, FALSE, _ ("Not supported."), NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_text (void *owner, const char *text)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    EVENTS (dap)->output (UI (dap), text, DEBUG_OUTPUT_ERROR);
}

static void
dap_closed (void *owner, dap_client_t *client, const char *why)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    if (why != NULL)
        EVENTS (dap)->output (UI (dap), why, DEBUG_OUTPUT_ERROR);
    g_ptr_array_set_size (dap->frames, 0);
    // the session of the program ends before the first one: that one says the end
    if (dap->parent != NULL && client == dap->client)
    {
        g_ptr_array_add (dap->gone, client);
        dap->client = dap->parent;
        dap->parent = NULL;
        if (dap_client_alive (dap->client))
            return;
    }
    else if (dap->parent != NULL && client == dap->parent)
    {
        g_ptr_array_add (dap->gone, client);
        dap->parent = NULL;
        if (dap_client_alive (dap->client))
            return;
    }
    EVENTS (dap)->exited (UI (dap), TRUE);
}

static void
dap_flush (void *owner)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    EVENTS (dap)->flush (UI (dap));
}

static const dap_handlers_t dap_handlers = {
    .event = dap_event,
    .request = dap_reverse_request,
    .text = dap_text,
    .closed = dap_closed,
    .flush = dap_flush,
};

/* --------------------------------------------------------------------------------------------- */
/* The start */
/* --------------------------------------------------------------------------------------------- */

/* The adapter is ready for the breakpoints: the plugin sends them, then DEBUG_EXEC_RUN */
static void
dap_configure (dap_backend_t *dap)
{
    if (dap->configured)
        return;
    dap->configured = TRUE;
    EVENTS (dap)->ready (UI (dap));
}

static void
dap_response_launch (void *owner, gboolean success, const char *message, JsonObject *body,
                     void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    (void) body;
    (void) data;
    if (!success)
        EVENTS (dap)->start_failed (
            UI (dap),
            message != NULL ? message : _ ("The debug adapter could not launch the program."));
}

/* The program launched: its name, arguments, directory and environment, with what the
   configuration adds for this adapter */
static void
dap_launch (dap_backend_t *dap)
{
    JsonObject *args;
    JsonNode *node;
    JsonArray *list = json_array_new ();
    JsonObject *env = json_object_new ();
    guint i;

    if (dap->launched)
        return;
    dap->launched = TRUE;
    node = dap_args (&args);
    json_object_set_string_member (args, "name", "coole");
    json_object_set_string_member (args, "type", "coole");
    json_object_set_string_member (args, "request", "launch");
    json_object_set_string_member (args, "program", dap->program);
    for (i = 0; dap->args != NULL && dap->args[i] != NULL; i++)
        json_array_add_string_element (list, dap->args[i]);
    json_object_set_array_member (args, "args", list);
    json_object_set_string_member (args, "cwd", dap->directory);
    for (i = 0; dap->environment != NULL && dap->environment[i] != NULL; i++)
    {
        char **pair = g_strsplit (dap->environment[i], "=", 2);

        if (pair[0] != NULL && pair[1] != NULL)
            json_object_set_string_member (env, pair[0], pair[1]);
        g_strfreev (pair);
    }
    json_object_set_object_member (args, "env", env);
    json_object_set_boolean_member (args, "stopOnEntry", FALSE);
    if (dap->launch_extra != NULL && *dap->launch_extra != '\0')
    {
        JsonParser *parser = json_parser_new ();

        // checked when the configuration was made: an object
        if (json_parser_load_from_data (parser, dap->launch_extra, -1, NULL)
            && JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
        {
            JsonObject *extra = json_node_get_object (json_parser_get_root (parser));
            GList *names = json_object_get_members (extra), *l;

            for (l = names; l != NULL; l = l->next)
                json_object_set_member (args, l->data,
                                        json_node_copy (json_object_get_member (extra, l->data)));
            g_list_free (names);
        }
        g_object_unref (parser);
    }
    (void) dap_request (dap, "launch", node, dap_response_launch, NULL, NULL);
}

static void
dap_response_initialize (void *owner, gboolean success, const char *message, JsonObject *body,
                         void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    (void) data;
    if (!success)
    {
        EVENTS (dap)->start_failed (
            UI (dap), message != NULL ? message : _ ("The debug adapter refused to start."));
        return;
    }
    if (dap->capabilities != NULL)
        json_object_unref (dap->capabilities);
    dap->capabilities = body != NULL ? json_object_ref (body) : json_object_new ();
    /* GDB says "initialized" with this answer and runs the program as soon as it is launched:
       the breakpoints go before; the others wait for the launch to say it */
    if (!dap->configure_first)
        dap_launch (dap);
}

/* Whether the adapter is GDB, which runs the program as soon as it is launched */
static gboolean
dap_is_gdb (char **argv)
{
    char *base = g_path_get_basename (argv[0]);
    const gboolean gdb = g_str_has_prefix (base, "gdb");

    g_free (base);
    return gdb;
}

/* What the client is and can do */
static JsonNode *
dap_initialize_args (const dap_backend_t *dap)
{
    JsonObject *args;
    JsonNode *node = dap_args (&args);

    json_object_set_string_member (args, "clientID", "coole");
    json_object_set_string_member (args, "clientName", "coole");
    json_object_set_string_member (
        args, "adapterID", dap->argv != NULL && dap->argv[0] != NULL ? dap->argv[0] : "coole");
    json_object_set_string_member (args, "pathFormat", "path");
    json_object_set_boolean_member (args, "linesStartAt1", TRUE);
    json_object_set_boolean_member (args, "columnsStartAt1", TRUE);
    json_object_set_boolean_member (args, "supportsVariableType", TRUE);
    json_object_set_boolean_member (args, "supportsRunInTerminalRequest", dap->tty != NULL);
    json_object_set_boolean_member (args, "supportsStartDebuggingRequest", TRUE);
    return node;
}

/* The session of a program the adapter had started is done with: the first one is the one */
static void
dap_child_drop (dap_backend_t *dap)
{
    if (dap->parent == NULL)
        return;
    // freed later: this may be from a callback of its own
    dap_client_close (dap->client, 0);
    g_ptr_array_add (dap->gone, dap->client);
    dap->client = dap->parent;
    dap->parent = NULL;
}

static gboolean
dap_start (debug_backend_t *b, const debug_start_t *spec, GError **error)
{
    dap_backend_t *dap = DAP (b);
    gboolean started;

    dap_child_drop (dap);
    g_ptr_array_set_size (dap->gone, 0);
    dap_client_close (dap->client, 0);
    g_strfreev (dap->argv);
    dap->argv = NULL;
    // no command: an adapter that listens already
    if (spec->debugger == NULL || *spec->debugger == '\0')
        dap->argv = g_new0 (char *, 1);
    else if (!g_shell_parse_argv (spec->debugger, NULL, &dap->argv, error))
        return FALSE;
    g_free (dap->program);
    dap->program = g_strdup (spec->program);
    g_strfreev (dap->args);
    dap->args = g_strdupv (spec->argv);
    g_free (dap->directory);
    dap->directory = g_strdup (spec->directory);
    g_strfreev (dap->environment);
    dap->environment = g_strdupv (spec->environment);
    g_free (dap->tty);
    dap->tty = g_strdup (spec->tty);
    g_free (dap->launch_extra);
    dap->launch_extra = g_strdup (spec->launch_extra);
    g_ptr_array_set_size (dap->breakpoints, 0);
    g_ptr_array_set_size (dap->frames, 0);
    dap->initialized = dap->launched = dap->configured = FALSE;
    if (dap->argv[0] == NULL && (spec->address == NULL || *spec->address == '\0'))
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                     _ ("The debug adapter has no command."));
        return FALSE;
    }
    dap->configure_first = dap->argv[0] != NULL && dap_is_gdb (dap->argv);
    dap->thread = 1;

    if (spec->address != NULL && *spec->address != '\0')
    {
        // host:port, the port the adapter's own when it is 0
        const char *colon = strrchr (spec->address, ':');
        char *host = colon != NULL ? g_strndup (spec->address, (gsize) (colon - spec->address))
                                   : g_strdup ("127.0.0.1");
        const int port = atoi (colon != NULL ? colon + 1 : spec->address);

        if (port == 0 && dap->argv[0] == NULL)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                         _ ("An adapter on port 0 is one to start: it needs its command."));
            started = FALSE;
        }
        else if (port == 0)
            started =
                dap_client_spawn_listening (dap->client, dap->argv, spec->directory, host,
                                            // "listening at: 127.0.0.1:38697"
                                            "(?:listening|port)[^\\n]*?([0-9]{2,5})\\s*$", error);
        else
            started = dap_client_connect (dap->client, host, port, error);
        g_free (host);
    }
    else
        started = dap_client_spawn (dap->client, dap->argv, spec->directory, error);
    if (!started)
        return FALSE;

    return dap_request (dap, "initialize", dap_initialize_args (dap), dap_response_initialize, NULL,
                        NULL)
        != 0;
}

/* --------------------------------------------------------------------------------------------- */
/* The requests of the plugin */
/* --------------------------------------------------------------------------------------------- */

static void
dap_stop (debug_backend_t *b)
{
    dap_backend_t *dap = DAP (b);

    // the session of the program first, then the first one
    while (TRUE)
    {
        if (dap_client_alive (dap->client))
        {
            JsonObject *args;
            JsonNode *node = dap_args (&args);

            json_object_set_boolean_member (args, "terminateDebuggee", TRUE);
            (void) dap_client_request (dap->client, "disconnect", node, NULL, NULL, NULL);
        }
        if (dap->parent == NULL)
            break;
        dap_child_drop (dap);
    }
    // the adapter ends when its input does, GDB ends the program
    dap_client_close (dap->client, 500);
    g_ptr_array_set_size (dap->frames, 0);
}

static gboolean
dap_alive (const debug_backend_t *b)
{
    const dap_backend_t *dap = DAP (b);

    return dap_client_alive (dap->client)
        || (dap->parent != NULL && dap_client_alive (dap->parent));
}

static void
dap_cancel (debug_backend_t *b)
{
    dap_backend_t *dap = DAP (b);

    dap_client_cancel (dap->client);
    if (dap->parent != NULL)
        dap_client_cancel (dap->parent);
}

/* --------------------------------------------------------------------------------------------- */

/* configurationDone done: GDB is launched only now */
static void
dap_response_configured (void *owner, gboolean success, const char *message, JsonObject *body,
                         void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;

    if (success && dap->configure_first)
        dap_launch (dap);
    dap_response_plain (owner, success, message, body, data);
}

static guint
dap_exec (debug_backend_t *b, debug_exec_t what, debug_reply_cb cb, void *data,
          GDestroyNotify free_data)
{
    dap_backend_t *dap = DAP (b);
    dap_wait_t *wait = dap_wait_new (dap, cb, data, free_data);
    const guint token = wait->token;
    const char *command = NULL;
    JsonObject *args = NULL;
    JsonNode *node = NULL;

    switch (what)
    {
    case DEBUG_EXEC_RUN:
        if (!dap_can (dap, "supportsConfigurationDoneRequest") && !dap->configure_first)
        {
            // nothing to say to it: the program runs
            debug_reply_t reply = { 0 };

            reply.ok = TRUE;
            dap_wait_answer (wait, &reply);
            dap_wait_free (wait);
            EVENTS (dap)->running (UI (dap));
            return token;
        }
        if (dap_request (dap, "configurationDone", NULL, dap_response_configured, wait,
                         dap_wait_free)
            == 0)
            return 0;
        EVENTS (dap)->running (UI (dap));
        return token;
    case DEBUG_EXEC_CONTINUE:
        command = "continue";
        break;
    case DEBUG_EXEC_PAUSE:
        command = "pause";
        break;
    case DEBUG_EXEC_NEXT:
    case DEBUG_EXEC_NEXT_INSTRUCTION:
        command = "next";
        break;
    case DEBUG_EXEC_STEP:
    case DEBUG_EXEC_STEP_INSTRUCTION:
        command = "stepIn";
        break;
    case DEBUG_EXEC_FINISH:
        command = "stepOut";
        break;
    default:
        dap_wait_free (wait);
        return 0;
    }
    node = dap_thread_args (dap, &args);
    if ((what == DEBUG_EXEC_NEXT_INSTRUCTION || what == DEBUG_EXEC_STEP_INSTRUCTION)
        && dap_can (dap, "supportsSteppingGranularity"))
        json_object_set_string_member (args, "granularity", "instruction");
    if (dap_request (dap, command, node, dap_response_plain, wait, dap_wait_free) == 0)
        return 0;
    if (what != DEBUG_EXEC_PAUSE)
    {
        g_ptr_array_set_size (dap->frames, 0);
        EVENTS (dap)->running (UI (dap));
    }
    return token;
}

/* --------------------------------------------------------------------------------------------- */

static guint
dap_break_insert (debug_backend_t *b, const char *file, long line, gboolean disabled,
                  debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    return dap_bp_add (DAP (b), DAP_BP_LINE, file, line, NULL, !disabled, FALSE, cb, data,
                       free_data);
}

static guint
dap_break_function (debug_backend_t *b, const char *func, gboolean temporary)
{
    return dap_bp_add (DAP (b), DAP_BP_FUNCTION, NULL, 0, func, TRUE, temporary, NULL, NULL, NULL);
}

static guint
dap_break_address (debug_backend_t *b, const char *address, debug_reply_cb cb, void *data,
                   GDestroyNotify free_data)
{
    dap_backend_t *dap = DAP (b);

    if (!dap_can (dap, "supportsInstructionBreakpoints"))
    {
        // the plugin is answered by no request: it keeps no breakpoint waiting
        EVENTS (dap)->output (UI (dap), _ ("The debug adapter has no breakpoints on instructions."),
                              DEBUG_OUTPUT_ERROR);
        if (free_data != NULL)
            free_data (data);
        return 0;
    }
    return dap_bp_add (dap, DAP_BP_ADDRESS, NULL, 0, address, TRUE, FALSE, cb, data, free_data);
}

static guint
dap_break_delete (debug_backend_t *b, const char *id)
{
    dap_backend_t *dap = DAP (b);
    dap_bp_t *bp = dap_bp_by_id (dap, id);
    dap_bp_kind_t kind;
    char *file;
    guint i;

    if (bp == NULL)
        return 0;
    kind = bp->kind;
    file = g_strdup (bp->file);
    for (i = 0; i < dap->breakpoints->len; i++)
        if (g_ptr_array_index (dap->breakpoints, i) == bp)
            g_ptr_array_remove_index (dap->breakpoints, i);
    dap_breakpoints_send (dap, kind, file);
    g_free (file);
    return ++dap->next_token;
}

static guint
dap_break_enable (debug_backend_t *b, const char *id, gboolean enable)
{
    dap_backend_t *dap = DAP (b);
    dap_bp_t *bp = dap_bp_by_id (dap, id);

    if (bp == NULL)
        return 0;
    bp->enabled = enable;
    dap_breakpoints_send (dap, bp->kind, bp->file);
    return ++dap->next_token;
}

/* Run to a line: a breakpoint taken off at the stop */
static guint
dap_run_to (debug_backend_t *b, const char *file, long line)
{
    dap_backend_t *dap = DAP (b);

    (void) dap_bp_add (dap, DAP_BP_LINE, file, line, NULL, TRUE, TRUE, NULL, NULL, NULL);
    return dap_exec (b, DEBUG_EXEC_CONTINUE, NULL, NULL, NULL);
}

static guint
dap_select_frame (debug_backend_t *b, long level)
{
    dap_backend_t *dap = DAP (b);

    if (level < 0 || (guint) level >= dap->frames->len)
        return 0;
    dap->selected = (guint) level;
    return ++dap->next_token;
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_response_stack (void *owner, gboolean success, const char *message, JsonObject *body,
                    void *data)
{
    dap_wait_t *wait = (dap_wait_t *) data;
    JsonArray *list = dap_array (body, "stackFrames");
    debug_reply_t reply = { 0 };
    guint i;

    (void) owner;
    reply.ok = success;
    reply.msg = message;
    reply.frames = g_ptr_array_new_with_free_func (debug_frame_free);
    for (i = 0; success && list != NULL && i < json_array_get_length (list); i++)
    {
        JsonObject *item = json_array_get_object_element (list, i);
        debug_frame_t *frame;

        if (item == NULL)
            continue;
        frame = g_new0 (debug_frame_t, 1);
        frame->level = (long) i;
        frame->func = g_strdup (dap_string (item, "name"));
        frame->file = g_strdup (dap_string (dap_object (item, "source"), "path"));
        frame->line = (long) dap_int (item, "line", 0);
        frame->address = g_strdup (dap_string (item, "instructionPointerReference"));
        if (frame->file == NULL)
            frame->from = g_strdup (dap_string (item, "moduleId"));
        g_ptr_array_add (reply.frames, frame);
    }
    dap_wait_answer (wait, &reply);
    g_ptr_array_unref (reply.frames);
}

static guint
dap_stack (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    dap_backend_t *dap = DAP (b);
    dap_wait_t *wait = dap_wait_new (dap, cb, data, free_data);
    const guint token = wait->token;
    JsonObject *args;
    JsonNode *node = dap_thread_args (dap, &args);

    json_object_set_int_member (args, "levels", 64);
    return dap_request (dap, "stackTrace", node, dap_response_stack, wait, dap_wait_free) != 0
        ? token
        : 0;
}

/* --------------------------------------------------------------------------------------------- */
/* The variables and the registers: the scopes of the frame, then the variables of those wanted */
/* --------------------------------------------------------------------------------------------- */

/* A request made of several; the answer goes when the last is done, none when they were
   forgotten */
typedef struct
{
    dap_wait_t *wait;
    gboolean registers;  // the scope of the registers, else those of the variables
    int refs;            // the requests owed
    int expected;
    int answered;
    GPtrArray *variables;
    char *msg;
} dap_gather_t;

static void
dap_gather_release (gpointer p)
{
    dap_gather_t *gather = (dap_gather_t *) p;

    if (--gather->refs > 0)
        return;
    if (gather->answered == gather->expected)
    {
        debug_reply_t reply = { 0 };

        reply.ok = gather->msg == NULL || gather->variables->len > 0;
        reply.msg = gather->msg;
        if (gather->registers)
            reply.registers = gather->variables;
        else
            reply.variables = gather->variables;
        dap_wait_answer (gather->wait, &reply);
    }
    dap_wait_free (gather->wait);
    g_ptr_array_unref (gather->variables);
    g_free (gather->msg);
    g_free (gather);
}

static void
dap_gather_failed (dap_gather_t *gather, const char *message)
{
    if (gather->msg == NULL)
        gather->msg = g_strdup (message != NULL ? message : _ ("The debug adapter refused."));
}

/* The variables of one scope */
static void
dap_response_gather_variables (void *owner, gboolean success, const char *message, JsonObject *body,
                               void *data)
{
    dap_gather_t *gather = (dap_gather_t *) data;
    JsonArray *list = dap_array (body, "variables");
    guint i;

    (void) owner;
    gather->answered++;
    if (!success)
    {
        dap_gather_failed (gather, message);
        return;
    }
    for (i = 0; list != NULL && i < json_array_get_length (list) && gather->variables->len < 256;
         i++)
    {
        JsonObject *item = json_array_get_object_element (list, i);
        debug_variable_t *variable;

        if (item == NULL || dap_string (item, "name") == NULL)
            continue;
        // a group with no value of its own: "special variables" of debugpy, say
        if (g_strcmp0 (dap_string (item, "value"), "") == 0
            && dap_int (item, "variablesReference", 0) > 0)
            continue;
        variable = g_new0 (debug_variable_t, 1);
        variable->name = g_strdup (dap_string (item, "name"));
        variable->value = g_strdup (dap_string (item, "value") != NULL ? dap_string (item, "value")
                                                                       : _ ("<unavailable>"));
        g_ptr_array_add (gather->variables, variable);
    }
}

/* The scopes of the frame: the variables of those wanted are asked for */
static void
dap_response_gather_scopes (void *owner, gboolean success, const char *message, JsonObject *body,
                            void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;
    dap_gather_t *gather = (dap_gather_t *) data;
    JsonArray *list = dap_array (body, "scopes");
    guint i;

    gather->answered++;
    if (!success)
    {
        dap_gather_failed (gather, message);
        return;
    }
    dap->registers_scope = FALSE;
    for (i = 0; list != NULL && i < json_array_get_length (list); i++)
    {
        JsonObject *scope = json_array_get_object_element (list, i);

        if (g_strcmp0 (dap_string (scope, "presentationHint"), "registers") == 0
            || g_strcmp0 (dap_string (scope, "name"), "Registers") == 0)
            dap->registers_scope = TRUE;
    }
    for (i = 0; list != NULL && i < json_array_get_length (list); i++)
    {
        JsonObject *scope = json_array_get_object_element (list, i);
        const gboolean of_registers =
            g_strcmp0 (dap_string (scope, "presentationHint"), "registers") == 0
            || g_strcmp0 (dap_string (scope, "name"), "Registers") == 0;
        JsonObject *args;
        JsonNode *node;

        if (scope == NULL || of_registers != gather->registers
            || dap_int (scope, "variablesReference", 0) <= 0
            // the globals, say: not at every stop
            || (!gather->registers && dap_bool (scope, "expensive", FALSE)))
            continue;
        node = dap_args (&args);
        json_object_set_int_member (args, "variablesReference",
                                    dap_int (scope, "variablesReference", 0));
        if (gather->registers && dap_can (dap, "supportsValueFormattingOptions"))
        {
            JsonObject *format = json_object_new ();

            json_object_set_boolean_member (format, "hex", TRUE);
            json_object_set_object_member (args, "format", format);
        }
        gather->refs++;
        gather->expected++;
        (void) dap_client_request (dap->client, "variables", node, dap_response_gather_variables,
                                   gather, dap_gather_release);
    }
}

static guint
dap_gather (dap_backend_t *dap, gboolean registers, debug_reply_cb cb, void *data,
            GDestroyNotify free_data)
{
    dap_gather_t *gather;
    JsonObject *args;
    JsonNode *node;
    guint token;

    if (dap_frame_id (dap) < 0)
    {
        if (free_data != NULL)
            free_data (data);
        return 0;
    }
    gather = g_new0 (dap_gather_t, 1);
    gather->wait = dap_wait_new (dap, cb, data, free_data);
    gather->registers = registers;
    gather->refs = 1;
    gather->expected = 1;
    gather->variables = g_ptr_array_new_with_free_func (debug_variable_free);
    token = gather->wait->token;
    node = dap_args (&args);
    json_object_set_int_member (args, "frameId", dap_frame_id (dap));
    return dap_request (dap, "scopes", node, dap_response_gather_scopes, gather, dap_gather_release)
            != 0
        ? token
        : 0;
}

static guint
dap_variables (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    return dap_gather (DAP (b), FALSE, cb, data, free_data);
}

static gboolean
dap_has_registers (const debug_backend_t *b)
{
    return DAP (b)->registers_scope;
}

static guint
dap_registers (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    return dap_gather (DAP (b), TRUE, cb, data, free_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_response_evaluate (void *owner, gboolean success, const char *message, JsonObject *body,
                       void *data)
{
    debug_reply_t reply = { 0 };

    (void) owner;
    reply.ok = success;
    reply.msg = message;
    reply.value = dap_string (body, "result");
    dap_wait_answer ((dap_wait_t *) data, &reply);
}

static guint
dap_evaluate_in (dap_backend_t *dap, const char *expression, const char *context,
                 dap_response_fn fn, debug_reply_cb cb, void *data, GDestroyNotify free_data)
{
    dap_wait_t *wait = dap_wait_new (dap, cb, data, free_data);
    const guint token = wait->token;
    JsonObject *args;
    JsonNode *node = dap_args (&args);

    json_object_set_string_member (args, "expression", expression);
    json_object_set_string_member (args, "context", context);
    if (dap_frame_id (dap) >= 0)
        json_object_set_int_member (args, "frameId", dap_frame_id (dap));
    return dap_request (dap, "evaluate", node, fn, wait, dap_wait_free) != 0 ? token : 0;
}

static guint
dap_evaluate (debug_backend_t *b, const char *expression, debug_reply_cb cb, void *data,
              GDestroyNotify free_data)
{
    return dap_evaluate_in (DAP (b), expression, "watch", dap_response_evaluate, cb, data,
                            free_data);
}

/* What the adapter answers to a command typed for it goes to the console */
static void
dap_response_console (void *owner, gboolean success, const char *message, JsonObject *body,
                      void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;
    const char *result = dap_string (body, "result");

    if (success && result != NULL && *result != '\0')
    {
        EVENTS (dap)->output (UI (dap), result, DEBUG_OUTPUT_CONSOLE);
        if (result[strlen (result) - 1] != '\n')
            EVENTS (dap)->output (UI (dap), "\n", DEBUG_OUTPUT_CONSOLE);
    }
    dap_response_plain (owner, success, message, body, data);
}

static guint
dap_console (debug_backend_t *b, const char *line, debug_reply_cb cb, void *data,
             GDestroyNotify free_data)
{
    return dap_evaluate_in (DAP (b), line, "repl", dap_response_console, cb, data, free_data);
}

/* --------------------------------------------------------------------------------------------- */

/* The source of an instruction: GDB may give the name of the file alone, that of a frame then */
static char *
dap_instruction_file (const dap_backend_t *dap, const char *path)
{
    guint i;

    if (path == NULL || g_path_is_absolute (path))
        return g_strdup (path);
    for (i = 0; i < dap->frames->len; i++)
    {
        const dap_frame_t *frame = g_ptr_array_index (dap->frames, i);

        if (frame->file != NULL && g_str_has_suffix (frame->file, path)
            && (strlen (frame->file) == strlen (path)
                || frame->file[strlen (frame->file) - strlen (path) - 1] == '/'))
            return g_strdup (frame->file);
    }
    return NULL;
}

/* A disassembly asked for: from some instructions before the address, then from the address
   itself when the adapter does not go back right (GDB 15 does not) */
typedef struct
{
    dap_wait_t *wait;
    char *address;
    gboolean from_address;
} dap_disasm_t;

static void
dap_disasm_free (gpointer p)
{
    dap_disasm_t *disasm = (dap_disasm_t *) p;

    if (disasm->wait != NULL)
        dap_wait_free (disasm->wait);
    g_free (disasm->address);
    g_free (disasm);
}

static guint dap_disassemble_from (dap_backend_t *dap, dap_disasm_t *disasm);

/* Whether the instructions have that of @address */
static gboolean
dap_instructions_have (JsonArray *list, const char *address)
{
    const guint64 wanted = g_ascii_strtoull (address, NULL, 16);
    guint i;

    for (i = 0; list != NULL && i < json_array_get_length (list); i++)
    {
        const char *at = dap_string (json_array_get_object_element (list, i), "address");

        if (at != NULL && g_ascii_strtoull (at, NULL, 16) == wanted)
            return TRUE;
    }
    return FALSE;
}

static void
dap_response_disassemble (void *owner, gboolean success, const char *message, JsonObject *body,
                          void *data)
{
    dap_backend_t *dap = (dap_backend_t *) owner;
    dap_disasm_t *disasm = (dap_disasm_t *) data;
    JsonArray *list = dap_array (body, "instructions");
    debug_reply_t reply = { 0 };
    char *func = NULL;
    guint64 start = 0;
    guint i;

    if (!disasm->from_address && !(success && dap_instructions_have (list, disasm->address)))
    {
        dap_disasm_t *again = g_new0 (dap_disasm_t, 1);

        // the request is the other one's now
        again->wait = disasm->wait;
        again->address = g_strdup (disasm->address);
        again->from_address = TRUE;
        disasm->wait = NULL;
        (void) dap_disassemble_from (dap, again);
        return;
    }

    reply.ok = success;
    reply.msg = message;
    reply.instructions = g_ptr_array_new_with_free_func (debug_instruction_free);
    for (i = 0; success && list != NULL && i < json_array_get_length (list); i++)
    {
        JsonObject *item = json_array_get_object_element (list, i);
        const char *address = dap_string (item, "address");
        const char *symbol = dap_string (item, "symbol");
        const guint64 at = address != NULL ? g_ascii_strtoull (address, NULL, 16) : 0;
        debug_instruction_t *insn;

        if (at == 0)
            continue;
        // the symbol comes with the first instruction of a function, "main" or "main+17"
        if (symbol != NULL)
        {
            const char *plus = strrchr (symbol, '+');

            g_free (func);
            func = plus != NULL ? g_strndup (symbol, (gsize) (plus - symbol)) : g_strdup (symbol);
            start = at - (plus != NULL ? g_ascii_strtoull (plus + 1, NULL, 0) : 0);
        }
        insn = g_new0 (debug_instruction_t, 1);
        insn->address = g_strdup (address);
        insn->func = g_strdup (func);
        insn->offset = func != NULL ? (long) (at - start) : 0;
        insn->text = g_strdup (dap_string (item, "instruction"));
        insn->file = dap_instruction_file (dap, dap_string (dap_object (item, "location"), "path"));
        insn->line = insn->file != NULL ? (long) dap_int (item, "line", 0) : 0;
        g_ptr_array_add (reply.instructions, insn);
    }
    g_free (func);
    dap_wait_answer (disasm->wait, &reply);
    g_ptr_array_unref (reply.instructions);
}

static guint
dap_disassemble_from (dap_backend_t *dap, dap_disasm_t *disasm)
{
    const guint token = disasm->wait->token;
    JsonObject *args;
    JsonNode *node = dap_args (&args);

    json_object_set_string_member (args, "memoryReference", disasm->address);
    json_object_set_int_member (args, "instructionOffset", disasm->from_address ? 0 : -16);
    json_object_set_int_member (args, "instructionCount", 64);
    json_object_set_boolean_member (args, "resolveSymbols", TRUE);
    return dap_request (dap, "disassemble", node, dap_response_disassemble, disasm, dap_disasm_free)
            != 0
        ? token
        : 0;
}

static guint
dap_disassemble (debug_backend_t *b, const char *address, debug_reply_cb cb, void *data,
                 GDestroyNotify free_data)
{
    dap_backend_t *dap = DAP (b);
    dap_disasm_t *disasm;

    if (!dap_can (dap, "supportsDisassembleRequest") || address == NULL)
    {
        if (free_data != NULL)
            free_data (data);
        return 0;
    }
    disasm = g_new0 (dap_disasm_t, 1);
    disasm->wait = dap_wait_new (dap, cb, data, free_data);
    disasm->address = g_strdup (address);
    return dap_disassemble_from (dap, disasm);
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_free (debug_backend_t *b)
{
    dap_backend_t *dap = DAP (b);

    dap_child_drop (dap);
    dap_client_free (dap->client);
    g_ptr_array_free (dap->gone, TRUE);
    g_free (dap->child_request);
    if (dap->child_configuration != NULL)
        json_object_unref (dap->child_configuration);
    g_strfreev (dap->argv);
    g_free (dap->program);
    g_strfreev (dap->args);
    g_free (dap->directory);
    g_strfreev (dap->environment);
    g_free (dap->tty);
    g_free (dap->launch_extra);
    if (dap->capabilities != NULL)
        json_object_unref (dap->capabilities);
    g_ptr_array_free (dap->frames, TRUE);
    g_ptr_array_free (dap->breakpoints, TRUE);
    g_free (dap);
}

static const debug_backend_ops_t dap_ops = {
    .name = "dap",
    .start = dap_start,
    .stop = dap_stop,
    .alive = dap_alive,
    .cancel = dap_cancel,
    .exec = dap_exec,
    .break_insert = dap_break_insert,
    .break_function = dap_break_function,
    .break_address = dap_break_address,
    .break_delete = dap_break_delete,
    .break_enable = dap_break_enable,
    .run_to = dap_run_to,
    .select_frame = dap_select_frame,
    .stack = dap_stack,
    .variables = dap_variables,
    .evaluate = dap_evaluate,
    .disassemble = dap_disassemble,
    .has_registers = dap_has_registers,
    .registers = dap_registers,
    .console = dap_console,
    .free = dap_free,
};

/* --------------------------------------------------------------------------------------------- */

debug_backend_t *
debug_dap_new (const debug_backend_events_t *events, void *ui)
{
    dap_backend_t *dap = g_new0 (dap_backend_t, 1);

    dap->base.ops = &dap_ops;
    dap->base.events = events;
    dap->base.ui = ui;
    dap->client = dap_client_new (&dap_handlers, dap);
    dap->frames = g_ptr_array_new_with_free_func (dap_frame_free);
    dap->breakpoints = g_ptr_array_new_with_free_func (dap_bp_free);
    dap->gone = g_ptr_array_new_with_free_func ((GDestroyNotify) dap_client_free);
    dap->thread = 1;
    return &dap->base;
}
