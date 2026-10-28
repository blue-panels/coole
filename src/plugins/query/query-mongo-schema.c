/*
   The query plugin: the window Schema of MongoDB, its databases, collections and fields.

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

/** \file query-mongo-schema.c
 *  \brief Source: the window Schema of the queries of MongoDB
 *
 *  mongosh keeps no connection: each thing read is a mongosh of its own, run by query_proc,
 *  one at a time, that prints what it read as Extended JSON on a line of its own.  The
 *  databases and their collections once, in groups by their kind; the fields of a collection,
 *  of a sample of its documents and never a value, its indexes and its validator when it is
 *  unfolded.  The system databases are left out, but that of the connection.
 */

#include <config.h>

#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <json-glib/json-glib.h>

#include "lib/global.h"

#include "query-core.h"
#include "query-mongo-schema.h"
#include "query-tree.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// the line mongosh prints what it read on, after this
#define QUERY_MS_TAG                                                                               \
    "\x1e"                                                                                         \
    "coole-schema "

/* The scripts mongosh runs, those of misc/query: mongo-list.js, mongo-details.js, mongo-ai.js,
   mongo-ddl.js; what they read printed after QUERY_MS_TAG */

// the collections AI is told of at most
#define QUERY_MS_AI_COLLECTIONS 300

/*** file scope type declarations ****************************************************************/

typedef enum
{
    QUERY_MS_ASK_LIST,
    QUERY_MS_ASK_DETAILS,
    QUERY_MS_ASK_AI,
    QUERY_MS_ASK_DDL
} query_ms_ask_t;

/* The groups of a database, in their order */
enum
{
    QUERY_MS_GROUP_COLLECTIONS,
    QUERY_MS_GROUP_VIEWS,
    QUERY_MS_GROUP_TIMESERIES,
    QUERY_MS_GROUP_COUNT
};

/* The sections of a collection unfolded, in their order */
enum
{
    QUERY_MS_SECTION_FIELDS,
    QUERY_MS_SECTION_INDEXES,
    QUERY_MS_SECTION_VALIDATOR,
    QUERY_MS_SECTION_PIPELINE,
    QUERY_MS_SECTION_COUNT
};

struct query_mongo_schema_t
{
    query_t *q;
    char *mongosh;

    query_proc_t *proc;  // the mongosh of now, NULL for none
    query_ms_ask_t asked;
    char *asked_key;  // the database and the collection of the details asked, as want_details has

    gboolean want_list;
    gboolean want_ai;         // what AI is told, to be read
    char *want_ai_use;        // of the database of the use () of the request, NULL for none
    gboolean ai_sent;         // and is read
    GPtrArray *want_details;  // "db\x1fcollection" of the collections to read
    char *want_ddl;           // "db\x1fcollection" of the one whose DDL F4 asks for, NULL for none
    guint want_ddl_id;        // the F4 it is of
    guint asked_ddl_id;

    char *current;  // the database of the connection, first and unfolded
    query_tree_t tree;
};

/*** forward declarations (file scope functions) *************************************************/

static void query_ms_next (query_mongo_schema_t *s);

/*** file scope variables ************************************************************************/

static const char *const query_ms_group_names[QUERY_MS_GROUP_COUNT] = {
    N_ ("Collections"),
    N_ ("Views"),
    N_ ("Time series"),
};

// the marks of the items of each section
static const query_icon_t query_ms_section_icons[QUERY_MS_SECTION_COUNT] = {
    QUERY_ICON_FIELD,
    QUERY_ICON_INDEX,
    QUERY_ICON_VALIDATOR,
    QUERY_ICON_PIPELINE,
};

