/*
   src/plugins/lsp - tests for the client of a language server

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

#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "lib/tty/key.h"
#include "src/plugins/lsp/lsp-client.h"

/* --------------------------------------------------------------------------------------------- */
/* The select loop of the program */

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

/* --------------------------------------------------------------------------------------------- */

typedef gboolean (*until_fn) (void);

static lsp_client_t *client;

/* The loop, with the ticks, until @until says so or ten seconds */
static void
run_until (until_fn until)
{
    const gint64 deadline = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;

    while (!until () && g_get_monotonic_time () < deadline)
    {
        fd_set set;
        struct timeval timeout = { 0, 20000 };
        int i, max_fd = -1, ready_count = 0;
        watched_t ready[8];

        FD_ZERO (&set);
        for (i = 0; i < watched_count; i++)
        {
            FD_SET (watched[i].fd, &set);
            max_fd = MAX (max_fd, watched[i].fd);
        }
        if (max_fd >= 0 && select (max_fd + 1, &set, NULL, NULL, &timeout) > 0)
        {
            for (i = 0; i < watched_count; i++)
                if (FD_ISSET (watched[i].fd, &set))
                    ready[ready_count++] = watched[i];
            // a callback may take another channel away
            for (i = 0; i < ready_count; i++)
            {
                int k;

                for (k = 0; k < watched_count; k++)
                    if (watched[k].fd == ready[i].fd && watched[k].data == ready[i].data)
                        break;
                if (k < watched_count)
                    ready[i].callback (ready[i].fd, ready[i].data);
            }
        }
        else if (max_fd < 0)
            g_usleep (20000);
        if (client != NULL)
            lsp_client_tick (client, g_get_monotonic_time ());
        lsp_client_reap_tick (g_get_monotonic_time ());
    }
    g_assert_true (until ());
}

/* --------------------------------------------------------------------------------------------- */
/* What the owner is told */

static struct
{
    int states;                // how many times told
    lsp_client_state_t state;  // the last one told
    int diagnostics;
    char *last_problem;
    int message_type;
    char *message;
} told;

static void
told_reset (void)
{
    g_free (told.last_problem);
    g_free (told.message);
    memset (&told, 0, sizeof (told));
}

static void
on_state (void *owner, lsp_client_t *c)
{
    (void) owner;
    told.states++;
    told.state = lsp_client_state (c);
}

static void
on_diagnostics (void *owner, lsp_client_t *c, const char *uri, JsonArray *diagnostics)
{
    (void) owner;
    (void) c;
    (void) uri;
    told.diagnostics++;
    g_free (told.last_problem);
    told.last_problem = NULL;
    if (json_array_get_length (diagnostics) > 0)
        told.last_problem = g_strdup (json_object_get_string_member (
            json_array_get_object_element (diagnostics, 0), "message"));
}

static void
on_message (void *owner, lsp_client_t *c, int type, const char *text)
{
    (void) owner;
    (void) c;
    told.message_type = type;
    g_free (told.message);
    told.message = g_strdup (text);
}

static const lsp_client_handlers_t handlers = { on_state, on_diagnostics, on_message };

/* --------------------------------------------------------------------------------------------- */

static gboolean
is_running (void)
{
    return lsp_client_state (client) == LSP_CLIENT_RUNNING;
}

static gboolean
is_down (void)
{
    return lsp_client_state (client) == LSP_CLIENT_STOPPED
        || lsp_client_state (client) == LSP_CLIENT_FAILED;
}

static gboolean
is_reaped (void)
{
    return !lsp_client_reaping ();
}

/* --------------------------------------------------------------------------------------------- */
/* The messages the server got, from its log */

static char *log_path;
// the interpreter of the server; NULL when there is none
static char *python;

