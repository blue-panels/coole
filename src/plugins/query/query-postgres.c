/*
   The query plugin: the queries of PostgreSQL run by libpq.

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

/** \file query-postgres.c
 *  \brief Source: the backend of the query plugin for PostgreSQL
 *
 *  One connection for the whole of coole -Q, made when it starts: a transaction, the temporary
 *  tables and what SET sets stay from one F5 to the next.  Nothing of it waits: libpq is told not
 *  to block, and its socket is a select channel of the editor, for a read and, while libpq has
 *  something to send, for a write.
 *
 *  coole reads nothing of the connection: libpq takes it from the environment, PGHOST,
 *  PGDATABASE, PGSERVICE and the like, the password from ~/.pgpass.  What coole adds are
 *  parameters of the connection, not queries after it, which would block: the encoding, its
 *  name, and with MC_QUERY_READ_ONLY=1 transactions read-only.
 */

#include <config.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/tty/key.h"

#include "query-core.h"
#include "query-int.h"
#include "query-pq.h"
#include "query-pg-schema.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// what the language server of lsp.json is told to connect to, by its "requests"
#define QUERY_PG_LSP_URI    "COOLE_PG_URI"
#define QUERY_PG_LSP_SCHEMA "COOLE_PG_SCHEMA"
// the language server of SQL lsp.json has, and where it comes from
#define QUERY_PG_LANGUAGE_SERVER "postgres-language-server"
#define QUERY_PG_LANGUAGE_SERVER_URL                                                               \
    "https://github.com/supabase-community/postgres-language-server"
// the directory of the socket of libpq when no host is given, that of Debian and of Ubuntu
#define QUERY_PG_SOCKET_DIR "/var/run/postgresql"

// the rows libpq gives at once when it can give them by chunks, libpq 17
#define QUERY_PG_CHUNK 256
// the bytes of a value taken for its cell, which is cut at 40 columns anyway
#define QUERY_PG_CELL_MAX 256
// the hexadecimal digits of a bytea shown
#define QUERY_PG_BYTEA_SHOWN 16
// the type bytea, its Oid in pg_type
#define QUERY_PG_BYTEA_OID 17

/*** file scope type declarations ****************************************************************/

/* A cancel sent by a thread of its own, PQcancel () blocking: libpq older than 17 */
typedef struct
{
    const query_pq_t *pq;
    PGcancel *cancel;  // the thread's, freed by it
    int fds[2];        // a pipe: the thread writes its answer, the editor reads it
    char error[256];
} query_pg_cancel_t;

typedef enum
{
    QUERY_PG_OFF,         // no connection: libpq is not there, or none was made yet
    QUERY_PG_CONNECTING,  // PQconnectPoll () until it says OK or FAILED
    QUERY_PG_READY,
    QUERY_PG_BUSY,   // a query sent, its results read
    QUERY_PG_BROKEN  // it failed, or was lost: F5 connects again
} query_pg_state_t;

typedef struct
{
    query_t *q;
    const query_pq_t *pq;  // NULL when libpq is not there
    char *pq_error;        // why it is not

    query_pq_link_t link;
    query_pg_state_t state;
    gboolean made;       // a connection was made once
    gboolean reconnect;  // a connection made again: what the one before had is gone
    gboolean pending;    // F5 while it connected: the query runs once it is made

    gboolean read_only;  // MC_QUERY_READ_ONLY=1
    char *name;          // of the connection for the user: MC_PG_CONNECTION, else PGSERVICE
    char *head;          // what Output tells at the start, before a query is run
    char *relation;      // of MC_PG_RELATION, the query of its rows run once connected; or NULL
    query_pg_schema_t *schema;  // the window Schema, NULL without libpq
    gboolean ddl;  // a command changed the schemas: Schema is read again once committed

    // the query running, or the last one
    GString *out;       // Output after its head: notices, what each command did, the errors
    gboolean cut;       // more than QUERY_OUTPUT_MAX: the rest is not kept
    GString *tables;    // the tab Table: a table of the rows of each command that has some
    guint ntables;      // the tables in it
    gboolean rows;      // whether one of them has a row
    gboolean failed;    // an error came
    gboolean copy_out;  // COPY ... TO STDOUT: its rows read into Output
    gboolean copy_in;   // COPY ... FROM STDIN: told there is nothing to read, not taken yet
    gboolean lost;      // the connection was lost while it ran
    gboolean told;      // an error of libpq itself is in Output: the others after it say no more
    char *summary;      // for History: what the last command did, "UPDATE 4", else the error

    // the cancel of the query, sent while it runs
    gboolean canceling;
    gboolean canceled;          // the query was ended by it, 57014: not by itself before it came
    query_pg_cancel_t *cancel;  // by a thread, libpq older than 17
    PGcancelConn *cancel_conn;  // libpq 17
    int cancel_fd;              // its socket, or the pipe of the thread, watched; -1 for none

    // the rows of the command being read, row by row or by chunks
    GPtrArray *columns;  // their names, NULL when no rows came yet
    GPtrArray *cells;    // of the first QUERY_TABLE_ROWS rows; the rest only counted
    gint64 nrows;
    char *status;  // "SELECT 600": the last chunk has it, not the end of the rows after it
} query_pg_t;

/*** forward declarations (file scope functions) *************************************************/

static void query_pg_read (query_pg_t *p);
static void query_pg_notice (void *arg, const PGresult *res);
static void query_pg_summary (query_pg_t *p, const char *status);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* What the environment says the connection is to, before it is made: "service prod",
   "host:port/db"; that of libpq once it is made */
