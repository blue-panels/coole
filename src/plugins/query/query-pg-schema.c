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
 *  does not block either: the schemas, their relations and functions once, in groups by their
 *  kind, the partitions under their table; the columns, keys, indexes, references and triggers of
 *  a relation when it is unfolded.  Its text is made again from that tree each time a line is
 *  folded or unfolded, the cursor staying on what it was on, by the names of its path.
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
#include "query-tree.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* The queries of the catalog, those of misc/query: pg-list.sql, pg-details.sql, pg-ai.sql,
   pg-ddl-relation.sql, pg-ddl-function.sql */

// the relations AI is told of at most, those of MC_PG_SCHEMA first
#define QUERY_SCHEMA_AI_MAX 300

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
    QUERY_SCHEMA_ASK_DETAILS,
    QUERY_SCHEMA_ASK_AI,
    QUERY_SCHEMA_ASK_DDL,
    QUERY_SCHEMA_ASK_ROLLBACK
} query_schema_ask_t;

/* The groups of a schema, in their order */
enum
{
    QUERY_GROUP_TABLES,
    QUERY_GROUP_VIEWS,
    QUERY_GROUP_MATVIEWS,
    QUERY_GROUP_FOREIGN,
    QUERY_GROUP_SEQUENCES,
    QUERY_GROUP_FUNCTIONS,
    QUERY_GROUP_COUNT
};

/* The sections of a relation unfolded, in their order and in that of the results of
   pg-details.sql, the keys and the foreign keys being of one */
enum
{
    QUERY_SECTION_COLUMNS,
    QUERY_SECTION_KEYS,
    QUERY_SECTION_FOREIGN,
    QUERY_SECTION_INDEXES,
    QUERY_SECTION_REFERENCES,
    QUERY_SECTION_TRIGGERS,
    QUERY_SECTION_COUNT
};

struct query_pg_schema_t
{
    query_t *q;
    const query_pq_t *pq;
    gboolean read_only;
    char *first;  // MC_PG_SCHEMA, first in the list and unfolded

    query_pq_link_t link;
    query_schema_state_t state;
    query_schema_ask_t asked;
    Oid asked_oid;       // of the relation whose sections are asked for
    GPtrArray *results;  // PGresult, of the statements of the query of now
    char *res_error;     // its error

    gboolean want_list;
    gboolean want_rollback;  // a query of its own ended in a transaction: it is rolled back first
    gboolean want_ai;        // what AI is told, to be sent
    gboolean ai_sent;        // and sent
    GArray *want_details;    // Oid of the relations whose sections are to be read
    Oid want_ddl;            // of the relation or the function whose DDL F4 asks for, 0 for none
    gboolean want_ddl_function;
    char *want_ddl_name;  // the relation as SQL takes it
    guint want_ddl_id;    // the F4 it is of
    gboolean asked_ddl_function;
    char *asked_ddl_name;
    guint asked_ddl_id;

    query_tree_t tree;
};

/*** forward declarations (file scope functions) *************************************************/

static void query_schema_next (query_pg_schema_t *s);
static void query_schema_read_input (query_pg_schema_t *s);

/*** file scope variables ************************************************************************/

static const char *const query_group_names[QUERY_GROUP_COUNT] = {
    N_ ("Tables"),         N_ ("Views"),     N_ ("Materialized views"),
    N_ ("Foreign tables"), N_ ("Sequences"), N_ ("Functions"),
};

