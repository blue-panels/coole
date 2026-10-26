/*
   The query plugin: the queries of MongoDB run by mongosh.

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

/** \file query-mongo.c
 *  \brief Source: the backend of the query plugin for MongoDB
 *
 *  A query is given to mongosh, a run of its own each, and what it printed is Output; the arrays
 *  printjson () and print () printed are the tables of Table.  A query with a line "// @table" is
 *  run with --json, its value made a table too.
 *
 *  mongosh is given what to connect to by the environment of coole, which it inherits:
 *  MC_MONGO_URI, MC_MONGO_DB; MC_MONGOSH is the program, else mongosh of PATH.  Neither the URI
 *  nor the password in it goes anywhere else: not to the screen, not to the history.
 */

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "lib/global.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

#include "query-core.h"
#include "query-int.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// the language server of the queries lsp.json has, and where it comes from
#define QUERY_LANGUAGE_SERVER     "mongolsp"
#define QUERY_LANGUAGE_SERVER_URL "https://github.com/blue-panels/mongolsp"

/* What AI is told of the database: its collections, the fields of a document of each with their
   types, never a value; the database of the use () of the request, @1 */
#define QUERY_AI_SCRIPT                                                                            \
    "db = connect(process.env.MC_MONGO_URI); "                                                     \
    "if (process.env.MC_MONGO_DB) db = db.getSiblingDB(process.env.MC_MONGO_DB); %s"               \
    "{ const __t = (v) => v === null ? 'null' : Array.isArray(v) ? 'array' "                       \
    ": (v && v.constructor && v.constructor.name) || typeof v; "                                   \
    "const __n = db.getCollectionNames().filter((n) => !n.startsWith('system.')).sort(); "         \
    "print('The database \"' + db.getName() + '\" of MongoDB ' + db.version() + '.'); "            \
    "print('Its collections, a line each: the name: the fields of a document of it, "              \
    "with their types.'); "                                                                        \
    "for (const n of __n.slice(0, %d)) { const d = db.getCollection(n).findOne(); "                \
    "print(n + ': ' + (d ? Object.keys(d).map((k) => k + ' ' + __t(d[k])).join(', ') "             \
    ": 'no document')); } "                                                                        \
    "if (__n.length > %d) print('And ' + (__n.length - %d) + ' collections more, not told.'); }"

// the collections AI is told of at most
#define QUERY_AI_COLLECTIONS 300

/*** file scope type declarations ****************************************************************/

typedef struct
{
    query_t *q;
    char *mongosh;

    // the run going on, or the last one
    query_proc_t *proc;  // mongosh, NULL when none runs
    gboolean cut;        // mongosh printed more than QUERY_OUTPUT_MAX: the rest is not kept
    gboolean table;      // asked for by "// @table": mongosh --json, its answer in columns too
    long limit;          // asked for by "// @limit N", 0 when not
    gboolean limited;    // and put into the query

    query_proc_t *ai;  // mongosh reading what AI is told of the database, NULL when none
} query_mongo_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* One table of the tab Table: its number, what it is, how many documents, the first
   QUERY_TABLE_ROWS of them; FALSE when @json is no JSON, said so */
