/*
   The query plugin: the queries of a file run by the shell of a database, its answer under it.

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

/** \file query.c
 *  \brief Source: the query plugin
 *
 *  A module the editor loads from its directory of plugins; it works in coole -Q alone.  F5 gives
 *  the selection, else the part of the file between lines "// ---", else the whole file, to the
 *  backend of the database, and its answer comes in the tabs at the bottom: Output, what it
 *  printed; Table, the documents or the rows in columns; History, the queries run.  The backend
 *  is chosen by the value of -Q: mongo for mongosh (query-mongo.c), postgres for libpq
 *  (query-postgres.c).
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>  // PRId64: G_GINT64_FORMAT is not read by xgettext
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/event.h"
#include "lib/mcconfig.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/editor-plugin.h"
#include "lib/plugin-service.h"

#include "src/keymap.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

#include "query-core.h"
#include "query-int.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define QUERY_KEYMAP_SECTION "query"
// the menu of the plugin, in the menubar before Window
#define QUERY_MENU      N_ ("&Query")
#define QUERY_HELP_FILE "query.md"
#define QUERY_HELP_NODE "[Query]"
// the menu of a file of queries, F11: in ~/.config/coole/query, else in the queries of coole
#define QUERY_MENU_FILE "query-menu.ini"
// a column of the text is not what the menu works on: lines are
#define QUERY_MENU_NO_COLUMN _ ("A column selection is not taken: select whole lines.")
#define QUERY_HISTORY_MAX    100
// the versions of a file of queries, as it was run, in the data of the user; the newest kept
#define QUERY_VERSIONS_DIR ".history"
#define QUERY_VERSIONS_MAX 200
/* the text Undo takes back whole when a version takes the place of the file, a character an
   action of it: a part of its 32768 actions */
#define QUERY_UNDO_TEXT 8000
// the status of an entry of History that is no run: the file kept before a version took its place
#define QUERY_SAVED (-2)
// the characters of a query of no file the head of Output tells at most: Table has all of it
#define QUERY_HEAD_QUERY 60
// the height of the tabs, percent of the screen
#define QUERY_TABS_SIZE 40
// what the writer of an export has before it goes to the file
#define QUERY_EXPORT_CHUNK (64 * 1024)
// the files "name.2.part" and on the export tries, those before it being there
#define QUERY_EXPORT_PARTS 100
// the rows of an INSERT of the export, unless the dialog says otherwise
#define QUERY_EXPORT_PER_INSERT 100

/*** file scope type declarations ****************************************************************/

/* A query run, in the history */
typedef struct
{
    char *when;  // "14:02:11"
    double seconds;
    int status;  // the exit code of the run, -1 when it was stopped, QUERY_SAVED for no run
    long line;   // the first line of the query in the file, from 1
    char *text;
    char *version;  // the file as it was run, a name in the directory of its versions; NULL none
    char *use;      // the context of a part run alone, a use () for mongo; NULL for none
    char *summary;  // how it ended, told in place of the exit code: "SELECT 12"; NULL for none
} query_entry_t;

/* The export of a run: its rows written to a file */
struct query_export_run_t
{
    query_export_t *writer;
    char *path;  // the file
    char *tmp;   // written first, the file once all is there: no half of a file in its place
    int fd;
    gint64 bytes;
    char *error;  // why it could not be written, NULL for none
};

/* The page of a format of the dialog of the export: the value of its button */
#define QUERY_PAGE(format) (B_USER + (int) (format))

/* What the pages of the dialog of the export are given, from one page to another */
typedef struct
{
    query_export_format_t format;                         // of the page seen
    query_export_format_t formats[QUERY_EXPORT_FORMATS];  // those of the pages, in their order
    int count;
    char *path;
    char *start;  // of the name proposed, before its extension: "/home/u/orders-20261108-101500."
    // CSV
    char *delimiter;  // as it is written: ",", "\\t" or "Tab" for a tab
    gboolean header, quote_all, null_word;
    // JSON
    gboolean indent;
    // SQL
    char *table;
    char *per;
    gboolean create_table;
    // Markdown
    gboolean null_markdown;
} query_export_form_t;

/*** forward declarations (file scope functions) *************************************************/

const mc_editor_plugin_t *mc_editor_plugin_register (void);
static void query_run_text (query_t *q, char *text, char *file, long first, char *use,
                            char *version);
static gboolean query_blank (const char *text);
static void query_current_one (query_t *q);
/* A query run as it is, or made another first by the menu of the file */
typedef enum
{
    QUERY_AS_IS,
    QUERY_AS_EXPLAIN,
    QUERY_AS_COUNT
} query_as_t;

static void query_run (query_t *q, void *edit, gboolean all, gboolean export);
static query_export_run_t *query_export_ask (query_t *q, const char *base, const char *table);
static char *query_history_file_text (query_t *q);
static char *query_version_of_text (const char *file, const char *text);

/*** file scope variables ************************************************************************/

static const mc_ep_command_t query_commands[QUERY_CMD_COUNT + 1] = {
    { "Help", NULL, "f1" },
    { "QueryRun", N_ ("Run the selection, else the part between \"// ---\", else the file"), "f5" },
    { "QueryRunAll", N_ ("Run the whole file, even with a selection"), "ctrl-f5" },
    { "QueryStop", N_ ("Stop the query"), "esc" },
    { "QueryHistoryInsert", N_ ("Put the version of the query of History into the file"), "enter" },
    { "QueryHistoryShow", N_ ("Compare the version of the query of History with the file"), "f3" },
    { "QueryRollback", N_ ("Roll the transaction of the session back"), NULL },
    { "QuerySchema", N_ ("Show or hide the schemas, the relations and their columns"),
      "ctrl-alt-s" },
    { "QuerySchemaRows", N_ ("The rows of the relation of the node of Schema in Table"), NULL },
    { "QuerySchemaInsert", N_ ("Put the name of the node of Schema into the file"), "insert" },
    { "QuerySchemaRefresh", N_ ("Read the schemas again"), NULL },
    { "QuerySchemaQuery",
      N_ ("Put the query of the rows of the relation of the node of Schema into the file"), NULL },
    { "QuerySchemaDdl",
      N_ ("Put the drop, commented, and the create of the node of Schema into the file"), "f4" },
    { "QueryAi", N_ ("Write the query of the request of the paragraph or the selection by AI"),
      "f6" },
    { "QueryExport",
      N_ ("Write all the rows of the query, or of the relation of Schema, to a file: CSV, JSON, "
          "SQL"),
      "ctrl-f6; alt-x" },
    { NULL, NULL, NULL },
};

/* the mark of the line of an error: its character from the skin, section [widget-editor]; this
   one, ASCII, when it has none or the terminal cannot show its own */
static const mc_ep_marker_kind_t query_mark_kind = {
    "query.error", "query-error-char", "E", "E", "builderror", NULL, NULL, 6, "brightred",
};

// the line of the options of a page of the dialog of the export, while it runs
static unsigned long query_export_options_line;

// the extension of the versions of a file of queries, that of the files of the backend
static const char *query_extension = ".js";

// the backends, by the value of -Q
static const query_backend_t *const query_backends[] = {
    &query_mongo_backend,
    &query_postgres_backend,
};

