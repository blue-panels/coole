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

// what is on the system left out
#define QUERY_SCHEMA_USER_NSP "n.nspname !~ '^pg_' AND n.nspname <> 'information_schema'"

/* The relations of the schemas, a schema with none having one row with no relation, a partition
   with its table; the functions, those of extensions left out */
#define QUERY_SCHEMA_LIST                                                                          \
    "SELECT n.nspname, c.oid, c.relname, c.relkind, c.reltuples::bigint, c.relispartition, "       \
    "(SELECT i.inhparent FROM pg_catalog.pg_inherits i WHERE i.inhrelid = c.oid LIMIT 1) "         \
    "FROM pg_catalog.pg_namespace n LEFT JOIN pg_catalog.pg_class c "                              \
    "ON c.relnamespace = n.oid AND c.relkind IN ('r', 'p', 'v', 'm', 'f', 'S') "                   \
    "WHERE " QUERY_SCHEMA_USER_NSP " ORDER BY n.nspname, c.relname; "                              \
    "SELECT n.nspname, p.proname, pg_catalog.pg_get_function_identity_arguments (p.oid), "         \
    "pg_catalog.pg_get_function_result (p.oid), p.prokind "                                        \
    "FROM pg_catalog.pg_proc p JOIN pg_catalog.pg_namespace n ON n.oid = p.pronamespace "          \
    "WHERE " QUERY_SCHEMA_USER_NSP " AND NOT EXISTS (SELECT 1 FROM pg_catalog.pg_depend d "        \
    "WHERE d.classid = 'pg_catalog.pg_proc'::pg_catalog.regclass AND d.objid = p.oid "             \
    "AND d.deptype = 'e') ORDER BY 1, 2, 3"

// the columns of a constraint, as SQL takes them, in its order
#define QUERY_SCHEMA_KEY_COLUMNS                                                                   \
    "(SELECT string_agg (pg_catalog.quote_ident (a.attname), ', ' "                                \
    "ORDER BY pg_catalog.array_position (o.conkey, a.attnum)) FROM pg_catalog.pg_attribute a "     \
    "WHERE a.attrelid = o.conrelid AND a.attnum = ANY (o.conkey))"

/* What a relation unfolded shows, of its oid five times: its columns, of PostgreSQL 12 and later
   (attgenerated); its keys; its indexes; the foreign keys of the others to it; its triggers */
#define QUERY_SCHEMA_DETAILS                                                                       \
    "SELECT a.attname, pg_catalog.format_type (a.atttypid, a.atttypmod), a.attnotnull, "           \
    "a.attidentity, a.attgenerated, pg_catalog.pg_get_expr (d.adbin, d.adrelid) "                  \
    "FROM pg_catalog.pg_attribute a LEFT JOIN pg_catalog.pg_attrdef d "                            \
    "ON d.adrelid = a.attrelid AND d.adnum = a.attnum "                                            \
    "WHERE a.attrelid = %u AND a.attnum > 0 AND NOT a.attisdropped ORDER BY a.attnum; "            \
    "SELECT o.conname, o.contype, pg_catalog.pg_get_constraintdef "                                \
    "(o.oid), " QUERY_SCHEMA_KEY_COLUMNS " FROM pg_catalog.pg_constraint o "                       \
    "WHERE o.conrelid = %u AND o.contype IN ('p', 'u', 'f') "                                      \
    "ORDER BY o.contype <> 'p', o.conname; "                                                       \
    "SELECT c.relname, pg_catalog.pg_get_indexdef (i.indexrelid), i.indisprimary, "                \
    "i.indisunique FROM pg_catalog.pg_index i "                                                    \
    "JOIN pg_catalog.pg_class c ON c.oid = i.indexrelid WHERE i.indrelid = %u ORDER BY 1; "        \
    "SELECT o.conname, o.conrelid::pg_catalog.regclass::text, " QUERY_SCHEMA_KEY_COLUMNS " "       \
    "FROM pg_catalog.pg_constraint o WHERE o.confrelid = %u AND o.contype = 'f' "                  \
    "ORDER BY 2, 1; "                                                                              \
    "SELECT t.tgname, pg_catalog.pg_get_triggerdef (t.oid) FROM pg_catalog.pg_trigger t "          \
    "WHERE t.tgrelid = %u AND NOT t.tgisinternal ORDER BY 1"

