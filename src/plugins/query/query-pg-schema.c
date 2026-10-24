/*
   The query plugin: the window Schema of PostgreSQL, its schemas, relations and columns.

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

/** \file query-pg-schema.c
 *  \brief Source: the window Schema of the queries of PostgreSQL
 *
 *  The catalog is read by a connection of its own, made as that of the queries is, and that
 *  does not block either: the list of the schemas and their relations once, the columns of a
 *  relation when it is unfolded.  Its text is made again from what was read each time a line
 *  is folded or unfolded, the cursor staying on what it was on.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "lib/global.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"  // COLS
#include "lib/widget.h"   // D_NORMAL

#include "query-core.h"
#include "query-pg-schema.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// the schemas and the relations in them, those of the system left out; a schema with none has
// one row, with no relation
#define QUERY_SCHEMA_LIST                                                                          \
    "SELECT n.nspname, c.oid, c.relname, c.relkind, c.reltuples::bigint "                          \
    "FROM pg_catalog.pg_namespace n LEFT JOIN pg_catalog.pg_class c "                              \
    "ON c.relnamespace = n.oid AND c.relkind IN ('r', 'p', 'v', 'm', 'f', 'S') "                   \
    "WHERE n.nspname !~ '^pg_' AND n.nspname <> 'information_schema' "                             \
    "ORDER BY n.nspname, c.relname"

// the columns of a relation, of PostgreSQL 12 and later: attgenerated
#define QUERY_SCHEMA_COLUMNS                                                                       \
    "SELECT a.attname, pg_catalog.format_type (a.atttypid, a.atttypmod), a.attnotnull, "           \
    "a.attidentity, a.attgenerated, pg_catalog.pg_get_expr (d.adbin, d.adrelid) "                  \
    "FROM pg_catalog.pg_attribute a LEFT JOIN pg_catalog.pg_attrdef d "                            \
    "ON d.adrelid = a.attrelid AND d.adnum = a.attnum "                                            \
    "WHERE a.attrelid = %u AND a.attnum > 0 AND NOT a.attisdropped ORDER BY a.attnum"

// the characters of a default shown at most
#define QUERY_SCHEMA_DEFAULT 24

/*** file scope type declarations ****************************************************************/

typedef enum
{
    QUERY_SCHEMA_OFF,
    QUERY_SCHEMA_CONNECTING,
    QUERY_SCHEMA_READY,
    QUERY_SCHEMA_BUSY,
    QUERY_SCHEMA_BROKEN  // it failed, or was lost: what is asked for next connects again
} query_schema_state_t;

typedef enum
{
    QUERY_SCHEMA_ASK_LIST,
    QUERY_SCHEMA_ASK_COLUMNS
} query_schema_ask_t;

typedef struct
{
    char *name;
    char *text;  // its line, after the name
} query_schema_column_t;

typedef struct
{
    char *name;
    Oid oid;
    char kind;    // relkind
    gint64 rows;  // reltuples, -1 for none known
    gboolean open;
    GPtrArray *columns;  // query_schema_column_t, NULL before they are read
} query_schema_rel_t;

typedef struct
{
    char *name;
    gboolean open;
    GPtrArray *rels;  // query_schema_rel_t
} query_schema_nsp_t;

/* What a line of the window is of: a schema, a relation of it, a column of that; -1 for none */
typedef struct
{
    int nsp, rel, col;
} query_schema_line_t;

struct query_pg_schema_t
{
    query_t *q;
    const query_pq_t *pq;
    gboolean read_only;
    char *first;  // MC_PG_SCHEMA, first in the list and unfolded

    PGconn *conn;
    query_schema_state_t state;
    int fd;  // the socket that is a select channel, -1 for none
    gboolean flushing;
    query_schema_ask_t asked;
    Oid asked_oid;    // of the columns asked for
    PGresult *res;    // the rows of the query of now
    char *res_error;  // its error

    gboolean want_list;
    GArray *want_columns;  // Oid of the relations whose columns are to be read

