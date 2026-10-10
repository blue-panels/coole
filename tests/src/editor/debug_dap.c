/*
   src/plugins/debugger - tests for the debug adapter as a backend of the debugger

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

/* -Dassert=false sets G_DISABLE_ASSERT in config.h: g_test_init () would then end the test and
   g_assert () check nothing, and the checks are the point of a test */
#undef G_DISABLE_ASSERT

#include <sys/select.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "lib/tty/key.h"
#include "src/plugins/debugger/debug-backend.h"

typedef struct
{
    int fd;
    select_fn callback;
    void *data;
} watched_t;

static watched_t watched[8];
static int watched_count;

void
add_select_channel (int fd, select_fn callback, void *data)
{
    g_assert_cmpint (watched_count, <, G_N_ELEMENTS (watched));
    watched[watched_count++] = (watched_t) { fd, callback, data };
}

void
delete_select_channel (int fd)
{
    int i;

    for (i = 0; i < watched_count; i++)
        if (watched[i].fd == fd)
        {
            watched[i] = watched[--watched_count];
            return;
        }
}

static void
run_until (const gboolean *done)
{
    const gint64 deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;

    while (!*done && g_get_monotonic_time () < deadline)
    {
        fd_set set;
        struct timeval timeout = { 0, 100000 };
        int i, max_fd = -1, ready_count = 0;
        watched_t ready[8];

        FD_ZERO (&set);
        for (i = 0; i < watched_count; i++)
        {
            FD_SET (watched[i].fd, &set);
            max_fd = MAX (max_fd, watched[i].fd);
        }
        if (max_fd < 0 || select (max_fd + 1, &set, NULL, NULL, &timeout) <= 0)
            continue;
        for (i = 0; i < watched_count; i++)
            if (FD_ISSET (watched[i].fd, &set))
                ready[ready_count++] = watched[i];
        for (i = 0; i < ready_count; i++)
            ready[i].callback (ready[i].fd, ready[i].data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* What the plugin would see */

typedef struct
{
    debug_backend_t *backend;
    gboolean ready;
    gboolean running;
    gboolean stopped;
    gboolean exited;
    gboolean ended;
    debug_stop_reason_t reason;
    char *stop_file;
    long stop_line;
    char *exit_code;
    GString *console;
    GString *program;
    // the answers
    gboolean answered;
    guint request;
    char *bp_id;
    long bp_line;
    GPtrArray *frames;
    GPtrArray *variables;
    GPtrArray *registers;
    GPtrArray *instructions;
    char *value;
    char *error;
} seen_t;

static seen_t seen;

static void
seen_clear (void)
{
    g_free (seen.stop_file);
    g_free (seen.exit_code);
    g_free (seen.bp_id);
    g_free (seen.value);
    g_free (seen.error);
    if (seen.console != NULL)
        g_string_free (seen.console, TRUE);
    if (seen.program != NULL)
        g_string_free (seen.program, TRUE);
    g_clear_pointer (&seen.frames, g_ptr_array_unref);
    g_clear_pointer (&seen.variables, g_ptr_array_unref);
    g_clear_pointer (&seen.registers, g_ptr_array_unref);
    g_clear_pointer (&seen.instructions, g_ptr_array_unref);
    memset (&seen, 0, sizeof (seen));
    seen.console = g_string_new (NULL);
    seen.program = g_string_new (NULL);
}

static void
on_output (void *ui, const char *text, debug_output_t kind)
{
    (void) ui;
    g_string_append (kind == DEBUG_OUTPUT_PROGRAM ? seen.program : seen.console, text);
}

static void
on_ready (void *ui)
{
    (void) ui;
    seen.ready = TRUE;
}

static void
on_start_failed (void *ui, const char *msg)
{
    (void) ui;
    g_free (seen.error);
    seen.error = g_strdup (msg != NULL ? msg : "?");
    seen.ended = TRUE;
}

static void
on_running (void *ui)
{
    (void) ui;
    seen.running = TRUE;
}

static void
on_stopped (void *ui, const debug_stop_t *stop)
{
    (void) ui;
    seen.stopped = TRUE;
    seen.reason = stop->reason;
    g_free (seen.stop_file);
    seen.stop_file = g_strdup (stop->file);
    seen.stop_line = stop->line;
    if (stop->reason == DEBUG_STOP_EXITED)
    {
        seen.exited = TRUE;
        g_free (seen.exit_code);
        seen.exit_code = g_strdup (stop->exit_code);
    }
}

static void
on_exited (void *ui, gboolean gone)
{
    (void) ui;
    (void) gone;
    seen.ended = TRUE;
}

static void
on_moved (void *ui, const char *id, long line)
{
    (void) ui;
    (void) id;
    (void) line;
}

static void
on_error (void *ui, const char *msg, gboolean unasked)
{
    (void) ui;
    (void) unasked;
    g_free (seen.error);
    seen.error = g_strdup (msg);
}

static void
on_flush (void *ui)
{
    (void) ui;
}

static const debug_backend_events_t events = {
    .output = on_output,
    .ready = on_ready,
    .start_failed = on_start_failed,
    .running = on_running,
    .stopped = on_stopped,
    .exited = on_exited,
    .breakpoint_moved = on_moved,
    .breakpoint_verified = on_moved,
    .error = on_error,
    .flush = on_flush,
};

static void
on_reply (void *ui, const debug_reply_t *reply, void *data)
{
    (void) ui;
    g_assert_cmpstr ((const char *) data, ==, "mine");
    seen.answered = TRUE;
    seen.request = reply->request;
    if (!reply->ok)
    {
        g_free (seen.error);
        seen.error = g_strdup (reply->msg != NULL ? reply->msg : "?");
    }
    if (reply->id != NULL)
    {
        g_free (seen.bp_id);
        seen.bp_id = g_strdup (reply->id);
        seen.bp_line = reply->line;
    }
    if (reply->value != NULL)
    {
        g_free (seen.value);
        seen.value = g_strdup (reply->value);
    }
    if (reply->frames != NULL)
        seen.frames = g_ptr_array_ref (reply->frames);
    if (reply->variables != NULL)
        seen.variables = g_ptr_array_ref (reply->variables);
    if (reply->registers != NULL)
        seen.registers = g_ptr_array_ref (reply->registers);
    if (reply->instructions != NULL)
        seen.instructions = g_ptr_array_ref (reply->instructions);
}

/* An answer to wait for */
static void
wait_answer (void)
{
    run_until (&seen.answered);
    g_assert_true (seen.answered);
    seen.answered = FALSE;
}

#define MINE (g_strdup ("mine")), g_free

static void
test_session (gconstpointer address)
{
    char *python = g_find_program_in_path ("python3");
    char *command;
    char *argv[] = { (char *) "one arg", NULL };
    char *environment[] = { (char *) "GREETING=hi", NULL };
    debug_start_t spec = { 0 };
    GError *error = NULL;
    const debug_variable_t *variable;
    const debug_instruction_t *insn;

    if (python == NULL)
    {
        g_test_skip ("python3 is unavailable");
        return;
    }
    seen_clear ();
    command = g_strdup_printf ("%s %s%s", python, MOCK_DAP, address != NULL ? " --tcp 0" : "");
    spec.program = "/bin/true";
    spec.argv = argv;
    spec.directory = "/";
    spec.environment = environment;
    spec.tty = "/dev/null";
    spec.debugger = command;
    spec.address = address;
    spec.launch_extra = "{\"console\": \"integratedTerminal\", \"justMyCode\": true}";
    seen.backend = debug_dap_new (&events, NULL);

    g_assert_true (seen.backend->ops->start (seen.backend, &spec, &error));
    g_assert_no_error (error);
    run_until (&seen.ready);
    g_assert_true (seen.ready);
    g_assert_true (seen.backend->ops->alive (seen.backend));

    // a breakpoint, then the run: it stops there
    g_assert_cmpuint (
        seen.backend->ops->break_insert (seen.backend, "/src/a.c", 7, FALSE, on_reply, MINE), >, 0);
    wait_answer ();
    g_assert_nonnull (seen.bp_id);
    g_assert_cmpint (seen.bp_line, ==, 7);
    // a disabled one is answered after the call has given its number, with that number
    {
        const guint token =
            seen.backend->ops->break_insert (seen.backend, "/src/a.c", 9, TRUE, on_reply, MINE);

        g_assert_cmpuint (token, >, 0);
        g_assert_false (seen.answered);
        wait_answer ();
        g_assert_cmpuint (seen.request, ==, token);
        g_assert_cmpint (seen.bp_line, ==, 9);
    }
    g_assert_cmpuint (seen.backend->ops->exec (seen.backend, DEBUG_EXEC_RUN, on_reply, MINE), >, 0);
    run_until (&seen.stopped);
    g_assert_true (seen.running);
    g_assert_cmpint (seen.reason, ==, DEBUG_STOP_BREAKPOINT);
    g_assert_cmpstr (seen.stop_file, ==, "/src/a.c");
    g_assert_cmpint (seen.stop_line, ==, 7);
    // what the program writes is its own; runInTerminal ran it on its terminal
    g_assert_nonnull (strstr (seen.program->str, "program says hello"));
    g_assert_nonnull (strstr (seen.console->str, "mock: runInTerminal True"));
    // the answer to the run has come with the stop
    g_assert_true (seen.answered);
    seen.answered = FALSE;

    (void) seen.backend->ops->stack (seen.backend, on_reply, MINE);
    wait_answer ();
    g_assert_cmpuint (seen.frames->len, ==, 2);
    g_assert_cmpstr (((debug_frame_t *) g_ptr_array_index (seen.frames, 0))->func, ==, "main");
    g_assert_cmpstr (((debug_frame_t *) g_ptr_array_index (seen.frames, 0))->address, ==, "0x1000");

    (void) seen.backend->ops->variables (seen.backend, on_reply, MINE);
    wait_answer ();
    g_assert_cmpuint (seen.variables->len, ==, 2);
    variable = g_ptr_array_index (seen.variables, 0);
    g_assert_cmpstr (variable->name, ==, "x");
    g_assert_cmpstr (variable->value, ==, "42");

    (void) seen.backend->ops->registers (seen.backend, on_reply, MINE);
    wait_answer ();
    g_assert_cmpuint (seen.registers->len, ==, 1);
    g_assert_cmpstr (((debug_variable_t *) g_ptr_array_index (seen.registers, 0))->name, ==, "rip");

    (void) seen.backend->ops->evaluate (seen.backend, "x+1", on_reply, MINE);
    wait_answer ();
    g_assert_cmpstr (seen.value, ==, "<x+1>");
    (void) seen.backend->ops->evaluate (seen.backend, "bad", on_reply, MINE);
    wait_answer ();
    g_assert_cmpstr (seen.error, ==, "no symbol bad");
    (void) seen.backend->ops->console (seen.backend, "info", on_reply, MINE);
    wait_answer ();
    g_assert_nonnull (strstr (seen.console->str, "<info>\n"));

    (void) seen.backend->ops->disassemble (seen.backend, "0x1000", on_reply, MINE);
    wait_answer ();
    g_assert_cmpuint (seen.instructions->len, ==, 3);
    insn = g_ptr_array_index (seen.instructions, 1);
    g_assert_cmpstr (insn->func, ==, "main");
    g_assert_cmpint (insn->offset, ==, 1);
    insn = g_ptr_array_index (seen.instructions, 0);
    g_assert_cmpstr (insn->file, ==, "/src/a.c");

    // a step stops again; the end is told
    seen.stopped = seen.running = FALSE;
    (void) seen.backend->ops->exec (seen.backend, DEBUG_EXEC_STEP_INSTRUCTION, NULL, NULL, NULL);
    run_until (&seen.stopped);
    g_assert_true (seen.running);
    g_assert_cmpint (seen.reason, ==, DEBUG_STOP_STEP);
    (void) seen.backend->ops->exec (seen.backend, DEBUG_EXEC_CONTINUE, NULL, NULL, NULL);
    run_until (&seen.ended);
    g_assert_true (seen.exited);
    g_assert_cmpstr (seen.exit_code, ==, "3");

    seen.backend->ops->stop (seen.backend);
    g_assert_false (seen.backend->ops->alive (seen.backend));
    seen.backend->ops->free (seen.backend);
    g_assert_cmpint (watched_count, ==, 0);
    seen_clear ();
    g_free (command);
    g_free (python);
}

static void
test_missing_adapter (void)
{
    debug_start_t spec = { 0 };
    GError *error = NULL;
    debug_backend_t *backend = debug_dap_new (&events, NULL);

    spec.program = "/bin/true";
    spec.directory = "/";
    spec.debugger = "/nonexistent/adapter --flag";
    g_assert_false (backend->ops->start (backend, &spec, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_false (backend->ops->alive (backend));
    backend->ops->free (backend);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_data_func ("/debugger/dap-backend-stdio", NULL, test_session);
    g_test_add_data_func ("/debugger/dap-backend-tcp", "127.0.0.1:0", test_session);
    g_test_add_func ("/debugger/dap-backend-missing", test_missing_adapter);
    return g_test_run ();
}