/* What AI is told of a relation, a row each, partitions left out: its columns and keys, the names
   as SQL takes them; of PostgreSQL 12 and later as the columns */
#define QUERY_SCHEMA_AI                                                                            \
    "SELECT n.nspname, c.relname, c.relkind, "                                                     \
    "(SELECT string_agg (pg_catalog.quote_ident (a.attname) || ' ' || "                            \
    "pg_catalog.format_type (a.atttypid, a.atttypmod) || "                                         \
    "CASE WHEN a.attnotnull THEN ' not null' ELSE '' END, ', ' ORDER BY a.attnum) "                \
    "FROM pg_catalog.pg_attribute a "                                                              \
    "WHERE a.attrelid = c.oid AND a.attnum > 0 AND NOT a.attisdropped), "                          \
    "(SELECT string_agg (pg_catalog.pg_get_constraintdef (o.oid), ', ' "                           \
    "ORDER BY o.contype <> 'p', o.conname) FROM pg_catalog.pg_constraint o "                       \
    "WHERE o.conrelid = c.oid AND o.contype IN ('p', 'u', 'f')) "                                  \
    "FROM pg_catalog.pg_namespace n JOIN pg_catalog.pg_class c ON c.relnamespace = n.oid "         \
    "WHERE c.relkind IN ('r', 'p', 'v', 'm', 'f') AND NOT c.relispartition "                       \
    "AND " QUERY_SCHEMA_USER_NSP " ORDER BY n.nspname, c.relname LIMIT 5000"

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
    QUERY_SCHEMA_ASK_AI
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
   QUERY_SCHEMA_DETAILS, the keys and the foreign keys being of one */
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
    gboolean want_ai;      // what AI is told, to be sent
    gboolean ai_sent;      // and sent
    GArray *want_details;  // Oid of the relations whose sections are to be read

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
    g_array_set_size (s->want_details, 0);
    query_tree_unask (&s->tree);
    g_free (s->tree.error);
    s->tree.error = g_strchomp (error);
    query_tree_mark (&s->tree);
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

