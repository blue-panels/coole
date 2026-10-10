/** \file dap.h
 *  \brief Header: a client of the Debug Adapter Protocol
 *
 *  A debug adapter speaks JSON messages, each after a header "Content-Length: N\r\n\r\n", on
 *  its stdin and stdout or on a TCP socket.  The client sends requests and gets their responses,
 *  gets the events of the adapter, and answers the requests the adapter sends back to it.
 */

#ifndef MC__DAP_H
#define MC__DAP_H

#include <glib.h>
#include <json-glib/json-glib.h>

/* --------------------------------------------------------------------------------------------- */
/* The messages of a stream of bytes */

typedef struct dap_reader_t dap_reader_t;
typedef void (*dap_message_fn) (const char *json, gsize len, void *data);

dap_reader_t *dap_reader_new (void);
/* The bytes that came: @fn gets each message they end.  FALSE when the stream is broken: a
   header without its length, or a length too big; what is buffered is then dropped. */
gboolean dap_reader_feed (dap_reader_t *reader, const char *bytes, gsize len, dap_message_fn fn,
                          void *data);
void dap_reader_free (dap_reader_t *reader);

/* A message with its header, as it goes to the adapter; free with g_free() */
char *dap_message_frame (const char *json, gsize json_len, gsize *len);

/* --------------------------------------------------------------------------------------------- */
/* The members of a JSON object: NULL, or @fallback, when there is none or it is of another type */

const char *dap_string (JsonObject *object, const char *name);
gint64 dap_int (JsonObject *object, const char *name, gint64 fallback);
gboolean dap_bool (JsonObject *object, const char *name, gboolean fallback);
JsonObject *dap_object (JsonObject *object, const char *name);
JsonArray *dap_array (JsonObject *object, const char *name);

/* --------------------------------------------------------------------------------------------- */
/* The client */

typedef struct dap_client_t dap_client_t;

/* The response to a request: @message says why it failed; @body may be NULL */
typedef void (*dap_response_fn) (void *owner, gboolean success, const char *message,
                                 JsonObject *body, void *data);

/* @client is the one the message came on: an owner may have several, an adapter that starts
   a session of its own for each program (js-debug) */
typedef struct
{
    void (*event) (void *owner, dap_client_t *client, const char *event, JsonObject *body);
    // a request of the adapter: answered with dap_client_respond(), on @client
    void (*request) (void *owner, dap_client_t *client, gint64 seq, const char *command,
                     JsonObject *arguments);
    // what the adapter writes beside the protocol
    void (*text) (void *owner, const char *text);
    // the adapter has gone, or the stream is broken: the client is closed, not freed
    void (*closed) (void *owner, dap_client_t *client, const char *why);
    // all that came in is told
    void (*flush) (void *owner);
} dap_handlers_t;

dap_client_t *dap_client_new (const dap_handlers_t *handlers, void *owner);
/* The adapter as a program speaking on its stdin and stdout */
gboolean dap_client_spawn (dap_client_t *client, char **argv, const char *directory,
                           GError **error);
/* An adapter listening on @host:@port */
gboolean dap_client_connect (dap_client_t *client, const char *host, int port, GError **error);
/* The adapter as a program that listens on a port it writes on its output: @pattern, a regular
   expression whose first group is the port, finds it there */
gboolean dap_client_spawn_listening (dap_client_t *client, char **argv, const char *directory,
                                     const char *host, const char *pattern, GError **error);
/* A request, @arguments taken (NULL for none); its seq, 0 when it could not be sent.  @data is
   freed with @free_data either way. */
guint dap_client_request (dap_client_t *client, const char *command, JsonNode *arguments,
                          dap_response_fn fn, void *data, GDestroyNotify free_data);
/* The answer to a request of the adapter, @body taken (NULL for none) */
gboolean dap_client_respond (dap_client_t *client, gint64 request_seq, const char *command,
                             gboolean success, const char *message, JsonNode *body);
gboolean dap_client_alive (const dap_client_t *client);
/* The port of the socket the client is on, 0 on stdin and stdout; the host with it */
int dap_client_port (const dap_client_t *client);
const char *dap_client_host (const dap_client_t *client);
/* The responses owed are forgotten */
void dap_client_cancel (dap_client_t *client);
/* The adapter stopped, a program it is ended: @wait_ms for it to end by itself first */
void dap_client_close (dap_client_t *client, int wait_ms);
void dap_client_free (dap_client_t *client);

#endif
