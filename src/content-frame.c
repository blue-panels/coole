/*
   The messages of a stream framed by a "Content-Length" header, for the clients of the debug
   adapters and of the language servers.

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

#include "lib/global.h"

#include "content-frame.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

struct content_frame_reader_t
{
    GString *buffer;
    gsize max_body;
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* FALSE when the stream is broken; else @header_len is that of the header with its empty line,
   0 when the header is not all there yet, and @body_len the length it says */
static gboolean
content_frame_header (const content_frame_reader_t *reader, gsize *header_len, gsize *body_len)
{
    const GString *buffer = reader->buffer;
    const char *end = g_strstr_len (buffer->str, (gssize) buffer->len, "\r\n\r\n");
    gboolean found = FALSE;
    const char *line;

    *header_len = 0;
    if (end == NULL)
        return buffer->len <= CONTENT_FRAME_MAX_HEADER;
    if ((gsize) (end - buffer->str) > CONTENT_FRAME_MAX_HEADER)
        return FALSE;
    for (line = buffer->str; line < end;)
    {
        const char *eol = strstr (line, "\r\n");

        if (g_ascii_strncasecmp (line, "Content-Length:", 15) == 0)
        {
            const char *digits = line + 15;
            char *digits_end = NULL;
            guint64 n;

            while (*digits == ' ' || *digits == '\t')
                digits++;
            // two lengths: which one is the body's?
            if (found || !g_ascii_isdigit (*digits))
                return FALSE;
            n = g_ascii_strtoull (digits, &digits_end, 10);
            while (digits_end != NULL && (*digits_end == ' ' || *digits_end == '\t'))
                digits_end++;
            if (digits_end != eol || n > reader->max_body)
                return FALSE;
            *body_len = (gsize) n;
            found = TRUE;
        }
        line = eol + 2;
    }
    *header_len = (gsize) (end - buffer->str) + 4;
    return found;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

content_frame_reader_t *
content_frame_reader_new (gsize max_body)
{
    content_frame_reader_t *reader = g_new0 (content_frame_reader_t, 1);

    reader->buffer = g_string_new (NULL);
    reader->max_body = max_body;
    return reader;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
content_frame_reader_feed (content_frame_reader_t *reader, const char *bytes, gsize len,
                           content_frame_fn fn, void *data)
{
    g_string_append_len (reader->buffer, bytes, (gssize) len);
    while (reader->buffer->len > 0)
    {
        gsize header_len = 0, length = 0;
        char *body;

        if (!content_frame_header (reader, &header_len, &length))
        {
            g_string_truncate (reader->buffer, 0);
            return FALSE;
        }
        if (header_len == 0)
            return TRUE;  // the header is not all there yet
        if (reader->buffer->len < header_len + length)
            return TRUE;  // nor the body
        // taken out of the buffer first: @fn may close what reads, and more may come
        body = g_malloc (length + 1);
        memcpy (body, reader->buffer->str + header_len, length);
        body[length] = '\0';
        g_string_erase (reader->buffer, 0, (gssize) (header_len + length));
        fn (body, length, data);
        g_free (body);
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
content_frame_reader_reset (content_frame_reader_t *reader)
{
    g_string_truncate (reader->buffer, 0);
}

/* --------------------------------------------------------------------------------------------- */

void
content_frame_reader_free (content_frame_reader_t *reader)
{
    if (reader == NULL)
        return;
    g_string_free (reader->buffer, TRUE);
    g_free (reader);
}

/* --------------------------------------------------------------------------------------------- */

char *
content_frame_wrap (const char *body, gsize body_len, gsize *len)
{
    GString *out = g_string_sized_new (body_len + 32);

    g_string_append_printf (out, "Content-Length: %" G_GSIZE_FORMAT "\r\n\r\n", body_len);
    g_string_append_len (out, body, (gssize) body_len);
    *len = out->len;
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
