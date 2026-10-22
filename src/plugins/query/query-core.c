/*
   The query plugin: what it runs, and how it reads what the database says.

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

/** \file query-core.c
 *  \brief Source: what the query plugin runs, and how it reads what the database says
 *
 *  None of it needs the screen: the tests take it alone.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <json-glib/json-glib.h>

#include "lib/global.h"

#include "query-core.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* The widest a column of the table is; a longer value ends with "..." */
#define QUERY_COLUMN_MAX 40

#define QUERY_WARNING    "DeprecationWarning: DBQuery.shellBatchSize"

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
query_line_empty (const char *start, const char *end)
{
    for (; start < end; start++)
        if (!g_ascii_isspace (*start))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The end of the line that starts at @p: its '\n', or the end of the text */
static const char *
query_line_end (const char *p)
{
    const char *nl = strchr (p, '\n');

    return nl != NULL ? nl : p + strlen (p);
}

/* --------------------------------------------------------------------------------------------- */

/* How many columns of the terminal @s takes */
static int
query_width (const char *s)
{
    int width = 0;

    for (; *s != '\0'; s = g_utf8_next_char (s))
    {
        const gunichar c = g_utf8_get_char (s);

        if (g_unichar_iszerowidth (c))
            continue;
        width += g_unichar_iswide (c) ? 2 : 1;
    }
    return width;
}

/* --------------------------------------------------------------------------------------------- */

/* @s cut to @max columns, with "..." at the end when it was longer */
static char *
query_cut (const char *s, int max)
{
    GString *out;
    int width = 0;

    if (query_width (s) <= max)
        return g_strdup (s);
    out = g_string_new (NULL);
    for (; *s != '\0'; s = g_utf8_next_char (s))
    {
        const gunichar c = g_utf8_get_char (s);
        const int w = g_unichar_iszerowidth (c) ? 0 : g_unichar_iswide (c) ? 2 : 1;

        if (width + w > max - 3)
            break;
        g_string_append_unichar (out, c);
        width += w;
    }
    g_string_append (out, "...");
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* A string of the data in a cell: on one line, without the bytes that move the cursor, its '|'
   no end of the cell of Markdown */
static void
query_append_string (GString *out, const char *s)
{
    for (; *s != '\0'; s++)
        if (*s == '\n')
            g_string_append (out, "\\n");
        else if (*s == '|')
            g_string_append (out, "\\|");
        else if (*s == '\t' || *s == '\r')
            g_string_append_c (out, ' ');
        else if ((unsigned char) *s < 0x20 || *s == 0x7f)
            g_string_append_c (out, '?');
        else
            g_string_append_c (out, *s);
}

/* --------------------------------------------------------------------------------------------- */

/* The value of a cell: a scalar as it is, the types of Extended JSON by their value, another
   object or an array by its size */
static char *
query_cell (JsonNode *node)
{
    GString *out = g_string_new (NULL);

    switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_NULL:
        g_string_append (out, "null");
        break;
    case JSON_NODE_ARRAY:
        g_string_printf (out, "[%u]", json_array_get_length (json_node_get_array (node)));
        break;
    case JSON_NODE_VALUE:
    {
        const GType type = json_node_get_value_type (node);

        if (type == G_TYPE_STRING)
            query_append_string (out, json_node_get_string (node));
        else if (type == G_TYPE_INT64)
            g_string_printf (out, "%" G_GINT64_FORMAT, json_node_get_int (node));
        else if (type == G_TYPE_DOUBLE)
            g_string_printf (out, "%.15g", json_node_get_double (node));
        else if (type == G_TYPE_BOOLEAN)
            g_string_append (out, json_node_get_boolean (node) ? "true" : "false");
        break;
    }
    case JSON_NODE_OBJECT:
    {
        JsonObject *o = json_node_get_object (node);
        const guint size = json_object_get_size (o);
        static const char *const scalars[] = { "$oid",           "$date",         "$numberLong",
                                               "$numberDecimal", "$numberDouble", "$numberInt",
                                               "$symbol" };
        guint i;

        for (i = 0; size == 1 && i < G_N_ELEMENTS (scalars); i++)
            if (json_object_has_member (o, scalars[i]))
            {
                JsonNode *value = json_object_get_member (o, scalars[i]);
                char *text = query_cell (value);

                g_string_append (out, text);
                g_free (text);
                break;
            }
        if (out->len == 0 && (size != 1 || i == G_N_ELEMENTS (scalars)))
            g_string_printf (out, size == 1 ? "{%u key}" : "{%u keys}", size);
        break;
    }
    default:
        break;
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The columns of the row @node into @columns, a key once: those of a document, "value" for
   anything else */
static void
query_add_columns (JsonNode *node, GPtrArray *columns, GHashTable *seen)
{
    GList *members, *m;

    if (!JSON_NODE_HOLDS_OBJECT (node))
    {
        if (!g_hash_table_contains (seen, "value"))
        {
            g_hash_table_add (seen, g_strdup ("value"));
            g_ptr_array_add (columns, g_strdup ("value"));
        }
        return;
    }
    members = json_object_get_members (json_node_get_object (node));
    for (m = members; m != NULL; m = m->next)
        if (!g_hash_table_contains (seen, m->data))
        {
            g_hash_table_add (seen, g_strdup (m->data));
            // _id first, as mongosh shows it
            if (strcmp (m->data, "_id") == 0)
                g_ptr_array_insert (columns, 0, g_strdup (m->data));
            else
                g_ptr_array_add (columns, g_strdup (m->data));
        }
    g_list_free (members);
}

/* --------------------------------------------------------------------------------------------- */

/* The cell of the column @column of the row @node; "" when the document has no such key */
static char *
query_row_cell (JsonNode *node, const char *column)
{
    if (JSON_NODE_HOLDS_OBJECT (node))
    {
        JsonObject *o = json_node_get_object (node);

        return json_object_has_member (o, column) ? query_cell (json_object_get_member (o, column))
                                                  : g_strdup ("");
    }
    return strcmp (column, "value") == 0 ? query_cell (node) : g_strdup ("");
}

/* --------------------------------------------------------------------------------------------- */

static void
query_append_padded (GString *out, const char *cell, int width)
{
    int pad = width - query_width (cell);

    g_string_append (out, cell);
    for (; pad > 0; pad--)
        g_string_append_c (out, ' ');
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

char *
query_block (const char *text, long line, long *first)
{
    const char *p = text, *start, *end;
    long n;
    GPtrArray *starts = g_ptr_array_new ();
    guint at, i;

    // the starts of the lines
    while (TRUE)
    {
        g_ptr_array_add (starts, (gpointer) p);
        p = query_line_end (p);
        if (*p == '\0')
            break;
        p++;
    }
    if (line < 0 || line >= (long) starts->len)
    {
        g_ptr_array_free (starts, TRUE);
        return NULL;
    }
    at = (guint) line;
    start = g_ptr_array_index (starts, at);
    if (query_line_empty (start, query_line_end (start)))
    {
        g_ptr_array_free (starts, TRUE);
        return NULL;
    }
    for (i = at; i > 0; i--)
    {
        const char *s = g_ptr_array_index (starts, i - 1);

        if (query_line_empty (s, query_line_end (s)))
            break;
    }
    *first = i;
    start = g_ptr_array_index (starts, i);
    for (n = at; n + 1 < (long) starts->len; n++)
    {
        const char *s = g_ptr_array_index (starts, n + 1);

        if (query_line_empty (s, query_line_end (s)))
            break;
    }
    end = query_line_end (g_ptr_array_index (starts, n));
    g_ptr_array_free (starts, TRUE);
    return g_strndup (start, (gsize) (end - start));
}

/* --------------------------------------------------------------------------------------------- */

long
query_line_of (const char *text, gsize offset)
{
    long line = 0;
    gsize i;

    for (i = 0; i < offset && text[i] != '\0'; i++)
        if (text[i] == '\n')
            line++;
    return line;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_line_blank (const char *text, long line)
{
    const char *p = text;

    for (; line > 0 && p != NULL; line--)
    {
        p = strchr (p, '\n');
        if (p != NULL)
            p++;
    }
    for (; p != NULL && *p != '\0' && *p != '\n'; p++)
        if (!g_ascii_isspace (*p))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

const char *
query_mongo_prefix (void)
{
    return "DBQuery.shellBatchSize = 1000; db = connect(process.env.MC_MONGO_URI); "
           "if (process.env.MC_MONGO_DB) db = db.getSiblingDB(process.env.MC_MONGO_DB);";
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_wants_table (const char *text)
{
    const char *p = text;

    while (*p != '\0')
    {
        const char *q = p;

        while (*q == ' ' || *q == '\t')
            q++;
        if (q[0] == '/' && q[1] == '/')
        {
            for (q += 2; *q == ' ' || *q == '\t'; q++)
                ;
            if (g_str_has_prefix (q, QUERY_TABLE_MARK)
                && !g_ascii_isalnum (q[strlen (QUERY_TABLE_MARK)]))
                return TRUE;
        }
        p = query_line_end (p);
        if (*p == '\n')
            p++;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_for_table (const char *text)
{
    static const char *const cursor[] = {
        "find",         "aggregate",
        "sort",         "limit",
        "skip",         "project",
        "batchSize",    "hint",
        "collation",    "maxTimeMS",
        "allowDiskUse", "comment",
        "readPref",     "min",
        "max",          "returnKey",
        "showRecordId", "noCursorTimeout",
        "readConcern",  "allowPartialResults",
    };
    GArray *opens = g_array_new (FALSE, FALSE, sizeof (const char *));
    const char *p, *name_end;
    const char *end = NULL;   // past the last character of the code, not of a comment
    const char *open = NULL;  // the '(' of the ')' the code ends with
    guint i;

    /* Through the text as JavaScript is read: the parentheses of strings and of comments are no
       part of the code, nor what follows the end of it, a comment or a ';' */
    for (p = text; *p != '\0'; p++)
    {
        if (p[0] == '/' && p[1] == '/')
        {
            p = query_line_end (p);
            if (*p == '\0')
                break;
        }
        else if (p[0] == '/' && p[1] == '*')
        {
            const char *close = strstr (p + 2, "*/");

            if (close == NULL)
                break;
            p = close + 1;
        }
        else if (*p == '"' || *p == '\'' || *p == '`')
        {
            const char quote = *p;

            for (p++; *p != '\0' && *p != quote; p++)
                if (*p == '\\' && p[1] != '\0')
                    p++;
            if (*p == '\0')
                break;
            end = p + 1;
            open = NULL;
        }
        else if (!g_ascii_isspace (*p) && *p != ';')
        {
            end = p + 1;
            open = NULL;
            if (*p == '(')
                g_array_append_val (opens, p);
            else if (*p == ')' && opens->len > 0)
            {
                open = g_array_index (opens, const char *, opens->len - 1);
                g_array_set_size (opens, opens->len - 1);
            }
        }
    }
    g_array_free (opens, TRUE);
    if (open == NULL)
        return g_strdup (text);

    // the name before the '(', blanks maybe between
    for (p = open; p > text && g_ascii_isspace (p[-1]); p--)
        ;
    for (name_end = p; p > text && (g_ascii_isalnum (p[-1]) || p[-1] == '_'); p--)
        ;
    for (i = 0; i < G_N_ELEMENTS (cursor); i++)
        if ((gsize) (name_end - p) == strlen (cursor[i])
            && strncmp (p, cursor[i], (gsize) (name_end - p)) == 0)
        {
            char *body = g_strndup (text, (gsize) (end - text));
            // on a line of its own, the comment and the ';' after the call left out
            char *array = g_strconcat (body, "\n.toArray()", (char *) NULL);

            g_free (body);
            return array;
        }
    return g_strdup (text);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_clean (const char *out)
{
    if (g_str_has_prefix (out, QUERY_WARNING))
    {
        const char *nl = strchr (out, '\n');

        return g_strdup (nl != NULL ? nl + 1 : "");
    }
    return g_strdup (out);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_mongo_error_position (const char *err, long *line, long *column)
{
    const char *p = err;

    while (*p != '\0')
    {
        const char *end = query_line_end (p);

        if (g_str_has_prefix (p, "SyntaxError: ") && end - p > 4 && end[-1] == ')')
        {
            const char *open = end - 1;

            while (open > p && *open != '(')
                open--;
            if (*open == '(' && g_ascii_isdigit (open[1]))
            {
                char *colon;

                *line = strtol (open + 1, &colon, 10);
                if (*colon == ':' && g_ascii_isdigit (colon[1]))
                {
                    *column = strtol (colon + 1, NULL, 10);
                    return TRUE;
                }
            }
        }
        p = *end == '\n' ? end + 1 : end;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_mongo_is_cursor (const char *out)
{
    return strstr (out, "Converting circular structure to EJSON") != NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
query_mongo_json_tail (const char *out)
{
    JsonParser *parser = json_parser_new ();
    GPtrArray *starts = g_ptr_array_new ();
    const char *p = out, *found = NULL;
    guint i;

    // the lines that start at the first column: mongosh indents what is inside the value
    while (*p != '\0')
    {
        if (strchr ("[{\"-0123456789tfn", *p) != NULL)
            g_ptr_array_add (starts, (gpointer) p);
        p = query_line_end (p);
        if (*p == '\n')
            p++;
    }
    for (i = starts->len; i > 0 && found == NULL; i--)
    {
        const char *start = g_ptr_array_index (starts, i - 1);

        if (json_parser_load_from_data (parser, start, -1, NULL))
            found = start;
    }
    g_ptr_array_free (starts, TRUE);
    g_object_unref (parser);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_json_error (const char *json)
{
    JsonParser *parser;
    char *message = NULL;

    if (json == NULL)
        return NULL;
    parser = json_parser_new ();
    if (json_parser_load_from_data (parser, json, -1, NULL)
        && JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
    {
        JsonObject *o = json_node_get_object (json_parser_get_root (parser));

        // a SyntaxError and a MongoServerError tell more: where, the code of the server
        if (json_object_has_member (o, "message") && json_object_has_member (o, "name")
            && json_object_has_member (o, "stack")
            && JSON_NODE_HOLDS_VALUE (json_object_get_member (o, "message"))
            && JSON_NODE_HOLDS_VALUE (json_object_get_member (o, "name")))
            message = g_strdup_printf ("%s: %s", json_object_get_string_member (o, "name"),
                                       json_object_get_string_member (o, "message"));
    }
    g_object_unref (parser);
    return message;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_endpoint (const char *uri, const char *db)
{
    const char *hosts, *at, *end;
    char *server, *path = NULL, *endpoint;

    if (uri == NULL)
        return g_strdup (db != NULL ? db : "");
    hosts = strstr (uri, "://");
    hosts = hosts != NULL ? hosts + 3 : uri;
    end = hosts + strcspn (hosts, "/?");
    // the user and the password: before the last '@' of the hosts
    for (at = end; at > hosts && at[-1] != '@'; at--)
        ;
    server = g_strndup (at, (gsize) (end - at));
    if (db == NULL || *db == '\0')
    {
        if (*end == '/')
            path = g_strndup (end + 1, strcspn (end + 1, "?"));
        db = path;
    }
    endpoint =
        db != NULL && *db != '\0' ? g_strdup_printf ("%s/%s", server, db) : g_strdup (server);
    g_free (path);
    g_free (server);
    return endpoint;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_table (const char *json, guint *rows, GError **error)
{
    JsonParser *parser = json_parser_new ();
    JsonNode *root;
    GPtrArray *nodes, *columns, *cells;
    GHashTable *seen;
    GString *out;
    int *widths;
    guint r, c;

    *rows = 0;
    if (!json_parser_load_from_data (parser, json, -1, error))
    {
        g_object_unref (parser);
        return NULL;
    }
    root = json_parser_get_root (parser);
    nodes = g_ptr_array_new ();
    if (root == NULL)
        ;
    else if (JSON_NODE_HOLDS_ARRAY (root))
    {
        JsonArray *a = json_node_get_array (root);

        for (r = 0; r < json_array_get_length (a); r++)
            g_ptr_array_add (nodes, json_array_get_element (a, r));
    }
    else
        g_ptr_array_add (nodes, root);

    columns = g_ptr_array_new_with_free_func (g_free);
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    for (r = 0; r < nodes->len; r++)
        query_add_columns (g_ptr_array_index (nodes, r), columns, seen);
    g_hash_table_destroy (seen);

    // the cells, cut, and the width of each column
    widths = g_new0 (int, columns->len);
    cells = g_ptr_array_new_with_free_func (g_free);
    for (c = 0; c < columns->len; c++)
    {
        GString *key = g_string_new (NULL);
        char *name;

        query_append_string (key, g_ptr_array_index (columns, c));
        name = query_cut (key->str, QUERY_COLUMN_MAX);
        g_string_free (key, TRUE);
        // a column of Markdown is three dashes at least
        widths[c] = MAX (3, query_width (name));
        g_ptr_array_add (cells, name);
    }
    for (r = 0; r < nodes->len; r++)
        for (c = 0; c < columns->len; c++)
        {
            char *cell =
                query_row_cell (g_ptr_array_index (nodes, r), g_ptr_array_index (columns, c));
            char *cut = query_cut (cell, QUERY_COLUMN_MAX);

            widths[c] = MAX (widths[c], query_width (cut));
            g_ptr_array_add (cells, cut);
            g_free (cell);
        }

    out = g_string_new (NULL);
    // a table of Markdown, its columns lined up to be read as they are
    for (r = 0; r <= nodes->len && columns->len > 0; r++)
    {
        for (c = 0; c < columns->len; c++)
        {
            g_string_append (out, "| ");
            query_append_padded (out, g_ptr_array_index (cells, r * columns->len + c), widths[c]);
            g_string_append_c (out, ' ');
        }
        g_string_append (out, "|\n");
        // under the names of the columns
        if (r == 0)
        {
            for (c = 0; c < columns->len; c++)
            {
                int i;

                g_string_append_c (out, '|');
                for (i = 0; i < widths[c] + 2; i++)
                    g_string_append_c (out, '-');
            }
            g_string_append (out, "|\n");
        }
    }
    *rows = nodes->len;

    g_free (widths);
    g_ptr_array_free (cells, TRUE);
    g_ptr_array_free (columns, TRUE);
    g_ptr_array_free (nodes, TRUE);
    g_object_unref (parser);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
