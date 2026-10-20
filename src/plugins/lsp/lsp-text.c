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
