/*
   The PostgreSQL backend of coole -Q run on a live server: queries, errors, transactions, COPY,
   the cancel.

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

/* Skipped unless COOLE_TEST_PG is set; the server is that of the PG* variables, with a password,
   and only temporary tables are made in it (PGSERVICE is not taken):

     docker run --rm -d --name coole-pg-test -e POSTGRES_PASSWORD=secret \
         -p 127.0.0.1:55434:5432 postgres:16
     COOLE_TEST_PG=1 PGHOST=127.0.0.1 PGPORT=55434 PGUSER=postgres PGPASSWORD=secret \
         meson test -C build query_pg query_pg_old

   The editor is not there: the functions of the plugin the backend calls are those below, and the
   sockets libpq gives it are polled here as the editor selects on them. */

#include <config.h>

/* -Dassert=false sets G_DISABLE_ASSERT in config.h: g_test_init () would then end the test and
   g_assert () check nothing, and the checks are the point of a test */
#undef G_DISABLE_ASSERT

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "lib/tty/key.h"

#include "src/plugins/query/query-int.h"
#include "src/plugins/query/query-pg-schema.h"
#include "src/plugins/query/query-pq.h"

/* A run that takes longer has hung */
#define RUN_TIMEOUT (30 * G_USEC_PER_SEC)

/* A select channel of the editor */
typedef struct
{
    int fd;
    select_fn callback;
    void *info;
    gboolean write;
} channel_t;

static GArray *channels;

/* What the backend has told the plugin */
static char *tab_text[QUERY_TAB_COUNT];
static char *output_title;
static int history_count;
static int history_status;
static char *history_summary;
static long error_line, error_column;

static query_t q;
static void *backend;
static mc_editor_host_t host;

/* --------------------------------------------------------------------------------------------- */
/* The editor */
/* --------------------------------------------------------------------------------------------- */

void
add_select_channel (int fd, select_fn callback, void *info)
{
    channel_t c = { fd, callback, info, FALSE };

    delete_select_channel (fd);
    g_array_append_val (channels, c);
}

void
select_channel_want_write (int fd, gboolean want)
{
    guint i;

    for (i = 0; i < channels->len; i++)
        if (g_array_index (channels, channel_t, i).fd == fd)
            g_array_index (channels, channel_t, i).write = want;
}

void
delete_select_channel (int fd)
{
    guint i;

    for (i = 0; i < channels->len; i++)
        if (g_array_index (channels, channel_t, i).fd == fd)
        {
            g_array_remove_index (channels, i);
            return;
        }
}

static void
host_call_later (mc_editor_host_t *h, void (*fn) (void *data), void *data)
{
    (void) h;
    fn (data);
}

/* The channels polled for @timeout ms, the callbacks of those ready called; as the editor does,
   each by its channel as it is then: one a callback before has taken off is not called */
static void
serve (int timeout)
{
    const guint n = channels->len;
    struct pollfd *fds = g_new (struct pollfd, n);
    guint i, j;
    int got;

    for (i = 0; i < n; i++)
    {
        const channel_t *c = &g_array_index (channels, channel_t, i);

        fds[i].fd = c->fd;
        fds[i].events = POLLIN | (c->write ? POLLOUT : 0);
        fds[i].revents = 0;
    }
    got = poll (fds, n, timeout);
    if (got < 0)
        g_assert_cmpint (errno, ==, EINTR);
    for (i = 0; got > 0 && i < n; i++)
        if (fds[i].revents != 0)
            for (j = 0; j < channels->len; j++)
            {
                const channel_t c = g_array_index (channels, channel_t, j);

                if (c.fd == fds[i].fd)
                {
                    (void) c.callback (c.fd, c.info);
                    break;
                }
            }
    g_free (fds);
}

/* The channels served until the backend @data runs nothing */
static void
loop_of (void *data)
{
    const gint64 end = g_get_monotonic_time () + RUN_TIMEOUT;

    while (query_postgres_backend.running (data))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, end);
        g_assert_cmpuint (channels->len, >, 0);
        serve (100);
    }
}

static void
loop (void)
{
    loop_of (backend);
}

/* Some time passes with the channels served, what runs not waited for */
static void
loop_for (gint64 usec)
{
    const gint64 end = g_get_monotonic_time () + usec;

    while (g_get_monotonic_time () < end)
        serve (20);
}

/* --------------------------------------------------------------------------------------------- */
/* The plugin */
/* --------------------------------------------------------------------------------------------- */

