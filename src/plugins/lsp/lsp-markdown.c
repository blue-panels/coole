/*
   The Markdown of a language server as the text of a note.

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

#include "lsp-markdown.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* "->" as an arrow of UTF-8, U+2192, and a space: how clangd says the type a function gives */
#define LSP_MARKDOWN_ARROW     "\xe2\x86\x92 "
#define LSP_MARKDOWN_ARROW_LEN 4

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The bytes of @s, @len of them, with the marks of Markdown in a line gone: the escapes, the
   code in backquotes and what is strong hot, a link its text */
static void
lsp_markdown_inline (GString *out, const char *s, gsize len)
{
    gboolean strong = FALSE;
    gsize i = 0;

    while (i < len)
    {
        const char c = s[i];

        if (c == '\\' && i + 1 < len && g_ascii_ispunct (s[i + 1]))
        {
            g_string_append_c (out, s[i + 1]);
            i += 2;
        }
        else if (c == '`')
        {
            gsize run = 0, close;

            while (i + run < len && s[i + run] == '`')
                run++;
            // the code goes to a run of as many backquotes, and is as it is
            for (close = i + run; close < len; close++)
                if (s[close] == '`')
                {
                    gsize n = 0;

                    while (close + n < len && s[close + n] == '`')
                        n++;
                    if (n == run)
                        break;
                    close += n - 1;
                }
            if (close >= len)
            {
                g_string_append_len (out, s + i, (gssize) run);
                i += run;
            }
            else
            {
                gsize from = i + run, to = close;

                if (to - from >= 2 && s[from] == ' ' && s[to - 1] == ' ')
                {
                    from++;
                    to--;
                }
                // what is strong is hot already
                if (!strong)
                    g_string_append_c (out, LSP_NOTE_HOT_ON);
                g_string_append_len (out, s + from, (gssize) (to - from));
                if (!strong)
                    g_string_append_c (out, LSP_NOTE_HOT_OFF);
                i = close + run;
            }
        }
        else if (c == '*' && i + 1 < len && s[i + 1] == '*')
        {
            strong = !strong;
            g_string_append_c (out, strong ? LSP_NOTE_HOT_ON : LSP_NOTE_HOT_OFF);
            i += 2;
        }
        else if (c == '[')
        {
            const char *end = memchr (s + i, ']', len - i);
            const char *url_end = end != NULL && (gsize) (end - s) + 1 < len && end[1] == '('
                ? memchr (end, ')', len - (gsize) (end - s))
                : NULL;

            if (url_end != NULL)
            {
                g_string_append_len (out, s + i + 1, end - (s + i + 1));
                i = (gsize) (url_end - s) + 1;
            }
            else
            {
                g_string_append_c (out, c);
                i++;
            }
        }
        else
        {
            g_string_append_c (out, c);
            i++;
        }
    }
    if (strong)
        g_string_append_c (out, LSP_NOTE_HOT_OFF);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @line is a rule: three or more of "-", "*", "_" or "=", one of them, spaces between */
static gboolean
lsp_markdown_is_rule (const char *line)
{
    char mark = '\0';
    int count = 0;

    for (; *line != '\0'; line++)
        if (*line == ' ')
            continue;
        else if (mark == '\0' && strchr ("-*_=", *line) != NULL)
        {
            mark = *line;
            count++;
        }
        else if (*line == mark)
            count++;
        else
            return FALSE;
    return count >= 3;
}

/* --------------------------------------------------------------------------------------------- */

/* The line @line, made, to @lines: no empty line at the start, nor two together, nor around a
   rule, nor two rules together */
static void
lsp_markdown_add (GPtrArray *lines, char *line)
{
    const char *last = lines->len > 0 ? g_ptr_array_index (lines, lines->len - 1) : NULL;
    const gboolean empty = *line == '\0';
    const gboolean rule = line[0] == LSP_NOTE_RULE;

    if ((empty || rule) && (last == NULL || *last == '\0' || last[0] == LSP_NOTE_RULE))
    {
        // an empty line before a rule goes, and the rule is put
        if (rule && last != NULL && *last == '\0')
        {
            g_ptr_array_remove_index (lines, lines->len - 1);
            lsp_markdown_add (lines, line);
            return;
        }
        g_free (line);
        return;
    }
    if (empty && last != NULL && last[0] == LSP_NOTE_RULE)
    {
        g_free (line);
        return;
    }
    g_ptr_array_add (lines, line);
}

/* The bytes at the start of @line, @len of them, that are the ornament of a comment of the source:
   a run of three or more "-", "=", "*" or "_", escaped, as clangd puts the line of dashes over a
   function, alone or before its documentation, and the spaces after it; a rule of the server is
   not escaped */
static gsize
lsp_markdown_ornament (const char *line, gsize len)
{
    gsize at = 0;
    char mark;
    int count = 0;

    while (at < len && line[at] == ' ')
        at++;
    if (at + 1 >= len || line[at] != '\\' || strchr ("-=*_", line[at + 1]) == NULL)
        return 0;
    mark = line[at + 1];
    while (at < len)
        if (line[at] == mark)
        {
            count++;
            at++;
        }
        else if (line[at] == '\\' && at + 1 < len && line[at + 1] == mark)
            at++;
        else
            break;
    if (count < 3)
        return 0;
    while (at < len && line[at] == ' ')
        at++;
    return at;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

char *
lsp_markdown_note (const char *markdown)
{
    GPtrArray *lines = g_ptr_array_new_with_free_func (g_free);
    char **parts = g_strsplit (markdown != NULL ? markdown : "", "\n", -1);
    char fence[4] = "";
    GString *out;
    guint i;

    for (i = 0; parts[i] != NULL; i++)
    {
        const char *line = parts[i];
        const char *trimmed = line;
        GString *made;
        gsize len;
        int hashes = 0;

        // a fenced block: its lines as they are, the fences gone
        while (*trimmed == ' ' && trimmed - line < 3)
            trimmed++;
        if (*fence != '\0')
        {
            if (strncmp (trimmed, fence, 3) == 0)
                *fence = '\0';
            else
                lsp_markdown_add (lines, g_strdup_printf ("%c%s", LSP_NOTE_CODE, line));
            continue;
        }
        if (strncmp (trimmed, "```", 3) == 0 || strncmp (trimmed, "~~~", 3) == 0)
        {
            g_strlcpy (fence, trimmed, sizeof (fence));
            continue;
        }
        if (lsp_markdown_is_rule (trimmed))
        {
            lsp_markdown_add (lines, g_strdup_printf ("%c", LSP_NOTE_RULE));
            continue;
        }

        made = g_string_new (NULL);
        while (trimmed[hashes] == '#' && hashes < 7)
            hashes++;
        if (hashes >= 1 && hashes <= 6 && (trimmed[hashes] == ' ' || trimmed[hashes] == '\0'))
        {
            // the closing hashes of a heading go too
            const char *text = trimmed + hashes;

            len = strlen (text);
            while (len > 0 && (text[len - 1] == '#' || text[len - 1] == ' '))
                len--;
            while (len > 0 && *text == ' ')
            {
                text++;
                len--;
            }
            g_string_append_c (made, LSP_NOTE_HEADING);
            lsp_markdown_inline (made, text, len);
        }
        else
        {
            // a break of the line at its end, two spaces or a backslash, goes
            len = strlen (line);
            while (len > 0 && line[len - 1] == ' ')
                len--;
            if (len > 0 && line[len - 1] == '\\')
                len--;
            /* what a function gives, after an arrow, is said in words, in English as the rest of
               what the server says: "Parameters:", "Type:" */
            if (len >= LSP_MARKDOWN_ARROW_LEN
                && strncmp (line, LSP_MARKDOWN_ARROW, LSP_MARKDOWN_ARROW_LEN) == 0)
            {
                g_string_append (made, "Returns:");
                line += LSP_MARKDOWN_ARROW_LEN - 1;
                len -= LSP_MARKDOWN_ARROW_LEN - 1;
            }
            {
                const gsize skip = lsp_markdown_ornament (line, len);

                line += skip;
                len -= skip;
            }
            lsp_markdown_inline (made, line, len);
        }
        lsp_markdown_add (lines, g_string_free (made, FALSE));
    }
    g_strfreev (parts);

    // what ends the text says nothing: empty lines, a rule
    while (lines->len > 0
           && (*(char *) g_ptr_array_index (lines, lines->len - 1) == '\0'
               || *(char *) g_ptr_array_index (lines, lines->len - 1) == LSP_NOTE_RULE))
        g_ptr_array_remove_index (lines, lines->len - 1);
    out = g_string_new (NULL);
    for (i = 0; i < lines->len; i++)
        g_string_append_printf (out, "%s%s", i > 0 ? "\n" : "",
                                (const char *) g_ptr_array_index (lines, i));
    g_ptr_array_free (lines, TRUE);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
