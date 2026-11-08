/*
   The query plugin: the rows of a query written to a file, CSV, JSON, SQL, Markdown.

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

/** \file query-export.c
 *  \brief Source: the rows of a query written to a file, CSV, JSON, SQL, Markdown
 *
 *  The rows come one by one and go out as they come, into a buffer the caller writes out: a
 *  table of millions of rows is never all in memory.  None of it needs the screen: the tests
 *  take it alone.
 */

#include <config.h>

#include <string.h>

#include <json-glib/json-glib.h>

#include "lib/global.h"

#include "query-core.h"
#include "query-export.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

struct query_export_t
{
    query_export_options_t o;
    GString *out;
    GPtrArray *columns;  // their names, NULL until they are known
    GHashTable *names;   // the same, to be found
    GPtrArray *types;    // of SQL, NULL for none known
    gint64 rows;
    gboolean opened;  // what goes before the first row is written: "[" of JSON

    // documents of mongo for a format of columns: the first ones kept until their keys are known
    GPtrArray *sample;  // JsonNode, NULL once the columns are made of them
    GHashTable *skipped;
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static const char *const query_export_extensions[QUERY_EXPORT_FORMATS] = {
    "csv", "json", "ndjson", "sql", "md",
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Whether @s of @len bytes is a number as JSON writes it: no NaN, no Infinity, no "+1", no ".5" */
static gboolean
query_json_number (const char *s, gsize len)
{
    const char *p = s, *end = s + len;

    if (p < end && *p == '-')
        p++;
    if (p == end || !g_ascii_isdigit (*p))
        return FALSE;
    if (*p == '0')
        p++;
    else
        while (p < end && g_ascii_isdigit (*p))
            p++;
    if (p < end && *p == '.')
    {
        if (++p == end || !g_ascii_isdigit (*p))
            return FALSE;
        while (p < end && g_ascii_isdigit (*p))
            p++;
    }
    if (p < end && (*p == 'e' || *p == 'E'))
    {
        p++;
        if (p < end && (*p == '+' || *p == '-'))
            p++;
        if (p == end || !g_ascii_isdigit (*p))
            return FALSE;
        while (p < end && g_ascii_isdigit (*p))
            p++;
    }
    return p == end;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a value of a column of booleans is true: "t" of PostgreSQL, "true" */
static gboolean
query_bool_true (const char *s, gsize len)
{
    return (len == 1 && *s == 't') || (len == 4 && strncmp (s, "true", 4) == 0);
}

/* --------------------------------------------------------------------------------------------- */

/* @s of @len bytes as a string of JSON, in its quotes */
static void
query_json_string (GString *out, const char *s, gsize len)
{
    const char *end = s + len;

    g_string_append_c (out, '"');
    for (; s < end; s++)
        switch (*s)
        {
        case '"':
            g_string_append (out, "\\\"");
            break;
        case '\\':
            g_string_append (out, "\\\\");
            break;
        case '\n':
            g_string_append (out, "\\n");
            break;
        case '\r':
            g_string_append (out, "\\r");
            break;
        case '\t':
            g_string_append (out, "\\t");
            break;
        default:
            if ((unsigned char) *s < 0x20)
                g_string_append_printf (out, "\\u%04x", (unsigned char) *s);
            else
                g_string_append_c (out, *s);
            break;
        }
    g_string_append_c (out, '"');
}

/* --------------------------------------------------------------------------------------------- */

/* @s of @len bytes as a literal of SQL, its quotes doubled: standard_conforming_strings, on since
   PostgreSQL 9.1, takes a backslash as it is */
static void
query_sql_string (GString *out, const char *s, gsize len)
{
    const char *end = s + len;

    g_string_append_c (out, '\'');
    for (; s < end; s++)
    {
        if (*s == '\'')
            g_string_append_c (out, '\'');
        g_string_append_c (out, *s);
    }
    g_string_append_c (out, '\'');
}

/* --------------------------------------------------------------------------------------------- */

/* A value of CSV: in quotes when it has the delimiter, a quote or the end of a line, or when all
   of them are */
static void
query_csv_value (const query_export_t *x, GString *out, const char *s, gsize len)
{
    gboolean quote = x->o.quote_all;
    gsize i;

    for (i = 0; i < len && !quote; i++)
        quote = s[i] == x->o.delimiter || s[i] == '"' || s[i] == '\n' || s[i] == '\r';
    if (!quote)
    {
        g_string_append_len (out, s, (gssize) len);
        return;
    }
    g_string_append_c (out, '"');
    for (i = 0; i < len; i++)
    {
        if (s[i] == '"')
            g_string_append_c (out, '"');
        g_string_append_c (out, s[i]);
    }
    g_string_append_c (out, '"');
}

/* --------------------------------------------------------------------------------------------- */

/* A cell of Markdown: on one line, its '|' no end of the cell */
static void
query_md_value (GString *out, const char *s, gsize len)
{
    const char *end = s + len;

    for (; s < end; s++)
        if (*s == '\n')
            g_string_append (out, "<br>");
        else if (*s == '|')
            g_string_append (out, "\\|");
        else if (*s == '\t' || *s == '\r')
            g_string_append_c (out, ' ');
        else if ((unsigned char) *s >= 0x20 && *s != 0x7f)
            g_string_append_c (out, *s);
}

/* --------------------------------------------------------------------------------------------- */

/* The names of the columns, the first line of CSV and of Markdown, the list of INSERT */
static void
query_export_head (query_export_t *x)
{
    GString *out = x->out;
    guint c;

    switch (x->o.format)
    {
    case QUERY_EXPORT_CSV:
        if (!x->o.header || x->columns->len == 0)
            break;
        for (c = 0; c < x->columns->len; c++)
        {
            const char *name = g_ptr_array_index (x->columns, c);

            if (c > 0)
                g_string_append_c (out, x->o.delimiter);
            query_csv_value (x, out, name, strlen (name));
        }
        g_string_append_c (out, '\n');
        break;
    case QUERY_EXPORT_SQL:
        // the table of the INSERT statements, of the columns of the rows
        if (!x->o.create_table || x->columns->len == 0)
            break;
        g_string_append_printf (out, "CREATE TABLE %s (\n", x->o.table);
        for (c = 0; c < x->columns->len; c++)
        {
            char *name = query_pg_ident (g_ptr_array_index (x->columns, c));
            const char *type = x->types != NULL && c < x->types->len
                ? (const char *) g_ptr_array_index (x->types, c)
                : "text";

            g_string_append_printf (out, "  %s %s%s\n", name, type,
                                    c + 1 < x->columns->len ? "," : "");
            g_free (name);
        }
        g_string_append (out, ");\n\n");
        break;
    case QUERY_EXPORT_MARKDOWN:
        if (x->columns->len == 0)
            break;
        for (c = 0; c < x->columns->len; c++)
        {
            const char *name = g_ptr_array_index (x->columns, c);

            g_string_append (out, "| ");
            query_md_value (out, name, strlen (name));
            g_string_append_c (out, ' ');
        }
        g_string_append (out, "|\n");
        for (c = 0; c < x->columns->len; c++)
            g_string_append (out, "|---");
        g_string_append (out, "|\n");
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The table @name of INSERT as SQL takes it: as it is when it is names of SQL, "sales.orders",
   "Sales"."Orders", else in double quotes, "my-table" */
static char *
query_sql_table (const char *name)
{
    const char *p = name;

    while (TRUE)
    {
        if (*p == '"')
        {
            // a name in quotes, its quotes doubled
            for (p++; *p != '\0' && !(p[0] == '"' && p[1] != '"'); p += p[0] == '"' ? 2 : 1)
                ;
            if (*p != '"')
                break;
            p++;
        }
        else if (g_ascii_isalpha (*p) || *p == '_')
            while (g_ascii_isalnum (*p) || *p == '_' || *p == '$')
                p++;
        else
            break;
        if (*p == '\0')
            return g_strdup (name);
        if (*p != '.')
            break;
        p++;
    }
    return query_pg_ident (name);
}

/* --------------------------------------------------------------------------------------------- */

/* The start of the INSERT of the rows that come: "INSERT INTO t (a, b) VALUES" */
static void
query_sql_insert (query_export_t *x)
{
    GString *out = x->out;
    guint c;

    g_string_append_printf (out, "INSERT INTO %s (", x->o.table);
    for (c = 0; c < x->columns->len; c++)
    {
        char *name = query_pg_ident (g_ptr_array_index (x->columns, c));

        if (c > 0)
            g_string_append (out, ", ");
        g_string_append (out, name);
        g_free (name);
    }
    g_string_append (out, ") VALUES");
}

/* --------------------------------------------------------------------------------------------- */

/* A value of JSON, by its kind */
static void
query_json_value (GString *out, const char *s, gsize len, query_value_t kind)
{
    if (s == NULL)
        g_string_append (out, "null");
    else if (kind == QUERY_VALUE_BOOL)
        g_string_append (out, query_bool_true (s, len) ? "true" : "false");
    else if ((kind == QUERY_VALUE_NUMBER && query_json_number (s, len)) || kind == QUERY_VALUE_JSON)
        g_string_append_len (out, s, (gssize) len);
    else
        // a NaN of numeric is a string of JSON
        query_json_string (out, s, len);
}

/* --------------------------------------------------------------------------------------------- */

/* A value of SQL, by its kind */
static void
query_sql_value (GString *out, const char *s, gsize len, query_value_t kind)
{
    if (s == NULL)
        g_string_append (out, "NULL");
    else if (kind == QUERY_VALUE_BOOL)
        g_string_append (out, query_bool_true (s, len) ? "TRUE" : "FALSE");
    else if (kind == QUERY_VALUE_NUMBER && query_json_number (s, len))
        g_string_append_len (out, s, (gssize) len);
    else
        // 'NaN' and 'Infinity' of numeric and of float are strings, as the server takes them
        query_sql_string (out, s, len);
}

/* --------------------------------------------------------------------------------------------- */

/* What a value of a document of mongo is: its text, @len bytes, and its kind; NULL for null */
static char *
query_document_value (JsonNode *node, gsize *len, query_value_t *kind)
{
    char *s = NULL;

    *kind = QUERY_VALUE_TEXT;
    switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_NULL:
        break;
    case JSON_NODE_VALUE:
    {
        const GType type = json_node_get_value_type (node);

        if (type == G_TYPE_STRING)
            s = g_strdup (json_node_get_string (node));
        else if (type == G_TYPE_INT64)
        {
            s = g_strdup_printf ("%" G_GINT64_FORMAT, json_node_get_int (node));
            *kind = QUERY_VALUE_NUMBER;
        }
        else if (type == G_TYPE_DOUBLE)
        {
            char buf[G_ASCII_DTOSTR_BUF_SIZE];

            s = g_strdup (g_ascii_dtostr (buf, sizeof (buf), json_node_get_double (node)));
            *kind = QUERY_VALUE_NUMBER;
        }
        else if (type == G_TYPE_BOOLEAN)
        {
            s = g_strdup (json_node_get_boolean (node) ? "true" : "false");
            *kind = QUERY_VALUE_BOOL;
        }
        break;
    }
    case JSON_NODE_OBJECT:
    {
        JsonObject *o = json_node_get_object (node);
        static const char *const numbers[] = { "$numberLong", "$numberDecimal", "$numberDouble",
                                               "$numberInt" };
        static const char *const texts[] = { "$oid", "$symbol" };
        guint i;

        // the types of Extended JSON of one value, by that value: an ObjectId by its hex
        if (json_object_get_size (o) == 1)
        {
            for (i = 0; s == NULL && i < G_N_ELEMENTS (numbers); i++)
                if (json_object_has_member (o, numbers[i])
                    && JSON_NODE_HOLDS_VALUE (json_object_get_member (o, numbers[i]))
                    && json_object_get_string_member (o, numbers[i]) != NULL)
                {
                    s = g_strdup (json_object_get_string_member (o, numbers[i]));
                    *kind = QUERY_VALUE_NUMBER;
                }
            for (i = 0; s == NULL && i < G_N_ELEMENTS (texts); i++)
                if (json_object_has_member (o, texts[i])
                    && JSON_NODE_HOLDS_VALUE (json_object_get_member (o, texts[i]))
                    && json_object_get_string_member (o, texts[i]) != NULL)
                    s = g_strdup (json_object_get_string_member (o, texts[i]));
            // a date of relaxed Extended JSON is an ISO string; one out of its range, a number
            if (s == NULL && json_object_has_member (o, "$date"))
            {
                JsonNode *date = json_object_get_member (o, "$date");

                if (JSON_NODE_HOLDS_VALUE (date)
                    && json_node_get_value_type (date) == G_TYPE_STRING)
                    s = g_strdup (json_node_get_string (date));
            }
        }
        if (s != NULL)
            break;
    }
        MC_FALLTHROUGH;
    case JSON_NODE_ARRAY:
        // an object or an array as its JSON, on one line
        s = json_to_string (node, FALSE);
        *kind = QUERY_VALUE_JSON;
        break;
    default:
        break;
    }
    *len = s != NULL ? strlen (s) : 0;
    return s;
}

/* --------------------------------------------------------------------------------------------- */

/* The columns of the documents of the sample: their keys, a key once, _id first; "value" for
   what is no document */
static void
query_sample_columns (query_export_t *x)
{
    GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
    GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
    guint i;

    for (i = 0; i < x->sample->len; i++)
    {
        JsonNode *node = g_ptr_array_index (x->sample, i);
        GList *members, *m;

        if (!JSON_NODE_HOLDS_OBJECT (node))
        {
            if (!g_hash_table_contains (seen, "value"))
            {
                g_hash_table_add (seen, (gpointer) "value");
                g_ptr_array_add (names, g_strdup ("value"));
            }
            continue;
        }
        members = json_object_get_members (json_node_get_object (node));
        for (m = members; m != NULL; m = m->next)
            if (!g_hash_table_contains (seen, m->data))
            {
                char *name = g_strdup (m->data);

                g_hash_table_add (seen, name);
                if (strcmp (name, "_id") == 0)
                    g_ptr_array_insert (names, 0, name);
                else
                    g_ptr_array_add (names, name);
            }
        g_list_free (members);
    }
    g_hash_table_destroy (seen);
    query_export_columns (x, names);
    g_ptr_array_free (names, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* A document as a row of the columns; its keys of no column told by query_export_skipped () */
static void
query_document_row (query_export_t *x, JsonNode *node)
{
    const guint n = x->columns->len;
    char **values = g_new0 (char *, n + 1);
    gsize *lens = g_new0 (gsize, n + 1);
    query_value_t *kinds = g_new0 (query_value_t, n + 1);
    JsonObject *o = JSON_NODE_HOLDS_OBJECT (node) ? json_node_get_object (node) : NULL;
    guint c;

    for (c = 0; c < n; c++)
    {
        const char *name = g_ptr_array_index (x->columns, c);
        JsonNode *value = o != NULL       ? json_object_get_member (o, name)
            : strcmp (name, "value") == 0 ? node
                                          : NULL;

        // a key the document has not is NULL
        if (value != NULL)
            values[c] = query_document_value (value, &lens[c], &kinds[c]);
    }
    if (o != NULL)
    {
        GList *members = json_object_get_members (o), *m;

        for (m = members; m != NULL; m = m->next)
            if (!g_hash_table_contains (x->names, m->data))
            {
                if (x->skipped == NULL)
                    x->skipped = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
                g_hash_table_add (x->skipped, g_strdup (m->data));
            }
        g_list_free (members);
    }
    query_export_row (x, (const char *const *) values, lens, kinds);
    for (c = 0; c < n; c++)
        g_free (values[c]);
    g_free (values);
    g_free (lens);
    g_free (kinds);
}

/* --------------------------------------------------------------------------------------------- */

/* The documents of the sample as rows, their columns made of them */
static void
query_sample_flush (query_export_t *x)
{
    guint i;

    if (x->sample == NULL)
        return;
    query_sample_columns (x);
    for (i = 0; i < x->sample->len; i++)
        query_document_row (x, g_ptr_array_index (x->sample, i));
    g_ptr_array_free (x->sample, TRUE);
    x->sample = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static int
query_name_cmp (gconstpointer a, gconstpointer b)
{
    return strcmp (*(const char *const *) a, *(const char *const *) b);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const char *
query_export_extension (query_export_format_t format)
{
    return format < QUERY_EXPORT_FORMATS ? query_export_extensions[format] : "txt";
}

/* --------------------------------------------------------------------------------------------- */

query_export_t *
query_export_new (const query_export_options_t *options)
{
    query_export_t *x = g_new0 (query_export_t, 1);

    x->o = *options;
    x->o.table = query_sql_table (options->table != NULL && *options->table != '\0' ? options->table
                                                                                    : "export");
    if (x->o.delimiter == '\0')
        x->o.delimiter = ',';
    if (x->o.per_insert < 1)
        x->o.per_insert = 1;
    x->out = g_string_new (NULL);
    return x;
}

/* --------------------------------------------------------------------------------------------- */

void
query_export_free (query_export_t *x)
{
    if (x == NULL)
        return;
    g_free (x->o.table);
    g_string_free (x->out, TRUE);
    if (x->names != NULL)
        g_hash_table_destroy (x->names);
    if (x->columns != NULL)
        g_ptr_array_free (x->columns, TRUE);
    if (x->types != NULL)
        g_ptr_array_free (x->types, TRUE);
    if (x->sample != NULL)
        g_ptr_array_free (x->sample, TRUE);
    if (x->skipped != NULL)
        g_hash_table_destroy (x->skipped);
    g_free (x);
}

/* --------------------------------------------------------------------------------------------- */

void
query_export_types (query_export_t *x, GPtrArray *types)
{
    guint c;

    if (x->types != NULL)
        return;
    x->types = g_ptr_array_new_with_free_func (g_free);
    for (c = 0; c < types->len; c++)
        g_ptr_array_add (x->types, g_strdup (g_ptr_array_index (types, c)));
}

/* --------------------------------------------------------------------------------------------- */

void
query_export_columns (query_export_t *x, GPtrArray *names)
{
    guint c;

    if (x->columns != NULL)
        return;
    x->columns = g_ptr_array_new_with_free_func (g_free);
    x->names = g_hash_table_new (g_str_hash, g_str_equal);
    for (c = 0; c < names->len; c++)
    {
        const char *name = g_ptr_array_index (names, c);
        char *unique = g_strdup (name);
        int i;

        /* a name once: "id" of two tables of a join is "id" and "id_2", a column of INSERT and a
           key of JSON once */
        for (i = 2; g_hash_table_contains (x->names, unique); i++)
        {
            g_free (unique);
            unique = g_strdup_printf ("%s_%d", name, i);
        }
        g_ptr_array_add (x->columns, unique);
        g_hash_table_add (x->names, unique);
    }
    query_export_head (x);
}

/* --------------------------------------------------------------------------------------------- */

void
query_export_row (query_export_t *x, const char *const *values, const gsize *lens,
                  const query_value_t *kinds)
{
    GString *out = x->out;
    const guint n = x->columns != NULL ? x->columns->len : 0;
    guint c;

    switch (x->o.format)
    {
    case QUERY_EXPORT_CSV:
        for (c = 0; c < n; c++)
        {
            if (c > 0)
                g_string_append_c (out, x->o.delimiter);
            if (values[c] == NULL)
                g_string_append (out, x->o.null_word ? "NULL" : "");
            else if (kinds[c] == QUERY_VALUE_BOOL)
                query_csv_value (x, out, query_bool_true (values[c], lens[c]) ? "true" : "false",
                                 query_bool_true (values[c], lens[c]) ? 4 : 5);
            else
                query_csv_value (x, out, values[c], lens[c]);
        }
        g_string_append_c (out, '\n');
        break;
    case QUERY_EXPORT_JSON:
    case QUERY_EXPORT_NDJSON:
    {
        // an array of objects indented: a key a line
        const gboolean indent = x->o.format == QUERY_EXPORT_JSON && x->o.indent;

        if (x->o.format == QUERY_EXPORT_JSON)
            g_string_append (out, x->opened ? ",\n" : "[\n");
        x->opened = TRUE;
        g_string_append (out, indent ? "  {" : "{");
        for (c = 0; c < n; c++)
        {
            const char *name = g_ptr_array_index (x->columns, c);

            if (c > 0)
                g_string_append_c (out, ',');
            if (indent)
                g_string_append (out, "\n    ");
            query_json_string (out, name, strlen (name));
            g_string_append (out, indent ? ": " : ":");
            query_json_value (out, values[c], lens[c], kinds[c]);
        }
        g_string_append (out, indent && n > 0 ? "\n  }" : "}");
    }
        if (x->o.format == QUERY_EXPORT_NDJSON)
            g_string_append_c (out, '\n');
        break;
    case QUERY_EXPORT_SQL:
        // the rows of a statement after its start, separated by commas; the last one ends it
        if (x->rows % x->o.per_insert == 0)
        {
            query_sql_insert (x);
            g_string_append (out, x->o.per_insert == 1 ? " (" : "\n  (");
        }
        else
            g_string_append (out, ",\n  (");
        for (c = 0; c < n; c++)
        {
            if (c > 0)
                g_string_append (out, ", ");
            query_sql_value (out, values[c], lens[c], kinds[c]);
        }
        g_string_append_c (out, ')');
        if ((x->rows + 1) % x->o.per_insert == 0)
            g_string_append (out, ";\n");
        break;
    case QUERY_EXPORT_MARKDOWN:
        for (c = 0; c < n; c++)
        {
            g_string_append (out, "| ");
            if (values[c] == NULL)
                g_string_append (out, x->o.null_markdown ? "NULL" : "");
            else if (kinds[c] == QUERY_VALUE_BOOL)
                g_string_append (out, query_bool_true (values[c], lens[c]) ? "true" : "false");
            else
                query_md_value (out, values[c], lens[c]);
            g_string_append_c (out, ' ');
        }
        g_string_append (out, n > 0 ? "|\n" : "");
        break;
    default:
        break;
    }
    x->rows++;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_export_document (query_export_t *x, const char *json, GError **error)
{
    JsonParser *parser;
    JsonNode *root;

    // JSON as mongosh wrote it, after a check that it is some
    if (x->o.format == QUERY_EXPORT_JSON || x->o.format == QUERY_EXPORT_NDJSON)
    {
        parser = json_parser_new ();
        if (!json_parser_load_from_data (parser, json, -1, error))
        {
            g_object_unref (parser);
            return FALSE;
        }
        if (x->o.format == QUERY_EXPORT_JSON)
            g_string_append (x->out, x->opened ? ",\n" : "[\n");
        x->opened = TRUE;
        if (x->o.format == QUERY_EXPORT_JSON && x->o.indent
            && json_parser_get_root (parser) != NULL)
        {
            // a key a line, inside the array
            char *pretty = json_to_string (json_parser_get_root (parser), TRUE);
            char **lines = g_strsplit (pretty, "\n", -1);
            char **l;

            for (l = lines; *l != NULL; l++)
                g_string_append_printf (x->out, "%s  %s", l == lines ? "" : "\n", *l);
            g_strfreev (lines);
            g_free (pretty);
        }
        else
            g_string_append (x->out, json);
        g_object_unref (parser);
        if (x->o.format == QUERY_EXPORT_NDJSON)
            g_string_append_c (x->out, '\n');
        x->rows++;
        return TRUE;
    }

    parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, json, -1, error))
    {
        g_object_unref (parser);
        return FALSE;
    }
    root = json_parser_get_root (parser);
    // the document kept past its parser, not copied
    root = root != NULL ? json_node_ref (root) : json_node_new (JSON_NODE_NULL);
    g_object_unref (parser);

    if (x->columns == NULL)
    {
        if (x->sample == NULL)
            x->sample = g_ptr_array_new_with_free_func ((GDestroyNotify) json_node_unref);
        g_ptr_array_add (x->sample, root);
        if (x->sample->len >= QUERY_EXPORT_SAMPLE)
            query_sample_flush (x);
        return TRUE;
    }
    query_document_row (x, root);
    json_node_unref (root);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
query_export_end (query_export_t *x)
{
    query_sample_flush (x);
    switch (x->o.format)
    {
    case QUERY_EXPORT_JSON:
        g_string_append (x->out, x->opened ? "\n]\n" : "[]\n");
        break;
    case QUERY_EXPORT_SQL:
        // the last statement, not full
        if (x->rows % x->o.per_insert != 0)
            g_string_append (x->out, ";\n");
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

GString *
query_export_text (query_export_t *x)
{
    return x->out;
}

/* --------------------------------------------------------------------------------------------- */

gint64
query_export_rows (const query_export_t *x)
{
    return x->rows;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_export_skipped (const query_export_t *x)
{
    GPtrArray *names;
    GHashTableIter iter;
    gpointer key;
    char *list;

    if (x->skipped == NULL)
        return NULL;
    names = g_ptr_array_new ();
    g_hash_table_iter_init (&iter, x->skipped);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (names, key);
    g_ptr_array_sort (names, query_name_cmp);
    g_ptr_array_add (names, NULL);
    list = g_strjoinv (", ", (char **) names->pdata);
    g_ptr_array_free (names, TRUE);
    return list;
}

/* --------------------------------------------------------------------------------------------- */
