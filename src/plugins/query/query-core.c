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

/* A call of a chain: its name, and its parentheses */
typedef struct
{
    const char *name;
    gsize len;
    const char *open;
    const char *close;
} query_call_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
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

/* The value of a type of Extended JSON, {"$oid": "..."} and the like, into @out; FALSE when
   @o is none */
static gboolean
query_ejson_scalar (JsonObject *o, GString *out)
{
    static const char *const scalars[] = { "$oid",           "$date",         "$numberLong",
                                           "$numberDecimal", "$numberDouble", "$numberInt",
                                           "$symbol" };
    guint i;

    if (json_object_get_size (o) != 1)
        return FALSE;
    for (i = 0; i < G_N_ELEMENTS (scalars); i++)
        if (json_object_has_member (o, scalars[i]))
        {
            JsonNode *value = json_object_get_member (o, scalars[i]);

            if (JSON_NODE_HOLDS_VALUE (value) && json_node_get_value_type (value) == G_TYPE_STRING)
                g_string_append (out, json_node_get_string (value));
            else if (JSON_NODE_HOLDS_VALUE (value)
                     && json_node_get_value_type (value) == G_TYPE_INT64)
                g_string_append_printf (out, "%" G_GINT64_FORMAT, json_node_get_int (value));
            else if (JSON_NODE_HOLDS_VALUE (value))
                g_string_append_printf (out, "%.15g", json_node_get_double (value));
            else
                return FALSE;
            return TRUE;
        }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* @node as text into @out, as mongosh shows it, the keys without quotes: a string inside an
   object or an array in quotes, the types of Extended JSON by their value; no more than a cell
   shows of it */
static void
query_value_text (JsonNode *node, gboolean inside, GString *out)
{
    // a cell is cut at QUERY_COLUMN_MAX: more of a big value is no use
    if (out->len > 4 * QUERY_COLUMN_MAX)
        return;
    switch (JSON_NODE_TYPE (node))
    {
    case JSON_NODE_NULL:
        g_string_append (out, "null");
        break;
    case JSON_NODE_ARRAY:
    {
        JsonArray *a = json_node_get_array (node);
        guint i;

        g_string_append_c (out, '[');
        for (i = 0; i < json_array_get_length (a) && out->len <= 4 * QUERY_COLUMN_MAX; i++)
        {
            if (i > 0)
                g_string_append (out, ", ");
            query_value_text (json_array_get_element (a, i), TRUE, out);
        }
        g_string_append_c (out, ']');
        break;
    }
    case JSON_NODE_VALUE:
    {
        const GType type = json_node_get_value_type (node);

        if (type == G_TYPE_STRING && inside)
            g_string_append_printf (out, "\"%s\"", json_node_get_string (node));
        else if (type == G_TYPE_STRING)
            g_string_append (out, json_node_get_string (node));
        else if (type == G_TYPE_INT64)
            g_string_append_printf (out, "%" G_GINT64_FORMAT, json_node_get_int (node));
        else if (type == G_TYPE_DOUBLE)
            g_string_append_printf (out, "%.15g", json_node_get_double (node));
        else if (type == G_TYPE_BOOLEAN)
            g_string_append (out, json_node_get_boolean (node) ? "true" : "false");
        break;
    }
    case JSON_NODE_OBJECT:
    {
        JsonObject *o = json_node_get_object (node);
        GList *names, *l;

        if (query_ejson_scalar (o, out))
            break;
        g_string_append_c (out, '{');
        names = json_object_get_members (o);
        for (l = names; l != NULL && out->len <= 4 * QUERY_COLUMN_MAX; l = l->next)
        {
            if (l != names)
                g_string_append (out, ", ");
            g_string_append_printf (out, "%s: ", (const char *) l->data);
            query_value_text (json_object_get_member (o, l->data), TRUE, out);
        }
        g_list_free (names);
        g_string_append_c (out, '}');
        break;
    }
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The value of a cell: a scalar as it is, the types of Extended JSON by their value, an object
   or an array as mongosh shows it, { ClientUuid: "hashed" } */
static char *
query_cell (JsonNode *node)
{
    GString *raw = g_string_new (NULL);
    GString *out = g_string_new (NULL);

    query_value_text (node, FALSE, raw);
    // a | of it is no end of the cell, a newline no end of the row
    query_append_string (out, raw->str);
    g_string_free (raw, TRUE);
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
query_line_separator (const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    if (p[0] != '/' || p[1] != '/')
        return FALSE;
    for (p += 2; *p == ' ' || *p == '\t'; p++)
        ;
    return strncmp (p, "---", 3) == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The starts of the lines of @text; the end of the text after a '\n' at its end is one too */
static GPtrArray *
query_line_starts (const char *text)
{
    GPtrArray *starts = g_ptr_array_new ();
    const char *p = text;

    while (TRUE)
    {
        g_ptr_array_add (starts, (gpointer) p);
        p = query_line_end (p);
        if (*p == '\0')
            break;
        p++;
    }
    return starts;
}

/* --------------------------------------------------------------------------------------------- */

/* The section of the 0-based @line: its lines from @start up to @end, not with it; FALSE when
   the text has no separator, the whole text being one */
static gboolean
query_section_of (GPtrArray *starts, long line, guint *start, guint *end)
{
    gboolean any = FALSE;
    guint i;

    line = CLAMP (line, 0, (long) starts->len - 1);
    *start = 0;
    *end = starts->len;
    for (i = 0; i < starts->len; i++)
        if (query_line_separator (g_ptr_array_index (starts, i)))
        {
            any = TRUE;
            // the separator heads the section after it
            if ((long) i <= line)
                *start = i + 1;
            else if (*end == starts->len)
                *end = i;
        }
    return any;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_section (const char *text, long line, long *first)
{
    GPtrArray *starts = query_line_starts (text);
    guint start, end;
    char *section = NULL;

    *first = 0;
    if (query_section_of (starts, line, &start, &end))
    {
        const char *from =
            start < starts->len ? g_ptr_array_index (starts, start) : text + strlen (text);
        const char *to = end < starts->len ? (const char *) g_ptr_array_index (starts, end)
                                           : text + strlen (text);

        section = g_strndup (from, (gsize) MAX (0, to - from));
        *first = (long) start;
    }
    g_ptr_array_free (starts, TRUE);
    return section;
}

/* --------------------------------------------------------------------------------------------- */

const char *
query_mongo_prefix (void)
{
    /* printjson () and print () of an array or a cursor also print a copy of it as Extended JSON
       on a line of its own, QUERY_TABLE_LINE before it, for the tab Table: what they show is as
       it was; a cursor is made the array of its documents first, which it is read as once */
    return "DBQuery.shellBatchSize = 1000; db = connect(process.env.MC_MONGO_URI); "
           "if (process.env.MC_MONGO_DB) db = db.getSiblingDB(process.env.MC_MONGO_DB); "
           "{ const __p = print, __pj = printjson; "
           "const __t = (v) => (v && !Array.isArray(v) && typeof v.toArray === 'function') "
           "? v.toArray() : v; "
           "const __e = (v) => { if (Array.isArray(v)) "
           "__p('\\x1e" QUERY_TABLE_TAG " ' + EJSON.stringify(v, { relaxed: true })); }; "
           "globalThis.printjson = (x, ...r) => { const v = __t(x); __pj(v, ...r); __e(v); }; "
           "globalThis.print = (...a) => { if (a.length === 1) { const v = __t(a[0]); "
           "if (Array.isArray(v)) { __p(v); __e(v); return; } } __p(...a); }; }";
}

/* --------------------------------------------------------------------------------------------- */

/* The line "// @name" of @text: what follows the name on it; NULL when there is none */
static const char *
query_annotation (const char *text, const char *name)
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
            if (*q == '@' && strncmp (q + 1, name, strlen (name)) == 0
                && !g_ascii_isalnum (q[1 + strlen (name)]))
                return q + 1 + strlen (name);
        }
        p = query_line_end (p);
        if (*p == '\n')
            p++;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_wants_table (const char *text)
{
    return query_annotation (text, QUERY_TABLE_MARK) != NULL;
}

/* --------------------------------------------------------------------------------------------- */

long
query_wants_limit (const char *text)
{
    const char *p = query_annotation (text, QUERY_LIMIT_MARK);
    long n;

    if (p == NULL)
        return 0;
    while (*p == ' ' || *p == '\t')
        p++;
    if (!g_ascii_isdigit (*p))
        return QUERY_LIMIT_DEFAULT;
    n = strtol (p, NULL, 10);
    return n > 0 ? n : QUERY_LIMIT_DEFAULT;
}

/* --------------------------------------------------------------------------------------------- */

/* What a cursor is made with and sorted, cut and told with: a call of them a query ends with is
   of a cursor */
static const char *const query_cursor_calls[] = {
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

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_is_cursor_call (const query_call_t *call)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS (query_cursor_calls); i++)
        if (call->len == strlen (query_cursor_calls[i])
            && strncmp (call->name, query_cursor_calls[i], call->len) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
query_call_is (const query_call_t *call, const char *name)
{
    return call->len == strlen (name) && strncmp (call->name, name, call->len) == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The chain of calls @text ends with, the last one first: "a.find(x).sort(y)" is sort, find.
   @end is past the last character of the code: a comment and a ';' after it are no part of it.
   An empty chain when the code ends with no call. */
static GArray *
query_chain (const char *text, const char **end)
{
    GArray *chain = g_array_new (FALSE, FALSE, sizeof (query_call_t));
    GArray *opens = g_array_new (FALSE, FALSE, sizeof (const char *));
    GArray *pairs = g_array_new (FALSE, FALSE, sizeof (const char *));  // '(' and ')', by two
    const char *p, *open = NULL, *close = NULL;

    *end = text;
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
            const char *c = strstr (p + 2, "*/");

            if (c == NULL)
                break;
            p = c + 1;
        }
        else if (*p == '"' || *p == '\'' || *p == '`')
        {
            const char quote = *p;

            for (p++; *p != '\0' && *p != quote; p++)
                if (*p == '\\' && p[1] != '\0')
                    p++;
            if (*p == '\0')
                break;
            *end = p + 1;
            open = NULL;
        }
        else if (!g_ascii_isspace (*p) && *p != ';')
        {
            *end = p + 1;
            open = NULL;
            if (*p == '(')
                g_array_append_val (opens, p);
            else if (*p == ')' && opens->len > 0)
            {
                open = g_array_index (opens, const char *, opens->len - 1);
                close = p;
                g_array_set_size (opens, opens->len - 1);
                g_array_append_val (pairs, open);
                g_array_append_val (pairs, close);
            }
        }
    }

    // back through the chain: the name before the '(', and a '.' and a call before the name
    while (open != NULL)
    {
        query_call_t call;
        guint i;

        for (p = open; p > text && g_ascii_isspace (p[-1]); p--)
            ;
        call.len = 0;
        for (; p > text && (g_ascii_isalnum (p[-1]) || p[-1] == '_' || p[-1] == '$'); p--)
            call.len++;
        if (call.len == 0)
            break;
        call.name = p;
        call.open = open;
        call.close = close;
        g_array_append_val (chain, call);

        for (; p > text && g_ascii_isspace (p[-1]); p--)
            ;
        if (p == text || p[-1] != '.')
            break;
        for (p--; p > text && g_ascii_isspace (p[-1]); p--)
            ;
        open = NULL;
        for (i = 0; p > text && p[-1] == ')' && i < pairs->len; i += 2)
            if (g_array_index (pairs, const char *, i + 1) == p - 1)
            {
                open = g_array_index (pairs, const char *, i);
                close = p - 1;
            }
    }
    g_array_free (opens, TRUE);
    g_array_free (pairs, TRUE);
    return chain;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_for_table (const char *text)
{
    const char *end;
    GArray *chain = query_chain (text, &end);
    char *array = NULL;

    if (chain->len > 0 && query_is_cursor_call (&g_array_index (chain, query_call_t, 0)))
    {
        char *body = g_strndup (text, (gsize) (end - text));

        // on a line of its own, the comment and the ';' after the call left out
        array = g_strconcat (body, "\n.toArray()", (char *) NULL);
        g_free (body);
    }
    g_array_free (chain, TRUE);
    return array != NULL ? array : g_strdup (text);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the text from @from up to @to has @word */
static gboolean
query_text_has (const char *from, const char *to, const char *word)
{
    const gsize len = strlen (word);
    const char *p;

    for (p = from; p + len <= to; p++)
        if (strncmp (p, word, len) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The last character of code from @from up to @to, not of a comment, not a blank: the quote
   that ends a string is one; NULL when there is none */
static const char *
query_last_code (const char *from, const char *to)
{
    const char *p, *last = NULL;

    for (p = from; p < to; p++)
    {
        if (p[0] == '/' && p + 1 < to && p[1] == '/')
        {
            while (p < to && *p != '\n')
                p++;
        }
        else if (p[0] == '/' && p + 1 < to && p[1] == '*')
        {
            for (p += 2; p + 1 < to && !(p[0] == '*' && p[1] == '/'); p++)
                ;
            p++;
        }
        else if (*p == '"' || *p == '\'' || *p == '`')
        {
            const char quote = *p;

            for (p++; p < to && *p != quote; p++)
                if (*p == '\\' && p + 1 < to)
                    p++;
            last = p < to ? p : last;
        }
        else if (!g_ascii_isspace (*p))
            last = p;
    }
    return last;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_use_before (const char *text, long line)
{
    const char *p = text, *found = NULL;
    gsize found_len = 0;
    long at = 0;

    while (*p != '\0' && at < line)
    {
        if (p[0] == '/' && p[1] == '/')
            p = query_line_end (p);
        else if (p[0] == '/' && p[1] == '*')
        {
            const char *c = strstr (p + 2, "*/");
            const char *e = c != NULL ? c + 2 : p + strlen (p);

            for (; p < e; p++)
                at += *p == '\n' ? 1 : 0;
        }
        else if (*p == '"' || *p == '\'' || *p == '`')
        {
            const char quote = *p;

            for (p++; *p != '\0' && *p != quote; p++)
            {
                if (*p == '\\' && p[1] != '\0')
                    p++;
                at += *p == '\n' ? 1 : 0;
            }
            if (*p != '\0')
                p++;
        }
        else if (strncmp (p, "use", 3) == 0
                 && (p == text
                     || !(g_ascii_isalnum (p[-1]) || p[-1] == '_' || p[-1] == '$' || p[-1] == '.')))
        {
            // use ( "name" ): the string, as it is written
            const char *q = p + 3, *lit;

            while (*q == ' ' || *q == '\t')
                q++;
            if (*q == '(')
            {
                for (q++; *q == ' ' || *q == '\t'; q++)
                    ;
                lit = q;
                if (*q == '"' || *q == '\'')
                {
                    const char quote = *q;

                    for (q++; *q != '\0' && *q != quote && *q != '\n'; q++)
                        if (*q == '\\' && q[1] != '\0')
                            q++;
                    if (*q == quote)
                    {
                        const char *e = q + 1;

                        while (*e == ' ' || *e == '\t')
                            e++;
                        if (*e == ')')
                        {
                            found = lit;
                            found_len = (gsize) (q + 1 - lit);
                        }
                    }
                }
            }
            p += 3;
        }
        else
        {
            at += *p == '\n' ? 1 : 0;
            p++;
        }
    }
    return found != NULL ? g_strndup (found, found_len) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_use (const char *text)
{
    GString *out = g_string_new (NULL);
    const char *p = text;

    while (*p != '\0')
    {
        if (p[0] == '/' && p[1] == '/')
        {
            const char *e = query_line_end (p);

            g_string_append_len (out, p, e - p);
            p = e;
        }
        else if (p[0] == '/' && p[1] == '*')
        {
            const char *c = strstr (p + 2, "*/");
            const char *e = c != NULL ? c + 2 : p + strlen (p);

            g_string_append_len (out, p, e - p);
            p = e;
        }
        else if (*p == '"' || *p == '\'' || *p == '`')
        {
            const char *e = p + 1;

            while (*e != '\0' && *e != *p)
                e += e[0] == '\\' && e[1] != '\0' ? 2 : 1;
            if (*e != '\0')
                e++;
            g_string_append_len (out, p, e - p);
            p = e;
        }
        else if (strncmp (p, "use", 3) == 0
                 && (p == text
                     || !(g_ascii_isalnum (p[-1]) || p[-1] == '_' || p[-1] == '$' || p[-1] == '.')))
        {
            const char *e = p + 3;

            while (*e == ' ' || *e == '\t')
                e++;
            if (*e == '(')
                g_string_append (out, "db = db.getSiblingDB");
            else
                g_string_append (out, "use");
            p += 3;
        }
        else
            g_string_append_c (out, *p++);
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_with_limit (const char *text, long limit, gboolean *done)
{
    const char *end;
    GArray *chain = query_chain (text, &end);
    gboolean find = FALSE;
    char *limited = NULL;
    guint i;

    // back to the find (): what the cursor is made with after it, a limit of its own not
    for (i = 0; i < chain->len; i++)
    {
        const query_call_t *call = &g_array_index (chain, query_call_t, i);

        if (query_call_is (call, "find"))
        {
            /* the find () of a collection, db.c.find () or db.getCollection (...).find (), not
               that of an array, db.c.find ().toArray ().find (...), which takes no limit */
            find = i + 1 == chain->len
                || query_call_is (&g_array_index (chain, query_call_t, i + 1), "getCollection");
            break;
        }
        if (query_call_is (call, "limit") || !query_is_cursor_call (call))
            break;
    }

    if (find)
    {
        char *body = g_strndup (text, (gsize) (end - text));

        limited = g_strdup_printf ("%s\n.limit(%ld)", body, limit);
        g_free (body);
    }
    else if (chain->len > 0 && query_call_is (&g_array_index (chain, query_call_t, 0), "aggregate"))
    {
        // aggregate ([...]): a stage $limit at the end of the pipeline
        const query_call_t *call = &g_array_index (chain, query_call_t, 0);
        const char *close = call->close, *open = call->open + 1, *last;

        while (close > call->open && g_ascii_isspace (close[-1]))
            close--;
        while (open < close && g_ascii_isspace (*open))
            open++;
        close--;
        /* a pipeline of its own: [ and ]; one with its own $limit, or that ends with $out or
           $merge, after which no stage may come, is left as it is */
        if (open < close && *open == '[' && *close == ']' && !query_text_has (open, close, "$limit")
            && !query_text_has (open, close, "$out") && !query_text_has (open, close, "$merge"))
        {
            last = query_last_code (open + 1, close);
            /* just before the ], after a comment at the end of the last stage too; the lines of
               the query stay where they were */
            limited = g_strdup_printf ("%.*s%s{ $limit: %ld }%s", (int) (close - text), text,
                                       last == NULL || *last == ',' ? "" : ", ", limit, close);
        }
    }
    g_array_free (chain, TRUE);
    *done = limited != NULL;
    return limited != NULL ? limited : g_strdup (text);
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

/* The hosts of a mongodb:// URI: from after the scheme up to the path or the options */
static const char *
query_uri_hosts (const char *uri, const char **end)
{
    const char *hosts = strstr (uri, "://");

    hosts = hosts != NULL ? hosts + 3 : uri;
    *end = hosts + strcspn (hosts, "/?");
    return hosts;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_db (const char *uri, const char *db)
{
    const char *end;

    if (db != NULL && *db != '\0')
        return g_strdup (db);
    if (uri == NULL)
        return NULL;
    (void) query_uri_hosts (uri, &end);
    if (*end != '/' || end[1] == '\0' || end[1] == '?')
        return NULL;
    return g_strndup (end + 1, strcspn (end + 1, "?"));
}

/* --------------------------------------------------------------------------------------------- */

char *
query_mongo_endpoint (const char *uri, const char *db)
{
    const char *hosts, *at, *end;
    char *server, *name, *endpoint;

    if (uri == NULL)
        return g_strdup (db != NULL ? db : "");
    hosts = query_uri_hosts (uri, &end);
    // the user and the password: before the last '@' of the hosts
    for (at = end; at > hosts && at[-1] != '@'; at--)
        ;
    server = g_strndup (at, (gsize) (end - at));
    name = query_mongo_db (uri, db);
    endpoint = name != NULL ? g_strdup_printf ("%s/%s", server, name) : g_strdup (server);
    g_free (name);
    g_free (server);
    return endpoint;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_has_use (const char *text)
{
    char *used = query_mongo_use (text);
    // a use () there is made getSiblingDB (): the text changes
    const gboolean has = strcmp (used, text) != 0;

    g_free (used);
    return has;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_use_line (const char *db)
{
    GString *line = g_string_new ("use(\"");
    const char *p;

    // a string of JavaScript: its quote and backslash escaped
    for (p = db; *p != '\0'; p++)
    {
        if (*p == '"' || *p == '\\')
            g_string_append_c (line, '\\');
        g_string_append_c (line, *p);
    }
    g_string_append (line, "\");\n\n");
    return g_string_free (line, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_table (const char *json, guint max, guint *rows, GError **error)
{
    JsonParser *parser = json_parser_new ();
    JsonNode *root;
    GPtrArray *nodes, *columns, *cells;
    GHashTable *seen;
    GString *out;
    int *widths;
    guint r, c, total = 0;

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

        // the first @max of them in the table, all of them counted
        for (r = 0; r < json_array_get_length (a) && (max == 0 || r < max); r++)
            g_ptr_array_add (nodes, json_array_get_element (a, r));
        total = json_array_get_length (a);
    }
    else
    {
        g_ptr_array_add (nodes, root);
        total = 1;
    }

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
    *rows = total;

    g_free (widths);
    g_ptr_array_free (cells, TRUE);
    g_ptr_array_free (columns, TRUE);
    g_ptr_array_free (nodes, TRUE);
    g_object_unref (parser);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

char *
query_versions_dir (const char *versions, const char *queries, const char *file)
{
    char *base = g_path_get_basename (file);
    char *dir = g_path_get_dirname (file);
    char *name, *path;

    if (queries != NULL && strcmp (dir, queries) == 0)
        name = g_strdup (base);
    else
    {
        char *hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, file, -1);

        name = g_strdup_printf ("%s-%.8s", base, hash);
        g_free (hash);
    }
    path = g_build_filename (versions, name, (char *) NULL);
    g_free (name);
    g_free (dir);
    g_free (base);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

char *
query_version_name (GDateTime *now, gboolean (*taken) (const char *name, void *data), void *data)
{
    char *stamp = g_date_time_format (now, "%Y%m%d-%H%M%S");
    char *name = g_strconcat (stamp, ".js", (char *) NULL);
    int n;

    // two runs in a second: the second one numbered
    for (n = 2; taken != NULL && taken (name, data); n++)
    {
        g_free (name);
        name = g_strdup_printf ("%s-%d.js", stamp, n);
    }
    g_free (stamp);
    return name;
}

/* --------------------------------------------------------------------------------------------- */

char **
query_versions_over (char **names, guint keep)
{
    const guint len = names != NULL ? g_strv_length (names) : 0;
    GPtrArray *over = g_ptr_array_new ();
    guint i;

    for (i = 0; len > keep && i < len - keep; i++)
        g_ptr_array_add (over, g_strdup (names[i]));
    g_ptr_array_add (over, NULL);
    return (char **) g_ptr_array_free (over, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_version_label (const char *name)
{
    // YYYYMMDD-HHMMSS
    if (strlen (name) < 15 || name[8] != '-')
        return g_strdup (name);
    return g_strdup_printf ("%.2s.%.2s %.2s:%.2s:%.2s", name + 6, name + 4, name + 9, name + 11,
                            name + 13);
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of @text, each with how many times it is there */
static GHashTable *
query_line_counts (const char *text)
{
    GHashTable *counts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    char **lines = g_strsplit (text != NULL ? text : "", "\n", -1);
    char **l;

    for (l = lines; *l != NULL; l++)
    {
        gpointer n;

        // the empty piece after a last newline is no line
        if (l[1] == NULL && **l == '\0')
            break;
        n = g_hash_table_lookup (counts, *l);
        g_hash_table_replace (counts, g_strdup (*l), GINT_TO_POINTER (GPOINTER_TO_INT (n) + 1));
    }
    g_strfreev (lines);
    return counts;
}

/* --------------------------------------------------------------------------------------------- */

void
query_line_changes (const char *before, const char *now, int *added, int *removed)
{
    GHashTable *a = query_line_counts (before), *b = query_line_counts (now);
    GHashTableIter iter;
    gpointer key, value;

    *added = *removed = 0;
    g_hash_table_iter_init (&iter, b);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gpointer was = g_hash_table_lookup (a, key);

        *added += MAX (0, GPOINTER_TO_INT (value) - GPOINTER_TO_INT (was));
    }
    g_hash_table_iter_init (&iter, a);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gpointer is = g_hash_table_lookup (b, key);

        *removed += MAX (0, GPOINTER_TO_INT (value) - GPOINTER_TO_INT (is));
    }
    g_hash_table_destroy (a);
    g_hash_table_destroy (b);
}

/* --------------------------------------------------------------------------------------------- */

/* The number of a version among those of its second: "20261024-173750.js" 1, "...-2.js" 2 */
static long
query_version_number (const char *name)
{
    return strlen (name) > 16 && name[15] == '-' ? strtol (name + 16, NULL, 10) : 1;
}

/* --------------------------------------------------------------------------------------------- */

int
query_version_cmp (const char *a, const char *b)
{
    const int time = strncmp (a, b, 15);
    const long na = query_version_number (a), nb = query_version_number (b);

    if (time != 0)
        return time;
    return na < nb ? -1 : na > nb ? 1 : strcmp (a, b);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_take_tables (const char *out, GPtrArray *tables)
{
    GString *rest = g_string_new (NULL);
    const char *p = out;

    while (*p != '\0')
    {
        const char *end = query_line_end (p);
        const gsize tag = strlen (QUERY_TABLE_LINE);

        if (strncmp (p, QUERY_TABLE_LINE, tag) == 0)
            g_ptr_array_add (tables, g_strndup (p + tag, (gsize) (end - p - tag)));
        else
            g_string_append_len (rest, p, end - p + (*end == '\n' ? 1 : 0));
        p = *end == '\n' ? end + 1 : end;
    }
    return g_string_free (rest, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
