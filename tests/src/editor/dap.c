/*
   src/plugins/debugger - tests for the client of the Debug Adapter Protocol

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
#include "src/plugins/debugger/dap.h"

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

/* The select loop of the program, until @done or five seconds */
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

static GPtrArray *messages;

static void
collect (const char *json, gsize len, void *data)
{
    (void) data;
    g_assert_cmpuint (strlen (json), ==, len);
    g_ptr_array_add (messages, g_strdup (json));
}

static void
test_reader_split (void)
{
    const char *stream =
        "Content-Length: 7\r\n\r\n{\"a\":1}content-length:2\r\nX-Other: y\r\n\r\n[]";
    const gsize len = strlen (stream);
    gsize cut;

    // the stream torn at every byte gives the same two messages
    for (cut = 0; cut <= len; cut++)
    {
        dap_reader_t *reader = dap_reader_new ();

        messages = g_ptr_array_new_with_free_func (g_free);
        g_assert_true (dap_reader_feed (reader, stream, cut, collect, NULL));
        g_assert_true (dap_reader_feed (reader, stream + cut, len - cut, collect, NULL));
        g_assert_cmpuint (messages->len, ==, 2);
        g_assert_cmpstr (g_ptr_array_index (messages, 0), ==, "{\"a\":1}");
        g_assert_cmpstr (g_ptr_array_index (messages, 1), ==, "[]");
        g_ptr_array_free (messages, TRUE);
        dap_reader_free (reader);
    }
}

static void
test_reader_byte_by_byte (void)
{
    char *framed;
    gsize len, i;
    dap_reader_t *reader = dap_reader_new ();

    framed = dap_message_frame ("{\"seq\":1}", 9, &len);
    g_assert_cmpstr (framed, ==, "Content-Length: 9\r\n\r\n{\"seq\":1}");
    messages = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < len; i++)
        g_assert_true (dap_reader_feed (reader, framed + i, 1, collect, NULL));
    g_assert_cmpuint (messages->len, ==, 1);
    g_ptr_array_free (messages, TRUE);
    g_free (framed);
    dap_reader_free (reader);
}

static void
test_reader_broken (void)
{
    dap_reader_t *reader = dap_reader_new ();

    messages = g_ptr_array_new_with_free_func (g_free);
    // a header with no length is no header
    g_assert_false (dap_reader_feed (reader, "Content-Type: x\r\n\r\n{}", 21, collect, NULL));
    g_assert_cmpuint (messages->len, ==, 0);
    // and the reader is clean for what comes after
    g_assert_true (dap_reader_feed (reader, "Content-Length: 2\r\n\r\n{}", 23, collect, NULL));
    g_assert_cmpuint (messages->len, ==, 1);
    g_ptr_array_free (messages, TRUE);
    dap_reader_free (reader);
}

static void
test_members (void)
{
    JsonParser *parser = json_parser_new ();
    JsonObject *o;

    g_assert_true (json_parser_load_from_data (
        parser, "{\"s\":\"x\",\"i\":5,\"d\":2.0,\"b\":true,\"o\":{},\"a\":[1],\"n\":null}", -1,
        NULL));
    o = json_node_get_object (json_parser_get_root (parser));
    g_assert_cmpstr (dap_string (o, "s"), ==, "x");
    g_assert_null (dap_string (o, "i"));
    g_assert_null (dap_string (o, "missing"));
    g_assert_cmpint (dap_int (o, "i", -1), ==, 5);
    g_assert_cmpint (dap_int (o, "d", -1), ==, 2);
    g_assert_cmpint (dap_int (o, "s", -1), ==, -1);
    g_assert_true (dap_bool (o, "b", FALSE));
    g_assert_true (dap_bool (o, "n", TRUE));
    g_assert_nonnull (dap_object (o, "o"));
    g_assert_null (dap_object (o, "a"));
    g_assert_nonnull (dap_array (o, "a"));
    g_assert_null (dap_string (NULL, "s"));
    g_object_unref (parser);
}

/* --------------------------------------------------------------------------------------------- */
/* With the mock adapter */

typedef struct
{
    gboolean initialized;   // the event came
    gboolean capabilities;  // the response to initialize came, with them
    gboolean said;          // its words beside the protocol came
    gboolean closed;
    gboolean evaluated;
    gboolean refused;
} seen_t;

static seen_t seen;

static void
on_event (void *owner, dap_client_t *client, const char *event, JsonObject *body)
{
    (void) owner;
    (void) client;
    (void) body;
    if (strcmp (event, "initialized") == 0)
        seen.initialized = TRUE;
}

static void
on_text (void *owner, const char *text)
{
    (void) owner;
    (void) text;
    seen.said = TRUE;
}