static char *
query_pg_endpoint (const query_pg_t *p)
{
    const char *service = g_getenv ("PGSERVICE");
    const char *host, *port, *db;

    if (p->link.conn != NULL && p->state == QUERY_PG_READY)
        return g_strdup_printf ("%s@%s:%s/%s", p->pq->PQuser (p->link.conn),
                                p->pq->PQhost (p->link.conn), p->pq->PQport (p->link.conn),
                                p->pq->PQdb (p->link.conn));
    if (service != NULL && *service != '\0')
        return g_strdup_printf ("service %s", service);
    host = g_getenv ("PGHOST");
    port = g_getenv ("PGPORT");
    db = g_getenv ("PGDATABASE");
    return g_strdup_printf ("%s:%s/%s", host != NULL && *host != '\0' ? host : "localhost",
                            port != NULL && *port != '\0' ? port : "5432",
                            db != NULL && *db != '\0' ? db : "-");
}

/* --------------------------------------------------------------------------------------------- */

static void
query_pg_set_endpoint (query_pg_t *p)
{
    char *endpoint = query_pg_endpoint (p);

    g_free (p->q->endpoint);
    p->q->endpoint =
        p->read_only ? g_strconcat ("[RO] ", endpoint, (char *) NULL) : g_strdup (endpoint);
    g_free (endpoint);
}

/* --------------------------------------------------------------------------------------------- */

/* The connection closed, its channel taken away */
static void
query_pg_close_conn (query_pg_t *p)
{
    query_pq_link_close (&p->link);
}

/* --------------------------------------------------------------------------------------------- */

/* Output tells it: the connection is made, or why not; after what it told at the start, before a
   query is run */
static void
query_pg_say (query_pg_t *p, const char *what, gboolean error)
{
    query_t *q = p->q;
    GString *text = g_string_new (q->text == NULL ? p->head : NULL);

    g_string_append_printf (text, "%s%s%s\n", error ? ANSI_RED : ANSI_BOLD, what, ANSI_RESET);
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, TRUE);
    g_string_free (text, TRUE);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

/* The query waiting for the connection is not run, it failed or was stopped, @status as History
   has it: Table and History tell it as they tell a run */
static void
query_pg_not_run (query_pg_t *p, int status, const char *summary)
{
    query_t *q = p->q;
    const double seconds = (double) (g_get_monotonic_time () - q->started) / G_USEC_PER_SEC;
    const time_t now = time (NULL);
    char when[16];

    if (!p->pending)
        return;
    p->pending = FALSE;
    query_table_say (q,
                     status < 0 ? _ ("The query was stopped before it ran.\n")
                                : _ ("The query was not run: there is no connection.\n"));
    (void) strftime (when, sizeof (when), "%H:%M:%S", localtime (&now));
    query_history_add (q, when, seconds, status, summary);
}

/* --------------------------------------------------------------------------------------------- */

/* The connection is made, or failed: the state, Output, and a query waiting for it run */
static void
query_pg_connected (query_pg_t *p, gboolean ok)
{
    const query_pq_t *pq = p->pq;
    gboolean first = !p->made;
    char *what;

    if (ok && strcmp (pq->pg_encoding_to_char (pq->PQclientEncoding (p->link.conn)), "UTF8") != 0)
    {
        // the editor and the screen are UTF-8: the text of another encoding would be wrong
        what = g_strdup_printf (_ ("The connection is not in UTF8 but in %s: it is closed."),
                                pq->pg_encoding_to_char (pq->PQclientEncoding (p->link.conn)));
        query_pg_close_conn (p);
        p->state = QUERY_PG_BROKEN;
        query_pg_not_run (p, 1, _ ("no connection"));
        query_pg_say (p, what, TRUE);
        g_free (what);
        return;
    }
    if (!ok)
    {
        what = g_strdup_printf (_ ("No connection to %s: %s"), p->q->endpoint,
                                pq->PQerrorMessage (p->link.conn));
        query_pg_close_conn (p);
        p->state = QUERY_PG_BROKEN;
        query_pg_not_run (p, 1, _ ("no connection"));
        query_pg_say (p, g_strchomp (what), TRUE);
        g_free (what);
        return;
    }
    (void) pq->PQsetNoticeReceiver (p->link.conn, query_pg_notice, p);
    p->state = QUERY_PG_READY;
    p->made = TRUE;
    query_pg_set_endpoint (p);
    what = g_strdup_printf (_ ("Connected to %s, PostgreSQL %d.%d."), p->q->endpoint,
                            pq->PQserverVersion (p->link.conn) / 10000,
                            pq->PQserverVersion (p->link.conn) % 10000);
    if (p->reconnect)
    {
        char *also = g_strconcat (what, "\n",
                                  _ ("It is a new connection: the transaction, the temporary "
                                     "tables, SET, the prepared\nstatements and the locks of "
                                     "the one before are gone."),
                                  (char *) NULL);

        g_free (what);
        what = also;
    }
    if (p->pending)
    {
        p->pending = FALSE;
        p->q->backend->start (p);
    }
    else if (p->q->text == NULL || p->reconnect)
        query_pg_say (p, what, FALSE);
    p->reconnect = FALSE;
    g_free (what);
    // the window Schema the first time, then the rows of the relation the panel was on, the
    // file of queries as it is
    if (first && p->schema != NULL)
        (void) query_pg_schema_do (p->schema, QUERY_SCHEMA_SHOW, FALSE);
    if (first && p->relation != NULL && p->state == QUERY_PG_READY)
        query_run_alone (p->q, p->relation);
}

/* --------------------------------------------------------------------------------------------- */

/* The connection is made, or failed: query_pq_link_t.connected */
static void
query_pg_link_connected (void *data, gboolean ok)
{
    query_pg_connected ((query_pg_t *) data, ok);
}

/* --------------------------------------------------------------------------------------------- */

/* The socket is ready while a query runs: query_pq_link_t.ready */
static void
query_pg_link_ready (void *data)
{
    query_pg_t *p = (query_pg_t *) data;

    if (p->state == QUERY_PG_BUSY)
        query_pg_read (p);
}

/* --------------------------------------------------------------------------------------------- */

