/*
   A client of one language server: its process, the messages, the requests and the documents.

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
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/tty/key.h"  // add_select_channel ()

#include "src/content-frame.h"

#include "lsp-client.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* JSON nested deeper than that is no message: the parser would go as deep on the stack */
#define LSP_MAX_DEPTH 256
/* the bytes waiting to be written to a server that does not read */
#define LSP_MAX_WRITING (32 * 1024 * 1024)

/* the error codes of JSON-RPC */
#define LSP_ERROR_METHOD_NOT_FOUND (-32601)
#define LSP_ERROR_INVALID_PARAMS   (-32602)

/*** file scope type declarations ****************************************************************/

typedef struct
{
    guint64 id;
    gint64 deadline;
    gboolean internal;  // the client's own: initialize, shutdown; no $/cancelRequest
    lsp_response_fn fn;
    void *data;
    GDestroyNotify destroy;
} lsp_pending_t;

typedef struct
{
    char *uri;
    char *language;
    GBytes *text;
    gint version;     // of the text the server has
    gboolean opened;  // didOpen sent to the server of the moment
} lsp_doc_t;

/* A process of a server stopped, waited for */
typedef struct
{
    GPid pid;
    gint64 term_at;  // SIGTERM then
    gint64 kill_at;  // SIGKILL then
    gboolean termed;
    int out, err;  // its output, closed once it has ended: what it writes then is no failure
} lsp_reaped_t;

struct lsp_client_t
{
    const lsp_client_handlers_t *handlers;
    void *owner;

    // what the client has of its backend
    char *backend_id;
    char **command;
    JsonObject *environment;
    JsonNode *initialization_options;
    JsonNode *settings;
    char *root;

    lsp_client_state_t state;
    gboolean state_changed;  // not told to the owner yet
    char *reason;

    GPid pid;
    int in, out, err;
    content_frame_reader_t *reader;
    GQueue *writing;  // GBytes, the frames to write
    gsize written;    // of the first of them
    gsize writing_bytes;
    gboolean close_in;  // the input of the server closed once all is written
    char *broken;       // a failure seen while writing, dealt with at the next tick
    GString *err_line;

    guint64 next_id;
    GPtrArray *pending;  // lsp_pending_t
    gint64 stop_deadline;

    JsonObject *capabilities;
    lsp_encoding_t encoding;
    int sync_change;  // 0 none, 1 full, 2 incremental: the full text all the same
    gboolean sync_open_close;
    gboolean sync_save;
    gboolean sync_save_text;

    GHashTable *docs;      // uri -> lsp_doc_t
    GHashTable *versions;  // uri -> the last version given in this process

    GQueue *log;
};

/*** forward declarations (file scope functions) *************************************************/

static void lsp_client_lost (lsp_client_t *client, const char *why);

/*** file scope variables ************************************************************************/

/* The capabilities of the client: what it does, and nothing more */
static const char lsp_client_capabilities_json[] =
    "{"
    "\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]},"
    "\"workspace\":{\"configuration\":true,\"workspaceFolders\":true,\"applyEdit\":false},"
    "\"window\":{\"workDoneProgress\":true},"
    "\"textDocument\":{"
    "\"synchronization\":{\"dynamicRegistration\":false,\"willSave\":false,"
    "\"willSaveWaitUntil\":false,\"didSave\":true},"
    "\"hover\":{\"dynamicRegistration\":false,\"contentFormat\":[\"markdown\",\"plaintext\"]},"
    "\"definition\":{\"dynamicRegistration\":false,\"linkSupport\":true},"
    "\"documentSymbol\":{\"dynamicRegistration\":false,"
    "\"hierarchicalDocumentSymbolSupport\":true},"
    "\"declaration\":{\"dynamicRegistration\":false,\"linkSupport\":true},"
    "\"references\":{\"dynamicRegistration\":false},"
    "\"signatureHelp\":{\"dynamicRegistration\":false,\"signatureInformation\":{"
    "\"documentationFormat\":[\"markdown\",\"plaintext\"],"
    "\"parameterInformation\":{\"labelOffsetSupport\":true},\"activeParameterSupport\":true}},"
    "\"completion\":{\"dynamicRegistration\":false,\"contextSupport\":true,"
    "\"completionItem\":{\"snippetSupport\":false,\"commitCharactersSupport\":false,"
    "\"documentationFormat\":[\"plaintext\"],\"insertReplaceSupport\":false}},"
    "\"publishDiagnostics\":{\"relatedInformation\":false,\"versionSupport\":true}"
    "}}";

static GArray *lsp_reaped = NULL;  // lsp_reaped_t

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* JSON */

