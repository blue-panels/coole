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
 *  the selection, else the part of the file between lines "// ---", else the whole file, to
 *  mongosh, and its answer comes in the tabs at the
 *  bottom: Output, what mongosh printed; Table, the documents in columns; History, the queries
 *  run.  A query with a line "// @table" is run with --json, its value made the table.
 *
 *  mongosh is given what to connect to by the environment of coole, which it inherits:
 *  MC_MONGO_URI, MC_MONGO_DB; MC_MONGOSH is the program, else mongosh of PATH.  Neither the URI
 *  nor the password in it goes anywhere else: not to the screen, not to the history.
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/event.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/editor-plugin.h"
#include "lib/plugin-service.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

#include "query-core.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define QUERY_KEYMAP_SECTION "query"
#define QUERY_HELP_FILE      "query.md"
#define QUERY_HELP_NODE      "[Query]"
// what mongosh prints that is shown; the rest is cut
#define QUERY_OUTPUT_MAX  (8 * 1024 * 1024)
#define QUERY_HISTORY_MAX 100
// the height of the tabs, percent of the screen
#define QUERY_TABS_SIZE 40

#define ANSI_BOLD       "\033[1m"
#define ANSI_RED        "\033[31m"
#define ANSI_RESET      "\033[0m"

/*** file scope type declarations ****************************************************************/

enum
{
    QUERY_CMD_NONE = -1,
    QUERY_CMD_HELP,
    QUERY_CMD_RUN,
    QUERY_CMD_RUN_ALL,
    QUERY_CMD_STOP,
    QUERY_CMD_HISTORY_INSERT,
    QUERY_CMD_HISTORY_SHOW,
    QUERY_CMD_COUNT
};

enum
{
    QUERY_TAB_OUTPUT,
    QUERY_TAB_TABLE,
    QUERY_TAB_HISTORY,
    QUERY_TAB_COUNT
};

/* A query run, in the history */
typedef struct
{
    char *when;  // "14:02:11"
    double seconds;
    int status;  // the exit code of mongosh, -1 when it was stopped
    long line;   // the first line of the query in the file, from 1
    char *text;
} query_entry_t;

typedef struct
{
    mc_editor_host_t *host;
    gboolean on;  // coole -Q
    char *mongosh;
    char *endpoint;  // "host:port/db", for the header of the output

    // the run going on, or the last one
    GPid pid;
    int fds[2];  // its stdout and stderr, -1 when ended
    GString *out, *err;
    gboolean cut;      // mongosh printed more than QUERY_OUTPUT_MAX: the rest is not kept
    gboolean table;    // asked for by "// @table": mongosh --json, its answer in columns too
    char *use;         // the string of the use () above a part run alone, NULL for none
    long limit;        // asked for by "// @limit N", 0 when not
    gboolean limited;  // and put into the query
    gboolean stopped;
    char *text;
    char *file;  // the file of the query, NULL for a file with no name
    long first;  // the first line of the query in it, from 1
    gint64 started;

    gint64 tabs[QUERY_TAB_COUNT];  // the windows of the viewer, 0 when none
    long commands[QUERY_CMD_COUNT];
    int mark;
    GPtrArray *history;  // query_entry_t, the last one last
    char *history_path;
    char *history_file;             // the file whose history it is
    GArray *rows;                   // the entry of each line of the tab History, an index
    const query_entry_t *expanded;  // the one shown there with all its text, F3
} query_t;

/*** forward declarations (file scope functions) *************************************************/

const mc_editor_plugin_t *mc_editor_plugin_register (void);
static void query_run_text (query_t *q, char *text, char *file, long first, char *use);

/*** file scope variables ************************************************************************/

static const mc_ep_command_t query_commands[QUERY_CMD_COUNT + 1] = {
    { "Help", NULL, "f1" },
    { "QueryRun", N_ ("Run the selection, else the part between \"// ---\", else the file"), "f5" },
    { "QueryRunAll", N_ ("Run the whole file, even with a selection"), "ctrl-f5" },
    { "QueryStop", N_ ("Stop the query"), NULL },
    { "QueryHistoryInsert", N_ ("Put the query of History under the cursor into the file"),
      "enter" },
    { "QueryHistoryShow", N_ ("Show all the text of the query of History under the cursor"), "f3" },
    { NULL, NULL, NULL },
};

