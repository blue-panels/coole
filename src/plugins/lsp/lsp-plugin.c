/*
   The language servers in the editor: the files they serve, their problems in the gutter, hover,
   go to definition and completion.

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
#include <string.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/mcconfig.h"
#include "lib/strutil.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/widget.h"

#include "src/keymap.h"
#include "src/fuzzy.h"
#include "src/plugins/build/build-core.h"  // build_detect (), the build directory of a project
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"
#include "src/editor/editnav.h"  // edit_nav_history ()

#include "lib/editor-plugin.h"
#include "lib/plugin-prefs.h"  // mc_plugin_prefs_is_disabled ()

#include "lsp-client.h"
#include "lsp-config.h"
#include "lsp-markdown.h"
#include "lsp-note.h"
#include "lsp-popup.h"
#include "lsp-setup.h"
#include "lsp-text.h"
#include "lsp-wizard.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define LSP_KEYMAP_SECTION "lsp"
#define LSP_MENU           N_ ("&LSP")

/* the servers running at once */
#define LSP_MAX_SERVERS 8
/* a server with no file to serve stops after that */
#define LSP_IDLE_MS 60000
/* the restarts of a server that fails, in LSP_RESTART_WINDOW_MS */
#define LSP_MAX_RESTARTS      3
#define LSP_RESTART_WINDOW_MS 60000
/* a dialog with the errors a server says, not more often */
#define LSP_MESSAGE_GAP_MS 10000
/* the ticks: while a server writes, and else */
#define LSP_TICK_FAST_MS 5
#define LSP_TICK_MS      100

/* the lines of the source of a definition Hover shows at most */
#define LSP_HOVER_SOURCE_LINES 10
/* the lines the help of a call shows at once: the code typed under it stays in sight */
#define LSP_SIGNATURE_LINES 8

/*** file scope type declarations ****************************************************************/

typedef enum
{
    LSP_CMD_COMPLETION = 0,
    LSP_CMD_DEFINITION,
    LSP_CMD_HOVER,
    LSP_CMD_PROBLEMS,
    LSP_CMD_START,
    LSP_CMD_STOP,
    LSP_CMD_RESTART,
    LSP_CMD_STATUS,
    LSP_CMD_LOG,
    LSP_CMD_SETTINGS,
    LSP_CMD_DECLARATION,
    LSP_CMD_SETUP,
    LSP_CMD_REFERENCES,
    LSP_CMD_HISTORY,
    LSP_CMD_SIGNATURE,
    LSP_CMD_COUNT,
    LSP_CMD_NONE = LSP_CMD_COUNT
} lsp_cmd_t;

typedef enum
{
    LSP_MARK_ERROR = 0,
    LSP_MARK_WARNING,
    LSP_MARK_INFO,
    LSP_MARK_COUNT
} lsp_mark_t;

typedef struct lsp_plugin_t lsp_plugin_t;

/* A client of the plugin, with what it does when the server fails */
typedef struct
{
    lsp_plugin_t *plugin;
    lsp_client_t *client;
    char *backend_id;
    char *root;
    char **command;            // of the backend, ${build_dir} in it
    gboolean manual_stop;      // stopped by the user: no autostart, no restart
    gboolean start_failed;     // could not be run: not tried again by itself
    gboolean restart_stopped;  // LSP Restart: started again once stopped
    GArray *failures;          // gint64, the times it failed lately
    gint64 restart_at;         // 0: none
    gint64 idle_since;         // 0: it has files
    gint64 message_at;         // the last dialog of an error it said
    GHashTable *diagnostics;   // uri -> JsonArray
    gboolean retired;          // of the settings before: stopped, and no file goes to it
} lsp_server_t;

/* A file window, served or not */
typedef struct
{
    guint64 id;
    WEdit *edit;
    char *path;  // absolute, as the window has it; NULL for a window with no file
    char *uri;
    char *language;
    lsp_server_t *server;  // NULL when not served
    char *why;             // why it is not served
} lsp_window_t;

typedef enum
{
    LSP_ASK_HOVER,
    LSP_ASK_DEFINITION,
    LSP_ASK_COMPLETION,
    LSP_ASK_DECLARATION,
    LSP_ASK_REFERENCES,
    LSP_ASK_PREVIEW,   // the definition, its source shown in Hover
    LSP_ASK_SYMBOLS,   // the symbols of the file of the definition, for its extent
    LSP_ASK_SIGNATURE  // the parameters of the call at the cursor
} lsp_ask_kind_t;

/* Hover waiting for the source of the definition of its name */
typedef struct lsp_preview_t
{
    guint64 id;
    JsonNode *hover;       // what the server said of the name
    lsp_client_t *client;  // the server asked
    char *uri;             // the file of the definition, opened on the server when @opened
    gboolean opened;
    gint64 line, character;  // the place of the name of the definition
} lsp_preview_t;

/* A request of the user, and its answer once there */
typedef struct
{
    lsp_plugin_t *plugin;
    lsp_ask_kind_t kind;
    guint64 window_id;
    guint64 revision;
    off_t offset;  // of the cursor
    GBytes *text;  // as the server had it
    lsp_encoding_t encoding;
    gboolean triggered;  // by a character typed, not by the user: nothing found is no news
    guint64 session;     // of a completion: the session it is of
    guint64 preview;     // of the source of a definition: the Hover it is of
    guint64 signature;   // of the parameters of a call: the help it is of
    lsp_request_status_t status;
    JsonNode *result;
    char *error;
} lsp_ask_t;

/* A completion going on: the word it is of, the items the server said */
typedef struct lsp_session_t
{
    guint64 id;
    guint64 window_id;
    off_t start;          // where the word starts in the text
    gboolean any;         // asked for, or after a dot: an empty word shows all
    char *trigger;        // the character it came after, NULL for none
    gboolean asking;      // a request out
    char *asked;          // the word as it was asked for
    JsonArray *items;     // of the last answer; NULL before it
    gboolean incomplete;  // the server has more than it said: the list is asked for again
    GBytes *text;         // the text of the answer: its ranges are of it
    off_t asked_at;       // the cursor in it
    lsp_encoding_t encoding;
    char pending;  // '-' or ':' typed, "->" or "::" if the next is '>' or ':'
} lsp_session_t;

struct lsp_plugin_t
{
    mc_editor_host_t *host;
    lsp_config_t *config;
    char *config_errors;
    const char *project;  // the directory of -D
    GPtrArray *servers;   // lsp_server_t
    GPtrArray *windows;   // lsp_window_t
    guint64 next_window_id;
    long commands[LSP_CMD_COUNT];
    int marks[LSP_MARK_COUNT];

    GQueue *answers;                // lsp_ask_t, to show once the editor is idle
    struct lsp_preview_t *preview;  // Hover while the source of its definition is asked for
    char *trigger;                  // a character that starts a completion, typed
    gboolean refilter;              // a key typed in the word completed: the list again
    gboolean later;                 // a call of the editor asked for
    lsp_client_t *completing;
    guint64 completion;      // the request of the completion of the moment
    lsp_session_t *session;  // the completion of the moment, NULL when none
    guint64 next_session;
    guint64 next_preview;
    lsp_popup_t *popup;
    lsp_note_t *note;  // what the server says of a name, Hover
    // the window and the place of the cursor the note is of: a click in the line moves no line
    guint64 note_window;
    off_t note_offset;
    // the parameters of the call the cursor is in, over its line, as it is typed
    lsp_note_t *signature;
    guint64 sig_id;  // the help of the moment, 0 for none
    guint64 next_signature;
    guint64 sig_window;  // its window
    gboolean sig_told;   // asked for by the user: nothing found is said
    gboolean sig_asking;
    off_t sig_offset;  // the cursor and the text as last asked for
    guint64 sig_revision;
    JsonNode *sig_help;  // the last answer, SignatureHelp
    lsp_encoding_t sig_encoding;
    int sig_active;  // the signature shown, of several

    // the ticks: a thread writes a byte in the pipe, the main loop reads it
    int tick_pipe[2];
    GThread *ticker;
    gint ticker_stop;
    gint ticker_on;
    gint ticker_fast;
};

/*** forward declarations (file scope functions) *************************************************/

// the entry of the module
const mc_editor_plugin_t *mc_editor_plugin_register (void);

static void lsp_on_state (void *owner, lsp_client_t *client);
static void lsp_on_diagnostics (void *owner, lsp_client_t *client, const char *uri,
                                JsonArray *diagnostics);
static void lsp_on_message (void *owner, lsp_client_t *client, int type, const char *text);
static void lsp_later (void *data);
static void lsp_session_end (lsp_plugin_t *plugin);
static gboolean lsp_session_in_word (const lsp_session_t *s, WEdit *edit, off_t end);
static char *lsp_menu_shortcut (int action_index);
static void lsp_signature_tick (lsp_plugin_t *plugin);

/*** file scope variables ************************************************************************/

static const mc_ep_command_t lsp_commands[LSP_CMD_COUNT + 1] = {
    { "LspCompletion", N_ ("Complete the word by the language server"), "ctrl-space" },
    { "LspDefinition", N_ ("Go to the definition"), "alt-dot" },
    { "LspHover", N_ ("What the language server says of the word"), "alt-comma" },
    { "LspProblems", N_ ("The problems the language servers see"), NULL },
    { "LspStart", N_ ("Start the language server of the file"), NULL },
    { "LspStop", N_ ("Stop the language server of the file"), NULL },
    { "LspRestart", N_ ("Restart the language server of the file"), NULL },
    { "LspStatus", N_ ("The state of the language servers"), NULL },
    { "LspLog", N_ ("The log of a language server"), NULL },
    { "LspSettings", N_ ("The settings of the language servers"), NULL },
    { "LspDeclaration", N_ ("Go to the declaration"), "alt-gt" },
    { "LspSetup", N_ ("Set the project up for its language server"), NULL },
    { "LspReferences", N_ ("Find the uses of the name"), NULL },
    { "LspHistory", N_ ("The places the jumps went from and to"), NULL },
    { "LspSignature", N_ ("The parameters of the call at the cursor"), "alt-lparenthesis" },
    { NULL, NULL, NULL },
};

static const mc_ep_marker_kind_t lsp_mark_kinds[LSP_MARK_COUNT] = {
    { "lsp.error", "lsp-error-char", "E", "E", "lsperror", NULL, NULL, 5, "brightred" },
    { "lsp.warning", "lsp-warning-char", "W", "W", "lspwarning", NULL, NULL, 4, "yellow" },
    { "lsp.info", "lsp-info-char", "i", "i", "lspinfo", NULL, NULL, 3, "cyan" },
};

static const lsp_client_handlers_t lsp_handlers = {
    lsp_on_state,
    lsp_on_diagnostics,
    lsp_on_message,
};

/* the plugin, for the menu and the ticker; one editor, one plugin */
static lsp_plugin_t *lsp_plugin = NULL;

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

static gint64
lsp_member_int (JsonObject *object, const char *name, gint64 fallback)
{
    JsonNode *node = lsp_member (object, name);

    return node != NULL && JSON_NODE_HOLDS_VALUE (node)
            && json_node_get_value_type (node) == G_TYPE_INT64
        ? json_node_get_int (node)
        : fallback;
}

/* --------------------------------------------------------------------------------------------- */