/* The schemas, their groups, the relations and the functions of the results of QUERY_SCHEMA_LIST:
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

        if (n == NULL || strcmp (n->name, nsp) != 0)
        {
            n = query_node_new (NULL, QUERY_NODE_ROOT, g_strdup (nsp), NULL, query_pg_ident (nsp));
            g_ptr_array_add (s->tree.roots, n);
            g_hash_table_insert (by_name, n->name, n);
            // the first time, that of MC_PG_SCHEMA open, else public
            n->open = query_tree_was_open (
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
        {
            groups[g] = query_node_new (n, QUERY_NODE_GROUP, g_strdup (_ (query_group_names[g])),
                                        NULL, NULL);
            groups[g]->open = query_tree_was_open (&s->tree, groups[g], g == QUERY_GROUP_TABLES);
        }
        r = query_node_new (groups[g], QUERY_NODE_OBJECT, g_strdup (pq->PQgetvalue (rels, i, 2)),
                            NULL, NULL);
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
            if (table->parts == NULL)
            {
                table->parts = query_node_new (table, QUERY_NODE_SECTION,
                                               g_strdup (_ ("Partitions")), NULL, NULL);
                table->parts->open = query_tree_was_open (&s->tree, table->parts, FALSE);
            }
            r = query_node_new (table->parts, QUERY_NODE_OBJECT,
                                g_strdup (pq->PQgetvalue (rels, row, 2)), NULL, NULL);
            g_hash_table_insert (by_oid, GUINT_TO_POINTER (query_schema_oid (pq, rels, row, 1)), r);
            g_ptr_array_index (parts, j) = GINT_TO_POINTER (-1);
            placed = TRUE;
        }
    }
    // a partition whose table is not there, in another schema of the system, with the tables
    for (j = 0; j < parts->len; j++)
    {
        const int row = GPOINTER_TO_INT (g_ptr_array_index (parts, j));
        query_node_t *ns, *tables = NULL, *r;
        guint k;

        if (row < 0)
            continue;
        ns = g_hash_table_lookup (by_name, pq->PQgetvalue (rels, row, 0));
        if (ns == NULL)
            continue;
        for (k = 0; k < ns->children->len; k++)
            if (strcmp (((query_node_t *) g_ptr_array_index (ns->children, k))->name,
                        _ (query_group_names[QUERY_GROUP_TABLES]))
                == 0)
                tables = g_ptr_array_index (ns->children, k);
        if (tables == NULL)
        {
            tables =
                query_node_new (ns, QUERY_NODE_GROUP,
                                g_strdup (_ (query_group_names[QUERY_GROUP_TABLES])), NULL, NULL);
            tables->open = query_tree_was_open (&s->tree, tables, TRUE);
        }
        r = query_node_new (tables, QUERY_NODE_OBJECT, g_strdup (pq->PQgetvalue (rels, row, 2)),
                            NULL, NULL);
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
        char *qn, *qr;
        gint64 reltuples;

        if (r == NULL)
            continue;
        r->id = oid;
        r->tag = pq->PQgetvalue (rels, i, 3)[0];
        r->leaf = r->tag == 'S';
        reltuples = g_ascii_strtoll (pq->PQgetvalue (rels, i, 4), NULL, 10);
        // the rows the server reckons, those of a table that was never analyzed none
        if (reltuples >= 0 && (r->tag == 'r' || r->tag == 'm'))
            r->text = g_strdup_printf ("~%" G_GINT64_FORMAT, reltuples);
        else if (r->tag == 'p')
            r->text = g_strdup (_ ("[partitioned]"));
        qn = query_pg_ident (nsp);
        qr = query_pg_ident (r->name);
        r->insert = g_strconcat (qn, ".", qr, (char *) NULL);
        r->rows = g_strdup_printf ("SELECT * FROM %s LIMIT %u;", r->insert, QUERY_TABLE_ROWS);
        g_free (qn);
        g_free (qr);
        // a relation unfolded before is read again
        r->open = query_tree_was_open (&s->tree, r, FALSE);
        if (r->open && r->tag != 'S')
        {
            r->asked = TRUE;
            g_array_append_val (s->want_details, r->id);
        }
    }

    // the functions, in the group of their schema
    rows = funcs != NULL ? pq->PQntuples (funcs) : 0;
    for (i = 0; i < rows; i++)
    {
        const char *nsp = pq->PQgetvalue (funcs, i, 0);
        const char *name = pq->PQgetvalue (funcs, i, 1);
        const char kind = pq->PQgetvalue (funcs, i, 4)[0];
        query_node_t *ns = g_hash_table_lookup (by_name, nsp), *group = NULL;
        char *qn, *qf;

        if (ns == NULL)
            continue;
        for (j = 0; j < ns->children->len; j++)
            if (strcmp (((query_node_t *) g_ptr_array_index (ns->children, j))->name,
                        _ (query_group_names[QUERY_GROUP_FUNCTIONS]))
                == 0)
                group = g_ptr_array_index (ns->children, j);
        if (group == NULL)
        {
            group = query_node_new (ns, QUERY_NODE_GROUP,
                                    g_strdup (_ (query_group_names[QUERY_GROUP_FUNCTIONS])), NULL,
                                    NULL);
            group->open = query_tree_was_open (&s->tree, group, FALSE);
        }
        qn = query_pg_ident (nsp);
        qf = query_pg_ident (name);
        (void) query_node_new (
            group, QUERY_NODE_ITEM, g_strdup_printf ("%s(%s)", name, pq->PQgetvalue (funcs, i, 2)),
            kind == 'p'       ? g_strdup (_ ("procedure"))
                : kind == 'a' ? g_strdup (_ ("aggregate"))
                : kind == 'w' ? g_strdup_printf ("-> %s, window", pq->PQgetvalue (funcs, i, 3))
                              : g_strdup_printf ("-> %s", pq->PQgetvalue (funcs, i, 3)),
            g_strconcat (qn, ".", qf, "(", (char *) NULL));
        g_free (qn);
        g_free (qf);
    }

    // the groups of a schema in their order
    for (j = 0; j < s->tree.roots->len; j++)
    {
        query_node_t *ns = g_ptr_array_index (s->tree.roots, j);
        GPtrArray *sorted = g_ptr_array_new_with_free_func (query_node_free);
        int g;

        for (g = 0; g < QUERY_GROUP_COUNT; g++)
        {
            guint k;

            for (k = 0; k < ns->children->len; k++)
                if (strcmp (((query_node_t *) g_ptr_array_index (ns->children, k))->name,
                            _ (query_group_names[g]))
                    == 0)
                    g_ptr_array_add (sorted, g_ptr_array_steal_index (ns->children, k--));
        }
        g_ptr_array_free (ns->children, TRUE);
        ns->children = sorted;
    }

    // that of MC_PG_SCHEMA first; the only one open
    for (j = 0; s->first != NULL && j < s->tree.roots->len; j++)
        if (strcmp (((query_node_t *) g_ptr_array_index (s->tree.roots, j))->name, s->first) == 0)
            first = j;
    if (first > 0)
    {
        gpointer f = g_ptr_array_steal_index (s->tree.roots, first);

        g_ptr_array_insert (s->tree.roots, 0, f);
    }
    if (s->tree.roots->len == 1 && !again)
        ((query_node_t *) g_ptr_array_index (s->tree.roots, 0))->open = TRUE;

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
    return n->id == *(const Oid *) data;
}

/* --------------------------------------------------------------------------------------------- */