/* A connection started; its state comes by query_pg_connected () */
static void
query_pg_connect (query_pg_t *p)
{
    p->state = QUERY_PG_CONNECTING;
    if (!query_pq_link_open (&p->link, p->read_only))
    {
        p->state = QUERY_PG_BROKEN;
        query_pg_not_run (p, 1, _ ("no connection"));
        query_pg_say (p, _ ("libpq has no memory for a connection."), TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The connection of the start, once Output tells what the start does: libpq may fail at once */
static void
query_pg_connect_later (void *data)
{
    query_pg_connect ((query_pg_t *) data);
}

/* --------------------------------------------------------------------------------------------- */
/* The run of a query */
/* --------------------------------------------------------------------------------------------- */

/* @text to Output, unless it has more than QUERY_OUTPUT_MAX */
static void
query_pg_out (query_pg_t *p, const char *text, gssize len)
{
    if (len < 0)
        len = (gssize) strlen (text);
    if (p->out->len + (gsize) len > QUERY_OUTPUT_MAX)
        p->cut = TRUE;
    if (p->out->len < QUERY_OUTPUT_MAX)
        g_string_append_len (p->out, text, MIN (len, (gssize) (QUERY_OUTPUT_MAX - p->out->len)));
}

/* --------------------------------------------------------------------------------------------- */

/* A line to Output in red */
static void
query_pg_red (query_pg_t *p, const char *text)
{
    char *line = g_strdup_printf (ANSI_RED "%s" ANSI_RESET "\n", text);

    query_pg_out (p, line, -1);
    g_free (line);
}

/* --------------------------------------------------------------------------------------------- */

/* A notice of the server, NOTICE: or WARNING:, in Output in its place among the results */
static void
query_pg_notice (void *arg, const PGresult *res)
{
    query_pg_t *p = (query_pg_t *) arg;
    const char *severity = p->pq->PQresultErrorField (res, QUERY_PG_DIAG_SEVERITY);
    const char *message = p->pq->PQresultErrorField (res, QUERY_PG_DIAG_MESSAGE_PRIMARY);
    char *line;

    if (p->state != QUERY_PG_BUSY)
        return;
    line = g_strdup_printf (ANSI_CYAN "%s: %s" ANSI_RESET "\n", severity != NULL ? severity : "",
                            message != NULL ? message : "");
    query_pg_out (p, line, -1);
    g_free (line);
}

/* --------------------------------------------------------------------------------------------- */

/* The cell of a value: NULL; a bytea by its length and its start; else as the server wrote it,
   on one line, its start alone when it is long */
static char *
query_pg_cell (const query_pg_t *p, const PGresult *res, int row, int column)
{
    const query_pq_t *pq = p->pq;
    const char *value = pq->PQgetvalue (res, row, column);
    const int len = pq->PQgetlength (res, row, column);
    char *start, *cell;
    const char *end;

    if (pq->PQgetisnull (res, row, column))
        return g_strdup ("NULL");
    // "\x" and two digits a byte
    if (pq->PQftype (res, column) == QUERY_PG_BYTEA_OID && len >= 2 && value[0] == '\\'
        && value[1] == 'x')
        return g_strdup_printf ("<bytea %d bytes> \\x%.*s%s", (len - 2) / 2,
                                MIN (len - 2, QUERY_PG_BYTEA_SHOWN), value + 2,
                                len - 2 > QUERY_PG_BYTEA_SHOWN ? "..." : "");
    if (len <= QUERY_PG_CELL_MAX)
        return query_cell_text (value);
    // the start of a long one, cut at a character
    end = g_utf8_find_prev_char (value, value + QUERY_PG_CELL_MAX + 1);
    start = g_strndup (value, (gsize) ((end != NULL ? end : value + QUERY_PG_CELL_MAX) - value));
    cell = query_cell_text (start);
    g_free (start);
    return cell;
}

/* --------------------------------------------------------------------------------------------- */

/* The rows of @res, of the command being read: the first QUERY_TABLE_ROWS kept, all counted */
static void
query_pg_rows (query_pg_t *p, const PGresult *res)
{
    const query_pq_t *pq = p->pq;
    const int nfields = pq->PQnfields (res), ntuples = pq->PQntuples (res);
    int r, c;

    if (p->columns == NULL)
    {
        p->columns = g_ptr_array_new_with_free_func (g_free);
        p->cells = g_ptr_array_new_with_free_func (g_free);
        p->nrows = 0;
        for (c = 0; c < nfields; c++)
            g_ptr_array_add (p->columns, g_strdup (pq->PQfname (res, c)));
    }
    for (r = 0; r < ntuples; r++, p->nrows++)
        if (p->nrows < (gint64) QUERY_TABLE_ROWS)
            for (c = 0; c < nfields; c++)
                g_ptr_array_add (p->cells, query_pg_cell (p, res, r, c));
}

/* --------------------------------------------------------------------------------------------- */

/* The rows of the command read are a table of Table, @status the status of the command, NULL
   when it failed after some rows: they are not all of them */
static void
query_pg_table (query_pg_t *p, const char *status)
{
    char *grid;

    if (p->columns == NULL)
        return;
    grid = query_grid (p->columns, p->cells);
    p->ntables++;
    if (status != NULL)
        g_string_append_printf (p->tables, ANSI_BOLD "%u. %s: " ANSI_RESET, p->ntables, status);
    else
        g_string_append_printf (p->tables, ANSI_BOLD "%u. " ANSI_RESET, p->ntables);
    if (status == NULL)
        g_string_append_printf (
            p->tables, _ ("incomplete, the command failed after %" G_GINT64_FORMAT " rows\n\n%s\n"),
            p->nrows, grid);
    else if (p->nrows == 0)
        g_string_append_printf (p->tables, _ ("no rows\n\n%s\n"), grid);
    else if (p->nrows > (gint64) QUERY_TABLE_ROWS)
        g_string_append_printf (p->tables, _ ("the first %u of %" G_GINT64_FORMAT " rows\n\n%s\n"),
                                QUERY_TABLE_ROWS, p->nrows, grid);
    else
        g_string_append_printf (p->tables,
                                ngettext ("%u row\n\n%s\n", "%u rows\n\n%s\n", (gulong) p->nrows),
                                (guint) p->nrows, grid);
    p->rows = p->rows || (status != NULL && p->nrows > 0);
    g_free (grid);
    g_ptr_array_free (p->columns, TRUE);
    g_ptr_array_free (p->cells, TRUE);
    p->columns = NULL;
    p->cells = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* An error of a command in Output, its place marked in the file */
static void
query_pg_error (query_pg_t *p, const PGresult *res)
{
    const query_pq_t *pq = p->pq;
    const char *severity = pq->PQresultErrorField (res, QUERY_PG_DIAG_SEVERITY);
    const char *state = pq->PQresultErrorField (res, QUERY_PG_DIAG_SQLSTATE);
    const char *primary = pq->PQresultErrorField (res, QUERY_PG_DIAG_MESSAGE_PRIMARY);
    const char *detail = pq->PQresultErrorField (res, QUERY_PG_DIAG_MESSAGE_DETAIL);
    const char *hint = pq->PQresultErrorField (res, QUERY_PG_DIAG_MESSAGE_HINT);
    const char *context = pq->PQresultErrorField (res, QUERY_PG_DIAG_CONTEXT);
    const char *position = pq->PQresultErrorField (res, QUERY_PG_DIAG_STATEMENT_POSITION);
    GString *text = g_string_new (ANSI_RED);

    if (!p->failed)
    {
        g_free (p->summary);
        p->summary = state != NULL ? g_strdup_printf ("ERROR %s", state) : g_strdup ("ERROR");
    }
    p->failed = TRUE;
    if (state != NULL && strcmp (state, "57014") == 0)
        p->canceled = TRUE;
    if (primary == NULL && p->told)
    {
        g_string_free (text, TRUE);
        return;
    }
    if (primary == NULL)
    {
        p->told = TRUE;
        // no fields: an error of libpq itself, the connection lost
        g_string_append (text, pq->PQresultErrorMessage (res));
        g_strchomp (text->str);
        text->len = strlen (text->str);
    }
    else
    {
        g_string_append_printf (text, "%s", severity != NULL ? severity : "ERROR");
        if (state != NULL)
            g_string_append_printf (text, " [%s]", state);
        g_string_append_printf (text, ": %s", primary);
    }
    g_string_append (text, ANSI_RESET "\n");
    if (detail != NULL)
        g_string_append_printf (text, "DETAIL: %s\n", detail);
    if (hint != NULL)
        g_string_append_printf (text, "HINT: %s\n", hint);
    if (context != NULL)
        g_string_append_printf (text, "CONTEXT: %s\n", context);
    // the place is in characters of the text sent, which is that of the file as it is
    if (position != NULL && p->q->text != NULL)
    {
        const char *sent = p->q->text;
        const gsize at = query_utf8_offset (sent, strlen (sent), atol (position) - 1);
        const long line = query_line_of (sent, at) + 1;
        const char *start = sent + at;
        long column;

        while (start > sent && start[-1] != '\n')
            start--;
        column = g_utf8_strlen (start, (gssize) (sent + at - start));
        // a query of no file has no line of it
        if (p->q->first == 0)
            g_string_append_printf (text, _ ("at column %ld\n"), column + 1);
        else
            g_string_append_printf (text, _ ("at line %ld, column %ld\n"), p->q->first + line - 1,
                                    column + 1 + (line == 1 ? p->q->first_column : 0));
        query_mark_error (p->q, line, column);
    }
    query_pg_out (p, text->str, (gssize) text->len);
    g_string_free (text, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* The rows of COPY ... TO STDOUT into Output, as many as have come; FALSE while more are to come */
static gboolean
query_pg_copy_out (query_pg_t *p)
{
    char *buf = NULL;
    int n;

    while ((n = p->pq->PQgetCopyData (p->link.conn, &buf, 1)) > 0)
    {
        query_pg_out (p, buf, n);
        p->pq->PQfreemem (buf);
        buf = NULL;
    }
    if (n == 0)
        return FALSE;
    // -1 the end, -2 an error that PQgetResult () tells
    p->copy_out = FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* What a command did, for History, unless an error came before */
static void
query_pg_summary (query_pg_t *p, const char *status)
{
    if (p->failed)
        return;
    g_free (p->summary);
    p->summary = g_strdup (status);
}

/* --------------------------------------------------------------------------------------------- */

/* The state of the transaction of the session, after the head of Output, and the title of the tab
   that tells it, NULL for its own: in one, or in one that failed and takes no query but
   ROLLBACK */
static char *
query_pg_transaction (query_pg_t *p, GString *head)
{
    const int state =
        p->link.conn != NULL ? p->pq->PQtransactionStatus (p->link.conn) : QUERY_PG_TRANS_IDLE;
    char *title;

    if (state == QUERY_PG_TRANS_INTRANS)
    {
        g_string_append (head, "   [TX]");
        title = g_strdup_printf ("%s [TX]", _ ("Output"));
    }
    else if (state == QUERY_PG_TRANS_INERROR)
    {
        g_string_append_printf (head, "   " ANSI_RED "[%s]" ANSI_BOLD,
                                _ ("TX FAILED: ROLLBACK required"));
        title = g_strdup_printf ("%s [%s]", _ ("Output"), _ ("TX FAILED"));
    }
    else
        title = NULL;
    return title;
}

/* --------------------------------------------------------------------------------------------- */

/* One result of the query: rows, the end of a command, an error */
static void
query_pg_result (query_pg_t *p, PGresult *res)
{
    const query_pq_t *pq = p->pq;
    char *line;

    switch (pq->PQresultStatus (res))
    {
    case QUERY_PG_SINGLE_TUPLE:
    case QUERY_PG_TUPLES_CHUNK:
        query_pg_rows (p, res);
        if (*pq->PQcmdStatus (res) != '\0')
        {
            g_free (p->status);
            p->status = g_strdup (pq->PQcmdStatus (res));
        }
        break;
    case QUERY_PG_TUPLES_OK:
    {
        // the end of the rows of the command: none in it, row by row; all, in the old way
        const char *status = *pq->PQcmdStatus (res) != '\0' ? pq->PQcmdStatus (res)
            : p->status != NULL                             ? p->status
                                                            : "SELECT";

        query_pg_rows (p, res);
        line = g_strdup_printf ("%s\n", status);
        query_pg_table (p, status);
        query_pg_summary (p, status);
        query_pg_out (p, line, -1);
        g_free (line);
        g_clear_pointer (&p->status, g_free);
        break;
    }
    case QUERY_PG_COMMAND_OK:
        /* what changes the schemas; CREATE TABLE AS and SELECT INTO tell "SELECT 5", a SELECT
           that has no rows to give: a SELECT that does is TUPLES_OK */
        if (g_str_has_prefix (pq->PQcmdStatus (res), "CREATE ")
            || g_str_has_prefix (pq->PQcmdStatus (res), "ALTER ")
            || g_str_has_prefix (pq->PQcmdStatus (res), "DROP ")
            || g_str_has_prefix (pq->PQcmdStatus (res), "SELECT ")
            || g_str_has_prefix (pq->PQcmdStatus (res), "IMPORT FOREIGN SCHEMA"))
            p->ddl = TRUE;
        query_pg_summary (p, pq->PQcmdStatus (res));
        line = g_strdup_printf ("%s\n", pq->PQcmdStatus (res));
        query_pg_out (p, line, -1);
        g_free (line);
        break;
    case QUERY_PG_EMPTY_QUERY:
        query_pg_out (p, _ ("(nothing to run)\n"), -1);
        break;
    case QUERY_PG_COPY_OUT:
        p->copy_out = TRUE;
        break;
    case QUERY_PG_COPY_IN:
        // nothing to send: the server ends the COPY with an error
        p->copy_in = TRUE;
        break;
    default:
        // rows of a command that failed after them are not its rows
        query_pg_table (p, NULL);
        g_clear_pointer (&p->status, g_free);
        query_pg_error (p, res);
        break;
    }
    pq->PQclear (res);
}

/* --------------------------------------------------------------------------------------------- */

/* The query has ended: Output, Table, History */
static void
query_pg_done (query_pg_t *p)
{
    query_t *q = p->q;
    const double seconds = (double) (g_get_monotonic_time () - q->started) / G_USEC_PER_SEC;
    char when[16];
    const time_t now = time (NULL);
    GString *text = g_string_new (NULL);
    char *title;

    // a query that ended by itself before the cancel came is no query stopped
    if (q->stopped && !p->canceled && !p->lost)
    {
        q->stopped = FALSE;
        query_pg_out (p, _ ("The query ended before the cancel came.\n"), -1);
    }
    query_pq_link_unwatch (&p->link);
    query_pg_table (p, NULL);
    if (p->lost || p->pq->PQstatus (p->link.conn) == QUERY_PG_CONNECTION_BAD)
    {
        query_pg_close_conn (p);
        p->state = QUERY_PG_BROKEN;
        query_pg_red (p,
                      _ ("The connection is lost: the transaction, the temporary tables, SET, "
                         "the prepared\nstatements and the locks of the session are gone. "
                         "F5 connects again."));
        p->failed = TRUE;
    }
    else
        p->state = QUERY_PG_READY;

    (void) strftime (when, sizeof (when), "%H:%M:%S", localtime (&now));
    query_output_head (q, text, seconds, when);
    if (q->stopped)
        g_string_append (text, _ ("stopped"));
    else if (p->failed)
        g_string_append (text, _ ("failed"));
    else
        g_string_append (text, _ ("done"));
    title = query_pg_transaction (p, text);
    g_string_append (text, ANSI_RESET "\n\n");
    g_string_append_len (text, p->out->str, (gssize) p->out->len);
    if (p->cut)
        g_string_append_printf (text, ANSI_RED "\n%s\n" ANSI_RESET,
                                _ ("... more than 8 MB: the rest is cut."));
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, TRUE);
    // after the text: a tab the user closed is made again with its own title
    query_tab_title (q, QUERY_TAB_OUTPUT, title);
    g_free (title);
    g_string_free (text, TRUE);

    if (p->ntables == 0)
        g_string_append (p->tables,
                         _ ("No rows: a command that gives some, SELECT, ... RETURNING, "
                            "makes a table.\n"));
    // in front when it has rows, Output with an error
    query_tab_set (q, QUERY_TAB_TABLE, p->tables->str, p->rows && !p->failed);
    query_history_add (q, when, seconds, q->stopped ? -1 : p->failed ? 1 : 0, p->summary);
    // the other connection of Schema sees what is committed alone
    if (p->ddl && p->schema != NULL
        && (p->state != QUERY_PG_READY
            || p->pq->PQtransactionStatus (p->link.conn) == QUERY_PG_TRANS_IDLE))
    {
        p->ddl = FALSE;
        query_pg_schema_changed (p->schema);
    }
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

/* The query sent on: the socket watched for a write while libpq has more of it */
static void
query_pg_flush (query_pg_t *p)
{
    if (!query_pq_link_flush (&p->link))
        p->lost = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The socket is ready while the query runs: what is to send sent, what came read, each result
   taken until there is none */
static void
query_pg_read (query_pg_t *p)
{
    const query_pq_t *pq = p->pq;

    // what came read first: the server may wait for its own output to be read to read more
    if (!pq->PQconsumeInput (p->link.conn))
        p->lost = TRUE;
    if (p->link.flushing && !p->lost)
        query_pg_flush (p);
    if (p->copy_in
        && pq->PQputCopyEnd (p->link.conn,
                             _ ("COPY FROM STDIN: coole -Q has nothing to "
                                "send"))
            != 0)
    {
        p->copy_in = FALSE;
        query_pg_flush (p);
    }
    if (p->copy_out && !query_pg_copy_out (p))
        return;
    while (!p->copy_out && !p->copy_in && !pq->PQisBusy (p->link.conn))
    {
        PGresult *res = pq->PQgetResult (p->link.conn);

        if (res == NULL)
        {
            query_pg_done (p);
            return;
        }
        query_pg_result (p, res);
        if (p->copy_out && !query_pg_copy_out (p))
            return;
        if (p->copy_in)
        {
            query_pg_read (p);
            return;
        }
    }
    if (p->lost && pq->PQisBusy (p->link.conn))
        query_pg_done (p);
}

/* --------------------------------------------------------------------------------------------- */

/* The text of the query sent, all of it in one PQsendQuery (): its commands one transaction
   unless it has its own, as the protocol of a simple query has it */
static void
query_pg_send (query_pg_t *p)
{
    const query_pq_t *pq = p->pq;

    g_string_truncate (p->out, 0);
    g_string_truncate (p->tables, 0);
    p->ntables = 0;
    p->rows = p->failed = p->cut = p->link.flushing = p->copy_out = p->copy_in = p->lost = p->told =
        p->canceled = FALSE;
    g_clear_pointer (&p->summary, g_free);
    if (p->reconnect)
    {
        query_pg_red (p,
                      _ ("It is a new connection: the transaction, the temporary tables, SET, "
                         "the prepared\nstatements and the locks of the one before are "
                         "gone.\n"));
        p->reconnect = FALSE;
    }
    p->state = QUERY_PG_BUSY;
    if (!pq->PQsendQuery (p->link.conn, p->q->text))
    {
        char *line = g_strdup_printf (ANSI_RED "%s" ANSI_RESET, pq->PQerrorMessage (p->link.conn));

        query_pg_out (p, line, -1);
        g_free (line);
        p->failed = TRUE;
        query_pg_done (p);
        return;
    }
    // the rows as they come, not all of them in the memory of libpq first
    if (!query_pq_chunks (pq) || !pq->PQsetChunkedRowsMode (p->link.conn, QUERY_PG_CHUNK))
        (void) pq->PQsetSingleRowMode (p->link.conn);
    query_pg_flush (p);
}

/* --------------------------------------------------------------------------------------------- */
/* The cancel of a query */
/* --------------------------------------------------------------------------------------------- */

/* What became of the cancel, in Output when the query ends: it is sent, the query is still to
   end, or why it could not be */
static void
query_pg_cancel_done (query_pg_t *p, const char *error)
{
    char *line;

    if (p->cancel_fd >= 0)
        delete_select_channel (p->cancel_fd);
    p->cancel_fd = -1;
    p->canceling = FALSE;
    if (error != NULL)
    {
        line = g_strdup_printf (_ ("The cancel could not be sent: %s"), error);
        query_pg_red (p, g_strchomp (line));
        g_free (line);
    }
    // the next query waited for the cancel to be sent, which would end it else
    if (p->pending && p->state == QUERY_PG_READY)
    {
        p->pending = FALSE;
        query_pg_send (p);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The thread of a cancel: PQcancel (), which blocks, its answer a byte in the pipe */
static gpointer
query_pg_cancel_thread (gpointer data)
{
    query_pg_cancel_t *c = (query_pg_cancel_t *) data;
    const char ok = c->pq->PQcancel (c->cancel, c->error, sizeof (c->error)) ? 1 : 0;
    ssize_t written;

    c->pq->PQfreeCancel (c->cancel);
    c->cancel = NULL;
    written = write (c->fds[1], &ok, 1);
    (void) written;
    close (c->fds[1]);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The thread of the cancel has answered */
static int
query_pg_cancel_answered (int fd, void *data)
{
    query_pg_t *p = (query_pg_t *) data;
    query_pg_cancel_t *c = p->cancel;
    char ok = 0;

    if (read (fd, &ok, 1) != 1)
        ok = 0;
    p->cancel = NULL;
    query_pg_cancel_done (p, ok ? NULL : c->error);
    close (c->fds[0]);
    g_free (c);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The cancel of libpq 17 on its way: its own connection to the server, polled as one is */
static int
query_pg_cancel_poll (int fd, void *data)
{
    query_pg_t *p = (query_pg_t *) data;
    const query_pq_t *pq = p->pq;
    int status, sock;

    (void) fd;
    status = pq->PQcancelPoll (p->cancel_conn);
    sock = pq->PQcancelSocket (p->cancel_conn);
    if ((status == QUERY_PG_POLLING_READING || status == QUERY_PG_POLLING_WRITING) && sock >= 0)
    {
        if (sock != p->cancel_fd)
        {
            delete_select_channel (p->cancel_fd);
            add_select_channel (sock, query_pg_cancel_poll, p);
            p->cancel_fd = sock;
        }
        select_channel_want_write (sock, status == QUERY_PG_POLLING_WRITING);
        return 0;
    }
    query_pg_cancel_done (
        p, status == QUERY_PG_POLLING_OK ? NULL : pq->PQcancelErrorMessage (p->cancel_conn));
    pq->PQcancelFinish (p->cancel_conn);
    p->cancel_conn = NULL;
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The server asked to cancel the query, without waiting for it: the query goes on being read
   until it ends, as a cancel sent is no query ended */
static void
query_pg_cancel (query_pg_t *p)
{
    const query_pq_t *pq = p->pq;

    if (p->canceling)
        return;
    p->canceling = TRUE;
    if (query_pq_async_cancel (pq))
    {
        p->cancel_conn = pq->PQcancelCreate (p->link.conn);
        if (p->cancel_conn == NULL || !pq->PQcancelStart (p->cancel_conn))
        {
            query_pg_cancel_done (p,
                                  p->cancel_conn != NULL ? pq->PQcancelErrorMessage (p->cancel_conn)
                                                         : _ ("no memory"));
            if (p->cancel_conn != NULL)
                pq->PQcancelFinish (p->cancel_conn);
            p->cancel_conn = NULL;
            return;
        }
        p->cancel_fd = pq->PQcancelSocket (p->cancel_conn);
        if (p->cancel_fd < 0)
        {
            p->cancel_fd = -1;
            (void) query_pg_cancel_poll (-1, p);
            return;
        }
        add_select_channel (p->cancel_fd, query_pg_cancel_poll, p);
        select_channel_want_write (p->cancel_fd, TRUE);
        return;
    }

    {
        query_pg_cancel_t *c = g_new0 (query_pg_cancel_t, 1);
        GThread *thread;

        c->pq = pq;
        // the thread's own: the connection may be gone before it is done
        c->cancel = pq->PQgetCancel (p->link.conn);
        if (c->cancel == NULL || pipe (c->fds) != 0)
        {
            if (c->cancel != NULL)
                pq->PQfreeCancel (c->cancel);
            g_free (c);
            query_pg_cancel_done (p, _ ("no cancel for this connection"));
            return;
        }
        p->cancel = c;
        p->cancel_fd = c->fds[0];
        add_select_channel (c->fds[0], query_pg_cancel_answered, p);
        thread = g_thread_new ("query-cancel", query_pg_cancel_thread, c);
        g_thread_unref (thread);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* The language server */
/* --------------------------------------------------------------------------------------------- */

/* The value of the option @keyword of @options, NULL for none */
static const char *
query_pg_option (const PQconninfoOption *options, const char *keyword)
{
    for (; options != NULL && options->keyword != NULL; options++)
        if (strcmp (options->keyword, keyword) == 0)
            return options->val != NULL && *options->val != '\0' ? options->val : NULL;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* What the language server connects to, in the environment of coole for the "requests" of
   lsp.json to tell it: a server of postgres-language-server is shared by all who use it, and
   reads the environment of the first one, without PGSERVICE.  It is what libpq takes from the
   environment, PGSERVICE and its file too, without the password: the server reads ~/.pgpass. */
static void
query_pg_lsp_context (const query_pg_t *p)
{
    PQconninfoOption *options = p->pq->PQconndefaults ();
    const char *host = query_pg_option (options, "host");
    const char *port = query_pg_option (options, "port");
    const char *user = query_pg_option (options, "user");
    const char *db = query_pg_option (options, "dbname");
    const char *schema = g_getenv ("MC_PG_SCHEMA");
    char *ehost, *euser, *edb, *uri;

    // a host of many, "a,b", is the first one; a directory of a socket is a host of the URI too
    ehost = host != NULL ? g_strndup (host, strcspn (host, ",")) : g_strdup (QUERY_PG_SOCKET_DIR);
    uri = g_uri_escape_string (ehost, NULL, FALSE);
    g_free (ehost);
    ehost = uri;
    euser = g_uri_escape_string (user != NULL ? user : g_get_user_name (), NULL, FALSE);
    edb = g_uri_escape_string (db != NULL         ? db
                                   : user != NULL ? user
                                                  : g_get_user_name (),
                               NULL, FALSE);
    uri = g_strdup_printf ("postgresql://%s@%s:%s/%s", euser, ehost, port != NULL ? port : "5432",
                           edb);
    g_setenv (QUERY_PG_LSP_URI, uri, TRUE);
    g_setenv (QUERY_PG_LSP_SCHEMA, schema != NULL && *schema != '\0' ? schema : "public", TRUE);
    g_free (uri);
    g_free (edb);
    g_free (euser);
    g_free (ehost);
    if (options != NULL)
        p->pq->PQconninfoFree (options);
}

/* --------------------------------------------------------------------------------------------- */
/* What the plugin asks of the backend */
/* --------------------------------------------------------------------------------------------- */

static void *
query_pg_open (query_t *q)
{
    query_pg_t *p = g_new0 (query_pg_t, 1);
    const char *read_only = g_getenv ("MC_QUERY_READ_ONLY");
    const char *name = g_getenv ("MC_PG_CONNECTION");
    const char *relation = g_getenv ("MC_PG_RELATION");

    p->q = q;
    p->cancel_fd = -1;
    p->out = g_string_new (NULL);
    p->tables = g_string_new (NULL);
    p->pq = query_pq_load (&p->pq_error);
    query_pq_link_init (&p->link, p->pq, query_pg_link_connected, query_pg_link_ready, p);
    p->read_only = read_only != NULL && strcmp (read_only, "1") == 0;
    if (name == NULL || *name == '\0')
        name = g_getenv ("PGSERVICE");
    p->name = g_strdup (name != NULL && *name != '\0' ? name : NULL);
    if (relation != NULL && *relation != '\0')
        p->relation = query_pg_relation_query (g_getenv ("MC_PG_SCHEMA"), relation);
    query_pg_set_endpoint (p);
    // before the language server starts, with the file of queries
    if (p->pq != NULL)
    {
        query_pg_lsp_context (p);
        p->schema = query_pg_schema_new (q, p->pq, p->read_only, g_getenv ("MC_PG_SCHEMA"));
    }
    return p;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_pg_close (void *data)
{
    query_pg_t *p = (query_pg_t *) data;

    if (p->cancel_conn != NULL)
    {
        delete_select_channel (p->cancel_fd);
        p->pq->PQcancelFinish (p->cancel_conn);
    }
    // the thread of a cancel has its pipe: it is left to it, its end read never closed, nor its
    // memory freed, the thread writing into them still
    else if (p->cancel != NULL)
        delete_select_channel (p->cancel_fd);
    if (p->pq != NULL)
        query_pg_close_conn (p);
    g_free (p->pq_error);
    g_free (p->name);
    g_free (p->head);
    g_free (p->relation);
    query_pg_schema_free (p->schema);
    g_string_free (p->out, TRUE);
    g_string_free (p->tables, TRUE);
    g_free (p->status);
    g_free (p->summary);
    if (p->columns != NULL)
    {
        g_ptr_array_free (p->columns, TRUE);
        g_ptr_array_free (p->cells, TRUE);
    }
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

/* The query of the rows of MC_PG_RELATION put in the file of queries when it has nothing, the
   file changed, to be saved or not; whether it was */
static gboolean
query_pg_put (query_pg_t *p, void *edit)
{
    gsize len = 0;
    char *text, *c;
    gboolean blank;

    if (edit == NULL || p->relation == NULL)
        return FALSE;
    text = p->q->host->get_text (p->q->host, edit, &len);
    if (text == NULL)
        return FALSE;
    for (c = text; *c != '\0' && g_ascii_isspace (*c); c++)
        ;
    blank = *c == '\0';
    g_free (text);
    if (blank)
    {
        char *line = g_strconcat (p->relation, "\n", (char *) NULL);

        p->q->host->insert_text (p->q->host, edit, line, 0);
        g_free (line);
    }
    return blank;
}

/* --------------------------------------------------------------------------------------------- */

/* What is found, libpq and the language server, the query of the relation put in the file, and
   the connection started */
static void
query_pg_startup (void *data, void *edit, GString *text)
{
    query_pg_t *p = (query_pg_t *) data;

    g_string_append_printf (text, _ ("Database: %s\n"), p->q->endpoint);
    if (query_pg_put (p, edit))
        g_string_append (text, _ ("The query of the rows of the relation is put in the file.\n"));
    if (p->pq == NULL)
    {
        g_string_append_printf (text, ANSI_RED "%s\n" ANSI_RESET, p->pq_error);
        return;
    }
    g_string_append_printf (text, "libpq: %d.%d%s\n", p->pq->PQlibVersion () / 10000,
                            p->pq->PQlibVersion () % 10000,
                            query_pq_chunks (p->pq) ? "" : _ (", rows one by one"));
    {
        char *server = g_find_program_in_path (QUERY_PG_LANGUAGE_SERVER);

        if (server != NULL)
            g_string_append_printf (text, "%s: %s\n", QUERY_PG_LANGUAGE_SERVER, server);
        else
            g_string_append_printf (text,
                                    _ ("%s is not found: no completion of the queries (%s)\n"),
                                    QUERY_PG_LANGUAGE_SERVER, QUERY_PG_LANGUAGE_SERVER_URL);
        g_free (server);
    }
    query_ai_startup (p->q, text);
    g_string_append (text,
                     _ ("F5 runs the selection, else the whole file; Ctrl-F5 the whole "
                        "file.\n\n"));
    p->head = g_strdup (text->str);
    g_string_append (text, _ ("Connecting...\n"));
    p->q->host->call_later (p->q->host, query_pg_connect_later, p);
}

/* --------------------------------------------------------------------------------------------- */

/* A connection on its way is stopped as a query is: libpq has no time limit for it when it does
   not block, and a server that does not answer leaves it waiting */
static gboolean
query_pg_running (void *data)
{
    const query_pg_t *p = (const query_pg_t *) data;

    // a query waiting for the cancel before it to be sent runs too
    return p->state == QUERY_PG_CONNECTING || p->state == QUERY_PG_BUSY || p->pending;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_pg_start (void *data)
{
    query_pg_t *p = (query_pg_t *) data;

    if (p->pq == NULL)
    {
        p->pending = TRUE;
        query_pg_not_run (p, 1, _ ("no libpq"));
        query_pg_say (p, p->pq_error, TRUE);
        return;
    }
    switch (p->state)
    {
    case QUERY_PG_CONNECTING:
        // it runs once the connection is made
        p->pending = TRUE;
        query_tab_set (p->q, QUERY_TAB_OUTPUT, _ ("Connecting...\n"), TRUE);
        return;
    case QUERY_PG_OFF:
    case QUERY_PG_BROKEN:
        p->reconnect = p->made;
        p->pending = TRUE;
        query_tab_set (p->q, QUERY_TAB_OUTPUT, _ ("Connecting...\n"), TRUE);
        query_pg_connect (p);
        return;
    default:
        break;
    }
    // the cancel of the query before is on its way: it would end this one
    if (p->canceling)
    {
        p->pending = TRUE;
        query_tab_set (p->q, QUERY_TAB_OUTPUT, _ ("Running... once the cancel before is sent.\n"),
                       TRUE);
        return;
    }
    query_pg_send (p);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_pg_stop (void *data)
{
    query_pg_t *p = (query_pg_t *) data;

    if (p->state == QUERY_PG_CONNECTING)
    {
        query_pg_close_conn (p);
        p->state = QUERY_PG_BROKEN;
        p->q->stopped = TRUE;
        query_pg_not_run (p, -1, NULL);
        query_pg_say (p, _ ("The connection was stopped: F5 connects again."), TRUE);
    }
    else if (p->state == QUERY_PG_READY && p->pending)
    {
        p->q->stopped = TRUE;
        query_pg_not_run (p, -1, NULL);
        query_pg_say (p, _ ("The query was stopped before it ran."), TRUE);
    }
    else if (p->state == QUERY_PG_BUSY && !p->canceling)
    {
        p->q->stopped = TRUE;
        query_tab_set (p->q, QUERY_TAB_OUTPUT, _ ("Running... the cancel is sent.\n"), TRUE);
        query_pg_cancel (p);
        query_redraw (p->q);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_pg_schema (void *data, query_schema_action_t action, gboolean key)
{
    query_pg_t *p = (query_pg_t *) data;

    if (p->schema != NULL)
        return query_pg_schema_do (p->schema, action, key);
    if (key)
        return FALSE;
    query_pg_say (p, p->pq_error, TRUE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_pg_ai_context (void *data, const char *use)
{
    query_pg_t *p = (query_pg_t *) data;

    (void) use;
    if (p->schema == NULL)
        query_ai_context (p->q, NULL, p->pq_error);
    else
        query_pg_schema_ai (p->schema);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const query_backend_t query_postgres_backend = {
    .id = "postgres",
    .extension = ".sql",
    .sections = FALSE,
    .open = query_pg_open,
    .close = query_pg_close,
    .startup = query_pg_startup,
    .context = NULL,
    .running = query_pg_running,
    .start = query_pg_start,
    .stop = query_pg_stop,
    .rollback = "ROLLBACK",
    .schema = query_pg_schema,
    .language = "SQL of PostgreSQL, run by libpq as it is: no command of psql",
    .comment = "-- ",
    .ai_context = query_pg_ai_context,
};

/* --------------------------------------------------------------------------------------------- */