static const char *const query_ms_section_names[QUERY_MS_SECTION_COUNT] = {
    N_ ("Fields"),
    N_ ("Indexes"),
    N_ ("Validator"),
    N_ ("Pipeline"),
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* @s as a string of JavaScript: a string of JSON */
static char *
query_ms_string (const char *s)
{
    JsonNode *node = json_node_new (JSON_NODE_VALUE);
    char *text;

    json_node_set_string (node, s);
    text = json_to_string (node, FALSE);
    json_node_unref (node);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @name is a name of JavaScript, db.@name reaching the collection */
static gboolean
query_ms_js_name (const char *name)
{
    const char *p;

    if (!g_ascii_isalpha (*name) && *name != '_' && *name != '$')
        return FALSE;
    for (p = name; *p != '\0'; p++)
        if (!g_ascii_isalnum (*p) && *p != '_' && *p != '$')
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* What reaches the collection @coll of the database @dbname: db.orders in the database of the
   connection, db.getSiblingDB ("x").getCollection ("y") in another */
static char *
query_ms_collection (const query_mongo_schema_t *s, const char *dbname, const char *coll)
{
    char *c = query_ms_string (coll), *d, *text;

    if (g_strcmp0 (dbname, s->current) == 0)
        text = query_ms_js_name (coll) ? g_strconcat ("db.", coll, (char *) NULL)
                                       : g_strdup_printf ("db.getCollection(%s)", c);
    else
    {
        d = query_ms_string (dbname);
        text = g_strdup_printf ("db.getSiblingDB(%s).getCollection(%s)", d, c);
        g_free (d);
    }
    g_free (c);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* What reaches the collection @coll of the database @dbname whatever the use () of the file:
   db.getSiblingDB ("shop").getCollection ("orders") */
static char *
query_ms_reach (const char *dbname, const char *coll)
{
    char *d = query_ms_string (dbname), *c = query_ms_string (coll);
    char *text = g_strdup_printf ("db.getSiblingDB(%s).getCollection(%s)", d, c);

    g_free (d);
    g_free (c);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* The database of @n: the root of the tree it is under */
static const query_node_t *
query_ms_db (const query_node_t *n)
{
    for (; n->parent != NULL && QUERY_ITEM (n)->kind != QUERY_NODE_ROOT; n = n->parent)
        ;
    return n;
}

/* --------------------------------------------------------------------------------------------- */

/* The key of the collection @o in want_details: "db\x1fcollection" */
static char *
query_ms_key (const query_node_t *o)
{
    return g_strconcat (query_ms_db (o)->key, "\x1f", o->key, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* What mongosh printed after QUERY_MS_TAG, as JSON; NULL with @error when it is none */
static JsonNode *
query_ms_json (const char *out, char **error)
{
    const char *tag = strstr (out, QUERY_MS_TAG);
    JsonParser *parser;
    JsonNode *node = NULL;
    GError *gerror = NULL;
    char *line;

    if (tag == NULL)
    {
        *error = g_strdup (_ ("mongosh printed nothing of the schema."));
        return NULL;
    }
    tag += strlen (QUERY_MS_TAG);
    line = g_strndup (tag, strcspn (tag, "\n"));
    parser = json_parser_new ();
    if (json_parser_load_from_data (parser, line, -1, &gerror))
        node = json_node_copy (json_parser_get_root (parser));
    else
    {
        *error = g_strdup (gerror->message);
        g_clear_error (&gerror);
    }
    g_object_unref (parser);
    g_free (line);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
query_ms_member_string (JsonObject *o, const char *name)
{
    JsonNode *m = json_object_get_member (o, name);

    return m != NULL && JSON_NODE_HOLDS_VALUE (m) && json_node_get_value_type (m) == G_TYPE_STRING
        ? json_node_get_string (m)
        : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The number @name of @o, -1 for none */
static gint64
query_ms_member_int (JsonObject *o, const char *name)
{
    JsonNode *m = json_object_get_member (o, name);

    if (m == NULL || !JSON_NODE_HOLDS_VALUE (m))
        return -1;
    if (json_node_get_value_type (m) == G_TYPE_INT64)
        return json_node_get_int (m);
    if (json_node_get_value_type (m) == G_TYPE_DOUBLE)
    {
        const double d = json_node_get_double (m);

        return (gint64) d;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_ms_member_true (JsonObject *o, const char *name)
{
    JsonNode *m = json_object_get_member (o, name);

    return m != NULL && JSON_NODE_HOLDS_VALUE (m) && json_node_get_value_type (m) == G_TYPE_BOOLEAN
        && json_node_get_boolean (m);
}

/* --------------------------------------------------------------------------------------------- */

/* The databases and their collections of the JSON of mongo-list.js: what was open open again, the
   details of a collection unfolded read again */
static void
query_ms_take_list (query_mongo_schema_t *s, JsonObject *list)
{
    JsonArray *dbs = json_object_get_array_member (list, "dbs");
    const char *current = query_ms_member_string (list, "current");
    GPtrArray *roots;
    guint i, j, first = 0;

    g_free (s->current);
    s->current = g_strdup (current);
    query_tree_forget (&s->tree);
    for (i = 0; dbs != NULL && i < json_array_get_length (dbs); i++)
    {
        JsonObject *d = json_array_get_object_element (dbs, i);
        const char *dbname = query_ms_member_string (d, "name");
        JsonArray *colls = json_object_get_array_member (d, "colls");
        char *quoted = query_ms_string (dbname);
        query_node_t *root = query_node_new (&s->tree, NULL, QUERY_NODE_ROOT, g_strdup (dbname),
                                             NULL, g_strdup_printf ("db.getSiblingDB(%s)", quoted));

        g_free (quoted);
        QUERY_ITEM (root)->icon = QUERY_ICON_DATABASE;
        root->expanded = query_tree_was_open (&s->tree, root, g_strcmp0 (dbname, current) == 0);
        if (g_strcmp0 (dbname, current) == 0)
            first = i;
        for (j = 0; colls != NULL && j < json_array_get_length (colls); j++)
        {
            JsonObject *c = json_array_get_object_element (colls, j);
            const char *name = query_ms_member_string (c, "name");
            const char *type = query_ms_member_string (c, "type");
            const char *view_on = query_ms_member_string (c, "viewOn");
            const gint64 count = query_ms_member_int (c, "count");
            const int g = g_strcmp0 (type, "view") == 0 ? QUERY_MS_GROUP_VIEWS
                : g_strcmp0 (type, "timeseries") == 0   ? QUERY_MS_GROUP_TIMESERIES
                                                        : QUERY_MS_GROUP_COLLECTIONS;
            query_node_t *o;
            query_item_t *item;
            char *reach;

            if (name == NULL)
                continue;
            o = query_node_new (&s->tree,
                                query_node_group (&s->tree, root, _ (query_ms_group_names[g]),
                                                  g == QUERY_MS_GROUP_COLLECTIONS),
                                QUERY_NODE_OBJECT, g_strdup (name),
                                view_on != NULL  ? g_strdup_printf (_ ("on %s"), view_on)
                                    : count >= 0 ? query_tree_about (count)
                                                 : NULL,
                                query_ms_collection (s, dbname, name));
            item = QUERY_ITEM (o);
            item->icon = g == QUERY_MS_GROUP_VIEWS ? QUERY_ICON_VIEW
                : g == QUERY_MS_GROUP_TIMESERIES   ? QUERY_ICON_TIMESERIES
                                                   : QUERY_ICON_COLLECTION;
            // the rows of F5 in the database of the collection, whatever the use () of the file
            reach = query_ms_reach (dbname, name);
            item->rows =
                g_strdup_printf ("// @table\n%s.find({}).limit(%u)", reach, QUERY_TABLE_ROWS);
            g_free (reach);
            o->expanded = query_tree_was_open (&s->tree, o, FALSE);
            if (o->expanded)
            {
                item->asked = TRUE;
                g_ptr_array_add (s->want_details, query_ms_key (o));
            }
        }
        // the groups in their order; the nodes are the model's, the arrays their order
        if (root->children != NULL)
        {
            GPtrArray *sorted = g_ptr_array_new ();
            int g;

            for (g = 0; g < QUERY_MS_GROUP_COUNT; g++)
            {
                guint k;

                for (k = 0; k < root->children->len; k++)
                    if (strcmp (((query_node_t *) g_ptr_array_index (root->children, k))->key,
                                _ (query_ms_group_names[g]))
                        == 0)
                        g_ptr_array_add (sorted, g_ptr_array_steal_index (root->children, k--));
            }
            g_ptr_array_free (root->children, TRUE);
            root->children = sorted;
        }
    }
    // that of the connection first
    roots = query_tree_roots (&s->tree);
    if (first > 0 && roots != NULL)
    {
        gpointer f = g_ptr_array_steal_index (roots, first);

        g_ptr_array_insert (roots, 0, f);
    }
    s->tree.read = TRUE;
    g_clear_pointer (&s->tree.error, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @n is the collection of the key at @data, "db\x1fcollection" */
static gboolean
query_ms_is_key (const query_node_t *n, const void *data)
{
    const char *key = (const char *) data;
    const char *sep = strchr (key, '\x1f');
    const char *db;

    if (sep == NULL || strcmp (n->key, sep + 1) != 0)
        return FALSE;
    db = query_ms_db (n)->key;
    return strlen (db) == (gsize) (sep - key) && strncmp (db, key, sep - key) == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The sections of the collection of @key, wherever it is now, of the JSON of mongo-details.js;
   none of them on an error */
static void
query_ms_take_details (query_mongo_schema_t *s, const char *key, JsonObject *details)
{
    query_node_t *o = query_tree_find (&s->tree, query_ms_is_key, key);
    query_node_t *sections[QUERY_MS_SECTION_COUNT];
    JsonArray *fields, *indexes;
    const gint64 sampled = details != NULL ? query_ms_member_int (details, "sampled") : 0;
    const char *validator = details != NULL ? query_ms_member_string (details, "validator") : NULL;
    const char *pipeline = details != NULL ? query_ms_member_string (details, "pipeline") : NULL;
    guint i;
    int k;

    // read already: the list read twice asked for it twice
    if (o == NULL || QUERY_ITEM (o)->read)
        return;
    // the fields and the indexes always, the validator and the pipeline when there is one
    for (k = 0; k < QUERY_MS_SECTION_COUNT; k++)
        sections[k] = k <= QUERY_MS_SECTION_INDEXES
                || (k == QUERY_MS_SECTION_VALIDATOR && validator != NULL)
                || (k == QUERY_MS_SECTION_PIPELINE && pipeline != NULL)
            ? query_node_new (&s->tree, o, QUERY_NODE_SECTION,
                              g_strdup (_ (query_ms_section_names[k])), NULL, NULL)
            : NULL;
    fields = details != NULL ? json_object_get_array_member (details, "fields") : NULL;
    indexes = details != NULL ? json_object_get_array_member (details, "indexes") : NULL;

    // the fields, the types of their values; how many documents have one, when not all do
    for (i = 0; fields != NULL && i < json_array_get_length (fields); i++)
    {
        JsonObject *f = json_array_get_object_element (fields, i);
        JsonArray *types = json_object_get_array_member (f, "types");
        const gint64 n = query_ms_member_int (f, "n");
        GString *text = g_string_new (NULL);
        guint t;

        for (t = 0; types != NULL && t < json_array_get_length (types); t++)
            g_string_append_printf (text, "%s%s", t > 0 ? ", " : "",
                                    json_array_get_string_element (types, t));
        if (sampled > 0 && n >= 0 && n < sampled)
            g_string_append_printf (text, " %d%%", (int) (n * 100 / sampled));
        (void) query_node_new (&s->tree, sections[QUERY_MS_SECTION_FIELDS], QUERY_NODE_ITEM,
                               g_strdup (query_ms_member_string (f, "name")),
                               g_string_free (text, FALSE),
                               g_strdup (query_ms_member_string (f, "name")));
    }
    // the indexes and their keys; Insert puts the key
    for (i = 0; indexes != NULL && i < json_array_get_length (indexes); i++)
    {
        JsonObject *x = json_array_get_object_element (indexes, i);
        const gint64 ttl = query_ms_member_int (x, "ttl");
        GString *text = g_string_new (query_ms_member_string (x, "key"));

        if (query_ms_member_true (x, "unique"))
            g_string_append (text, " unique");
        if (query_ms_member_true (x, "sparse"))
            g_string_append (text, " sparse");
        if (ttl >= 0)
            g_string_append_printf (text, " ttl %" G_GINT64_FORMAT " s", ttl);
        (void) query_node_new (&s->tree, sections[QUERY_MS_SECTION_INDEXES], QUERY_NODE_ITEM,
                               g_strdup (query_ms_member_string (x, "name")),
                               g_string_free (text, FALSE),
                               g_strdup (query_ms_member_string (x, "key")));
    }
    if (validator != NULL)
        (void) query_node_new (&s->tree, sections[QUERY_MS_SECTION_VALIDATOR], QUERY_NODE_ITEM,
                               g_strdup (validator), NULL, g_strdup (validator));
    if (pipeline != NULL)
        (void) query_node_new (&s->tree, sections[QUERY_MS_SECTION_PIPELINE], QUERY_NODE_ITEM,
                               g_strdup (pipeline), NULL, g_strdup (pipeline));

    // what the items of each section are, for their marks
    for (k = 0; k < QUERY_MS_SECTION_COUNT; k++)
        for (i = 0;
             sections[k] != NULL && sections[k]->children != NULL && i < sections[k]->children->len;
             i++)
            QUERY_ITEM ((query_node_t *) g_ptr_array_index (sections[k]->children, i))->icon =
                query_ms_section_icons[k];
    // the fields open the first time
    for (k = 0; k < QUERY_MS_SECTION_COUNT; k++)
        if (sections[k] != NULL)
            sections[k]->expanded =
                query_tree_was_open (&s->tree, sections[k], k == QUERY_MS_SECTION_FIELDS);
    query_node_read (o);
    query_tree_read (&s->tree, o, details != NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* The mongosh of now has ended: what it read taken, the window made again, the next one run */
static void
query_ms_done (char *out, char *err, int status, gboolean cut, void *data)
{
    query_mongo_schema_t *s = (query_mongo_schema_t *) data;
    const gboolean ok = WIFEXITED (status) && WEXITSTATUS (status) == 0;
    char *error = NULL;

    (void) cut;
    s->proc = NULL;
    if (s->asked == QUERY_MS_ASK_AI)
    {
        char *clean = query_mongo_clean (out);
        const char *read_only = g_getenv ("MC_QUERY_READ_ONLY");

        s->ai_sent = FALSE;
        g_strstrip (clean);
        if (!ok || *clean == '\0')
            query_ai_context (s->q, NULL, g_strstrip (err));
        else
        {
            char *context = read_only != NULL && strcmp (read_only, "1") == 0
                ? g_strconcat (clean, "\nThe user reads only: write no change.", (char *) NULL)
                : g_strdup (clean);

            query_ai_context (s->q, context, NULL);
            g_free (context);
        }
        g_free (clean);
    }
    else
    {
        JsonNode *node = ok ? query_ms_json (out, &error) : NULL;
        JsonObject *o =
            node != NULL && JSON_NODE_HOLDS_OBJECT (node) ? json_node_get_object (node) : NULL;

        if (!ok)
            error = g_strdup (*err != '\0' ? g_strstrip (err) : _ ("mongosh failed."));
        if (s->asked == QUERY_MS_ASK_DDL)
            query_tree_ddl_put (&s->tree, s->asked_ddl_id,
                                o != NULL ? query_ms_member_string (o, "ddl") : NULL,
                                o != NULL && query_ms_member_string (o, "error") != NULL
                                    ? query_ms_member_string (o, "error")
                                    : error);
        else if (s->asked == QUERY_MS_ASK_LIST)
        {
            if (o != NULL)
                query_ms_take_list (s, o);
            else
            {
                g_free (s->tree.error);
                s->tree.error = error;
                error = NULL;
            }
        }
        else
            // an error is the sections of none: the collection shows nothing in them
            query_ms_take_details (s, s->asked_key, o);
        if (node != NULL)
            json_node_unref (node);
        g_free (error);
        query_tree_render (&s->tree);
    }
    g_clear_pointer (&s->asked_key, g_free);
    g_free (out);
    g_free (err);
    query_ms_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* mongosh run on @script, as @asked, both taken; no script, @why tells it as a failure of mongosh
   would be */
static void
query_ms_run (query_mongo_schema_t *s, char *script, char *why, query_ms_ask_t asked)
{
    const char *argv[] = { s->mongosh, "--nodb", "--quiet", "--eval", script, NULL };
    GError *error = NULL;

    s->asked = asked;
    if (script != NULL)
    {
        s->proc = query_proc_start (argv, NULL, NULL, "", query_ms_done, s, &error);
        g_free (script);
        if (s->proc != NULL)
            return;
    }
    // told as an answer of mongosh would be
    {
        char *message = why != NULL ? why : g_strdup_printf ("%s: %s", s->mongosh, error->message);

        g_clear_error (&error);
        if (asked == QUERY_MS_ASK_AI)
        {
            s->ai_sent = FALSE;
            query_ai_context (s->q, NULL, message);
        }
        else if (asked == QUERY_MS_ASK_LIST)
        {
            g_free (s->tree.error);
            s->tree.error = g_strdup (message);
            query_tree_render (&s->tree);
        }
        g_free (message);
        g_clear_pointer (&s->asked_key, g_free);
        // the details of the collections are not asked for again and again, nor the DDL
        g_ptr_array_set_size (s->want_details, 0);
        g_clear_pointer (&s->want_ddl, g_free);
        query_tree_unask (&s->tree);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* What is asked for run, the list first, what AI is told, the details of the collections */
static void
query_ms_next (query_mongo_schema_t *s)
{
    if (s->proc != NULL)
        return;
    if (s->want_list)
    {
        s->want_list = FALSE;
        char *why = NULL;
        char *script = query_script ("mongo-list.js", &why);

        query_ms_run (s, script, why, QUERY_MS_ASK_LIST);
    }
    else if (s->want_ddl != NULL)
    {
        char *key = s->want_ddl;
        char *sep = strchr (key, '\x1f');
        char *why = NULL;
        char *script = query_script ("mongo-ddl.js", &why);
        char *d, *c;

        s->want_ddl = NULL;
        s->asked_ddl_id = s->want_ddl_id;
        *sep = '\0';
        d = query_ms_string (key);
        c = query_ms_string (sep + 1);
        query_ms_run (
            s, script != NULL ? query_script_fill (script, "db", d, "collection", c, NULL) : NULL,
            why, QUERY_MS_ASK_DDL);
        g_free (script);
        g_free (d);
        g_free (c);
        g_free (key);
    }
    else if (s->want_ai)
    {
        char *used = s->want_ai_use != NULL
            ? g_strdup_printf ("db = db.getSiblingDB(%s);", s->want_ai_use)
            : g_strdup ("");
        char *max = g_strdup_printf ("%d", QUERY_MS_AI_COLLECTIONS);
        char *why = NULL;
        char *script = query_script ("mongo-ai.js", &why);
        char *filled =
            script != NULL ? query_script_fill (script, "use", used, "max", max, NULL) : NULL;

        s->want_ai = FALSE;
        s->ai_sent = TRUE;
        g_clear_pointer (&s->want_ai_use, g_free);
        query_ms_run (s, filled, why, QUERY_MS_ASK_AI);
        g_free (script);
        g_free (used);
        g_free (max);
    }
    else if (s->want_details->len > 0)
    {
        char *key = g_ptr_array_steal_index (s->want_details, 0);
        char *sep = strchr (key, '\x1f');
        char *why = NULL;
        char *script = query_script ("mongo-details.js", &why);
        char *d, *c;

        *sep = '\0';
        d = query_ms_string (key);
        c = query_ms_string (sep + 1);
        *sep = '\x1f';
        s->asked_key = key;
        query_ms_run (
            s, script != NULL ? query_script_fill (script, "db", d, "collection", c, NULL) : NULL,
            why, QUERY_MS_ASK_DETAILS);
        g_free (script);
        g_free (d);
        g_free (c);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The query of F3 of the collection @o: find () of its @fields, the names of the fields, all of
   them when there is none; its documents in Table */
static char *
query_ms_query (const query_node_t *o, GPtrArray *fields)
{
    GString *text = g_string_new ("// @table\n");
    // whatever the use () of the file, as F5 of Schema
    char *reach = query_ms_reach (query_ms_db (o)->key, o->key);
    guint i;

    g_string_append_printf (text, "%s.find({}", reach);
    g_free (reach);
    for (i = 0; i < fields->len; i++)
    {
        const char *name = g_ptr_array_index (fields, i);
        char *key = query_ms_js_name (name) ? g_strdup (name) : query_ms_string (name);

        g_string_append_printf (text, "%s%s: 1", i == 0 ? ", { " : ", ", key);
        g_free (key);
    }
    if (fields->len > 0)
        g_string_append (text, " }");
    g_string_append_printf (text, ").limit(%u)", QUERY_TABLE_ROWS);
    return g_string_free (text, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* F4 on @n: the drop, commented, and the create of its collection or view, read by mongosh, put
   in the file of queries; of a database, its drop and its use () */
static void
query_ms_ddl (query_mongo_schema_t *s, query_node_t *n)
{
    const query_item_t *item = n != NULL ? QUERY_ITEM (n) : NULL;
    const query_node_t *o;
    guint id;

    if (item == NULL)
        return;
    id = query_tree_ddl_ask (&s->tree);
    if (item->kind == QUERY_NODE_ROOT)
    {
        char *quoted = query_ms_string (n->key);
        char *ddl = g_strdup_printf ("// db.getSiblingDB(%s).dropDatabase()\n"
                                     "// a database is made with its first collection\n"
                                     "use(%s)\n",
                                     quoted, quoted);

        query_tree_ddl_put (&s->tree, id, ddl, NULL);
        g_free (ddl);
        g_free (quoted);
        return;
    }
    o = query_node_object (n);
    if (o == NULL)
    {
        query_tree_ddl_put (&s->tree, id, NULL,
                            _ ("F4 puts the DDL of a database or a collection."));
        return;
    }
    g_free (s->want_ddl);
    s->want_ddl = query_ms_key (o);
    s->want_ddl_id = id;
    query_ms_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* A collection opened, or F3 on one: its details asked for, the tree query_tree_t.details */
static void
query_ms_details (query_node_t *o, void *data)
{
    query_mongo_schema_t *s = (query_mongo_schema_t *) data;

    g_ptr_array_add (s->want_details, query_ms_key (o));
    query_ms_next (s);
}

/* --------------------------------------------------------------------------------------------- */

/* The list read again, the window made when it is not there yet */
static void
query_ms_refresh (query_mongo_schema_t *s)
{
    g_clear_pointer (&s->tree.error, g_free);
    s->want_list = TRUE;
    // the details asked for come with the list
    g_ptr_array_set_size (s->want_details, 0);
    query_tree_cancel (&s->tree);
    query_tree_render (&s->tree);
    query_ms_next (s);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

query_mongo_schema_t *
query_mongo_schema_new (query_t *q, const char *mongosh)
{
    query_mongo_schema_t *s = g_new0 (query_mongo_schema_t, 1);

    s->q = q;
    s->mongosh = g_strdup (mongosh);
    s->want_details = g_ptr_array_new_with_free_func (g_free);
    query_tree_init (&s->tree, q, query_ms_details, s);
    s->tree.query = query_ms_query;
    return s;
}

/* --------------------------------------------------------------------------------------------- */

void
query_mongo_schema_free (query_mongo_schema_t *s)
{
    if (s == NULL)
        return;
    if (s->proc != NULL)
        query_proc_kill (s->proc);
    g_ptr_array_free (s->want_details, TRUE);
    g_free (s->want_ddl);
    query_tree_done (&s->tree);
    g_free (s->asked_key);
    g_free (s->want_ai_use);
    g_free (s->current);
    g_free (s->mongosh);
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_mongo_schema_do (query_mongo_schema_t *s, query_schema_action_t action, gboolean key)
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
        if (!s->tree.read && !(s->proc != NULL && s->asked == QUERY_MS_ASK_LIST))
            query_ms_refresh (s);
        else
            query_tree_render (&s->tree);
        // QuerySchema gives it the focus, the start does not
        if (action == QUERY_SCHEMA_TOGGLE && s->tree.window != NULL)
            s->q->host->window_show (s->q->host, s->tree.window);
        break;
    case QUERY_SCHEMA_REFRESH:
        query_ms_refresh (s);
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
            query_ms_details (o, s);
        break;
    case QUERY_SCHEMA_DDL:
        query_ms_ddl (s, n);
        break;
    default:
        break;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
query_mongo_schema_changed (query_mongo_schema_t *s)
{
    if (s->tree.read)
        query_ms_refresh (s);
}

/* --------------------------------------------------------------------------------------------- */

void
query_mongo_schema_ai (query_mongo_schema_t *s, const char *use)
{
    if (s->want_ai || s->ai_sent)
        return;
    s->want_ai = TRUE;
    g_free (s->want_ai_use);
    s->want_ai_use = g_strdup (use);
    query_ms_next (s);
}

/* --------------------------------------------------------------------------------------------- */