/* The sections of the relation of @oid, wherever it is now, of the @results of
   QUERY_SCHEMA_DETAILS; none of them on an error */
static void
query_schema_take_details (query_pg_schema_t *s, Oid oid, GPtrArray *results)
{
    const query_pq_t *pq = s->pq;
    query_node_t *r = NULL;
    query_node_t *sections[QUERY_SECTION_COUNT];
    GString *all;
    int k, i;

    r = query_tree_find (&s->tree, query_schema_is_oid, &oid);
    if (r == NULL)
        return;
    r->asked = FALSE;
    r->read = TRUE;
    if (r->children != NULL)
        g_ptr_array_free (r->children, TRUE);
    r->children = g_ptr_array_new_with_free_func (query_node_free);
    for (i = 0; i < QUERY_SECTION_COUNT; i++)
        sections[i] = query_node_new (r, QUERY_NODE_SECTION, g_strdup (_ (query_section_names[i])),
                                      NULL, NULL);
    if (results != NULL && results->len == QUERY_SECTION_COUNT - 1)
    {
        PGresult *res = g_ptr_array_index (results, 0);

        // the columns; Insert on their section puts them all
        all = g_string_new (NULL);
        for (k = 0; k < pq->PQntuples (res); k++)
        {
            char *name = query_pg_ident (pq->PQgetvalue (res, k, 0));

            (void) query_node_new (sections[QUERY_SECTION_COLUMNS], QUERY_NODE_ITEM,
                                   g_strdup (pq->PQgetvalue (res, k, 0)),
                                   query_schema_column_text (pq, res, k), name);
            g_string_append_printf (all, "%s%s", all->len > 0 ? ", " : "", name);
        }
        sections[QUERY_SECTION_COLUMNS]->insert = g_string_free (all, FALSE);

        // the keys and the foreign keys; Insert puts their columns
        res = g_ptr_array_index (results, 1);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (
                sections[pq->PQgetvalue (res, k, 1)[0] == 'f' ? QUERY_SECTION_FOREIGN
                                                              : QUERY_SECTION_KEYS],
                QUERY_NODE_ITEM, g_strdup (pq->PQgetvalue (res, k, 0)),
                g_strdup (pq->PQgetvalue (res, k, 2)), g_strdup (pq->PQgetvalue (res, k, 3)));

        res = g_ptr_array_index (results, 2);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (sections[QUERY_SECTION_INDEXES], QUERY_NODE_ITEM,
                                   g_strdup (pq->PQgetvalue (res, k, 0)),
                                   query_schema_index_text (pq->PQgetvalue (res, k, 1),
                                                            pq->PQgetvalue (res, k, 2)[0] == 't',
                                                            pq->PQgetvalue (res, k, 3)[0] == 't'),
                                   query_pg_ident (pq->PQgetvalue (res, k, 0)));

        // the foreign keys of the others: the table, its columns; Insert puts the table
        res = g_ptr_array_index (results, 3);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (
                sections[QUERY_SECTION_REFERENCES], QUERY_NODE_ITEM,
                g_strdup_printf ("%s (%s)", pq->PQgetvalue (res, k, 1), pq->PQgetvalue (res, k, 2)),
                g_strdup (pq->PQgetvalue (res, k, 0)), g_strdup (pq->PQgetvalue (res, k, 1)));

        res = g_ptr_array_index (results, 4);
        for (k = 0; k < pq->PQntuples (res); k++)
            (void) query_node_new (
                sections[QUERY_SECTION_TRIGGERS], QUERY_NODE_ITEM,
                g_strdup (pq->PQgetvalue (res, k, 0)),
                query_schema_trigger_text (pq->PQgetvalue (res, k, 1), pq->PQgetvalue (res, k, 0)),
                query_pg_ident (pq->PQgetvalue (res, k, 0)));
    }
    // a table shows all its sections, a view and the like those with something
    for (i = QUERY_SECTION_COUNT - 1; i > QUERY_SECTION_COLUMNS; i--)
        if (r->tag != 'r' && r->tag != 'p' && sections[i]->children->len == 0)
            g_ptr_array_remove_index (r->children, (guint) i);
    for (k = 0; k < (int) r->children->len; k++)
    {
        query_node_t *section = g_ptr_array_index (r->children, k);

        // the columns open the first time
        section->open = query_tree_was_open (&s->tree, section, k == QUERY_SECTION_COLUMNS);
    }
    query_tree_read (&s->tree, r, results != NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* What AI is told of the database, of the rows of QUERY_SCHEMA_AI in @res, NULL for none */
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
    query_tree_mark (&s->tree);
    if (s->asked == QUERY_SCHEMA_ASK_AI)
    {
        char *text =
            s->res_error == NULL ? query_schema_ai_text (s, query_schema_result (s, 0)) : NULL;

        s->ai_sent = FALSE;
        query_ai_context (s->q, text, s->res_error);
        g_free (text);
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

/* What is asked for sent, the list first, what AI is told, the sections of the relations; the
   connection made first */
static void
query_schema_next (query_pg_schema_t *s)
{
    const gboolean wanted = s->want_list || s->want_ai || s->want_details->len > 0;

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
    else if (s->want_ai)
    {
        s->want_ai = FALSE;
        s->ai_sent = TRUE;
        query_schema_send (s, QUERY_SCHEMA_AI, QUERY_SCHEMA_ASK_AI);
    }
    else
    {
        const Oid oid = g_array_index (s->want_details, Oid, 0);
        char *sql = g_strdup_printf (QUERY_SCHEMA_DETAILS, oid, oid, oid, oid, oid);

        s->asked_oid = oid;
        g_array_remove_index (s->want_details, 0);
        query_schema_send (s, sql, QUERY_SCHEMA_ASK_DETAILS);
        g_free (sql);
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
        query_tree_mark (&s->tree);
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
    g_string_append_printf (text, "\nFROM %s\nLIMIT %u;", o->insert, QUERY_TABLE_ROWS);
    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* Enter on @n: it is folded or unfolded, the sections of a relation read the first time */
static void
query_schema_enter (query_pg_schema_t *s, query_node_t *n)
{
    const query_node_t *r = query_tree_enter (&s->tree, n);

    if (r == NULL && n == NULL)
        return;
    if (r != NULL)
    {
        const Oid oid = (Oid) r->id;

        g_array_append_val (s->want_details, oid);
    }
    query_tree_render (&s->tree);
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
    query_tree_init (&s->tree, q);
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
    query_tree_done (&s->tree);
    g_free (s->first);
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

void
query_pg_schema_do (query_pg_schema_t *s, query_schema_action_t action, long line)
{
    query_node_t *n = query_tree_node_at (&s->tree, line);

    switch (action)
    {
    case QUERY_SCHEMA_SHOW:
        s->tree.make = TRUE;
        if (!s->tree.read && s->state != QUERY_SCHEMA_BUSY)
            query_schema_refresh (s);
        else
        {
            query_tree_mark (&s->tree);
            query_tree_render (&s->tree);
        }
        break;
    case QUERY_SCHEMA_REFRESH:
        query_schema_refresh (s);
        break;
    case QUERY_SCHEMA_ENTER:
        query_schema_enter (s, n);
        break;
    case QUERY_SCHEMA_ROWS:
        query_tree_rows (&s->tree, n);
        break;
    case QUERY_SCHEMA_INSERT:
        query_tree_insert (&s->tree, n);
        break;
    case QUERY_SCHEMA_QUERY:
    {
        const query_node_t *r = query_tree_query (&s->tree, n);

        if (r != NULL)
        {
            const Oid oid = (Oid) r->id;

            g_array_append_val (s->want_details, oid);
            query_schema_next (s);
        }
        break;
    }
    default:
        break;
    }
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
