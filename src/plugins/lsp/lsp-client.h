/** \file lsp-client.h
 *  \brief Header: a client of one language server
 *
 *  The client runs the server, speaks JSON-RPC with it on its stdin and stdout, keeps the
 *  documents it is told of in step with it, and answers what the server asks.  All of it in the
 *  main loop of the program: the pipes are channels of add_select_channel(), and what has to
 *  happen in time is done by lsp_client_tick(), which the owner calls often.
 *
 *  The callbacks of the owner never come from a call of the owner into the client, but for the
 *  end of a request it cancels: they come from the reading of the pipes and from
 *  lsp_client_tick().  The owner does not free a client from them.
 */

#ifndef MC__LSP_CLIENT_H
#define MC__LSP_CLIENT_H

#include <glib.h>
#include <json-glib/json-glib.h>

#include "lsp-config.h"
#include "lsp-text.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* a request not answered in that time is given up */
#define LSP_REQUEST_TIMEOUT_MS    10000
#define LSP_INITIALIZE_TIMEOUT_MS 15000
/* a stop: the time for the server to say goodbye, then to end after SIGTERM */
#define LSP_STOP_TIMEOUT_MS 3000
/* the requests of a client not answered yet */
#define LSP_MAX_PENDING 256
/* a message, and the text of a document */
#define LSP_MAX_MESSAGE (16 * 1024 * 1024)
/* the lines of the log of a client */
#define LSP_LOG_LINES 2000

/*** enums ***************************************************************************************/

typedef enum
{
    LSP_CLIENT_STOPPED = 0,  // not started, or stopped
    LSP_CLIENT_STARTING,     // the server runs, and has not answered initialize yet
    LSP_CLIENT_RUNNING,
    LSP_CLIENT_STOPPING,  // shutdown and exit are sent
    LSP_CLIENT_FAILED     // the server is gone, or speaks no protocol: lsp_client_reason()
} lsp_client_state_t;

typedef enum
{
    LSP_REQUEST_OK = 0,  // @result is the result
    LSP_REQUEST_ERROR,   // @error is the error the server answered
    LSP_REQUEST_TIMEOUT,
    LSP_REQUEST_CANCELLED,
    LSP_REQUEST_CLOSED  // the server is gone, or stopped
} lsp_request_status_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct lsp_client_t lsp_client_t;

/* The end of a request, once for each request taken: @result and @error are the client's */
typedef void (*lsp_response_fn) (lsp_client_t *client, lsp_request_status_t status,
                                 JsonNode *result, JsonObject *error, void *data);

typedef struct
{
    // the state has changed: lsp_client_state()
    void (*state) (void *owner, lsp_client_t *client);
    // the problems of a document, as the server has them now; an empty array: none
    void (*diagnostics) (void *owner, lsp_client_t *client, const char *uri,
                         JsonArray *diagnostics);
    // window/showMessage: @type 1 is an error, 2 a warning, 3 an information, 4 a log
    void (*message) (void *owner, lsp_client_t *client, int type, const char *text);
} lsp_client_handlers_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* A client of the server @backend for the project at @root; what it needs of @backend is copied */
lsp_client_t *lsp_client_new (const lsp_backend_t *backend, const char *root,
                              const lsp_client_handlers_t *handlers, void *owner);
/* The server stopped at once if it runs, the requests closed, the client freed */
void lsp_client_free (lsp_client_t *client);

/* The command the next start runs, @command copied */
void lsp_client_set_command (lsp_client_t *client, char *const *command);
/* The server run and initialized; the documents of the client are opened on it once it is.
   FALSE with @error when it cannot be run. */
gboolean lsp_client_start (lsp_client_t *client, GError **error);
/* shutdown, exit, and the process waited for, SIGTERM and SIGKILL past LSP_STOP_TIMEOUT_MS */
void lsp_client_stop (lsp_client_t *client);
/* The program ends: shutdown and exit sent, the pipes closed, the process left to
   lsp_client_reap_all() */
void lsp_client_quit (lsp_client_t *client);
/* What is due: the timeouts, the stop, what is left to write.  @now is g_get_monotonic_time(). */
void lsp_client_tick (lsp_client_t *client, gint64 now);
/* Whether the client has bytes left to write, which the next tick tries again */
gboolean lsp_client_writing (const lsp_client_t *client);

/* The processes of the servers stopped and not reaped yet: reaped as they end, killed when they
   are late.  lsp_client_reap_tick() is called with the ticks; lsp_client_reap_all() waits for all
   of them, @wait_ms at most before SIGTERM, and kills what is left. */
void lsp_client_reap_tick (gint64 now);
gboolean lsp_client_reaping (void);
void lsp_client_reap_all (int wait_ms);

lsp_client_state_t lsp_client_state (const lsp_client_t *client);
const char *lsp_client_state_name (lsp_client_state_t state);
/* Why it failed or stopped; NULL when nothing went wrong */
const char *lsp_client_reason (const lsp_client_t *client);
const char *lsp_client_backend_id (const lsp_client_t *client);
const char *lsp_client_root (const lsp_client_t *client);
GPid lsp_client_pid (const lsp_client_t *client);
lsp_encoding_t lsp_client_encoding (const lsp_client_t *client);
guint lsp_client_pending (const lsp_client_t *client);
guint lsp_client_documents (const lsp_client_t *client);
/* The lines of the log, oldest first */
const GQueue *lsp_client_log (const lsp_client_t *client);
void lsp_client_log_add (lsp_client_t *client, const char *format, ...) G_GNUC_PRINTF (2, 3);
/* The capabilities the server said, NULL before it said them */
JsonObject *lsp_client_capabilities (const lsp_client_t *client);
/* Whether the capability @provider ("hoverProvider") is there and not false */
gboolean lsp_client_supports (const lsp_client_t *client, const char *provider);
/* Whether @c is one of the characters that start a completion */
gboolean lsp_client_completion_trigger (const lsp_client_t *client, const char *c);
/* Whether @c is one of the triggerCharacters of the options of @provider ("signatureHelpProvider")
 */
gboolean lsp_client_trigger (const lsp_client_t *client, const char *provider, const char *c);

/* A document, @len bytes of UTF-8: opened on the server now, or once it runs */
void lsp_client_doc_open (lsp_client_t *client, const char *uri, const char *language_id,
                          const char *text, gsize len);
/* The text of a document now: sent when it is not what the server has */
void lsp_client_doc_change (lsp_client_t *client, const char *uri, const char *text, gsize len);
/* The document is written: its text as written */
void lsp_client_doc_save (lsp_client_t *client, const char *uri, const char *text, gsize len);
void lsp_client_doc_close (lsp_client_t *client, const char *uri);
gboolean lsp_client_has_doc (const lsp_client_t *client, const char *uri);
/* The version of the text the server has, 0 when it has none */
gint lsp_client_doc_version (const lsp_client_t *client, const char *uri);

/* A request, @params taken (NULL for none), while the server runs; its id, 0 when it is not
   taken, and then @destroy has freed @data already and @fn is not called.  @fn is called once,
   then @destroy. */
guint64 lsp_client_request (lsp_client_t *client, const char *method, JsonNode *params,
                            guint timeout_ms, lsp_response_fn fn, void *data,
                            GDestroyNotify destroy);
/* The request is given up: its @fn is called with LSP_REQUEST_CANCELLED now */
void lsp_client_cancel (lsp_client_t *client, guint64 id);
/* A notification, @params taken; FALSE when the server does not run */
gboolean lsp_client_notify (lsp_client_t *client, const char *method, JsonNode *params);

/*** inline functions ****************************************************************************/

#endif
