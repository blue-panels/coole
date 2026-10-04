/*
   src/plugins/debugger - tests for the GDB/MI records and session

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
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
#include "src/plugins/debugger/gdb-mi.h"

typedef struct
{
    int fd;
    select_fn callback;
    void *data;
} watched_t;

static watched_t watched[4];
static int watched_count;
static gboolean got_version;
static gboolean exited;

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
record (const char *line, void *data)
{
    (void) data;
    if (strstr (line, "GNU gdb") != NULL)
        got_version = TRUE;
    if (g_str_has_prefix (line, "=gdb-exited"))
        exited = TRUE;
}

static void
test_quote (void)
{
    char *quoted = gdb_mi_quote ("a b\\\"c\n");

    g_assert_cmpstr (quoted, ==, "\"a b\\\\\\\"c\\n\"");
    g_free (quoted);
}

static void
test_parse_stopped (void)
{
    gdb_mi_record_t *r = gdb_mi_parse (
        "*stopped,reason=\"breakpoint-hit\",disp=\"keep\",bkptno=\"1\",frame={addr=\"0x1\","
        "func=\"main\",args=[],file=\"a.c\",fullname=\"/tmp/"
        "a\\\"b.c\",line=\"42\"},thread-id=\"1\"\r");
    const gdb_mi_value_t *frame;

    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_EXEC);
    g_assert_false (r->has_token);
    g_assert_cmpstr (r->klass, ==, "stopped");
    g_assert_cmpstr (gdb_mi_record_string (r, "reason"), ==, "breakpoint-hit");
    frame = gdb_mi_get (r->results, "frame");
    g_assert_nonnull (frame);
    g_assert_cmpint (frame->kind, ==, GDB_MI_TUPLE);
    g_assert_cmpstr (gdb_mi_get_string (frame, "fullname"), ==, "/tmp/a\"b.c");
    g_assert_cmpstr (gdb_mi_get_string (frame, "line"), ==, "42");
    g_assert_cmpint (gdb_mi_get (frame, "args")->kind, ==, GDB_MI_LIST);
    g_assert_null (gdb_mi_get_string (frame, "args"));
    g_assert_cmpstr (gdb_mi_record_string (r, "thread-id"), ==, "1");
    // a name inside a tuple is not one of the record
    g_assert_null (gdb_mi_record_string (r, "line"));
    gdb_mi_record_free (r);
}

static void
test_parse_result_lists (void)
{
    gdb_mi_record_t *r = gdb_mi_parse ("12^done,stack=[frame={level=\"0\",func=\"f\"},"
                                       "frame={level=\"1\",func=\"main\",from=\"/lib/x.so\"}]");
    const gdb_mi_value_t *stack;
    const gdb_mi_value_t *second;

    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_RESULT);
    g_assert_true (r->has_token);
    g_assert_cmpuint (r->token, ==, 12);
    g_assert_cmpstr (r->klass, ==, "done");
    stack = gdb_mi_get (r->results, "stack");
    g_assert_cmpint (stack->kind, ==, GDB_MI_LIST);
    g_assert_cmpuint (stack->items->len, ==, 2);
    second = g_ptr_array_index (stack->items, 1);
    g_assert_cmpstr (second->name, ==, "frame");
    g_assert_cmpstr (gdb_mi_get_string (second, "from"), ==, "/lib/x.so");
    gdb_mi_record_free (r);

    // a list of values, and a list of tuples with no names
    r = gdb_mi_parse ("^done,variables=[{name=\"i\",value=\"1\"},{name=\"s\",type=\"struct x\"}],"
                      "names=[\"a\",\"b\"]");
    stack = gdb_mi_get (r->results, "variables");
    g_assert_cmpuint (stack->items->len, ==, 2);
    g_assert_null (((gdb_mi_value_t *) g_ptr_array_index (stack->items, 0))->name);
    g_assert_cmpstr (gdb_mi_get_string (g_ptr_array_index (stack->items, 1), "type"), ==,
                     "struct x");
    stack = gdb_mi_get (r->results, "names");
    g_assert_cmpstr (((gdb_mi_value_t *) g_ptr_array_index (stack->items, 1))->string, ==, "b");
    gdb_mi_record_free (r);
}

static void
test_parse_streams (void)
{
    gdb_mi_record_t *r = gdb_mi_parse ("~\"Breakpoint 1 at 0x1: \\303\\251.c, line 3.\\n\"");

    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_CONSOLE);
    g_assert_cmpstr (r->text, ==, "Breakpoint 1 at 0x1: \xc3\xa9.c, line 3.\n");
    gdb_mi_record_free (r);

    r = gdb_mi_parse ("@\"out\"");
    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_TARGET);
    g_assert_cmpstr (r->text, ==, "out");
    gdb_mi_record_free (r);

    r = gdb_mi_parse ("&\"set x\\n\"");
    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_LOG);
    gdb_mi_record_free (r);

    r = gdb_mi_parse ("(gdb) ");
    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_PROMPT);
    gdb_mi_record_free (r);
}

static void
test_parse_errors (void)
{
    gdb_mi_record_t *r = gdb_mi_parse ("7^error,msg=\"No symbol \\\"x\\\" in current context.\"");

    g_assert_cmpstr (r->klass, ==, "error");
    g_assert_cmpstr (gdb_mi_record_string (r, "msg"), ==, "No symbol \"x\" in current context.");
    gdb_mi_record_free (r);

    // what is no record stays as it came: a line of GDB on stderr
    r = gdb_mi_parse ("warning: Error disabling address space randomization");
    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_OTHER);
    g_assert_cmpstr (r->text, ==, "warning: Error disabling address space randomization");
    gdb_mi_record_free (r);

    // a record cut short keeps what could be read
    r = gdb_mi_parse ("=breakpoint-modified,bkpt={number=\"1\",line=\"5\"},broken=\"");
    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_NOTIFY);
    g_assert_cmpstr (gdb_mi_get_string (gdb_mi_get (r->results, "bkpt"), "line"), ==, "5");
    gdb_mi_record_free (r);

    r = gdb_mi_parse ("=gdb-exited");
    g_assert_cmpint (r->kind, ==, GDB_MI_RECORD_NOTIFY);
    g_assert_cmpstr (r->klass, ==, "gdb-exited");
    gdb_mi_record_free (r);
}

static void
test_gdb_transport (void)
{
    char *gdb = g_find_program_in_path ("gdb");
    gdb_mi_session_t *session;
    GError *error = NULL;
    gint64 deadline;

    if (gdb == NULL)
    {
        g_test_skip ("GDB is unavailable");
        return;
    }
    session = gdb_mi_session_new (record, NULL);
    g_assert_true (gdb_mi_session_start (session, gdb, &error));
    g_assert_no_error (error);
    g_assert_true (gdb_mi_session_send (session, "-gdb-version"));
    g_assert_true (gdb_mi_session_send (session, "-gdb-exit"));
    deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!exited && g_get_monotonic_time () < deadline)
    {
        fd_set set;
        struct timeval timeout = { 0, 100000 };
        int i, max_fd = -1;
        watched_t ready[4];
        int ready_count = 0;

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
    g_assert_true (got_version);
    g_assert_true (exited);
    gdb_mi_session_free (session);
    g_free (gdb);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/debugger/mi-quote", test_quote);
    g_test_add_func ("/debugger/mi-parse-stopped", test_parse_stopped);
    g_test_add_func ("/debugger/mi-parse-result-lists", test_parse_result_lists);
    g_test_add_func ("/debugger/mi-parse-streams", test_parse_streams);
    g_test_add_func ("/debugger/mi-parse-errors", test_parse_errors);
    g_test_add_func ("/debugger/gdb-transport", test_gdb_transport);
    return g_test_run ();
}