void
query_tab_set (query_t *qq, int tab, const char *text, gboolean front)
{
    (void) qq;
    (void) front;
    g_free (tab_text[tab]);
    tab_text[tab] = g_strdup (text);
}

void
query_tab_title (query_t *qq, int tab, const char *title)
{
    (void) qq;
    if (tab == QUERY_TAB_OUTPUT)
    {
        g_free (output_title);
        output_title = g_strdup (title);
    }
}

void
query_table_say (query_t *qq, const char *text)
{
    query_tab_set (qq, QUERY_TAB_TABLE, text, FALSE);
}

void
query_redraw (query_t *qq)
{
    (void) qq;
}

void
query_output_head (query_t *qq, GString *text, double seconds, const char *when)
{
    (void) qq;
    (void) seconds;
    (void) when;
    g_string_append (text, "head: ");
}

void
query_mark_error (query_t *qq, long line, long column)
{
    (void) qq;
    error_line = line;
    error_column = column;
}

void
query_history_add (query_t *qq, const char *when, double seconds, int status, const char *summary)
{
    (void) qq;
    (void) when;
    (void) seconds;
    history_count++;
    history_status = status;
    g_free (history_summary);
    history_summary = g_strdup (summary);
}

void
query_run_alone (query_t *qq, const char *text)
{
    (void) qq;
    (void) text;
}

void
query_ai_context (query_t *qq, const char *context, const char *error)
{
    (void) qq;
    (void) context;
    (void) error;
}

void
query_ai_startup (query_t *qq, GString *text)
{
    (void) qq;
    (void) text;
}

/* The window Schema is not there */
query_pg_schema_t *
query_pg_schema_new (query_t *qq, const query_pq_t *pq, gboolean read_only, const char *schema)
{
    (void) qq;
    (void) pq;
    (void) read_only;
    (void) schema;
    return NULL;
}

void
query_pg_schema_free (query_pg_schema_t *s)
{
    (void) s;
}

gboolean
query_pg_schema_do (query_pg_schema_t *s, query_schema_action_t action, gboolean key)
{
    (void) s;
    (void) action;
    (void) key;
    return FALSE;
}

void
query_pg_schema_changed (query_pg_schema_t *s)
{
    (void) s;
}

void
query_pg_schema_ai (query_pg_schema_t *s)
{
    (void) s;
}

/* --------------------------------------------------------------------------------------------- */
/* The checks */
/* --------------------------------------------------------------------------------------------- */

/* @text started in the backend @data of @qq as F5 runs a selection from the first line of the
   file */
static void
run_start (query_t *qq, void *data, const char *text)
{
    g_free (qq->text);
    qq->text = g_strdup (text);
    qq->first = 1;
    qq->first_column = 0;
    qq->stopped = FALSE;
    qq->started = g_get_monotonic_time ();
    error_line = error_column = -1;
    query_postgres_backend.start (data);
}

/* @text run: until it has ended */
static void
run (const char *text)
{
    const int before = history_count;

    run_start (&q, backend, text);
    loop ();
    g_assert_cmpint (history_count, ==, before + 1);
}