static const char *const query_section_names[QUERY_SECTION_COUNT] = {
    N_ ("Columns"), N_ ("Keys"),       N_ ("Foreign keys"),
    N_ ("Indexes"), N_ ("References"), N_ ("Triggers"),
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/* The connection */
/* --------------------------------------------------------------------------------------------- */

/* The socket is ready while a query runs: query_pq_link_t.ready */
static void
query_schema_ready (void *data)
{
    query_pg_schema_t *s = (query_pg_schema_t *) data;

    if (s->state == QUERY_SCHEMA_BUSY)
        query_schema_read_input (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_close_conn (query_pg_schema_t *s)
{
    query_pq_link_close (&s->link);
    g_ptr_array_set_size (s->results, 0);
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
    s->want_rollback = FALSE;
    g_array_set_size (s->want_details, 0);
    s->want_ddl = 0;
    query_tree_unask (&s->tree);
    g_free (s->tree.error);
    s->tree.error = g_strchomp (error);
    query_tree_render (&s->tree);
    if (s->want_ai || s->ai_sent)
    {
        s->want_ai = s->ai_sent = FALSE;
        query_ai_context (s->q, NULL, s->tree.error);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The connection is made, or failed: query_pq_link_t.connected */
static void
query_schema_connected (void *data, gboolean ok)
{
    query_pg_schema_t *s = (query_pg_schema_t *) data;

    if (!ok)
    {
        query_schema_broken (s, s->pq->PQerrorMessage (s->link.conn));
        return;
    }
    s->state = QUERY_SCHEMA_READY;
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_connect (query_pg_schema_t *s)
{
    s->state = QUERY_SCHEMA_CONNECTING;
    if (!query_pq_link_open (&s->link, s->read_only))
        query_schema_broken (s, _ ("libpq has no memory for a connection."));
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_flush (query_pg_schema_t *s)
{
    if (!query_pq_link_flush (&s->link))
        query_schema_broken (s, s->pq->PQerrorMessage (s->link.conn));
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_send (query_pg_schema_t *s, const char *sql, query_schema_ask_t asked)
{
    s->asked = asked;
    if (s->pq->PQsendQuery (s->link.conn, sql) == 0)
    {
        query_schema_broken (s, s->pq->PQerrorMessage (s->link.conn));
        return;
    }
    s->state = QUERY_SCHEMA_BUSY;
    query_schema_flush (s);
}

/* --------------------------------------------------------------------------------------------- */
/* What is read */
/* --------------------------------------------------------------------------------------------- */

/* The Oid of the field @field of the row @row of @res */
static Oid
query_schema_oid (const query_pq_t *pq, PGresult *res, int row, int field)
{
    return (Oid) strtoul (pq->PQgetvalue (res, row, field), NULL, 10);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/* F4: the DDL */
/* --------------------------------------------------------------------------------------------- */

/* The field @field of the row @row of @res, NULL when it is null or there is no such row */
static const char *
query_ddl_value (const query_pq_t *pq, PGresult *res, int row, int field)
{
    if (res == NULL || row >= pq->PQntuples (res) || pq->PQgetisnull (res, row, field))
        return NULL;
    return pq->PQgetvalue (res, row, field);
}

/* --------------------------------------------------------------------------------------------- */

/* @text, a query of a view, with no ";" and no blank at its end */
static void
query_ddl_append_query (GString *ddl, const char *text)
{
    gsize len = strlen (text);

    while (len > 0 && (g_ascii_isspace (text[len - 1]) || text[len - 1] == ';'))
        len--;
    g_string_append_len (ddl, text, (gssize) len);
}

/* --------------------------------------------------------------------------------------------- */

/* @text put as comments: each of its lines, a name in it with a line break of its own too, after
   "-- "; a line ends at "\n", "\r" or "\r\n", as a comment of SQL does */
static void
query_ddl_append_comment (GString *ddl, const char *text)
{
    const char *at;

    for (at = text; *at != '\0';)
    {
        const gsize len = strcspn (at, "\r\n");

        g_string_append (ddl, "-- ");
        g_string_append_len (ddl, at, (gssize) len);
        g_string_append_c (ddl, '\n');
        at += len;
        if (at[0] == '\r' && at[1] == '\n')
            at += 2;
        else if (*at != '\0')
            at++;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The CREATE of the sequence @name of the row @row of @res, its type and its numbers from the
   field @at, unlogged when @unlogged, commented when @commented */
static void
query_ddl_append_sequence (const query_pq_t *pq, GString *ddl, const char *name, PGresult *res,
                           int row, int at, gboolean unlogged, gboolean commented)
{
    char *text =
        g_strdup_printf ("CREATE %sSEQUENCE %s AS %s\n    INCREMENT BY %s MINVALUE %s MAXVALUE %s\n"
                         "    START WITH %s CACHE %s %sCYCLE;\n",
                         unlogged ? "UNLOGGED " : "", name, pq->PQgetvalue (res, row, at),
                         pq->PQgetvalue (res, row, at + 2), pq->PQgetvalue (res, row, at + 3),
                         pq->PQgetvalue (res, row, at + 4), pq->PQgetvalue (res, row, at + 1),
                         pq->PQgetvalue (res, row, at + 5),
                         pq->PQgetvalue (res, row, at + 6)[0] == 't' ? "" : "NO ");

    if (commented)
        query_ddl_append_comment (ddl, text);
    else
        g_string_append (ddl, text);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

/* The numbers of the sequence of the identity of the row @row of the columns @cols, those that
   are not those PostgreSQL gives by itself, as GENERATED ... AS IDENTITY takes them: " (START
   WITH 100 INCREMENT BY 10)"; nothing when they all are */
static void
query_ddl_append_identity (const query_pq_t *pq, GString *ddl, PGresult *cols, int row)
{
    const char *type = query_ddl_value (pq, cols, row, 9);
    GString *opts;
    gint64 inc, min, max, start, type_min, type_max;

    if (type == NULL)
        return;
    type_max = strcmp (type, "smallint") == 0 ? G_MAXINT16
        : strcmp (type, "integer") == 0       ? G_MAXINT32
                                              : G_MAXINT64;
    type_min = strcmp (type, "smallint") == 0 ? G_MININT16
        : strcmp (type, "integer") == 0       ? G_MININT32
                                              : G_MININT64;
    start = g_ascii_strtoll (pq->PQgetvalue (cols, row, 10), NULL, 10);
    inc = g_ascii_strtoll (pq->PQgetvalue (cols, row, 11), NULL, 10);
    min = g_ascii_strtoll (pq->PQgetvalue (cols, row, 12), NULL, 10);
    max = g_ascii_strtoll (pq->PQgetvalue (cols, row, 13), NULL, 10);
    opts = g_string_new (NULL);
    // a name of its own, not the one PostgreSQL gives it
    if (query_ddl_value (pq, cols, row, 19) != NULL
        && g_strcmp0 (pq->PQgetvalue (cols, row, 19), query_ddl_value (pq, cols, row, 20)) != 0)
        g_string_append_printf (opts, " SEQUENCE NAME %s", pq->PQgetvalue (cols, row, 19));
    // ascending from 1 to the end of its type, descending from -1, starting at its end
    if (inc != 1)
        g_string_append_printf (opts, " INCREMENT BY %" G_GINT64_FORMAT, inc);
    if (min != (inc > 0 ? 1 : type_min))
        g_string_append_printf (opts, " MINVALUE %" G_GINT64_FORMAT, min);
    if (max != (inc > 0 ? type_max : -1))
        g_string_append_printf (opts, " MAXVALUE %" G_GINT64_FORMAT, max);
    if (start != (inc > 0 ? min : max))
        g_string_append_printf (opts, " START WITH %" G_GINT64_FORMAT, start);
    if (strcmp (pq->PQgetvalue (cols, row, 14), "1") != 0)
        g_string_append_printf (opts, " CACHE %s", pq->PQgetvalue (cols, row, 14));
    if (pq->PQgetvalue (cols, row, 15)[0] == 't')
        g_string_append (opts, " CYCLE");
    if (opts->len > 0)
        g_string_append_printf (ddl, " (%s)", opts->str + 1);
    g_string_free (opts, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* How many of the constraints @cons are validated: those in the parentheses of a CREATE */
static int
query_ddl_validated (const query_pq_t *pq, PGresult *cons)
{
    int i, n = 0;

    for (i = 0; cons != NULL && i < pq->PQntuples (cons); i++)
        if (pq->PQgetvalue (cons, i, 2)[0] == 't')
            n++;
    return n;
}

/* --------------------------------------------------------------------------------------------- */

/* The columns and the constraints of a table, in its parentheses; a partition has those of its
   table, its own constraints alone; one that inherits from others, @inherits, its own columns
   alone, those of the others and their CHECK coming with INHERITS */
static void
query_ddl_append_columns (const query_pq_t *pq, GString *ddl, PGresult *cols, PGresult *cons,
                          gboolean partition, gboolean inherits)
{
    gboolean first = TRUE;
    int i;

    for (i = 0; !partition && cols != NULL && i < pq->PQntuples (cols); i++)
    {
        const char *identity = query_ddl_value (pq, cols, i, 3);
        const char *generated = query_ddl_value (pq, cols, i, 4);
        const char *expr = query_ddl_value (pq, cols, i, 5);
        const char *collation = query_ddl_value (pq, cols, i, 6);

        // its parents give it defaults that are not the same: CREATE takes it as its own, with its
        // default, or fails
        if (inherits && pq->PQgetvalue (cols, i, 7)[0] != 't'
            && g_ascii_strtoll (pq->PQgetvalue (cols, i, 21), NULL, 10) < 2)
            continue;
        g_string_append_printf (ddl, "%s\n    %s %s", first ? "" : ",", pq->PQgetvalue (cols, i, 0),
                                pq->PQgetvalue (cols, i, 1));
        first = FALSE;
        // of a foreign table: the column of the server it is, column_name
        if (query_ddl_value (pq, cols, i, 18) != NULL)
            g_string_append_printf (ddl, " OPTIONS (%s)", pq->PQgetvalue (cols, i, 18));
        if (collation != NULL)
            g_string_append_printf (ddl, " COLLATE %s", collation);
        if (generated != NULL && *generated != '\0' && expr != NULL)
            g_string_append_printf (ddl, " GENERATED ALWAYS AS (%s) %s", expr,
                                    *generated == 'v' ? "VIRTUAL" : "STORED");
        else if (identity != NULL && *identity != '\0')
        {
            g_string_append_printf (ddl, " GENERATED %s AS IDENTITY",
                                    *identity == 'a' ? "ALWAYS" : "BY DEFAULT");
            query_ddl_append_identity (pq, ddl, cols, i);
        }
        else if (expr != NULL)
            g_string_append_printf (ddl, " DEFAULT %s", expr);
        if (pq->PQgetvalue (cols, i, 2)[0] == 't')
            g_string_append (ddl, " NOT NULL");
    }
    for (i = 0; cons != NULL && i < pq->PQntuples (cons); i++)
    {
        const char *def = pq->PQgetvalue (cons, i, 1);
        const char *storage = query_ddl_value (pq, cons, i, 3);
        // the storage of the index of a key goes before DEFERRABLE
        const char *attrs = strstr (def, " DEFERRABLE");

        // one NOT VALID is added after the table: made in it, it is validated
        if (pq->PQgetvalue (cons, i, 2)[0] != 't')
            continue;
        g_string_append_printf (ddl, "%s\n    CONSTRAINT %s ", first ? "" : ",",
                                pq->PQgetvalue (cons, i, 0));
        if (storage != NULL && *storage != '\0')
        {
            if (attrs == NULL)
                attrs = def + strlen (def);
            g_string_append_len (ddl, def, (gssize) (attrs - def));
            g_string_append_printf (ddl, " WITH (%s)%s", storage, attrs);
        }
        else
            g_string_append (ddl, def);
        first = FALSE;
    }
    if (!first)
        g_string_append (ddl, "\n");
}

/* --------------------------------------------------------------------------------------------- */

/* The DDL of the relation @name of the results of pg-ddl-relation.sql: its drop,
   commented, its create, its indexes and triggers, its comments */
static char *
query_ddl_relation (const query_pq_t *pq, const char *name, GPtrArray *results)
{
    PGresult *rel = g_ptr_array_index (results, 0);
    PGresult *cols = g_ptr_array_index (results, 1);
    PGresult *cons = g_ptr_array_index (results, 2);
    PGresult *idx = g_ptr_array_index (results, 3);
    PGresult *trg = g_ptr_array_index (results, 4);
    PGresult *seq = g_ptr_array_index (results, 5);
    PGresult *owned = g_ptr_array_index (results, 6);
    GString *ddl = g_string_new (NULL);
    const char *kind, *what, *options, *comment;
    char *drop;
    gboolean partition;
    int i;

    if (pq->PQntuples (rel) == 0)
    {
        g_string_free (ddl, TRUE);
        return NULL;
    }
    kind = pq->PQgetvalue (rel, 0, 0);
    what = *kind == 'v' ? "VIEW"
        : *kind == 'm'  ? "MATERIALIZED VIEW"
        : *kind == 'f'  ? "FOREIGN TABLE"
        : *kind == 'S'  ? "SEQUENCE"
                        : "TABLE";
    partition = pq->PQgetvalue (rel, 0, 2)[0] == 't' && query_ddl_value (pq, rel, 0, 3) != NULL;
    options = query_ddl_value (pq, rel, 0, 9);
    if (options != NULL && *options == '\0')
        options = NULL;

    drop = g_strdup_printf ("DROP %s %s;\n", what, name);
    query_ddl_append_comment (ddl, drop);
    g_free (drop);
    if (*kind == 'v' || *kind == 'm')
    {
        if (*kind == 'v')
            g_string_append_printf (ddl, "CREATE OR REPLACE VIEW %s", name);
        else
            g_string_append_printf (ddl, "CREATE MATERIALIZED VIEW %s", name);
        if (options != NULL)
            g_string_append_printf (ddl, " WITH (%s)", options);
        g_string_append (ddl, " AS\n");
        query_ddl_append_query (
            ddl, query_ddl_value (pq, rel, 0, 6) != NULL ? query_ddl_value (pq, rel, 0, 6) : "");
        // a materialized view not populated is made so: its query is not run
        if (*kind == 'm')
            g_string_append (ddl,
                             pq->PQgetvalue (rel, 0, 12)[0] == 'f' ? "\nWITH NO DATA;\n"
                                                                   : "\nWITH DATA;\n");
        else
            g_string_append (ddl, ";\n");
        // the defaults of the columns of a view, for the INSERT through it
        for (i = 0; *kind == 'v' && cols != NULL && i < pq->PQntuples (cols); i++)
            if (query_ddl_value (pq, cols, i, 5) != NULL)
                g_string_append_printf (ddl, "ALTER VIEW %s ALTER COLUMN %s SET DEFAULT %s;\n",
                                        name, pq->PQgetvalue (cols, i, 0),
                                        pq->PQgetvalue (cols, i, 5));
    }
    else if (*kind == 'S')
    {
        const char *dep = query_ddl_value (pq, seq, 0, 7);
        const char *column = query_ddl_value (pq, seq, 0, 8);
        // that of an identity is made and dropped with its table: its create told, commented
        const gboolean identity = dep != NULL && *dep == 'i' && column != NULL;

        if (seq == NULL || pq->PQntuples (seq) == 0)
            g_string_append_printf (ddl, "CREATE %sSEQUENCE %s;\n",
                                    pq->PQgetvalue (rel, 0, 1)[0] == 'u' ? "UNLOGGED " : "", name);
        else
        {
            if (identity)
            {
                char *note = g_strdup_printf (
                    "the sequence of the identity of %s, made and dropped with it\n", column);

                query_ddl_append_comment (ddl, note);
                g_free (note);
            }
            query_ddl_append_sequence (pq, ddl, name, seq, 0, 0,
                                       pq->PQgetvalue (rel, 0, 1)[0] == 'u', identity);
            if (dep != NULL && *dep == 'a' && column != NULL)
                g_string_append_printf (ddl, "ALTER SEQUENCE %s OWNED BY %s;\n", name, column);
        }
    }
    else
    {
        const gboolean unlogged = pq->PQgetvalue (rel, 0, 1)[0] == 'u';
        const char *key = query_ddl_value (pq, rel, 0, 5);
        // the tables it inherits from, not as a partition: INHERITS
        const char *parents = partition ? NULL : query_ddl_value (pq, rel, 0, 11);

        /* the sequences of its serial columns go with it: made first, for their default, owned
           by their columns once it is */
        for (i = 0; owned != NULL && i < pq->PQntuples (owned); i++)
            query_ddl_append_sequence (pq, ddl, pq->PQgetvalue (owned, i, 0), owned, i, 1,
                                       pq->PQgetvalue (owned, i, 9)[0] == 'u', FALSE);
        g_string_append_printf (ddl, "CREATE %s%s %s", unlogged ? "UNLOGGED " : "", what, name);
        if (partition)
            g_string_append_printf (ddl, " PARTITION OF %s", pq->PQgetvalue (rel, 0, 3));
        if (!partition || query_ddl_validated (pq, cons) > 0)
        {
            g_string_append (ddl, " (");
            query_ddl_append_columns (pq, ddl, cols, cons, partition, parents != NULL);
            g_string_append (ddl, ")");
        }
        if (parents != NULL)
            g_string_append_printf (ddl, "\nINHERITS (%s)", parents);
        if (partition && query_ddl_value (pq, rel, 0, 4) != NULL)
            g_string_append_printf (ddl, "\n%s", pq->PQgetvalue (rel, 0, 4));
        if (key != NULL)
            g_string_append_printf (ddl, "\nPARTITION BY %s", key);
        if (*kind == 'f' && query_ddl_value (pq, rel, 0, 7) != NULL)
        {
            g_string_append_printf (ddl, "\nSERVER %s", pq->PQgetvalue (rel, 0, 7));
            if (query_ddl_value (pq, rel, 0, 8) != NULL)
                g_string_append_printf (ddl, "\nOPTIONS (%s)", pq->PQgetvalue (rel, 0, 8));
        }
        if (options != NULL)
            g_string_append_printf (ddl, "\nWITH (%s)", options);
        g_string_append (ddl, ";\n");
        // an inherited column, of a partition too, as the table has made it, not as its parents
        // give it
        for (i = 0; (parents != NULL || partition) && cols != NULL && i < pq->PQntuples (cols); i++)
        {
            const char *col = pq->PQgetvalue (cols, i, 0);
            const char *own = query_ddl_value (pq, cols, i, 5);
            const char *given = query_ddl_value (pq, cols, i, 16);
            const char *generated = query_ddl_value (pq, cols, i, 4);

            if (pq->PQgetvalue (cols, i, 7)[0] == 't' || (generated != NULL && *generated != '\0')
                || g_ascii_strtoll (pq->PQgetvalue (cols, i, 21), NULL, 10) > 1)
                continue;
            if (own != NULL && g_strcmp0 (own, given) != 0)
                g_string_append_printf (ddl, "ALTER TABLE %s ALTER COLUMN %s SET DEFAULT %s;\n",
                                        name, col, own);
            else if (own == NULL && given != NULL)
                g_string_append_printf (ddl, "ALTER TABLE %s ALTER COLUMN %s DROP DEFAULT;\n", name,
                                        col);
            if (pq->PQgetvalue (cols, i, 2)[0] == 't'
                && g_strcmp0 (query_ddl_value (pq, cols, i, 17), "t") != 0)
                g_string_append_printf (ddl, "ALTER TABLE %s ALTER COLUMN %s SET NOT NULL;\n", name,
                                        col);
        }
    }

    // its constraints NOT VALID, which a CREATE would validate
    for (i = 0; (*kind == 'r' || *kind == 'p' || *kind == 'f') && i < pq->PQntuples (cons); i++)
        if (pq->PQgetvalue (cons, i, 2)[0] != 't')
            g_string_append_printf (ddl, "ALTER TABLE %s ADD CONSTRAINT %s %s;\n", name,
                                    pq->PQgetvalue (cons, i, 0), pq->PQgetvalue (cons, i, 1));
    for (i = 0; owned != NULL && i < pq->PQntuples (owned); i++)
        g_string_append_printf (ddl, "ALTER SEQUENCE %s OWNED BY %s.%s;\n",
                                pq->PQgetvalue (owned, i, 0), name, pq->PQgetvalue (owned, i, 8));
    for (i = 0; idx != NULL && i < pq->PQntuples (idx); i++)
        g_string_append_printf (ddl, "%s;\n", pq->PQgetvalue (idx, i, 0));
    for (i = 0; trg != NULL && i < pq->PQntuples (trg); i++)
    {
        const char mode = pq->PQgetvalue (trg, i, 2)[0];
        // the mode of the trigger of the table of the partition it is the clone of
        const char *of = query_ddl_value (pq, trg, i, 3);

        // a clone PARTITION OF makes
        if (of == NULL)
            g_string_append_printf (ddl, "%s;\n", pq->PQgetvalue (trg, i, 0));
        if (mode != (of != NULL ? of[0] : 'O'))
            g_string_append_printf (ddl, "ALTER TABLE %s %s TRIGGER %s;\n", name,
                                    mode == 'D'       ? "DISABLE"
                                        : mode == 'R' ? "ENABLE REPLICA"
                                        : mode == 'A' ? "ENABLE ALWAYS"
                                                      : "ENABLE",
                                    pq->PQgetvalue (trg, i, 1));
    }
    comment = query_ddl_value (pq, rel, 0, 10);
    if (comment != NULL)
        g_string_append_printf (ddl, "COMMENT ON %s %s IS %s;\n", what, name, comment);
    for (i = 0; cols != NULL && i < pq->PQntuples (cols); i++)
        if (query_ddl_value (pq, cols, i, 8) != NULL)
            g_string_append_printf (ddl, "COMMENT ON COLUMN %s.%s IS %s;\n", name,
                                    pq->PQgetvalue (cols, i, 0), pq->PQgetvalue (cols, i, 8));
    return g_string_free (ddl, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The DDL of the function of the result of pg-ddl-function.sql */
static char *
query_ddl_function (const query_pq_t *pq, PGresult *res)
{
    const char *def, *kind, *name;
    char *drop;
    GString *ddl;

    if (res == NULL || pq->PQntuples (res) == 0)
        return NULL;
    def = query_ddl_value (pq, res, 0, 0);
    kind = pq->PQgetvalue (res, 0, 1);
    name = pq->PQgetvalue (res, 0, 2);
    ddl = g_string_new (NULL);
    drop = g_strdup_printf ("DROP %s %s;\n",
                            *kind == 'p'       ? "PROCEDURE"
                                : *kind == 'a' ? "AGGREGATE"
                                               : "FUNCTION",
                            name);
    query_ddl_append_comment (ddl, drop);
    g_free (drop);
    if (def == NULL)
        g_string_append (ddl, "-- PostgreSQL gives no CREATE of an aggregate\n");
    else
    {
        query_ddl_append_query (ddl, def);
        g_string_append (ddl, ";\n");
    }
    return g_string_free (ddl, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The schemas, their groups, the relations and the functions of the results of pg-list.sql:
   what was open before open again, the sections of a relation unfolded read again */
static void
query_schema_take_list (query_pg_schema_t *s, PGresult *rels, PGresult *funcs)
{
    const query_pq_t *pq = s->pq;
    const gboolean again = s->tree.read;
    GHashTable *by_oid = g_hash_table_new (g_direct_hash, g_direct_equal);
    GHashTable *by_name = g_hash_table_new (g_str_hash, g_str_equal);
    GPtrArray *parts = g_ptr_array_new ();  // the rows of partitions, as int
    query_node_t *groups[QUERY_GROUP_COUNT];
    query_node_t *n = NULL;
    GPtrArray *roots;
    int i, rows;
    guint j, first = 0;
    gboolean placed;

    // what was open, by the paths of the nodes
    query_tree_forget (&s->tree);

    rows = rels != NULL ? pq->PQntuples (rels) : 0;
    for (i = 0; i < rows; i++)
    {
        const char *nsp = pq->PQgetvalue (rels, i, 0);
        query_node_t *r;
        char kind;
        int g;

        if (n == NULL || strcmp (n->key, nsp) != 0)
        {
            n = query_node_new (&s->tree, NULL, QUERY_NODE_ROOT, g_strdup (nsp), NULL,
                                query_pg_ident (nsp));
            g_hash_table_insert (by_name, n->key, n);
            // the first time, that of MC_PG_SCHEMA open, else public
            n->expanded = query_tree_was_open (
                &s->tree, n, strcmp (nsp, s->first != NULL ? s->first : "public") == 0);
            memset (groups, 0, sizeof (groups));
        }
        if (pq->PQgetisnull (rels, i, 1))
            continue;
        if (pq->PQgetvalue (rels, i, 5)[0] == 't' && !pq->PQgetisnull (rels, i, 6))
        {
            g_ptr_array_add (parts, GINT_TO_POINTER (i));
            continue;
        }
        kind = pq->PQgetvalue (rels, i, 3)[0];
        g = kind == 'v'   ? QUERY_GROUP_VIEWS
            : kind == 'm' ? QUERY_GROUP_MATVIEWS
            : kind == 'f' ? QUERY_GROUP_FOREIGN
            : kind == 'S' ? QUERY_GROUP_SEQUENCES
                          : QUERY_GROUP_TABLES;
        if (groups[g] == NULL)
            groups[g] =
                query_node_group (&s->tree, n, _ (query_group_names[g]), g == QUERY_GROUP_TABLES);
        r = query_node_new (&s->tree, groups[g], QUERY_NODE_OBJECT,
                            g_strdup (pq->PQgetvalue (rels, i, 2)), NULL, NULL);
        g_hash_table_insert (by_oid, GUINT_TO_POINTER (query_schema_oid (pq, rels, i, 1)), r);
    }
    /* the partitions under their table, those of a partition under it, in a section of its own:
       as many rounds as a partition may come before the partition it is of */
    for (placed = TRUE; placed;)
    {
        placed = FALSE;
        for (j = 0; j < parts->len; j++)
        {
            const int row = GPOINTER_TO_INT (g_ptr_array_index (parts, j));
            query_node_t *table = row < 0
                ? NULL
                : g_hash_table_lookup (by_oid,
                                       GUINT_TO_POINTER (query_schema_oid (pq, rels, row, 6)));
            query_node_t *r;

            if (table == NULL)
                continue;
            r = query_node_new (&s->tree, query_node_parts (&s->tree, table, _ ("Partitions")),
                                QUERY_NODE_OBJECT, g_strdup (pq->PQgetvalue (rels, row, 2)), NULL,
                                NULL);
            g_hash_table_insert (by_oid, GUINT_TO_POINTER (query_schema_oid (pq, rels, row, 1)), r);
            g_ptr_array_index (parts, j) = GINT_TO_POINTER (-1);
            placed = TRUE;
        }
    }
    // a partition whose table is not there, in another schema of the system, with the tables
    for (j = 0; j < parts->len; j++)
    {
        const int row = GPOINTER_TO_INT (g_ptr_array_index (parts, j));
        query_node_t *ns, *r;

        if (row < 0)
            continue;
        ns = g_hash_table_lookup (by_name, pq->PQgetvalue (rels, row, 0));
        if (ns == NULL)
            continue;
        r = query_node_new (
            &s->tree,
            query_node_group (&s->tree, ns, _ (query_group_names[QUERY_GROUP_TABLES]), TRUE),
            QUERY_NODE_OBJECT, g_strdup (pq->PQgetvalue (rels, row, 2)), NULL, NULL);
        g_hash_table_insert (by_oid, GUINT_TO_POINTER (query_schema_oid (pq, rels, row, 1)), r);
    }
    // what each relation is, now that its node is where it stays
    for (i = 0; i < rows; i++)
    {
        const Oid oid = query_schema_oid (pq, rels, i, 1);
        query_node_t *r = pq->PQgetisnull (rels, i, 1)
            ? NULL
            : g_hash_table_lookup (by_oid, GUINT_TO_POINTER (oid));
        const char *nsp = pq->PQgetvalue (rels, i, 0);
        query_item_t *item;
        char *qn, *qr;
        gint64 reltuples;

        if (r == NULL)
            continue;
        item = QUERY_ITEM (r);
        item->id = oid;
        item->tag = pq->PQgetvalue (rels, i, 3)[0];
        // a sequence has nothing under it
        mctree_node_set_lazy (r, item->tag != 'S');
        reltuples = g_ascii_strtoll (pq->PQgetvalue (rels, i, 4), NULL, 10);
        // the rows the server reckons, those of a table that was never analyzed none
        if (reltuples >= 0 && (item->tag == 'r' || item->tag == 'm'))
            r->value = query_tree_about (reltuples);
        else if (item->tag == 'p')
            r->value = g_strdup (_ ("[partitioned]"));
        qn = query_pg_ident (nsp);
        qr = query_pg_ident (r->key);
        item->insert = g_strconcat (qn, ".", qr, (char *) NULL);
        item->rows = g_strdup_printf ("SELECT * FROM %s LIMIT %u;", item->insert, QUERY_TABLE_ROWS);
        g_free (qn);
        g_free (qr);
        // a relation unfolded before is read again
        r->expanded = query_tree_was_open (&s->tree, r, FALSE);
        if (r->expanded && r->lazy)
        {
            item->asked = TRUE;
            g_array_append_val (s->want_details, oid);
        }
    }

    // the functions, in the group of their schema
    rows = funcs != NULL ? pq->PQntuples (funcs) : 0;
    for (i = 0; i < rows; i++)
    {
        const char *nsp = pq->PQgetvalue (funcs, i, 0);
        const char *name = pq->PQgetvalue (funcs, i, 1);
        const char kind = pq->PQgetvalue (funcs, i, 4)[0];
        query_node_t *ns = g_hash_table_lookup (by_name, nsp);
        query_node_t *f;
        char *qn, *qf;

        if (ns == NULL)
            continue;
        qn = query_pg_ident (nsp);
        qf = query_pg_ident (name);
        f = query_node_new (
            &s->tree,
            query_node_group (&s->tree, ns, _ (query_group_names[QUERY_GROUP_FUNCTIONS]), FALSE),
            QUERY_NODE_ITEM, g_strdup_printf ("%s(%s)", name, pq->PQgetvalue (funcs, i, 2)),
            kind == 'p'       ? g_strdup (_ ("procedure"))
                : kind == 'a' ? g_strdup (_ ("aggregate"))
                : kind == 'w' ? g_strdup_printf ("-> %s, window", pq->PQgetvalue (funcs, i, 3))
                              : g_strdup_printf ("-> %s", pq->PQgetvalue (funcs, i, 3)),
            g_strconcat (qn, ".", qf, "(", (char *) NULL));
        // F4 reads its DDL by it
        QUERY_ITEM (f)->id = query_schema_oid (pq, funcs, i, 5);
        QUERY_ITEM (f)->tag = 'F';
        g_free (qn);
        g_free (qf);
    }

    // the groups of a schema in their order; the nodes are the model's, the arrays their order
    roots = query_tree_roots (&s->tree);
    for (j = 0; roots != NULL && j < roots->len; j++)
    {
        query_node_t *ns = g_ptr_array_index (roots, j);
        GPtrArray *sorted;
        int g;

        if (ns->children == NULL)
            continue;
        sorted = g_ptr_array_new ();
        for (g = 0; g < QUERY_GROUP_COUNT; g++)
        {
            guint k;

            for (k = 0; k < ns->children->len; k++)
                if (strcmp (((query_node_t *) g_ptr_array_index (ns->children, k))->key,
                            _ (query_group_names[g]))
                    == 0)
                    g_ptr_array_add (sorted, g_ptr_array_steal_index (ns->children, k--));
        }
        g_ptr_array_free (ns->children, TRUE);
        ns->children = sorted;
    }

    // that of MC_PG_SCHEMA first; the only one open
    for (j = 0; s->first != NULL && roots != NULL && j < roots->len; j++)
        if (strcmp (((query_node_t *) g_ptr_array_index (roots, j))->key, s->first) == 0)
            first = j;
    if (first > 0)
    {
        gpointer f = g_ptr_array_steal_index (roots, first);

        g_ptr_array_insert (roots, 0, f);
    }
    if (roots != NULL && roots->len == 1 && !again)
        ((query_node_t *) g_ptr_array_index (roots, 0))->expanded = TRUE;

    g_ptr_array_free (parts, TRUE);
    g_hash_table_destroy (by_name);
    g_hash_table_destroy (by_oid);
    s->tree.read = TRUE;
    g_clear_pointer (&s->tree.error, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* The line of a column of row @k of @res, after its name: its type, not null, identity,
   generated, its default */
static char *
query_schema_column_text (const query_pq_t *pq, PGresult *res, int k)
{
    GString *text = g_string_new (pq->PQgetvalue (res, k, 1));
    const char identity = pq->PQgetvalue (res, k, 3)[0];

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
                (int) (g_utf8_offset_to_pointer (def, QUERY_SCHEMA_DEFAULT) - def), def);
        else
            g_string_append_printf (text, " = %s", def);
    }
    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* An index as it is shown: "unique btree (id)" of "CREATE UNIQUE INDEX ... USING btree (id)" */
static char *
query_schema_index_text (const char *def, gboolean primary, gboolean unique)
{
    const char *using = strstr (def, " USING ");

    return g_strdup_printf ("%s%s",
                            primary      ? "primary "
                                : unique ? "unique "
                                         : "",
                            using != NULL ? using + 7 : def);
}

/* --------------------------------------------------------------------------------------------- */

/* A trigger as it is shown: "BEFORE INSERT FOR EACH ROW EXECUTE FUNCTION f()" of its definition,
   without what it starts with and the table it is on */
static char *
query_schema_trigger_text (const char *def, const char *name)
{
    const char *p = strstr (def, " TRIGGER "), *on, *rest;
    GString *text;

    p = p != NULL ? p + 9 : def;
    // the name, as it is written, quoted or not
    if (*p == '"')
        for (p++; *p != '\0' && !(p[0] == '"' && p[1] != '"'); p += p[0] == '"' ? 2 : 1)
            ;
    else
        p += strlen (name) - 1;
    if (*p != '\0')
        p++;
    while (*p == ' ')
        p++;
    text = g_string_new (p);
    on = strstr (text->str, " ON ");
    rest = on != NULL ? strstr (on, " FOR EACH ") : NULL;
    if (rest == NULL && on != NULL)
        rest = strstr (on, " EXECUTE ");
    if (on != NULL && rest != NULL)
        g_string_erase (text, on - text->str, rest - on);
    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @n is the relation of the Oid at @data */
static gboolean
query_schema_is_oid (const query_node_t *n, const void *data)
{
    return QUERY_ITEM (n)->id == *(const Oid *) data;
}

/* --------------------------------------------------------------------------------------------- */

/* The sections of the relation of @oid, wherever it is now, of the @results of
   pg-details.sql; none of them on an error */
static void
query_schema_take_details (query_pg_schema_t *s, Oid oid, GPtrArray *results)
{
    const query_pq_t *pq = s->pq;
    query_node_t *r;
    query_node_t *sections[QUERY_SECTION_COUNT];
    GString *all;
    gboolean table, ok;
    int k, i;

    r = query_tree_find (&s->tree, query_schema_is_oid, &oid);
    // read already: the list read twice asked for it twice
    if (r == NULL || QUERY_ITEM (r)->read)
        return;
    table = QUERY_ITEM (r)->tag == 'r' || QUERY_ITEM (r)->tag == 'p';
    ok = results != NULL && results->len == QUERY_SECTION_COUNT - 1;
    // a table shows all its sections, a view and the like its columns and those with something
    for (i = 0; i < QUERY_SECTION_COUNT; i++)
    {
        PGresult *res = !ok              ? NULL
            : i == QUERY_SECTION_COLUMNS ? g_ptr_array_index (results, 0)
            : i == QUERY_SECTION_KEYS || i == QUERY_SECTION_FOREIGN
            ? g_ptr_array_index (results, 1)
            : g_ptr_array_index (results, i - 1);
        gboolean some = FALSE;

        for (k = 0; res != NULL && !some && k < pq->PQntuples (res); k++)
            some = (i != QUERY_SECTION_KEYS && i != QUERY_SECTION_FOREIGN)
                || (pq->PQgetvalue (res, k, 1)[0] == 'f') == (i == QUERY_SECTION_FOREIGN);
        sections[i] = table || some || i == QUERY_SECTION_COLUMNS
            ? query_node_new (&s->tree, r, QUERY_NODE_SECTION,
                              g_strdup (_ (query_section_names[i])), NULL, NULL)
            : NULL;
    }
    if (ok)
    {
        PGresult *res = g_ptr_array_index (results, 0);

        // the columns; Insert on their section puts them all
        all = g_string_new (NULL);
        for (k = 0; k < pq->PQntuples (res); k++)
        {
            char *name = query_pg_ident (pq->PQgetvalue (res, k, 0));

            (void) query_node_new (&s->tree, sections[QUERY_SECTION_COLUMNS], QUERY_NODE_ITEM,
                                   g_strdup (pq->PQgetvalue (res, k, 0)),
                                   query_schema_column_text (pq, res, k), name);
            g_string_append_printf (all, "%s%s", all->len > 0 ? ", " : "", name);
        }
        QUERY_ITEM (sections[QUERY_SECTION_COLUMNS])->insert = g_string_free (all, FALSE);

        // the keys and the foreign keys; Insert puts their columns
        res = g_ptr_array_index (results, 1);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (
                &s->tree,
                sections[pq->PQgetvalue (res, k, 1)[0] == 'f' ? QUERY_SECTION_FOREIGN
                                                              : QUERY_SECTION_KEYS],
                QUERY_NODE_ITEM, g_strdup (pq->PQgetvalue (res, k, 0)),
                g_strdup (pq->PQgetvalue (res, k, 2)), g_strdup (pq->PQgetvalue (res, k, 3)));

        res = g_ptr_array_index (results, 2);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (&s->tree, sections[QUERY_SECTION_INDEXES], QUERY_NODE_ITEM,
                                   g_strdup (pq->PQgetvalue (res, k, 0)),
                                   query_schema_index_text (pq->PQgetvalue (res, k, 1),
                                                            pq->PQgetvalue (res, k, 2)[0] == 't',
                                                            pq->PQgetvalue (res, k, 3)[0] == 't'),
                                   query_pg_ident (pq->PQgetvalue (res, k, 0)));

        // the foreign keys of the others: the table, its columns; Insert puts the table
        res = g_ptr_array_index (results, 3);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (
                &s->tree, sections[QUERY_SECTION_REFERENCES], QUERY_NODE_ITEM,
                g_strdup_printf ("%s (%s)", pq->PQgetvalue (res, k, 1), pq->PQgetvalue (res, k, 2)),
                g_strdup (pq->PQgetvalue (res, k, 0)), g_strdup (pq->PQgetvalue (res, k, 1)));

        res = g_ptr_array_index (results, 4);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (
                &s->tree, sections[QUERY_SECTION_TRIGGERS], QUERY_NODE_ITEM,
                g_strdup (pq->PQgetvalue (res, k, 0)),
                query_schema_trigger_text (pq->PQgetvalue (res, k, 1), pq->PQgetvalue (res, k, 0)),
                query_pg_ident (pq->PQgetvalue (res, k, 0)));
    }
    // the columns open the first time
    for (i = 0; i < QUERY_SECTION_COUNT; i++)
        if (sections[i] != NULL)
            sections[i]->expanded =
                query_tree_was_open (&s->tree, sections[i], i == QUERY_SECTION_COLUMNS);
    query_node_read (r);
    query_tree_read (&s->tree, r, results != NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* What AI is told of the database, of the rows of pg-ai.sql in @res, NULL for none */
static char *
query_schema_ai_text (query_pg_schema_t *s, PGresult *res)
{
    const query_pq_t *pq = s->pq;
    const int version = pq->PQserverVersion (s->link.conn);
    const int rows = res != NULL ? pq->PQntuples (res) : 0;
    GString *text = g_string_new (NULL);
    int told = 0, pass, i;

    g_string_append_printf (text, "The database \"%s\" of PostgreSQL %d.%d.\n",
                            pq->PQdb (s->link.conn), version / 10000, version % 10000);
    if (s->first != NULL)
        g_string_append_printf (text,
                                "The schema of the user is %s; write the names with their "
                                "schema.\n",
                                s->first);
    if (s->read_only)
        g_string_append (text, "The connection is read-only: write SELECT, no change.\n");
    g_string_append (text,
                     "Its relations, a line each: schema.name (kind): the columns; the keys.\n");
    // those of the schema of the user first
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < rows; i++)
        {
            const char *nsp = pq->PQgetvalue (res, i, 0);
            const gboolean of_first = s->first != NULL && strcmp (nsp, s->first) == 0;
            const char kind = pq->PQgetvalue (res, i, 2)[0];
            char *n, *r;

            if ((pass == 0) != of_first && s->first != NULL)
                continue;
            if (pass == 1 && s->first == NULL)
                break;
            if (told++ >= QUERY_SCHEMA_AI_MAX)
                continue;
            n = query_pg_ident (nsp);
            r = query_pg_ident (pq->PQgetvalue (res, i, 1));
            g_string_append_printf (text, "%s.%s (%s): %s", n, r,
                                    kind == 'p'       ? "partitioned table"
                                        : kind == 'v' ? "view"
                                        : kind == 'm' ? "materialized view"
                                        : kind == 'f' ? "foreign table"
                                                      : "table",
                                    pq->PQgetvalue (res, i, 3));
            if (!pq->PQgetisnull (res, i, 4))
                g_string_append_printf (text, "; %s", pq->PQgetvalue (res, i, 4));
            g_string_append_c (text, '\n');
            g_free (n);
            g_free (r);
        }
    if (told == 0)
        g_string_append (text, "None: the database has no relation yet.\n");
    else if (told > QUERY_SCHEMA_AI_MAX)
        g_string_append_printf (text, "And %d relations more, not told.\n",
                                told - QUERY_SCHEMA_AI_MAX);
    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The result @i of the query of now, NULL for none */
static PGresult *
query_schema_result (const query_pg_schema_t *s, guint i)
{
    return i < s->results->len ? g_ptr_array_index (s->results, i) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The query of now has ended: what it read taken, the window made again, the next one sent */
static void
query_schema_done (query_pg_schema_t *s)
{
    query_pq_link_unwatch (&s->link);
    s->state = QUERY_SCHEMA_READY;
    if (s->asked == QUERY_SCHEMA_ASK_ROLLBACK)
    {
        // not rolled back: the connection goes, the next query makes another
        if (s->res_error != NULL)
        {
            query_schema_close_conn (s);
            s->state = QUERY_SCHEMA_OFF;
        }
        g_ptr_array_set_size (s->results, 0);
        g_clear_pointer (&s->res_error, g_free);
        query_schema_next (s);
        return;
    }
    /* a script that failed before its COMMIT, the DDL of a timeout: the transaction is left
       open and failed, every query after it would fail too */
    if (s->link.conn != NULL && s->pq->PQtransactionStatus (s->link.conn) != QUERY_PG_TRANS_IDLE)
        s->want_rollback = TRUE;
    if (s->asked == QUERY_SCHEMA_ASK_AI)
    {
        char *text =
            s->res_error == NULL ? query_schema_ai_text (s, query_schema_result (s, 0)) : NULL;

        s->ai_sent = FALSE;
        query_ai_context (s->q, text, s->res_error);
        g_free (text);
    }
    else if (s->asked == QUERY_SCHEMA_ASK_DDL)
    {
        char *ddl = NULL;

        if (s->res_error == NULL && s->asked_ddl_function)
            ddl = query_ddl_function (s->pq, query_schema_result (s, 0));
        else if (s->res_error == NULL && s->results->len == 7)
            ddl = query_ddl_relation (s->pq, s->asked_ddl_name, s->results);
        query_tree_ddl_put (&s->tree, s->asked_ddl_id, ddl,
                            s->res_error != NULL ? g_strchomp (s->res_error)
                                                 : _ ("It is no more: F4 has no DDL of it."));
        g_free (ddl);
        g_clear_pointer (&s->asked_ddl_name, g_free);
    }
    else if (s->asked == QUERY_SCHEMA_ASK_LIST)
    {
        if (s->res_error != NULL)
        {
            g_free (s->tree.error);
            s->tree.error = g_strchomp (g_strdup (s->res_error));
        }
        else
            query_schema_take_list (s, query_schema_result (s, 0), query_schema_result (s, 1));
    }
    else
        // an error is the sections of none: the relation shows nothing in them
        query_schema_take_details (s, s->asked_oid, s->res_error == NULL ? s->results : NULL);
    g_ptr_array_set_size (s->results, 0);
    g_clear_pointer (&s->res_error, g_free);
    query_tree_render (&s->tree);
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_schema_read_input (query_pg_schema_t *s)
{
    const query_pq_t *pq = s->pq;
    PGresult *res;

    if (s->link.flushing)
    {
        query_schema_flush (s);
        if (s->state != QUERY_SCHEMA_BUSY)
            return;
    }
    if (pq->PQconsumeInput (s->link.conn) == 0)
    {
        query_schema_broken (s, pq->PQerrorMessage (s->link.conn));
        return;
    }
    while (pq->PQisBusy (s->link.conn) == 0)
    {
        res = pq->PQgetResult (s->link.conn);
        if (res == NULL)
        {
            query_schema_done (s);
            return;
        }
        // the rows of each statement, in their order
        if (pq->PQresultStatus (res) == QUERY_PG_TUPLES_OK)
            g_ptr_array_add (s->results, res);
        else
        {
            if (pq->PQresultStatus (res) != QUERY_PG_COMMAND_OK && s->res_error == NULL)
                s->res_error = g_strdup (pq->PQresultErrorMessage (res));
            pq->PQclear (res);
        }
    }
    if (pq->PQstatus (s->link.conn) == QUERY_PG_CONNECTION_BAD)
        query_schema_broken (s, pq->PQerrorMessage (s->link.conn));
}

/* --------------------------------------------------------------------------------------------- */

/* The script @name, with its value {{oid}} @oid when it is not 0, sent as @asked; no script,
   what it asks for ends with the error of it, as one of the server would */
static void
query_schema_send_script (query_pg_schema_t *s, const char *name, Oid oid, query_schema_ask_t asked)
{
    char *why = NULL;
    char *sql = query_script (name, &why);

    if (sql == NULL)
    {
        s->asked = asked;
        g_free (s->res_error);
        s->res_error = why;
        query_schema_done (s);
        return;
    }
    if (oid != 0)
    {
        char *number = g_strdup_printf ("%u", oid);
        char *filled = query_script_fill (sql, "oid", number, NULL);

        g_free (sql);
        g_free (number);
        sql = filled;
    }
    query_schema_send (s, sql, asked);
    g_free (sql);
}

/* --------------------------------------------------------------------------------------------- */

/* What is asked for sent, the list first, what AI is told, the sections of the relations; the
   connection made first */
static void
query_schema_next (query_pg_schema_t *s)
{
    const gboolean wanted = s->want_rollback || s->want_list || s->want_ddl != 0 || s->want_ai
        || s->want_details->len > 0;

    if (!wanted)
        return;
    if (s->state == QUERY_SCHEMA_OFF || s->state == QUERY_SCHEMA_BROKEN)
    {
        query_schema_connect (s);
        return;
    }
    if (s->state != QUERY_SCHEMA_READY)
        return;
    if (s->want_rollback)
    {
        s->want_rollback = FALSE;
        query_schema_send (s, "ROLLBACK", QUERY_SCHEMA_ASK_ROLLBACK);
    }
    else if (s->want_list)
    {
        s->want_list = FALSE;
        query_schema_send_script (s, "pg-list.sql", 0, QUERY_SCHEMA_ASK_LIST);
    }
    else if (s->want_ddl != 0)
    {
        const Oid oid = s->want_ddl;

        s->want_ddl = 0;
        s->asked_ddl_function = s->want_ddl_function;
        s->asked_ddl_id = s->want_ddl_id;
        g_free (s->asked_ddl_name);
        s->asked_ddl_name = s->want_ddl_name;
        s->want_ddl_name = NULL;
        query_schema_send_script (
            s, s->asked_ddl_function ? "pg-ddl-function.sql" : "pg-ddl-relation.sql", oid,
            QUERY_SCHEMA_ASK_DDL);
    }
    else if (s->want_ai)
    {
        s->want_ai = FALSE;
        s->ai_sent = TRUE;
        query_schema_send_script (s, "pg-ai.sql", 0, QUERY_SCHEMA_ASK_AI);
    }
    else
    {
        const Oid oid = g_array_index (s->want_details, Oid, 0);

        s->asked_oid = oid;
        g_array_remove_index (s->want_details, 0);
        query_schema_send_script (s, "pg-details.sql", oid, QUERY_SCHEMA_ASK_DETAILS);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* What the keys do */
/* --------------------------------------------------------------------------------------------- */

/* The list read again, the window made when it is not there yet */
static void
query_schema_refresh (query_pg_schema_t *s)
{
    if (s->state == QUERY_SCHEMA_BROKEN)
        g_clear_pointer (&s->tree.error, g_free);
    s->want_list = TRUE;
    // the sections asked for come with the list
    g_array_set_size (s->want_details, 0);
    query_tree_cancel (&s->tree);
    if (!s->tree.shown || s->tree.error == NULL)
    {
        query_tree_render (&s->tree);
    }
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* The query of F3 of the relation @o: its @fields, the names of its columns as SQL takes them,
   one a line, all of them when there is none */
static char *
query_schema_query (const query_node_t *o, GPtrArray *fields)
{
    GString *text = g_string_new ("SELECT ");
    guint i;

    if (fields->len == 0)
        g_string_append_c (text, '*');
    for (i = 0; i < fields->len; i++)
        g_string_append_printf (text, "%s%s", i > 0 ? ",\n       " : "",
                                (const char *) g_ptr_array_index (fields, i));
    g_string_append_printf (text, "\nFROM %s\nLIMIT %u;", QUERY_ITEM (o)->insert, QUERY_TABLE_ROWS);
    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* F4 on @n: the drop, commented, and the create of its schema, its function or its relation, read
   from the server, put in the file of queries */
static void
query_schema_ddl (query_pg_schema_t *s, const query_node_t *n)
{
    const query_item_t *item = n != NULL ? QUERY_ITEM (n) : NULL;
    const query_node_t *o;
    guint id;

    if (item == NULL)
        return;
    id = query_tree_ddl_ask (&s->tree);
    if (item->kind == QUERY_NODE_ROOT)
    {
        GString *ddl = g_string_new (NULL);
        char *drop = g_strdup_printf ("DROP SCHEMA %s;\n", item->insert);

        query_ddl_append_comment (ddl, drop);
        g_string_append_printf (ddl, "CREATE SCHEMA %s;\n", item->insert);
        query_tree_ddl_put (&s->tree, id, ddl->str, NULL);
        g_string_free (ddl, TRUE);
        g_free (drop);
        return;
    }
    s->want_ddl_id = id;
    if (item->tag == 'F')
    {
        s->want_ddl = (Oid) item->id;
        s->want_ddl_function = TRUE;
    }
    else
    {
        o = query_node_object ((query_node_t *) n);
        if (o == NULL || QUERY_ITEM (o)->id == 0)
        {
            query_tree_ddl_put (&s->tree, id, NULL,
                                _ ("F4 puts the DDL of a schema, a relation or a function."));
            return;
        }
        s->want_ddl = (Oid) QUERY_ITEM (o)->id;
        s->want_ddl_function = FALSE;
        g_free (s->want_ddl_name);
        s->want_ddl_name = g_strdup (QUERY_ITEM (o)->insert);
    }
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* An object opened, or F3 on one: its sections asked for, the tree query_tree_t.details */
static void
query_schema_details (query_node_t *o, void *data)
{
    query_pg_schema_t *s = (query_pg_schema_t *) data;
    const Oid oid = (Oid) QUERY_ITEM (o)->id;

    g_array_append_val (s->want_details, oid);
    query_schema_next (s);
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
    query_pq_link_init (&s->link, pq, query_schema_connected, query_schema_ready, s);
    s->results = g_ptr_array_new_with_free_func ((GDestroyNotify) pq->PQclear);
    s->want_details = g_array_new (FALSE, FALSE, sizeof (Oid));
    query_tree_init (&s->tree, q, query_schema_details, s);
    s->tree.query = query_schema_query;
    return s;
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_free (query_pg_schema_t *s)
{
    if (s == NULL)
        return;
    query_schema_close_conn (s);
    g_ptr_array_free (s->results, TRUE);
    g_array_free (s->want_details, TRUE);
    g_free (s->want_ddl_name);
    g_free (s->asked_ddl_name);
    query_tree_done (&s->tree);
    g_free (s->first);
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_pg_schema_do (query_pg_schema_t *s, query_schema_action_t action, gboolean key)
{
    // the node of the cursor; a key acts in the window alone, when it has the focus, with no node
    // too: the key is not that of the file then
    query_node_t *n = s->tree.window != NULL ? edit_tree_current (s->tree.window) : NULL;
    query_node_t *o;

    if (key && !query_tree_has_focus (&s->tree))
        return FALSE;
    switch (action)
    {
    case QUERY_SCHEMA_TOGGLE:
        if (query_tree_toggle (&s->tree))
            break;
        MC_FALLTHROUGH;
    case QUERY_SCHEMA_SHOW:
        s->tree.make = TRUE;
        if (!s->tree.read && s->state != QUERY_SCHEMA_BUSY)
            query_schema_refresh (s);
        else
            query_tree_render (&s->tree);
        // QuerySchema gives it the focus, the start does not
        if (action == QUERY_SCHEMA_TOGGLE && s->tree.window != NULL)
            s->q->host->window_show (s->q->host, s->tree.window);
        break;
    case QUERY_SCHEMA_REFRESH:
        query_schema_refresh (s);
        break;
    case QUERY_SCHEMA_ROWS:
        query_tree_rows (&s->tree, n);
        break;
    case QUERY_SCHEMA_INSERT:
        query_tree_insert (&s->tree, n);
        break;
    case QUERY_SCHEMA_QUERY:
        o = query_tree_query (&s->tree, n);
        if (o != NULL)
            query_schema_details (o, s);
        break;
    case QUERY_SCHEMA_DDL:
        query_schema_ddl (s, n);
        break;
    default:
        break;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_ai (query_pg_schema_t *s)
{
    if (s->want_ai || s->ai_sent)
        return;
    s->want_ai = TRUE;
    query_schema_next (s);
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_changed (query_pg_schema_t *s)
{
    if (s->tree.read)
        query_schema_refresh (s);
}

/* --------------------------------------------------------------------------------------------- */