static GPtrArray *
server_got (void)
{
    GPtrArray *messages = g_ptr_array_new_with_free_func ((GDestroyNotify) json_node_unref);
    char *text = NULL;
    char **lines;
    int i;

    (void) g_file_get_contents (log_path, &text, NULL, NULL);
    lines = g_strsplit (text != NULL ? text : "", "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
        if (*lines[i] != '\0')
            g_ptr_array_add (messages, json_from_string (lines[i], NULL));
    g_strfreev (lines);
    g_free (text);
    return messages;
}

/* The methods the server got, in order, "," between them */
static char *
server_methods (void)
{
    GPtrArray *messages = server_got ();
    GString *out = g_string_new (NULL);
    guint i;

    for (i = 0; i < messages->len; i++)
    {
        JsonObject *o = json_node_get_object (g_ptr_array_index (messages, i));

        if (out->len > 0)
            g_string_append_c (out, ',');
        g_string_append (out,
                         json_object_has_member (o, "method")
                             ? json_object_get_string_member (o, "method")
                             : "response");
    }
    g_ptr_array_free (messages, TRUE);
    return g_string_free (out, FALSE);
}

/* The message the server got with @id, as JSON; NULL when none */
static char *
server_response (const char *id)
{
    GPtrArray *messages = server_got ();
    char *found = NULL;
    guint i;

    for (i = 0; i < messages->len && found == NULL; i++)
    {
        JsonNode *node = g_ptr_array_index (messages, i);
        JsonObject *o = json_node_get_object (node);
        JsonNode *message_id = json_object_get_member (o, "id");

        if (message_id != NULL && !json_object_has_member (o, "method"))
        {
            char *text = json_to_string (message_id, FALSE);

            if (strcmp (text, id) == 0)
            {
                json_object_remove_member (o, "jsonrpc");
                found = json_to_string (node, FALSE);
            }
            g_free (text);
        }
    }
    g_ptr_array_free (messages, TRUE);
    return found;
}

static gboolean
server_has (const char *needle)
{
    char *methods = server_methods ();
    const gboolean has = strstr (methods, needle) != NULL;

    g_free (methods);
    return has;
}

/* --------------------------------------------------------------------------------------------- */

/* the server of the tests is a script of python */
static void
test_no_python (void)
{
    g_test_skip ("python is unavailable");
}

/* --------------------------------------------------------------------------------------------- */

static lsp_client_t *
new_client (const char *mode, const char *settings)
{
    static int count = 0;
    lsp_config_t *config;
    char *json, *name;

    g_assert_nonnull (python);
    if (log_path != NULL)
        (void) g_remove (log_path);
    g_free (log_path);
    name = g_strdup_printf ("lsp-client-test-%d-%d.log", (int) getpid (), ++count);
    log_path = g_build_filename (g_get_tmp_dir (), name, (char *) NULL);
    g_free (name);
    json = g_strdup_printf ("{\"backends\":[{\"id\":\"mock\",\"command\":[\"%s\",\"%s\",\"%s\","
                            "\"%s\"],\"settings\":%s}]}",
                            python, MOCK_LSP, log_path, mode, settings);
    config = lsp_config_parse (json, -1, NULL);
    g_assert_nonnull (config);
    told_reset ();
    client =
        lsp_client_new (g_ptr_array_index (config->backends, 0), g_get_tmp_dir (), &handlers, NULL);
    lsp_config_free (config);
    g_free (json);
    return client;
}

static void
free_client (void)
{
    lsp_client_free (client);
    client = NULL;
    run_until (is_reaped);
}

/* --------------------------------------------------------------------------------------------- */
/* The requests */

typedef struct
{
    int calls;
    int destroyed;
    lsp_request_status_t status;
    char *result;
    char *error;
} answer_t;

static void
on_answer (lsp_client_t *c, lsp_request_status_t status, JsonNode *result, JsonObject *error,
           void *data)
{
    answer_t *answer = (answer_t *) data;

    (void) c;
    answer->calls++;
    answer->status = status;
    g_free (answer->result);
    answer->result = result != NULL ? json_to_string (result, FALSE) : NULL;
    g_free (answer->error);
    answer->error = error != NULL && json_object_has_member (error, "message")
        ? g_strdup (json_object_get_string_member (error, "message"))
        : NULL;
}

static void
on_answer_destroyed (void *data)
{
    ((answer_t *) data)->destroyed++;
}

static answer_t *waited;

static gboolean
is_answered (void)
{
    return waited->calls > 0;
}

static void
answer_clear (answer_t *answer)
{
    g_free (answer->result);
    g_free (answer->error);
    memset (answer, 0, sizeof (*answer));
}

static JsonNode *
document_params (const char *uri)
{
    JsonObject *params = json_object_new ();
    JsonObject *document = json_object_new ();
    JsonObject *position = json_object_new ();
    JsonNode *node = json_node_new (JSON_NODE_OBJECT);

    json_object_set_string_member (document, "uri", uri);
    json_object_set_object_member (params, "textDocument", document);
    json_object_set_int_member (position, "line", 0);
    json_object_set_int_member (position, "character", 0);
    json_object_set_object_member (params, "position", position);
    json_node_take_object (node, params);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

static int problems_wanted;

static gboolean
has_problems (void)
{
    return told.diagnostics >= problems_wanted;
}

static gboolean
has_message (void)
{
    return told.message != NULL;
}

/* The whole life of a server: what is sent, in what order, what comes back */
static void
test_session (gconstpointer mode)
{
    const char *uri = "file:///tmp/a.c";
    answer_t answer = { 0 };
    char *methods, *response;

    new_client (mode, "{\"a\":{\"b\":42}}");
    // a document before the server runs is opened once it does
    lsp_client_doc_open (client, uri, "c", "int x;", 6);
    g_assert_cmpuint (lsp_client_request (client, "textDocument/hover", NULL, 0, on_answer, &answer,
                                          on_answer_destroyed),
                      ==, 0);
    // a request not taken: freed, never answered
    g_assert_cmpint (answer.destroyed, ==, 1);
    g_assert_cmpint (answer.calls, ==, 0);
    answer_clear (&answer);

    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_running);
    g_assert_cmpint (lsp_client_encoding (client), ==, LSP_ENCODING_UTF16);
    g_assert_true (lsp_client_supports (client, "hoverProvider"));
    g_assert_false (lsp_client_supports (client, "renameProvider"));
    g_assert_true (lsp_client_completion_trigger (client, "->"));
    g_assert_false (lsp_client_completion_trigger (client, ":"));
    g_assert_true (lsp_client_trigger (client, "signatureHelpProvider", ","));
    g_assert_false (lsp_client_trigger (client, "signatureHelpProvider", "."));
    problems_wanted = 1;
    run_until (has_problems);
    g_assert_cmpstr (told.last_problem, ==, "length 6");
    run_until (has_message);
    g_assert_cmpint (told.message_type, ==, 1);

    // the text before the request: the hover has it
    lsp_client_doc_change (client, uri, "int xy;", 7);
    lsp_client_doc_change (client, uri, "int xy;", 7);
    waited = &answer;
    g_assert_cmpuint (lsp_client_request (client, "textDocument/hover", document_params (uri), 0,
                                          on_answer, &answer, on_answer_destroyed),
                      >, 0);
    run_until (is_answered);
    g_assert_cmpint (answer.status, ==, LSP_REQUEST_OK);
    g_assert_cmpstr (answer.result, ==,
                     "{\"contents\":{\"kind\":\"plaintext\",\"value\":\"int xy;\"}}");
    g_assert_cmpint (answer.destroyed, ==, 1);
    answer_clear (&answer);
    // the problems of the old text are dropped
    problems_wanted = 2;
    run_until (has_problems);
    g_assert_cmpstr (told.last_problem, ==, "length 7");
    g_assert_cmpint (lsp_client_doc_version (client, uri), ==, 2);

    // an error
    g_assert_cmpuint (
        lsp_client_request (client, "test/error", NULL, 0, on_answer, &answer, on_answer_destroyed),
        >, 0);
    run_until (is_answered);
    g_assert_cmpint (answer.status, ==, LSP_REQUEST_ERROR);
    g_assert_cmpstr (answer.error, ==, "it went wrong");
    answer_clear (&answer);

    // a request given up: once, at once, and the server told
    {
        const guint64 id = lsp_client_request (client, "test/slow", NULL, 0, on_answer, &answer,
                                               on_answer_destroyed);

        g_assert_cmpuint (id, >, 0);
        lsp_client_cancel (client, id);
        g_assert_cmpint (answer.calls, ==, 1);
        g_assert_cmpint (answer.status, ==, LSP_REQUEST_CANCELLED);
        g_assert_cmpint (answer.destroyed, ==, 1);
        lsp_client_cancel (client, id);
        g_assert_cmpint (answer.calls, ==, 1);
        answer_clear (&answer);
    }
    // and one that takes too long
    g_assert_cmpuint (lsp_client_request (client, "test/slow", NULL, 200, on_answer, &answer,
                                          on_answer_destroyed),
                      >, 0);
    run_until (is_answered);
    g_assert_cmpint (answer.status, ==, LSP_REQUEST_TIMEOUT);
    g_assert_cmpint (answer.destroyed, ==, 1);
    answer_clear (&answer);

    lsp_client_doc_save (client, uri, "int xyz;", 8);
    lsp_client_doc_close (client, uri);
    g_assert_false (lsp_client_has_doc (client, uri));
    lsp_client_stop (client);
    g_assert_cmpint (lsp_client_state (client), ==, LSP_CLIENT_STOPPING);
    run_until (is_down);
    g_assert_cmpint (lsp_client_state (client), ==, LSP_CLIENT_STOPPED);
    g_assert_null (lsp_client_reason (client));
    run_until (is_reaped);

    methods = server_methods ();
    g_assert_cmpstr (methods, ==,
                     "initialize,initialized,workspace/didChangeConfiguration,"
                     "textDocument/didOpen,response,response,textDocument/didChange,"
                     "textDocument/hover,test/error,test/slow,$/cancelRequest,test/slow,"
                     "$/cancelRequest,textDocument/didChange,textDocument/didSave,"
                     "textDocument/didClose,shutdown,exit");
    g_free (methods);
    // what the server asked
    response = server_response ("\"cfg\"");
    g_assert_cmpstr (response, ==, "{\"id\":\"cfg\",\"result\":[42,null,{\"a\":{\"b\":42}}]}");
    g_free (response);
    response = server_response ("7");
    g_assert_nonnull (strstr (response, "-32601"));
    g_free (response);
    free_client ();
}

/* --------------------------------------------------------------------------------------------- */

static void
test_utf8 (void)
{
    new_client ("utf8", "null");
    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_running);
    g_assert_cmpint (lsp_client_encoding (client), ==, LSP_ENCODING_UTF8);
    // no settings: no configuration sent
    g_assert_false (server_has ("didChangeConfiguration"));
    free_client ();
}

