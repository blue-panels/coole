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

#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/event.h"
#include "lib/mcconfig.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/editor-plugin.h"
#include "lib/plugin-service.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

#include "query-core.h"
#include "query-int.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define QUERY_KEYMAP_SECTION "query"
#define QUERY_HELP_FILE      "query.md"
#define QUERY_HELP_NODE      "[Query]"
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

/*** forward declarations (file scope functions) *************************************************/

const mc_editor_plugin_t *mc_editor_plugin_register (void);
static void query_run_text (query_t *q, char *text, char *file, long first, char *use,
                            char *version);
static gboolean query_blank (const char *text);
static void query_current_one (query_t *q);
static char *query_history_file_text (query_t *q);
static char *query_version_of_text (const char *file, const char *text);

/*** file scope variables ************************************************************************/

static const mc_ep_command_t query_commands[QUERY_CMD_COUNT + 1] = {
    { "Help", NULL, "f1" },
    { "QueryRun", N_ ("Run the selection, else the part between \"// ---\", else the file"), "f5" },
    { "QueryRunAll", N_ ("Run the whole file, even with a selection"), "ctrl-f5" },
    { "QueryStop", N_ ("Stop the query"), NULL },
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
    { NULL, NULL, NULL },
};

static const mc_ep_marker_kind_t query_mark_kind = {
    "query.error", "query-error-char", "\u00d7", "E", "builderror", NULL, NULL, 6, "brightred",
};

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
    widget_draw (WIDGET (q->host->host_data));
    tty_refresh ();
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

/* Run the selection of @edit, else the section of its cursor between lines "// ---", else all of
   it; all of it when @all */
static void
query_run (query_t *q, void *edit, gboolean all)
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
    // a part run alone is in the database of the use ("name") above it
    if (first > 0 && q->backend->context != NULL)
        use = q->backend->context (text, first);
    file = q->host->get_current_file (q->host, edit);
    // the file as it is run, unless it is that of the run before; nothing run, no version
    version = file != NULL && !query_in_versions (file) ? query_version_save (file, text) : NULL;
    g_free (text);
    q->first_column = column;
    query_run_text (q, block, file, first + 1, use, version);
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
    query_tab_set (q, QUERY_TAB_OUTPUT, _ ("Running...\n"), TRUE);
    query_table_say (q, _ ("Running...\n"));
    q->backend->start (q->backend_data);
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
        query_run (q, NULL, FALSE);
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
            query_run (q, edit, FALSE);
        return TRUE;
    case QUERY_CMD_RUN_ALL:
        query_run (q, edit, TRUE);
        return TRUE;
    case QUERY_CMD_STOP:
        query_stop (q);
        return TRUE;
    case QUERY_CMD_ROLLBACK:
        if (q->backend->rollback == NULL)
            q->host->message (q->host, D_NORMAL, _ ("Query"),
                              _ ("The queries of this database have no transaction."));
        else if (q->backend->running (q->backend_data))
            query_run (q, NULL, FALSE);
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
        query_extension = q->backend->extension;
        q->viewer_signal = host->service_connect (host, "viewer", query_viewer_signal, q);
        host->call_later (host, query_startup, q);
    }
    return q;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_close (void *data)
{
    query_t *q = (query_t *) data;

    query_ai_free (q);
    if (q->backend != NULL)
        q->backend->close (q->backend_data);
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
