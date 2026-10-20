/*
   What the setup of a project for clangd finds in it, and the .clangd it writes.

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

#include <config.h>

#include <string.h>

#include <json-glib/json-glib.h>

#include "lsp-setup-core.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* the includes of a file that count: the first ones */
#define LSP_SETUP_FIRST 3
/* the lines of a file looked at for its includes */
#define LSP_SETUP_HEAD_LINES 300

/*** file scope type declarations ****************************************************************/

typedef struct
{
    char *name;
    int score;
} lsp_setup_count_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_setup_is_c (const char *name)
{
    return g_str_has_suffix (name, ".c") || g_str_has_suffix (name, ".cc")
        || g_str_has_suffix (name, ".cpp") || g_str_has_suffix (name, ".cxx");
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_setup_str_cmp (gconstpointer a, gconstpointer b)
{
    return strcmp (*(const char *const *) a, *(const char *const *) b);
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_setup_count_cmp (gconstpointer a, gconstpointer b)
{
    const lsp_setup_count_t *x = *(const lsp_setup_count_t *const *) a;
    const lsp_setup_count_t *y = *(const lsp_setup_count_t *const *) b;

    if (x->score != y->score)
        return x->score > y->score ? -1 : 1;
    return strcmp (x->name, y->name);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_setup_count_free (gpointer data)
{
    lsp_setup_count_t *c = (lsp_setup_count_t *) data;

    g_free (c->name);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

/* The name of an include "x" of a line, NULL when the line is none */
static char *
lsp_setup_include_of (const char *line)
{
    const char *p = line, *end;

    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '#')
        return NULL;
    for (p++; *p == ' ' || *p == '\t'; p++)
        ;
    if (strncmp (p, "include", 7) != 0)
        return NULL;
    for (p += 7; *p == ' ' || *p == '\t'; p++)
        ;
    if (*p != '"')
        return NULL;
    end = strchr (p + 1, '"');
    return end != NULL && end > p + 1 ? g_strndup (p + 1, (gsize) (end - p - 1)) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_setup_blank (const char *line)
{
    for (; *line != '\0'; line++)
        if (!g_ascii_isspace (*line))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_setup_keyword (const char *name)
{
    static const char *const words[] = { "if",     "while",   "for",      "switch", "return",
                                         "sizeof", "typedef", "defined",  "case",   "do",
                                         "else",   "goto",    "_Generic", NULL };
    int i;

    for (i = 0; words[i] != NULL; i++)
        if (strcmp (words[i], name) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The name of the function a definition starts with at @lines[@i]: a line from its first column,
   the name before its '(', the parameters closed, then '{' with no ';' before; NULL for none */
static char *
lsp_setup_definition_at (char **lines, int i)
{
    const char *line = lines[i];
    const char *paren, *start, *end, *p;
    int depth = 0, k;

    if (!g_ascii_isalpha (line[0]) && line[0] != '_')
        return NULL;
    paren = strchr (line, '(');
    // an initialized variable, "int x = f (y);", is none
    if (paren == NULL || (strchr (line, '=') != NULL && strchr (line, '=') < paren))
        return NULL;
    for (end = paren; end > line && (end[-1] == ' ' || end[-1] == '\t'); end--)
        ;
    for (start = end; start > line && (g_ascii_isalnum (start[-1]) || start[-1] == '_'); start--)
        ;
    if (start == end || g_ascii_isdigit (*start))
        return NULL;
    // the parameters, on this line and the next ones, then the body
    for (k = i, p = paren; lines[k] != NULL && k < i + 20; k++, p = lines[k])
    {
        for (; p != NULL && *p != '\0'; p++)
        {
            if (*p == '(')
                depth++;
            else if (*p == ')')
                depth--;
            else if (depth == 0 && *p == ';')
                return NULL;
            else if (depth == 0 && *p == '{')
                goto body;
        }
        if (depth == 0)
        {
            // the '{' on a line of its own
            int n;

            for (n = k + 1; lines[n] != NULL && lsp_setup_blank (lines[n]); n++)
                ;
            if (lines[n] != NULL && lines[n][0] == '{')
                goto body;
            return NULL;
        }
        if (lines[k + 1] == NULL)
            break;
    }
    return NULL;

body:
{
    char *name = g_strndup (start, (gsize) (end - start));

    if (lsp_setup_keyword (name))
        g_clear_pointer (&name, g_free);
    return name;
}
}

/* --------------------------------------------------------------------------------------------- */

/* The functions a file defines, added to @names */
static void
lsp_setup_definitions (const char *path, GHashTable *names)
{
    char *text = NULL;
    char **lines;
    int i;

    if (!g_file_get_contents (path, &text, NULL, NULL))
        return;
    lines = g_strsplit (text, "\n", -1);
    g_free (text);
    for (i = 0; lines[i] != NULL; i++)
    {
        char *name = lsp_setup_definition_at (lines, i);

        if (name != NULL)
            g_hash_table_add (names, name);
    }
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

/* A path as a regular expression of the configuration of clangd */
static void
lsp_setup_append_regex (GString *out, const char *path)
{
    const char *p;

    for (p = path; *p != '\0'; p++)
    {
        if (strchr (".^$|()[]{}*+?\\", *p) != NULL)
            g_string_append_c (out, '\\');
        g_string_append_c (out, *p);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_setup_is_header (const char *name)
{
    return g_str_has_suffix (name, ".h") || g_str_has_suffix (name, ".hh")
        || g_str_has_suffix (name, ".hpp") || g_str_has_suffix (name, ".hxx");
}

/* --------------------------------------------------------------------------------------------- */

char **
lsp_setup_test_dirs (const GPtrArray *files)
{
    GPtrArray *dirs = g_ptr_array_new ();
    guint i, k;

    for (i = 0; files != NULL && i < files->len; i++)
    {
        const char *file = g_ptr_array_index (files, i);
        char **parts = g_strsplit (file, "/", -1);
        GString *dir = g_string_new (NULL);
        int p;

        // the directories of the name, not the file itself
        for (p = 0; parts[p] != NULL && parts[p + 1] != NULL; p++)
        {
            if (dir->len > 0)
                g_string_append_c (dir, '/');
            g_string_append (dir, parts[p]);
            if (strcmp (parts[p], "test") == 0 || strcmp (parts[p], "tests") == 0)
            {
                gboolean seen = FALSE;

                for (k = 0; k < dirs->len && !seen; k++)
                    seen = strcmp (g_ptr_array_index (dirs, k), dir->str) == 0;
                if (!seen)
                    g_ptr_array_add (dirs, g_strdup (dir->str));
                break;
            }
        }
        g_string_free (dir, TRUE);
        g_strfreev (parts);
    }
    g_ptr_array_sort (dirs, lsp_setup_str_cmp);
    g_ptr_array_add (dirs, NULL);
    return (char **) g_ptr_array_free (dirs, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_setup_in_dirs (const char *file, char *const *dirs)
{
    int i;

    for (i = 0; dirs != NULL && dirs[i] != NULL; i++)
    {
        const gsize len = strlen (dirs[i]);

        if (strncmp (file, dirs[i], len) == 0 && file[len] == '/')
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

char **
lsp_setup_first_includes (const char *root, const GPtrArray *files, char *const *tests, int max)
{
    GHashTable *scores = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray *ranked = g_ptr_array_new_with_free_func (lsp_setup_count_free);
    GPtrArray *out = g_ptr_array_new ();
    GHashTableIter iter;
    gpointer key, value;
    guint i;

    for (i = 0; files != NULL && i < files->len; i++)
    {
        const char *file = g_ptr_array_index (files, i);
        char *path, *text = NULL;
        char **lines;
        int line, found = 0;

        if (!lsp_setup_is_c (file) || lsp_setup_in_dirs (file, tests))
            continue;
        path = g_build_filename (root, file, (char *) NULL);
        if (!g_file_get_contents (path, &text, NULL, NULL))
        {
            g_free (path);
            continue;
        }
        g_free (path);
        lines = g_strsplit (text, "\n", LSP_SETUP_HEAD_LINES + 1);
        g_free (text);
        for (line = 0;
             lines[line] != NULL && line < LSP_SETUP_HEAD_LINES && found < LSP_SETUP_FIRST; line++)
        {
            char *name = lsp_setup_include_of (lines[line]);
            char *at;

            if (name == NULL)
                continue;
            // a header of the project by its name from the root, not one beside the file
            at = g_build_filename (root, name, (char *) NULL);
            if (strchr (name, '/') != NULL && g_file_test (at, G_FILE_TEST_IS_REGULAR))
            {
                const gpointer was = g_hash_table_lookup (scores, name);
                const int score = GPOINTER_TO_INT (was);

                // the first include of a file weighs most
                g_hash_table_insert (scores, g_strdup (name),
                                     GINT_TO_POINTER (score + LSP_SETUP_FIRST - found));
                found++;
            }
            g_free (at);
            g_free (name);
        }
        g_strfreev (lines);
    }
    g_hash_table_iter_init (&iter, scores);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        lsp_setup_count_t *c = g_new0 (lsp_setup_count_t, 1);

        c->name = g_strdup (key);
        c->score = GPOINTER_TO_INT (value);
        g_ptr_array_add (ranked, c);
    }
    g_ptr_array_sort (ranked, lsp_setup_count_cmp);
    for (i = 0; i < ranked->len && (int) i < max; i++)
        g_ptr_array_add (out,
                         g_strdup (((lsp_setup_count_t *) g_ptr_array_index (ranked, i))->name));
    g_ptr_array_add (out, NULL);
    g_ptr_array_free (ranked, TRUE);
    g_hash_table_destroy (scores);
    return (char **) g_ptr_array_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
lsp_setup_shadowed (const char *root, const GPtrArray *files, char *const *tests)
{
    GHashTable *in_tests = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GHashTable *in_code = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    GPtrArray *both = g_ptr_array_new_with_free_func (g_free);
    GHashTableIter iter;
    gpointer key;
    guint i;

    for (i = 0; files != NULL && i < files->len; i++)
    {
        const char *file = g_ptr_array_index (files, i);
        char *path;

        if (!lsp_setup_is_c (file))
            continue;
        path = g_build_filename (root, file, (char *) NULL);
        lsp_setup_definitions (path, lsp_setup_in_dirs (file, tests) ? in_tests : in_code);
        g_free (path);
    }
    g_hash_table_iter_init (&iter, in_tests);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        // main is in every program
        if (strcmp (key, "main") != 0 && g_hash_table_contains (in_code, key))
            g_ptr_array_add (both, g_strdup (key));
    g_ptr_array_sort (both, lsp_setup_str_cmp);
    g_hash_table_destroy (in_tests);
    g_hash_table_destroy (in_code);
    return both;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_setup_cdb_with (const char *text, const char *root, char *const *includes)
{
    JsonNode *node = json_from_string (text, NULL);
    JsonArray *entries;
    char *out;
    guint i;
    int k;

    if (node == NULL || !JSON_NODE_HOLDS_ARRAY (node))
    {
        if (node != NULL)
            json_node_unref (node);
        return NULL;
    }
    entries = json_node_get_array (node);
    for (i = 0; i < json_array_get_length (entries); i++)
    {
        JsonObject *entry = json_array_get_object_element (entries, i);

        if (entry == NULL)
            continue;
        for (k = 0; includes != NULL && includes[k] != NULL; k++)
        {
            char *path = g_build_filename (root, includes[k], (char *) NULL);

            if (json_object_has_member (entry, "arguments")
                && JSON_NODE_HOLDS_ARRAY (json_object_get_member (entry, "arguments")))
            {
                JsonArray *args = json_object_get_array_member (entry, "arguments");

                json_array_add_string_element (args, "-include");
                json_array_add_string_element (args, path);
            }
            else if (json_object_has_member (entry, "command"))
            {
                char *quoted = g_shell_quote (path);
                char *command = g_strconcat (json_object_get_string_member (entry, "command"),
                                             " -include ", quoted, (char *) NULL);

                json_object_set_string_member (entry, "command", command);
                g_free (command);
                g_free (quoted);
            }
            g_free (path);
        }
    }
    out = json_to_string (node, FALSE);
    json_node_unref (node);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

int
lsp_setup_count_errors (const char *output)
{
    const char *p;
    int n = 0;

    // E[12:00:00.000] [unknown_typename] Line 42: unknown type name 'WDialog'
    for (p = output; p != NULL && (p = strstr (p, "] Line ")) != NULL; p += 7)
        if (g_ascii_isdigit (p[7]))
            n++;
    return n;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_setup_rules (char *const *includes, char *const *skip, const char *absolute_root)
{
    GString *out = g_string_new (NULL);
    int i;

    if (includes != NULL && includes[0] != NULL)
    {
        g_string_append (out,
                         "# a header is read alone: it gets what the files that include it "
                         "include first\nIf:\n  PathMatch: ");
        if (absolute_root != NULL)
        {
            lsp_setup_append_regex (out, absolute_root);
            g_string_append (out, "/");
        }
        g_string_append (out, ".*\\.(h|hh|hpp|hxx)\nCompileFlags:\n  Add: [");
        for (i = 0; includes[i] != NULL; i++)
            g_string_append_printf (out, "%s-include, \"%s\"", i > 0 ? ", " : "", includes[i]);
        g_string_append (out, "]\n");
    }
    if (skip != NULL && skip[0] != NULL)
    {
        if (out->len > 0)
            g_string_append (out, "---\n");
        g_string_append (out,
                         "# the functions the tests define again are not those of the "
                         "program\nIf:\n  PathMatch: ");
        if (absolute_root != NULL)
        {
            lsp_setup_append_regex (out, absolute_root);
            g_string_append (out, "/");
        }
        g_string_append (out, "(");
        for (i = 0; skip[i] != NULL; i++)
        {
            if (i > 0)
                g_string_append_c (out, '|');
            lsp_setup_append_regex (out, skip[i]);
        }
        g_string_append (out, ")/.*\nIndex:\n  Background: Skip\n");
    }
    if (out->len == 0)
    {
        g_string_free (out, TRUE);
        return NULL;
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_setup_clangd (const char *old, const char *rules)
{
    GString *before = g_string_new (NULL), *after = g_string_new (NULL);
    GString *out = g_string_new (NULL);
    char **lines = g_strsplit (old != NULL ? old : "", "\n", -1);
    int i, begin = -1, end = -1;

    // the block: its first line and its last; a block that lost its end is but its first line
    for (i = 0; lines[i] != NULL && end < 0; i++)
        if (begin < 0 && g_str_has_prefix (lines[i], "# >>> coole"))
            begin = i;
        else if (begin >= 0 && g_str_has_prefix (lines[i], LSP_SETUP_END))
            end = i;
    if (begin >= 0 && end < 0)
        end = begin;
    for (i = 0; lines[i] != NULL; i++)
    {
        if (begin >= 0 && i >= begin && i <= end)
            continue;
        if (lines[i + 1] != NULL || *lines[i] != '\0')
            g_string_append_printf (begin >= 0 && i > end ? after : before, "%s\n", lines[i]);
    }
    g_strfreev (lines);
    // the separators of documents the block stood between
    while (g_str_has_suffix (before->str, "---\n"))
        g_string_truncate (before, before->len - 4);
    while (g_str_has_prefix (after->str, "---\n"))
        g_string_erase (after, 0, 4);
    g_strstrip (before->str);
    g_string_set_size (before, strlen (before->str));
    g_strstrip (after->str);
    g_string_set_size (after, strlen (after->str));

    if (before->len > 0)
        g_string_append_printf (out, "%s\n", before->str);
    if (rules != NULL)
    {
        if (out->len > 0)
            g_string_append (out, "---\n");
        g_string_append_printf (out, "%s\n%s%s\n", LSP_SETUP_BEGIN, rules, LSP_SETUP_END);
    }
    if (after->len > 0)
    {
        if (out->len > 0)
            g_string_append (out, "---\n");
        g_string_append_printf (out, "%s\n", after->str);
    }
    g_string_free (before, TRUE);
    g_string_free (after, TRUE);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