/* --------------------------------------------------------------------------------------------- */

/* "ascii_text": "sql": the letters not of ASCII of the comments and the strings sent as '_', the
   positions in UTF-16 alone; a server that counts in UTF-8 all the same gets the text as it is */
static void
test_ascii_sql (gconstpointer mode)
{
    const char *uri = "file:///tmp/a.sql";
    const char *text = "select '\xd1\x89' -- \xd1\x8f\n";
    const gboolean utf8 = strcmp (mode, "utf8") == 0;
    answer_t answer = { 0 };
    char *log = NULL;

    new_client (mode, "null,\"ascii_text\":\"sql\"");
    lsp_client_doc_open (client, uri, "sql", text, strlen (text));
    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_running);
    waited = &answer;
    g_assert_cmpuint (lsp_client_request (client, "textDocument/hover", document_params (uri), 0,
                                          on_answer, &answer, on_answer_destroyed),
                      >, 0);
    run_until (is_answered);
    g_assert_cmpint (answer.status, ==, LSP_REQUEST_OK);
    // the text the server has, in JSON: its newline escaped
    g_assert_nonnull (
        strstr (answer.result, utf8 ? "select '\xd1\x89' -- \xd1\x8f" : "select '_' -- _"));
    answer_clear (&answer);
    g_assert_true (g_file_get_contents (log_path, &log, NULL, NULL));
    g_assert_nonnull (strstr (log, "\"positionEncodings\": [\"utf-16\"]"));
    g_free (log);
    free_client ();
}