static JsonNode *
lsp_member (JsonObject *object, const char *name)
{
    return object != NULL && json_object_has_member (object, name)
        ? json_object_get_member (object, name)
        : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
lsp_member_string (JsonObject *object, const char *name)
{
    JsonNode *node = lsp_member (object, name);

    return node != NULL && JSON_NODE_HOLDS_VALUE (node)
            && json_node_get_value_type (node) == G_TYPE_STRING
        ? json_node_get_string (node)
        : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static JsonObject *
lsp_member_object (JsonObject *object, const char *name)
{
    JsonNode *node = lsp_member (object, name);

    return node != NULL && JSON_NODE_HOLDS_OBJECT (node) ? json_node_get_object (node) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the member is there, an integer, put in @value */
static gboolean
lsp_member_int (JsonObject *object, const char *name, gint64 *value)
{
    JsonNode *node = lsp_member (object, name);

    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
        return FALSE;
    if (json_node_get_value_type (node) == G_TYPE_INT64)
    {
        *value = json_node_get_int (node);
        return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* A true member, an object counting as true */
static gboolean
lsp_member_true (JsonObject *object, const char *name)
{
    JsonNode *node = lsp_member (object, name);

    if (node == NULL || JSON_NODE_HOLDS_NULL (node))
        return FALSE;
    if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_BOOLEAN)
        return json_node_get_boolean (node);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A node of @object, which it takes */
static JsonNode *
lsp_object_node (JsonObject *object)
{
    JsonNode *node = json_node_new (JSON_NODE_OBJECT);

    json_node_take_object (node, object);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether JSON nests no deeper than LSP_MAX_DEPTH */
static gboolean
lsp_depth_ok (const char *json, gsize len)
{
    gboolean in_string = FALSE;
    int depth = 0;
    gsize i;

    for (i = 0; i < len; i++)
    {
        const char c = json[i];

        if (in_string)
        {
            if (c == '\\')
                i++;
            else if (c == '"')
                in_string = FALSE;
        }
        else if (c == '"')
            in_string = TRUE;
        else if (c == '[' || c == '{')
        {
            if (++depth > LSP_MAX_DEPTH)
                return FALSE;
        }
        else if (c == ']' || c == '}')
            depth--;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/* The log */

/* A line of the log: what the terminal would take as a control is shown as '?' */
static void
lsp_client_log_line (lsp_client_t *client, const char *text, gssize len)
{
    char *line = len < 0 ? g_strdup (text) : g_strndup (text, (gsize) len);
    char *p;

    for (p = line; *p != '\0'; p++)
        if ((unsigned char) *p < 0x20 || *p == 0x7f)
            *p = *p == '\t' ? ' ' : '?';
    g_queue_push_tail (client->log, line);
    while (g_queue_get_length (client->log) > LSP_LOG_LINES)
        g_free (g_queue_pop_head (client->log));
}

/* --------------------------------------------------------------------------------------------- */
/* The processes stopped */

static void
lsp_reap_add (GPid pid, gint64 term_at, gint64 kill_at, int out, int err)
{
    lsp_reaped_t reaped = { pid, term_at, kill_at, FALSE, out, err };

    if (lsp_reaped == NULL)
        lsp_reaped = g_array_new (FALSE, FALSE, sizeof (lsp_reaped_t));
    g_array_append_val (lsp_reaped, reaped);
}

/* --------------------------------------------------------------------------------------------- */
/* The pending requests */

static void
lsp_pending_end (lsp_client_t *client, lsp_pending_t *pending, lsp_request_status_t status,
                 JsonNode *result, JsonObject *error)
{
    if (pending->fn != NULL)
        pending->fn (client, status, result, error, pending->data);
    if (pending->destroy != NULL)
        pending->destroy (pending->data);
    g_free (pending);
}

/* --------------------------------------------------------------------------------------------- */

static lsp_pending_t *
lsp_pending_take (lsp_client_t *client, guint64 id)
{
    guint i;

    for (i = 0; i < client->pending->len; i++)
    {
        lsp_pending_t *pending = g_ptr_array_index (client->pending, i);

        if (pending->id == id)
            return g_ptr_array_steal_index (client->pending, i);
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* All the requests ended with @status; those a callback makes on the way are not */
static void
lsp_pending_end_all (lsp_client_t *client, lsp_request_status_t status)
{
    GPtrArray *ending = client->pending;
    guint i;

    client->pending = g_ptr_array_new ();
    for (i = 0; i < ending->len; i++)
        lsp_pending_end (client, g_ptr_array_index (ending, i), status, NULL, NULL);
    g_ptr_array_free (ending, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/* The transport */

static void
lsp_nonblocking (int fd)
{
    (void) fcntl (fd, F_SETFL, fcntl (fd, F_GETFL) | O_NONBLOCK);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_close_in (lsp_client_t *client)
{
    if (client->in >= 0)
        close (client->in);
    client->in = -1;
    client->close_in = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* What is waiting written, as much as the pipe takes */
static void
lsp_client_flush (lsp_client_t *client)
{
    struct sigaction ignore, previous;

    if (client->in < 0 || client->broken != NULL)
        return;
    // a server that has crashed closes its input between two writes
    memset (&ignore, 0, sizeof (ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    (void) sigaction (SIGPIPE, &ignore, &previous);
    while (!g_queue_is_empty (client->writing))
    {
        GBytes *frame = g_queue_peek_head (client->writing);
        gsize size;
        const char *data = g_bytes_get_data (frame, &size);
        const ssize_t n = write (client->in, data + client->written, size - client->written);

        if (n > 0)
        {
            client->written += (gsize) n;
            client->writing_bytes -= (gsize) n;
            if (client->written == size)
            {
                g_bytes_unref (g_queue_pop_head (client->writing));
                client->written = 0;
            }
        }
        else if (n < 0 && errno == EINTR)
            continue;
        else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        else
        {
            client->broken = g_strdup_printf ("cannot write to the server: %s",
                                              n < 0 ? g_strerror (errno) : "nothing written");
            break;
        }
    }
    (void) sigaction (SIGPIPE, &previous, NULL);
    if (g_queue_is_empty (client->writing) && client->close_in)
        lsp_client_close_in (client);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_drop_writing (lsp_client_t *client)
{
    g_queue_free_full (client->writing, (GDestroyNotify) g_bytes_unref);
    client->writing = g_queue_new ();
    client->written = 0;
    client->writing_bytes = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* A message to the server, @message taken; FALSE when it cannot go */
static gboolean
lsp_client_send (lsp_client_t *client, JsonObject *message)
{
    JsonNode *root;
    JsonGenerator *generator;
    char *json, *framed;
    gsize json_len, len;

    if (client->in < 0 || client->close_in || client->broken != NULL)
    {
        json_object_unref (message);
        return FALSE;
    }
    json_object_set_string_member (message, "jsonrpc", "2.0");
    root = json_node_new (JSON_NODE_OBJECT);
    json_node_take_object (root, message);
    generator = json_generator_new ();
    json_generator_set_root (generator, root);
    json = json_generator_to_data (generator, &json_len);
    g_object_unref (generator);
    json_node_unref (root);
    framed = content_frame_wrap (json, json_len, &len);
    g_free (json);
    if (client->writing_bytes + len > LSP_MAX_WRITING)
    {
        g_free (framed);
        client->broken = g_strdup ("the server does not read what it is sent");
        return FALSE;
    }
    g_queue_push_tail (client->writing, g_bytes_new_take (framed, len));
    client->writing_bytes += len;
    lsp_client_flush (client);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A request to the server, whatever the state; its id, 0 when it cannot go */
static guint64
lsp_client_send_request (lsp_client_t *client, const char *method, JsonNode *params,
                         guint timeout_ms, gboolean internal, lsp_response_fn fn, void *data,
                         GDestroyNotify destroy)
{
    JsonObject *message = json_object_new ();
    lsp_pending_t *pending;
    const guint64 id = ++client->next_id;

    json_object_set_int_member (message, "id", (gint64) id);
    json_object_set_string_member (message, "method", method);
    if (params != NULL)
        json_object_set_member (message, "params", params);
    if (!lsp_client_send (client, message))
    {
        if (destroy != NULL)
            destroy (data);
        return 0;
    }
    pending = g_new0 (lsp_pending_t, 1);
    pending->id = id;
    pending->deadline = g_get_monotonic_time () + (gint64) timeout_ms * 1000;
    pending->internal = internal;
    pending->fn = fn;
    pending->data = data;
    pending->destroy = destroy;
    g_ptr_array_add (client->pending, pending);
    return id;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_client_send_notification (lsp_client_t *client, const char *method, JsonNode *params)
{
    JsonObject *message = json_object_new ();

    json_object_set_string_member (message, "method", method);
    if (params != NULL)
        json_object_set_member (message, "params", params);
    return lsp_client_send (client, message);
}

/* --------------------------------------------------------------------------------------------- */

/* The answer to the request @id of the server: @result taken, or an error when @code is not 0 */
static void
lsp_client_respond (lsp_client_t *client, JsonNode *id, JsonNode *result, int code,
                    const char *message)
{
    JsonObject *response = json_object_new ();

    json_object_set_member (response, "id", json_node_copy (id));
    if (code != 0)
    {
        JsonObject *error = json_object_new ();

        json_object_set_int_member (error, "code", code);
        json_object_set_string_member (error, "message", message);
        json_object_set_object_member (response, "error", error);
        if (result != NULL)
            json_node_unref (result);
    }
    else
        json_object_set_member (response, "result",
                                result != NULL ? result : json_node_new (JSON_NODE_NULL));
    (void) lsp_client_send (client, response);
}

/* --------------------------------------------------------------------------------------------- */
/* The end of a process */

/* The pipes closed, the process left to the reaper, the requests ended: the state is @state now,
   for @reason.  The process gets SIGTERM after @term_ms. */
static void
lsp_client_teardown (lsp_client_t *client, lsp_client_state_t state, const char *reason,
                     int term_ms)
{
    const gint64 now = g_get_monotonic_time ();
    GHashTableIter iter;
    gpointer doc;

    if (client->out >= 0)
        delete_select_channel (client->out);
    if (client->err >= 0)
        delete_select_channel (client->err);
    if (client->pid == 0)
    {
        if (client->out >= 0)
            close (client->out);
        if (client->err >= 0)
            close (client->err);
        client->out = client->err = -1;
    }
    if (client->err_line->len > 0)
        lsp_client_log_line (client, client->err_line->str, (gssize) client->err_line->len);
    g_string_truncate (client->err_line, 0);
    lsp_client_close_in (client);
    content_frame_reader_reset (client->reader);
    lsp_client_drop_writing (client);
    g_clear_pointer (&client->broken, g_free);
    if (client->pid != 0)
    {
        lsp_reap_add (client->pid, now + (gint64) term_ms * 1000,
                      now + (gint64) (term_ms + 500) * 1000, client->out, client->err);
        client->pid = 0;
        client->out = client->err = -1;
    }
    if (client->capabilities != NULL)
        json_object_unref (client->capabilities);
    client->capabilities = NULL;
    g_hash_table_iter_init (&iter, client->docs);
    while (g_hash_table_iter_next (&iter, NULL, &doc))
        ((lsp_doc_t *) doc)->opened = FALSE;

    client->state = state;
    g_free (client->reason);
    client->reason = g_strdup (reason);
    client->state_changed = TRUE;
    if (reason != NULL)
        lsp_client_log_add (client, "%s: %s", lsp_client_state_name (state), reason);
    else
        lsp_client_log_add (client, "%s", lsp_client_state_name (state));
    // last: a callback may start the client again
    lsp_pending_end_all (client, LSP_REQUEST_CLOSED);
}

/* --------------------------------------------------------------------------------------------- */

/* The owner told of a change of the state */
static void
lsp_client_tell (lsp_client_t *client)
{
    if (!client->state_changed)
        return;
    client->state_changed = FALSE;
    if (client->handlers->state != NULL)
        client->handlers->state (client->owner, client);
}

/* --------------------------------------------------------------------------------------------- */

/* The server has gone, or speaks no protocol */
static void
lsp_client_lost (lsp_client_t *client, const char *why)
{
    if (client->state == LSP_CLIENT_STOPPED || client->state == LSP_CLIENT_FAILED)
        return;
    if (client->state == LSP_CLIENT_STOPPING && why == NULL)
        // it said goodbye: SIGTERM only when it lingers
        lsp_client_teardown (client, LSP_CLIENT_STOPPED, NULL, 500);
    else
        lsp_client_teardown (client, LSP_CLIENT_FAILED, why != NULL ? why : "the server exited", 0);
}

/* --------------------------------------------------------------------------------------------- */
/* The documents */

static void
lsp_doc_free (gpointer data)
{
    lsp_doc_t *doc = (lsp_doc_t *) data;

    g_free (doc->uri);
    g_free (doc->language);
    g_bytes_unref (doc->text);
    g_free (doc);
}

/* --------------------------------------------------------------------------------------------- */

static JsonNode *
lsp_text_node (GBytes *text)
{
    gsize len;
    const char *data = g_bytes_get_data (text, &len);
    // an empty file has no data: NULL, which g_strndup () gives back
    char *copy = data != NULL ? g_strndup (data, len) : g_strdup ("");
    JsonNode *node = json_node_new (JSON_NODE_VALUE);

    json_node_set_string (node, copy);
    g_free (copy);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* The next version of @uri in this process */
static gint
lsp_client_next_version (lsp_client_t *client, const char *uri)
{
    const gpointer last = g_hash_table_lookup (client->versions, uri);
    const gint version = GPOINTER_TO_INT (last) + 1;

    g_hash_table_insert (client->versions, g_strdup (uri), GINT_TO_POINTER (version));
    return version;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_send_open (lsp_client_t *client, lsp_doc_t *doc)
{
    JsonObject *params, *item;

    if (!client->sync_open_close)
        return;
    doc->version = lsp_client_next_version (client, doc->uri);
    item = json_object_new ();
    json_object_set_string_member (item, "uri", doc->uri);
    json_object_set_string_member (item, "languageId", doc->language);
    json_object_set_int_member (item, "version", doc->version);
    json_object_set_member (item, "text", lsp_text_node (doc->text));
    params = json_object_new ();
    json_object_set_object_member (params, "textDocument", item);
    doc->opened =
        lsp_client_send_notification (client, "textDocument/didOpen", lsp_object_node (params));
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_send_close (lsp_client_t *client, lsp_doc_t *doc)
{
    JsonObject *params, *item;

    if (!doc->opened)
        return;
    doc->opened = FALSE;
    item = json_object_new ();
    json_object_set_string_member (item, "uri", doc->uri);
    params = json_object_new ();
    json_object_set_object_member (params, "textDocument", item);
    (void) lsp_client_send_notification (client, "textDocument/didClose", lsp_object_node (params));
}

/* --------------------------------------------------------------------------------------------- */
/* What the server asks and tells */

static void
lsp_client_configuration (lsp_client_t *client, JsonNode *id, JsonObject *params)
{
    JsonNode *items = lsp_member (params, "items");
    JsonArray *result = json_array_new ();
    JsonNode *node;
    guint i;

    if (items == NULL || !JSON_NODE_HOLDS_ARRAY (items))
    {
        json_array_unref (result);
        lsp_client_respond (client, id, NULL, LSP_ERROR_INVALID_PARAMS, "no items");
        return;
    }
    for (i = 0; i < json_array_get_length (json_node_get_array (items)); i++)
    {
        JsonNode *item = json_array_get_element (json_node_get_array (items), i);
        const char *section = JSON_NODE_HOLDS_OBJECT (item)
            ? lsp_member_string (json_node_get_object (item), "section")
            : NULL;
        JsonNode *value = lsp_settings_lookup (client->settings, section);

        json_array_add_element (
            result, value != NULL ? json_node_copy (value) : json_node_new (JSON_NODE_NULL));
    }
    node = json_node_new (JSON_NODE_ARRAY);
    json_node_take_array (node, result);
    lsp_client_respond (client, id, node, 0, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static JsonNode *
lsp_client_folders (const lsp_client_t *client)
{
    JsonArray *folders = json_array_new ();
    JsonObject *folder = json_object_new ();
    char *uri = lsp_uri_from_path (client->root);
    char *name = g_path_get_basename (client->root);
    JsonNode *node;

    json_object_set_string_member (folder, "uri", uri != NULL ? uri : "");
    json_object_set_string_member (folder, "name", name);
    json_array_add_object_element (folders, folder);
    node = json_node_new (JSON_NODE_ARRAY);
    json_node_take_array (node, folders);
    g_free (uri);
    g_free (name);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_server_request (lsp_client_t *client, JsonNode *id, const char *method,
                           JsonObject *params)
{
    if (strcmp (method, "workspace/configuration") == 0)
        lsp_client_configuration (client, id, params);
    else if (strcmp (method, "workspace/workspaceFolders") == 0)
        lsp_client_respond (client, id, lsp_client_folders (client), 0, NULL);
    else if (strcmp (method, "window/workDoneProgress/create") == 0
             || strcmp (method, "client/registerCapability") == 0
             || strcmp (method, "client/unregisterCapability") == 0)
        lsp_client_respond (client, id, NULL, 0, NULL);
    else if (strcmp (method, "window/showMessageRequest") == 0)
    {
        const char *message = lsp_member_string (params, "message");

        lsp_client_log_add (client, "server asks: %s", message != NULL ? message : "");
        lsp_client_respond (client, id, NULL, 0, NULL);
    }
    else if (strcmp (method, "workspace/applyEdit") == 0)
    {
        JsonObject *result = json_object_new ();

        json_object_set_boolean_member (result, "applied", FALSE);
        json_object_set_string_member (result, "failureReason", "not supported");
        lsp_client_respond (client, id, lsp_object_node (result), 0, NULL);
    }
    else
        lsp_client_respond (client, id, NULL, LSP_ERROR_METHOD_NOT_FOUND, "not supported");
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_diagnostics (lsp_client_t *client, JsonObject *params)
{
    const char *uri = lsp_member_string (params, "uri");
    JsonNode *diagnostics = lsp_member (params, "diagnostics");
    lsp_doc_t *doc;
    gint64 version;

    if (uri == NULL || diagnostics == NULL || !JSON_NODE_HOLDS_ARRAY (diagnostics))
        return;
    doc = g_hash_table_lookup (client->docs, uri);
    // of another text than the server has now: the problems of this one come after
    if (doc != NULL && doc->opened && lsp_member_int (params, "version", &version)
        && version != doc->version)
        return;
    if (client->handlers->diagnostics != NULL)
        client->handlers->diagnostics (client->owner, client, uri,
                                       json_node_get_array (diagnostics));
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_notification (lsp_client_t *client, const char *method, JsonObject *params)
{
    if (strcmp (method, "textDocument/publishDiagnostics") == 0)
        lsp_client_diagnostics (client, params);
    else if (strcmp (method, "window/logMessage") == 0
             || strcmp (method, "window/showMessage") == 0)
    {
        const char *text = lsp_member_string (params, "message");
        gint64 type = 4;

        if (text == NULL)
            return;
        (void) lsp_member_int (params, "type", &type);
        lsp_client_log_line (client, text, -1);
        if (strcmp (method, "window/showMessage") == 0 && client->handlers->message != NULL)
            client->handlers->message (client->owner, client, (int) type, text);
    }
    else if (strcmp (method, "$/progress") == 0)
    {
        JsonObject *value = lsp_member_object (params, "value");
        const char *kind = lsp_member_string (value, "kind");
        const char *title = lsp_member_string (value, "title");
        const char *message = lsp_member_string (value, "message");

        if (g_strcmp0 (kind, "begin") == 0 || g_strcmp0 (kind, "end") == 0)
            lsp_client_log_add (client, "progress %s: %s%s%s", kind, title != NULL ? title : "",
                                message != NULL ? " " : "", message != NULL ? message : "");
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_response (lsp_client_t *client, JsonObject *message)
{
    JsonNode *id = lsp_member (message, "id");
    JsonNode *result = lsp_member (message, "result");
    JsonObject *error = lsp_member_object (message, "error");
    lsp_pending_t *pending;

    // the ids of the client are numbers: any other is no answer of its
    if (id == NULL || !JSON_NODE_HOLDS_VALUE (id) || json_node_get_value_type (id) != G_TYPE_INT64)
        return;
    pending = lsp_pending_take (client, (guint64) json_node_get_int (id));
    if (pending == NULL)
        return;  // late: given up already
    if (error != NULL)
        lsp_pending_end (client, pending, LSP_REQUEST_ERROR, NULL, error);
    else if (result != NULL)
        lsp_pending_end (client, pending, LSP_REQUEST_OK, result, NULL);
    else
    {
        lsp_client_log_add (client, "a response with neither result nor error");
        lsp_pending_end (client, pending, LSP_REQUEST_ERROR, NULL, NULL);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_message (const char *json, gsize len, void *data)
{
    lsp_client_t *client = (lsp_client_t *) data;
    JsonParser *parser;
    JsonNode *root;
    JsonObject *message;
    const char *method;
    JsonNode *id;

    // what comes after the server was let go is no one's
    if (client->out < 0)
        return;
    if (!lsp_depth_ok (json, len))
    {
        lsp_client_lost (client, "a message nested too deep");
        return;
    }
    parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json, (gssize) len, NULL)
        || (root = json_parser_get_root (parser)) == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    {
        g_object_unref (parser);
        lsp_client_lost (client, "a message that is no JSON object");
        return;
    }
    message = json_node_get_object (root);
    method = lsp_member_string (message, "method");
    id = lsp_member (message, "id");
    if (g_strcmp0 (lsp_member_string (message, "jsonrpc"), "2.0") != 0)
        lsp_client_lost (client, "a message that is not JSON-RPC 2.0");
    else if (method == NULL)
        lsp_client_response (client, message);
    else
    {
        JsonObject *params = lsp_member_object (message, "params");

        if (id != NULL && !JSON_NODE_HOLDS_NULL (id))
            lsp_client_server_request (client, id, method, params);
        else
            lsp_client_notification (client, method, params);
    }
    g_object_unref (parser);
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_client_read (int fd, void *data)
{
    lsp_client_t *client = (lsp_client_t *) data;
    char buf[65536];
    const ssize_t n = read (fd, buf, sizeof (buf));

    if (n > 0)
    {
        if (!content_frame_reader_feed (client->reader, buf, (gsize) n, lsp_client_message, client))
            lsp_client_lost (client, "the server speaks no protocol");
    }
    else if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    else
        lsp_client_lost (client, NULL);
    lsp_client_tell (client);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_client_read_err (int fd, void *data)
{
    lsp_client_t *client = (lsp_client_t *) data;
    char buf[4096];
    const ssize_t n = read (fd, buf, sizeof (buf));
    char *eol;

    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    if (n <= 0)
    {
        delete_select_channel (fd);
        close (fd);
        client->err = -1;
        return 0;
    }
    g_string_append_len (client->err_line, buf, n);
    while ((eol = memchr (client->err_line->str, '\n', client->err_line->len)) != NULL)
    {
        lsp_client_log_line (client, client->err_line->str, eol - client->err_line->str);
        g_string_erase (client->err_line, 0, eol - client->err_line->str + 1);
    }
    // a line without end that goes on and on is cut
    if (client->err_line->len > 4096)
    {
        lsp_client_log_line (client, client->err_line->str, (gssize) client->err_line->len);
        g_string_truncate (client->err_line, 0);
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */
/* The start */

/* The server in a session of its own: no key of the terminal reaches it, and it ends with the
   editor */
static void
lsp_child_setup (gpointer data)
{
    (void) data;
    (void) setsid ();
#if defined(HAVE_SYS_PRCTL_H) && defined(PR_SET_PDEATHSIG)
    (void) prctl (PR_SET_PDEATHSIG, SIGTERM);
#endif
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_sync_options (lsp_client_t *client)
{
    JsonNode *sync = lsp_member (client->capabilities, "textDocumentSync");

    client->sync_change = 0;
    client->sync_open_close = FALSE;
    client->sync_save = FALSE;
    client->sync_save_text = FALSE;
    if (sync == NULL)
        return;
    if (JSON_NODE_HOLDS_VALUE (sync) && json_node_get_value_type (sync) == G_TYPE_INT64)
    {
        client->sync_change = (int) json_node_get_int (sync);
        client->sync_open_close = client->sync_change != 0;
    }
    else if (JSON_NODE_HOLDS_OBJECT (sync))
    {
        JsonObject *o = json_node_get_object (sync);
        JsonNode *save = lsp_member (o, "save");
        gint64 change = 0;

        (void) lsp_member_int (o, "change", &change);
        client->sync_change = (int) change;
        client->sync_open_close = lsp_member_true (o, "openClose");
        client->sync_save = lsp_member_true (o, "save");
        client->sync_save_text = save != NULL && JSON_NODE_HOLDS_OBJECT (save)
            && lsp_member_true (json_node_get_object (save), "includeText");
    }
    if (client->sync_change < 0 || client->sync_change > 2)
        client->sync_change = 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_initialized (lsp_client_t *client, lsp_request_status_t status, JsonNode *result,
                        JsonObject *error, void *data)
{
    JsonObject *capabilities;
    const char *encoding;
    GHashTableIter iter;
    gpointer doc;

    (void) data;
    if (status == LSP_REQUEST_CLOSED || client->state != LSP_CLIENT_STARTING)
        return;
    if (status != LSP_REQUEST_OK || !JSON_NODE_HOLDS_OBJECT (result))
    {
        char *why =
            g_strdup_printf ("initialize: %s",
                             status == LSP_REQUEST_TIMEOUT ? "no answer"
                                 : error != NULL && lsp_member_string (error, "message") != NULL
                                 ? lsp_member_string (error, "message")
                                 : "no result");

        lsp_client_lost (client, why);
        g_free (why);
        return;
    }
    capabilities = lsp_member_object (json_node_get_object (result), "capabilities");
    client->capabilities =
        capabilities != NULL ? json_object_ref (capabilities) : json_object_new ();
    encoding = lsp_member_string (client->capabilities, "positionEncoding");
    if (encoding == NULL || strcmp (encoding, "utf-16") == 0)
        client->encoding = LSP_ENCODING_UTF16;
    else if (strcmp (encoding, "utf-8") == 0)
        client->encoding = LSP_ENCODING_UTF8;
    else
    {
        char *why = g_strdup_printf ("the server counts characters in %s", encoding);

        lsp_client_lost (client, why);
        g_free (why);
        return;
    }
    lsp_client_sync_options (client);

    (void) lsp_client_send_notification (client, "initialized",
                                         lsp_object_node (json_object_new ()));
    if (client->settings != NULL)
    {
        JsonObject *params = json_object_new ();

        json_object_set_member (params, "settings", json_node_copy (client->settings));
        (void) lsp_client_send_notification (client, "workspace/didChangeConfiguration",
                                             lsp_object_node (params));
    }
    client->state = LSP_CLIENT_RUNNING;
    client->state_changed = TRUE;
    lsp_client_log_add (client, "running, positions in %s",
                        client->encoding == LSP_ENCODING_UTF8 ? "UTF-8" : "UTF-16");
    g_hash_table_iter_init (&iter, client->docs);
    while (g_hash_table_iter_next (&iter, NULL, &doc))
        lsp_client_send_open (client, (lsp_doc_t *) doc);
}

/* --------------------------------------------------------------------------------------------- */

static JsonNode *
lsp_client_initialize_params (const lsp_client_t *client)
{
    JsonObject *params = json_object_new ();
    JsonObject *info = json_object_new ();
    char *uri = lsp_uri_from_path (client->root);

    json_object_set_int_member (params, "processId", (gint64) getpid ());
    json_object_set_string_member (info, "name", PACKAGE);
    json_object_set_string_member (info, "version", PACKAGE_VERSION);
    json_object_set_object_member (params, "clientInfo", info);
    json_object_set_string_member (params, "rootPath", client->root);
    if (uri != NULL)
        json_object_set_string_member (params, "rootUri", uri);
    else
        json_object_set_null_member (params, "rootUri");
    json_object_set_member (params, "workspaceFolders", lsp_client_folders (client));
    json_object_set_member (params, "capabilities",
                            json_from_string (lsp_client_capabilities_json, NULL));
    if (client->initialization_options != NULL)
        json_object_set_member (params, "initializationOptions",
                                json_node_copy (client->initialization_options));
    json_object_set_string_member (params, "trace", "off");
    g_free (uri);
    return lsp_object_node (params);
}

/* --------------------------------------------------------------------------------------------- */

static char **
lsp_client_environment (const lsp_client_t *client)
{
    char **env = g_get_environ ();
    GList *names, *l;

    if (client->environment == NULL)
        return env;
    names = json_object_get_members (client->environment);
    for (l = names; l != NULL; l = l->next)
    {
        JsonNode *value = json_object_get_member (client->environment, l->data);

        if (JSON_NODE_HOLDS_NULL (value))
            env = g_environ_unsetenv (env, l->data);
        else if (JSON_NODE_HOLDS_VALUE (value) && json_node_get_value_type (value) == G_TYPE_STRING)
            env = g_environ_setenv (env, l->data, json_node_get_string (value), TRUE);
    }
    g_list_free (names);
    return env;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_client_shutdown_answered (lsp_client_t *client, lsp_request_status_t status, JsonNode *result,
                              JsonObject *error, void *data)
{
    (void) result;
    (void) error;
    (void) data;
    if (status == LSP_REQUEST_CLOSED || client->state != LSP_CLIENT_STOPPING)
        return;
    (void) lsp_client_send_notification (client, "exit", NULL);
    // a server that waits for the end of its input has it
    client->close_in = TRUE;
    lsp_client_flush (client);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

lsp_client_t *
lsp_client_new (const lsp_backend_t *backend, const char *root,
                const lsp_client_handlers_t *handlers, void *owner)
{
    lsp_client_t *client = g_new0 (lsp_client_t, 1);

    client->handlers = handlers;
    client->owner = owner;
    client->backend_id = g_strdup (backend->id);
    client->command = g_strdupv (backend->command);
    if (backend->environment != NULL)
        client->environment = json_object_ref (backend->environment);
    if (backend->initialization_options != NULL)
        client->initialization_options = json_node_copy (backend->initialization_options);
    if (backend->settings != NULL)
        client->settings = json_node_copy (backend->settings);
    client->root = g_strdup (root);
    client->in = client->out = client->err = -1;
    client->reader = content_frame_reader_new (LSP_MAX_MESSAGE);
    client->writing = g_queue_new ();
    client->err_line = g_string_new (NULL);
    client->pending = g_ptr_array_new ();
    client->docs = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, lsp_doc_free);
    client->versions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    client->log = g_queue_new ();
    return client;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_free (lsp_client_t *client)
{
    if (client == NULL)
        return;
    if (client->pid != 0 || client->out >= 0)
        lsp_client_teardown (client, LSP_CLIENT_STOPPED, NULL, 0);
    lsp_pending_end_all (client, LSP_REQUEST_CLOSED);
    g_ptr_array_free (client->pending, TRUE);
    content_frame_reader_free (client->reader);
    g_queue_free_full (client->writing, (GDestroyNotify) g_bytes_unref);
    g_string_free (client->err_line, TRUE);
    g_free (client->broken);
    if (client->capabilities != NULL)
        json_object_unref (client->capabilities);
    g_hash_table_destroy (client->docs);
    g_hash_table_destroy (client->versions);
    g_queue_free_full (client->log, g_free);
    g_free (client->backend_id);
    g_strfreev (client->command);
    if (client->environment != NULL)
        json_object_unref (client->environment);
    if (client->initialization_options != NULL)
        json_node_unref (client->initialization_options);
    if (client->settings != NULL)
        json_node_unref (client->settings);
    g_free (client->root);
    g_free (client->reason);
    g_free (client);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_set_command (lsp_client_t *client, char *const *command)
{
    g_strfreev (client->command);
    client->command = g_strdupv ((char **) command);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_start (lsp_client_t *client, GError **error)
{
    char **env;
    gboolean spawned;

    if (client->state == LSP_CLIENT_STARTING || client->state == LSP_CLIENT_RUNNING
        || client->state == LSP_CLIENT_STOPPING)
        return TRUE;
    env = lsp_client_environment (client);
    spawned = g_spawn_async_with_pipes (
        client->root, client->command, env, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        lsp_child_setup, NULL, &client->pid, &client->in, &client->out, &client->err, error);
    g_strfreev (env);
    if (!spawned)
    {
        client->pid = 0;
        client->in = client->out = client->err = -1;
        client->state = LSP_CLIENT_FAILED;
        g_free (client->reason);
        client->reason = g_strdup (error != NULL && *error != NULL ? (*error)->message
                                                                   : "cannot run the server");
        lsp_client_log_add (client, "failed: %s", client->reason);
        return FALSE;
    }
    lsp_nonblocking (client->in);
    lsp_nonblocking (client->out);
    lsp_nonblocking (client->err);
    add_select_channel (client->out, lsp_client_read, client);
    add_select_channel (client->err, lsp_client_read_err, client);
    content_frame_reader_reset (client->reader);
    g_hash_table_remove_all (client->versions);
    client->encoding = LSP_ENCODING_UTF16;
    client->state = LSP_CLIENT_STARTING;
    client->state_changed = TRUE;
    g_clear_pointer (&client->reason, g_free);
    {
        char *line = g_strjoinv (" ", client->command);

        lsp_client_log_add (client, "started %s in %s, pid %d", line, client->root,
                            (int) client->pid);
        g_free (line);
    }
    if (lsp_client_send_request (client, "initialize", lsp_client_initialize_params (client),
                                 LSP_INITIALIZE_TIMEOUT_MS, TRUE, lsp_client_initialized, NULL,
                                 NULL)
        == 0)
        // what failed is seen at the next tick
        client->broken = g_strdup ("cannot write to the server");
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_stop (lsp_client_t *client)
{
    switch (client->state)
    {
    case LSP_CLIENT_STARTING:
        // no shutdown before the answer to initialize
        lsp_client_teardown (client, LSP_CLIENT_STOPPED, NULL, 0);
        break;
    case LSP_CLIENT_RUNNING:
        client->state = LSP_CLIENT_STOPPING;
        client->state_changed = TRUE;
        client->stop_deadline = g_get_monotonic_time () + LSP_STOP_TIMEOUT_MS * 1000;
        lsp_client_log_add (client, "stopping");
        if (lsp_client_send_request (client, "shutdown", NULL, LSP_STOP_TIMEOUT_MS / 2, TRUE,
                                     lsp_client_shutdown_answered, NULL, NULL)
            == 0)
            client->stop_deadline = 0;
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_quit (lsp_client_t *client)
{
    const gint64 deadline = g_get_monotonic_time () + 200 * 1000;

    if (client->state == LSP_CLIENT_RUNNING)
    {
        (void) lsp_client_send_request (client, "shutdown", NULL, LSP_STOP_TIMEOUT_MS, TRUE, NULL,
                                        NULL, NULL);
        (void) lsp_client_send_notification (client, "exit", NULL);
        // a little while for what is left to go, not more: the editor is ending
        while (client->in >= 0 && !g_queue_is_empty (client->writing) && client->broken == NULL
               && g_get_monotonic_time () < deadline)
        {
            g_usleep (5000);
            lsp_client_flush (client);
        }
    }
    if (client->pid != 0 || client->out >= 0)
        lsp_client_teardown (client, LSP_CLIENT_STOPPED, NULL, 1000);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_tick (lsp_client_t *client, gint64 now)
{
    GPtrArray *late = NULL;
    guint i;

    if (client->broken != NULL)
    {
        char *why = client->broken;

        client->broken = NULL;
        lsp_client_lost (client, why);
        g_free (why);
    }
    else if (!g_queue_is_empty (client->writing))
        lsp_client_flush (client);

    if (client->state == LSP_CLIENT_STOPPING && now >= client->stop_deadline)
        lsp_client_teardown (client, LSP_CLIENT_STOPPED, "the server did not stop in time", 0);

    for (i = 0; i < client->pending->len; i++)
    {
        lsp_pending_t *pending = g_ptr_array_index (client->pending, i);

        if (pending->deadline <= now)
        {
            if (late == NULL)
                late = g_ptr_array_new ();
            g_ptr_array_add (late, g_ptr_array_steal_index (client->pending, i--));
        }
    }
    for (i = 0; late != NULL && i < late->len; i++)
    {
        lsp_pending_t *pending = g_ptr_array_index (late, i);

        if (!pending->internal && client->state == LSP_CLIENT_RUNNING)
        {
            JsonObject *params = json_object_new ();

            json_object_set_int_member (params, "id", (gint64) pending->id);
            (void) lsp_client_send_notification (client, "$/cancelRequest",
                                                 lsp_object_node (params));
        }
        lsp_pending_end (client, pending, LSP_REQUEST_TIMEOUT, NULL, NULL);
    }
    if (late != NULL)
        g_ptr_array_free (late, TRUE);
    lsp_client_tell (client);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_writing (const lsp_client_t *client)
{
    return !g_queue_is_empty (client->writing) || client->broken != NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_reap_tick (gint64 now)
{
    guint i;

    for (i = 0; lsp_reaped != NULL && i < lsp_reaped->len; i++)
    {
        lsp_reaped_t *r = &g_array_index (lsp_reaped, lsp_reaped_t, i);
        const pid_t done = waitpid (r->pid, NULL, WNOHANG);

        if (done == r->pid || (done < 0 && errno == ECHILD))
        {
            g_spawn_close_pid (r->pid);
            if (r->out >= 0)
                close (r->out);
            if (r->err >= 0)
                close (r->err);
            g_array_remove_index_fast (lsp_reaped, i--);
            continue;
        }
        // not reaped yet, its number still the group's: the signal reaches no other
        if (now >= r->kill_at)
            (void) kill (-r->pid, SIGKILL);
        else if (now >= r->term_at && !r->termed)
        {
            (void) kill (-r->pid, SIGTERM);
            r->termed = TRUE;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_reaping (void)
{
    return lsp_reaped != NULL && lsp_reaped->len > 0;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_reap_all (int wait_ms)
{
    const gint64 start = g_get_monotonic_time ();
    guint i;

    if (lsp_reaped == NULL)
        return;
    // the servers end in parallel, each in the time it has, all of them in @wait_ms
    for (i = 0; i < lsp_reaped->len; i++)
    {
        lsp_reaped_t *r = &g_array_index (lsp_reaped, lsp_reaped_t, i);

        r->term_at = MIN (r->term_at, start + (gint64) wait_ms * 1000);
        r->kill_at = MIN (r->kill_at, r->term_at + 300 * 1000);
    }
    while (lsp_client_reaping ())
    {
        lsp_client_reap_tick (g_get_monotonic_time ());
        if (lsp_client_reaping ())
            g_usleep (10000);
    }
    g_array_free (lsp_reaped, TRUE);
    lsp_reaped = NULL;
}

/* --------------------------------------------------------------------------------------------- */

lsp_client_state_t
lsp_client_state (const lsp_client_t *client)
{
    return client->state;
}

/* --------------------------------------------------------------------------------------------- */

const char *
lsp_client_state_name (lsp_client_state_t state)
{
    switch (state)
    {
    case LSP_CLIENT_STARTING:
        return "starting";
    case LSP_CLIENT_RUNNING:
        return "running";
    case LSP_CLIENT_STOPPING:
        return "stopping";
    case LSP_CLIENT_FAILED:
        return "failed";
    default:
        return "stopped";
    }
}

/* --------------------------------------------------------------------------------------------- */

const char *
lsp_client_reason (const lsp_client_t *client)
{
    return client->reason;
}

/* --------------------------------------------------------------------------------------------- */

const char *
lsp_client_backend_id (const lsp_client_t *client)
{
    return client->backend_id;
}

/* --------------------------------------------------------------------------------------------- */

const char *
lsp_client_root (const lsp_client_t *client)
{
    return client->root;
}

/* --------------------------------------------------------------------------------------------- */

GPid
lsp_client_pid (const lsp_client_t *client)
{
    return client->pid;
}

/* --------------------------------------------------------------------------------------------- */

lsp_encoding_t
lsp_client_encoding (const lsp_client_t *client)
{
    return client->encoding;
}

/* --------------------------------------------------------------------------------------------- */

guint
lsp_client_pending (const lsp_client_t *client)
{
    return client->pending->len;
}

/* --------------------------------------------------------------------------------------------- */

guint
lsp_client_documents (const lsp_client_t *client)
{
    return g_hash_table_size (client->docs);
}

/* --------------------------------------------------------------------------------------------- */

const GQueue *
lsp_client_log (const lsp_client_t *client)
{
    return client->log;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_log_add (lsp_client_t *client, const char *format, ...)
{
    va_list args;
    char *line;

    va_start (args, format);
    line = g_strdup_vprintf (format, args);
    va_end (args);
    lsp_client_log_line (client, line, -1);
    g_free (line);
}

/* --------------------------------------------------------------------------------------------- */

JsonObject *
lsp_client_capabilities (const lsp_client_t *client)
{
    return client->capabilities;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_supports (const lsp_client_t *client, const char *provider)
{
    return client->state == LSP_CLIENT_RUNNING && lsp_member_true (client->capabilities, provider);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_completion_trigger (const lsp_client_t *client, const char *c)
{
    return lsp_client_trigger (client, "completionProvider", c);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_trigger (const lsp_client_t *client, const char *provider, const char *c)
{
    JsonObject *options = lsp_member_object (client->capabilities, provider);
    JsonNode *triggers = lsp_member (options, "triggerCharacters");
    JsonArray *array;
    guint i;

    if (triggers == NULL || !JSON_NODE_HOLDS_ARRAY (triggers))
        return FALSE;
    array = json_node_get_array (triggers);
    for (i = 0; i < json_array_get_length (array); i++)
    {
        JsonNode *element = json_array_get_element (array, i);

        if (JSON_NODE_HOLDS_VALUE (element) && json_node_get_value_type (element) == G_TYPE_STRING
            && strcmp (json_node_get_string (element), c) == 0)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_doc_open (lsp_client_t *client, const char *uri, const char *language_id,
                     const char *text, gsize len)
{
    lsp_doc_t *doc = g_hash_table_lookup (client->docs, uri);

    if (doc != NULL && strcmp (doc->language, language_id) == 0)
    {
        lsp_client_doc_change (client, uri, text, len);
        return;
    }
    if (doc != NULL)
        lsp_client_doc_close (client, uri);
    doc = g_new0 (lsp_doc_t, 1);
    doc->uri = g_strdup (uri);
    doc->language = g_strdup (language_id);
    doc->text = g_bytes_new (text, len);
    g_hash_table_insert (client->docs, doc->uri, doc);
    if (client->state == LSP_CLIENT_RUNNING)
        lsp_client_send_open (client, doc);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_doc_change (lsp_client_t *client, const char *uri, const char *text, gsize len)
{
    lsp_doc_t *doc = g_hash_table_lookup (client->docs, uri);
    GBytes *now;
    JsonObject *params, *item, *change;
    JsonArray *changes;

    if (doc == NULL)
        return;
    now = g_bytes_new (text, len);
    if (g_bytes_equal (now, doc->text))
    {
        g_bytes_unref (now);
        return;
    }
    g_bytes_unref (doc->text);
    doc->text = now;
    if (client->state != LSP_CLIENT_RUNNING || !doc->opened || client->sync_change == 0)
        return;
    doc->version = lsp_client_next_version (client, uri);
    item = json_object_new ();
    json_object_set_string_member (item, "uri", uri);
    json_object_set_int_member (item, "version", doc->version);
    change = json_object_new ();
    json_object_set_member (change, "text", lsp_text_node (doc->text));
    changes = json_array_new ();
    json_array_add_object_element (changes, change);
    params = json_object_new ();
    json_object_set_object_member (params, "textDocument", item);
    json_object_set_array_member (params, "contentChanges", changes);
    (void) lsp_client_send_notification (client, "textDocument/didChange",
                                         lsp_object_node (params));
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_doc_save (lsp_client_t *client, const char *uri, const char *text, gsize len)
{
    lsp_doc_t *doc = g_hash_table_lookup (client->docs, uri);
    JsonObject *params, *item;

    if (doc == NULL)
        return;
    lsp_client_doc_change (client, uri, text, len);
    if (client->state != LSP_CLIENT_RUNNING || !doc->opened || !client->sync_save)
        return;
    item = json_object_new ();
    json_object_set_string_member (item, "uri", uri);
    params = json_object_new ();
    json_object_set_object_member (params, "textDocument", item);
    if (client->sync_save_text)
        json_object_set_member (params, "text", lsp_text_node (doc->text));
    (void) lsp_client_send_notification (client, "textDocument/didSave", lsp_object_node (params));
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_doc_close (lsp_client_t *client, const char *uri)
{
    lsp_doc_t *doc = g_hash_table_lookup (client->docs, uri);

    if (doc == NULL)
        return;
    if (client->state == LSP_CLIENT_RUNNING)
        lsp_client_send_close (client, doc);
    g_hash_table_remove (client->docs, uri);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_has_doc (const lsp_client_t *client, const char *uri)
{
    return g_hash_table_contains (client->docs, uri);
}

/* --------------------------------------------------------------------------------------------- */

gint
lsp_client_doc_version (const lsp_client_t *client, const char *uri)
{
    const lsp_doc_t *doc = g_hash_table_lookup (client->docs, uri);

    return doc != NULL && doc->opened ? doc->version : 0;
}

/* --------------------------------------------------------------------------------------------- */

guint64
lsp_client_request (lsp_client_t *client, const char *method, JsonNode *params, guint timeout_ms,
                    lsp_response_fn fn, void *data, GDestroyNotify destroy)
{
    if (client->state != LSP_CLIENT_RUNNING || client->pending->len >= LSP_MAX_PENDING)
    {
        if (params != NULL)
            json_node_unref (params);
        if (destroy != NULL)
            destroy (data);
        return 0;
    }
    return lsp_client_send_request (client, method, params,
                                    timeout_ms != 0 ? timeout_ms : LSP_REQUEST_TIMEOUT_MS, FALSE,
                                    fn, data, destroy);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_client_cancel (lsp_client_t *client, guint64 id)
{
    lsp_pending_t *pending = lsp_pending_take (client, id);

    if (pending == NULL)
        return;
    if (client->state == LSP_CLIENT_RUNNING)
    {
        JsonObject *params = json_object_new ();

        json_object_set_int_member (params, "id", (gint64) id);
        (void) lsp_client_send_notification (client, "$/cancelRequest", lsp_object_node (params));
    }
    lsp_pending_end (client, pending, LSP_REQUEST_CANCELLED, NULL, NULL);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_client_notify (lsp_client_t *client, const char *method, JsonNode *params)
{
    if (client->state != LSP_CLIENT_RUNNING)
    {
        if (params != NULL)
            json_node_unref (params);
        return FALSE;
    }
    return lsp_client_send_notification (client, method, params);
}

/* --------------------------------------------------------------------------------------------- */