    gboolean shown;   // the window is made
    gboolean read;    // the list was read once
    char *error;      // why it could not be, NULL for none
    GPtrArray *nsps;  // query_schema_nsp_t
    GArray *lines;    // query_schema_line_t, a line of the window each
    char *keep_nsp;   // what the cursor is on, for it to stay there when the text is made again
    char *keep_rel;
    int keep_col;
};

/*** forward declarations (file scope functions) *************************************************/

static void query_schema_next (query_pg_schema_t *s);
static void query_schema_read_input (query_pg_schema_t *s);
static void query_schema_poll (query_pg_schema_t *s);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
query_schema_column_free (gpointer data)
{
    query_schema_column_t *c = (query_schema_column_t *) data;

    g_free (c->name);
    g_free (c->text);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_rel_free (gpointer data)
{
    query_schema_rel_t *r = (query_schema_rel_t *) data;

    g_free (r->name);
    if (r->columns != NULL)
        g_ptr_array_free (r->columns, TRUE);
    g_free (r);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_nsp_free (gpointer data)
{
    query_schema_nsp_t *n = (query_schema_nsp_t *) data;

    g_free (n->name);
    g_ptr_array_free (n->rels, TRUE);
    g_free (n);
}

/* --------------------------------------------------------------------------------------------- */

/* What the 0-based @line of the window is of; NULL for none */
static const query_schema_line_t *
query_schema_line_at (const query_pg_schema_t *s, long line)
{
    if (line < 0 || (guint) line >= s->lines->len)
        return NULL;
    return &g_array_index (s->lines, query_schema_line_t, line);
}

/* --------------------------------------------------------------------------------------------- */

static query_schema_nsp_t *
query_schema_nsp (const query_pg_schema_t *s, const query_schema_line_t *l)
{
    return l != NULL && l->nsp >= 0 ? g_ptr_array_index (s->nsps, l->nsp) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static query_schema_rel_t *
query_schema_rel (const query_pg_schema_t *s, const query_schema_line_t *l)
{
    const query_schema_nsp_t *n = query_schema_nsp (s, l);

    return n != NULL && l->rel >= 0 ? g_ptr_array_index (n->rels, l->rel) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* What the cursor of the window is on, kept by its names before the text is made again */
static void
query_schema_mark (query_pg_schema_t *s)
{
    const query_schema_line_t *l = query_schema_line_at (s, query_schema_line (s->q));
    const query_schema_nsp_t *n = query_schema_nsp (s, l);
    const query_schema_rel_t *r = query_schema_rel (s, l);

    g_free (s->keep_nsp);
    g_free (s->keep_rel);
    s->keep_nsp = n != NULL ? g_strdup (n->name) : NULL;
    s->keep_rel = r != NULL ? g_strdup (r->name) : NULL;
    s->keep_col = l != NULL ? l->col : -1;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_add_line (query_pg_schema_t *s, GString *text, int nsp, int rel, int col,
                       const char *line)
{
    const query_schema_line_t l = { nsp, rel, col };

    g_string_append (text, line);
    g_string_append_c (text, '\n');
    g_array_append_val (s->lines, l);
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of @what, each a line of the window that is of nothing, cut at the blanks to the
   width of the window: an error of libpq is a long line */
static void
query_schema_add_lines (query_pg_schema_t *s, GString *text, const char *what)
{
    const long width = MAX (20, (long) COLS * QUERY_SCHEMA_SIZE / 100 - 4);
    char **lines = g_strsplit (what, "\n", -1);
    char **l;

    for (l = lines; *l != NULL; l++)
    {
        const char *rest = *l;

        while (*rest != '\0')
        {
            const char *end = rest, *cut = NULL;
            long n;
            char *part;

            for (n = 0; *end != '\0' && n < width; n++, end = g_utf8_next_char (end))
                if (*end == ' ')
                    cut = end;
            if (*end == '\0' || cut == NULL)
                cut = end;
            part = g_strndup (rest, (gsize) (cut - rest));
            query_schema_add_line (s, text, -1, -1, -1, part);
            g_free (part);
            rest = cut;
            while (*rest == ' ')
                rest++;
        }
    }
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

/* The text of the window made again from what was read, the cursor on what it was on */
static void
query_schema_render (query_pg_schema_t *s)
{
    GString *text = g_string_new (NULL);
    long at = -1;
    guint i, j, k;

    g_array_set_size (s->lines, 0);
    query_schema_add_line (s, text, -1, -1, -1, _ ("Enter columns, F5 rows, Ins the name"));
    if (s->error != NULL)
        query_schema_add_lines (s, text, s->error);
    else if (!s->read)
        query_schema_add_line (s, text, -1, -1, -1, _ ("Reading..."));
    for (i = 0; i < s->nsps->len; i++)
    {
        const query_schema_nsp_t *n = g_ptr_array_index (s->nsps, i);
        const gboolean here = g_strcmp0 (n->name, s->keep_nsp) == 0;
        char *line = g_strdup_printf ("%c %s (%u)", n->open ? '-' : '+', n->name, n->rels->len);

        if (here && s->keep_rel == NULL)
            at = (long) s->lines->len;
        query_schema_add_line (s, text, (int) i, -1, -1, line);
        g_free (line);
        for (j = 0; n->open && j < n->rels->len; j++)
        {
            const query_schema_rel_t *r = g_ptr_array_index (n->rels, j);
            const gboolean rel_here = here && g_strcmp0 (r->name, s->keep_rel) == 0;
            char *count = r->rows >= 0 && (r->kind == 'r' || r->kind == 'm')
                ? g_strdup_printf (" ~%" G_GINT64_FORMAT, r->rows)
                : g_strdup ("");

            line = g_strdup_printf ("  %c %c %s%s", r->open ? '-' : '+',
                                    r->kind == 'r' ? 't' : r->kind, r->name, count);
            if (rel_here && (s->keep_col < 0 || !r->open))
                at = (long) s->lines->len;
            query_schema_add_line (s, text, (int) i, (int) j, -1, line);
            g_free (line);
            g_free (count);
            if (!r->open)
                continue;
            if (r->columns == NULL)
                query_schema_add_line (s, text, (int) i, (int) j, -1, _ ("      Reading..."));
            for (k = 0; r->columns != NULL && k < r->columns->len; k++)
            {
                const query_schema_column_t *c = g_ptr_array_index (r->columns, k);

                if (rel_here && s->keep_col == (int) k)
                    at = (long) s->lines->len;
                line = g_strdup_printf ("      %s %s", c->name, c->text);
                query_schema_add_line (s, text, (int) i, (int) j, (int) k, line);
                g_free (line);
            }
        }
    }
    // the cursor where it was when what it was on is gone, the first schema at the start
    if (at < 0)
        at = s->keep_nsp != NULL || s->lines->len < 2 ? MAX (query_schema_line (s->q), 0) : 1;
    query_schema_set (s->q, text->str, at);
    s->shown = TRUE;
    g_string_free (text, TRUE);
    // an answer of the server comes with no key: the screen is drawn again for it
    query_redraw (s->q);
}

/* --------------------------------------------------------------------------------------------- */
/* The connection */
/* --------------------------------------------------------------------------------------------- */

static int
query_schema_ready (int fd, void *data)
{
    query_pg_schema_t *s = (query_pg_schema_t *) data;

    (void) fd;
    if (s->state == QUERY_SCHEMA_CONNECTING)
        query_schema_poll (s);
    else if (s->state == QUERY_SCHEMA_BUSY)
        query_schema_read_input (s);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The socket watched for a read, and for a write when @write; none for -1 */
static void
query_schema_watch (query_pg_schema_t *s, int fd, gboolean write)
{
    if (s->fd != fd && s->fd >= 0)
        delete_select_channel (s->fd);
    if (fd >= 0 && s->fd != fd)
        add_select_channel (fd, query_schema_ready, s);
    s->fd = fd;
    if (fd >= 0)
        select_channel_want_write (fd, write);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_close_conn (query_pg_schema_t *s)
{
    query_schema_watch (s, -1, FALSE);
    if (s->conn != NULL)
        s->pq->PQfinish (s->conn);
    s->conn = NULL;
    g_clear_pointer (&s->res, s->pq->PQclear);
    g_clear_pointer (&s->res_error, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* The connection failed or was lost, @why: the window tells it, what is asked for next connects
   again */
static void
query_schema_broken (query_pg_schema_t *s, const char *why)
{
    char *error = g_strdup_printf (_ ("No connection: %s"), why);

    query_schema_close_conn (s);
    s->state = QUERY_SCHEMA_BROKEN;
    s->want_list = FALSE;
    g_array_set_size (s->want_columns, 0);
    g_free (s->error);
    s->error = g_strchomp (error);
    query_schema_mark (s);
    query_schema_render (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_poll (query_pg_schema_t *s)
{
    switch (s->pq->PQconnectPoll (s->conn))
    {
    case QUERY_PG_POLLING_READING:
        query_schema_watch (s, s->pq->PQsocket (s->conn), FALSE);
        break;
    case QUERY_PG_POLLING_WRITING:
        query_schema_watch (s, s->pq->PQsocket (s->conn), TRUE);
        break;
    case QUERY_PG_POLLING_OK:
        (void) s->pq->PQsetnonblocking (s->conn, 1);
        query_schema_watch (s, -1, FALSE);
        s->state = QUERY_SCHEMA_READY;
        query_schema_next (s);
        break;
    default:
        query_schema_broken (s, s->pq->PQerrorMessage (s->conn));
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_connect (query_pg_schema_t *s)
{
    query_schema_close_conn (s);
    s->conn = query_pq_start (s->pq, s->read_only);
    if (s->conn == NULL)
    {
        query_schema_broken (s, _ ("libpq has no memory for a connection."));
        return;
    }
    s->state = QUERY_SCHEMA_CONNECTING;
    if (s->pq->PQstatus (s->conn) == QUERY_PG_CONNECTION_BAD)
        query_schema_broken (s, s->pq->PQerrorMessage (s->conn));
    else if (s->pq->PQsocket (s->conn) < 0)
        query_schema_poll (s);
    else
        query_schema_watch (s, s->pq->PQsocket (s->conn), TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_flush (query_pg_schema_t *s)
{
    const int r = s->pq->PQflush (s->conn);

    if (r < 0)
    {
        query_schema_broken (s, s->pq->PQerrorMessage (s->conn));
        return;
    }
    s->flushing = r > 0;
    query_schema_watch (s, s->pq->PQsocket (s->conn), s->flushing);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_send (query_pg_schema_t *s, const char *sql, query_schema_ask_t asked)
{
    s->asked = asked;
    if (s->pq->PQsendQuery (s->conn, sql) == 0)
    {
        query_schema_broken (s, s->pq->PQerrorMessage (s->conn));
        return;
    }
    s->state = QUERY_SCHEMA_BUSY;
    query_schema_flush (s);
}

/* --------------------------------------------------------------------------------------------- */

/* The schemas and relations of the result: the folds of those there before kept, the columns of
   a relation unfolded read again */
static void
query_schema_take_list (query_pg_schema_t *s, PGresult *res)
{
    const query_pq_t *pq = s->pq;
    GPtrArray *was = s->nsps;
    query_schema_nsp_t *n = NULL;
    int i, rows = pq->PQntuples (res);
    guint j, first = 0;

    s->nsps = g_ptr_array_new_with_free_func (query_schema_nsp_free);
    for (i = 0; i < rows; i++)
    {
        const char *nsp = pq->PQgetvalue (res, i, 0);
        query_schema_rel_t *r;

        if (n == NULL || strcmp (n->name, nsp) != 0)
        {
            n = g_new0 (query_schema_nsp_t, 1);
            n->name = g_strdup (nsp);
            n->rels = g_ptr_array_new_with_free_func (query_schema_rel_free);
            // the first time, that of MC_PG_SCHEMA open, else public
            n->open = strcmp (nsp, s->first != NULL ? s->first : "public") == 0;
            for (j = 0; was != NULL && j < was->len; j++)
                if (strcmp (((query_schema_nsp_t *) g_ptr_array_index (was, j))->name, nsp) == 0)
                    n->open = ((query_schema_nsp_t *) g_ptr_array_index (was, j))->open;
            g_ptr_array_add (s->nsps, n);
        }
        if (pq->PQgetisnull (res, i, 1))
            continue;
        r = g_new0 (query_schema_rel_t, 1);
        r->oid = (Oid) strtoul (pq->PQgetvalue (res, i, 1), NULL, 10);
        r->name = g_strdup (pq->PQgetvalue (res, i, 2));
        r->kind = pq->PQgetvalue (res, i, 3)[0];
        r->rows = g_ascii_strtoll (pq->PQgetvalue (res, i, 4), NULL, 10);
        for (j = 0; was != NULL && j < was->len; j++)
        {
            const query_schema_nsp_t *old = g_ptr_array_index (was, j);
            guint k;

            for (k = 0; strcmp (old->name, nsp) == 0 && k < old->rels->len; k++)
            {
                const query_schema_rel_t *o = g_ptr_array_index (old->rels, k);

                if (o->open && strcmp (o->name, r->name) == 0)
                {
                    r->open = TRUE;
                    g_array_append_val (s->want_columns, r->oid);
                }
            }
        }
        g_ptr_array_add (n->rels, r);
    }
    // that of MC_PG_SCHEMA first; the only one open
    for (j = 0; s->first != NULL && j < s->nsps->len; j++)
        if (strcmp (((query_schema_nsp_t *) g_ptr_array_index (s->nsps, j))->name, s->first) == 0)
            first = j;
    if (first > 0)
    {
        gpointer f = g_ptr_array_steal_index (s->nsps, first);

        g_ptr_array_insert (s->nsps, 0, f);
    }
    if (s->nsps->len == 1)
        ((query_schema_nsp_t *) g_ptr_array_index (s->nsps, 0))->open = TRUE;
    if (was != NULL)
        g_ptr_array_free (was, TRUE);
    s->read = TRUE;
    g_clear_pointer (&s->error, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* The columns of the relation of @oid, wherever it is now */
static void
query_schema_take_columns (query_pg_schema_t *s, Oid oid, PGresult *res)
{
    const query_pq_t *pq = s->pq;
    guint i, j;
    int k;

    for (i = 0; i < s->nsps->len; i++)
    {
        const query_schema_nsp_t *n = g_ptr_array_index (s->nsps, i);

        for (j = 0; j < n->rels->len; j++)
        {
            query_schema_rel_t *r = g_ptr_array_index (n->rels, j);

            if (r->oid != oid)
                continue;
            if (r->columns != NULL)
                g_ptr_array_free (r->columns, TRUE);
            r->columns = g_ptr_array_new_with_free_func (query_schema_column_free);
            for (k = 0; res != NULL && k < pq->PQntuples (res); k++)
            {
                query_schema_column_t *c = g_new0 (query_schema_column_t, 1);
                GString *text = g_string_new (pq->PQgetvalue (res, k, 1));
                const char identity = pq->PQgetvalue (res, k, 3)[0];

                c->name = g_strdup (pq->PQgetvalue (res, k, 0));
                if (pq->PQgetvalue (res, k, 2)[0] == 't')
                    g_string_append (text, " not null");
                if (identity != '\0')
                    g_string_append (text, " identity");
                if (pq->PQgetvalue (res, k, 4)[0] != '\0')
                    g_string_append (text, " generated");
                else if (identity == '\0' && !pq->PQgetisnull (res, k, 5))
                {
                    const char *def = pq->PQgetvalue (res, k, 5);

                    if (g_utf8_strlen (def, -1) > QUERY_SCHEMA_DEFAULT)
                        g_string_append_printf (
                            text, " = %.*s...",
                            (int) (g_utf8_offset_to_pointer (def, QUERY_SCHEMA_DEFAULT) - def),
                            def);
                    else
                        g_string_append_printf (text, " = %s", def);
                }
                c->text = g_string_free (text, FALSE);
                g_ptr_array_add (r->columns, c);
            }
            return;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The query of now has ended: what it read taken, the window made again, the next one sent */
static void
query_schema_done (query_pg_schema_t *s)
{
    query_schema_watch (s, -1, FALSE);
    s->state = QUERY_SCHEMA_READY;
    query_schema_mark (s);
    if (s->asked == QUERY_SCHEMA_ASK_LIST)
    {
        if (s->res_error != NULL)
        {
            g_free (s->error);
            s->error = g_strchomp (g_strdup (s->res_error));
        }
        else if (s->res != NULL)
            query_schema_take_list (s, s->res);
    }
    else
        // an error is the columns of none: the relation shows nothing under it
        query_schema_take_columns (s, s->asked_oid, s->res_error == NULL ? s->res : NULL);
    g_clear_pointer (&s->res, s->pq->PQclear);
    g_clear_pointer (&s->res_error, g_free);
    query_schema_render (s);
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_read_input (query_pg_schema_t *s)
{
    const query_pq_t *pq = s->pq;
    PGresult *res;

    if (s->flushing)
    {
        query_schema_flush (s);
        if (s->state != QUERY_SCHEMA_BUSY)
            return;
    }
    if (pq->PQconsumeInput (s->conn) == 0)
    {
        query_schema_broken (s, pq->PQerrorMessage (s->conn));
        return;
    }
    while (pq->PQisBusy (s->conn) == 0)
    {
        res = pq->PQgetResult (s->conn);
        if (res == NULL)
        {
            query_schema_done (s);
            return;
        }
        if (pq->PQresultStatus (res) == QUERY_PG_TUPLES_OK)
        {
            if (s->res != NULL)
                pq->PQclear (s->res);
            s->res = res;
        }
        else
        {
            if (pq->PQresultStatus (res) != QUERY_PG_COMMAND_OK && s->res_error == NULL)
                s->res_error = g_strdup (pq->PQresultErrorMessage (res));
            pq->PQclear (res);
        }
    }
    if (pq->PQstatus (s->conn) == QUERY_PG_CONNECTION_BAD)
        query_schema_broken (s, pq->PQerrorMessage (s->conn));
}

/* --------------------------------------------------------------------------------------------- */

/* What is asked for sent, the list before the columns; the connection made first */
static void
query_schema_next (query_pg_schema_t *s)
{
    const gboolean wanted = s->want_list || s->want_columns->len > 0;

    if (!wanted)
        return;
    if (s->state == QUERY_SCHEMA_OFF || s->state == QUERY_SCHEMA_BROKEN)
    {
        query_schema_connect (s);
        return;
    }
    if (s->state != QUERY_SCHEMA_READY)
        return;
    if (s->want_list)
    {
        s->want_list = FALSE;
        query_schema_send (s, QUERY_SCHEMA_LIST, QUERY_SCHEMA_ASK_LIST);
    }
    else
    {
        char *sql;

        s->asked_oid = g_array_index (s->want_columns, Oid, 0);
        g_array_remove_index (s->want_columns, 0);
        sql = g_strdup_printf (QUERY_SCHEMA_COLUMNS, s->asked_oid);
        query_schema_send (s, sql, QUERY_SCHEMA_ASK_COLUMNS);
        g_free (sql);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The list read again, the window made when it is not there yet */
static void
query_schema_refresh (query_pg_schema_t *s)
{
    if (s->state == QUERY_SCHEMA_BROKEN)
        g_clear_pointer (&s->error, g_free);
    s->want_list = TRUE;
    if (!s->shown || s->error == NULL)
    {
        query_schema_mark (s);
        query_schema_render (s);
    }
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* Enter on @l: a schema folded or unfolded, the columns of a relation, read the first time */
static void
query_schema_enter (query_pg_schema_t *s, const query_schema_line_t *l)
{
    query_schema_nsp_t *n = query_schema_nsp (s, l);
    query_schema_rel_t *r = query_schema_rel (s, l);

    if (n == NULL || l->col >= 0)
        return;
    query_schema_mark (s);
    if (r == NULL)
        n->open = !n->open;
    else
    {
        r->open = !r->open;
        if (r->open && r->columns == NULL)
            g_array_append_val (s->want_columns, r->oid);
    }
    query_schema_render (s);
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* The name of @l put at the cursor of the file of queries: a schema, its relation with it, a
   column, as SQL takes them */
static void
query_schema_insert (query_pg_schema_t *s, const query_schema_line_t *l)
{
    const query_schema_nsp_t *n = query_schema_nsp (s, l);
    const query_schema_rel_t *r = query_schema_rel (s, l);
    void *edit = s->q->host->window_top_file (s->q->host);
    char *name;

    if (n == NULL || edit == NULL)
        return;
    if (r == NULL)
        name = query_pg_ident (n->name);
    else if (l->col >= 0)
        name = query_pg_ident (
            ((query_schema_column_t *) g_ptr_array_index (r->columns, l->col))->name);
    else
    {
        char *nsp = query_pg_ident (n->name);
        char *rel = query_pg_ident (r->name);

        name = g_strconcat (nsp, ".", rel, (char *) NULL);
        g_free (nsp);
        g_free (rel);
    }
    s->q->host->insert_text (s->q->host, edit, name, 0);
    g_free (name);
    query_redraw (s->q);
}

/* --------------------------------------------------------------------------------------------- */

/* The rows of the relation of @l in Table, a query of no file */
static void
query_schema_rows (query_pg_schema_t *s, const query_schema_line_t *l)
{
    const query_schema_nsp_t *n = query_schema_nsp (s, l);
    const query_schema_rel_t *r = query_schema_rel (s, l);
    char *sql;

    if (r == NULL)
        return;
    if (s->q->backend->running (s->q->backend_data))
    {
        s->q->host->message (s->q->host, D_NORMAL, _ ("Query"),
                             _ ("A query is running: its rows come once it ends."));
        return;
    }
    sql = query_pg_relation_query (n->name, r->name);
    query_run_alone (s->q, sql);
    g_free (sql);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

query_pg_schema_t *
query_pg_schema_new (query_t *q, const query_pq_t *pq, gboolean read_only, const char *schema)
{
    query_pg_schema_t *s = g_new0 (query_pg_schema_t, 1);

    s->q = q;
    s->pq = pq;
    s->read_only = read_only;
    s->first = schema != NULL && *schema != '\0' ? g_strdup (schema) : NULL;
    s->fd = -1;
    s->keep_col = -1;
    s->want_columns = g_array_new (FALSE, FALSE, sizeof (Oid));
    s->nsps = g_ptr_array_new_with_free_func (query_schema_nsp_free);
    s->lines = g_array_new (FALSE, FALSE, sizeof (query_schema_line_t));
    return s;
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_free (query_pg_schema_t *s)
{
    if (s == NULL)
        return;
    query_schema_close_conn (s);
    g_array_free (s->want_columns, TRUE);
    g_ptr_array_free (s->nsps, TRUE);
    g_array_free (s->lines, TRUE);
    g_free (s->first);
    g_free (s->error);
    g_free (s->keep_nsp);
    g_free (s->keep_rel);
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_do (query_pg_schema_t *s, query_schema_action_t action, long line)
{
    const query_schema_line_t *l = query_schema_line_at (s, line);

    switch (action)
    {
    case QUERY_SCHEMA_SHOW:
        if (!s->read && s->state != QUERY_SCHEMA_BUSY)
            query_schema_refresh (s);
        else
        {
            query_schema_mark (s);
            query_schema_render (s);
        }
        break;
    case QUERY_SCHEMA_REFRESH:
        query_schema_refresh (s);
        break;
    case QUERY_SCHEMA_ENTER:
        query_schema_enter (s, l);
        break;
    case QUERY_SCHEMA_ROWS:
        query_schema_rows (s, l);
        break;
    case QUERY_SCHEMA_INSERT:
        query_schema_insert (s, l);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_changed (query_pg_schema_t *s)
{
    if (s->read)
        query_schema_refresh (s);
}

/* --------------------------------------------------------------------------------------------- */