/* --------------------------------------------------------------------------------------------- */

/* The server goes: the client fails with a reason, its requests closed, its documents kept */
static void
test_failures (gconstpointer mode)
{
    new_client (mode, "null");
    lsp_client_doc_open (client, "file:///tmp/b.c", "c", "x", 1);
    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_down);
    g_assert_cmpint (lsp_client_state (client), ==, LSP_CLIENT_FAILED);
    g_assert_nonnull (lsp_client_reason (client));
    if (strcmp (mode, "garbage") == 0)
        g_assert_cmpstr (lsp_client_reason (client), ==, "the server speaks no protocol");
    g_assert_cmpuint (lsp_client_documents (client), ==, 1);
    g_assert_cmpint (lsp_client_doc_version (client, "file:///tmp/b.c"), ==, 0);
    g_assert_cmpuint (lsp_client_pending (client), ==, 0);
    // the owner was told
    g_assert_cmpint (told.state, ==, LSP_CLIENT_FAILED);
    free_client ();
}

/* --------------------------------------------------------------------------------------------- */

static void
test_too_deep (void)
{
    answer_t answer = { 0 };

    new_client ("deep", "null");
    lsp_client_doc_open (client, "file:///tmp/c.c", "c", "x", 1);
    // an empty file, a new one, is opened too: its text is no NULL
    lsp_client_doc_open (client, "file:///tmp/empty.c", "c", "", 0);
    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_running);
    waited = &answer;
    g_assert_cmpuint (lsp_client_request (client, "textDocument/hover",
                                          document_params ("file:///tmp/c.c"), 0, on_answer,
                                          &answer, on_answer_destroyed),
                      >, 0);
    run_until (is_answered);
    g_assert_cmpint (answer.status, ==, LSP_REQUEST_CLOSED);
    g_assert_cmpint (answer.destroyed, ==, 1);
    g_assert_cmpint (lsp_client_state (client), ==, LSP_CLIENT_FAILED);
    g_assert_cmpstr (lsp_client_reason (client), ==, "a message nested too deep");
    answer_clear (&answer);
    free_client ();
}