static const char *const query_tab_titles[QUERY_TAB_COUNT] = {
    N_ ("Output"),
    N_ ("Table"),
    N_ ("History"),
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
query_entry_free (gpointer p)
{
    query_entry_t *e = (query_entry_t *) p;

    g_free (e->when);
    g_free (e->text);
    g_free (e->version);
    g_free (e->use);
    g_free (e->summary);
    g_free (e);
}

/* --------------------------------------------------------------------------------------------- */

static GVariant *
query_viewer (query_t *q, const char *method, GVariantDict *dict)
{
    return q->host->service_call (q->host, "viewer", method, g_variant_dict_end (dict), NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_viewer_id (query_t *q, const char *method, gint64 id, gboolean focus)
{
    GVariantDict dict;
    GVariant *reply;

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", id);
    g_variant_dict_insert (&dict, "focus", "b", focus);
    reply = query_viewer (q, method, &dict);
    if (reply != NULL)
        g_variant_unref (reply);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the query run is of no file, of Schema, of History, QueryRollback: no file shows it */
static gboolean
query_of_no_file (const query_t *q)
{
    return q->file == NULL && q->text != NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The text of a tab, the tab made again when the user has closed it; the one seen when @front */
void
query_tab_set (query_t *q, int tab, const char *text, gboolean front)
{
    GVariantDict dict;
    GVariant *reply = NULL;
    char *with = NULL;

    // a query of no file, of Schema or of History: its text over its table, no file showing it
    if (tab == QUERY_TAB_TABLE && query_of_no_file (q))
    {
        char *query = g_strstrip (g_strdup (q->text));

        with = g_strdup_printf ("%s\n\n%s", query, text);
        g_free (query);
        text = with;
    }

    if (q->tabs[tab] != 0)
    {
        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "id", "x", q->tabs[tab]);
        g_variant_dict_insert (&dict, "text", "s", text);
        reply = query_viewer (q, "set_text", &dict);
        if (reply == NULL)
            q->tabs[tab] = 0;
        else
        {
            g_variant_unref (reply);
            // to its start: what was there before is gone
            g_variant_dict_init (&dict, NULL);
            g_variant_dict_insert (&dict, "id", "x", q->tabs[tab]);
            g_variant_dict_insert (&dict, "line", "x", (gint64) 0);
            reply = query_viewer (q, "scroll_to", &dict);
            if (reply != NULL)
                g_variant_unref (reply);
        }
    }
    if (q->tabs[tab] == 0)
    {
        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "text", "s", text);
        g_variant_dict_insert (&dict, "title", "s", _ (query_tab_titles[tab]));
        g_variant_dict_insert (&dict, "place", "s", "bottom");
        g_variant_dict_insert (&dict, "size", "i", QUERY_TABS_SIZE);
        g_variant_dict_insert (&dict, "focus", "b", FALSE);
        // History is a list to choose a query from
        g_variant_dict_insert (&dict, "cursorline", "b", tab == QUERY_TAB_HISTORY);
        reply = query_viewer (q, "open", &dict);
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "id", "x", &q->tabs[tab]);
            g_variant_unref (reply);
        }
    }
    if (front && q->tabs[tab] != 0)
        query_viewer_id (q, "show", q->tabs[tab], FALSE);
    g_free (with);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tab_title (query_t *q, int tab, const char *title)
{
    GVariantDict dict;
    GVariant *reply;

    if (q->tabs[tab] == 0)
        return;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", q->tabs[tab]);
    g_variant_dict_insert (&dict, "title", "s", title != NULL ? title : _ (query_tab_titles[tab]));
    reply = query_viewer (q, "set_title", &dict);
    if (reply != NULL)
        g_variant_unref (reply);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_script (const char *name, char **why)
{
    const char *misc = g_getenv ("COOLE_MISC_DIR");
    char *dirs[] = {
        g_build_filename (mc_config_get_path (), "query", (char *) NULL),
        misc != NULL ? g_build_filename (misc, "query", (char *) NULL) : NULL,
        g_build_filename (mc_global.sysconfig_dir, "query", (char *) NULL),
        g_build_filename (mc_global.share_data_dir, "query", (char *) NULL),
    };
    GError *error = NULL;
    char *text;
    guint i;

    text = query_script_read ((const char *const *) dirs, G_N_ELEMENTS (dirs), name, &error);
    if (text == NULL)
    {
        *why = g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_LOOP)
            ? g_strdup_printf (_ ("The script %s\nof the queries is a part of itself."),
                               error->message)
            : g_strdup_printf (_ ("The script %s\nof the queries is not there:\nit is in %s,\n"
                                  "or in ~/.config/coole/query."),
                               error->message, dirs[3]);
        g_error_free (error);
    }
    for (i = 0; i < G_N_ELEMENTS (dirs); i++)
        g_free (dirs[i]);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

void
query_redraw (query_t *q)
{
    // the dialogs over the editor drawn again after it: a question asked while a run goes on
    // stays over it at each second
    if (q->host->redraw != NULL)
        q->host->redraw (q->host);
    else
        widget_draw (WIDGET (q->host->host_data));
    tty_refresh ();
}

/* --------------------------------------------------------------------------------------------- */
/* The time of a run */
/* --------------------------------------------------------------------------------------------- */

/* Output says what the run does and how long it has taken: "Running... 12 s"; @front brings the
   tab forward, the tab made again when it is closed, else a closed tab stays closed */
static void
query_running_show (query_t *q, gboolean front)
{
    const gint64 seconds = (g_get_monotonic_time () - q->started) / G_USEC_PER_SEC;
    const size_t len = strlen (q->running);
    // the line is said without its newline, the time after it
    const int say = (int) (len > 0 && q->running[len - 1] == '\n' ? len - 1 : len);
    char *text;

    if (!front && q->tabs[QUERY_TAB_OUTPUT] == 0)
        return;
    if (seconds < 1)
        text = g_strdup (q->running);
    else if (seconds < 60)
        text = g_strdup_printf ("%.*s %" G_GINT64_FORMAT " s\n", say, q->running, seconds);
    else
        text = g_strdup_printf ("%.*s %" G_GINT64_FORMAT ":%02" G_GINT64_FORMAT "\n", say,
                                q->running, seconds / 60, seconds % 60);
    query_tab_set (q, QUERY_TAB_OUTPUT, text, front);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

void
query_running (query_t *q, const char *say)
{
    g_free (q->running);
    q->running = g_strdup (say != NULL ? say : q->run_line != NULL ? q->run_line : "");
    query_running_show (q, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* A second has gone: the time of the run on in Output, while it goes on */
static int
query_tick_ready (int fd, void *data)
{
    query_t *q = (query_t *) data;
    char buf[64];

    while (read (fd, buf, sizeof (buf)) > 0)
        ;
    if (q->running == NULL)
        return 0;
    // the run is over: what it gave is in Output, not the time
    if (q->backend == NULL || !q->backend->running (q->backend_data))
    {
        g_clear_pointer (&q->running, g_free);
        return 0;
    }
    query_running_show (q, FALSE);
    query_redraw (q);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* A second, written to the pipe; a full pipe has a tick waiting already */
static gpointer
query_ticker (gpointer data)
{
    query_t *q = (query_t *) data;
    int i = 0;

    while (g_atomic_int_get (&q->ticker_stop) == 0)
    {
        // a tenth at a time: the close does not wait a second for the thread
        g_usleep (G_USEC_PER_SEC / 10);
        if (++i == 10)
        {
            const ssize_t written = write (q->tick_pipe[1], "t", 1);

            (void) written;
            i = 0;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_ticker_start (query_t *q)
{
    int i;

    q->tick_pipe[0] = q->tick_pipe[1] = -1;
    if (pipe (q->tick_pipe) != 0)
        return;
    for (i = 0; i < 2; i++)
    {
        (void) fcntl (q->tick_pipe[i], F_SETFL, fcntl (q->tick_pipe[i], F_GETFL) | O_NONBLOCK);
        (void) fcntl (q->tick_pipe[i], F_SETFD, FD_CLOEXEC);
    }
    add_select_channel (q->tick_pipe[0], query_tick_ready, q);
    q->ticker = g_thread_new ("query-ticker", query_ticker, q);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_ticker_stop (query_t *q)
{
    int i;

    if (q->ticker != NULL)
    {
        g_atomic_int_set (&q->ticker_stop, 1);
        g_thread_join (q->ticker);
        q->ticker = NULL;
    }
    if (q->tick_pipe[0] >= 0)
        delete_select_channel (q->tick_pipe[0]);
    for (i = 0; i < 2; i++)
        if (q->tick_pipe[i] >= 0)
        {
            close (q->tick_pipe[i]);
            q->tick_pipe[i] = -1;
        }
}

/* --------------------------------------------------------------------------------------------- */
/* The history */
/* --------------------------------------------------------------------------------------------- */

/* The history of the queries of a file: in the data of the user, by the path of the file */
static char *
query_history_path_of (const char *file)
{
    char *hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, file, -1);
    char *name = g_strconcat (hash, ".history", (char *) NULL);
    char *path = g_build_filename (g_get_user_data_dir (), "coole", "query", name, (char *) NULL);

    g_free (name);
    g_free (hash);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* The versions */
/* --------------------------------------------------------------------------------------------- */

/* The order of two names of versions in a GPtrArray, by their time and number */
static int
query_version_cmp_ptr (const void *a, const void *b)
{
    return query_version_cmp (*(char *const *) a, *(char *const *) b);
}

/* --------------------------------------------------------------------------------------------- */

/* The directory of the versions of @file: ~/.local/share/coole/queries/.history/<its name> */
static char *
query_versions_dir_of (const char *file)
{
    char *queries = g_build_filename (g_get_user_data_dir (), "coole", "queries", (char *) NULL);
    char *versions = g_build_filename (queries, QUERY_VERSIONS_DIR, (char *) NULL);
    char *dir = query_versions_dir (versions, queries, file);

    g_free (versions);
    g_free (queries);
    return dir;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @file is of the store of the versions: a version run has no versions of its own */
static gboolean
query_in_versions (const char *file)
{
    char *root = g_build_filename (g_get_user_data_dir (), "coole", "queries", QUERY_VERSIONS_DIR,
                                   (char *) NULL);
    char *prefix = g_strconcat (root, G_DIR_SEPARATOR_S, (char *) NULL);
    const gboolean in = g_str_has_prefix (file, prefix);

    g_free (prefix);
    g_free (root);
    return in;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_version_taken (const char *name, void *data)
{
    char *path = g_build_filename ((const char *) data, name, (char *) NULL);
    const gboolean taken = g_file_test (path, G_FILE_TEST_EXISTS);

    g_free (path);
    return taken;
}

/* --------------------------------------------------------------------------------------------- */

/* The names of the versions in @dir, the oldest first */
static char **
query_version_names (const char *dir)
{
    GPtrArray *names = g_ptr_array_new ();
    GDir *d = g_dir_open (dir, 0, NULL);
    const char *name;

    while (d != NULL && (name = g_dir_read_name (d)) != NULL)
        if (g_str_has_suffix (name, query_extension))
            g_ptr_array_add (names, g_strdup (name));
    if (d != NULL)
        g_dir_close (d);
    g_ptr_array_sort (names, (GCompareFunc) query_version_cmp_ptr);
    g_ptr_array_add (names, NULL);
    return (char **) g_ptr_array_free (names, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The version of @file whose text is @text: the newest when it has that text, else a new one,
   the oldest beyond QUERY_VERSIONS_MAX taken away; its name, NULL when it cannot be kept */
static char *
query_version_save (const char *file, const char *text)
{
    char *dir = query_versions_dir_of (file);
    char **names = query_version_names (dir);
    const guint n = g_strv_length (names);
    char *name = NULL, *path, *last = NULL;
    GDateTime *now;

    // a run of the file as the run before: the same version
    if (n > 0)
    {
        char *path_last = g_build_filename (dir, names[n - 1], (char *) NULL);

        if (g_file_get_contents (path_last, &last, NULL, NULL) && strcmp (last, text) == 0)
            name = g_strdup (names[n - 1]);
        g_free (path_last);
        g_free (last);
    }
    // the queries may name what is private: the directory is the user's alone
    if (name == NULL && g_mkdir_with_parents (dir, 0700) == 0)
    {
        now = g_date_time_new_now_local ();
        name = query_version_name (now, query_extension, query_version_taken, dir);
        g_date_time_unref (now);
        path = g_build_filename (dir, name, (char *) NULL);
        if (g_file_set_contents (path, text, -1, NULL))
        {
            char **all = query_version_names (dir);
            char **over = query_versions_over (all, QUERY_VERSIONS_MAX);
            char **o;

            for (o = over; *o != NULL; o++)
            {
                char *old = g_build_filename (dir, *o, (char *) NULL);

                (void) unlink (old);
                g_free (old);
            }
            g_strfreev (over);
            g_strfreev (all);
        }
        else
            g_clear_pointer (&name, g_free);
        g_free (path);
    }
    g_strfreev (names);
    g_free (dir);
    return name;
}

/* --------------------------------------------------------------------------------------------- */

/* The line of the tab History where its cursor is, from 0; -1 when the tab is not there, or
   not the window with the focus and @focused */
static long
query_history_cursor (query_t *q, gboolean focused)
{
    GVariantDict dict;
    GVariant *reply;
    gint64 line = -1;
    gboolean has = FALSE;

    if (q->tabs[QUERY_TAB_HISTORY] == 0)
        return -1;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", q->tabs[QUERY_TAB_HISTORY]);
    reply = query_viewer (q, "info", &dict);
    if (reply == NULL)
        return -1;
    (void) g_variant_lookup (reply, "focused", "b", &has);
    if (!g_variant_lookup (reply, "line", "x", &line) || (focused && !has))
        line = -1;
    g_variant_unref (reply);
    return (long) line;
}

/* --------------------------------------------------------------------------------------------- */

/* The version of the line of the cursor of History; NULL when there is none, or History has not
   the focus and @focused */
static const char *
query_history_current (query_t *q, gboolean focused)
{
    const long line = query_history_cursor (q, focused);

    if (line < 0 || line >= (long) q->rows->len)
        return NULL;
    return g_ptr_array_index (q->rows, line);
}

/* --------------------------------------------------------------------------------------------- */

/* The last entry of the history of the version @version: a run, or the text kept as it when
   @runs is FALSE; NULL when there is none.  @count: the runs of it. */
static const query_entry_t *
query_history_last (const query_t *q, const char *version, gboolean runs, guint *count)
{
    const query_entry_t *last = NULL;
    guint i;

    if (count != NULL)
        *count = 0;
    for (i = 0; i < q->history->len; i++)
    {
        const query_entry_t *e = g_ptr_array_index (q->history, i);

        if (g_strcmp0 (e->version, version) != 0 || (runs && e->status == QUERY_SAVED))
            continue;
        last = e;
        if (count != NULL && e->status != QUERY_SAVED)
            (*count)++;
    }
    return last;
}

/* --------------------------------------------------------------------------------------------- */

/* The first line of @text that tells it: not "// @table", "// @limit", use () or empty; @more
   whether other lines follow it */
static char *
query_summary (const char *text, gboolean *more)
{
    const char *start = text, *nl;

    while ((nl = strchr (start, '\n')) != NULL)
    {
        char *line = g_strndup (start, (gsize) (nl - start));
        const gboolean mark = query_wants_table (line) || query_wants_limit (line) > 0
            || query_has_use (line) || query_blank (line);

        g_free (line);
        if (!mark)
            break;
        start = nl + 1;
    }
    // more lines, not empty ones alone
    *more = nl != NULL && nl[strspn (nl, " \t\r\n")] != '\0';
    return g_strndup (start, nl != NULL ? (gsize) (nl - start) : strlen (start));
}

/* --------------------------------------------------------------------------------------------- */

/* The versions of the file of the history, the newest first, a line each: their time, the runs
   of each and how the last one ended, the lines put and taken away since the version before,
   the first line of the query of the last run.  The cursor goes to the line of @current, else to
   the first one; the title of the tab names the file. */
static void
query_history_show (query_t *q, const char *current)
{
    GString *text = g_string_new (NULL);
    char *dir = q->history_file != NULL ? query_versions_dir_of (q->history_file) : NULL;
    char **names = dir != NULL ? query_version_names (dir) : g_new0 (char *, 1);
    const guint n = g_strv_length (names);
    GPtrArray *lines = g_ptr_array_new_with_free_func (g_free);
    char *before = NULL, *title, *base;
    long cursor = 0;
    guint i;

    // @current may be a line of the list it makes again
    char *here = g_strdup (current);

    g_ptr_array_set_size (q->rows, 0);
    // the oldest first, each against the one before; shown the other way
    for (i = 0; i < n; i++)
    {
        char *path = g_build_filename (dir, names[i], (char *) NULL);
        char *now = NULL, *label = query_version_label (names[i]), *changes, *summary, *runs;
        char *status;
        guint count = 0;
        const query_entry_t *last = query_history_last (q, names[i], FALSE, &count);
        gboolean more = FALSE, failed;
        int added = 0, removed = 0;

        (void) g_file_get_contents (path, &now, NULL, NULL);
        query_line_changes (before, now, &added, &removed);
        changes = added + removed == 0 ? g_strdup ("")
            : removed == 0             ? g_strdup_printf ("+%d", added)
            : added == 0               ? g_strdup_printf ("-%d", removed)
                                       : g_strdup_printf ("+%d -%d", added, removed);
        summary = query_summary (last != NULL ? last->text : now != NULL ? now : "", &more);
        runs = count > 0   ? g_strdup_printf (ngettext ("%u run", "%u runs", count), count)
            : last != NULL ? g_strdup (_ ("current"))
                           : g_strdup ("");
        last = query_history_last (q, names[i], TRUE, NULL);
        failed = last != NULL && last->status != 0;
        status = last == NULL       ? g_strdup ("")
            : last->status < 0      ? g_strdup (_ ("stopped"))
            : last->summary != NULL ? g_strdup (last->summary)
                                    : g_strdup_printf (_ ("exit %d"), last->status);
        g_ptr_array_add (lines,
                         g_strdup_printf ("%s  %-8s  %-10s  %-9s  %s%s%s%s\n", label, runs, status,
                                          changes, failed ? ANSI_RED : "", summary,
                                          more ? " ..." : "", failed ? ANSI_RESET : ""));
        g_free (status);
        g_free (runs);
        g_free (summary);
        g_free (changes);
        g_free (label);
        g_free (path);
        g_free (before);
        before = now;
    }
    g_free (before);

    if (q->history_file == NULL)
        g_string_append (text, _ ("No file of queries in front.\n"));
    else if (n == 0)
        g_string_append (text, _ ("No version yet: a run keeps the file as it is.\n"));
    for (i = n; i > 0; i--)
    {
        if (g_strcmp0 (names[i - 1], here) == 0)
            cursor = (long) q->rows->len;
        g_string_append (text, g_ptr_array_index (lines, i - 1));
        g_ptr_array_add (q->rows, g_strdup (names[i - 1]));
    }
    g_ptr_array_free (lines, TRUE);
    g_strfreev (names);
    g_free (dir);
    g_free (here);

    query_tab_set (q, QUERY_TAB_HISTORY, text->str, FALSE);
    g_string_free (text, TRUE);

    if (q->tabs[QUERY_TAB_HISTORY] != 0)
    {
        GVariantDict dict;
        GVariant *reply;

        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "id", "x", q->tabs[QUERY_TAB_HISTORY]);
        g_variant_dict_insert (&dict, "line", "x", (gint64) cursor);
        reply = query_viewer (q, "move_to", &dict);
        if (reply != NULL)
            g_variant_unref (reply);
    }
    // the file whose versions they are, in the title of the tab
    base = q->history_file != NULL ? g_path_get_basename (q->history_file) : NULL;
    title = base != NULL ? g_strdup_printf (_ ("History: %s"), base) : g_strdup (_ ("History"));
    query_tab_title (q, QUERY_TAB_HISTORY, title);
    g_free (title);
    g_free (base);
}

/* --------------------------------------------------------------------------------------------- */

/* The history kept: a line a query, its fields between tabs, the text escaped */
static void
query_history_save (query_t *q)
{
    GString *data;
    char *dir;
    guint i;

    if (q->history_path == NULL)
        return;
    data = g_string_new (NULL);
    for (i = 0; i < q->history->len; i++)
    {
        const query_entry_t *e = g_ptr_array_index (q->history, i);
        char *text = g_strescape (e->text, NULL);
        char seconds[G_ASCII_DTOSTR_BUF_SIZE];

        char *use = e->use != NULL ? g_strescape (e->use, NULL) : g_strdup ("-");
        char *summary = e->summary != NULL ? g_strescape (e->summary, NULL) : g_strdup ("-");

        // the version, the use () and the summary before the text, which is the last; "-" none
        g_string_append_printf (data, "%s\t%s\t%d\t%ld\t%s\t%s\t%s\t%s\n", e->when,
                                g_ascii_dtostr (seconds, sizeof (seconds), e->seconds), e->status,
                                e->line, e->version != NULL ? e->version : "-", use, summary, text);
        g_free (summary);
        g_free (use);
        g_free (text);
    }
    dir = g_path_get_dirname (q->history_path);
    // the directory the user's alone: the queries may name what is private
    if (g_mkdir_with_parents (dir, 0700) == 0)
        (void) g_file_set_contents (q->history_path, data->str, (gssize) data->len, NULL);
    g_free (dir);
    g_string_free (data, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_history_load (query_t *q, const char *file)
{
    char *data = NULL;
    char **lines;
    guint i;

    g_free (q->history_path);
    q->history_path = file != NULL ? query_history_path_of (file) : NULL;
    g_free (q->history_file);
    q->history_file = g_strdup (file);
    g_ptr_array_set_size (q->history, 0);
    if (q->history_path == NULL || !g_file_get_contents (q->history_path, &data, NULL, NULL))
        return;
    lines = g_strsplit (data, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        char **f = g_strsplit (lines[i], "\t", 8);
        const guint n = g_strv_length (f);

        // five fields, of before the versions: no version; six, no use (); seven, no summary
        if (n >= 5 && n <= 8)
        {
            query_entry_t *e = g_new0 (query_entry_t, 1);

            e->when = g_strdup (f[0]);
            e->seconds = g_ascii_strtod (f[1], NULL);
            e->status = (int) g_ascii_strtoll (f[2], NULL, 10);
            e->line = (long) g_ascii_strtoll (f[3], NULL, 10);
            if (n >= 6 && strcmp (f[4], "-") != 0 && strchr (f[4], G_DIR_SEPARATOR) == NULL)
                e->version = g_strdup (f[4]);
            if (n >= 7 && strcmp (f[5], "-") != 0)
                e->use = g_strcompress (f[5]);
            if (n == 8 && strcmp (f[6], "-") != 0)
                e->summary = g_strcompress (f[6]);
            e->text = g_strcompress (f[n - 1]);
            g_ptr_array_add (q->history, e);
        }
        g_strfreev (f);
    }
    g_strfreev (lines);
    g_free (data);
    if (q->history->len > QUERY_HISTORY_MAX)
        g_ptr_array_remove_range (q->history, 0, q->history->len - QUERY_HISTORY_MAX);
    query_current_one (q);
}

/* --------------------------------------------------------------------------------------------- */

void
query_history_add (query_t *q, const char *when, double seconds, int status, const char *summary)
{
    query_entry_t *e = g_new0 (query_entry_t, 1);

    e->when = g_strdup (when);
    e->seconds = seconds;
    e->status = status;
    e->line = q->first;
    e->text = g_strdup (q->text);
    e->version = g_strdup (q->version);
    e->use = g_strdup (q->use);
    e->summary = g_strdup (summary);
    // another file came to the front while it ran: the run goes to the history of its own
    if (q->file != NULL && g_strcmp0 (q->file, q->history_file) != 0)
    {
        GPtrArray *front = q->history;
        char *front_path = q->history_path, *front_file = q->history_file;

        q->history = g_ptr_array_new_with_free_func (query_entry_free);
        q->history_path = NULL;
        q->history_file = NULL;
        query_history_load (q, q->file);
        g_ptr_array_add (q->history, e);
        if (q->history->len > QUERY_HISTORY_MAX)
            g_ptr_array_remove_index (q->history, 0);
        query_history_save (q);
        g_ptr_array_free (q->history, TRUE);
        g_free (q->history_path);
        g_free (q->history_file);
        q->history = front;
        q->history_path = front_path;
        q->history_file = front_file;
        return;
    }
    g_ptr_array_add (q->history, e);
    if (q->history->len > QUERY_HISTORY_MAX)
        g_ptr_array_remove_index (q->history, 0);
    query_history_save (q);
    query_history_show (q, e->version);
}

/* --------------------------------------------------------------------------------------------- */
/* The run */
/* --------------------------------------------------------------------------------------------- */

void
query_table_say (query_t *q, const char *text)
{
    query_tab_set (q, QUERY_TAB_TABLE, text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The start of the head of the output of a run */
void
query_output_head (query_t *q, GString *text, double seconds, const char *when)
{
    const size_t len = strlen (q->text);
    // the newline the last line ends with starts no line of the query
    const long last =
        q->first + query_line_of (q->text, len > 0 && q->text[len - 1] == '\n' ? len - 1 : len);

    g_string_append (text, ANSI_BOLD);
    // a query of no file is told itself, on one line, cut: Table has all of it
    if (query_of_no_file (q))
    {
        char *query = g_strstrip (g_strdup (q->text));
        char *c;

        for (c = query; *c != '\0'; c++)
            if (*c == '\n' || *c == '\t')
                *c = ' ';
        if (g_utf8_strlen (query, -1) > QUERY_HEAD_QUERY)
            g_string_append_printf (
                text, "%.*s...", (int) (g_utf8_offset_to_pointer (query, QUERY_HEAD_QUERY) - query),
                query);
        else
            g_string_append (text, query);
        g_free (query);
    }
    else if (last > q->first)
        g_string_append_printf (text, _ ("lines %ld-%ld"), q->first, last);
    else
        g_string_append_printf (text, _ ("line %ld"), q->first);
    g_string_append_printf (text, "   %.2f s   %s   %s   ", seconds, when, q->endpoint);
}

/* --------------------------------------------------------------------------------------------- */

/* The error marked in the file of the query and gone to; not in a query not of the file as it is
   now, run again from History */
void
query_mark_error (query_t *q, long line, long column)
{
    void *edit;

    if (q->file == NULL)
        return;
    edit = q->host->window_top_file (q->host);
    // the first line of a selection from the middle of a line starts there
    if (line == 1)
        column += q->first_column;
    line += q->first - 1;
    q->host->set_marker (q->host, q->file, line, q->mark, TRUE);
    if (edit != NULL)
        (void) q->host->go_to (q->host, edit, q->file, line, column);
}

/* --------------------------------------------------------------------------------------------- */

/* Stop the query: what it printed until then is shown */
static void
query_stop (query_t *q)
{
    q->backend->stop (q->backend_data);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_blank (const char *text)
{
    for (; *text != '\0'; text++)
        if (!g_ascii_isspace (*text))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The query @block as the menu of the file wants it run: explained, its rows counted; NULL, and
   why said, when this one cannot be */
static char *
query_as (query_t *q, const char *block, query_as_t as)
{
    const gboolean pg = strcmp (q->backend->id, "postgres") == 0;
    char *changed = NULL;

    if (as == QUERY_AS_EXPLAIN)
    {
        changed = pg ? query_pg_explain (block) : query_mongo_explain (block);
        if (changed == NULL)
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              pg ? _ ("The query has nothing but comments.")
                                 : _ ("Only a query that ends with find () or aggregate ()\n"
                                      "is explained."));
    }
    else if (as == QUERY_AS_COUNT)
    {
        gboolean many = FALSE;

        changed = pg ? query_pg_count (block, &many) : NULL;
        if (changed == NULL)
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              many ? _ ("The rows of one query alone are counted:\n"
                                        "select it, or part the queries by lines \"// ---\".")
                                   : _ ("Only the rows of a SELECT, VALUES, TABLE or WITH\n"
                                        "are counted."));
    }
    return changed;
}

/* --------------------------------------------------------------------------------------------- */

/* Run the selection of @edit, else the section of its cursor between lines "// ---", else all of
   it; all of it when @all; its rows written to a file asked for when @export; made another first
   as @as says */
static void
query_run_as (query_t *q, void *edit, gboolean all, gboolean export, query_as_t as)
{
    WEdit *e;
    char *text, *block = NULL, *use = NULL, *file, *version;
    gsize len = 0;
    long first = 0, column = 0;
    off_t start, end;
    gboolean selected = FALSE, sectioned = FALSE;

    if (q->backend->running (q->backend_data))
    {
        if (query_dialog (_ ("Query"), _ ("A query is running. Stop it?"), D_NORMAL, 2, _ ("&Stop"),
                          _ ("&Cancel"))
            == 0)
            query_stop (q);
        return;
    }
    if (edit == NULL)
        edit = q->host->window_top_file (q->host);
    if (edit == NULL)
        return;
    e = EDIT (edit);
    text = q->host->get_text (q->host, edit, &len);
    if (text == NULL)
        return;
    if (all)
        block = g_strdup (text);
    else if (eval_marks (e, &start, &end) && end > start && (gsize) end <= len)
    {
        const char *bol = text + start;

        block = g_strndup (text + start, (gsize) (end - start));
        first = query_line_of (text, (gsize) start);
        // where it starts on its line: the column of an error of its first line is after it
        while (bol > text && bol[-1] != '\n')
            bol--;
        column = g_utf8_strlen (bol, (gssize) (text + start - bol));
        selected = TRUE;
        // a mark F3 began ends where it is: the selection run stays, the cursor drags it no more
        if (e->mark2 < 0)
            edit_mark_cmd (e, FALSE);
    }
    else
    {
        // a script is run as a whole, its lines using what those before made; "// ---" parts it
        block = q->backend->sections ? query_section (text, e->buffer.curs_line, &first) : NULL;
        sectioned = block != NULL;
        if (block == NULL)
            block = g_strdup (text);
    }
    if (query_blank (block))
    {
        q->host->message (q->host, D_NORMAL, _ ("Query"),
                          selected        ? _ ("The selection is empty.")
                              : sectioned ? _ ("The part of the file between the lines \"// ---\"\n"
                                               "around the cursor is empty.")
                                          : _ ("The file is empty."));
        g_free (block);
        g_free (text);
        return;
    }
    if (as != QUERY_AS_IS)
    {
        char *changed = query_as (q, block, as);

        g_free (block);
        if (changed == NULL)
        {
            g_free (text);
            return;
        }
        block = changed;
    }
    file = q->host->get_current_file (q->host, edit);
    if (export)
    {
        char *base = file != NULL ? g_path_get_basename (file) : g_strdup ("query");
        char *dot = strrchr (base, '.');

        if (dot != NULL && dot != base)
            *dot = '\0';
        q->export = query_export_ask (q, base, NULL);
        g_free (base);
        if (q->export == NULL)
        {
            g_free (file);
            g_free (block);
            g_free (text);
            return;
        }
    }
    // a part run alone is in the database of the use ("name") above it
    if (first > 0 && q->backend->context != NULL)
        use = q->backend->context (text, first);
    // the file as it is run, unless it is that of the run before; nothing run, no version
    version = file != NULL && !query_in_versions (file) ? query_version_save (file, text) : NULL;
    g_free (text);
    q->first_column = column;
    query_run_text (q, block, file, first + 1, use, version);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_run (query_t *q, void *edit, gboolean all, gboolean export)
{
    query_run_as (q, edit, all, export, QUERY_AS_IS);
}

/* --------------------------------------------------------------------------------------------- */

/* Run @text, from the line @first, from 1, of @file; NULL for a query not of the file as it is
   now, whose errors are not marked in it; in the database of @use, the string of a use () above
   it, NULL for none; @version the version of the file it is of, NULL for none.  All are taken. */
static void
query_run_text (query_t *q, char *text, char *file, long first, char *use, char *version)
{
    g_free (q->version);
    q->version = version;
    g_free (q->use);
    q->use = use;
    g_free (q->text);
    q->text = text;
    // the history of the file the query is of, when another than that of the last one
    if (file != NULL && g_strcmp0 (file, q->history_file) != 0)
        query_history_load (q, file);
    g_free (q->file);
    q->file = file;
    q->first = first;
    q->stopped = FALSE;
    q->started = g_get_monotonic_time ();
    q->host->clear_markers (q->host, NULL, q->mark);
    g_free (q->run_line);
    q->run_line = q->export != NULL
        ? g_strdup_printf (_ ("Running, the rows written to %s...\n"), q->export->path)
        : g_strdup (_ ("Running...\n"));
    query_running (q, NULL);
    query_table_say (q, _ ("Running...\n"));
    q->backend->start (q->backend_data);
    // a backend that could not start it: no file
    if (q->export != NULL && !q->backend->running (q->backend_data))
        g_free (query_export_close (q, FALSE, NULL));
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

void
query_run_alone (query_t *q, const char *text)
{
    // a query of no file, of no line: nothing of it is marked in one
    q->first_column = 0;
    query_run_text (q, g_strdup (text), NULL, 0, NULL, NULL);
}

/* --------------------------------------------------------------------------------------------- */
/* QueryExport */
/* --------------------------------------------------------------------------------------------- */

static void
query_export_run_free (query_export_run_t *x)
{
    query_export_free (x->writer);
    g_free (x->path);
    g_free (x->tmp);
    g_free (x->error);
    g_free (x);
}

/* --------------------------------------------------------------------------------------------- */

/* The file of the export opened, a file of its own next to the file asked for, made new: one of
   that name there, of another export or of the user, is left as it is, "name.2.part" taken;
   FALSE with @error when it cannot be */
static gboolean
query_export_open (query_export_run_t *x, char **error)
{
    int i;

    for (i = 1; i <= QUERY_EXPORT_PARTS; i++)
    {
        g_free (x->tmp);
        x->tmp = i == 1 ? g_strconcat (x->path, ".part", (char *) NULL)
                        : g_strdup_printf ("%s.%d.part", x->path, i);
        x->fd = g_open (x->tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (x->fd >= 0)
            return TRUE;
        if (errno != EEXIST)
            break;
    }
    *error = g_strdup_printf ("%s: %s", x->tmp, g_strerror (errno));
    // no file of its own to remove: that of the name is not its
    g_clear_pointer (&x->tmp, g_free);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The options of the export of the user, the last ones given to the dialog */
static query_export_options_t *
query_export_options (query_t *q)
{
    if (q->export_options == NULL)
    {
        query_export_options_t *o = g_new0 (query_export_options_t, 1);

        o->format = QUERY_EXPORT_CSV;
        o->delimiter = ',';
        o->header = TRUE;
        o->per_insert = QUERY_EXPORT_PER_INSERT;
        o->create_table = TRUE;
        q->export_options = o;
    }
    return q->export_options;
}

/* --------------------------------------------------------------------------------------------- */

/* The delimiter of CSV written @text: a character of ASCII, a quote and the end of a line not one;
   "\\t" or "Tab" a tab.  FALSE when it is none. */
static gboolean
query_export_delimiter (const char *text, char *c)
{
    if (strcmp (text, "\\t") == 0 || g_ascii_strcasecmp (text, "tab") == 0)
    {
        *c = '\t';
        return TRUE;
    }
    if (text[0] == '\0' || text[1] != '\0' || !g_ascii_isprint (text[0]) || text[0] == '"')
        return FALSE;
    *c = text[0];
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The dialog of the export made: the line of the options of the format has its title at its left,
   as a groupbox has, the line going from one side of the dialog to the other */
static cb_ret_t
query_export_dialog_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_INIT)
    {
        Widget *line = widget_find_by_id (w, query_export_options_line);

        if (line != NULL)
        {
            char *title = g_strdup_printf (" %s ", _ ("Options"));

            hline_set_text (HLINE (line), title);
            HLINE (line)->text_align = J_LEFT;
            g_free (title);
        }
    }
    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

/* A page of the dialog of the export, that of the format @f->format, the buttons at its top going
   to those of the others: what it was given put in @f unless it is canceled; B_ENTER, B_CANCEL
   or the page of a format */
static int
query_export_page (query_export_form_t *f)
{
    static const char *const tab_names[QUERY_EXPORT_FORMATS] = {
        N_ ("C&SV"), N_ ("&JSON"), N_ ("&NDJSON"), N_ ("S&QL"), N_ ("&Markdown"),
    };
    char *path = NULL, *table = NULL, *per = NULL;
    char *delimiter = NULL;
    int ret, i;
    gboolean header = f->header, quote_all = f->quote_all, null_word = f->null_word;
    gboolean indent = f->indent, create_table = f->create_table, null_markdown = f->null_markdown;
    GArray *widgets = g_array_new (FALSE, FALSE, sizeof (quick_widget_t));

    {
        quick_widget_t top[] = {
            QUICK_TOP_BUTTONS (FALSE, TRUE),
        };
        quick_widget_t head[] = {
            QUICK_LABELED_INPUT (_ ("File:"), input_label_above, f->path, "query-export-file",
                                 &path, NULL, FALSE, FALSE, INPUT_COMPLETE_FILENAMES),
            // the line of the options of the format: its title put in by the callback
            QUICK_SEPARATOR (TRUE),
        };
        quick_widget_t csv[] = {
            QUICK_LABELED_INPUT (_ ("Delimiter:"), input_label_left, f->delimiter,
                                 "query-export-delimiter", &delimiter, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_CHECKBOX (_ ("The names of the columns on the first &line"), &header, NULL),
            QUICK_CHECKBOX (_ ("Every value in quot&es, else those that need them"), &quote_all,
                            NULL),
            QUICK_CHECKBOX (_ ("NULL as the word N&ULL, else nothing"), &null_word, NULL),
        };
        quick_widget_t json[] = {
            QUICK_LABEL (_ ("An array of objects, a row an object."), NULL),
            QUICK_CHECKBOX (_ ("&Indented: a key a line"), &indent, NULL),
        };
        quick_widget_t ndjson[] = {
            QUICK_LABEL (_ ("An object a line, for jq, mongoimport and the like:\n"
                            "nothing to choose."),
                         NULL),
        };
        quick_widget_t sql[] = {
            QUICK_LABELED_INPUT (_ ("Table of the INSERT statements:"), input_label_above, f->table,
                                 "query-export-table", &table, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_CHECKBOX (_ ("CREATE &TABLE first, of the types of the columns"), &create_table,
                            NULL),
            QUICK_LABELED_INPUT (_ ("Rows a statement:"), input_label_left, f->per,
                                 "query-export-rows", &per, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
        };
        quick_widget_t markdown[] = {
            QUICK_LABEL (_ ("A table, each cell on one line."), NULL),
            QUICK_CHECKBOX (_ ("NULL as the word N&ULL, else nothing"), &null_markdown, NULL),
        };
        quick_widget_t end[] = {
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
        };
        // the height of the highest page, that of CSV: the dialog does not jump from one to another
        WRect r = { -1, -1, 16, MIN (64, COLS - 4) };
        quick_dialog_t qdlg = {
            .rect = r,
            .title = _ ("Export the rows of the query"),
            .help = QUERY_HELP_NODE,
            .help_file = QUERY_HELP_FILE,
            .widgets = NULL,
            .callback = query_export_dialog_cb,
            .mouse_callback = NULL,
        };

        head[1].id = &query_export_options_line;
        g_array_append_vals (widgets, top, G_N_ELEMENTS (top));
        for (i = 0; i < f->count; i++)
        {
            quick_widget_t tab[] = {
                QUICK_BUTTON (_ (tab_names[f->formats[i]]), QUERY_PAGE (f->formats[i]), NULL, NULL),
            };

            g_array_append_vals (widgets, tab, 1);
        }
        g_array_append_vals (widgets, head, G_N_ELEMENTS (head));
        switch (f->format)
        {
        case QUERY_EXPORT_CSV:
            g_array_append_vals (widgets, csv, G_N_ELEMENTS (csv));
            break;
        case QUERY_EXPORT_JSON:
            g_array_append_vals (widgets, json, G_N_ELEMENTS (json));
            break;
        case QUERY_EXPORT_NDJSON:
            g_array_append_vals (widgets, ndjson, G_N_ELEMENTS (ndjson));
            break;
        case QUERY_EXPORT_SQL:
            g_array_append_vals (widgets, sql, G_N_ELEMENTS (sql));
            break;
        default:
            g_array_append_vals (widgets, markdown, G_N_ELEMENTS (markdown));
            break;
        }
        g_array_append_vals (widgets, end, G_N_ELEMENTS (end));
        qdlg.widgets = (quick_widget_t *) widgets->data;
        // the cursor on the button of the page seen, as the dialog of FTP has it: that one shows
        // which it is
        for (i = 0; i < f->count && f->formats[i] != f->format; i++)
            ;
        ret = quick_dialog_skip (&qdlg, i + 1);
        g_array_free (widgets, TRUE);
    }

    if (ret == B_CANCEL)
    {
        g_free (path);
        g_free (table);
        g_free (per);
        g_free (delimiter);
        return ret;
    }
    if (delimiter != NULL)
    {
        g_free (f->delimiter);
        f->delimiter = delimiter;
    }
    f->header = header;
    f->quote_all = quote_all;
    f->null_word = null_word;
    f->indent = indent;
    f->create_table = create_table;
    f->null_markdown = null_markdown;
    if (path != NULL)
    {
        g_free (f->path);
        f->path = g_strstrip (path);
    }
    if (table != NULL)
    {
        g_free (f->table);
        f->table = g_strstrip (table);
    }
    if (per != NULL)
    {
        g_free (f->per);
        f->per = per;
    }
    return ret;
}

/* --------------------------------------------------------------------------------------------- */

/* The page of the format @format seen: the name proposed, left as it was, takes its extension */
static void
query_export_form_turn (query_export_form_t *f, query_export_format_t format)
{
    char *was = g_strconcat (f->start, query_export_extension (f->format), (char *) NULL);

    if (strcmp (f->path, was) == 0)
    {
        g_free (f->path);
        f->path = g_strconcat (f->start, query_export_extension (format), (char *) NULL);
    }
    g_free (was);
    f->format = format;
}

/* --------------------------------------------------------------------------------------------- */

/* The file and the format of QueryExport asked for, @base the start of the name of the file, the
   time after it; @table the name INSERT is given, NULL for that of the dialog before, the
   relation of Schema whose rows are exported.  NULL when the user cancels, or the file cannot be
   made. */
static query_export_run_t *
query_export_ask (query_t *q, const char *base, const char *table)
{
    query_export_options_t *o = query_export_options (q);
    query_export_form_t f = { 0 };
    query_export_options_t given;
    GDateTime *now = g_date_time_new_now_local ();
    char *dir = g_get_current_dir (), *stamp, *name, *error = NULL;
    query_export_run_t *x;
    char delimiter = o->delimiter;
    int ret, i;

    // the pages: INSERT of the rows of a database of tables of SQL alone
    for (i = 0; i < QUERY_EXPORT_FORMATS; i++)
        if (i != QUERY_EXPORT_SQL || q->backend->export_sql)
            f.formats[f.count++] = (query_export_format_t) i;
    f.format =
        o->format == QUERY_EXPORT_SQL && !q->backend->export_sql ? QUERY_EXPORT_CSV : o->format;
    f.delimiter = o->delimiter == '\t' ? g_strdup ("\\t") : g_strdup_printf ("%c", o->delimiter);
    f.header = o->header;
    f.quote_all = o->quote_all;
    f.null_word = o->null_word;
    f.indent = o->indent;
    f.null_markdown = o->null_markdown;
    f.table = g_strdup (table != NULL ? table : o->table != NULL ? o->table : "export");
    f.per = g_strdup_printf ("%d", o->per_insert);
    // the rows of a relation of Schema go to its table, which is there; those of a query to a
    // new one
    f.create_table = table == NULL && o->create_table;

    // "orders-20261108-101500.csv" in the directory of coole
    stamp = g_date_time_format (now, "%Y%m%d-%H%M%S");
    g_date_time_unref (now);
    name = g_strdup_printf ("%s-%s.", base, stamp);
    f.start = g_build_filename (dir, name, (char *) NULL);
    g_free (name);
    g_free (stamp);
    g_free (dir);
    f.path = g_strconcat (f.start, query_export_extension (f.format), (char *) NULL);

    // a page after another, what each was given kept, until OK or Cancel; a delimiter of CSV
    // that is none goes back to its page
    while (TRUE)
    {
        ret = query_export_page (&f);
        if (ret >= QUERY_PAGE (0) && ret < QUERY_PAGE (QUERY_EXPORT_FORMATS))
            query_export_form_turn (&f, (query_export_format_t) (ret - QUERY_PAGE (0)));
        else if (ret != B_CANCEL && f.format == QUERY_EXPORT_CSV
                 && !query_export_delimiter (f.delimiter, &delimiter))
            q->host->message (q->host, D_ERROR, _ ("Export"),
                              _ ("The delimiter is one character, not a quote, or \\t for a "
                                 "tab."));
        else
            break;
    }

    g_free (f.delimiter);
    if (ret == B_CANCEL || *f.path == '\0')
    {
        g_free (f.path);
        g_free (f.start);
        g_free (f.table);
        g_free (f.per);
        return NULL;
    }

    o->format = f.format;
    if (f.format == QUERY_EXPORT_CSV)
        o->delimiter = delimiter;
    o->header = f.header;
    o->quote_all = f.quote_all;
    o->null_word = f.null_word;
    o->indent = f.indent;
    o->null_markdown = f.null_markdown;
    o->per_insert = MAX (1, atoi (f.per));
    // the table and CREATE TABLE of Schema are those of its relation, not kept for the next one
    if (table == NULL)
    {
        g_free (o->table);
        o->table = g_strdup (f.table);
        o->create_table = f.create_table;
    }
    g_free (f.per);
    g_free (f.start);

    // "~/exports/a.csv" in the home directory
    {
        char *home = tilde_expand (f.path);

        g_free (f.path);
        f.path = home;
    }
    if (!g_path_is_absolute (f.path))
    {
        char *cwd = g_get_current_dir ();
        char *full = g_build_filename (cwd, f.path, (char *) NULL);

        g_free (cwd);
        g_free (f.path);
        f.path = full;
    }

    if (g_file_test (f.path, G_FILE_TEST_EXISTS))
    {
        char *text = g_strdup_printf (_ ("The file %s is there. Write over it?"), f.path);
        const int answer = query_dialog (_ ("Export"), text, D_NORMAL, 2, _ ("&Yes"), _ ("&No"));

        g_free (text);
        if (answer != 0)
        {
            g_free (f.path);
            g_free (f.table);
            return NULL;
        }
    }

    given = *o;
    given.table = f.table;
    given.create_table = f.create_table;
    x = g_new0 (query_export_run_t, 1);
    x->writer = query_export_new (&given);
    x->path = f.path;
    x->fd = -1;
    g_free (f.table);
    if (!query_export_open (x, &error))
    {
        q->host->message (q->host, D_ERROR, _ ("Export"), error);
        g_free (error);
        query_export_run_free (x);
        return NULL;
    }
    return x;
}

/* --------------------------------------------------------------------------------------------- */

void
query_export_alone (query_t *q, const char *text, const char *name, const char *table)
{
    if (q->backend->running (q->backend_data))
    {
        query_run (q, NULL, FALSE, FALSE);
        return;
    }
    q->export = query_export_ask (q, name, table);
    if (q->export != NULL)
        query_run_alone (q, text);
}

/* --------------------------------------------------------------------------------------------- */

query_export_t *
query_export_writer (query_t *q)
{
    return q->export != NULL && q->export->error == NULL ? q->export->writer : NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_export_flush (query_t *q, gboolean all)
{
    query_export_run_t *x = q->export;
    GString *out;
    gsize done = 0;

    if (x == NULL)
        return TRUE;
    if (x->error != NULL)
        return FALSE;
    out = query_export_text (x->writer);
    if (!all && out->len < QUERY_EXPORT_CHUNK)
        return TRUE;
    while (done < out->len)
    {
        const ssize_t n = write (x->fd, out->str + done, out->len - done);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            x->error = g_strdup_printf ("%s: %s", x->tmp, g_strerror (n < 0 ? errno : ENOSPC));
            g_string_truncate (out, 0);
            return FALSE;
        }
        done += (gsize) n;
    }
    x->bytes += (gint64) out->len;
    g_string_truncate (out, 0);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_export_close (query_t *q, gboolean ok, const char *why)
{
    query_export_run_t *x = q->export;
    char *line;

    if (x == NULL)
        return NULL;
    if (ok && x->error == NULL)
    {
        query_export_end (x->writer);
        (void) query_export_flush (q, TRUE);
    }
    if (x->fd >= 0 && close (x->fd) != 0 && x->error == NULL)
        x->error = g_strdup_printf ("%s: %s", x->tmp, g_strerror (errno));
    x->fd = -1;
    ok = ok && x->error == NULL;
    if (ok && g_rename (x->tmp, x->path) != 0)
    {
        x->error = g_strdup_printf ("%s: %s", x->path, g_strerror (errno));
        ok = FALSE;
    }
    if (!ok)
        (void) g_unlink (x->tmp);
    q->export = NULL;

    if (ok)
    {
        const gint64 rows = query_export_rows (x->writer);
        char *size = g_format_size ((guint64) x->bytes);
        char *skipped = query_export_skipped (x->writer);
        GString *text = g_string_new (ANSI_GREEN);

        g_string_append_printf (text,
                                ngettext ("%" PRId64 " row written to %s, %s.",
                                          "%" PRId64 " rows written to %s, %s.", (gulong) rows),
                                (int64_t) rows, x->path, size);
        g_string_append (text, ANSI_RESET "\n");
        if (skipped != NULL)
            g_string_append_printf (text,
                                    _ ("Keys the first %d documents have not, no columns of "
                                       "the file, are left out: %s\n"),
                                    QUERY_EXPORT_SAMPLE, skipped);
        g_free (skipped);
        g_free (size);
        line = g_string_free (text, FALSE);
    }
    else
        line = g_strdup_printf (ANSI_RED "%s: %s" ANSI_RESET "\n", _ ("No file is written"),
                                x->error != NULL  ? x->error
                                    : why != NULL ? why
                                    : q->stopped  ? _ ("the query was stopped")
                                                  : _ ("the query failed"));
    query_export_run_free (x);
    return line;
}

/* --------------------------------------------------------------------------------------------- */
/* What History does */
/* --------------------------------------------------------------------------------------------- */

/* F5 in History: the query of the last run of the version run again as it was, the file left as
   it is now */
static void
query_history_run (query_t *q, const char *version)
{
    const query_entry_t *e = query_history_last (q, version, TRUE, NULL);

    if (q->backend->running (q->backend_data))
    {
        query_run (q, NULL, FALSE, FALSE);
        return;
    }
    if (e == NULL)
    {
        query_tab_set (q, QUERY_TAB_OUTPUT,
                       _ ("The version was kept, not run: Enter puts it in "
                          "the file, to run it there.\n"),
                       TRUE);
        return;
    }
    // in the database it ran in: the use () above a part run alone
    q->first_column = 0;
    query_run_text (q, g_strdup (e->text), NULL, e->line, g_strdup (e->use), g_strdup (e->version));
}

/* --------------------------------------------------------------------------------------------- */

/* The path of the version @name of the file of the history, NULL when there is none */
static char *
query_version_path (const query_t *q, const char *name)
{
    char *dir, *path;

    if (name == NULL || q->history_file == NULL)
        return NULL;
    dir = query_versions_dir_of (q->history_file);
    path = g_build_filename (dir, name, (char *) NULL);
    g_free (dir);
    if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
    {
        g_free (path);
        return NULL;
    }
    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* The version of @file whose text is @text, any of them; NULL when there is none */
static char *
query_version_of_text (const char *file, const char *text)
{
    char *dir = query_versions_dir_of (file);
    char **names = query_version_names (dir);
    char *found = NULL;
    guint i;

    // the newest first: the one gone to last is likely
    for (i = g_strv_length (names); found == NULL && i > 0; i--)
    {
        char *path = g_build_filename (dir, names[i - 1], (char *) NULL);
        char *version = NULL;

        if (g_file_get_contents (path, &version, NULL, NULL) && strcmp (version, text) == 0)
            found = g_strdup (names[i - 1]);
        g_free (version);
        g_free (path);
    }
    g_strfreev (names);
    g_free (dir);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

/* The texts kept as "current" and never run taken away, their versions and their entries, but
   those of the versions @keep and @also, NULL for none: one "current" stays */
static void
query_current_drop (query_t *q, const char *keep, const char *also)
{
    guint i;

    for (i = q->history->len; i > 0; i--)
    {
        const query_entry_t *e = g_ptr_array_index (q->history, i - 1);

        if (e->status != QUERY_SAVED || e->version == NULL || g_strcmp0 (e->version, keep) == 0
            || g_strcmp0 (e->version, also) == 0
            || query_history_last (q, e->version, TRUE, NULL) != NULL)
            continue;
        {
            char *path = query_version_path (q, e->version);

            if (path != NULL)
                (void) unlink (path);
            g_free (path);
        }
        g_ptr_array_remove_index (q->history, i - 1);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* One "current" of the history: the newest of those never run, the others taken away, as an
   older coole left them, one at each going from version to version */
static void
query_current_one (query_t *q)
{
    const char *newest = NULL;
    guint i;

    for (i = 0; i < q->history->len; i++)
    {
        const query_entry_t *e = g_ptr_array_index (q->history, i);

        if (e->status == QUERY_SAVED && e->version != NULL
            && query_history_last (q, e->version, TRUE, NULL) == NULL
            && (newest == NULL || strcmp (e->version, newest) > 0))
            newest = e->version;
    }
    if (newest != NULL)
    {
        char *keep = g_strdup (newest);
        // the text the file has now stays, one of them or not
        char *now = query_history_file_text (q);
        char *here = now != NULL ? query_version_of_text (q->history_file, now) : NULL;
        const guint before = q->history->len;

        query_current_drop (q, keep, here);
        if (q->history->len != before)
            query_history_save (q);
        g_free (here);
        g_free (now);
        g_free (keep);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* An entry of History that is no run: the text of the file, @text, kept as the version @version
   before another took its place, the cursor at @line, from 1; Enter on it brings it back */
static void
query_history_saved (query_t *q, const char *version, const char *text, long line)
{
    query_entry_t *e = g_new0 (query_entry_t, 1);
    char when[16];
    const time_t now = time (NULL);

    (void) strftime (when, sizeof (when), "%H:%M:%S", localtime (&now));
    e->when = g_strdup (when);
    e->status = QUERY_SAVED;
    e->line = line;
    e->text = g_strdup (text);
    e->version = g_strdup (version);
    g_ptr_array_add (q->history, e);
    if (q->history->len > QUERY_HISTORY_MAX)
        g_ptr_array_remove_index (q->history, 0);
    query_history_save (q);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_edit_put (query_t *q, void *edit, gsize from, gsize to, const char *text)
{
    WEdit *e = EDIT (edit);
    const char *p;
    gsize n;

    // one change: an edit of another going on, the plugin's or the editor's, is not mixed in
    if (!edit_runtime_begin (e))
        return FALSE;
    // a selection, or a mark F3 began, is not dragged on by the cursor sent to the text put
    edit_mark_cmd (e, TRUE);
    edit_cursor_move (e, (off_t) from - e->buffer.curs1);
    for (n = from; n < to; n++)
        (void) edit_delete (e, TRUE);
    for (p = text; *p != '\0'; p++)
        edit_insert (e, (unsigned char) *p);
    edit_runtime_end (e);
    e->force |= REDRAW_PAGE;
    edit_scroll_screen_over_cursor (e);
    q->host->window_show (q->host, edit);
    query_redraw (q);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_put_query (query_t *q, void *edit, const char *text)
{
    char *all, *put;
    const char *bol, *eol;
    gsize len = 0, at;
    gboolean done;

    all = edit != NULL ? q->host->get_text (q->host, edit, &len) : NULL;
    if (all == NULL)
        return FALSE;
    // the line of the cursor
    for (bol = all + MIN ((gsize) EDIT (edit)->buffer.curs1, len); bol > all && bol[-1] != '\n';
         bol--)
        ;
    eol = strchr (bol, '\n');
    if (eol == NULL)
        eol = all + len;
    if (query_blank_line (bol, eol))
    {
        /* an empty line: the query on it, a paragraph of its own, an empty line between it and
           the lines around it that are not */
        const char *next = *eol == '\n' ? eol + 1 : NULL;
        const char *prev = bol > all ? bol - 1 : NULL;
        const char *pbol;

        for (pbol = prev; pbol != NULL && pbol > all && pbol[-1] != '\n'; pbol--)
            ;
        at = (gsize) (bol - all);
        put = g_strconcat (prev != NULL && !query_blank_line (pbol, prev) ? "\n" : "", text,
                           next != NULL && !query_blank_line (next, query_line_end (next)) ? "\n"
                                                                                           : "",
                           (char *) NULL);
    }
    else
    {
        // after the paragraph of the cursor, an empty line between
        for (;;)
        {
            const char *next = *eol == '\n' ? eol + 1 : eol, *n_eol;

            if (next >= all + len)
                break;
            n_eol = strchr (next, '\n');
            if (n_eol == NULL)
                n_eol = all + len;
            if (query_blank_line (next, n_eol))
                break;
            eol = n_eol;
        }
        at = (gsize) (eol - all);
        put = g_strconcat ("\n\n", text, (char *) NULL);
    }
    done = query_edit_put (q, edit, at, at, put);
    g_free (put);
    g_free (all);
    return done;
}

/* --------------------------------------------------------------------------------------------- */

/* The window of @file among the windows, NULL when it has none */
void *
query_file_window (query_t *q, const char *file)
{
    GList *item;

    for (item = GROUP (q->host->host_data)->widgets; file != NULL && item != NULL;
         item = g_list_next (item))
        if (edit_widget_is_editor (CONST_WIDGET (item->data)))
        {
            char *path = q->host->get_current_file (q->host, item->data);
            const gboolean same = g_strcmp0 (path, file) == 0;

            g_free (path);
            if (same)
                return item->data;
        }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Enter and a double click in History: the text of the file of queries made that of the version
   of the run, in its window, one change that Undo takes back, the cursor on the query.  What the
   file had is kept as a version first, unless it is one already.  The store of the versions is
   never shown.  A run of before the versions shows its query alone. */
static void
query_history_open (query_t *q, const char *version)
{
    const query_entry_t *e = query_history_last (q, version, FALSE, NULL);
    char *path = query_version_path (q, version);
    char *label = query_version_label (version);
    char *was = NULL, *then = NULL, *kept = NULL, *message;
    void *edit;
    WEdit *ed;
    gsize len = 0;
    long kept_line = 1;
    const long line = e != NULL ? MAX (e->line, 1) : 1;

    if (path == NULL || !g_file_get_contents (path, &then, NULL, NULL))
    {
        query_tab_set (q, QUERY_TAB_OUTPUT, _ ("The version is gone from the store.\n"), TRUE);
        g_free (label);
        g_free (path);
        return;
    }

    edit = query_file_window (q, q->history_file);
    if (edit == NULL && q->history_file != NULL && q->host->open_file (q->host, q->history_file))
        edit = query_file_window (q, q->history_file);
    if (edit != NULL)
        was = q->host->get_text (q->host, edit, &len);
    if (edit == NULL || was == NULL)
    {
        query_tab_set (q, QUERY_TAB_OUTPUT, _ ("The file of queries is not open.\n"), TRUE);
        g_free (then);
        g_free (label);
        g_free (path);
        return;
    }

    ed = EDIT (edit);
    if (strcmp (was, then) != 0)
    {
        /* what the file had is not lost: in History to come back to, and Undo.  A text that is a
           version already, as when one goes from version to version, is not kept again; a text
           of none is the one "current", in place of the one before that was never run */
        kept = query_version_of_text (q->history_file, was);
        if (kept == NULL)
        {
            // the version gone to stays, a "current" of before too
            query_current_drop (q, version, NULL);
            kept = query_version_save (q->history_file, was);
        }
        // what it had could not be kept: it stays
        if (kept == NULL)
        {
            query_tab_set (q, QUERY_TAB_OUTPUT,
                           _ ("What the file has cannot be kept as a version: it stays as it "
                              "is.\n"),
                           TRUE);
            g_free (was);
            g_free (then);
            g_free (label);
            g_free (path);
            return;
        }
        kept_line = ed->buffer.curs_line + 1;
        edit_push_key_press (ed);
        edit_cursor_move (ed, -ed->buffer.curs1);
        while (ed->buffer.size > 0)
            (void) edit_delete (ed, TRUE);
        q->host->insert_text (q->host, edit, then, 0);
        /* the text goes a character at a time, and Undo keeps so many of them: a big file is
           not all taken back, the version is */
        message = strlen (was) + strlen (then) < QUERY_UNDO_TEXT
            ? g_strdup_printf (_ ("The file is as it was at %s; what it had is the version "
                                  "\"current\" of History.\nUndo takes it back.\n"),
                               label)
            : g_strdup_printf (_ ("The file is as it was at %s; what it had is the version "
                                  "\"current\" of History, Enter on it brings it back.\n"),
                               label);
    }
    else
        message = g_strdup_printf (_ ("The file is as it was at %s already.\n"), label);
    query_tab_set (q, QUERY_TAB_OUTPUT, message, FALSE);
    g_free (message);

    edit_move_to_line (ed, line - 1);
    ed->force |= REDRAW_COMPLETELY;
    q->host->window_show (q->host, edit);
    /* in History last: an entry may go for it, @e too, the oldest one; the cursor on the version
       of what the file had, Enter brings it back */
    if (kept != NULL)
    {
        // a text the store had already is shown where it is
        if (query_history_last (q, kept, FALSE, NULL) == NULL)
            query_history_saved (q, kept, was, kept_line);
        // the cursor on the version the file has now
        query_history_show (q, version);
    }
    g_free (kept);
    g_free (label);
    g_free (was);
    g_free (then);
    g_free (path);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

/* The text of the file of the history now: that of its window, saved or not, else of the disk */
static char *
query_history_file_text (query_t *q)
{
    void *edit = query_file_window (q, q->history_file);
    char *text = NULL;
    gsize len = 0;

    if (edit != NULL)
        text = q->host->get_text (q->host, edit, &len);
    if (text == NULL && q->history_file != NULL)
        (void) g_file_get_contents (q->history_file, &text, NULL, NULL);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* History shown, the cursor on the version the file has now, when it is one */
static void
query_history_show_here (query_t *q)
{
    char *now = query_history_file_text (q);
    char *version = now != NULL ? query_version_of_text (q->history_file, now) : NULL;

    query_history_show (q, version);
    g_free (version);
    g_free (now);
}

/* --------------------------------------------------------------------------------------------- */

/* F3 in History: what has changed in the file since that version, diff -u, in Output */
static void
query_history_diff (query_t *q, const char *version)
{
    char *path = query_version_path (q, version);
    char *label = query_version_label (version);
    char *now = query_history_file_text (q);
    GString *text = g_string_new (NULL);
    char *tmp = NULL, *out = NULL, *err = NULL, *label_old, *label_new;
    int fd;

    if (path == NULL || now == NULL)
    {
        g_string_append (text, _ ("The query has no version of the file to compare.\n"));
        goto done;
    }
    // the text of the window, saved or not, in a file of the user's alone
    {
        char *pattern = g_strconcat ("coole-query-XXXXXX", query_extension, (char *) NULL);

        fd = g_file_open_tmp (pattern, &tmp, NULL);
        g_free (pattern);
    }
    if (fd < 0 || write (fd, now, strlen (now)) != (ssize_t) strlen (now))
    {
        if (fd >= 0)
            close (fd);
        g_string_append (text, _ ("The file cannot be compared now.\n"));
        goto done;
    }
    close (fd);
    label_old = g_strdup_printf ("%s, %s", label, version);
    {
        char *base = g_path_get_basename (q->history_file);

        label_new = g_strdup_printf (_ ("%s, now"), base);
        g_free (base);
    }
    {
        const char *argv[] = { "diff",    "-u", "--label", label_old, "--label",
                               label_new, path, tmp,       NULL };
        int status = 0;

        if (!g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &out, &err,
                           &status, NULL))
            g_string_append (text, _ ("diff is not found.\n"));
        else if (out == NULL || *out == '\0')
            g_string_append_printf (text, _ ("The file is as it was at %s.\n"), label);
        else
        {
            char **lines = g_strsplit (out, "\n", -1);
            char **l;

            // what is taken away red, what is put green, the places cyan
            for (l = lines; *l != NULL; l++)
            {
                const char *color = g_str_has_prefix (*l, "---") || g_str_has_prefix (*l, "+++")
                    ? ANSI_BOLD
                    : **l == '-'                  ? ANSI_RED
                    : **l == '+'                  ? ANSI_GREEN
                    : g_str_has_prefix (*l, "@@") ? ANSI_CYAN
                                                  : NULL;

                if (l[1] == NULL && **l == '\0')
                    break;
                if (color != NULL)
                    g_string_append_printf (text, "%s%s" ANSI_RESET "\n", color, *l);
                else
                    g_string_append_printf (text, "%s\n", *l);
            }
            g_strfreev (lines);
        }
    }
    g_free (label_old);
    g_free (label_new);

done:
    if (tmp != NULL)
    {
        (void) unlink (tmp);
        g_free (tmp);
    }
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, TRUE);
    g_string_free (text, TRUE);
    g_free (out);
    g_free (err);
    g_free (now);
    g_free (path);
    g_free (label);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */
/* The plugin */
/* --------------------------------------------------------------------------------------------- */

static int
query_command (const query_t *q, long command)
{
    int i;

    for (i = 0; command != CK_IgnoreKey && i < QUERY_CMD_COUNT; i++)
        if (q->commands[i] == command)
            return i;
    return QUERY_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */

/* The menu of a file of queries, F11 */
/* --------------------------------------------------------------------------------------------- */

/* The query of @e a command of the menu is given, of its @text: the selection, else the part
   between the lines "// ---" around the cursor, else the file; its bytes @from to @to in @text */
static char *
query_menu_block (query_t *q, WEdit *e, const char *text, gsize len, gsize *from, gsize *to)
{
    off_t start, end;
    long first = 0;
    char *block;

    if (eval_marks (e, &start, &end) && end > start && (gsize) end <= len && e->column_highlight)
        return NULL;
    if (eval_marks (e, &start, &end) && end > start && (gsize) end <= len)
    {
        *from = (gsize) start;
        *to = (gsize) end;
        return g_strndup (text + start, (gsize) (end - start));
    }
    block = q->backend->sections ? query_section (text, e->buffer.curs_line, &first) : NULL;
    if (block != NULL)
    {
        const char *p = text;
        long line;

        // the part is lines first.. of the text: where its first one starts
        for (line = 0; line < first && *p != '\0'; line++)
        {
            p = query_line_end (p);
            if (*p == '\n')
                p++;
        }
        if (strncmp (p, block, strlen (block)) == 0)
        {
            *from = (gsize) (p - text);
            *to = *from + strlen (block);
            return block;
        }
        g_free (block);
    }
    *from = 0;
    *to = len;
    return g_strndup (text, len);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_menu_child_setup (gpointer data)
{
    // the query on the stdin of the command
    (void) dup2 (GPOINTER_TO_INT (data), STDIN_FILENO);
}

/* --------------------------------------------------------------------------------------------- */

/* A command of the menu: the query on its stdin; what it prints goes where @item says */
static void
query_menu_command (query_t *q, WEdit *e, const query_menu_item_t *item)
{
    const char *output = item->output != NULL ? item->output : "insert";
    gsize len = 0, from, to;
    char *text = q->host->get_text (q->host, e, &len);
    char *block, *file, *dir, *path = NULL, *out = NULL, *err = NULL;
    char *argv[4];
    char **envp;
    int fd, status = -1;
    gboolean ran = FALSE;

    if (text == NULL)
        return;
    block = query_menu_block (q, e, text, len, &from, &to);
    if (block == NULL)
    {
        q->host->message (q->host, D_NORMAL, _ ("Query"), QUERY_MENU_NO_COLUMN);
        g_free (text);
        return;
    }
    file = q->host->get_current_file (q->host, e);
    dir = file != NULL ? g_path_get_dirname (file) : NULL;
    envp = g_environ_setenv (g_get_environ (), "COOLE_QUERY_KIND", q->backend->id, TRUE);
    argv[0] = (char *) "/bin/sh";
    argv[1] = (char *) "-c";
    argv[2] = item->command;
    argv[3] = NULL;
    // the query in a file of ours alone, the stdin of the command
    fd = g_file_open_tmp ("coole-query-XXXXXX", &path, NULL);
    if (fd >= 0)
    {
        const size_t n = strlen (block);

        // through the descriptor: g_file_set_contents () would put another file in its place
        if (write (fd, block, n) == (ssize_t) n && lseek (fd, 0, SEEK_SET) == 0)
            ran = g_spawn_sync (dir, argv, envp, G_SPAWN_DEFAULT, query_menu_child_setup,
                                GINT_TO_POINTER (fd), &out, &err, &status, NULL);
        close (fd);
        unlink (path);
    }
    if (!ran || !g_spawn_check_wait_status (status, NULL))
        q->host->message (q->host, D_ERROR, _ ("Query"),
                          err != NULL && *g_strstrip (err) != '\0' ? err
                                                                   : _ ("The command failed."));
    else if (strcmp (output, "replace") == 0)
    {
        const size_t n = strlen (out);

        // nothing printed is no query: the one there stays
        if (query_blank (out))
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              _ ("The command printed nothing: the query stays as it is."));
        else
        {
            // a query that did not end with a newline gets none from the command
            if (n > 0 && out[n - 1] == '\n' && from < to && text[to - 1] != '\n')
                out[n - 1] = '\0';
            (void) query_edit_put (q, e, from, to, out);
        }
    }
    else if (strcmp (output, "show") == 0)
        query_tab_set (q, QUERY_TAB_OUTPUT, out, TRUE);
    else if (strcmp (output, "insert") == 0 && *out != '\0')
    {
        const size_t n = strlen (out);

        // the last newline is the end of the output, not a line to insert
        if (out[n - 1] == '\n')
            out[n - 1] = '\0';
        q->host->insert_text (q->host, e, out, 0);
    }
    else if (strcmp (output, "insert") != 0 && strcmp (output, "none") != 0)
    {
        char *said = g_strdup_printf (
            _ ("No output \"%s\" in the menu: insert, replace, show or none."), output);

        q->host->message (q->host, D_ERROR, _ ("Query"), said);
        g_free (said);
    }
    g_strfreev (envp);
    g_free (path);
    g_free (out);
    g_free (err);
    g_free (dir);
    g_free (file);
    g_free (block);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

/* A line @line put before the line of the cursor: a directive, "// @table" */
static void
query_menu_directive (query_t *q, WEdit *e, const char *line)
{
    gsize len = 0, at;
    char *text = q->host->get_text (q->host, e, &len);

    if (text == NULL)
        return;
    at = MIN ((gsize) e->buffer.curs1, len);
    while (at > 0 && text[at - 1] != '\n')
        at--;
    (void) query_edit_put (q, e, at, at, line);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

/* The selection in capitals, @upper, or in small letters */
static void
query_menu_case (query_t *q, WEdit *e, gboolean upper)
{
    gsize len = 0;
    char *text = q->host->get_text (q->host, e, &len);
    off_t start, end;

    if (text == NULL)
        return;
    if (!eval_marks (e, &start, &end) || end <= start || (gsize) end > len)
        q->host->message (q->host, D_NORMAL, _ ("Query"), _ ("Select the text first."));
    else if (e->column_highlight)
        q->host->message (q->host, D_NORMAL, _ ("Query"), QUERY_MENU_NO_COLUMN);
    else
    {
        const char *s = text + start;
        const gssize n = (gssize) (end - start);
        char *changed;

        if (g_utf8_validate (s, n, NULL))
            changed = upper ? g_utf8_strup (s, n) : g_utf8_strdown (s, n);
        else
            changed = upper ? g_ascii_strup (s, n) : g_ascii_strdown (s, n);
        (void) query_edit_put (q, e, (gsize) start, (gsize) end, changed);
        g_free (changed);
    }
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

static char *
query_menu_path (void)
{
    return g_build_filename (mc_config_get_path (), "query", QUERY_MENU_FILE, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* The text of the menu as it is, not as a script: that of the user when @user, else that of
   coole, of a build run by meson devenv, of /etc/coole/query, of the data of coole; NULL when
   there is none.  @path the file it is of */
static char *
query_menu_text (gboolean user, char **path)
{
    const char *misc = g_getenv ("COOLE_MISC_DIR");
    char *dirs[] = {
        user ? g_build_filename (mc_config_get_path (), "query", (char *) NULL) : NULL,
        misc != NULL ? g_build_filename (misc, "query", (char *) NULL) : NULL,
        g_build_filename (mc_global.sysconfig_dir, "query", (char *) NULL),
        g_build_filename (mc_global.share_data_dir, "query", (char *) NULL),
    };
    char *text = NULL;
    guint i;

    *path = NULL;
    for (i = 0; i < G_N_ELEMENTS (dirs); i++)
    {
        if (text == NULL && dirs[i] != NULL)
        {
            char *file = g_build_filename (dirs[i], QUERY_MENU_FILE, (char *) NULL);

            if (g_file_get_contents (file, &text, NULL, NULL))
                *path = file;
            else
            {
                text = NULL;
                g_free (file);
            }
        }
        g_free (dirs[i]);
    }
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* The menu of the user to edit, made from that of coole when there is none */
static void
query_menu_edit (query_t *q)
{
    char *path = query_menu_path ();

    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        char *dir = g_path_get_dirname (path);
        char *of = NULL;
        char *text = query_menu_text (FALSE, &of);

        (void) g_mkdir_with_parents (dir, 0700);
        (void) g_file_set_contents (path, text != NULL ? text : "", -1, NULL);
        g_free (text);
        g_free (of);
        g_free (dir);
    }
    (void) q->host->open_file (q->host, path);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

/* The menu: that of the user, else that of coole; NULL, and why said, when there is none */
static GPtrArray *
query_menu_load (query_t *q)
{
    char *path = NULL;
    char *text = query_menu_text (TRUE, &path);
    GPtrArray *menu = NULL;
    GError *error = NULL;

    if (text == NULL)
    {
        char *said = g_strdup_printf (_ ("The menu %s of the queries is not there:\n"
                                         "it is in the queries of coole, or in\n"
                                         "~/.config/coole/query."),
                                      QUERY_MENU_FILE);

        q->host->message (q->host, D_ERROR, _ ("Query"), said);
        g_free (said);
        return NULL;
    }
    menu = query_menu_parse (text, &error);
    if (menu == NULL)
    {
        // the file that was read, of the user or of coole
        char *said = g_strdup_printf ("%s:\n%s", path, error->message);

        q->host->message (q->host, D_ERROR, _ ("Query"), said);
        g_free (said);
        g_error_free (error);
    }
    g_free (path);
    g_free (text);
    return menu;
}

/* --------------------------------------------------------------------------------------------- */

/* What the menu shows of @item */
static const char *
query_menu_label (const query_t *q, const query_menu_item_t *item)
{
    const char *a = item->action;

    if (item->label != NULL)
        return item->label;
    if (a == NULL)
        return item->command;
    if (strcmp (a, "explain") == 0)
        return strcmp (q->backend->id, "postgres") == 0
            ? _ ("EXPLAIN the query, ANALYZE a SELECT")
            : _ ("Explain the query, .explain (\"executionStats\")");
    if (strcmp (a, "count") == 0)
        return _ ("Count the rows of the query");
    if (strcmp (a, "table") == 0)
        return _ ("Put // @table before the line: a table of the result");
    if (strcmp (a, "limit") == 0)
        return _ ("Put // @limit 20 before the line: 20 documents");
    if (strcmp (a, "upper") == 0)
        return _ ("The selection in capitals");
    if (strcmp (a, "lower") == 0)
        return _ ("The selection in small letters");
    if (strcmp (a, "menu") == 0)
        return _ ("Edit this menu");
    return a;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @edit is a file of queries, of the extension of the database or of no name, where F11
   is the menu of the queries; in another file, query-menu.ini or a script, it is that of the user
 */
static gboolean
query_is_query_file (const query_t *q, void *edit)
{
    char *file;
    gboolean is;

    if (!q->on || edit == NULL || !edit_widget_is_editor (CONST_WIDGET (edit)))
        return FALSE;
    file = q->host->get_current_file (q->host, edit);
    is = file == NULL || g_str_has_suffix (file, q->backend->extension);
    g_free (file);
    return is;
}

/* --------------------------------------------------------------------------------------------- */

/* F11 in a file of queries: the menu of the queries, not the menu of the user */
static void
query_user_menu (query_t *q, WEdit *e)
{
    GPtrArray *menu = query_menu_load (q);
    GPtrArray *shown;
    const query_menu_item_t *item = NULL;
    Listbox *l;
    int choice, width = 40;
    guint i;

    if (menu == NULL)
        return;
    shown = g_ptr_array_new ();
    for (i = 0; i < menu->len; i++)
    {
        const query_menu_item_t *it = g_ptr_array_index (menu, i);

        if (query_menu_item_for (it, q->backend->id))
        {
            g_ptr_array_add (shown, (gpointer) it);
            width = MAX (width, str_term_width1 (query_menu_label (q, it)) + 8);
        }
    }
    l = listbox_window_new (MAX (1, MIN ((int) shown->len, LINES - 6)), MIN (width, COLS - 6),
                            _ ("Query"), "[Query]");
    for (i = 0; i < shown->len; i++)
    {
        const query_menu_item_t *it = g_ptr_array_index (shown, i);
        char *label = g_strdup_printf ("%c   %s", it->key, query_menu_label (q, it));

        LISTBOX_APPEND_TEXT (l, it->key, label, NULL, FALSE);
        g_free (label);
    }
    choice = listbox_run (l);
    if (choice >= 0 && choice < (int) shown->len)
        item = g_ptr_array_index (shown, choice);
    g_ptr_array_free (shown, TRUE);

    if (item == NULL)
        ;
    else if (item->command != NULL)
        query_menu_command (q, e, item);
    else if (strcmp (item->action, "explain") == 0)
        query_run_as (q, e, FALSE, FALSE, QUERY_AS_EXPLAIN);
    else if (strcmp (item->action, "count") == 0)
        query_run_as (q, e, FALSE, FALSE, QUERY_AS_COUNT);
    else if (strcmp (item->action, "table") == 0)
        query_menu_directive (q, e, "// @table\n");
    else if (strcmp (item->action, "limit") == 0)
        query_menu_directive (q, e, "// @limit 20\n");
    else if (strcmp (item->action, "upper") == 0 || strcmp (item->action, "lower") == 0)
        query_menu_case (q, e, strcmp (item->action, "upper") == 0);
    else if (strcmp (item->action, "menu") == 0)
        query_menu_edit (q);
    else
    {
        char *said = g_strdup_printf (_ ("No action %s in the menu."), item->action);

        q->host->message (q->host, D_ERROR, _ ("Query"), said);
        g_free (said);
    }
    g_ptr_array_free (menu, TRUE);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

/* @cmd, by its key when @key: the keys of History only when it has the focus */
static gboolean
query_run_command (query_t *q, int cmd, void *edit, gboolean key)
{
    const char *version;

    // the editor's F5, Copy, but in coole -Q
    if (!q->on)
        return FALSE;
    switch (cmd)
    {
    case QUERY_CMD_HELP:
    {
        // the help of the plugin, not that of the editor
        ev_help_t event_data = { QUERY_HELP_FILE, QUERY_HELP_NODE, NULL };

        mc_event_raise (MCEVENT_GROUP_CORE, "help", &event_data);
        return TRUE;
    }
    case QUERY_CMD_RUN:
        // F5 in Schema: the rows of the relation of the node of its cursor
        if (edit == NULL && q->backend->schema != NULL
            && q->backend->schema (q->backend_data, QUERY_SCHEMA_ROWS, TRUE))
            return TRUE;
        version = edit == NULL ? query_history_current (q, TRUE) : NULL;
        if (version != NULL)
            query_history_run (q, version);
        else
            query_run (q, edit, FALSE, FALSE);
        return TRUE;
    case QUERY_CMD_RUN_ALL:
        query_run (q, edit, TRUE, FALSE);
        return TRUE;
    case QUERY_CMD_EXPORT:
        if (!q->backend->export)
        {
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              _ ("The rows of the queries of this database are not exported."));
            return TRUE;
        }
        // in Schema: the rows of the relation of the node of its cursor
        if (edit == NULL && q->backend->schema != NULL
            && q->backend->schema (q->backend_data, QUERY_SCHEMA_EXPORT, TRUE))
            return TRUE;
        query_run (q, edit, FALSE, TRUE);
        return TRUE;
    case QUERY_CMD_STOP:
        // Esc: with no query running it is the editor's, Quit; with one, a slip of the hand must
        // not stop it
        if (key)
        {
            if (!q->backend->running (q->backend_data))
                return FALSE;
            if (query_dialog (_ ("Query"), _ ("A query is running. Stop it?"), D_NORMAL, 2,
                              _ ("&Stop"), _ ("&Cancel"))
                == 0)
                query_stop (q);
            return TRUE;
        }
        query_stop (q);
        return TRUE;
    case QUERY_CMD_ROLLBACK:
        if (q->backend->rollback == NULL)
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              _ ("The queries of this database have no transaction."));
        else if (q->backend->running (q->backend_data))
            query_run (q, NULL, FALSE, FALSE);
        else
            query_run_alone (q, q->backend->rollback);
        return TRUE;
    case QUERY_CMD_SCHEMA:
    case QUERY_CMD_SCHEMA_ROWS:
    case QUERY_CMD_SCHEMA_INSERT:
    case QUERY_CMD_SCHEMA_REFRESH:
    case QUERY_CMD_SCHEMA_QUERY:
    case QUERY_CMD_SCHEMA_DDL:
        // the keys of the nodes of Schema are its own there alone: Insert of a file is that of
        // the editor
        if (key && cmd != QUERY_CMD_SCHEMA && edit != NULL)
            return FALSE;
        if (q->backend->schema == NULL)
        {
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              _ ("The queries of this database have no schema shown."));
            return TRUE;
        }
        return q->backend->schema (q->backend_data,
                                   cmd == QUERY_CMD_SCHEMA              ? QUERY_SCHEMA_TOGGLE
                                       : cmd == QUERY_CMD_SCHEMA_ROWS   ? QUERY_SCHEMA_ROWS
                                       : cmd == QUERY_CMD_SCHEMA_INSERT ? QUERY_SCHEMA_INSERT
                                       : cmd == QUERY_CMD_SCHEMA_QUERY  ? QUERY_SCHEMA_QUERY
                                       : cmd == QUERY_CMD_SCHEMA_DDL    ? QUERY_SCHEMA_DDL
                                                                        : QUERY_SCHEMA_REFRESH,
                                   key && cmd != QUERY_CMD_SCHEMA);
    case QUERY_CMD_AI:
        // the request is in a file: the file in front, from the tabs and Schema too
        query_ai_ask (q, edit);
        return TRUE;
    case QUERY_CMD_HISTORY_INSERT:
    case QUERY_CMD_HISTORY_SHOW:
        // F3 in Schema: the query of the rows of the relation of its node in the file; Enter
        // there is that of its window, which opens and closes the node
        if (cmd == QUERY_CMD_HISTORY_SHOW && key && edit == NULL && q->backend->schema != NULL
            && q->backend->schema (q->backend_data, QUERY_SCHEMA_QUERY, TRUE))
            return TRUE;
        // Enter and F3 of a file are those of the editor
        version = !key || edit == NULL ? query_history_current (q, key) : NULL;
        if (version == NULL)
            return FALSE;
        if (cmd == QUERY_CMD_HISTORY_INSERT)
            query_history_open (q, version);
        else
            query_history_diff (q, version);
        return TRUE;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_handle_key (void *data, int key, void *edit)
{
    query_t *q = (query_t *) data;
    const int cmd = query_command (q, q->host->command_lookup (q->host, QUERY_KEYMAP_SECTION, key));

    // the key of the menu of the user, before the scripts that take it: the menu of the queries
    if (keybind_lookup_keymap_command (editor_map, key) == CK_UserMenu
        && query_is_query_file (q, edit))
    {
        query_user_menu (q, EDIT (edit));
        return MC_EPR_OK;
    }
    return query_run_command (q, cmd, edit, TRUE) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

/* The labels of a file: the keys of the plugin over those of the editor, F5 Copy say */
static void
query_buttonbar (query_t *q, void *edit)
{
    WButtonBar *bb = buttonbar_find (DIALOG (WIDGET (edit)->owner));
    int i;

    if (bb == NULL)
        return;
    for (i = 1; i <= 10; i++)
    {
        const long command = q->host->command_lookup (q->host, QUERY_KEYMAP_SECTION, KEY_F (i));

        // the others of the plugin are of History and Schema, a file has those of the editor
        switch (query_command (q, command))
        {
        case QUERY_CMD_RUN:
            buttonbar_set_label_command (bb, i, Q_ ("ButtonBar|Run"), command, WIDGET (edit));
            break;
        case QUERY_CMD_AI:
            buttonbar_set_label_command (bb, i, Q_ ("ButtonBar|AIGen"), command, WIDGET (edit));
            break;
        default:
            break;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A file window in front: History is of its file, unless it is of the store of versions */
static mc_ep_result_t
query_handle_event (void *data, void *edit, int event_id, void *payload)
{
    query_t *q = (query_t *) data;
    char *file;

    (void) payload;
    if (!q->on || event_id != MC_EP_EVENT_FOCUS_IN || edit == NULL)
        return MC_EPR_NOT_SUPPORTED;
    query_buttonbar (q, edit);
    // before the tabs are made, at the start, the start loads the history itself
    if (q->tabs[QUERY_TAB_HISTORY] == 0)
        return MC_EPR_NOT_SUPPORTED;
    file = q->host->get_current_file (q->host, edit);
    if (file != NULL && g_strcmp0 (file, q->history_file) != 0 && !query_in_versions (file))
    {
        query_history_load (q, file);
        query_history_show_here (q);
    }
    g_free (file);
    return MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_handle_action (void *data, long command, void *edit)
{
    query_t *q = (query_t *) data;

    // the menu of the user from the menu Command: that of the queries in coole -Q
    if (command == CK_UserMenu && query_is_query_file (q, edit))
    {
        query_user_menu (q, EDIT (edit));
        return MC_EPR_OK;
    }
    return query_run_command (q, query_command (q, command), edit, FALSE) ? MC_EPR_OK
                                                                          : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

/* A double click on a line of History, "activate" of the viewer: the version of its query open */
static void
query_viewer_signal (const char *name, const char *signal, GVariant *args, void *data)
{
    query_t *q = (query_t *) data;
    gint64 id = 0, line = -1;

    (void) name;
    if (!g_variant_lookup (args, "id", "x", &id))
        return;
    if (strcmp (signal, "activate") != 0 || q->tabs[QUERY_TAB_HISTORY] == 0
        || id != q->tabs[QUERY_TAB_HISTORY] || !g_variant_lookup (args, "line", "x", &line)
        || line < 0 || line >= (gint64) q->rows->len)
        return;
    query_history_open (q, g_ptr_array_index (q->rows, (guint) line));
}

/* --------------------------------------------------------------------------------------------- */

/* coole -Q, once the editor runs: the tabs at the bottom, the history of the file */
static void
query_startup (void *data)
{
    query_t *q = (query_t *) data;
    void *edit = q->host->window_top_file (q->host);
    char *file = edit != NULL ? q->host->get_current_file (q->host, edit) : NULL;
    GString *text = g_string_new (NULL);

    query_history_load (q, file);
    g_free (file);
    q->backend->startup (q->backend_data, edit, text);
    // the tabs in their order, the first one seen
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, FALSE);
    query_table_say (q, _ ("No query run yet.\n"));
    query_history_show_here (q);
    query_viewer_id (q, "show", q->tabs[QUERY_TAB_OUTPUT], FALSE);
    g_string_free (text, TRUE);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

static void *
query_open (mc_editor_host_t *host, void *editor_dialog)
{
    query_t *q = g_new0 (query_t, 1);
    const char *kind = host->startup_option (host, "query");
    guint i;

    (void) editor_dialog;
    q->host = host;
    q->history = g_ptr_array_new_with_free_func (query_entry_free);
    q->rows = g_ptr_array_new_with_free_func (g_free);
    host->commands_register (host, QUERY_KEYMAP_SECTION, N_ ("&Query"), query_commands);
    for (i = 0; i < QUERY_CMD_COUNT; i++)
        q->commands[i] = host->command_id (host, query_commands[i].name);
    q->mark = host->marker_kind (host, &query_mark_kind);
    for (i = 0; kind != NULL && q->backend == NULL && i < G_N_ELEMENTS (query_backends); i++)
        if (strcmp (kind, query_backends[i]->id) == 0)
            q->backend = query_backends[i];
    q->on = q->backend != NULL;
    if (q->on)
    {
        q->backend_data = q->backend->open (q);
        // Ctrl held, the bar says what Ctrl-F6 does
        if (host->command_label != NULL)
            host->command_label (host, query_commands[QUERY_CMD_EXPORT].name,
                                 N_ ("ButtonBar|Export"));
        query_extension = q->backend->extension;
        q->viewer_signal = host->service_connect (host, "viewer", query_viewer_signal, q);
        query_ticker_start (q);
        host->call_later (host, query_startup, q);
    }
    return q;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_close (void *data)
{
    query_t *q = (query_t *) data;

    if (q->on)
        query_ticker_stop (q);
    query_ai_free (q);
    // the key is gone with the plugin: the bar shows the editor's there again
    if (q->on && q->host->command_label != NULL)
        q->host->command_label (q->host, query_commands[QUERY_CMD_EXPORT].name, NULL);
    if (q->backend != NULL)
        q->backend->close (q->backend_data);
    // a run killed by the close: no half of a file
    g_free (query_export_close (q, FALSE, NULL));
    if (q->export_options != NULL)
    {
        g_free (q->export_options->table);
        g_free (q->export_options);
    }
    if (q->viewer_signal != 0)
        q->host->service_disconnect (q->host, q->viewer_signal);
    g_ptr_array_free (q->history, TRUE);
    g_ptr_array_free (q->rows, TRUE);
    g_free (q->history_path);
    g_free (q->history_file);
    g_free (q->endpoint);
    g_free (q->text);
    g_free (q->file);
    g_free (q->use);
    g_free (q->version);
    g_free (q->run_line);
    g_free (q->running);
    g_free (q);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_ok_to_quit (void *data)
{
    query_t *q = (query_t *) data;

    return q->backend == NULL || !q->backend->running (q->backend_data)
        || query_dialog (_ ("Query"), _ ("Stop the query and quit?"), D_NORMAL, 2, _ ("&Stop"),
                         _ ("&Cancel"))
        == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The commands of the menu Query: those of the keys, the menu running them as their keys do
   outside a window of the tabs or of Schema */
static mc_ep_result_t
query_menu_run (void *data, void *edit, int cmd)
{
    query_t *q = (query_t *) data;

    return query_run_command (q, cmd, edit, FALSE) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_run (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_RUN);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_run_all (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_RUN_ALL);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_stop (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_STOP);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_export (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_EXPORT);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_ai (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_AI);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_rollback (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_ROLLBACK);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_schema (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_SCHEMA);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
query_act_schema_refresh (void *data, void *edit)
{
    return query_menu_run (data, edit, QUERY_CMD_SCHEMA_REFRESH);
}

/* --------------------------------------------------------------------------------------------- */

/* The actions of the menu, in the order of query_actions[] */
enum
{
    QUERY_ACT_RUN,
    QUERY_ACT_RUN_ALL,
    QUERY_ACT_STOP,
    QUERY_ACT_EXPORT,
    QUERY_ACT_AI,
    QUERY_ACT_ROLLBACK,
    QUERY_ACT_SCHEMA,
    QUERY_ACT_SCHEMA_REFRESH,
    QUERY_ACT_COUNT
};

/* each action, and the command whose key the menu shows */
static const mc_ep_action_t query_actions[QUERY_ACT_COUNT] = {
    { "Run", query_act_run },       { "Run the whole file", query_act_run_all },
    { "Stop", query_act_stop },     { "Export", query_act_export },
    { "AI", query_act_ai },         { "Rollback", query_act_rollback },
    { "Schema", query_act_schema }, { "Read the schema again", query_act_schema_refresh },
};

static const int query_action_commands[QUERY_ACT_COUNT] = {
    QUERY_CMD_RUN, QUERY_CMD_RUN_ALL,  QUERY_CMD_STOP,   QUERY_CMD_EXPORT,
    QUERY_CMD_AI,  QUERY_CMD_ROLLBACK, QUERY_CMD_SCHEMA, QUERY_CMD_SCHEMA_REFRESH,
};

static const mc_ep_cmd_menu_entry_t query_menu[] = {
    // the run of the query
    { QUERY_MENU, N_ ("&Run"), QUERY_ACT_RUN, NULL },
    { QUERY_MENU, N_ ("Run the whole &file"), QUERY_ACT_RUN_ALL, NULL },
    { QUERY_MENU, N_ ("&Stop"), QUERY_ACT_STOP, NULL },
    { QUERY_MENU, NULL, 0, NULL },
    // what is made of it
    { QUERY_MENU, N_ ("&Export the rows..."), QUERY_ACT_EXPORT, NULL },
    { QUERY_MENU, N_ ("Write the query by &AI"), QUERY_ACT_AI, NULL },
    { QUERY_MENU, NULL, 0, NULL },
    { QUERY_MENU, N_ ("Roll &back"), QUERY_ACT_ROLLBACK, NULL },
    { QUERY_MENU, NULL, 0, NULL },
    // the database
    { QUERY_MENU, N_ ("S&chema"), QUERY_ACT_SCHEMA, NULL },
    { QUERY_MENU, N_ ("Read the schema a&gain"), QUERY_ACT_SCHEMA_REFRESH, NULL },
};

/* --------------------------------------------------------------------------------------------- */

/* The key of the action @action_index, as the keymap has it now */
static char *
query_menu_shortcut (int action_index)
{
    if (action_index < 0 || action_index >= QUERY_ACT_COUNT)
        return NULL;
    return keymap_menu_shortcut (QUERY_KEYMAP_SECTION,
                                 query_commands[query_action_commands[action_index]].name);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t query_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "query",
    .display_name = "Query",
    .flags = MC_EPF_NONE,
    .open = query_open,
    .close = query_close,
    .handle_key = query_handle_key,
    .handle_event = query_handle_event,
    .handle_action = query_handle_action,
    .ok_to_quit = query_ok_to_quit,
    .actions = query_actions,
    .action_count = QUERY_ACT_COUNT,
    .cmd_menu_entries = query_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (query_menu),
    .get_menu_shortcut = query_menu_shortcut,
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The entry of the module, which the editor calls when it loads it */
const mc_editor_plugin_t *
mc_editor_plugin_register (void)
{
    return &query_plugin;
}

/* --------------------------------------------------------------------------------------------- */
