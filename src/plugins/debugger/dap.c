/*
   A client of the Debug Adapter Protocol: the messages, the transport, the requests and their
   responses.

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
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/tty/key.h"

#include "dap.h"

/* a message bigger than that is a broken stream */
#define DAP_MAX_MESSAGE (64 * 1024 * 1024)

/* --------------------------------------------------------------------------------------------- */
/* The messages of a stream of bytes */
/* --------------------------------------------------------------------------------------------- */

struct dap_reader_t
{
    GString *buffer;
};

dap_reader_t *
dap_reader_new (void)
{
    dap_reader_t *reader = g_new0 (dap_reader_t, 1);

    reader->buffer = g_string_new (NULL);
    return reader;
}

/* --------------------------------------------------------------------------------------------- */

/* The length the header says, -1 when it says none, 0 when the header is not all there yet;
   @header_len is that of the header with its empty line */
static gssize
dap_reader_header (const GString *buffer, gsize *header_len)
{
    const char *end = g_strstr_len (buffer->str, (gssize) buffer->len, "\r\n\r\n");
    gssize length = -1;
    const char *line;

    if (end == NULL)
        // a header is a few short lines: one that long is no header
        return buffer->len > 4096 ? -1 : 0;
    *header_len = (gsize) (end - buffer->str) + 4;
    for (line = buffer->str; line < end;)
    {
        const char *eol = strstr (line, "\r\n");

        if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
        {
            char *digits_end = NULL;
            const gint64 n = g_ascii_strtoll (line + 15, &digits_end, 10);

            if (digits_end != line + 15 && n >= 0 && n <= DAP_MAX_MESSAGE)
                length = (gssize) n;
        }
        line = eol + 2;
    }
    return length;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_reader_feed (dap_reader_t *reader, const char *bytes, gsize len, dap_message_fn fn, void *data)
{
    g_string_append_len (reader->buffer, bytes, (gssize) len);
    while (reader->buffer->len > 0)
    {
        gsize header_len = 0;
        const gssize length = dap_reader_header (reader->buffer, &header_len);
        char *message;

        if (length < 0)
        {
            g_string_truncate (reader->buffer, 0);
            return FALSE;
        }
        if (length == 0 && header_len == 0)
            return TRUE;  // the header is not all there yet
        if (reader->buffer->len < header_len + (gsize) length)
            return TRUE;  // nor the body
        // taken out of the buffer first: @fn may close what reads, and more may come
        message = g_strndup (reader->buffer->str + header_len, (gsize) length);
        g_string_erase (reader->buffer, 0, (gssize) (header_len + (gsize) length));
        fn (message, (gsize) length, data);
        g_free (message);
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
dap_reader_free (dap_reader_t *reader)
{
    if (reader == NULL)
        return;
    g_string_free (reader->buffer, TRUE);
    g_free (reader);
}

/* --------------------------------------------------------------------------------------------- */

char *
dap_message_frame (const char *json, gsize json_len, gsize *len)
{
    GString *out = g_string_sized_new (json_len + 32);

    g_string_append_printf (out, "Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n", json_len);
    g_string_append_len (out, json, (gssize) json_len);
    *len = out->len;
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/* The members of a JSON object */
/* --------------------------------------------------------------------------------------------- */

static JsonNode *
dap_member (JsonObject *object, const char *name, JsonNodeType type)
{
    JsonNode *node;

    if (object == NULL || !json_object_has_member (object, name))
        return NULL;
    node = json_object_get_member (object, name);
    return node != NULL && JSON_NODE_TYPE (node) == type ? node : NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
dap_string (JsonObject *object, const char *name)
{
    JsonNode *node = dap_member (object, name, JSON_NODE_VALUE);

    return node != NULL && json_node_get_value_type (node) == G_TYPE_STRING
        ? json_node_get_string (node)
        : NULL;
}

/* --------------------------------------------------------------------------------------------- */

gint64
dap_int (JsonObject *object, const char *name, gint64 fallback)
{
    JsonNode *node = dap_member (object, name, JSON_NODE_VALUE);

    if (node == NULL)
        return fallback;
    if (json_node_get_value_type (node) == G_TYPE_INT64)
        return json_node_get_int (node);
    if (json_node_get_value_type (node) == G_TYPE_DOUBLE)
    {
        const gdouble value = json_node_get_double (node);

        return (gint64) value;
    }
    return fallback;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_bool (JsonObject *object, const char *name, gboolean fallback)
{
    JsonNode *node = dap_member (object, name, JSON_NODE_VALUE);

    return node != NULL && json_node_get_value_type (node) == G_TYPE_BOOLEAN
        ? json_node_get_boolean (node)
        : fallback;
}

/* --------------------------------------------------------------------------------------------- */

JsonObject *
dap_object (JsonObject *object, const char *name)
{
    JsonNode *node = dap_member (object, name, JSON_NODE_OBJECT);

    return node != NULL ? json_node_get_object (node) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

JsonArray *
dap_array (JsonObject *object, const char *name)
{
    JsonNode *node = dap_member (object, name, JSON_NODE_ARRAY);

    return node != NULL ? json_node_get_array (node) : NULL;
}

/* --------------------------------------------------------------------------------------------- */
/* The client */
/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    guint seq;
    dap_response_fn fn;
    void *data;
    GDestroyNotify free_data;
} dap_pending_t;

struct dap_client_t
{
    const dap_handlers_t *handlers;
    void *owner;
    GPid pid;
    int input;   // what goes to the adapter
    int output;  // what comes from it: its stdout, or the socket
    int errors;  // its stderr
    int said;    // its stdout, when it speaks on a socket
    dap_reader_t *reader;
    guint next_seq;
    GPtrArray *pending;  // dap_pending_t
    char *host;
    int port;  // of the socket, 0 on stdin and stdout
};

/* --------------------------------------------------------------------------------------------- */

static void
dap_pending_free (gpointer p)
{
    dap_pending_t *pending = (dap_pending_t *) p;

    if (pending->free_data != NULL)
        pending->free_data (pending->data);
    g_free (pending);
}

/* --------------------------------------------------------------------------------------------- */

dap_client_t *
dap_client_new (const dap_handlers_t *handlers, void *owner)
{
    dap_client_t *client = g_new0 (dap_client_t, 1);

    client->handlers = handlers;
    client->owner = owner;
    client->input = client->output = client->errors = client->said = -1;
    client->reader = dap_reader_new ();
    client->pending = g_ptr_array_new_with_free_func (dap_pending_free);
    return client;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a child has ended, left to be reaped */
static gboolean
dap_exited (GPid pid)
{
    siginfo_t info;

    memset (&info, 0, sizeof (info));
    return waitid (P_PID, (id_t) pid, &info, WEXITED | WNOHANG | WNOWAIT) == 0
        && info.si_pid == pid;
}

/* --------------------------------------------------------------------------------------------- */

/* The channels closed and the adapter waited for: SIGTERM after @wait_ms, SIGKILL after more */
static void
dap_client_shut (dap_client_t *client, int wait_ms)
{
    int waited;

    if (client->output >= 0)
    {
        delete_select_channel (client->output);
        if (client->output != client->input)
            close (client->output);
    }
    if (client->errors >= 0)
    {
        delete_select_channel (client->errors);
        close (client->errors);
    }
    if (client->said >= 0)
    {
        delete_select_channel (client->said);
        close (client->said);
    }
    if (client->input >= 0)
        close (client->input);
    client->input = client->output = client->errors = client->said = -1;
    if (client->pid == 0)
        return;
    /* the adapter leads the group of its session: what it has started goes with it (the node of
       js-debug), signalled while the adapter is not reaped yet, its number still the group's */
    for (waited = 0; waited < wait_ms && !dap_exited (client->pid); waited += 10)
        g_usleep (10000);
    (void) kill (-client->pid, SIGTERM);
    for (waited = 0; waited < 500 && !dap_exited (client->pid); waited += 10)
        g_usleep (10000);
    if (!dap_exited (client->pid))
        (void) kill (-client->pid, SIGKILL);
    (void) waitpid (client->pid, NULL, 0);
    g_spawn_close_pid (client->pid);
    client->pid = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The adapter has gone, or speaks no protocol: all is closed, and the owner told */
static void
dap_client_lost (dap_client_t *client, const char *why)
{
    if (client->output < 0 && client->pid == 0)
        return;
    dap_client_shut (client, 0);
    g_ptr_array_set_size (client->pending, 0);
    if (client->handlers->closed != NULL)
        client->handlers->closed (client->owner, client, why);
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_client_response (dap_client_t *client, JsonObject *message)
{
    const gint64 seq = dap_int (message, "request_seq", -1);
    dap_pending_t *pending = NULL;
    guint i;

    for (i = 0; i < client->pending->len && pending == NULL; i++)
        if (((dap_pending_t *) g_ptr_array_index (client->pending, i))->seq == seq)
            pending = g_ptr_array_steal_index (client->pending, i);
    if (pending == NULL)
        return;
    if (pending->fn != NULL)
        pending->fn (client->owner, dap_bool (message, "success", FALSE),
                     dap_string (message, "message"), dap_object (message, "body"), pending->data);
    dap_pending_free (pending);
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_client_message (const char *json, gsize len, void *data)
{
    dap_client_t *client = (dap_client_t *) data;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *message;
    const char *type;

    // a message after the adapter was closed is of no one
    if (client->output < 0)
        return;
    parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json, (gssize) len, NULL)
        || (root = json_parser_get_root (parser)) == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    {
        g_object_unref (parser);
        return;
    }
    message = json_node_get_object (root);
    type = dap_string (message, "type");
    if (g_strcmp0 (type, "response") == 0)
        dap_client_response (client, message);
    else if (g_strcmp0 (type, "event") == 0 && dap_string (message, "event") != NULL)
    {
        if (client->handlers->event != NULL)
            client->handlers->event (client->owner, client, dap_string (message, "event"),
                                     dap_object (message, "body"));
    }
    else if (g_strcmp0 (type, "request") == 0 && dap_string (message, "command") != NULL)
    {
        if (client->handlers->request != NULL)
            client->handlers->request (client->owner, client, dap_int (message, "seq", 0),
                                       dap_string (message, "command"),
                                       dap_object (message, "arguments"));
        else
            (void) dap_client_respond (client, dap_int (message, "seq", 0),
                                       dap_string (message, "command"), FALSE, _ ("Not supported."),
                                       NULL);
    }
    g_object_unref (parser);
}

/* --------------------------------------------------------------------------------------------- */

static int
dap_client_read (int fd, void *data)
{
    dap_client_t *client = (dap_client_t *) data;
    char buf[16384];
    ssize_t n;

    n = read (fd, buf, sizeof (buf));
    if (n > 0)
    {
        if (!dap_reader_feed (client->reader, buf, (gsize) n, dap_client_message, client))
            dap_client_lost (client, _ ("The debug adapter does not speak the protocol."));
        if (client->handlers->flush != NULL)
            client->handlers->flush (client->owner);
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
        dap_client_lost (client, NULL);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
dap_client_read_text (int fd, void *data)
{
    dap_client_t *client = (dap_client_t *) data;
    char buf[4096];
    ssize_t n = read (fd, buf, sizeof (buf) - 1);

    if (n > 0)
    {
        buf[n] = '\0';
        if (client->handlers->text != NULL)
            client->handlers->text (client->owner, buf);
        if (client->handlers->flush != NULL)
            client->handlers->flush (client->owner);
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
    {
        delete_select_channel (fd);
        close (fd);
        if (fd == client->errors)
            client->errors = -1;
        if (fd == client->said)
            client->said = -1;
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
dap_nonblocking (int fd)
{
    (void) fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
}

/* --------------------------------------------------------------------------------------------- */

/* The channels watched, the client open */
static void
dap_client_watch (dap_client_t *client)
{
    // what the adapter before left of a message is not the start of this one's
    dap_reader_free (client->reader);
    client->reader = dap_reader_new ();
    client->next_seq = 0;
    dap_nonblocking (client->output);
    add_select_channel (client->output, dap_client_read, client);
    if (client->errors >= 0)
    {
        dap_nonblocking (client->errors);
        add_select_channel (client->errors, dap_client_read_text, client);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The adapter in a session of its own, with no controlling terminal: neither it nor the
   program it starts takes the terminal of the editor, its keys or its foreground (the launcher of
   debugpy does) */
static void
dap_child_setup (gpointer data)
{
    (void) data;
    (void) setsid ();
#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_PDEATHSIG)
    // no hangup of the terminal reaches it now: it ends with the editor all the same
    (void) prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_client_spawn (dap_client_t *client, char **argv, const char *directory, GError **error)
{
    if (dap_client_alive (client))
        dap_client_close (client, 0);
    client->port = 0;
    if (!g_spawn_async_with_pipes (
            directory, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, dap_child_setup,
            NULL, &client->pid, &client->input, &client->output, &client->errors, error))
        return FALSE;
    dap_client_watch (client);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A socket connected to @host:@port, -1 when it cannot be */
static int
dap_connect_socket (const char *host, int port, GError **error)
{
    struct addrinfo hints = { 0 }, *found = NULL, *a;
    char service[16];
    int fd = -1, rc;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    g_snprintf (service, sizeof (service), "%d", port);
    rc = getaddrinfo (host != NULL && *host != '\0' ? host : "127.0.0.1", service, &hints, &found);
    if (rc != 0)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", gai_strerror (rc));
        return -1;
    }
    for (a = found; a != NULL && fd < 0; a = a->ai_next)
    {
        fd = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd >= 0 && connect (fd, a->ai_addr, a->ai_addrlen) != 0)
        {
            close (fd);
            fd = -1;
        }
    }
    if (fd < 0)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     _ ("Could not connect to the debug adapter at %s:%d: %s"),
                     host != NULL ? host : "127.0.0.1", port, g_strerror (errno));
    freeaddrinfo (found);
    return fd;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_client_connect (dap_client_t *client, const char *host, int port, GError **error)
{
    int fd;

    if (dap_client_alive (client))
        dap_client_close (client, 0);
    fd = dap_connect_socket (host, port, error);
    if (fd < 0)
        return FALSE;
    client->input = client->output = fd;
    g_free (client->host);
    client->host = g_strdup (host);
    client->port = port;
    dap_client_watch (client);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_client_spawn_listening (dap_client_t *client, char **argv, const char *directory,
                            const char *host, const char *pattern, GError **error)
{
    GRegex *regex;
    GString *said = g_string_new (NULL);
    int in = -1, out = -1, port = 0, waited, fd = -1;
    gboolean ok = FALSE;

    regex = g_regex_new (pattern, G_REGEX_MULTILINE, 0, error);
    if (regex == NULL)
    {
        g_string_free (said, TRUE);
        return FALSE;
    }
    if (dap_client_alive (client))
        dap_client_close (client, 0);
    // its stdin is of no use, and not the terminal of the editor either
    if (!g_spawn_async_with_pipes (directory, argv, NULL,
                                   G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, dap_child_setup,
                                   NULL, &client->pid, &in, &out, &client->errors, error))
        goto done;
    close (in);

    // the port it says, in ten seconds at most
    for (waited = 0; waited < 10000 && port == 0; waited += 100)
    {
        struct pollfd p = { out, POLLIN, 0 };
        GMatchInfo *match = NULL;

        if (poll (&p, 1, 100) > 0)
        {
            char buf[1024];
            const ssize_t n = read (out, buf, sizeof (buf));

            if (n <= 0)
                break;
            g_string_append_len (said, buf, n);
        }
        if (g_regex_match (regex, said->str, 0, &match))
        {
            char *digits = g_match_info_fetch (match, 1);

            port = digits != NULL ? atoi (digits) : 0;
            g_free (digits);
        }
        g_match_info_free (match);
    }
    if (port <= 0 || port > 65535)
    {
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                     _ ("The debug adapter did not say the port it listens on:\n%s"), said->str);
        goto done;
    }
    // it may say so a moment before it listens
    for (waited = 0; waited < 3000 && fd < 0; waited += 100)
    {
        g_clear_error (error);
        fd = dap_connect_socket (host, port, error);
        if (fd < 0)
            g_usleep (100000);
    }
    if (fd < 0)
        goto done;
    g_clear_error (error);
    client->input = client->output = fd;
    g_free (client->host);
    client->host = g_strdup (host);
    client->port = port;
    dap_client_watch (client);
    // what it writes after is text to read, as its stderr is
    client->said = out;
    out = -1;
    dap_nonblocking (client->said);
    add_select_channel (client->said, dap_client_read_text, client);
    ok = TRUE;

done:
    if (!ok)
    {
        if (out >= 0)
            close (out);
        dap_client_shut (client, 0);
    }
    g_regex_unref (regex);
    g_string_free (said, TRUE);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/* A message to the adapter */
static gboolean
dap_client_send (dap_client_t *client, JsonObject *message)
{
    JsonNode *root = json_node_new (JSON_NODE_OBJECT);
    JsonGenerator *generator = json_generator_new ();
    struct sigaction ignore = { 0 }, previous;
    char *json, *framed;
    gsize json_len, len, sent = 0;
    gboolean ok = TRUE;

    if (client->input < 0)
    {
        json_object_unref (message);
        g_object_unref (generator);
        json_node_free (root);
        return FALSE;
    }
    json_node_take_object (root, message);
    json_generator_set_root (generator, root);
    json = json_generator_to_data (generator, &json_len);
    framed = dap_message_frame (json, json_len, &len);
    // an adapter that has crashed can close its input between two writes
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    (void) sigaction (SIGPIPE, &ignore, &previous);
    while (sent < len)
    {
        const ssize_t n = write (client->input, framed + sent, len - sent);

        if (n < 0 && errno == EINTR)
            continue;
        // a socket is read without blocking, and written so too: half a message is no message
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            struct pollfd p = { client->input, POLLOUT, 0 };

            if (poll (&p, 1, 5000) > 0)
                continue;
        }
        if (n <= 0)
        {
            ok = FALSE;
            break;
        }
        sent += (gsize) n;
    }
    (void) sigaction (SIGPIPE, &previous, NULL);
    g_free (framed);
    g_free (json);
    g_object_unref (generator);
    json_node_free (root);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

guint
dap_client_request (dap_client_t *client, const char *command, JsonNode *arguments,
                    dap_response_fn fn, void *data, GDestroyNotify free_data)
{
    JsonObject *message = json_object_new ();
    dap_pending_t *pending;

    pending = g_new0 (dap_pending_t, 1);
    pending->seq = ++client->next_seq;
    pending->fn = fn;
    pending->data = data;
    pending->free_data = free_data;
    json_object_set_int_member (message, "seq", pending->seq);
    json_object_set_string_member (message, "type", "request");
    json_object_set_string_member (message, "command", command);
    if (arguments != NULL)
        json_object_set_member (message, "arguments", arguments);
    if (!dap_client_send (client, message))
    {
        dap_pending_free (pending);
        return 0;
    }
    g_ptr_array_add (client->pending, pending);
    return pending->seq;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_client_respond (dap_client_t *client, gint64 request_seq, const char *command, gboolean success,
                    const char *message_text, JsonNode *body)
{
    JsonObject *message = json_object_new ();

    json_object_set_int_member (message, "seq", ++client->next_seq);
    json_object_set_string_member (message, "type", "response");
    json_object_set_int_member (message, "request_seq", request_seq);
    json_object_set_boolean_member (message, "success", success);
    json_object_set_string_member (message, "command", command != NULL ? command : "");
    if (message_text != NULL)
        json_object_set_string_member (message, "message", message_text);
    if (body != NULL)
        json_object_set_member (message, "body", body);
    return dap_client_send (client, message);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
dap_client_alive (const dap_client_t *client)
{
    return client != NULL && client->output >= 0;
}

/* --------------------------------------------------------------------------------------------- */

int
dap_client_port (const dap_client_t *client)
{
    return client->port;
}

/* --------------------------------------------------------------------------------------------- */

const char *
dap_client_host (const dap_client_t *client)
{
    return client->host;
}

/* --------------------------------------------------------------------------------------------- */

void
dap_client_cancel (dap_client_t *client)
{
    g_ptr_array_set_size (client->pending, 0);
}

/* --------------------------------------------------------------------------------------------- */

void
dap_client_close (dap_client_t *client, int wait_ms)
{
    if (client == NULL)
        return;
    dap_client_shut (client, wait_ms);
    g_ptr_array_set_size (client->pending, 0);
    g_string_truncate (client->reader->buffer, 0);
}

/* --------------------------------------------------------------------------------------------- */

void
dap_client_free (dap_client_t *client)
{
    if (client == NULL)
        return;
    dap_client_close (client, 0);
    dap_reader_free (client->reader);
    g_ptr_array_free (client->pending, TRUE);
    g_free (client->host);
    g_free (client);
}
