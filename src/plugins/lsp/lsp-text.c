/*
   The places of a text as a language server has them: URIs, lines and characters.

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

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lsp-text.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The bytes of the character at @p, @left bytes there at most: 1 for a byte that starts no
   character of UTF-8, which is a character of its own then */
static gsize
lsp_text_char_bytes (const unsigned char *p, gsize left)
{
    gsize n, i;

    if (p[0] < 0x80)
        return 1;
    if ((p[0] & 0xe0) == 0xc0)
        n = 2;
    else if ((p[0] & 0xf0) == 0xe0)
        n = 3;
    else if ((p[0] & 0xf8) == 0xf0)
        n = 4;
    else
        return 1;
    if (n > left)
        return 1;
    for (i = 1; i < n; i++)
        if ((p[i] & 0xc0) != 0x80)
            return 1;
    return n;
}

/* --------------------------------------------------------------------------------------------- */

/* The units of a character of @bytes bytes: a character beyond the basic plane is two of UTF-16 */
static guint
lsp_text_char_units (gsize bytes, lsp_encoding_t encoding)
{
    if (encoding == LSP_ENCODING_UTF8)
        return (guint) bytes;
    return bytes == 4 ? 2 : 1;
}

/* --------------------------------------------------------------------------------------------- */

/* The bytes of the end of line at @i: 2 for "\r\n", 1 for "\n" or "\r", 0 for none */
static gsize
lsp_text_eol (const char *text, gsize len, gsize i)
{
    if (text[i] == '\n')
        return 1;
    if (text[i] == '\r')
        return i + 1 < len && text[i + 1] == '\n' ? 2 : 1;
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* A tab stop in the plain text of a snippet, and the parenthesis it is in */
typedef struct
{
    gsize from, to;
    gssize opener;  // where the parenthesis opens, -1 for none
    gboolean gone;  // taken with another of its parentheses, or in another
} lsp_snippet_span_t;

/* The places of the tab stops of a snippet in its plain text */
typedef struct
{
    gint64 first;      // the number of the first tab stop, 0 for none yet
    gsize first_from;  // where it is
    GArray *spans;     // lsp_snippet_span_t: the tab stops but $0
    gssize zero;       // $0, -1 for none
} lsp_snippet_marks_t;

static void
lsp_snippet_mark (lsp_snippet_marks_t *m, gint64 n, gsize from, gsize to)
{
    if (n == 0)
    {
        m->zero = (gssize) from;
        return;
    }
    if (m->first == 0 || n < m->first)
    {
        m->first = n;
        m->first_from = from;
    }
    {
        const lsp_snippet_span_t span = { from, to, -1, FALSE };

        g_array_append_val (m->spans, span);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Where the parenthesis, the bracket, the brace or the "<" that has @at in @text opens; -1 for
   none */
static gssize
lsp_snippet_opener (const char *text, gsize at)
{
    int depth = 0;
    gssize i;

    for (i = (gssize) at - 1; i >= 0; i--)
        if (strchr (")]}>", text[i]) != NULL)
            depth++;
        else if (strchr ("([{<", text[i]) != NULL && depth-- == 0)
            return i;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_snippet_span_cmp (gconstpointer a, gconstpointer b)
{
    const lsp_snippet_span_t *x = a, *y = b;

    // from the end
    return x->from < y->from ? 1 : x->from > y->from ? -1 : 0;
}

/* --------------------------------------------------------------------------------------------- */

static gint64
lsp_snippet_number (const char **p)
{
    gint64 n = 0;

    while (g_ascii_isdigit (**p))
        n = n * 10 + (*(*p)++ - '0');
    return n;
}

/* --------------------------------------------------------------------------------------------- */

/* The snippet from @p to its end, or to the '}' that closes what it is in when @depth is not 0,
   as plain text to @out; where it stops */
static const char *
lsp_snippet_any (const char *p, GString *out, int depth, lsp_snippet_marks_t *m)
{
    while (*p != '\0')
    {
        if (*p == '\\' && p[1] != '\0' && strchr ("$}\\,|", p[1]) != NULL)
        {
            g_string_append_c (out, p[1]);
            p += 2;
        }
        else if (*p == '}' && depth > 0)
            return p;
        else if (*p == '$' && g_ascii_isdigit (p[1]))
        {
            p++;
            lsp_snippet_mark (m, lsp_snippet_number (&p), out->len, out->len);
        }
        else if (*p == '$' && (g_ascii_isalpha (p[1]) || p[1] == '_'))
        {
            // a variable: nothing the client knows
            for (p++; g_ascii_isalnum (*p) || *p == '_'; p++)
                ;
        }
        else if (*p == '$' && p[1] == '{' && g_ascii_isdigit (p[2]))
        {
            const gsize from = out->len;
            gint64 n;

            p += 2;
            n = lsp_snippet_number (&p);
            if (*p == ':')
                p = lsp_snippet_any (p + 1, out, depth + 1, m);
            else if (*p == '|')
            {
                // the first choice
                for (p++; *p != '\0' && *p != ',' && *p != '|'; p++)
                {
                    if (*p == '\\' && p[1] != '\0')
                        p++;
                    g_string_append_c (out, *p);
                }
                while (*p != '\0' && !(*p == '|' && p[1] == '}'))
                    p += *p == '\\' && p[1] != '\0' ? 2 : 1;
                if (*p == '|')
                    p++;
            }
            else if (*p == '/')
                // a transform of what is typed at it: none
                while (*p != '\0' && *p != '}')
                    p += *p == '\\' && p[1] != '\0' ? 2 : 1;
            lsp_snippet_mark (m, n, from, out->len);
            if (*p == '}')
                p++;
        }
        else if (*p == '$' && p[1] == '{' && (g_ascii_isalpha (p[2]) || p[2] == '_'))
        {
            // a variable, its default if it has one
            for (p += 2; g_ascii_isalnum (*p) || *p == '_'; p++)
                ;
            if (*p == ':')
                p = lsp_snippet_any (p + 1, out, depth + 1, m);
            else
                // a transform, or nothing
                while (*p != '\0' && *p != '}')
                    p += *p == '\\' && p[1] != '\0' ? 2 : 1;
            if (*p == '}')
                p++;
        }
        else
            g_string_append_c (out, *p++);
    }
    return p;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_text_position (const char *text, gsize len, gsize offset, lsp_encoding_t encoding, guint *line,
                   guint *character)
{
    gsize i = 0;
    guint l = 0, c = 0;

    if (offset > len)
        return FALSE;
    while (i < offset)
    {
        const gsize eol = lsp_text_eol (text, len, i);

        if (eol != 0)
        {
            // inside "\r\n" is its start
            if (i + eol > offset)
                break;
            i += eol;
            l++;
            c = 0;
        }
        else
        {
            const gsize n = lsp_text_char_bytes ((const unsigned char *) text + i, len - i);

            if (i + n > offset)
                break;
            c += lsp_text_char_units (n, encoding);
            i += n;
        }
    }
    *line = l;
    *character = c;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_text_offset (const char *text, gsize len, gint64 line, gint64 character,
                 lsp_encoding_t encoding, gsize *offset)
{
    gsize i = 0;
    gint64 l = 0, c = 0;

    if (line < 0 || character < 0)
        return FALSE;
    // the start of the line
    while (l < line)
    {
        gsize eol;

        if (i >= len)
            return FALSE;
        eol = lsp_text_eol (text, len, i);
        if (eol != 0)
        {
            i += eol;
            l++;
        }
        else
            i++;
    }
    // the character in it
    while (c < character && i < len && lsp_text_eol (text, len, i) == 0)
    {
        const gsize n = lsp_text_char_bytes ((const unsigned char *) text + i, len - i);

        c += lsp_text_char_units (n, encoding);
        if (c > character)
            return FALSE;
        i += n;
    }
    *offset = i;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_canonical_path (const char *path)
{
    char *real, *dir, *base, *result;

    if (path == NULL || !g_path_is_absolute (path))
        return NULL;
    real = realpath (path, NULL);
    if (real != NULL)
    {
        result = g_strdup (real);
        free (real);
        return result;
    }
    // a file not written yet: the directory it goes in
    dir = g_path_get_dirname (path);
    base = g_path_get_basename (path);
    real = realpath (dir, NULL);
    if (real != NULL)
    {
        result = g_build_filename (real, base, (char *) NULL);
        free (real);
    }
    else
        result = g_canonicalize_filename (path, NULL);
    g_free (dir);
    g_free (base);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_uri_from_path (const char *path)
{
    char *canonical = lsp_canonical_path (path);
    char *uri;

    if (canonical == NULL)
        return NULL;
    uri = g_filename_to_uri (canonical, NULL, NULL);
    g_free (canonical);
    return uri;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_path_from_uri (const char *uri)
{
    char *host = NULL;
    char *path;

    if (uri == NULL || g_ascii_strncasecmp (uri, "file:", 5) != 0)
        return NULL;
    path = g_filename_from_uri (uri, &host, NULL);
    if (path != NULL && host != NULL && *host != '\0'
        && g_ascii_strcasecmp (host, "localhost") != 0)
    {
        g_free (path);
        path = NULL;
    }
    g_free (host);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_snippet_plain (const char *snippet, gboolean drop, gsize *cursor)
{
    GString *out = g_string_new (NULL);
    lsp_snippet_marks_t m = { 0, 0, g_array_new (FALSE, FALSE, sizeof (lsp_snippet_span_t)), -1 };

    (void) lsp_snippet_any (snippet != NULL ? snippet : "", out, 0, &m);
    if (drop && m.spans->len > 0)
    {
        /* the tab stops in one pair of parentheses go with what is between them, the ", "; those
           of another pair on their own: "make_shared<${1:T}>(${2:args})" is "make_shared<>()" */
        GArray *spans = m.spans;
        guint i, k;

        for (i = 0; i < spans->len; i++)
        {
            lsp_snippet_span_t *a = &g_array_index (spans, lsp_snippet_span_t, i);

            a->opener = lsp_snippet_opener (out->str, a->from);
        }
        for (i = 0; i < spans->len; i++)
        {
            lsp_snippet_span_t *a = &g_array_index (spans, lsp_snippet_span_t, i);

            for (k = 0; k < spans->len && !a->gone; k++)
            {
                lsp_snippet_span_t *b = &g_array_index (spans, lsp_snippet_span_t, k);

                if (k == i || b->gone)
                    continue;
                // in another: gone with it
                if (b->from <= a->from && a->to <= b->to && (b->from < a->from || b->to > a->to))
                    a->gone = TRUE;
                // of one pair: one span
                else if (b->opener == a->opener && k > i)
                {
                    a->from = MIN (a->from, b->from);
                    a->to = MAX (a->to, b->to);
                    b->gone = TRUE;
                }
            }
        }
        g_array_sort (spans, lsp_snippet_span_cmp);
        *cursor = out->len;
        for (i = 0; i < spans->len; i++)
        {
            const lsp_snippet_span_t *a = &g_array_index (spans, lsp_snippet_span_t, i);

            if (!a->gone)
            {
                g_string_erase (out, (gssize) a->from, (gssize) (a->to - a->from));
                // the earliest, the last erased: none before it moves it
                *cursor = a->from;
            }
        }
    }
    else if (m.first != 0)
        *cursor = m.first_from;
    else if (m.zero >= 0)
        *cursor = (gsize) m.zero;
    else
        *cursor = out->len;
    g_array_free (m.spans, TRUE);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