static void
assert_has (const char *text, const char *part)
{
    if (text == NULL || strstr (text, part) == NULL)
        g_error ("\"%s\" is not in:\n%s", part, text != NULL ? text : "(null)");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_connect (void)
{
    GString *head = g_string_new (NULL);

    query_postgres_backend.startup (backend, NULL, head);
    assert_has (head->str, "libpq: ");
    loop ();
    assert_has (tab_text[QUERY_TAB_OUTPUT], "Connected to ");
    g_string_free (head, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_commands (void)
{
    run ("CREATE TEMP TABLE t (id int, name text);\n"
         "INSERT INTO t VALUES (1, 'one'), (2, 'two') RETURNING id;\n"
         "SELECT * FROM t ORDER BY id");
    g_assert_cmpint (history_status, ==, 0);
    g_assert_cmpstr (history_summary, ==, "SELECT 2");
    assert_has (tab_text[QUERY_TAB_OUTPUT], "head: done");
    assert_has (tab_text[QUERY_TAB_OUTPUT], "CREATE TABLE\n");
    assert_has (tab_text[QUERY_TAB_OUTPUT], "INSERT 0 2\n");
    // two tables: the ids returned, the rows
    assert_has (tab_text[QUERY_TAB_TABLE], "1. INSERT 0 2: ");
    assert_has (tab_text[QUERY_TAB_TABLE], "2. SELECT 2: ");
    assert_has (tab_text[QUERY_TAB_TABLE], "two");
    g_assert_cmpint (error_line, ==, -1);

    // NULL, and a query longer than the buffer of the socket, sent by pieces
    {
        GString *big = g_string_new ("SELECT NULL AS nothing, length('");

        while (big->len < 3 * 1024 * 1024)
            g_string_append (big, "abcdefghijklmnopqrstuvwxyz0123456789");
        g_string_append (big, "') AS n");
        run (big->str);
        g_string_free (big, TRUE);
    }
    g_assert_cmpint (history_status, ==, 0);
    g_assert_cmpstr (history_summary, ==, "SELECT 1");
    assert_has (tab_text[QUERY_TAB_TABLE], "nothing");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_errors (void)
{
    // the place of the error is in characters: Cyrillic before it is two bytes each
    run ("SELECT '\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82',\n  nosuch");
    g_assert_cmpint (history_status, ==, 1);
    g_assert_cmpstr (history_summary, ==, "ERROR 42703");
    g_assert_cmpint (error_line, ==, 2);
    g_assert_cmpint (error_column, ==, 2);
    assert_has (tab_text[QUERY_TAB_OUTPUT], "head: failed");
    assert_has (tab_text[QUERY_TAB_OUTPUT], "at line 2, column 3");

    /* rows came before the error: they are not all of the rows of the command; by chunks libpq
       drops the chunk the error came in */
    run ("SELECT 1 / (3 - i) FROM generate_series (1, 5) AS i");
    g_assert_cmpint (history_status, ==, 1);
    g_assert_cmpstr (history_summary, ==, "ERROR 22012");
    if (query_pq_chunks (query_pq_load (NULL)))
        assert_has (tab_text[QUERY_TAB_TABLE], "No rows");
    else
        assert_has (tab_text[QUERY_TAB_TABLE], "incomplete, the command failed after 2 rows");

    // an error in the third command of a query: those before it are of its transaction
    run ("CREATE TEMP TABLE gone (id int);\nINSERT INTO gone VALUES (1);\nSELECT 1 / 0");
    g_assert_cmpint (history_status, ==, 1);
    // of this session alone: query_pg_old may run beside it on the same server
    run ("SELECT to_regclass ('pg_temp.gone') IS NULL AS gone");
    assert_has (tab_text[QUERY_TAB_TABLE], "| t ");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_transaction (void)
{
    run ("BEGIN");
    g_assert_cmpint (history_status, ==, 0);
    assert_has (output_title, "[TX]");
    run ("SELECT 1 / 0");
    assert_has (output_title, "[TX FAILED]");
    assert_has (tab_text[QUERY_TAB_OUTPUT], "TX FAILED: ROLLBACK required");
    run ("SELECT 1");
    g_assert_cmpstr (history_summary, ==, "ERROR 25P02");
    run ("ROLLBACK");
    g_assert_cmpint (history_status, ==, 0);
    g_assert_null (output_title);

    // the session is kept between runs
    run ("SET search_path TO pg_catalog");
    run ("SHOW search_path");
    assert_has (tab_text[QUERY_TAB_TABLE], "pg_catalog");
    run ("RESET search_path");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_copy (void)
{
    run ("COPY (SELECT i FROM generate_series (1, 3) AS i) TO STDOUT");
    g_assert_cmpint (history_status, ==, 0);
    assert_has (tab_text[QUERY_TAB_OUTPUT], "1\n2\n3\n");

    // there is nothing to send: the server ends it with an error, the session goes on
    run ("CREATE TEMP TABLE c (id int); COPY c FROM STDIN");
    g_assert_cmpint (history_status, ==, 1);
    assert_has (tab_text[QUERY_TAB_OUTPUT], "coole -Q has nothing to send");
    run ("SELECT 7 AS seven");
    g_assert_cmpint (history_status, ==, 0);
    assert_has (tab_text[QUERY_TAB_TABLE], "seven");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_cancel (void)
{
    const int before = history_count;
    const gint64 started = g_get_monotonic_time ();

    run_start (&q, backend, "SELECT pg_sleep (30)");
    loop_for (G_USEC_PER_SEC / 2);
    g_assert_true (query_postgres_backend.running (backend));
    query_postgres_backend.stop (backend);
    loop ();
    g_assert_cmpint (history_count, ==, before + 1);
    g_assert_cmpint (history_status, ==, -1);
    g_assert_cmpstr (history_summary, ==, "ERROR 57014");
    g_assert_cmpint (g_get_monotonic_time () - started, <, 10 * G_USEC_PER_SEC);
    assert_has (tab_text[QUERY_TAB_OUTPUT], "head: stopped");

    // the cancel of the query before does not end this one
    run ("SELECT 'after' AS word");
    g_assert_cmpint (history_status, ==, 0);
    assert_has (tab_text[QUERY_TAB_TABLE], "after");
}

/* --------------------------------------------------------------------------------------------- */

/* The session ended by the server: the next run connects again and says what is gone */
static void
test_lost (void)
{
    run ("CREATE TEMP TABLE kept (id int)");
    run ("SELECT pg_terminate_backend (pg_backend_pid ())");
    g_assert_cmpint (history_status, ==, 1);
    assert_has (tab_text[QUERY_TAB_OUTPUT], "The connection is lost");
    run ("SELECT to_regclass ('pg_temp.kept') IS NULL AS gone");
    g_assert_cmpint (history_status, ==, 0);
    assert_has (tab_text[QUERY_TAB_OUTPUT], "It is a new connection");
    assert_has (tab_text[QUERY_TAB_TABLE], "| t ");
}

/* --------------------------------------------------------------------------------------------- */

/* Another connection, its password wrong: the run that waited for it is not run */
static void
test_password (void)
{
    query_t other = { 0 };
    void *data;
    char *password = g_strdup (g_getenv ("PGPASSWORD"));
    const int before = history_count;

    if (password == NULL)
    {
        g_test_skip ("PGPASSWORD is not set: the server may take no password");
        return;
    }
    g_setenv ("PGPASSWORD", "surely-not-the-password", TRUE);
    other.host = &host;
    other.backend = &query_postgres_backend;
    data = query_postgres_backend.open (&other);
    run_start (&other, data, "SELECT 1");
    loop_of (data);
    g_assert_cmpint (history_count, ==, before + 1);
    // trust in pg_hba.conf: any password is taken
    if (history_status == 0)
        g_test_skip ("the server takes any password");
    else
    {
        g_assert_cmpint (history_status, ==, 1);
        g_assert_cmpstr (history_summary, ==, "no connection");
        assert_has (tab_text[QUERY_TAB_OUTPUT], "No connection to ");
        assert_has (tab_text[QUERY_TAB_OUTPUT], "password");
    }
    query_postgres_backend.close (data);
    g_free (other.text);
    g_free (other.endpoint);
    g_setenv ("PGPASSWORD", password, TRUE);
    g_free (password);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    int ret, i;

    g_test_init (&argc, &argv, NULL);
    if (g_getenv ("COOLE_TEST_PG") == NULL)
        return 77;  // skipped: no server
    /* the variables of the plugin would change what it does, and a service another server: a
       shell of coole -Q has them */
    g_unsetenv ("MC_PG_RELATION");
    g_unsetenv ("MC_QUERY_READ_ONLY");
    g_unsetenv ("MC_PG_CONNECTION");
    g_unsetenv ("MC_PG_SCHEMA");
    g_unsetenv ("PGSERVICE");

    channels = g_array_new (FALSE, FALSE, sizeof (channel_t));
    host.call_later = host_call_later;
    q.host = &host;
    q.backend = &query_postgres_backend;
    backend = query_postgres_backend.open (&q);
    q.backend_data = backend;

    g_test_add_func ("/query-pg/connect", test_connect);
    g_test_add_func ("/query-pg/commands", test_commands);
    g_test_add_func ("/query-pg/errors", test_errors);
    g_test_add_func ("/query-pg/transaction", test_transaction);
    g_test_add_func ("/query-pg/copy", test_copy);
    // before a thread of a cancel is there: it changes the environment
    g_test_add_func ("/query-pg/password", test_password);
    g_test_add_func ("/query-pg/cancel", test_cancel);
    g_test_add_func ("/query-pg/lost", test_lost);
    ret = g_test_run ();

    query_postgres_backend.close (backend);
    g_free (q.text);
    g_free (q.endpoint);
    for (i = 0; i < QUERY_TAB_COUNT; i++)
        g_free (tab_text[i]);
    g_free (output_title);
    g_free (history_summary);
    g_array_free (channels, TRUE);
    return ret;
}