static const mc_ep_marker_kind_t query_mark_kind = {
    "query.error", "query-error-char", "\u00d7", "E", "builderror", NULL, NULL, 6, "brightred",
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

/* The text of a tab, the tab made again when the user has closed it; the one seen when @front */
static void
query_tab_set (query_t *q, int tab, const char *text, gboolean front)
{
    GVariantDict dict;
    GVariant *reply = NULL;

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
}

/* --------------------------------------------------------------------------------------------- */

static void
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

/* The entry of the line of the cursor of History; NULL when there is none, or History has not
   the focus and @focused */
static const query_entry_t *
query_history_current (query_t *q, gboolean focused)
{
    const long line = query_history_cursor (q, focused);

    if (line < 0 || line >= (long) q->rows->len)
        return NULL;
    return g_ptr_array_index (q->history, g_array_index (q->rows, guint, line));
}

/* --------------------------------------------------------------------------------------------- */

/* The queries run, the last one first, a line each; the one expanded with all its text under its
   line.  The cursor goes to the line of @current, else to the first one. */
static void
query_history_show (query_t *q, const query_entry_t *current)
{
    GString *text = g_string_new (NULL);
    long cursor = 0;
    guint i;

    g_array_set_size (q->rows, 0);
    if (q->history->len == 0)
        g_string_append (text, _ ("No query run yet.\n"));
    // the last one first
    for (i = q->history->len; i > 0; i--)
    {
        const guint index = i - 1;
        const query_entry_t *e = g_ptr_array_index (q->history, index);
        const char *start = e->text, *nl;

        if (e == current)
            cursor = (long) q->rows->len;
        // the query, not the "// @table" and "// @limit" before it
        while ((nl = strchr (start, '\n')) != NULL)
        {
            char *line = g_strndup (start, (gsize) (nl - start));
            const gboolean mark = query_wants_table (line) || query_wants_limit (line) > 0;

            g_free (line);
            if (!mark)
                break;
            start = nl + 1;
        }
        char *status =
            e->status < 0 ? g_strdup (_ ("stopped")) : g_strdup_printf (_ ("exit %d"), e->status);

        g_string_append_printf (text, "%s  %6.2f s  %-8s  %5ld  ", e->when, e->seconds, status,
                                e->line);
        if (e->status != 0)
            g_string_append (text, ANSI_RED);
        g_string_append_len (text, start, nl != NULL ? nl - start : (gssize) strlen (start));
        // more lines, not empty ones alone
        if (nl != NULL && nl[strspn (nl, " \t\r\n")] != '\0' && e != q->expanded)
            g_string_append (text, " ...");
        if (e->status != 0)
            g_string_append (text, ANSI_RESET);
        g_string_append_c (text, '\n');
        g_array_append_val (q->rows, index);
        g_free (status);

        // all of its text, a line under the other, each a line of History of that entry
        if (e == q->expanded)
        {
            char **lines = g_strsplit (e->text, "\n", -1);
            char **l;

            for (l = lines; *l != NULL; l++)
            {
                if (l[1] == NULL && **l == '\0')
                    break;
                g_string_append_printf (text, "    %s\n", *l);
                g_array_append_val (q->rows, index);
            }
            g_strfreev (lines);
        }
    }
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

        g_string_append_printf (data, "%s\t%s\t%d\t%ld\t%s\n", e->when,
                                g_ascii_dtostr (seconds, sizeof (seconds), e->seconds), e->status,
                                e->line, text);
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
    q->expanded = NULL;
    g_ptr_array_set_size (q->history, 0);
    if (q->history_path == NULL || !g_file_get_contents (q->history_path, &data, NULL, NULL))
        return;
    lines = g_strsplit (data, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        char **f = g_strsplit (lines[i], "\t", 5);

        if (g_strv_length (f) == 5)
        {
            query_entry_t *e = g_new0 (query_entry_t, 1);

            e->when = g_strdup (f[0]);
            e->seconds = g_ascii_strtod (f[1], NULL);
            e->status = (int) g_ascii_strtoll (f[2], NULL, 10);
            e->line = (long) g_ascii_strtoll (f[3], NULL, 10);
            e->text = g_strcompress (f[4]);
            g_ptr_array_add (q->history, e);
        }
        g_strfreev (f);
    }
    g_strfreev (lines);
    g_free (data);
    if (q->history->len > QUERY_HISTORY_MAX)
        g_ptr_array_remove_range (q->history, 0, q->history->len - QUERY_HISTORY_MAX);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_history_add (query_t *q, const char *when, double seconds, int status)
{
    query_entry_t *e = g_new0 (query_entry_t, 1);

    e->when = g_strdup (when);
    e->seconds = seconds;
    e->status = status;
    e->line = q->first;
    e->text = g_strdup (q->text);
    g_ptr_array_add (q->history, e);
    if (q->history->len > QUERY_HISTORY_MAX)
    {
        if (q->expanded == g_ptr_array_index (q->history, 0))
            q->expanded = NULL;
        g_ptr_array_remove_index (q->history, 0);
    }
    query_history_save (q);
    query_history_show (q, e);
}

/* --------------------------------------------------------------------------------------------- */
/* The run */
/* --------------------------------------------------------------------------------------------- */

static void
query_child_setup (gpointer data)
{
    (void) data;
    // a session of its own: stopped as a whole, with what mongosh starts
    (void) setsid ();
}

/* --------------------------------------------------------------------------------------------- */

static void
query_close_fds (query_t *q)
{
    int i;

    for (i = 0; i < 2; i++)
        if (q->fds[i] >= 0)
        {
            delete_select_channel (q->fds[i]);
            close (q->fds[i]);
            q->fds[i] = -1;
        }
}

/* --------------------------------------------------------------------------------------------- */

/* The status of mongosh, waited for a while after its output has ended, then killed */
static int
query_reap (query_t *q)
{
    int status = 0, i;

    for (i = 0; i < 200 && waitpid (q->pid, &status, WNOHANG) == 0; i++)
        g_usleep (10000);
    if (i == 200)
    {
        (void) kill (-q->pid, SIGKILL);
        (void) waitpid (q->pid, &status, 0);
    }
    g_spawn_close_pid (q->pid);
    q->pid = 0;
    return status;
}

/* --------------------------------------------------------------------------------------------- */

/* The run of mongosh ended at once, whatever it was doing */
static void
query_kill (query_t *q)
{
    if (q->pid == 0)
        return;
    query_close_fds (q);
    // nobody waits for its answer: no time given to end by itself
    (void) kill (-q->pid, SIGKILL);
    (void) waitpid (q->pid, NULL, 0);
    g_spawn_close_pid (q->pid);
    q->pid = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* What mongosh printed, valid UTF-8 */
static char *
query_take (GString *s)
{
    char *text = g_utf8_make_valid (s->str, (gssize) s->len);

    g_string_truncate (s, 0);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_table_say (query_t *q, const char *text)
{
    query_tab_set (q, QUERY_TAB_TABLE, text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The documents of the value mongosh --json printed in the tab Table */
static void
query_table_show (query_t *q, const char *json)
{
    GError *error = NULL;
    guint rows = 0;
    char *table = query_table (json, &rows, &error);
    char *text;

    if (table == NULL)
    {
        text = g_strdup_printf (_ ("mongosh did not answer with JSON: %s\n"), error->message);
        g_error_free (error);
        query_table_say (q, text);
        g_free (text);
        return;
    }
    if (rows == 0)
        text = g_strdup (_ ("No documents.\n"));
    else
        text = g_strdup_printf (ngettext ("%s%u document%s\n\n%s", "%s%u documents%s\n\n%s", rows),
                                ANSI_BOLD, rows, ANSI_RESET, table);
    // documents to be read in columns, not a number counted
    query_tab_set (q, QUERY_TAB_TABLE, text, *json == '[' && rows > 0);
    g_free (text);
    g_free (table);
}

/* --------------------------------------------------------------------------------------------- */

/* The output of the query, and the line of a SyntaxError marked and gone to */
static void
query_output_done (query_t *q, const char *out, const char *err, int status)
{
    const double seconds = (double) (g_get_monotonic_time () - q->started) / G_USEC_PER_SEC;
    const size_t len = strlen (q->text);
    // the newline the last line ends with starts no line of the query
    const long last =
        q->first + query_line_of (q->text, len > 0 && q->text[len - 1] == '\n' ? len - 1 : len);
    char when[16];
    const time_t now = time (NULL);
    GString *text = g_string_new (NULL);
    long line, column;

    (void) strftime (when, sizeof (when), "%H:%M:%S", localtime (&now));
    g_string_append (text, ANSI_BOLD);
    if (last > q->first)
        g_string_append_printf (text, _ ("lines %ld-%ld"), q->first, last);
    else
        g_string_append_printf (text, _ ("line %ld"), q->first);
    g_string_append_printf (text, "   %.2f s   %s   %s   ", seconds, when, q->endpoint);
    if (q->use != NULL)
        g_string_append_printf (text, "use(%s)   ", q->use);
    if (q->limited)
        g_string_append_printf (text, _ ("limit %ld   "), q->limit);
    else if (q->limit > 0)
        g_string_append (text, _ ("@limit: no find () or aggregate ([...]) to limit   "));
    if (q->stopped)
        g_string_append (text, _ ("stopped"));
    else if (WIFSIGNALED (status))
        g_string_append_printf (text, _ ("killed by signal %d"), WTERMSIG (status));
    else
        g_string_append_printf (text, _ ("exit %d"), WEXITSTATUS (status));
    g_string_append (text, ANSI_RESET "\n\n");
    g_string_append (text, out);
    if (q->cut)
        g_string_append_printf (text, ANSI_RED "\n%s\n" ANSI_RESET,
                                _ ("... mongosh printed more than 8 MB: the rest is cut."));
    if (*err != '\0')
    {
        if (*out != '\0' && text->str[text->len - 1] != '\n')
            g_string_append_c (text, '\n');
        g_string_append_printf (text, ANSI_RED "%s" ANSI_RESET, err);
    }
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, TRUE);
    g_string_free (text, TRUE);

    if (q->file != NULL && query_mongo_error_position (err, &line, &column))
    {
        void *edit = q->host->window_top_file (q->host);

        line += q->first - 1;
        q->host->set_marker (q->host, q->file, line, q->mark, TRUE);
        if (edit != NULL)
            (void) q->host->go_to (q->host, edit, q->file, line, column);
    }
    // killed by a signal, not stopped: the code a shell tells for it
    query_history_add (q, when, seconds,
                       q->stopped                 ? -1
                           : WIFSIGNALED (status) ? 128 + WTERMSIG (status)
                                                  : WEXITSTATUS (status));
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh has ended: what it printed shown, and the documents in the table when asked for */
static void
query_done (query_t *q, int status)
{
    char *raw = query_take (q->out), *out = query_mongo_clean (raw);
    char *err = query_take (q->err);
    const int code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    // the value mongosh --json printed, after what print () printed; an error is such a value
    const char *tail = q->table ? query_mongo_json_tail (out) : NULL;
    char *message = query_mongo_json_error (tail);
    char *json = NULL;

    g_free (raw);
    if (message != NULL)
    {
        char *both = g_strconcat (err, message, "\n", (char *) NULL);

        out[tail - out] = '\0';
        g_free (err);
        err = both;
    }
    else if (tail != NULL)
        json = g_strdup (tail);
    query_output_done (q, out, err, status);
    if (!q->table)
        query_table_say (q, _ ("A line \"// @table\" in the query asks for its table.\n"));
    else if (q->stopped)
        query_table_say (q, _ ("The query was stopped.\n"));
    else if (message != NULL && query_mongo_is_cursor (message))
        query_table_say (q,
                         _ ("The query ends with a cursor: .toArray() after it makes the "
                            "table.\n"));
    else if (message != NULL || code != 0)
        query_table_say (q, _ ("The query failed: Output tells why.\n"));
    else if (q->cut)
        query_table_say (q, _ ("mongosh printed more than 8 MB: no table of a part of it.\n"));
    else
        query_table_show (q, json != NULL ? json : out);
    g_free (json);
    g_free (message);
    g_free (out);
    g_free (err);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

static int
query_output_ready (int fd, void *data)
{
    query_t *q = (query_t *) data;
    const int which = fd == q->fds[0] ? 0 : 1;
    GString *s = which == 0 ? q->out : q->err;
    char buf[8192];
    const ssize_t n = read (fd, buf, sizeof (buf));

    if (n > 0)
    {
        if (s->len + (gsize) n > QUERY_OUTPUT_MAX)
            q->cut = TRUE;
        if (s->len < QUERY_OUTPUT_MAX)
            g_string_append_len (s, buf, MIN (n, (ssize_t) (QUERY_OUTPUT_MAX - s->len)));
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
    {
        delete_select_channel (fd);
        close (fd);
        q->fds[which] = -1;
        if (q->fds[0] < 0 && q->fds[1] < 0)
            query_done (q, query_reap (q));
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh run on the query, in an --eval of its own after the prefix, so that the lines of a
   SyntaxError are those of the query; --json for the table */
static void
query_start (query_t *q)
{
    char *used = query_mongo_use (q->text);
    // the database of the use () above a part run alone, in the prefix: the lines stay the same
    char *prefix = q->use != NULL
        ? g_strdup_printf ("%s db = db.getSiblingDB(%s);", query_mongo_prefix (), q->use)
        : g_strdup (query_mongo_prefix ());
    char *text =
        q->limit > 0 ? query_mongo_with_limit (used, q->limit, &q->limited) : g_strdup (used);
    const char *argv[10];
    int n = 0, in, out, err, i;
    GError *error = NULL;

    g_free (used);
    if (q->table)
    {
        char *array = query_mongo_for_table (text);

        g_free (text);
        text = array;
    }
    // what goes to mongosh, ".toArray()" too, and a query of History, which was not looked at
    if (strlen (text) > QUERY_TEXT_MAX)
    {
        query_tab_set (q, QUERY_TAB_OUTPUT,
                       _ ("The query is longer than mongosh takes (128 KB).\n"), TRUE);
        query_table_say (q, "");
        g_free (text);
        g_free (prefix);
        return;
    }

    argv[n++] = q->mongosh;
    argv[n++] = "--nodb";
    argv[n++] = "--quiet";
    if (q->table)
        argv[n++] = "--json=relaxed";
    argv[n++] = "--eval";
    argv[n++] = prefix;
    argv[n++] = "--eval";
    argv[n++] = text;
    argv[n] = NULL;

    // the memory of a big answer not kept for this one
    for (i = 0; i < 2; i++)
    {
        GString **s = i == 0 ? &q->out : &q->err;

        if ((*s)->allocated_len > 64 * 1024)
        {
            g_string_free (*s, TRUE);
            *s = g_string_new (NULL);
        }
        g_string_truncate (*s, 0);
    }
    q->cut = FALSE;
    if (!g_spawn_async_with_pipes (NULL, (char **) argv, NULL,
                                   G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                                   query_child_setup, NULL, &q->pid, &in, &out, &err, &error))
    {
        char *message = g_strdup_printf ("%s: %s\n", q->mongosh, error->message);

        g_clear_error (&error);
        q->pid = 0;
        query_tab_set (q, QUERY_TAB_OUTPUT, message, TRUE);
        query_table_say (q, "");
        g_free (message);
        g_free (text);
        g_free (prefix);
        return;
    }
    g_free (text);
    g_free (prefix);
    // nothing to read from the terminal of coole
    close (in);
    q->fds[0] = out;
    q->fds[1] = err;
    for (i = 0; i < 2; i++)
    {
        (void) fcntl (q->fds[i], F_SETFL, fcntl (q->fds[i], F_GETFL) | O_NONBLOCK);
        add_select_channel (q->fds[i], query_output_ready, q);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Stop the query: what it printed until then is shown */
static void
query_stop (query_t *q)
{
    if (q->pid == 0)
        return;
    q->stopped = TRUE;
    (void) kill (-q->pid, SIGTERM);
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
    char *text, *block = NULL, *use = NULL;
    gsize len = 0;
    long first = 0;
    off_t start, end;
    gboolean selected = FALSE, sectioned = FALSE;

    if (q->pid != 0)
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
        block = g_strndup (text + start, (gsize) (end - start));
        first = query_line_of (text, (gsize) start);
        selected = TRUE;
    }
    else
    {
        // a script is run as a whole, its lines using what those before made; "// ---" parts it
        block = query_section (text, e->buffer.curs_line, &first);
        sectioned = block != NULL;
        if (block == NULL)
            block = g_strdup (text);
    }
    // a part run alone is in the database of the use ("name") above it
    if (first > 0)
        use = query_use_before (text, first);
    g_free (text);

    if (query_blank (block))
    {
        q->host->message (q->host, D_NORMAL, _ ("Query"),
                          selected        ? _ ("The selection is empty.")
                              : sectioned ? _ ("The part of the file between the lines \"// ---\"\n"
                                               "around the cursor is empty.")
                                          : _ ("The file is empty."));
        g_free (block);
        g_free (use);
        return;
    }
    query_run_text (q, block, q->host->get_current_file (q->host, edit), first + 1, use);
}

/* --------------------------------------------------------------------------------------------- */

/* Run @text, from the line @first, from 1, of @file; NULL for a query not of the file as it is
   now, whose errors are not marked in it; in the database of @use, the string of a use () above
   it, NULL for none.  All are taken. */
static void
query_run_text (query_t *q, char *text, char *file, long first, char *use)
{
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
    q->table = query_wants_table (text);
    q->limit = query_wants_limit (text);
    q->limited = FALSE;
    q->stopped = FALSE;
    q->started = g_get_monotonic_time ();
    q->host->clear_markers (q->host, NULL, q->mark);
    query_tab_set (q, QUERY_TAB_OUTPUT, _ ("Running...\n"), TRUE);
    query_table_say (q, _ ("Running...\n"));
    query_start (q);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */
/* What History does */
/* --------------------------------------------------------------------------------------------- */

/* F5 in History: its query run again as it was, the file left as it is now */
static void
query_history_run (query_t *q, const query_entry_t *e)
{
    if (q->pid != 0)
    {
        query_run (q, NULL, FALSE);
        return;
    }
    query_run_text (q, g_strdup (e->text), NULL, e->line, NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* F3 in History: all the text of its query under its line, or no more */
static void
query_history_expand (query_t *q, const query_entry_t *e)
{
    q->expanded = q->expanded == e ? NULL : e;
    query_history_show (q, e);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

/* Enter in History: its query put into the file, a section of its own after the section of the
   cursor, the cursor on it, the file in front to run it with F5 */
static void
query_history_insert (query_t *q, const query_entry_t *e)
{
    void *edit = q->host->window_top_file (q->host);
    WEdit *ed;
    char *text, *put, *file;
    gsize len = 0, offset = 0;
    long n = 0;

    if (edit == NULL)
        return;
    ed = EDIT (edit);
    text = q->host->get_text (q->host, edit, &len);
    if (text == NULL)
        return;
    put = query_put_section (text, ed->buffer.curs_line, e->text, &offset, &n);
    g_free (text);

    edit_cursor_move (ed, (off_t) offset - ed->buffer.curs1);
    q->host->insert_text (q->host, edit, put, 0);
    g_free (put);

    file = q->host->get_current_file (q->host, edit);
    if (file == NULL || !q->host->go_to (q->host, edit, file, n + 1, 0))
    {
        edit_move_to_line (ed, n);
        q->host->window_show (q->host, edit);
    }
    g_free (file);
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
    const query_entry_t *e;

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
        e = edit == NULL ? query_history_current (q, TRUE) : NULL;
        if (e != NULL)
            query_history_run (q, e);
        else
            query_run (q, edit, FALSE);
        return TRUE;
    case QUERY_CMD_RUN_ALL:
        query_run (q, edit, TRUE);
        return TRUE;
    case QUERY_CMD_STOP:
        query_stop (q);
        return TRUE;
    case QUERY_CMD_HISTORY_INSERT:
    case QUERY_CMD_HISTORY_SHOW:
        // Enter and F3 of a file are those of the editor
        e = !key || edit == NULL ? query_history_current (q, key) : NULL;
        if (e == NULL)
            return FALSE;
        if (cmd == QUERY_CMD_HISTORY_INSERT)
            query_history_insert (q, e);
        else
            query_history_expand (q, e);
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

static mc_ep_result_t
query_handle_action (void *data, long command, void *edit)
{
    query_t *q = (query_t *) data;

    return query_run_command (q, query_command (q, command), edit, FALSE) ? MC_EPR_OK
                                                                          : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

/* coole -Q, once the editor runs: the tabs at the bottom, the history of the file */
static void
query_startup (void *data)
{
    query_t *q = (query_t *) data;
    void *edit = q->host->window_top_file (q->host);
    char *file = edit != NULL ? q->host->get_current_file (q->host, edit) : NULL;
    char *found = g_find_program_in_path (q->mongosh);
    GString *text = g_string_new (NULL);

    query_history_load (q, file);
    g_free (file);

    g_string_append_printf (text, _ ("Database: %s\n"), q->endpoint);
    if (found != NULL)
        g_string_append_printf (text, "mongosh: %s\n\n", found);
    else
        g_string_append_printf (text, ANSI_RED "%s\n\n" ANSI_RESET,
                                _ ("mongosh is not found: MC_MONGOSH or PATH should have it."));
    g_string_append (text,
                     _ ("F5 runs the selection, else the part between the lines \"// ---\"\n"
                        "around the cursor, else the whole file; Ctrl-F5 the whole file.\n"
                        "A line \"// @table\" in the query makes the tab Table of the\n"
                        "documents it ends with.\n"));
    // the tabs in their order, the first one seen
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, FALSE);
    query_table_say (q, _ ("No query run yet.\n"));
    query_history_show (q, NULL);
    query_viewer_id (q, "show", q->tabs[QUERY_TAB_OUTPUT], FALSE);
    g_string_free (text, TRUE);
    g_free (found);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

static void *
query_open (mc_editor_host_t *host, void *editor_dialog)
{
    query_t *q = g_new0 (query_t, 1);
    const char *kind = host->startup_option (host, "query");
    const char *mongosh = g_getenv ("MC_MONGOSH");
    int i;

    (void) editor_dialog;
    q->host = host;
    q->fds[0] = q->fds[1] = -1;
    q->out = g_string_new (NULL);
    q->err = g_string_new (NULL);
    q->history = g_ptr_array_new_with_free_func (query_entry_free);
    q->rows = g_array_new (FALSE, FALSE, sizeof (guint));
    q->mongosh = g_strdup (mongosh != NULL && *mongosh != '\0' ? mongosh : "mongosh");
    q->endpoint = query_mongo_endpoint (g_getenv ("MC_MONGO_URI"), g_getenv ("MC_MONGO_DB"));
    host->commands_register (host, QUERY_KEYMAP_SECTION, N_ ("&Query"), query_commands);
    for (i = 0; i < QUERY_CMD_COUNT; i++)
        q->commands[i] = host->command_id (host, query_commands[i].name);
    q->mark = host->marker_kind (host, &query_mark_kind);
    q->on = kind != NULL && strcmp (kind, "mongo") == 0;
    if (q->on)
        host->call_later (host, query_startup, q);
    return q;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_close (void *data)
{
    query_t *q = (query_t *) data;

    query_kill (q);
    g_ptr_array_free (q->history, TRUE);
    g_array_free (q->rows, TRUE);
    g_string_free (q->out, TRUE);
    g_string_free (q->err, TRUE);
    g_free (q->history_path);
    g_free (q->history_file);
    g_free (q->mongosh);
    g_free (q->endpoint);
    g_free (q->text);
    g_free (q->file);
    g_free (q->use);
    g_free (q);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_ok_to_quit (void *data)
{
    query_t *q = (query_t *) data;

    return q->pid == 0
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