static gboolean
query_table_add (GString *text, guint n, const char *what, const char *json)
{
    GError *error = NULL;
    guint rows = 0;
    char *table = query_table (json, QUERY_TABLE_ROWS, &rows, &error);

    g_string_append_printf (text, ANSI_BOLD "%u. %s: " ANSI_RESET, n, what);
    if (table == NULL)
    {
        g_string_append_printf (text, _ ("no JSON: %s\n\n"), error->message);
        g_error_free (error);
        return FALSE;
    }
    if (rows == 0)
        g_string_append (text, _ ("no documents\n\n"));
    else if (rows > QUERY_TABLE_ROWS)
        g_string_append_printf (text, _ ("the first %u of %u documents\n\n%s\n"), QUERY_TABLE_ROWS,
                                rows, table);
    else
        g_string_append_printf (
            text, ngettext ("%u document\n\n%s\n", "%u documents\n\n%s\n", rows), rows, table);
    g_free (table);
    return rows > 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The tab Table: a table of each array printjson () and print () printed, in their order, then of
   the value the query ends with, @json, for "// @table"; @why when that value is none.  In front
   when it has documents. */
static void
query_tables_show (query_t *q, GPtrArray *tables, const char *json, const char *why)
{
    GString *text = g_string_new (NULL);
    gboolean rows = FALSE;
    guint i;

    for (i = 0; i < tables->len; i++)
        rows = query_table_add (text, i + 1, _ ("printed"), g_ptr_array_index (tables, i)) || rows;
    if (json != NULL)
        rows = query_table_add (text, tables->len + 1, _ ("the value at the end"), json) || rows;
    else if (why != NULL)
        g_string_append (text, why);
    if (text->len == 0)
        g_string_append (text,
                         _ ("No table: printjson () of an array or a cursor makes one, and "
                            "a line \"// @table\" one of the value the query ends with.\n"));
    query_tab_set (q, QUERY_TAB_TABLE, text->str, rows);
    g_string_free (text, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* The output of the query, and the line of a SyntaxError marked and gone to */
static void
query_output_done (query_mongo_t *m, const char *out, const char *err, int status)
{
    query_t *q = m->q;
    const double seconds = (double) (g_get_monotonic_time () - q->started) / G_USEC_PER_SEC;
    char when[16];
    const time_t now = time (NULL);
    GString *text = g_string_new (NULL);
    long line, column;

    (void) strftime (when, sizeof (when), "%H:%M:%S", localtime (&now));
    query_output_head (q, text, seconds, when);
    if (q->use != NULL)
        g_string_append_printf (text, "use(%s)   ", q->use);
    if (m->limited)
        g_string_append_printf (text, _ ("limit %ld   "), m->limit);
    else if (m->limit > 0)
        g_string_append (text, _ ("@limit: no find () or aggregate ([...]) to limit   "));
    if (q->stopped)
        g_string_append (text, _ ("stopped"));
    else if (WIFSIGNALED (status))
        g_string_append_printf (text, _ ("killed by signal %d"), WTERMSIG (status));
    else
        g_string_append_printf (text, _ ("exit %d"), WEXITSTATUS (status));
    g_string_append (text, ANSI_RESET "\n\n");
    g_string_append (text, out);
    if (m->cut)
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

    if (query_mongo_error_position (err, &line, &column))
        query_mark_error (q, line, column);
    // killed by a signal, not stopped: the code a shell tells for it
    query_history_add (q, when, seconds,
                       q->stopped                 ? -1
                           : WIFSIGNALED (status) ? 128 + WTERMSIG (status)
                                                  : WEXITSTATUS (status),
                       NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh has ended: what it printed shown, and the documents in the table when asked for */
static void
query_done (query_mongo_t *m, char *raw, char *err, int status)
{
    query_t *q = m->q;
    char *clean = query_mongo_clean (raw);
    // the copies of the arrays printed, for the tab Table, not shown in Output
    GPtrArray *tables = g_ptr_array_new_with_free_func (g_free);
    char *out = query_take_tables (clean, tables);
    const int code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    // the value mongosh --json printed, after what print () printed; an error is such a value
    const char *tail = m->table ? query_mongo_json_tail (out) : NULL;
    char *message = query_mongo_json_error (tail);
    char *json = NULL;

    g_free (raw);
    g_free (clean);
    if (message != NULL)
    {
        char *both = g_strconcat (err, message, "\n", (char *) NULL);

        out[tail - out] = '\0';
        g_free (err);
        err = both;
    }
    else if (tail != NULL)
        json = g_strdup (tail);
    query_output_done (m, out, err, status);
    if (m->cut)
        // copies cut too: none of them
        g_ptr_array_set_size (tables, 0);
    if (!m->table)
        query_tables_show (q, tables, NULL, NULL);
    else if (q->stopped)
        query_tables_show (q, tables, NULL, _ ("The query was stopped.\n"));
    else if (message != NULL && query_mongo_is_cursor (message))
        query_tables_show (q, tables, NULL,
                           _ ("The query ends with a cursor: .toArray() after it makes the "
                              "table.\n"));
    else if (message != NULL || code != 0)
        query_tables_show (q, tables, NULL, _ ("The query failed: Output tells why.\n"));
    else if (m->cut)
        query_tables_show (q, tables, NULL,
                           _ ("mongosh printed more than 8 MB: no table of a part of it.\n"));
    else if (json == NULL)
        query_tables_show (q, tables, NULL,
                           _ ("The query ends with no value for the table: printjson () and "
                              "print () print, and have none.\n"));
    else
        query_tables_show (q, tables, json, NULL);
    g_ptr_array_free (tables, TRUE);
    g_free (json);
    g_free (message);
    g_free (out);
    g_free (err);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh has ended: query_proc_done_fn */
static void
query_mongo_proc_done (char *out, char *err, int status, gboolean cut, void *data)
{
    query_mongo_t *m = (query_mongo_t *) data;

    m->proc = NULL;
    m->cut = cut;
    query_done (m, out, err, status);
}

/* --------------------------------------------------------------------------------------------- */

/* The file of queries of @edit without a use ("name"): use("db") put at its top, the database
   of -Q, for the language server, which knows the database of the collections it completes by
   it alone; the file is changed, to be saved or not.  The name of the database when it was put
   there, else NULL. */
static char *
query_use_put (query_t *q, void *edit)
{
    char *db = query_mongo_db (g_getenv ("MC_MONGO_URI"), g_getenv ("MC_MONGO_DB"));
    char *text, *line;
    gsize len = 0;
    WEdit *e;
    off_t at;

    if (edit == NULL || db == NULL)
    {
        g_free (db);
        return NULL;
    }
    text = q->host->get_text (q->host, edit, &len);
    if (text == NULL || query_has_use (text))
    {
        g_free (text);
        g_free (db);
        return NULL;
    }
    g_free (text);

    // at the top, the cursor staying on the text it was on
    e = EDIT (edit);
    at = e->buffer.curs1;
    line = query_use_line (db);
    edit_cursor_move (e, -e->buffer.curs1);
    q->host->insert_text (q->host, edit, line, 0);
    edit_cursor_move (e, at);
    g_free (line);
    return db;
}

/* --------------------------------------------------------------------------------------------- */
/* What the plugin asks of the backend */
/* --------------------------------------------------------------------------------------------- */

static void *
query_mongo_open (query_t *q)
{
    query_mongo_t *m = g_new0 (query_mongo_t, 1);
    const char *mongosh = g_getenv ("MC_MONGOSH");

    m->q = q;
    m->mongosh = g_strdup (mongosh != NULL && *mongosh != '\0' ? mongosh : "mongosh");
    q->endpoint = query_mongo_endpoint (g_getenv ("MC_MONGO_URI"), g_getenv ("MC_MONGO_DB"));
    return m;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_mongo_close (void *data)
{
    query_mongo_t *m = (query_mongo_t *) data;

    if (m->proc != NULL)
        query_proc_kill (m->proc);
    if (m->ai != NULL)
        query_proc_kill (m->ai);
    g_free (m->mongosh);
    g_free (m);
}

/* --------------------------------------------------------------------------------------------- */

/* What is found, mongosh and the language server, and use ("db") put in the file */
static void
query_mongo_startup (void *data, void *edit, GString *text)
{
    query_mongo_t *m = (query_mongo_t *) data;
    query_t *q = m->q;
    char *found = g_find_program_in_path (m->mongosh);
    // the language server of the queries, of lsp.json
    char *server = g_find_program_in_path (QUERY_LANGUAGE_SERVER);
    char *used = query_use_put (q, edit);

    g_string_append_printf (text, _ ("Database: %s\n"), q->endpoint);
    if (found != NULL)
        g_string_append_printf (text, "mongosh: %s\n", found);
    else
        g_string_append_printf (text, ANSI_RED "%s\n" ANSI_RESET,
                                _ ("mongosh is not found: MC_MONGOSH or PATH should have it."));
    if (server != NULL)
        g_string_append_printf (text, "%s: %s\n", QUERY_LANGUAGE_SERVER, server);
    else
        g_string_append_printf (text, _ ("%s is not found: no completion of the queries (%s)\n"),
                                QUERY_LANGUAGE_SERVER, QUERY_LANGUAGE_SERVER_URL);
    query_ai_startup (q, text);
    g_string_append_c (text, '\n');
    if (used != NULL)
        g_string_append_printf (
            text,
            _ ("use(\"%s\") is put at the top of the file: the language server\n"
               "completes the collections of that database.\n\n"),
            used);
    g_string_append (text,
                     _ ("F5 runs the selection, else the part between the lines \"// ---\"\n"
                        "around the cursor, else the whole file; Ctrl-F5 the whole file.\n"
                        "A line \"// @table\" in the query makes the tab Table of the\n"
                        "documents it ends with.\n"));
    g_free (found);
    g_free (server);
    g_free (used);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_mongo_running (void *data)
{
    return ((query_mongo_t *) data)->proc != NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh run on the query, in an --eval of its own after the prefix, so that the lines of a
   SyntaxError are those of the query; --json for the table */
static void
query_mongo_start (void *data)
{
    query_mongo_t *m = (query_mongo_t *) data;
    query_t *q = m->q;
    char *used = query_mongo_use (q->text);
    // the database of the use () above a part run alone, in the prefix: the lines stay the same
    char *prefix = q->use != NULL
        ? g_strdup_printf ("%s db = db.getSiblingDB(%s);", query_mongo_prefix (), q->use)
        : g_strdup (query_mongo_prefix ());
    char *text;
    const char *argv[10];
    int n = 0;
    GError *error = NULL;

    m->table = query_wants_table (q->text);
    m->limit = query_wants_limit (q->text);
    m->limited = FALSE;
    text = m->limit > 0 ? query_mongo_with_limit (used, m->limit, &m->limited) : g_strdup (used);
    g_free (used);
    if (m->table)
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

    argv[n++] = m->mongosh;
    argv[n++] = "--nodb";
    argv[n++] = "--quiet";
    if (m->table)
        argv[n++] = "--json=relaxed";
    argv[n++] = "--eval";
    argv[n++] = prefix;
    argv[n++] = "--eval";
    argv[n++] = text;
    argv[n] = NULL;

    m->cut = FALSE;
    // nothing to read from the terminal of coole
    m->proc = query_proc_start (argv, NULL, NULL, "", query_mongo_proc_done, m, &error);
    if (m->proc == NULL)
    {
        char *message = g_strdup_printf ("%s: %s\n", m->mongosh, error->message);

        g_clear_error (&error);
        query_tab_set (q, QUERY_TAB_OUTPUT, message, TRUE);
        query_table_say (q, "");
        g_free (message);
    }
    g_free (text);
    g_free (prefix);
}

/* --------------------------------------------------------------------------------------------- */

/* Stop the query: what it printed until then is shown */
static void
query_mongo_stop (void *data)
{
    query_mongo_t *m = (query_mongo_t *) data;

    if (m->proc == NULL)
        return;
    m->q->stopped = TRUE;
    query_proc_stop (m->proc);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_mongo_ai_done (char *out, char *err, int status, gboolean cut, void *data)
{
    query_mongo_t *m = (query_mongo_t *) data;
    char *clean = query_mongo_clean (out);

    (void) cut;
    m->ai = NULL;
    g_strstrip (clean);
    if (!WIFEXITED (status) || WEXITSTATUS (status) != 0 || *clean == '\0')
        query_ai_context (m->q, NULL, g_strstrip (err));
    else
    {
        const char *read_only = g_getenv ("MC_QUERY_READ_ONLY");
        char *context = read_only != NULL && strcmp (read_only, "1") == 0
            ? g_strconcat (clean, "\nThe user reads only: write no change.", (char *) NULL)
            : g_strdup (clean);

        query_ai_context (m->q, context, NULL);
        g_free (context);
    }
    g_free (clean);
    g_free (out);
    g_free (err);
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh run on the script that reads what AI is told, in the database of the request */
static void
query_mongo_ai_context (void *data, const char *use)
{
    query_mongo_t *m = (query_mongo_t *) data;
    char *used = use != NULL ? g_strdup_printf ("db = db.getSiblingDB(%s); ", use) : g_strdup ("");
    char *script = g_strdup_printf (QUERY_AI_SCRIPT, used, QUERY_AI_COLLECTIONS,
                                    QUERY_AI_COLLECTIONS, QUERY_AI_COLLECTIONS);
    const char *argv[] = { m->mongosh, "--nodb", "--quiet", "--eval", script, NULL };
    GError *error = NULL;

    if (m->ai != NULL)
        query_proc_kill (m->ai);
    m->ai = query_proc_start (argv, NULL, NULL, "", query_mongo_ai_done, m, &error);
    if (m->ai == NULL)
    {
        char *message = g_strdup_printf ("%s: %s", m->mongosh, error->message);

        g_clear_error (&error);
        query_ai_context (m->q, NULL, message);
        g_free (message);
    }
    g_free (script);
    g_free (used);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const query_backend_t query_mongo_backend = {
    .id = "mongo",
    .extension = ".js",
    .sections = TRUE,
    .open = query_mongo_open,
    .close = query_mongo_close,
    .startup = query_mongo_startup,
    .context = query_use_before,
    .running = query_mongo_running,
    .start = query_mongo_start,
    .stop = query_mongo_stop,
    .language = "JavaScript of mongosh, db being the database below: db.name.find (), "
                ".aggregate ([...]) and the like, no use ()",
    .comment = "// ",
    .ai_context = query_mongo_ai_context,
};

/* --------------------------------------------------------------------------------------------- */