static JsonNode *
lsp_object_node (JsonObject *object)
{
    JsonNode *node = json_node_new (JSON_NODE_OBJECT);

    json_node_take_object (node, object);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* The start of a range: line and character; FALSE when it has none */
static gboolean
lsp_range_start (JsonObject *range, gint64 *line, gint64 *character)
{
    JsonObject *start = lsp_member_object (range, "start");

    *line = lsp_member_int (start, "line", -1);
    *character = lsp_member_int (start, "character", -1);
    return *line >= 0 && *character >= 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The bytes of a range of @text; FALSE when it is no range of it */
static gboolean
lsp_range_offsets (JsonObject *range, const char *text, gsize len, lsp_encoding_t encoding,
                   gsize *from, gsize *to)
{
    JsonObject *start = lsp_member_object (range, "start");
    JsonObject *end = lsp_member_object (range, "end");

    return start != NULL && end != NULL
        && lsp_text_offset (text, len, lsp_member_int (start, "line", -1),
                            lsp_member_int (start, "character", -1), encoding, from)
        && lsp_text_offset (text, len, lsp_member_int (end, "line", -1),
                            lsp_member_int (end, "character", -1), encoding, to)
        && *from <= *to;
}

/* --------------------------------------------------------------------------------------------- */

/* A text of a server to show: the controls of the terminal are not passed on */
static char *
lsp_shown (const char *text, gsize max_lines)
{
    GString *out = g_string_new (NULL);
    gsize lines = 1;
    const char *p;

    for (p = text; p != NULL && *p != '\0'; p++)
    {
        const unsigned char c = (unsigned char) *p;

        if (c == '\n')
        {
            if (++lines > max_lines)
            {
                g_string_append (out, "\n...");
                break;
            }
            g_string_append_c (out, '\n');
        }
        else if (c == '\t')
            g_string_append (out, "    ");
        else if (c < 0x20 || c == 0x7f)
            g_string_append_c (out, '?');
        else
            g_string_append_c (out, (char) c);
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/* The ticker */

static gpointer
lsp_ticker (gpointer data)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;

    while (g_atomic_int_get (&plugin->ticker_stop) == 0)
    {
        g_usleep ((g_atomic_int_get (&plugin->ticker_fast) != 0 ? LSP_TICK_FAST_MS : LSP_TICK_MS)
                  * 1000);
        // a full pipe has a tick waiting already: what write () says does not matter
        if (g_atomic_int_get (&plugin->ticker_on) != 0)
        {
            const ssize_t written = write (plugin->tick_pipe[1], "t", 1);

            (void) written;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/* The windows and the servers */

static lsp_window_t *
lsp_window_of (lsp_plugin_t *plugin, const WEdit *edit)
{
    guint i;

    for (i = 0; i < plugin->windows->len; i++)
    {
        lsp_window_t *w = g_ptr_array_index (plugin->windows, i);

        if (w->edit == edit)
            return w;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static lsp_window_t *
lsp_window_by_id (lsp_plugin_t *plugin, guint64 id)
{
    guint i;

    for (i = 0; i < plugin->windows->len; i++)
    {
        lsp_window_t *w = g_ptr_array_index (plugin->windows, i);

        if (w->id == id)
            return w;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The text of a window, when a server can have it: UTF-8, no NUL, not too big; else NULL and
   @why */
static char *
lsp_window_text (lsp_plugin_t *plugin, WEdit *edit, gsize *len, const char **why)
{
    char *text = plugin->host->get_text (plugin->host, edit, len);

    if (text == NULL)
        *why = _ ("the window has no text");
    else if (*len > LSP_MAX_MESSAGE / 2)
        *why = _ ("the file is too big for the language server");
    else if (!g_utf8_validate (text, (gssize) *len, NULL))
        *why = _ ("the file is not UTF-8 text");
    else
        return text;
    g_free (text);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static guint
lsp_running_servers (const lsp_plugin_t *plugin)
{
    guint i, n = 0;

    for (i = 0; i < plugin->servers->len; i++)
    {
        const lsp_server_t *server = g_ptr_array_index (plugin->servers, i);
        const lsp_client_state_t state = lsp_client_state (server->client);

        if (state == LSP_CLIENT_STARTING || state == LSP_CLIENT_RUNNING
            || state == LSP_CLIENT_STOPPING)
            n++;
    }
    return n;
}

/* --------------------------------------------------------------------------------------------- */

static lsp_server_t *
lsp_server_find (lsp_plugin_t *plugin, const char *backend_id, const char *root)
{
    guint i;

    for (i = 0; i < plugin->servers->len; i++)
    {
        lsp_server_t *server = g_ptr_array_index (plugin->servers, i);

        if (!server->retired && strcmp (server->backend_id, backend_id) == 0
            && strcmp (server->root, root) == 0)
            return server;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_server_free (gpointer data)
{
    lsp_server_t *server = (lsp_server_t *) data;

    lsp_client_free (server->client);
    g_strfreev (server->command);
    g_free (server->backend_id);
    g_free (server->root);
    g_array_free (server->failures, TRUE);
    g_hash_table_destroy (server->diagnostics);
    g_free (server);
}

/* --------------------------------------------------------------------------------------------- */

static lsp_server_t *
lsp_server_new (lsp_plugin_t *plugin, const lsp_backend_t *backend, const char *root)
{
    lsp_server_t *server = g_new0 (lsp_server_t, 1);

    server->plugin = plugin;
    server->backend_id = g_strdup (backend->id);
    server->root = g_strdup (root);
    server->command = g_strdupv (backend->command);
    server->client = lsp_client_new (backend, root, &lsp_handlers, server);
    server->failures = g_array_new (FALSE, FALSE, sizeof (gint64));
    server->diagnostics =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) json_array_unref);
    g_ptr_array_add (plugin->servers, server);
    return server;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_ticker_update (lsp_plugin_t *plugin)
{
    gboolean fast = FALSE;
    guint i;

    for (i = 0; i < plugin->servers->len && !fast; i++)
        fast =
            lsp_client_writing (((lsp_server_t *) g_ptr_array_index (plugin->servers, i))->client);
    g_atomic_int_set (&plugin->ticker_on, plugin->servers->len > 0 || lsp_client_reaping ());
    g_atomic_int_set (&plugin->ticker_fast, fast);
}

/* --------------------------------------------------------------------------------------------- */

/* The command of the server for its project now: the build directory may have come since */
static void
lsp_server_command (lsp_server_t *server)
{
    build_info_t *build = build_detect (server->root);
    const char *build_dir =
        build != NULL && (build->system == BUILD_MESON || build->system == BUILD_CMAKE)
        ? build->build_dir
        : NULL;
    char **command = lsp_command_expand (server->command, build_dir);

    lsp_client_set_command (server->client, command);
    g_strfreev (command);
    build_info_free (build);
}

/* --------------------------------------------------------------------------------------------- */

/* The server run; @manual: the user asked, and is told when it cannot be */
static gboolean
lsp_server_start (lsp_server_t *server, gboolean manual)
{
    lsp_plugin_t *plugin = server->plugin;
    GError *error = NULL;

    if (manual)
    {
        server->manual_stop = FALSE;
        server->start_failed = FALSE;
        g_array_set_size (server->failures, 0);
    }
    server->restart_at = 0;
    if (lsp_running_servers (plugin) >= LSP_MAX_SERVERS)
    {
        lsp_client_log_add (server->client, "not started: %d servers run already", LSP_MAX_SERVERS);
        if (manual)
            plugin->host->message (plugin->host, D_ERROR, _ ("Language server"),
                                   _ ("Too many language servers run already."));
        return FALSE;
    }
    lsp_server_command (server);
    if (!lsp_client_start (server->client, &error))
    {
        server->start_failed = TRUE;
        if (manual)
        {
            char *text = g_strdup_printf (_ ("Cannot run %s:\n%s"), server->backend_id,
                                          error != NULL ? error->message : "");

            plugin->host->message (plugin->host, D_ERROR, _ ("Language server"), text);
            g_free (text);
        }
        g_clear_error (&error);
        return FALSE;
    }
    lsp_ticker_update (plugin);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether what happens @when happens now: on a project of -D for "ide" */
static gboolean
lsp_now (const lsp_plugin_t *plugin, lsp_when_t when)
{
    switch (when)
    {
    case LSP_WHEN_ALWAYS:
        return TRUE;
    case LSP_WHEN_IDE:
        return plugin->project != NULL;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the server of a file starts by itself */
static gboolean
lsp_autostart (const lsp_plugin_t *plugin, const lsp_backend_t *backend)
{
    return lsp_now (plugin, lsp_backend_autostart (plugin->config, backend));
}

/* --------------------------------------------------------------------------------------------- */
/* The marks of the problems */

static lsp_mark_t
lsp_mark_of (JsonObject *diagnostic)
{
    switch (lsp_member_int (diagnostic, "severity", 1))
    {
    case 2:
        return LSP_MARK_WARNING;
    case 3:
    case 4:
        return LSP_MARK_INFO;
    default:
        return LSP_MARK_ERROR;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The marks of the problems of @uri in the windows of the file */
static void
lsp_marks_refresh (lsp_plugin_t *plugin, lsp_server_t *server, const char *uri)
{
    mc_editor_host_t *host = plugin->host;
    JsonArray *diagnostics = g_hash_table_lookup (server->diagnostics, uri);
    const char *done = NULL;
    guint i, k;

    if (host->marker_kind == NULL)
        return;
    for (i = 0; i < plugin->windows->len; i++)
    {
        const lsp_window_t *w = g_ptr_array_index (plugin->windows, i);

        // the marks are of a file: once for all its windows
        if (w->server != server || g_strcmp0 (w->uri, uri) != 0 || g_strcmp0 (w->path, done) == 0)
            continue;
        done = w->path;
        for (k = 0; k < LSP_MARK_COUNT; k++)
            if (plugin->marks[k] >= 0)
                host->clear_markers (host, w->path, plugin->marks[k]);
        for (k = 0; diagnostics != NULL && k < json_array_get_length (diagnostics); k++)
        {
            JsonNode *node = json_array_get_element (diagnostics, k);
            gint64 line, character;

            if (JSON_NODE_HOLDS_OBJECT (node)
                && lsp_range_start (lsp_member_object (json_node_get_object (node), "range"), &line,
                                    &character)
                && plugin->marks[lsp_mark_of (json_node_get_object (node))] >= 0)
                host->set_marker (host, w->path, (long) line + 1,
                                  plugin->marks[lsp_mark_of (json_node_get_object (node))], TRUE);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The problems a server said dropped, and their marks */
static void
lsp_server_forget_problems (lsp_server_t *server)
{
    GList *uris = g_hash_table_get_keys (server->diagnostics);
    GList *l;

    for (l = uris; l != NULL; l = l->next)
    {
        char *uri = g_strdup (l->data);

        g_hash_table_remove (server->diagnostics, uri);
        lsp_marks_refresh (server->plugin, server, uri);
        g_free (uri);
    }
    g_list_free (uris);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_window_unbind (lsp_plugin_t *plugin, lsp_window_t *w)
{
    mc_editor_host_t *host = plugin->host;
    guint i, k;

    if (w->server != NULL && w->uri != NULL)
    {
        gboolean other = FALSE;

        // another window of the same file keeps it open on the server
        for (i = 0; i < plugin->windows->len; i++)
        {
            const lsp_window_t *o = g_ptr_array_index (plugin->windows, i);

            other = other || (o != w && o->server == w->server && g_strcmp0 (o->uri, w->uri) == 0);
        }
        if (!other)
            lsp_client_doc_close (w->server->client, w->uri);
        if (!other && w->path != NULL && host->marker_kind != NULL)
            for (k = 0; k < LSP_MARK_COUNT; k++)
                if (plugin->marks[k] >= 0)
                    host->clear_markers (host, w->path, plugin->marks[k]);
    }
    g_clear_pointer (&w->path, g_free);
    g_clear_pointer (&w->uri, g_free);
    g_clear_pointer (&w->language, g_free);
    g_clear_pointer (&w->why, g_free);
    w->server = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the window has the marks of the problems of its file, when it has problems */
static gboolean
lsp_window_has_marks (lsp_plugin_t *plugin, const lsp_window_t *w)
{
    mc_editor_host_t *host = plugin->host;
    gboolean has = FALSE;
    int k;

    if (host->marker_lines == NULL || !g_hash_table_contains (w->server->diagnostics, w->uri))
        return TRUE;
    for (k = 0; k < LSP_MARK_COUNT && !has; k++)
        if (plugin->marks[k] >= 0)
        {
            GArray *lines = host->marker_lines (host, w->path, plugin->marks[k]);

            has = lines != NULL && lines->len > 0;
            if (lines != NULL)
                g_array_free (lines, TRUE);
        }
    return has;
}

/* --------------------------------------------------------------------------------------------- */

/* The window served by the server of its file, if it has one */
static void
lsp_window_bind (lsp_plugin_t *plugin, lsp_window_t *w, char *path)
{
    const lsp_backend_t *backend;
    const char *language = NULL;
    const char *why = NULL;
    lsp_server_t *server;
    char *root, *text;
    gsize len = 0;

    w->path = path;
    if (path == NULL)
    {
        w->why = g_strdup (_ ("the window has no file"));
        return;
    }
    backend = lsp_config_backend_for (plugin->config, path, &language);
    if (backend == NULL)
    {
        w->why = g_strdup (_ ("no language server for this file in lsp.json, or it is not "
                              "installed"));
        return;
    }
    root = lsp_find_root (backend, path, plugin->project);
    if (root == NULL)
    {
        w->why = g_strdup_printf (_ ("the file is in no project, and %s takes no single files"),
                                  backend->id);
        return;
    }
    w->uri = lsp_uri_from_path (path);
    text = w->uri != NULL ? lsp_window_text (plugin, w->edit, &len, &why) : NULL;
    if (text == NULL)
    {
        w->why = g_strdup (why != NULL ? why : _ ("the file has no URI"));
        g_clear_pointer (&w->uri, g_free);
        g_free (root);
        return;
    }
    server = lsp_server_find (plugin, backend->id, root);
    if (server == NULL)
        server = lsp_server_new (plugin, backend, root);
    g_free (root);
    w->server = server;
    w->language = g_strdup (language);
    server->idle_since = 0;
    lsp_client_doc_open (server->client, w->uri, language, text, len);
    g_free (text);
    if (lsp_client_state (server->client) == LSP_CLIENT_STOPPED && !server->manual_stop
        && !server->start_failed && server->restart_at == 0 && lsp_autostart (plugin, backend))
        (void) lsp_server_start (server, FALSE);
    lsp_marks_refresh (plugin, server, w->uri);
}

/* --------------------------------------------------------------------------------------------- */

/* The absolute path of the file of a window, NULL when it has none */
static char *
lsp_window_path (lsp_plugin_t *plugin, WEdit *edit)
{
    char *file = plugin->host->get_current_file (plugin->host, edit);
    char *path;

    if (file == NULL || *file == '\0')
    {
        g_free (file);
        return NULL;
    }
    path = g_canonicalize_filename (file, NULL);
    g_free (file);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* The window of @edit as the plugin has it, bound again when the window shows another file now;
   @text: its text sent to the server when it has changed */
static lsp_window_t *
lsp_window_sync (lsp_plugin_t *plugin, WEdit *edit, gboolean text)
{
    lsp_window_t *w = lsp_window_of (plugin, edit);
    char *path = lsp_window_path (plugin, edit);

    if (w == NULL)
    {
        w = g_new0 (lsp_window_t, 1);
        w->id = ++plugin->next_window_id;
        w->edit = edit;
        g_ptr_array_add (plugin->windows, w);
    }
    else if (g_strcmp0 (w->path, path) == 0)
    {
        g_free (path);
        if (text && w->server != NULL)
        {
            const char *why = NULL;
            gsize len = 0;
            char *now = lsp_window_text (plugin, edit, &len, &why);

            if (now != NULL)
                lsp_client_doc_change (w->server->client, w->uri, now, len);
            g_free (now);
            // a file loaded again in its window, by a jump in it, has lost its marks
            if (!lsp_window_has_marks (plugin, w))
                lsp_marks_refresh (plugin, w->server, w->uri);
        }
        return w;
    }
    else
        // the window shows another file: a jump, Save as
        lsp_window_unbind (plugin, w);
    lsp_window_bind (plugin, w, path);
    lsp_ticker_update (plugin);
    return w;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_window_forget (lsp_plugin_t *plugin, WEdit *edit)
{
    lsp_window_t *w = lsp_window_of (plugin, edit);

    if (w == NULL)
        return;
    if (plugin->session != NULL && plugin->session->window_id == w->id)
        lsp_session_end (plugin);
    lsp_window_unbind (plugin, w);
    g_ptr_array_remove (plugin->windows, w);
    g_free (w);
}

/* --------------------------------------------------------------------------------------------- */
/* What the clients tell */

static void
lsp_on_state (void *owner, lsp_client_t *client)
{
    lsp_server_t *server = (lsp_server_t *) owner;
    lsp_plugin_t *plugin = server->plugin;
    const lsp_client_state_t state = lsp_client_state (client);
    const gint64 now = g_get_monotonic_time ();

    if (state != LSP_CLIENT_STOPPED && state != LSP_CLIENT_FAILED)
        return;
    lsp_server_forget_problems (server);
    if (state == LSP_CLIENT_STOPPED && server->restart_stopped)
    {
        server->restart_stopped = FALSE;
        server->restart_at = now;
    }
    else if (state == LSP_CLIENT_FAILED && !server->manual_stop)
    {
        guint i;

        for (i = 0; i < server->failures->len; i++)
            if (g_array_index (server->failures, gint64, i)
                < now - (gint64) LSP_RESTART_WINDOW_MS * 1000)
                g_array_remove_index (server->failures, i--);
        if (server->failures->len < LSP_MAX_RESTARTS)
        {
            server->restart_at = now + ((gint64) 1000 << server->failures->len) * 1000;
            g_array_append_val (server->failures, now);
            lsp_client_log_add (client, "restart in %d s", 1 << (server->failures->len - 1));
        }
        else
        {
            char *text = g_strdup_printf (
                _ ("%s failed: %s\nLSP Restart starts it again."), server->backend_id,
                lsp_client_reason (client) != NULL ? lsp_client_reason (client) : "");

            lsp_on_message (server, client, 1, text);
            g_free (text);
        }
    }
    plugin->host->redraw (plugin->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_on_diagnostics (void *owner, lsp_client_t *client, const char *uri, JsonArray *diagnostics)
{
    lsp_server_t *server = (lsp_server_t *) owner;

    (void) client;
    if (json_array_get_length (diagnostics) == 0)
        g_hash_table_remove (server->diagnostics, uri);
    else
        g_hash_table_insert (server->diagnostics, g_strdup (uri), json_array_ref (diagnostics));
    lsp_marks_refresh (server->plugin, server, uri);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_ask_later (lsp_plugin_t *plugin)
{
    if (plugin->later)
        return;
    plugin->later = TRUE;
    plugin->host->call_later (plugin->host, lsp_later, plugin);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_on_message (void *owner, lsp_client_t *client, int type, const char *text)
{
    lsp_server_t *server = (lsp_server_t *) owner;
    const gint64 now = g_get_monotonic_time ();
    lsp_ask_t *ask;

    (void) client;
    if (type != 1 || now - server->message_at < (gint64) LSP_MESSAGE_GAP_MS * 1000)
        return;
    server->message_at = now;
    // a dialog, but not from the reading of a pipe
    ask = g_new0 (lsp_ask_t, 1);
    ask->plugin = server->plugin;
    ask->status = LSP_REQUEST_ERROR;
    ask->error = g_strdup_printf ("%s: %s", server->backend_id, text);
    g_queue_push_tail (server->plugin->answers, ask);
    lsp_ask_later (server->plugin);
}

/* --------------------------------------------------------------------------------------------- */
/* The ticks */

static void
lsp_tick (lsp_plugin_t *plugin)
{
    const gint64 now = g_get_monotonic_time ();
    guint i;

    // the list of a completion goes once the cursor is put out of its word, by the mouse
    if (plugin->session != NULL)
    {
        const lsp_window_t *w = lsp_window_by_id (plugin, plugin->session->window_id);

        if (w == NULL
            || !lsp_session_in_word (plugin->session, w->edit,
                                     w->edit->buffer.curs1
                                         - (plugin->session->pending != 0 ? 1 : 0)))
            lsp_session_end (plugin);
    }
    lsp_signature_tick (plugin);
    // the note goes too once the cursor is put elsewhere, in its line too
    if (lsp_note_there (plugin->note))
    {
        const lsp_window_t *w = lsp_window_by_id (plugin, plugin->note_window);

        // by the mouse, out of a key: no one draws the screen after it
        if (w == NULL || w->edit->buffer.curs1 != plugin->note_offset)
        {
            lsp_note_hide (plugin->note);
            tty_refresh ();
        }
    }

    for (i = 0; i < plugin->servers->len; i++)
    {
        lsp_server_t *server = g_ptr_array_index (plugin->servers, i);
        const lsp_client_state_t state = lsp_client_state (server->client);

        lsp_client_tick (server->client, now);
        if (server->restart_at != 0 && now >= server->restart_at)
            (void) lsp_server_start (server, FALSE);
        if (lsp_client_documents (server->client) > 0 || state != LSP_CLIENT_RUNNING)
            server->idle_since = 0;
        else if (server->idle_since == 0)
            server->idle_since = now;
        else if (now - server->idle_since > (gint64) LSP_IDLE_MS * 1000)
        {
            lsp_client_log_add (server->client, "no file to serve");
            lsp_client_stop (server->client);
        }
    }
    // those with nothing to do go; not from what a client tells
    for (i = 0; i < plugin->servers->len; i++)
    {
        lsp_server_t *server = g_ptr_array_index (plugin->servers, i);
        const lsp_client_state_t state = lsp_client_state (server->client);

        if (lsp_client_documents (server->client) == 0 && server->restart_at == 0
            && (state == LSP_CLIENT_STOPPED || state == LSP_CLIENT_FAILED))
        {
            g_ptr_array_remove_index (plugin->servers, i--);
            lsp_server_free (server);
        }
    }
    lsp_client_reap_tick (now);
    lsp_ticker_update (plugin);
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_tick_ready (int fd, void *data)
{
    char buf[64];

    while (read (fd, buf, sizeof (buf)) > 0)
        ;
    lsp_tick ((lsp_plugin_t *) data);
    // an answer given up for no answer is shown, or ends what waited for it, now: not with the
    // next key
    return ((lsp_plugin_t *) data)->later ? 1 : 0;
}

/* --------------------------------------------------------------------------------------------- */
/* The requests of the user */

static void
lsp_ask_free (lsp_ask_t *ask)
{
    if (ask->text != NULL)
        g_bytes_unref (ask->text);
    if (ask->result != NULL)
        json_node_unref (ask->result);
    g_free (ask->error);
    g_free (ask);
}

/* --------------------------------------------------------------------------------------------- */

/* The answer of a request: kept to be shown once the editor is idle */
static void
lsp_answered (lsp_client_t *client, lsp_request_status_t status, JsonNode *result,
              JsonObject *error, void *data)
{
    lsp_ask_t *ask = (lsp_ask_t *) data;
    lsp_plugin_t *plugin = ask->plugin;

    if (ask->kind == LSP_ASK_COMPLETION && plugin->completing == client)
    {
        plugin->completing = NULL;
        plugin->completion = 0;
    }
    /* given up, or the server is gone: nothing to show; but a completion waits for its answer,
       and is ended by one that will not come, Hover waits for the source of the definition, and
       is shown without it, and the help of a call is asked for again once answered */
    if (status == LSP_REQUEST_CANCELLED
        || (status != LSP_REQUEST_OK && status != LSP_REQUEST_ERROR
            && ask->kind != LSP_ASK_COMPLETION && ask->kind != LSP_ASK_PREVIEW
            && ask->kind != LSP_ASK_SYMBOLS && ask->kind != LSP_ASK_SIGNATURE))
    {
        lsp_ask_free (ask);
        return;
    }
    ask->status = status;
    ask->result = result != NULL ? json_node_copy (result) : NULL;
    if (error != NULL)
        ask->error = g_strdup (lsp_member_string (error, "message") != NULL
                                   ? lsp_member_string (error, "message")
                                   : _ ("the language server answered with an error"));
    g_queue_push_tail (plugin->answers, ask);
    lsp_ask_later (plugin);
}

/* --------------------------------------------------------------------------------------------- */

/* The window of @edit, served by a server that runs; else NULL, and the user told why when
   @tell */
static lsp_window_t *
lsp_window_served (lsp_plugin_t *plugin, WEdit *edit, gboolean tell)
{
    lsp_window_t *w;
    const char *why = NULL;
    char *text = NULL;

    if (edit == NULL)
        return NULL;
    w = lsp_window_sync (plugin, edit, TRUE);
    if (w->server == NULL)
        why = w->why;
    else
        switch (lsp_client_state (w->server->client))
        {
        case LSP_CLIENT_RUNNING:
            return w;
        case LSP_CLIENT_STARTING:
            why = _ ("the language server is starting");
            break;
        case LSP_CLIENT_FAILED:
            text = g_strdup_printf (_ ("the language server failed: %s"),
                                    lsp_client_reason (w->server->client) != NULL
                                        ? lsp_client_reason (w->server->client)
                                        : "");
            why = text;
            break;
        default:
            why = _ ("the language server is not running (LSP Start)");
            break;
        }
    if (tell)
        plugin->host->message (plugin->host, D_NORMAL, _ ("Language server"), why);
    g_free (text);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* A request at the cursor of a window: the text the server has first, then the request */
static gboolean
lsp_ask (lsp_plugin_t *plugin, WEdit *edit, lsp_ask_kind_t kind, const char *trigger)
{
    static const char *const methods[] = {
        "textDocument/hover",          "textDocument/definition",   "textDocument/completion",
        "textDocument/declaration",    "textDocument/references",   "textDocument/definition",
        "textDocument/documentSymbol", "textDocument/signatureHelp"
    };
    static const char *const providers[] = { "hoverProvider",          "definitionProvider",
                                             "completionProvider",     "declarationProvider",
                                             "referencesProvider",     "definitionProvider",
                                             "documentSymbolProvider", "signatureHelpProvider" };
    lsp_window_t *w = lsp_window_served (plugin, edit, trigger == NULL);
    lsp_client_t *client;
    const char *why = NULL;
    JsonObject *params, *document, *position;
    lsp_ask_t *ask;
    guint line = 0, character = 0;
    gsize len = 0;
    char *text;
    guint64 id;

    if (w == NULL)
        return FALSE;
    client = w->server->client;
    if (!lsp_client_supports (client, providers[kind]))
    {
        if (trigger == NULL)
            plugin->host->message (plugin->host, D_NORMAL, _ ("Language server"),
                                   _ ("The language server cannot do that."));
        return FALSE;
    }
    text = lsp_window_text (plugin, edit, &len, &why);
    if (text == NULL)
        return FALSE;
    // the text the server has is that of the request: sent before it, if not sent yet
    lsp_client_doc_change (client, w->uri, text, len);
    ask = g_new0 (lsp_ask_t, 1);
    ask->plugin = plugin;
    ask->kind = kind;
    ask->window_id = w->id;
    ask->revision = plugin->host->get_revision (plugin->host, edit);
    ask->offset = edit->buffer.curs1;
    ask->triggered = trigger != NULL;
    ask->session = kind == LSP_ASK_COMPLETION && plugin->session != NULL ? plugin->session->id : 0;
    ask->preview = kind == LSP_ASK_PREVIEW && plugin->preview != NULL ? plugin->preview->id : 0;
    ask->signature = kind == LSP_ASK_SIGNATURE ? plugin->sig_id : 0;
    ask->encoding = lsp_client_encoding (client);
    ask->text = g_bytes_new_take (text, len);
    (void) lsp_text_position (text, len, (gsize) ask->offset, ask->encoding, &line, &character);

    params = json_object_new ();
    document = json_object_new ();
    json_object_set_string_member (document, "uri", w->uri);
    json_object_set_object_member (params, "textDocument", document);
    position = json_object_new ();
    json_object_set_int_member (position, "line", line);
    json_object_set_int_member (position, "character", character);
    json_object_set_object_member (params, "position", position);
    if (kind == LSP_ASK_REFERENCES)
    {
        JsonObject *context = json_object_new ();

        // the definition is a use to see too
        json_object_set_boolean_member (context, "includeDeclaration", TRUE);
        json_object_set_object_member (params, "context", context);
    }
    if (kind == LSP_ASK_COMPLETION)
    {
        JsonObject *context = json_object_new ();

        json_object_set_int_member (context, "triggerKind", trigger != NULL ? 2 : 1);
        if (trigger != NULL)
            json_object_set_string_member (context, "triggerCharacter", trigger);
        json_object_set_object_member (params, "context", context);
        // the completion of before is of no use now
        if (plugin->completing != NULL && plugin->completion != 0)
            lsp_client_cancel (plugin->completing, plugin->completion);
    }
    id = lsp_client_request (client, methods[kind], lsp_object_node (params), 0, lsp_answered, ask,
                             NULL);
    if (id == 0)
    {
        lsp_ask_free (ask);
        return FALSE;
    }
    if (kind == LSP_ASK_COMPLETION)
    {
        plugin->completing = client;
        plugin->completion = id;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/* Hover */

/* The text of MarkupContent, MarkedString, or an array of them, added to @out; @markdown set
   when one of them is Markdown */
static void
lsp_hover_text (JsonNode *node, GString *out, gboolean *markdown)
{
    if (node == NULL)
        return;
    if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
    {
        // a MarkedString alone is Markdown
        if (out->len > 0)
            g_string_append (out, "\n\n");
        g_string_append (out, json_node_get_string (node));
        *markdown = TRUE;
    }
    else if (JSON_NODE_HOLDS_OBJECT (node))
    {
        JsonObject *o = json_node_get_object (node);
        const char *language = lsp_member_string (o, "language");
        const char *value = lsp_member_string (o, "value");

        if (value == NULL)
            return;
        if (out->len > 0)
            g_string_append (out, "\n\n");
        // MarkedString of a language is code; MarkupContent says what it is
        if (language != NULL)
        {
            g_string_append_printf (out, "```%s\n%s\n```", language, value);
            *markdown = TRUE;
        }
        else
        {
            g_string_append (out, value);
            if (g_strcmp0 (lsp_member_string (o, "kind"), "markdown") == 0)
                *markdown = TRUE;
        }
    }
    else if (JSON_NODE_HOLDS_ARRAY (node))
    {
        JsonArray *array = json_node_get_array (node);
        guint i;

        for (i = 0; i < json_array_get_length (array); i++)
            lsp_hover_text (json_array_get_element (array, i), out, markdown);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The text of a file: that of a window of it, else the file; NULL when there is none */
static char *
lsp_file_text (lsp_plugin_t *plugin, const char *path, gsize *len)
{
    char *text = NULL;
    guint i;

    for (i = 0; i < plugin->windows->len && text == NULL; i++)
    {
        const lsp_window_t *w = g_ptr_array_index (plugin->windows, i);
        char *canonical = lsp_canonical_path (w->path);

        if (g_strcmp0 (canonical, path) == 0)
            text = plugin->host->get_text (plugin->host, w->edit, len);
        g_free (canonical);
    }
    if (text == NULL && !g_file_get_contents (path, &text, len, NULL))
        text = NULL;
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* The lines @first to @last, from 0, of a file, whole; NULL when there are none */
static char *
lsp_source_lines (lsp_plugin_t *plugin, const char *path, gint64 first, gint64 last)
{
    gsize len = 0, from = 0, to = 0;
    char *text = lsp_file_text (plugin, path, &len);
    char *source = NULL;

    if (text != NULL && lsp_text_offset (text, len, first, 0, LSP_ENCODING_UTF8, &from))
    {
        // to the end of the last line
        if (!lsp_text_offset (text, len, last, 0, LSP_ENCODING_UTF8, &to))
            to = from;
        while (to < len && text[to] != '\n')
            to++;
        source = g_strndup (text + from, to - from);
    }
    g_free (text);
    return source;
}

/* --------------------------------------------------------------------------------------------- */

/* The first Location or LocationLink of the answer of a definition, NULL when there is none */
static JsonObject *
lsp_first_location (JsonNode *result)
{
    if (result != NULL && JSON_NODE_HOLDS_ARRAY (result)
        && json_array_get_length (json_node_get_array (result)) > 0)
        result = json_array_get_element (json_node_get_array (result), 0);
    return result != NULL && JSON_NODE_HOLDS_OBJECT (result) ? json_node_get_object (result) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the place @line, @character is in @range */
static gboolean
lsp_range_has (JsonObject *range, gint64 line, gint64 character)
{
    JsonObject *start = lsp_member_object (range, "start");
    JsonObject *end = lsp_member_object (range, "end");
    const gint64 l0 = lsp_member_int (start, "line", -1);
    const gint64 c0 = lsp_member_int (start, "character", 0);
    const gint64 l1 = lsp_member_int (end, "line", -1);
    const gint64 c1 = lsp_member_int (end, "character", 0);

    if (l0 < 0 || l1 < 0)
        return FALSE;
    return (line > l0 || (line == l0 && character >= c0))
        && (line < l1 || (line == l1 && character <= c1));
}

/* --------------------------------------------------------------------------------------------- */

/* The extent of the symbol of @symbols, DocumentSymbol or SymbolInformation, the deepest, whose
   name is at @line, @character: its first and last lines; FALSE when there is none */
static gboolean
lsp_symbol_extent (JsonNode *symbols, gint64 line, gint64 character, gint64 *first, gint64 *last)
{
    JsonArray *array;
    guint i;

    if (symbols == NULL || !JSON_NODE_HOLDS_ARRAY (symbols))
        return FALSE;
    array = json_node_get_array (symbols);
    for (i = 0; i < json_array_get_length (array); i++)
    {
        JsonNode *node = json_array_get_element (array, i);
        JsonObject *symbol, *range, *name;
        gboolean found = FALSE;

        if (!JSON_NODE_HOLDS_OBJECT (node))
            continue;
        symbol = json_node_get_object (node);
        range = lsp_member_object (symbol, "range");
        name = lsp_member_object (symbol, "selectionRange");
        // SymbolInformation: the range of its location is all of it
        if (range == NULL)
            range = name = lsp_member_object (lsp_member_object (symbol, "location"), "range");
        if (!lsp_range_has (range, line, character))
            continue;
        if (lsp_range_has (name, line, character))
        {
            *first = lsp_member_int (lsp_member_object (range, "start"), "line", 0);
            *last = lsp_member_int (lsp_member_object (range, "end"), "line", *first);
            found = TRUE;
        }
        // a symbol in it is nearer
        if (lsp_symbol_extent (lsp_member (symbol, "children"), line, character, first, last)
            || found)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @client is that of a server still there */
static gboolean
lsp_client_alive (lsp_plugin_t *plugin, const lsp_client_t *client)
{
    guint i;

    for (i = 0; i < plugin->servers->len; i++)
        if (((const lsp_server_t *) g_ptr_array_index (plugin->servers, i))->client == client)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a window served by @client has the document @uri */
static gboolean
lsp_doc_in_window (lsp_plugin_t *plugin, const lsp_client_t *client, const char *uri)
{
    guint i;

    for (i = 0; i < plugin->windows->len; i++)
    {
        const lsp_window_t *w = g_ptr_array_index (plugin->windows, i);

        if (w->server != NULL && w->server->client == client && g_strcmp0 (w->uri, uri) == 0)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_preview_free (lsp_plugin_t *plugin, lsp_preview_t *preview)
{
    if (preview == NULL)
        return;
    /* the file opened for its symbols is the server's no more, unless a window has it now: the
       user may have gone to it meanwhile */
    if (preview->opened && lsp_client_alive (plugin, preview->client)
        && !lsp_doc_in_window (plugin, preview->client, preview->uri))
        lsp_client_doc_close (preview->client, preview->uri);
    if (preview->hover != NULL)
        json_node_unref (preview->hover);
    g_free (preview->uri);
    g_free (preview);
}

/* --------------------------------------------------------------------------------------------- */

/* What the server says of the name at the cursor of @ask, @hover, after the problems of its line;
   the code it prints replaced by @source, that of the definition, when it is longer */
static void
lsp_show_hover (lsp_plugin_t *plugin, lsp_window_t *w, lsp_ask_t *ask, JsonNode *hover_node,
                const char *source)
{
    GString *out = g_string_new (NULL);
    JsonArray *diagnostics = NULL;
    guint line = 0, character = 0, i;
    gsize len;
    const char *text = g_bytes_get_data (ask->text, &len);
    WEdit *edit = w->edit;
    const WRect *r = &CONST_WIDGET (edit)->rect;
    const int frame = EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1;
    char *shown, *read_key;
    int x, y;

    // of a window no more in front: of no use, and in the way of another
    if (plugin->host->window_current (plugin->host) != (void *) edit)
    {
        g_string_free (out, TRUE);
        return;
    }
    // the problems of the line first
    (void) lsp_text_position (text, len, (gsize) ask->offset, ask->encoding, &line, &character);
    if (w->server != NULL)
        diagnostics = g_hash_table_lookup (w->server->diagnostics, w->uri);
    for (i = 0; diagnostics != NULL && i < json_array_get_length (diagnostics); i++)
    {
        JsonNode *node = json_array_get_element (diagnostics, i);
        static const char *const kinds[] = { N_ ("error"), N_ ("warning"), N_ ("note") };
        JsonObject *d;
        gint64 l, c;

        if (!JSON_NODE_HOLDS_OBJECT (node))
            continue;
        d = json_node_get_object (node);
        if (lsp_range_start (lsp_member_object (d, "range"), &l, &c) && l == (gint64) line
            && lsp_member_string (d, "message") != NULL)
            g_string_append_printf (out, "%s%s: %s", out->len > 0 ? "\n" : "",
                                    _ (kinds[lsp_mark_of (d)]), lsp_member_string (d, "message"));
    }
    // a long text scrolls in the box; an endless one is cut
    shown = lsp_shown (out->str, 500);
    g_string_assign (out, shown);
    g_free (shown);
    if (hover_node != NULL && JSON_NODE_HOLDS_OBJECT (hover_node))
    {
        GString *hover = g_string_new (NULL);
        gboolean markdown = FALSE;

        lsp_hover_text (lsp_member (json_node_get_object (hover_node), "contents"), hover,
                        &markdown);
        if (markdown && source != NULL)
        {
            char *with = lsp_markdown_with_code (hover->str, source, LSP_HOVER_SOURCE_LINES);

            g_string_assign (hover, with);
            g_free (with);
        }
        // its controls gone first: the note has its marks in them
        shown = lsp_shown (hover->str, 500);
        if (markdown)
        {
            char *note = lsp_markdown_note (shown);

            g_free (shown);
            shown = note;
        }
        if (*shown != '\0')
        {
            // a rule between the problems and what the server says of the name
            if (out->len > 0)
                g_string_append_printf (out, "\n%c\n", LSP_NOTE_RULE);
            g_string_append (out, shown);
        }
        g_free (shown);
        g_string_free (hover, TRUE);
    }
    if (out->len == 0)
        g_string_assign (out, _ ("The language server says nothing of it."));
    shown = g_strdup (out->str);
    // under the cursor, from its column
    edit_update_curs_col (edit);
    x = r->x + edit->curs_col + edit->start_col + EDIT_TEXT_HORIZONTAL_OFFSET + frame
        + edit_options.line_state_width;
    y = r->y + edit->curs_row + EDIT_TEXT_VERTICAL_OFFSET + frame;
    // the key of Quick documentation, which has it read when it is long
    read_key = lsp_menu_shortcut (LSP_CMD_HOVER);
    lsp_note_show (plugin->note, WIDGET (edit)->owner, _ ("Quick documentation"), read_key, shown,
                   y, x);
    plugin->note_window = w->id;
    plugin->note_offset = edit->buffer.curs1;
    g_free (read_key);
    g_free (shown);
    g_string_free (out, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/* The help of a call */

/* The documentation of a signature or a parameter, a string or MarkupContent: its text, NULL
   when there is none, and whether it is Markdown */
static const char *
lsp_doc_value (JsonNode *doc, gboolean *markdown)
{
    const char *value = NULL;

    *markdown = FALSE;
    if (doc != NULL && JSON_NODE_HOLDS_VALUE (doc)
        && json_node_get_value_type (doc) == G_TYPE_STRING)
        value = json_node_get_string (doc);
    else if (doc != NULL && JSON_NODE_HOLDS_OBJECT (doc))
    {
        value = lsp_member_string (json_node_get_object (doc), "value");
        *markdown =
            g_strcmp0 (lsp_member_string (json_node_get_object (doc), "kind"), "markdown") == 0;
    }
    return value != NULL && *value != '\0' ? value : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* A text of documentation, Markdown or not, as the text of a note; NULL when it is empty */
static char *
lsp_doc_note (const char *value, gboolean markdown)
{
    char *shown, *note;

    if (value == NULL)
        return NULL;
    // its controls gone first: the note has its marks in them
    shown = lsp_shown (value, 50);
    if (!markdown)
        return shown;
    note = lsp_markdown_note (shown);
    g_free (shown);
    if (*note == '\0')
    {
        g_free (note);
        return NULL;
    }
    return note;
}

/* --------------------------------------------------------------------------------------------- */

static JsonArray *
lsp_signatures (JsonNode *help)
{
    JsonNode *node = help != NULL && JSON_NODE_HOLDS_OBJECT (help)
        ? lsp_member (json_node_get_object (help), "signatures")
        : NULL;

    return node != NULL && JSON_NODE_HOLDS_ARRAY (node)
            && json_array_get_length (json_node_get_array (node)) > 0
        ? json_node_get_array (node)
        : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The name of a parameter in its label, "const char *file": the last word of it; NULL when it
   has none, as "..." */
static char *
lsp_parameter_name (const char *label, gsize len)
{
    gsize end = len, start;

    // after it, the brackets of an array
    while (end > 0 && !(g_ascii_isalnum (label[end - 1]) || label[end - 1] == '_'))
        end--;
    for (start = end; start > 0 && (g_ascii_isalnum (label[start - 1]) || label[start - 1] == '_');
         start--)
        ;
    return end > start ? g_strndup (label + start, end - start) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The bytes of the parameter @param in @label: its label, a string in it or the offsets of it */
static gboolean
lsp_parameter_place (const char *label, JsonObject *param, lsp_encoding_t encoding, gsize *from,
                     gsize *to)
{
    JsonNode *node = lsp_member (param, "label");
    const gsize len = strlen (label);

    if (node == NULL)
        return FALSE;
    if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
    {
        const char *name = json_node_get_string (node);
        const char *at = *name != '\0' ? strstr (label, name) : NULL;

        if (at == NULL)
            return FALSE;
        *from = (gsize) (at - label);
        *to = *from + strlen (name);
        return TRUE;
    }
    if (JSON_NODE_HOLDS_ARRAY (node) && json_array_get_length (json_node_get_array (node)) == 2)
    {
        JsonArray *offsets = json_node_get_array (node);

        // offsets in the units of the positions, in a label of one line
        return lsp_text_offset (label, len, 0, json_array_get_int_element (offsets, 0), encoding,
                                from)
            && lsp_text_offset (label, len, 0, json_array_get_int_element (offsets, 1), encoding,
                                to)
            && *from <= *to;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_signature_end (lsp_plugin_t *plugin)
{
    if (lsp_note_there (plugin->signature))
        lsp_note_hide (plugin->signature);
    // a request out is answered for no help
    plugin->sig_id = 0;
    plugin->sig_window = 0;
    plugin->sig_told = FALSE;
    if (plugin->sig_help != NULL)
        json_node_unref (plugin->sig_help);
    plugin->sig_help = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The help of the last answer, over the line of the cursor: the signature, its parameter of the
   moment hot, and what the server says of them */
static void
lsp_signature_show (lsp_plugin_t *plugin, lsp_window_t *w)
{
    JsonArray *signatures = lsp_signatures (plugin->sig_help);
    JsonObject *help, *sig;
    JsonArray *params = NULL;
    JsonNode *node;
    const char *label;
    gint64 active;
    GString *out;
    char *title = NULL, *doc = NULL;
    const char *value;
    gboolean markdown;
    WEdit *edit = w->edit;
    const WRect *r = &CONST_WIDGET (edit)->rect;
    const int frame = EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1;
    int x, y;

    if (signatures == NULL)
        return;
    help = json_node_get_object (plugin->sig_help);
    node = json_array_get_element (signatures, (guint) plugin->sig_active);
    if (!JSON_NODE_HOLDS_OBJECT (node))
        return;
    sig = json_node_get_object (node);
    label = lsp_member_string (sig, "label");
    if (label == NULL)
        return;
    node = lsp_member (sig, "parameters");
    if (node != NULL && JSON_NODE_HOLDS_ARRAY (node))
        params = json_node_get_array (node);
    // that of the signature first, of the help else
    active = lsp_member (sig, "activeParameter") != NULL
        ? lsp_member_int (sig, "activeParameter", 0)
        : lsp_member_int (help, "activeParameter", 0);

    out = g_string_new (NULL);
    {
        JsonObject *param = NULL;
        gsize from = 0, to = 0;
        char *part;

        if (params != NULL && active >= 0 && active < (gint64) json_array_get_length (params)
            && JSON_NODE_HOLDS_OBJECT (json_array_get_element (params, (guint) active)))
            param = json_array_get_object_element (params, (guint) active);
        if (param != NULL && lsp_parameter_place (label, param, plugin->sig_encoding, &from, &to))
        {
            char *before = g_strndup (label, from);
            char *name = g_strndup (label + from, to - from);

            part = lsp_shown (before, 1);
            g_string_append (out, part);
            g_free (part);
            part = lsp_shown (name, 1);
            g_string_append_printf (out, "%c%s%c", LSP_NOTE_HOT_ON, part, LSP_NOTE_HOT_OFF);
            g_free (part);
            part = lsp_shown (label + to, 1);
            g_string_append (out, part);
            g_free (part);
            g_free (before);
            g_free (name);
        }
        else
        {
            part = lsp_shown (label, 1);
            g_string_append (out, part);
            g_free (part);
        }
        /* what the parameter is, as the server says it, or as the comment of the function says
           it, "@param"; else what the function does, in a word, not all of its comment */
        markdown = FALSE;
        value = lsp_doc_value (lsp_member (sig, "documentation"), &markdown);
        if (param != NULL)
        {
            gboolean param_markdown;
            const char *own = lsp_doc_value (lsp_member (param, "documentation"), &param_markdown);
            char *name = to > from ? lsp_parameter_name (label + from, to - from) : NULL;

            if (own != NULL)
                doc = lsp_doc_note (own, param_markdown);
            else
            {
                char *said = lsp_markdown_param (value, name);

                doc = lsp_doc_note (said, markdown);
                g_free (said);
            }
            if (doc != NULL && name != NULL)
            {
                // the name, hot as in the signature
                part = g_strdup_printf ("%c%s%c %s", LSP_NOTE_HOT_ON, name, LSP_NOTE_HOT_OFF, doc);
                g_free (doc);
                doc = part;
            }
            g_free (name);
        }
        if (doc == NULL)
        {
            char *brief = lsp_markdown_brief (value);

            doc = lsp_doc_note (brief, markdown);
            g_free (brief);
        }
        if (doc != NULL)
            g_string_append_printf (out, "\n%c\n%s", LSP_NOTE_RULE, doc);
        g_free (doc);
    }

    if (json_array_get_length (signatures) > 1)
        title =
            g_strdup_printf ("%d/%u", plugin->sig_active + 1, json_array_get_length (signatures));
    // over the line of the cursor, from its column: what is typed stays in sight
    edit_update_curs_col (edit);
    x = r->x + edit->curs_col + edit->start_col + EDIT_TEXT_HORIZONTAL_OFFSET + frame
        + edit_options.line_state_width;
    y = r->y + edit->curs_row + EDIT_TEXT_VERTICAL_OFFSET + frame;
    lsp_note_show (plugin->signature, WIDGET (edit)->owner, title, NULL, out->str, y, x);
    g_free (title);
    g_string_free (out, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* The help of the call at the cursor of @edit, asked for at the next tick and as the cursor
   moves; told when the user asks for it and there is none */
static gboolean
lsp_signature_start (lsp_plugin_t *plugin, WEdit *edit, gboolean told)
{
    lsp_window_t *w = lsp_window_served (plugin, edit, told);

    if (w == NULL)
        return FALSE;
    if (!lsp_client_supports (w->server->client, "signatureHelpProvider"))
    {
        if (told)
            plugin->host->message (plugin->host, D_NORMAL, _ ("Language server"),
                                   _ ("The language server cannot do that."));
        return FALSE;
    }
    if (plugin->sig_window != w->id || plugin->sig_id == 0)
    {
        lsp_signature_end (plugin);
        plugin->sig_id = ++plugin->next_signature;
        plugin->sig_window = w->id;
        plugin->sig_active = 0;
    }
    plugin->sig_told = plugin->sig_told || told;
    // asked for again, at once
    plugin->sig_offset = -1;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The signature before or after the one shown, when there are several; FALSE else */
static gboolean
lsp_signature_next (lsp_plugin_t *plugin, int by)
{
    JsonArray *signatures = lsp_signatures (plugin->sig_help);
    lsp_window_t *w = lsp_window_by_id (plugin, plugin->sig_window);
    int count;

    if (signatures == NULL || w == NULL || json_array_get_length (signatures) < 2)
        return FALSE;
    count = (int) json_array_get_length (signatures);
    plugin->sig_active = (plugin->sig_active + by + count) % count;
    lsp_signature_show (plugin, w);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The answer of the server: the help shown, or ended when the cursor is in no call */
static void
lsp_signature_answer (lsp_plugin_t *plugin, lsp_window_t *w, lsp_ask_t *ask)
{
    const gboolean told = plugin->sig_told;
    JsonArray *signatures;

    plugin->sig_asking = FALSE;
    // the answer of a help ended since
    if (plugin->sig_id == 0 || ask->signature != plugin->sig_id)
        return;
    if (w == NULL)
    {
        lsp_signature_end (plugin);
        return;
    }
    plugin->sig_told = FALSE;
    signatures = ask->status == LSP_REQUEST_OK ? lsp_signatures (ask->result) : NULL;
    if (signatures == NULL)
    {
        if (told && ask->status == LSP_REQUEST_ERROR)
            plugin->host->message (plugin->host, D_ERROR, _ ("Language server"), ask->error);
        else if (told)
            plugin->host->message (plugin->host, D_NORMAL, _ ("Language server"),
                                   _ ("The language server sees no call here."));
        lsp_signature_end (plugin);
        return;
    }
    // the signature the server says, the first time; the one the user went to, after
    if (plugin->sig_help == NULL)
        plugin->sig_active =
            (int) lsp_member_int (json_node_get_object (ask->result), "activeSignature", 0);
    else
        json_node_unref (plugin->sig_help);
    plugin->sig_active =
        CLAMP (plugin->sig_active, 0, (int) json_array_get_length (signatures) - 1);
    plugin->sig_help = json_node_ref (ask->result);
    plugin->sig_encoding = ask->encoding;
    // the list of a completion is in front: the help waits for it to go
    if (!lsp_popup_shown (plugin->popup)
        && plugin->host->window_current (plugin->host) == (void *) w->edit)
        lsp_signature_show (plugin, w);
}

/* --------------------------------------------------------------------------------------------- */

/* At a tick: the help asked for again once the cursor or the text has moved, out of the way of
   the list of a completion, ended with its window out of the front */
static void
lsp_signature_tick (lsp_plugin_t *plugin)
{
    lsp_window_t *w;

    if (plugin->sig_id == 0)
        return;
    w = lsp_window_by_id (plugin, plugin->sig_window);
    if (w == NULL || plugin->host->window_current (plugin->host) != (void *) w->edit)
    {
        // out of a key: no one draws the screen after it
        lsp_signature_end (plugin);
        tty_refresh ();
        return;
    }
    if (lsp_popup_shown (plugin->popup))
    {
        if (lsp_note_there (plugin->signature))
        {
            lsp_note_hide (plugin->signature);
            tty_refresh ();
        }
        return;
    }
    if (plugin->sig_asking)
        return;
    if (w->edit->buffer.curs1 != plugin->sig_offset
        || plugin->host->get_revision (plugin->host, w->edit) != plugin->sig_revision)
    {
        plugin->sig_offset = w->edit->buffer.curs1;
        plugin->sig_revision = plugin->host->get_revision (plugin->host, w->edit);
        plugin->sig_asking =
            lsp_ask (plugin, w->edit, LSP_ASK_SIGNATURE, plugin->sig_told ? NULL : "(");
        if (!plugin->sig_asking)
        {
            lsp_signature_end (plugin);
            tty_refresh ();
        }
    }
    else if (!lsp_note_there (plugin->signature) && plugin->sig_help != NULL)
        lsp_signature_show (plugin, w);
}

/* --------------------------------------------------------------------------------------------- */
/* Places in files */

typedef struct
{
    char *path;
    gint64 line;
    gint64 character;
    lsp_encoding_t encoding;  // of the server that said it
} lsp_place_t;

static void
lsp_place_free (gpointer data)
{
    lsp_place_t *place = (lsp_place_t *) data;

    g_free (place->path);
    g_free (place);
}

/* --------------------------------------------------------------------------------------------- */

/* A Location or a LocationLink added to @places */
static void
lsp_place_add (GPtrArray *places, JsonNode *node, lsp_encoding_t encoding)
{
    JsonObject *o;
    const char *uri;
    JsonObject *range;
    lsp_place_t *place;
    char *path;
    gint64 line, character;

    if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
        return;
    o = json_node_get_object (node);
    uri = lsp_member_string (o, "uri");
    range = lsp_member_object (o, "range");
    if (uri == NULL)
    {
        uri = lsp_member_string (o, "targetUri");
        range = lsp_member_object (o, "targetSelectionRange");
    }
    path = lsp_path_from_uri (uri);
    if (path == NULL || !lsp_range_start (range, &line, &character))
    {
        g_free (path);
        return;
    }
    place = g_new0 (lsp_place_t, 1);
    place->path = path;
    place->line = line;
    place->character = character;
    place->encoding = encoding;
    g_ptr_array_add (places, place);
}

/* --------------------------------------------------------------------------------------------- */

/* The byte in its line of a character of a line of a file: the text of a window of it, else the
   file */
static long
lsp_place_column (lsp_plugin_t *plugin, const lsp_place_t *place)
{
    char *text = NULL;
    gsize len = 0, line_start = 0, at = 0;
    guint i;

    for (i = 0; i < plugin->windows->len && text == NULL; i++)
    {
        const lsp_window_t *w = g_ptr_array_index (plugin->windows, i);
        char *canonical = lsp_canonical_path (w->path);

        if (g_strcmp0 (canonical, place->path) == 0)
            text = plugin->host->get_text (plugin->host, w->edit, &len);
        g_free (canonical);
    }
    if (text == NULL && !g_file_get_contents (place->path, &text, &len, NULL))
        return 0;
    if (!lsp_text_offset (text, len, place->line, 0, place->encoding, &line_start)
        || !lsp_text_offset (text, len, place->line, place->character, place->encoding, &at))
        at = line_start;
    g_free (text);
    return (long) (at - line_start);
}

/* --------------------------------------------------------------------------------------------- */

/* The cursor at @column bytes into its line, the line shown */
static void
lsp_cursor_to_column (WEdit *edit, long column)
{
    const off_t bol = edit_buffer_get_current_bol (&edit->buffer);
    const off_t eol = edit_buffer_get_current_eol (&edit->buffer);
    const off_t to = MIN (bol + column, eol);

    edit_cursor_move (edit, to - edit->buffer.curs1);
    edit->search_start = edit->buffer.curs1;
    edit->prev_col = edit_get_col (edit);
    edit->force |= REDRAW_PAGE;
    edit_scroll_screen_over_cursor (edit);
}

/* --------------------------------------------------------------------------------------------- */

/* The path of a window of the file @path, when one has it open */
static char *
lsp_open_path (const lsp_plugin_t *plugin, const char *path)
{
    guint i;

    for (i = 0; i < plugin->windows->len; i++)
    {
        const lsp_window_t *w = g_ptr_array_index (plugin->windows, i);
        char *canonical = lsp_canonical_path (w->path);

        if (canonical != NULL && strcmp (canonical, path) == 0)
        {
            g_free (canonical);
            return g_strdup (w->path);
        }
        g_free (canonical);
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* To a place: a file not open as Go to does, with a step of the navigation history; one open in
   its window, as it is there, its undo kept */
static void
lsp_go (lsp_plugin_t *plugin, WEdit *edit, const lsp_place_t *place)
{
    mc_editor_host_t *host = plugin->host;
    const long column = lsp_place_column (plugin, place);
    char *open;
    gboolean gone;
    void *now;

    // recorded for Go back, the window of the file to the front when one has it
    if (host->go_to != NULL)
    {
        (void) host->go_to (host, edit, place->path, (long) place->line + 1, column);
        return;
    }
    open = lsp_open_path (plugin, place->path);
    if (open != NULL)
        gone = host->show_location (host, open, (long) place->line + 1);
    else
        gone = host->jump_to (host, edit, place->path, (long) place->line + 1);
    g_free (open);
    if (!gone)
        return;
    // the window the jump went to: this one, or a new one for a file changed here
    now = host->window_current (host);
    if (now != NULL && edit_widget_is_editor (CONST_WIDGET (now)))
    {
        char *path = lsp_window_path (plugin, EDIT (now));
        char *canonical = lsp_canonical_path (path);

        if (g_strcmp0 (canonical, place->path) == 0)
            lsp_cursor_to_column (EDIT (now), column);
        g_free (canonical);
        g_free (path);
    }
    host->redraw (host);
}

/* --------------------------------------------------------------------------------------------- */

/* A list to choose from; the index chosen, -1 for none */
static int
lsp_choose (const char *title, GPtrArray *rows, int current)
{
    int width = 20, height;
    Listbox *list;
    guint i;

    for (i = 0; i < rows->len; i++)
        width = MAX (width, str_term_width1 (g_ptr_array_index (rows, i)) + 2);
    height = MIN ((int) rows->len, LINES - 8);
    list = listbox_window_new (MAX (height, 1), MIN (width, COLS - 8), title, NULL);
    list->dlg->help_file = "lsp.md";
    for (i = 0; i < rows->len; i++)
        LISTBOX_APPEND_TEXT (list, 0, g_ptr_array_index (rows, i), NULL, FALSE);
    if (current >= 0 && current < (int) rows->len)
        listbox_set_current (list->list, current);
    return listbox_run (list);
}

/* --------------------------------------------------------------------------------------------- */

/* A path to show: from the root of the server */
static const char *
lsp_short_path (const char *path, const char *root)
{
    const gsize len = root != NULL ? strlen (root) : 0;

    if (len > 0 && strncmp (path, root, len) == 0 && path[len] == G_DIR_SEPARATOR)
        return path + len + 1;
    return path;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_show_definition (lsp_plugin_t *plugin, lsp_window_t *w, lsp_ask_t *ask)
{
    GPtrArray *places = g_ptr_array_new_with_free_func (lsp_place_free);
    int chosen = 0;

    if (ask->result != NULL && JSON_NODE_HOLDS_ARRAY (ask->result))
    {
        JsonArray *array = json_node_get_array (ask->result);
        guint i;

        for (i = 0; i < json_array_get_length (array); i++)
            lsp_place_add (places, json_array_get_element (array, i), ask->encoding);
    }
    else
        lsp_place_add (places, ask->result, ask->encoding);

    if (places->len == 0)
        plugin->host->message (plugin->host, D_NORMAL, _ ("Go to definition"),
                               _ ("The language server knows no definition of it."));
    else
    {
        if (places->len > 1)
        {
            GPtrArray *rows = g_ptr_array_new_with_free_func (g_free);
            guint i;

            for (i = 0; i < places->len; i++)
            {
                const lsp_place_t *place = g_ptr_array_index (places, i);

                g_ptr_array_add (rows,
                                 g_strdup_printf ("%s:%" G_GINT64_FORMAT,
                                                  lsp_short_path (place->path, w->server->root),
                                                  place->line + 1));
            }
            chosen = lsp_choose (_ ("Definitions"), rows, 0);
            g_ptr_array_free (rows, TRUE);
        }
        if (chosen >= 0)
            lsp_go (plugin, w->edit, g_ptr_array_index (places, chosen));
    }
    g_ptr_array_free (places, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* The line @line of a file, its text in a window first, the file else; NULL when there is none */
static char *
lsp_line_of (lsp_plugin_t *plugin, GHashTable *texts, const char *path, gint64 line)
{
    GBytes *text = g_hash_table_lookup (texts, path);
    gsize len, start = 0, end;
    const char *data;
    char *row, *shown;

    if (text == NULL)
    {
        char *content = NULL;
        gsize content_len = 0;
        guint i;

        for (i = 0; i < plugin->windows->len && content == NULL; i++)
        {
            const lsp_window_t *w = g_ptr_array_index (plugin->windows, i);
            char *canonical = lsp_canonical_path (w->path);

            if (g_strcmp0 (canonical, path) == 0)
                content = plugin->host->get_text (plugin->host, w->edit, &content_len);
            g_free (canonical);
        }
        if (content == NULL && !g_file_get_contents (path, &content, &content_len, NULL))
            content = NULL;
        text = content != NULL ? g_bytes_new_take (content, content_len) : g_bytes_new (NULL, 0);
        g_hash_table_insert (texts, g_strdup (path), text);
    }
    data = g_bytes_get_data (text, &len);
    if (data == NULL || !lsp_text_offset (data, len, line, 0, LSP_ENCODING_UTF8, &start))
        return NULL;
    for (end = start; end < len && data[end] != '\n' && data[end] != '\r'; end++)
        ;
    row = g_strndup (data + start, end - start);
    shown = lsp_shown (g_strstrip (row), 1);
    g_free (row);
    return shown;
}

/* --------------------------------------------------------------------------------------------- */

/* The uses of a name: a list of them with their lines, the one chosen gone to */
static void
lsp_show_references (lsp_plugin_t *plugin, lsp_window_t *w, lsp_ask_t *ask)
{
    GPtrArray *places = g_ptr_array_new_with_free_func (lsp_place_free);
    GHashTable *texts =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_bytes_unref);
    char *here = lsp_canonical_path (w->path);
    guint here_line = 0, here_character = 0;
    GPtrArray *rows;
    int chosen = 0, current = 0;
    gsize len;
    const char *text = g_bytes_get_data (ask->text, &len);
    guint i;

    (void) lsp_text_position (text, len, (gsize) ask->offset, ask->encoding, &here_line,
                              &here_character);
    if (ask->result != NULL && JSON_NODE_HOLDS_ARRAY (ask->result))
    {
        JsonArray *array = json_node_get_array (ask->result);

        for (i = 0; i < json_array_get_length (array); i++)
            lsp_place_add (places, json_array_get_element (array, i), ask->encoding);
    }
    if (places->len == 0)
    {
        plugin->host->message (plugin->host, D_NORMAL, _ ("Find references"),
                               _ ("The language server knows no use of it."));
        g_ptr_array_free (places, TRUE);
        g_hash_table_destroy (texts);
        g_free (here);
        return;
    }
    rows = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < places->len; i++)
    {
        const lsp_place_t *place = g_ptr_array_index (places, i);
        char *line = lsp_line_of (plugin, texts, place->path, place->line);

        g_ptr_array_add (rows,
                         g_strdup_printf ("%s:%" G_GINT64_FORMAT ": %s",
                                          lsp_short_path (place->path, w->server->root),
                                          place->line + 1, line != NULL ? line : ""));
        g_free (line);
        // the list opens at the use the cursor is on
        if (g_strcmp0 (place->path, here) == 0 && place->line == (gint64) here_line)
            current = (int) i;
    }
    if (places->len > 1)
    {
        char *title = g_strdup_printf (_ ("References: %u"), places->len);

        chosen = lsp_choose (title, rows, current);
        g_free (title);
    }
    if (chosen >= 0)
        lsp_go (plugin, w->edit, g_ptr_array_index (places, chosen));
    g_ptr_array_free (rows, TRUE);
    g_ptr_array_free (places, TRUE);
    g_hash_table_destroy (texts);
    g_free (here);
}

/* --------------------------------------------------------------------------------------------- */
/* Completion: a list under the word typed, which follows it as it is typed */

/* Whether a byte is of a word: letters, digits, '_' and what is not ASCII */
static gboolean
lsp_word_byte (char c)
{
    return g_ascii_isalnum (c) || c == '_' || (unsigned char) c >= 0x80;
}

/* --------------------------------------------------------------------------------------------- */

/* The text of a snippet as plain text: "$1", "${1:x}" and "${1|a,b|}" are what they hold */
static char *
lsp_snippet_text (const char *snippet)
{
    GString *out = g_string_new (NULL);
    const char *p = snippet;

    while (*p != '\0')
    {
        if (*p == '\\' && p[1] != '\0')
        {
            g_string_append_c (out, p[1]);
            p += 2;
        }
        else if (*p == '$' && g_ascii_isdigit (p[1]))
            for (p++; g_ascii_isdigit (*p); p++)
                ;
        else if (*p == '$' && p[1] == '{')
        {
            p += 2;
            while (g_ascii_isalnum (*p) || *p == '_')
                p++;
            if (*p == ':')
                p++;
            else if (*p == '|')
            {
                // the first choice
                for (p++; *p != '\0' && *p != ',' && *p != '|'; p++)
                    g_string_append_c (out, *p);
                while (*p != '\0' && *p != '}')
                    p++;
            }
            // what it holds is copied on; its '}' is dropped below
        }
        else if (*p == '}')
            p++;
        else
            g_string_append_c (out, *p++);
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    gsize from;
    gsize to;
    char *text;
} lsp_edit_t;

static void
lsp_edit_free (gpointer data)
{
    lsp_edit_t *e = (lsp_edit_t *) data;

    g_free (e->text);
    g_free (e);
}

static int
lsp_edit_cmp (gconstpointer a, gconstpointer b)
{
    const lsp_edit_t *x = *(const lsp_edit_t *const *) a;
    const lsp_edit_t *y = *(const lsp_edit_t *const *) b;

    // from the end; of two at one place, the one that takes more first
    if (x->from != y->from)
        return x->from < y->from ? 1 : -1;
    return x->to < y->to ? 1 : x->to > y->to ? -1 : 0;
}

/* --------------------------------------------------------------------------------------------- */

/* What is typed in the word since the items were asked for moves what comes after it: a place
   of the text of the answer in the text now; FALSE for one inside the word, which is no more */
static gboolean
lsp_session_map (const lsp_session_t *s, off_t now_at, gsize at, gsize *out)
{
    if ((off_t) at <= s->start)
        *out = at;
    else if ((off_t) at >= s->asked_at)
        *out = (gsize) ((off_t) at + now_at - s->asked_at);
    else
        return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A TextEdit of the answer added to @edits, at its place in the text now; FALSE when it is no
   edit of the text */
static gboolean
lsp_edit_add (GPtrArray *edits, JsonObject *edit, const lsp_session_t *s, off_t now_at)
{
    JsonObject *range = lsp_member_object (edit, "range");
    const char *new_text = lsp_member_string (edit, "newText");
    gsize len, from, to;
    const char *text = g_bytes_get_data (s->text, &len);
    lsp_edit_t *e;

    // an InsertReplaceEdit, which the client does not ask for: its insert range
    if (range == NULL)
        range = lsp_member_object (edit, "insert");
    if (new_text == NULL || !lsp_range_offsets (range, text, len, s->encoding, &from, &to)
        || !lsp_session_map (s, now_at, from, &from) || !lsp_session_map (s, now_at, to, &to))
        return FALSE;
    e = g_new0 (lsp_edit_t, 1);
    e->from = from;
    e->to = to;
    e->text = g_strdup (new_text);
    g_ptr_array_add (edits, e);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A completion put in the text: the word from its start to @end, and the additional edits, all
   or none, as one step of undo */
static void
lsp_complete (lsp_plugin_t *plugin, WEdit *edit, const lsp_session_t *s, JsonObject *item,
              off_t end)
{
    GPtrArray *edits = g_ptr_array_new_with_free_func (lsp_edit_free);
    JsonObject *text_edit = lsp_member_object (item, "textEdit");
    JsonNode *additional = lsp_member (item, "additionalTextEdits");
    const gboolean snippet = lsp_member_int (item, "insertTextFormat", 1) == 2;
    const char *insert = lsp_member_string (item, "insertText");
    lsp_edit_t *main_edit = g_new0 (lsp_edit_t, 1);
    // what is typed after the word, the '-' of "->", stays after the cursor
    const off_t tail = edit->buffer.curs1 - end;
    gboolean ok = TRUE;
    off_t cursor;
    guint i;

    // the word typed, all of it, whatever the server thought it was when asked
    main_edit->from = (gsize) s->start;
    main_edit->to = (gsize) end;
    if (text_edit != NULL)
    {
        GPtrArray *one = g_ptr_array_new_with_free_func (lsp_edit_free);

        if (lsp_edit_add (one, text_edit, s, end))
        {
            const lsp_edit_t *e = g_ptr_array_index (one, 0);

            main_edit->from = MIN (e->from, main_edit->from);
            main_edit->to = MAX (e->to, main_edit->to);
            main_edit->text = g_strdup (e->text);
        }
        g_ptr_array_free (one, TRUE);
    }
    if (main_edit->text == NULL)
        main_edit->text = g_strdup (insert != NULL ? insert : lsp_member_string (item, "label"));
    if (snippet)
    {
        char *plain = lsp_snippet_text (main_edit->text);

        g_free (main_edit->text);
        main_edit->text = plain;
    }
    g_ptr_array_add (edits, main_edit);
    if (additional != NULL && JSON_NODE_HOLDS_ARRAY (additional))
        for (i = 0; ok && i < json_array_get_length (json_node_get_array (additional)); i++)
        {
            JsonNode *node = json_array_get_element (json_node_get_array (additional), i);

            ok = JSON_NODE_HOLDS_OBJECT (node)
                && lsp_edit_add (edits, json_node_get_object (node), s, edit->buffer.curs1);
        }
    // from the end, none over another, none two at one place
    g_ptr_array_sort (edits, lsp_edit_cmp);
    for (i = 1; ok && i < edits->len; i++)
    {
        const lsp_edit_t *later = g_ptr_array_index (edits, i - 1);
        const lsp_edit_t *earlier = g_ptr_array_index (edits, i);

        ok = earlier->to < later->from || (earlier->to == later->from && earlier->from < later->to);
    }
    if (!ok || !edit_runtime_begin (edit))
    {
        plugin->host->message (plugin->host, D_ERROR, _ ("Completion"),
                               _ ("The completion does not fit the text."));
        g_ptr_array_free (edits, TRUE);
        return;
    }
    cursor = (off_t) (main_edit->from + strlen (main_edit->text)) + tail;
    for (i = 0; i < edits->len; i++)
    {
        const lsp_edit_t *e = g_ptr_array_index (edits, i);
        const char *p;
        gsize n;

        edit_cursor_move (edit, (off_t) e->from - edit->buffer.curs1);
        for (n = e->from; n < e->to; n++)
            (void) edit_delete (edit, TRUE);
        for (p = e->text; *p != '\0'; p++)
            edit_insert (edit, (unsigned char) *p);
        // what is put before the completion moves it
        if (e != main_edit && e->to <= main_edit->from)
            cursor += (off_t) strlen (e->text) - (off_t) (e->to - e->from);
    }
    edit_cursor_move (edit, cursor - edit->buffer.curs1);
    edit_runtime_end (edit);
    edit->force |= REDRAW_PAGE;
    edit_scroll_screen_over_cursor (edit);
    plugin->host->redraw (plugin->host);
    g_ptr_array_free (edits, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_session_free (lsp_session_t *s)
{
    if (s == NULL)
        return;
    g_free (s->trigger);
    g_free (s->asked);
    if (s->items != NULL)
        json_array_unref (s->items);
    if (s->text != NULL)
        g_bytes_unref (s->text);
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

/* The completion of the moment ended, its list gone */
static void
lsp_session_end (lsp_plugin_t *plugin)
{
    if (plugin->popup != NULL)
        lsp_popup_hide (plugin->popup);
    if (plugin->completing != NULL && plugin->completion != 0)
        lsp_client_cancel (plugin->completing, plugin->completion);
    lsp_session_free (plugin->session);
    plugin->session = NULL;
    plugin->refilter = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The letter of a CompletionItemKind */
static char
lsp_kind_letter (gint64 kind)
{
    // text, method, function, constructor, field, variable, class, interface, module, property,
    // unit, value, enum, keyword, snippet, color, file, reference, folder, enum member,
    // constant, struct, event, operator, type parameter
    static const char letters[] = " tmFcfvCIMpu=Eksxdrde#SeoT";

    return kind >= 1 && kind < (gint64) sizeof (letters) - 1 ? letters[kind] : ' ';
}

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    JsonObject *item;
    int score;
    const char *sort;
    guint index;  // in the order of the server
} lsp_choice_t;

static int
lsp_choice_cmp (gconstpointer a, gconstpointer b)
{
    const lsp_choice_t *x = *(const lsp_choice_t *const *) a;
    const lsp_choice_t *y = *(const lsp_choice_t *const *) b;
    int c;

    if (x->score != y->score)
        return x->score > y->score ? -1 : 1;
    c = strcmp (x->sort, y->sort);
    if (c != 0)
        return c;
    return x->index < y->index ? -1 : x->index > y->index ? 1 : 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The word before the cursor: where it starts, and its text */
static off_t
lsp_word_before (WEdit *edit, char **word)
{
    off_t start = edit->buffer.curs1;
    GString *out = g_string_new (NULL);
    off_t i;

    while (start > 0 && lsp_word_byte ((char) edit_buffer_get_byte (&edit->buffer, start - 1)))
        start--;
    for (i = start; i < edit->buffer.curs1; i++)
        g_string_append_c (out, (char) edit_buffer_get_byte (&edit->buffer, i));
    *word = g_string_free (out, FALSE);
    return start;
}

/* --------------------------------------------------------------------------------------------- */

static void lsp_session_take_cb (void *data);

/* The items that fit the word typed, best first, in the list under it */
static void
lsp_session_show (lsp_plugin_t *plugin, lsp_window_t *w, const char *word)
{
    lsp_session_t *s = plugin->session;
    GPtrArray *choices = g_ptr_array_new_with_free_func (g_free);
    GPtrArray *items;
    WEdit *edit = w->edit;
    const WRect *r = &CONST_WIDGET (edit)->rect;
    const int frame = EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1;
    guint i;
    int x, y;

    for (i = 0; i < json_array_get_length (s->items); i++)
    {
        JsonNode *node = json_array_get_element (s->items, i);
        JsonObject *item;
        const char *label, *filter;
        lsp_choice_t *choice;
        int score;

        if (!JSON_NODE_HOLDS_OBJECT (node))
            continue;
        item = json_node_get_object (node);
        label = lsp_member_string (item, "label");
        if (label == NULL)
            continue;
        filter = lsp_member_string (item, "filterText");
        if (filter == NULL)
            filter = label;
        score = *word == '\0' ? 1 : fuzzy_score (filter, word, NULL);
        if (score == 0)
            continue;
        choice = g_new0 (lsp_choice_t, 1);
        choice->item = item;
        choice->score = score;
        choice->sort = lsp_member_string (item, "sortText");
        if (choice->sort == NULL)
            choice->sort = label;
        choice->index = i;
        g_ptr_array_add (choices, choice);
    }
    if (choices->len == 0)
    {
        g_ptr_array_free (choices, TRUE);
        lsp_popup_hide (plugin->popup);
        return;
    }
    g_ptr_array_sort (choices, lsp_choice_cmp);

    items = g_ptr_array_new ();
    for (i = 0; i < choices->len && i < 500; i++)
    {
        const lsp_choice_t *choice = g_ptr_array_index (choices, i);
        const char *label = lsp_member_string (choice->item, "label");
        const char *detail = lsp_member_string (choice->item, "detail");
        GArray *hits = NULL;
        char *shown, *detail_shown = NULL;

        // clangd puts a space before most of its labels, a mark before those it would include
        while (*label == ' ')
            label++;
        if (*word != '\0')
        {
            hits = g_array_new (FALSE, FALSE, sizeof (int));
            (void) fuzzy_score (label, word, hits);
        }
        shown = lsp_shown (label, 1);
        if (detail != NULL)
        {
            while (*detail == ' ')
                detail++;
            detail_shown = lsp_shown (detail, 1);
        }
        g_ptr_array_add (
            items,
            lsp_popup_item_new (lsp_kind_letter (lsp_member_int (choice->item, "kind", 0)), shown,
                                detail_shown, hits, choice->item));
        g_free (shown);
        g_free (detail_shown);
    }
    g_ptr_array_free (choices, TRUE);
    lsp_popup_set_items (plugin->popup, items);

    // under the start of the word
    edit_update_curs_col (edit);
    x = r->x + edit->curs_col + edit->start_col + EDIT_TEXT_HORIZONTAL_OFFSET + frame
        + edit_options.line_state_width - str_term_width1 (word);
    y = r->y + edit->curs_row + EDIT_TEXT_VERTICAL_OFFSET + frame;
    lsp_popup_show (plugin->popup, WIDGET (edit)->owner, y, x);
}

/* --------------------------------------------------------------------------------------------- */

/* The items asked for, for the word before the cursor */
static void
lsp_session_ask (lsp_plugin_t *plugin, WEdit *edit, const char *word)
{
    lsp_session_t *s = plugin->session;

    g_free (s->asked);
    s->asked = g_strdup (word);
    s->asking = lsp_ask (plugin, edit, LSP_ASK_COMPLETION, s->trigger);
}

/* --------------------------------------------------------------------------------------------- */

/* The list as the word before the cursor is now: a new completion when it is another word, the
   items asked for again when the server had more, the list sifted else */
static void
lsp_session_update (lsp_plugin_t *plugin, WEdit *edit, gboolean asked_for, const char *trigger)
{
    lsp_window_t *w = lsp_window_served (plugin, edit, FALSE);
    lsp_session_t *s = plugin->session;
    char *word = NULL;
    off_t start;

    if (w == NULL)
    {
        lsp_session_end (plugin);
        return;
    }
    start = lsp_word_before (edit, &word);
    if (s != NULL && (s->window_id != w->id || s->start != start || asked_for))
    {
        lsp_session_end (plugin);
        s = NULL;
    }
    if (s == NULL)
    {
        /* nothing typed yet, nor asked for: nothing to complete; nor with a server that does
           not complete, unless the user asks it and is told so */
        if ((*word == '\0' && !asked_for && trigger == NULL)
            || (!asked_for && !lsp_client_supports (w->server->client, "completionProvider")))
        {
            g_free (word);
            return;
        }
        s = g_new0 (lsp_session_t, 1);
        s->id = ++plugin->next_session;
        s->window_id = w->id;
        s->start = start;
        s->any = asked_for || trigger != NULL;
        s->trigger = g_strdup (trigger);
        plugin->session = s;
        lsp_session_ask (plugin, edit, word);
    }
    else if (*word == '\0' && !s->any)
        lsp_session_end (plugin);
    else if (s->items != NULL)
    {
        if (s->incomplete && !s->asking && g_strcmp0 (s->asked, word) != 0)
            lsp_session_ask (plugin, edit, word);
        lsp_session_show (plugin, w, word);
    }
    g_free (word);
}

/* --------------------------------------------------------------------------------------------- */

/* The answer of the server: the items of the session */
static void
lsp_session_answer (lsp_plugin_t *plugin, lsp_window_t *w, lsp_ask_t *ask)
{
    lsp_session_t *s = plugin->session;
    JsonArray *items = NULL;

    if (s == NULL || s->id != ask->session || w == NULL)
        return;
    s->asking = FALSE;
    if (ask->status != LSP_REQUEST_OK)
    {
        lsp_session_end (plugin);
        return;
    }
    s->incomplete = FALSE;
    if (ask->result != NULL && JSON_NODE_HOLDS_ARRAY (ask->result))
        items = json_node_get_array (ask->result);
    else if (ask->result != NULL && JSON_NODE_HOLDS_OBJECT (ask->result))
    {
        JsonObject *list = json_node_get_object (ask->result);
        JsonNode *node = lsp_member (list, "items");

        if (node != NULL && JSON_NODE_HOLDS_ARRAY (node))
            items = json_node_get_array (node);
        s->incomplete = lsp_member (list, "isIncomplete") != NULL
            && JSON_NODE_HOLDS_VALUE (lsp_member (list, "isIncomplete"))
            && json_node_get_boolean (lsp_member (list, "isIncomplete"));
    }
    if (s->items != NULL)
        json_array_unref (s->items);
    s->items = items != NULL ? json_array_ref (items) : json_array_new ();
    if (s->text != NULL)
        g_bytes_unref (s->text);
    s->text = g_bytes_ref (ask->text);
    s->asked_at = ask->offset;
    s->encoding = ask->encoding;
    lsp_session_update (plugin, w->edit, FALSE, NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the text from the start of the word of @s to @end is still a word: the cursor may have
   been put elsewhere in the line, by the mouse, the editor saying nothing of it */
static gboolean
lsp_session_in_word (const lsp_session_t *s, WEdit *edit, off_t end)
{
    off_t at;

    if (end < s->start || end > edit->buffer.size)
        return FALSE;
    for (at = s->start; at < end; at++)
        if (!lsp_word_byte ((char) edit_buffer_get_byte (&edit->buffer, at)))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The item chosen put in the text, the word up to @end replaced; nothing put when the cursor is
   no more in the word */
static void
lsp_session_take (lsp_plugin_t *plugin, WEdit *edit, off_t end)
{
    lsp_popup_item_t *item = plugin->popup != NULL ? lsp_popup_current (plugin->popup) : NULL;
    lsp_session_t *s = plugin->session;

    if (item != NULL && s != NULL && s->text != NULL && lsp_session_in_word (s, edit, end))
    {
        plugin->session = NULL;
        lsp_popup_hide (plugin->popup);
        lsp_complete (plugin, edit, s, (JsonObject *) item->data, end);
        lsp_session_free (s);
    }
    lsp_session_end (plugin);
}

/* --------------------------------------------------------------------------------------------- */

/* A double click on an item */
static void
lsp_session_take_cb (void *data)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;
    void *current = plugin->host->window_current (plugin->host);

    if (current != NULL && edit_widget_is_editor (CONST_WIDGET (current)))
        lsp_session_take (plugin, EDIT (current), EDIT (current)->buffer.curs1);
}

/* --------------------------------------------------------------------------------------------- */

/* A key while the list is there: the choice moved, taken or let go; the others are the editor's,
   and the list follows what they do to the word */
static gboolean
lsp_session_key (lsp_plugin_t *plugin, WEdit *edit, int key)
{
    lsp_session_t *s = plugin->session;
    const lsp_window_t *w = lsp_window_of (plugin, edit);
    const gboolean shown = plugin->popup != NULL && lsp_popup_shown (plugin->popup);

    if (s == NULL || w == NULL || s->window_id != w->id)
        return FALSE;
    // the cursor put out of the word: the list is of no use, the key is the editor's
    if (!lsp_session_in_word (s, edit, edit->buffer.curs1 - (s->pending != 0 ? 1 : 0)))
    {
        lsp_session_end (plugin);
        return FALSE;
    }
    if (s->pending != 0)
    {
        const char pending = s->pending;

        s->pending = 0;
        // "->" or "::": the item, then the members of it
        if ((pending == '-' && key == '>') || (pending == ':' && key == ':'))
        {
            lsp_session_take (plugin, edit, edit->buffer.curs1 - 1);
            g_free (plugin->trigger);
            plugin->trigger = g_strdup (pending == '-' ? "->" : "::");
            lsp_ask_later (plugin);
        }
        else
            lsp_session_end (plugin);
        return FALSE;
    }
    if (shown)
        switch (key)
        {
        case KEY_UP:
            lsp_popup_move (plugin->popup, -1);
            return TRUE;
        case KEY_DOWN:
            lsp_popup_move (plugin->popup, 1);
            return TRUE;
        case KEY_PPAGE:
            lsp_popup_move (plugin->popup, -LSP_POPUP_LINES);
            return TRUE;
        case KEY_NPAGE:
            lsp_popup_move (plugin->popup, LSP_POPUP_LINES);
            return TRUE;
        case '\n':
        case KEY_ENTER:
        case '\t':
            lsp_session_take (plugin, edit, edit->buffer.curs1);
            return TRUE;
        case ESC_CHAR:
            lsp_session_end (plugin);
            return TRUE;
        case '.':
            // the item, then the members of it
            lsp_session_take (plugin, edit, edit->buffer.curs1);
            g_free (plugin->trigger);
            plugin->trigger = g_strdup (".");
            lsp_ask_later (plugin);
            return FALSE;
        case '-':
        case ':':
            s->pending = (char) key;
            return FALSE;
        default:
            break;
        }
    if (key == KEY_BACKSPACE || (key >= 0 && key < 0x100 && lsp_word_byte ((char) key)))
    {
        plugin->refilter = TRUE;
        lsp_ask_later (plugin);
    }
    else
        lsp_session_end (plugin);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The trigger of the server just typed before the cursor: "->" or "::" before the character
   itself; NULL when it is none */
static const char *
lsp_trigger_typed (WEdit *edit, const lsp_window_t *w, const char *typed)
{
    static char two[3];
    const off_t at = edit->buffer.curs1;

    if (typed == NULL || w == NULL || w->server == NULL || at < 1)
        return NULL;
    two[0] = at >= 2 ? (char) edit_buffer_get_byte (&edit->buffer, at - 2) : '\0';
    two[1] = (char) edit_buffer_get_byte (&edit->buffer, at - 1);
    two[2] = '\0';
    if (two[0] != '\0' && lsp_client_completion_trigger (w->server->client, two))
        return two;
    if (two[1] == typed[0] && lsp_client_completion_trigger (w->server->client, two + 1))
        return two + 1;
    // the trigger of a list taken: "->" after the item put in place of the word
    if (strlen (typed) == 2 && strcmp (two, typed) == 0)
        return two;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The symbols of the file of the definition of @preview asked for, the file opened on the server
   when it has not got it; FALSE when they cannot be */
static gboolean
lsp_preview_symbols (lsp_plugin_t *plugin, lsp_preview_t *preview, const lsp_ask_t *after)
{
    JsonObject *params, *document;
    lsp_ask_t *ask;
    char *path = lsp_path_from_uri (preview->uri);
    guint64 id;

    if (path == NULL || !lsp_client_supports (preview->client, "documentSymbolProvider"))
    {
        g_free (path);
        return FALSE;
    }
    if (!lsp_client_has_doc (preview->client, preview->uri))
    {
        const char *language = NULL;
        gsize len = 0;
        char *text = lsp_file_text (plugin, path, &len);

        if (text == NULL || lsp_config_backend_for (plugin->config, path, &language) == NULL)
        {
            g_free (text);
            g_free (path);
            return FALSE;
        }
        lsp_client_doc_open (preview->client, preview->uri, language, text, len);
        preview->opened = TRUE;
        g_free (text);
    }
    g_free (path);

    // of the window and the text of Hover: the answer is of no use once they change
    ask = g_new0 (lsp_ask_t, 1);
    ask->plugin = plugin;
    ask->kind = LSP_ASK_SYMBOLS;
    ask->preview = preview->id;
    ask->window_id = after->window_id;
    ask->revision = after->revision;
    ask->offset = after->offset;
    ask->encoding = after->encoding;
    ask->text = g_bytes_ref (after->text);
    params = json_object_new ();
    document = json_object_new ();
    json_object_set_string_member (document, "uri", preview->uri);
    json_object_set_object_member (params, "textDocument", document);
    id = lsp_client_request (preview->client, "textDocument/documentSymbol",
                             lsp_object_node (params), 0, lsp_answered, ask, NULL);
    if (id == 0)
        lsp_ask_free (ask);
    return id != 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The definition of the name of Hover, then the symbols of its file: the source of the
   definition, Hover shown with it, or without it when it cannot be had */
static void
lsp_preview_answer (lsp_plugin_t *plugin, lsp_window_t *w, lsp_ask_t *ask)
{
    lsp_preview_t *preview = plugin->preview;
    char *source = NULL;

    // the answer of a Hover asked for before: this one is not its
    if (preview == NULL || preview->id != ask->preview)
        return;
    // the window gone, its text changed, the cursor moved: Hover is of no use
    if (w == NULL || plugin->host->get_revision (plugin->host, w->edit) != ask->revision
        || w->edit->buffer.curs1 != ask->offset)
    {
        lsp_preview_free (plugin, preview);
        plugin->preview = NULL;
        return;
    }
    if (ask->kind == LSP_ASK_PREVIEW && ask->status == LSP_REQUEST_OK)
    {
        JsonObject *place = lsp_first_location (ask->result);
        JsonObject *range = lsp_member_object (place, "targetRange");

        if (range != NULL)
        {
            // a LocationLink has all of the definition
            char *path = lsp_path_from_uri (lsp_member_string (place, "targetUri"));
            const gint64 first = lsp_member_int (lsp_member_object (range, "start"), "line", 0);

            if (path != NULL)
                source = lsp_source_lines (
                    plugin, path, first,
                    lsp_member_int (lsp_member_object (range, "end"), "line", first));
            g_free (path);
        }
        else if (place != NULL && lsp_member_string (place, "uri") != NULL)
        {
            // a Location has the name alone: the symbol named there has all of it
            JsonObject *start = lsp_member_object (lsp_member_object (place, "range"), "start");

            preview->uri = g_strdup (lsp_member_string (place, "uri"));
            preview->line = lsp_member_int (start, "line", 0);
            preview->character = lsp_member_int (start, "character", 0);
            if (lsp_preview_symbols (plugin, preview, ask))
                return;
        }
    }
    else if (ask->kind == LSP_ASK_SYMBOLS && ask->status == LSP_REQUEST_OK)
    {
        gint64 first, last;
        char *path = lsp_path_from_uri (preview->uri);

        if (path != NULL
            && lsp_symbol_extent (ask->result, preview->line, preview->character, &first, &last))
            source = lsp_source_lines (plugin, path, first, last);
        g_free (path);
    }
    lsp_show_hover (plugin, w, ask, preview->hover, source);
    g_free (source);
    lsp_preview_free (plugin, preview);
    plugin->preview = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The editor idle: the answers shown, the completion of a trigger asked for */
static void
lsp_later (void *data)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;
    lsp_ask_t *ask;
    char *trigger;
    gboolean refilter;

    plugin->later = FALSE;
    /* the answers first, what was typed then: an answer that ends a completion, given up for no
       answer, comes with the key typed after it, which then asks again */
    trigger = plugin->trigger;
    refilter = plugin->refilter;
    plugin->trigger = NULL;
    plugin->refilter = FALSE;
    while ((ask = g_queue_pop_head (plugin->answers)) != NULL)
    {
        lsp_window_t *w = ask->window_id != 0 ? lsp_window_by_id (plugin, ask->window_id) : NULL;
        char *error = ask->error != NULL ? lsp_shown (ask->error, 20) : NULL;

        if (ask->kind == LSP_ASK_PREVIEW || ask->kind == LSP_ASK_SYMBOLS)
            // Hover waited for the source of the definition
            lsp_preview_answer (plugin, w, ask);
        else if (ask->kind == LSP_ASK_COMPLETION && ask->window_id != 0)
            // the list follows what is typed: the text has changed since, and that is right
            lsp_session_answer (plugin, w, ask);
        else if (ask->kind == LSP_ASK_SIGNATURE)
            // so does the help of a call
            lsp_signature_answer (plugin, w, ask);
        else if (ask->window_id == 0)
            // a message of a server
            plugin->host->message (plugin->host, D_ERROR, _ ("Language server"), error);
        // the window gone, its text changed, the cursor moved: the answer is of no use
        else if (w == NULL || plugin->host->get_revision (plugin->host, w->edit) != ask->revision
                 || w->edit->buffer.curs1 != ask->offset)
            ;
        else if (ask->status == LSP_REQUEST_ERROR)
            plugin->host->message (plugin->host, D_ERROR, _ ("Language server"), error);
        else if (ask->kind == LSP_ASK_HOVER)
        {
            // the source of the definition first, when the server can say where it is
            lsp_preview_free (plugin, plugin->preview);
            plugin->preview = NULL;
            if (ask->result != NULL && JSON_NODE_HOLDS_OBJECT (ask->result)
                && lsp_client_supports (w->server->client, "definitionProvider"))
            {
                plugin->preview = g_new0 (lsp_preview_t, 1);
                plugin->preview->id = ++plugin->next_preview;
                plugin->preview->hover = json_node_ref (ask->result);
                plugin->preview->client = w->server->client;
                if (!lsp_ask (plugin, w->edit, LSP_ASK_PREVIEW, NULL))
                {
                    lsp_preview_free (plugin, plugin->preview);
                    plugin->preview = NULL;
                }
            }
            if (plugin->preview == NULL)
                lsp_show_hover (plugin, w, ask, ask->result, NULL);
        }
        else if (ask->kind == LSP_ASK_REFERENCES)
            lsp_show_references (plugin, w, ask);
        else
            lsp_show_definition (plugin, w, ask);
        g_free (error);
        lsp_ask_free (ask);
    }
    if (trigger != NULL || refilter)
    {
        void *current = plugin->host->window_current (plugin->host);

        if (current != NULL && edit_widget_is_editor (CONST_WIDGET (current)))
        {
            WEdit *edit = EDIT (current);
            const lsp_window_t *w = lsp_window_of (plugin, edit);
            const char *now = lsp_trigger_typed (edit, w, trigger);

            // a '>' that is no "->", a ':' that is no "::": nothing to complete
            if (trigger == NULL || now != NULL)
                lsp_session_update (plugin, edit, FALSE, now);
        }
        g_free (trigger);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* The commands */

static void
lsp_show_problems (lsp_plugin_t *plugin, WEdit *edit)
{
    GPtrArray *rows = g_ptr_array_new_with_free_func (g_free);
    GPtrArray *places = g_ptr_array_new_with_free_func (lsp_place_free);
    const lsp_window_t *here = edit != NULL ? lsp_window_of (plugin, edit) : NULL;
    int current = -1;
    guint i;

    for (i = 0; i < plugin->servers->len; i++)
    {
        lsp_server_t *server = g_ptr_array_index (plugin->servers, i);
        GHashTableIter iter;
        gpointer uri, value;

        g_hash_table_iter_init (&iter, server->diagnostics);
        while (g_hash_table_iter_next (&iter, &uri, &value))
        {
            JsonArray *diagnostics = (JsonArray *) value;
            char *path = lsp_path_from_uri (uri);
            guint k;

            for (k = 0; path != NULL && k < json_array_get_length (diagnostics); k++)
            {
                JsonNode *node = json_array_get_element (diagnostics, k);
                static const char *const kinds[] = { N_ ("error"), N_ ("warning"), N_ ("note") };
                JsonObject *d;
                lsp_place_t *place;
                char *message;
                gint64 line, character;

                if (!JSON_NODE_HOLDS_OBJECT (node))
                    continue;
                d = json_node_get_object (node);
                if (!lsp_range_start (lsp_member_object (d, "range"), &line, &character))
                    continue;
                place = g_new0 (lsp_place_t, 1);
                place->path = g_strdup (path);
                place->line = line;
                place->character = character;
                place->encoding = lsp_client_encoding (server->client);
                g_ptr_array_add (places, place);
                message = lsp_shown (lsp_member_string (d, "message") != NULL
                                         ? lsp_member_string (d, "message")
                                         : "",
                                     1);
                g_ptr_array_add (rows,
                                 g_strdup_printf ("%s:%" G_GINT64_FORMAT ": %s: %s",
                                                  lsp_short_path (path, server->root), line + 1,
                                                  _ (kinds[lsp_mark_of (d)]), message));
                g_free (message);
                if (current < 0 && here != NULL && g_strcmp0 (here->uri, uri) == 0)
                    current = (int) rows->len - 1;
            }
            g_free (path);
        }
    }
    if (rows->len == 0)
        plugin->host->message (plugin->host, D_NORMAL, _ ("Problems"),
                               _ ("The language servers see no problem."));
    else
    {
        const int chosen = lsp_choose (_ ("Problems"), rows, current);

        if (chosen >= 0 && edit != NULL)
            lsp_go (plugin, edit, g_ptr_array_index (places, chosen));
    }
    g_ptr_array_free (places, TRUE);
    g_ptr_array_free (rows, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_show_status (lsp_plugin_t *plugin, WEdit *edit)
{
    GString *out = g_string_new (NULL);
    const lsp_window_t *w = edit != NULL ? lsp_window_sync (plugin, edit, FALSE) : NULL;
    char *shown;
    guint i;

    if (w != NULL && w->server == NULL)
        g_string_append_printf (out, _ ("This file: %s\n\n"), w->why != NULL ? w->why : "");
    else if (w != NULL)
        g_string_append_printf (out, _ ("This file: %s, %s\n\n"), w->server->backend_id,
                                w->language);
    for (i = 0; i < plugin->servers->len; i++)
    {
        const lsp_server_t *server = g_ptr_array_index (plugin->servers, i);
        const lsp_client_t *client = server->client;
        const lsp_client_state_t state = lsp_client_state (client);

        g_string_append_printf (out, "%s  %s\n  %s", server->backend_id, server->root,
                                lsp_client_state_name (state));
        if (lsp_client_reason (client) != NULL)
            g_string_append_printf (out, ": %s", lsp_client_reason (client));
        if (lsp_client_pid (client) != 0)
            g_string_append_printf (out, _ (", pid %d"), (int) lsp_client_pid (client));
        if (state == LSP_CLIENT_RUNNING)
            g_string_append_printf (out, _ (", positions in %s"),
                                    lsp_client_encoding (client) == LSP_ENCODING_UTF8 ? "UTF-8"
                                                                                      : "UTF-16");
        g_string_append_printf (out, _ (", %u files, %u requests"), lsp_client_documents (client),
                                lsp_client_pending (client));
        if (server->failures->len > 0)
            g_string_append_printf (out, _ (", %u restarts"), server->failures->len);
        if (server->manual_stop)
            g_string_append (out, _ (", stopped by the user"));
        g_string_append (out, "\n");
    }
    if (plugin->servers->len == 0)
        g_string_append (out, _ ("No language server runs.\n"));
    if (plugin->config_errors != NULL)
        g_string_append_printf (out, _ ("\nlsp.json:\n%s"), plugin->config_errors);
    shown = lsp_shown (out->str, (gsize) MAX (LINES - 8, 3));
    plugin->host->message (plugin->host, D_NORMAL, _ ("Language servers"), shown);
    g_free (shown);
    g_string_free (out, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_show_log (lsp_plugin_t *plugin, WEdit *edit)
{
    const lsp_window_t *w = edit != NULL ? lsp_window_of (plugin, edit) : NULL;
    lsp_server_t *server = w != NULL ? w->server : NULL;
    GPtrArray *rows;
    const GList *l;

    if (server == NULL && plugin->servers->len == 1)
        server = g_ptr_array_index (plugin->servers, 0);
    else if (server == NULL && plugin->servers->len > 1)
    {
        guint i;
        int chosen;

        rows = g_ptr_array_new_with_free_func (g_free);
        for (i = 0; i < plugin->servers->len; i++)
        {
            const lsp_server_t *s = g_ptr_array_index (plugin->servers, i);

            g_ptr_array_add (rows, g_strdup_printf ("%s  %s", s->backend_id, s->root));
        }
        chosen = lsp_choose (_ ("Log of the language server"), rows, 0);
        g_ptr_array_free (rows, TRUE);
        if (chosen < 0)
            return;
        server = g_ptr_array_index (plugin->servers, chosen);
    }
    if (server == NULL)
    {
        plugin->host->message (plugin->host, D_NORMAL, _ ("Language servers"),
                               _ ("No language server runs."));
        return;
    }
    rows = g_ptr_array_new ();
    for (l = lsp_client_log (server->client)->head; l != NULL; l = l->next)
        g_ptr_array_add (rows, l->data);
    if (rows->len == 0)
        g_ptr_array_add (rows, (gpointer) _ ("(empty)"));
    (void) lsp_choose (server->backend_id, rows, (int) rows->len - 1);
    g_ptr_array_free (rows, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* The system's lsp.json and the user's */
static void
lsp_config_files (char **system, char **user)
{
    *system = g_build_filename (mc_global.sysconfig_dir, LSP_CONFIG_FILE, (char *) NULL);
    *user = g_build_filename (mc_config_get_path (), LSP_CONFIG_FILE, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_load_config (lsp_plugin_t *plugin)
{
    char *system, *user;
    GString *errors = g_string_new (NULL);

    lsp_config_files (&system, &user);
    plugin->config = lsp_config_load (system, user, errors);
    g_free (plugin->config_errors);
    plugin->config_errors = errors->len > 0 ? g_strdup (errors->str) : NULL;
    g_string_free (errors, TRUE);
    g_free (system);
    g_free (user);
}

/* --------------------------------------------------------------------------------------------- */

/* lsp.json read again: the servers of before stop, the windows go to the servers of now */
static void
lsp_reload (lsp_plugin_t *plugin)
{
    guint i;

    for (i = 0; i < plugin->windows->len; i++)
        lsp_window_unbind (plugin, g_ptr_array_index (plugin->windows, i));
    for (i = 0; i < plugin->servers->len; i++)
    {
        lsp_server_t *server = g_ptr_array_index (plugin->servers, i);

        server->retired = TRUE;
        server->manual_stop = TRUE;
        server->restart_at = 0;
        server->restart_stopped = FALSE;
        lsp_client_stop (server->client);
    }
    lsp_config_free (plugin->config);
    lsp_load_config (plugin);
    for (i = 0; i < plugin->windows->len; i++)
        (void) lsp_window_sync (
            plugin, ((lsp_window_t *) g_ptr_array_index (plugin->windows, i))->edit, FALSE);
    lsp_ticker_update (plugin);
    plugin->host->redraw (plugin->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_settings_dialog (lsp_plugin_t *plugin)
{
    char *system, *user;

    lsp_config_files (&system, &user);
    if (lsp_wizard_run (system, user))
        lsp_reload (plugin);
    g_free (system);
    g_free (user);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_run_command (lsp_plugin_t *plugin, lsp_cmd_t cmd, WEdit *edit)
{
    lsp_window_t *w;

    switch (cmd)
    {
    case LSP_CMD_COMPLETION:
        if (lsp_window_served (plugin, edit, TRUE) == NULL)
            return FALSE;
        lsp_session_update (plugin, edit, TRUE, NULL);
        return TRUE;
    case LSP_CMD_DEFINITION:
        return lsp_ask (plugin, edit, LSP_ASK_DEFINITION, NULL);
    case LSP_CMD_HOVER:
        return lsp_ask (plugin, edit, LSP_ASK_HOVER, NULL);
    case LSP_CMD_SIGNATURE:
        return lsp_signature_start (plugin, edit, TRUE);
    case LSP_CMD_PROBLEMS:
        lsp_show_problems (plugin, edit);
        return TRUE;
    case LSP_CMD_STATUS:
        lsp_show_status (plugin, edit);
        return TRUE;
    case LSP_CMD_LOG:
        lsp_show_log (plugin, edit);
        return TRUE;
    case LSP_CMD_SETTINGS:
        lsp_settings_dialog (plugin);
        return TRUE;
    case LSP_CMD_DECLARATION:
        return lsp_ask (plugin, edit, LSP_ASK_DECLARATION, NULL);
    case LSP_CMD_HISTORY:
        if (edit != NULL)
            edit_nav_history (edit);
        return TRUE;
    case LSP_CMD_REFERENCES:
        return lsp_ask (plugin, edit, LSP_ASK_REFERENCES, NULL);
    default:
        break;
    }

    // the server of the file
    w = edit != NULL ? lsp_window_sync (plugin, edit, TRUE) : NULL;
    if (w == NULL || w->server == NULL)
    {
        plugin->host->message (plugin->host, D_NORMAL, _ ("Language server"),
                               w != NULL && w->why != NULL ? w->why : _ ("No file window."));
        return TRUE;
    }
    switch (cmd)
    {
    case LSP_CMD_SETUP:
        if (!lsp_setup_knows (w->server->command[0]))
            plugin->host->message (plugin->host, D_NORMAL, _ ("Project setup"),
                                   _ ("The project setup knows clangd only, for now."));
        else if (lsp_setup_run (w->server->root, w->server->command[0]))
            // the server reads its configuration as it starts, whatever it does now
            return lsp_run_command (plugin, LSP_CMD_RESTART, edit);
        break;
    case LSP_CMD_START:
        if (lsp_client_state (w->server->client) == LSP_CLIENT_STOPPING)
            w->server->restart_stopped = TRUE;
        else
            (void) lsp_server_start (w->server, TRUE);
        break;
    case LSP_CMD_STOP:
        w->server->manual_stop = TRUE;
        w->server->restart_at = 0;
        w->server->restart_stopped = FALSE;
        lsp_client_stop (w->server->client);
        break;
    case LSP_CMD_RESTART:
        w->server->manual_stop = FALSE;
        w->server->start_failed = FALSE;
        g_array_set_size (w->server->failures, 0);
        switch (lsp_client_state (w->server->client))
        {
        case LSP_CLIENT_RUNNING:
        case LSP_CLIENT_STOPPING:
            w->server->restart_stopped = TRUE;
            lsp_client_stop (w->server->client);
            break;
        case LSP_CLIENT_STARTING:
            lsp_client_stop (w->server->client);
            (void) lsp_server_start (w->server, TRUE);
            break;
        default:
            (void) lsp_server_start (w->server, TRUE);
            break;
        }
        break;
    default:
        break;
    }
    lsp_ticker_update (plugin);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static lsp_cmd_t
lsp_command_of (const lsp_plugin_t *plugin, long command)
{
    int i;

    for (i = 0; i < LSP_CMD_COUNT; i++)
        if (plugin->commands[i] == command)
            return (lsp_cmd_t) i;
    return LSP_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */
/* The plugin */

static mc_ep_result_t
lsp_handle_key (void *data, int key, void *edit)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;
    lsp_cmd_t cmd;

    if (edit == NULL)
        return MC_EPR_NOT_SUPPORTED;
    cmd = lsp_command_of (plugin,
                          plugin->host->command_lookup (plugin->host, LSP_KEYMAP_SECTION, key));
    /* Quick documentation: PgUp and PgDn scroll it; its key again has it read, the arrows
       scrolling it, and once read takes it away; Esc only takes it away; another key takes it
       away and does what it does */
    if (lsp_note_shown (plugin->note))
    {
        if (lsp_note_key (plugin->note, key))
            return MC_EPR_OK;
        if (cmd == LSP_CMD_HOVER && !lsp_note_reading (plugin->note))
        {
            lsp_note_read (plugin->note);
            return MC_EPR_OK;
        }
        lsp_note_hide (plugin->note);
        if (key == ESC_CHAR || cmd == LSP_CMD_HOVER)
            return MC_EPR_OK;
    }
    // out of sight, the screen too small for it: it goes all the same
    else if (lsp_note_there (plugin->note))
        lsp_note_hide (plugin->note);
    /* the help of a call takes no key but Esc, which takes it away, and Alt-Up and Alt-Down,
       which go through the signatures when there are several */
    if (lsp_note_shown (plugin->signature))
    {
        if (key == ESC_CHAR)
        {
            lsp_signature_end (plugin);
            return MC_EPR_OK;
        }
        if ((key == ALT (KEY_UP) || key == ALT (KEY_DOWN))
            && lsp_signature_next (plugin, key == ALT (KEY_UP) ? -1 : 1))
            return MC_EPR_OK;
    }
    if (cmd != LSP_CMD_NONE)
    {
        // a file no server serves: the key is the editor's
        const lsp_window_t *w = lsp_window_sync (plugin, EDIT (edit), FALSE);

        if (w->server == NULL && cmd != LSP_CMD_PROBLEMS && cmd != LSP_CMD_STATUS
            && cmd != LSP_CMD_LOG && cmd != LSP_CMD_SETTINGS)
            return MC_EPR_NOT_SUPPORTED;
        (void) lsp_run_command (plugin, cmd, EDIT (edit));
        return MC_EPR_OK;
    }
    // the list of a completion has the keys it moves by; it follows the others
    if (plugin->session != NULL)
    {
        const gboolean taken = lsp_session_key (plugin, EDIT (edit), key);

        if (taken)
            return MC_EPR_OK;
        if (plugin->session != NULL || plugin->trigger != NULL)
            return MC_EPR_NOT_SUPPORTED;
    }
    // as a word is typed, or after a dot: a completion, once the editor has typed the character
    if (lsp_now (plugin, plugin->config->completion_on_trigger) && key > ' ' && key < 0x100)
    {
        const lsp_window_t *w = lsp_window_of (plugin, EDIT (edit));
        const char c[2] = { (char) key, '\0' };

        if (w == NULL || w->server == NULL
            || lsp_client_state (w->server->client) != LSP_CLIENT_RUNNING)
            return MC_EPR_NOT_SUPPORTED;
        if (lsp_word_byte ((char) key))
        {
            plugin->refilter = TRUE;
            lsp_ask_later (plugin);
        }
        else if (lsp_client_completion_trigger (w->server->client, c)
                 || (key == '>' && lsp_client_completion_trigger (w->server->client, "->"))
                 || (key == ':' && lsp_client_completion_trigger (w->server->client, "::")))
        {
            g_free (plugin->trigger);
            plugin->trigger = g_strdup (c);
            lsp_ask_later (plugin);
        }
        // a call opened: its parameters, asked for once the editor has typed the character
        if (lsp_client_trigger (w->server->client, "signatureHelpProvider", c))
            (void) lsp_signature_start (plugin, EDIT (edit), FALSE);
    }
    return MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
lsp_handle_action (void *data, long command, void *edit)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;
    const lsp_cmd_t cmd = lsp_command_of (plugin, command);

    if (cmd == LSP_CMD_NONE)
        return MC_EPR_NOT_SUPPORTED;
    (void) lsp_run_command (plugin, cmd, (WEdit *) edit);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the server of the file of @edit is to answer what the plugins are offered, in the round
   @payload: the first, and only when the server can */
static gboolean
lsp_offered (lsp_plugin_t *plugin, WEdit *edit, void *payload, const char *provider)
{
    const lsp_window_t *w;

    if (GPOINTER_TO_INT (payload) != 0)
        return FALSE;
    w = lsp_window_served (plugin, edit, FALSE);
    return w != NULL && lsp_client_supports (w->server->client, provider);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
lsp_handle_event (void *data, void *edit, int event_id, void *payload)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;
    lsp_window_t *w;

    if (edit == NULL)
        return MC_EPR_NOT_SUPPORTED;
    switch (event_id)
    {
    case MC_EP_EVENT_CTRL_CLICK:
        // what the server knows comes first; the tags guess after it, with no word of a server
        if (lsp_offered (plugin, EDIT (edit), payload, "definitionProvider")
            && lsp_ask (plugin, EDIT (edit), LSP_ASK_DEFINITION, NULL))
            return MC_EPR_OK;
        break;
    case MC_EP_EVENT_REFERENCES:
        if (lsp_offered (plugin, EDIT (edit), payload, "referencesProvider")
            && lsp_ask (plugin, EDIT (edit), LSP_ASK_REFERENCES, NULL))
            return MC_EPR_OK;
        break;
    case MC_EP_EVENT_FOCUS_OUT:
    case MC_EP_EVENT_CURSOR_MOVED:
        // the list and Hover are of a word of a line of this window, the one asked for too
        if (plugin->session != NULL)
            lsp_session_end (plugin);
        if (lsp_note_there (plugin->note))
            lsp_note_hide (plugin->note);
        lsp_preview_free (plugin, plugin->preview);
        plugin->preview = NULL;
        // the help of a call follows the lines of the call, in its window
        if (event_id == MC_EP_EVENT_FOCUS_OUT)
            lsp_signature_end (plugin);
        break;
    case MC_EP_EVENT_TEXT_CHANGED:
    case MC_EP_EVENT_FILE_RENAMED:
        (void) lsp_window_sync (plugin, EDIT (edit), TRUE);
        break;
    case MC_EP_EVENT_FILE_SAVED:
        w = lsp_window_sync (plugin, EDIT (edit), FALSE);
        if (w->server != NULL)
        {
            const char *why = NULL;
            gsize len = 0;
            char *text = lsp_window_text (plugin, EDIT (edit), &len, &why);

            if (text != NULL)
                lsp_client_doc_save (w->server->client, w->uri, text, len);
            g_free (text);
        }
        break;
    default:
        break;
    }
    return MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
lsp_file_open (void *data, void *edit)
{
    (void) lsp_window_sync ((lsp_plugin_t *) data, EDIT (edit), FALSE);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
lsp_file_close (void *data, void *edit)
{
    lsp_window_forget ((lsp_plugin_t *) data, EDIT (edit));
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

static void *
lsp_open (mc_editor_host_t *host, void *editor_dialog)
{
    lsp_plugin_t *plugin;
    int i;

    (void) editor_dialog;
    if (lsp_plugin != NULL)
        return NULL;
    plugin = g_new0 (lsp_plugin_t, 1);
    plugin->host = host;
    plugin->servers = g_ptr_array_new ();
    plugin->windows = g_ptr_array_new ();
    plugin->answers = g_queue_new ();
    plugin->project = host->startup_option (host, "debug");
    plugin->popup = lsp_popup_new (lsp_session_take_cb, plugin);
    plugin->note = lsp_note_new ();
    plugin->signature = lsp_note_new ();
    lsp_note_set_place (plugin->signature, TRUE, LSP_SIGNATURE_LINES);
    lsp_load_config (plugin);

    host->commands_register (host, LSP_KEYMAP_SECTION, N_ ("Language servers"), lsp_commands);
    for (i = 0; i < LSP_CMD_COUNT; i++)
        plugin->commands[i] = host->command_id (host, lsp_commands[i].name);
    for (i = 0; i < LSP_MARK_COUNT; i++)
        plugin->marks[i] =
            host->marker_kind != NULL ? host->marker_kind (host, &lsp_mark_kinds[i]) : -1;

    plugin->tick_pipe[0] = plugin->tick_pipe[1] = -1;
    if (pipe (plugin->tick_pipe) == 0)
    {
        for (i = 0; i < 2; i++)
        {
            (void) fcntl (plugin->tick_pipe[i], F_SETFL,
                          fcntl (plugin->tick_pipe[i], F_GETFL) | O_NONBLOCK);
            (void) fcntl (plugin->tick_pipe[i], F_SETFD, FD_CLOEXEC);
        }
        add_select_channel (plugin->tick_pipe[0], lsp_tick_ready, plugin);
        plugin->ticker = g_thread_new ("lsp-ticker", lsp_ticker, plugin);
    }
    lsp_plugin = plugin;
    return plugin;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_close (void *data)
{
    lsp_plugin_t *plugin = (lsp_plugin_t *) data;
    guint i;

    if (plugin->ticker != NULL)
    {
        g_atomic_int_set (&plugin->ticker_stop, 1);
        g_thread_join (plugin->ticker);
    }
    if (plugin->tick_pipe[0] >= 0)
    {
        delete_select_channel (plugin->tick_pipe[0]);
        close (plugin->tick_pipe[0]);
        close (plugin->tick_pipe[1]);
    }
    /* before the servers quit, and before the windows, which it looks at: the file it opened on
       one is closed there */
    lsp_preview_free (plugin, plugin->preview);
    plugin->preview = NULL;
    // all the servers told at once, then waited for together
    for (i = 0; i < plugin->servers->len; i++)
        lsp_client_quit (((lsp_server_t *) g_ptr_array_index (plugin->servers, i))->client);
    lsp_client_reap_all (1000);
    for (i = 0; i < plugin->windows->len; i++)
    {
        lsp_window_t *w = g_ptr_array_index (plugin->windows, i);

        g_free (w->path);
        g_free (w->uri);
        g_free (w->language);
        g_free (w->why);
        g_free (w);
    }
    g_ptr_array_free (plugin->windows, TRUE);
    g_ptr_array_set_free_func (plugin->servers, lsp_server_free);
    g_ptr_array_free (plugin->servers, TRUE);
    g_queue_free_full (plugin->answers, (GDestroyNotify) lsp_ask_free);
    g_free (plugin->trigger);
    lsp_session_free (plugin->session);
    lsp_popup_free (plugin->popup);
    lsp_note_free (plugin->note);
    lsp_note_free (plugin->signature);
    if (plugin->sig_help != NULL)
        json_node_unref (plugin->sig_help);
    lsp_config_free (plugin->config);
    g_free (plugin->config_errors);
    g_free (plugin);
    lsp_plugin = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Options > Manage plugins, the plugin chosen */
static mc_ep_result_t
lsp_configure (void *data, void *edit)
{
    (void) edit;
    lsp_settings_dialog ((lsp_plugin_t *) data);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* The menu */

static mc_ep_result_t
lsp_action (lsp_cmd_t cmd, void *edit)
{
    if (lsp_plugin == NULL)
        return MC_EPR_FAILED;
    (void) lsp_run_command (lsp_plugin, cmd, (WEdit *) edit);
    return MC_EPR_OK;
}

#define LSP_ACTION(name, cmd)                                                                      \
    static mc_ep_result_t name (void *data, void *edit)                                            \
    {                                                                                              \
        (void) data;                                                                               \
        return lsp_action (cmd, edit);                                                             \
    }

LSP_ACTION (lsp_act_completion, LSP_CMD_COMPLETION)
LSP_ACTION (lsp_act_definition, LSP_CMD_DEFINITION)
LSP_ACTION (lsp_act_hover, LSP_CMD_HOVER)
LSP_ACTION (lsp_act_signature, LSP_CMD_SIGNATURE)
LSP_ACTION (lsp_act_problems, LSP_CMD_PROBLEMS)
LSP_ACTION (lsp_act_start, LSP_CMD_START)
LSP_ACTION (lsp_act_stop, LSP_CMD_STOP)
LSP_ACTION (lsp_act_restart, LSP_CMD_RESTART)
LSP_ACTION (lsp_act_status, LSP_CMD_STATUS)
LSP_ACTION (lsp_act_log, LSP_CMD_LOG)
LSP_ACTION (lsp_act_settings, LSP_CMD_SETTINGS)
LSP_ACTION (lsp_act_declaration, LSP_CMD_DECLARATION)
LSP_ACTION (lsp_act_setup, LSP_CMD_SETUP)
LSP_ACTION (lsp_act_references, LSP_CMD_REFERENCES)
LSP_ACTION (lsp_act_history, LSP_CMD_HISTORY)

/* in the order of lsp_cmd_t */
static const mc_ep_action_t lsp_actions[LSP_CMD_COUNT] = {
    { "Completion", lsp_act_completion },
    { "Definition", lsp_act_definition },
    { "Hover", lsp_act_hover },
    { "Problems", lsp_act_problems },
    { "Start", lsp_act_start },
    { "Stop", lsp_act_stop },
    { "Restart", lsp_act_restart },
    { "Status", lsp_act_status },
    { "Log", lsp_act_log },
    { "Settings", lsp_act_settings },
    { "Declaration", lsp_act_declaration },
    { "Setup", lsp_act_setup },
    { "References", lsp_act_references },
    { "History", lsp_act_history },
    { "Signature", lsp_act_signature },
};

/* With no ctags, what goes to a name is in the menu LSP too: Navigate has nothing of it then */
static const mc_ep_cmd_menu_entry_t lsp_menu_alone[] = {
    { MC_EP_MENU_NAVIGATE, N_ ("Go to the protot&ype"), LSP_CMD_DECLARATION, NULL },
    { LSP_MENU, N_ ("Go to &definition"), LSP_CMD_DEFINITION, NULL },
    { LSP_MENU, N_ ("Find re&ferences"), LSP_CMD_REFERENCES, NULL },
    { LSP_MENU, N_ ("Navigation histor&y..."), LSP_CMD_HISTORY, NULL },
    { LSP_MENU, NULL, 0, NULL },
    { LSP_MENU, N_ ("&Completion"), LSP_CMD_COMPLETION, NULL },
    { LSP_MENU, N_ ("&Quick documentation"), LSP_CMD_HOVER, NULL },
    { LSP_MENU, N_ ("Parameter &info"), LSP_CMD_SIGNATURE, NULL },
    { LSP_MENU, N_ ("&Problems..."), LSP_CMD_PROBLEMS, NULL },
    { LSP_MENU, NULL, 0, NULL },
    { LSP_MENU, N_ ("&Start server"), LSP_CMD_START, NULL },
    { LSP_MENU, N_ ("S&top server"), LSP_CMD_STOP, NULL },
    { LSP_MENU, N_ ("&Restart server"), LSP_CMD_RESTART, NULL },
    { LSP_MENU, NULL, 0, NULL },
    { LSP_MENU, N_ ("St&atus..."), LSP_CMD_STATUS, NULL },
    { LSP_MENU, N_ ("&Log..."), LSP_CMD_LOG, NULL },
    { LSP_MENU, N_ ("S&ettings..."), LSP_CMD_SETTINGS, NULL },
    { LSP_MENU, N_ ("Pro&ject setup..."), LSP_CMD_SETUP, NULL },
};

static const mc_ep_cmd_menu_entry_t lsp_menu[] = {
    // what goes to a name is in Navigate, with the tags: the server is asked first
    { MC_EP_MENU_NAVIGATE, N_ ("Go to the protot&ype"), LSP_CMD_DECLARATION, NULL },
    { LSP_MENU, N_ ("&Completion"), LSP_CMD_COMPLETION, NULL },
    { LSP_MENU, N_ ("&Quick documentation"), LSP_CMD_HOVER, NULL },
    { LSP_MENU, N_ ("Parameter &info"), LSP_CMD_SIGNATURE, NULL },
    { LSP_MENU, N_ ("&Problems..."), LSP_CMD_PROBLEMS, NULL },
    { LSP_MENU, NULL, 0, NULL },
    { LSP_MENU, N_ ("&Start server"), LSP_CMD_START, NULL },
    { LSP_MENU, N_ ("S&top server"), LSP_CMD_STOP, NULL },
    { LSP_MENU, N_ ("&Restart server"), LSP_CMD_RESTART, NULL },
    { LSP_MENU, NULL, 0, NULL },
    { LSP_MENU, N_ ("St&atus..."), LSP_CMD_STATUS, NULL },
    { LSP_MENU, N_ ("&Log..."), LSP_CMD_LOG, NULL },
    { LSP_MENU, N_ ("S&ettings..."), LSP_CMD_SETTINGS, NULL },
    { LSP_MENU, N_ ("Pro&ject setup..."), LSP_CMD_SETUP, NULL },
};

/* --------------------------------------------------------------------------------------------- */

/* The key of a menu entry, the first the keymap has for its command */
static char *
lsp_menu_shortcut (int action_index)
{
    const global_keymap_t *map = keymap_section_map (LSP_KEYMAP_SECTION);
    long command;
    size_t i;

    if (map == NULL || action_index < 0 || action_index >= LSP_CMD_COUNT)
        return NULL;
    command = keybind_lookup_action (lsp_commands[action_index].name);
    for (i = 0; map[i].key != 0; i++)
        if (map[i].command == command && map[i].caption[0] != '\0')
            return g_strdup (map[i].caption);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_editor_plugin_t lsp_plugin_table = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "lsp",
    .display_name = "Language servers",
    .flags = MC_EPF_NONE,
    .open = lsp_open,
    .close = lsp_close,
    .on_file_open = lsp_file_open,
    .on_file_close = lsp_file_close,
    .handle_key = lsp_handle_key,
    .handle_action = lsp_handle_action,
    .handle_event = lsp_handle_event,
    .configure = lsp_configure,
    .actions = lsp_actions,
    .action_count = G_N_ELEMENTS (lsp_actions),
    .cmd_menu_entries = lsp_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (lsp_menu),
    .get_menu_shortcut = lsp_menu_shortcut,
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The entry of the module, which the editor calls when it loads it */
const mc_editor_plugin_t *
mc_editor_plugin_register (void)
{
    // the plugins of the program are there before those of the modules
    if (mc_editor_plugin_find_by_name ("ctags") == NULL
        || mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_EDITOR, "ctags"))
    {
        lsp_plugin_table.cmd_menu_entries = lsp_menu_alone;
        lsp_plugin_table.cmd_menu_entry_count = G_N_ELEMENTS (lsp_menu_alone);
    }
    return &lsp_plugin_table;
}

/* --------------------------------------------------------------------------------------------- */