/* --------------------------------------------------------------------------------------------- */

/* A server that does not stop is stopped all the same, and killed */
static void
test_hang (void)
{
    GPid pid;

    new_client ("hang", "null");
    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_running);
    pid = lsp_client_pid (client);
    lsp_client_stop (client);
    run_until (is_down);
    g_assert_cmpint (lsp_client_state (client), ==, LSP_CLIENT_STOPPED);
    g_assert_cmpstr (lsp_client_reason (client), ==, "the server did not stop in time");
    run_until (is_reaped);
    g_assert_cmpint (kill (pid, 0), !=, 0);
    free_client ();
}

/* --------------------------------------------------------------------------------------------- */

/* The editor ends: the servers are told, and none is left */
static void
test_quit (void)
{
    GPid pids[2];
    lsp_client_t *other;
    char *other_log, *hang_log;
    int i;

    new_client ("normal", "null");
    other = client;
    g_assert_true (lsp_client_start (other, NULL));
    run_until (is_running);
    pids[0] = lsp_client_pid (other);
    other_log = log_path;
    log_path = NULL;
    new_client ("hang", "null");
    g_assert_true (lsp_client_start (client, NULL));
    run_until (is_running);
    pids[1] = lsp_client_pid (client);

    lsp_client_quit (other);
    lsp_client_quit (client);
    lsp_client_reap_all (300);
    for (i = 0; i < 2; i++)
        g_assert_cmpint (kill (pids[i], 0), !=, 0);
    // the one that ends as asked has had the whole goodbye
    hang_log = log_path;
    log_path = other_log;
    g_assert_true (server_has ("shutdown,exit"));
    (void) g_remove (other_log);
    g_free (other_log);
    log_path = hang_log;
    lsp_client_free (other);
    lsp_client_free (client);
    client = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The interpreter of the scripts the tests run, the one meson runs with; NULL when there is none */
static char *
test_python (void)
{
    // a full path, or python3 to be found in PATH when meson could not say
    return g_find_program_in_path (TEST_PYTHON);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_missing (void)
{
    lsp_config_t *config = lsp_config_parse (
        "{\"backends\":[{\"id\":\"none\",\"command\":[\"/no/such/server\"]}]}", -1, NULL);
    GError *error = NULL;

    told_reset ();
    client = lsp_client_new (g_ptr_array_index (config->backends, 0), "/tmp", &handlers, NULL);
    lsp_config_free (config);
    g_assert_false (lsp_client_start (client, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_cmpint (lsp_client_state (client), ==, LSP_CLIENT_FAILED);
    g_assert_nonnull (lsp_client_reason (client));
    lsp_client_free (client);
    client = NULL;
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    int result;

    g_test_init (&argc, &argv, NULL);
    python = test_python ();
    if (python == NULL)
        g_test_add_func ("/lsp/client-session", test_no_python);
    else
    {
        g_test_add_data_func ("/lsp/client-session", "normal", test_session);
        g_test_add_data_func ("/lsp/client-session-split", "split", test_session);
        g_test_add_func ("/lsp/client-utf8", test_utf8);
        g_test_add_data_func ("/lsp/client-ascii-sql", "normal", test_ascii_sql);
        g_test_add_data_func ("/lsp/client-ascii-sql-utf8", "utf8", test_ascii_sql);
        g_test_add_data_func ("/lsp/client-crash", "crash", test_failures);
        g_test_add_data_func ("/lsp/client-garbage", "garbage", test_failures);
        g_test_add_func ("/lsp/client-too-deep", test_too_deep);
        g_test_add_func ("/lsp/client-hang", test_hang);
        g_test_add_func ("/lsp/client-quit", test_quit);
    }
    g_test_add_func ("/lsp/client-missing", test_missing);
    result = g_test_run ();
    g_free (python);
    if (log_path != NULL)
        (void) g_remove (log_path);
    g_free (log_path);
    return result;
}