static void
on_closed (void *owner, dap_client_t *client, const char *why)
{
    (void) owner;
    (void) client;
    (void) why;
    seen.closed = TRUE;
}

static const dap_handlers_t handlers = {
    .event = on_event,
    .text = on_text,
    .closed = on_closed,
};

static void
on_initialize (void *owner, gboolean success, const char *message, JsonObject *body, void *data)
{
    (void) owner;
    (void) message;
    g_assert_cmpstr ((const char *) data, ==, "mine");
    seen.capabilities = success && dap_bool (body, "supportsDisassembleRequest", FALSE);
}

static void
on_evaluate (void *owner, gboolean success, const char *message, JsonObject *body, void *data)
{
    (void) owner;
    (void) data;
    if (success)
        seen.evaluated = g_strcmp0 (dap_string (body, "result"), "<1+1>") == 0;
    else
        seen.refused = g_strcmp0 (message, "no symbol bad") == 0;
}

static JsonNode *
expression (const char *text)
{
    JsonObject *args = json_object_new ();
    JsonNode *node = json_node_new (JSON_NODE_OBJECT);

    json_object_set_string_member (args, "expression", text);
    json_node_take_object (node, args);
    return node;
}

/* The adapter answers, and goes when asked to */
static void
talk (dap_client_t *client)
{
    g_assert_cmpuint (
        dap_client_request (client, "initialize", NULL, on_initialize, g_strdup ("mine"), g_free),
        >, 0);
    run_until (&seen.initialized);
    g_assert_true (seen.capabilities);
    g_assert_true (seen.initialized);
    (void) dap_client_request (client, "evaluate", expression ("1+1"), on_evaluate, NULL, NULL);
    (void) dap_client_request (client, "evaluate", expression ("bad"), on_evaluate, NULL, NULL);
    run_until (&seen.refused);
    g_assert_true (seen.evaluated);
    g_assert_true (seen.refused);
    (void) dap_client_request (client, "disconnect", NULL, NULL, NULL, NULL);
    run_until (&seen.closed);
    g_assert_true (seen.closed);
    g_assert_false (dap_client_alive (client));
}

static void
test_stdio (gconstpointer split)
{
    char *python = g_find_program_in_path ("python3");
    char *argv[] = { python, (char *) MOCK_DAP, (char *) split, NULL };
    dap_client_t *client;
    GError *error = NULL;

    if (python == NULL)
    {
        g_test_skip ("python3 is unavailable");
        return;
    }
    memset (&seen, 0, sizeof (seen));
    client = dap_client_new (&handlers, NULL);
    g_assert_true (dap_client_spawn (client, argv, NULL, &error));
    g_assert_no_error (error);
    talk (client);
    dap_client_free (client);
    g_assert_cmpint (watched_count, ==, 0);
    g_free (python);
}

static void
test_tcp (void)
{
    char *python = g_find_program_in_path ("python3");
    char *argv[] = { python, (char *) MOCK_DAP, (char *) "--tcp", (char *) "0", NULL };
    dap_client_t *client;
    GError *error = NULL;

    if (python == NULL)
    {
        g_test_skip ("python3 is unavailable");
        return;
    }
    memset (&seen, 0, sizeof (seen));
    client = dap_client_new (&handlers, NULL);
    g_assert_true (dap_client_spawn_listening (client, argv, NULL, "127.0.0.1",
                                               "listening at [0-9.]+:([0-9]+)", &error));
    g_assert_no_error (error);
    talk (client);
    dap_client_free (client);
    g_assert_cmpint (watched_count, ==, 0);
    g_free (python);
}

static void
test_spawn_missing (void)
{
    char *argv[] = { (char *) "/nonexistent/adapter", NULL };
    dap_client_t *client = dap_client_new (&handlers, NULL);
    GError *error = NULL;

    g_assert_false (dap_client_spawn (client, argv, NULL, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_false (dap_client_alive (client));
    g_assert_cmpuint (dap_client_request (client, "initialize", NULL, NULL, NULL, NULL), ==, 0);
    dap_client_free (client);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/debugger/dap-reader-split", test_reader_split);
    g_test_add_func ("/debugger/dap-reader-byte-by-byte", test_reader_byte_by_byte);
    g_test_add_func ("/debugger/dap-reader-broken", test_reader_broken);
    g_test_add_func ("/debugger/dap-members", test_members);
    g_test_add_data_func ("/debugger/dap-stdio", NULL, test_stdio);
    g_test_add_data_func ("/debugger/dap-stdio-split", "--split", test_stdio);
    g_test_add_func ("/debugger/dap-tcp", test_tcp);
    g_test_add_func ("/debugger/dap-spawn-missing", test_spawn_missing);
    return g_test_run ();
}
