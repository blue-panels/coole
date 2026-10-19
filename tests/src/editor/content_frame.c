/*
   src - tests for the messages framed by a "Content-Length" header

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

/* -Dassert=false sets G_DISABLE_ASSERT in config.h: g_test_init () would then end the test and
   g_assert () check nothing, and the checks are the point of a test */
#undef G_DISABLE_ASSERT

#include <string.h>

#include <glib.h>

#include "src/content-frame.h"

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *messages;

static void
collect (const char *body, gsize len, void *data)
{
    (void) data;
    g_assert_cmpuint (strlen (body), ==, len);
    g_ptr_array_add (messages, g_strdup (body));
}

/* --------------------------------------------------------------------------------------------- */

static void
test_split (void)
{
    const char *stream =
        "Content-Length: 7\r\n\r\n{\"a\":1}content-length:2\r\nX-Other: y\r\n\r\n[]";
    const gsize len = strlen (stream);
    gsize cut;

    // the stream torn at every byte gives the same two messages
    for (cut = 0; cut <= len; cut++)
    {
        content_frame_reader_t *reader = content_frame_reader_new (1024);

        messages = g_ptr_array_new_with_free_func (g_free);
        g_assert_true (content_frame_reader_feed (reader, stream, cut, collect, NULL));
        g_assert_true (content_frame_reader_feed (reader, stream + cut, len - cut, collect, NULL));
        g_assert_cmpuint (messages->len, ==, 2);
        g_assert_cmpstr (g_ptr_array_index (messages, 0), ==, "{\"a\":1}");
        g_assert_cmpstr (g_ptr_array_index (messages, 1), ==, "[]");
        g_ptr_array_free (messages, TRUE);
        content_frame_reader_free (reader);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_byte_by_byte (void)
{
    char *framed;
    gsize len, i;
    content_frame_reader_t *reader = content_frame_reader_new (1024);

    framed = content_frame_wrap ("{\"seq\":1}", 9, &len);
    g_assert_cmpstr (framed, ==, "Content-Length: 9\r\n\r\n{\"seq\":1}");
    messages = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < len; i++)
        g_assert_true (content_frame_reader_feed (reader, framed + i, 1, collect, NULL));
    g_assert_cmpuint (messages->len, ==, 1);
    g_ptr_array_free (messages, TRUE);
    g_free (framed);
    content_frame_reader_free (reader);
}

/* --------------------------------------------------------------------------------------------- */

/* Several messages in one read, an empty body among them */
static void
test_many_in_one (void)
{
    const char *stream = "Content-Length: 2\r\n\r\n{}Content-Length: 0\r\n\r\n"
                         "Content-Length:   3  \r\n\r\nabcContent-Le";
    content_frame_reader_t *reader = content_frame_reader_new (1024);

    messages = g_ptr_array_new_with_free_func (g_free);
    g_assert_true (content_frame_reader_feed (reader, stream, strlen (stream), collect, NULL));
    g_assert_cmpuint (messages->len, ==, 3);
    g_assert_cmpstr (g_ptr_array_index (messages, 0), ==, "{}");
    g_assert_cmpstr (g_ptr_array_index (messages, 1), ==, "");
    g_assert_cmpstr (g_ptr_array_index (messages, 2), ==, "abc");
    g_assert_true (content_frame_reader_feed (reader, "ngth: 1\r\n\r\nx", 12, collect, NULL));
    g_assert_cmpuint (messages->len, ==, 4);
    g_assert_cmpstr (g_ptr_array_index (messages, 3), ==, "x");
    g_ptr_array_free (messages, TRUE);
    content_frame_reader_free (reader);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_broken (void)
{
    static const char *const broken[] = {
        // a header with no length is no header
        "Content-Type: x\r\n\r\n{}",
        // two lengths
        "Content-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
        // no number
        "Content-Length: -2\r\n\r\n{}",
        "Content-Length: 2x\r\n\r\n{}",
        "Content-Length: \r\n\r\n{}",
        // a body bigger than the reader takes
        "Content-Length: 1025\r\n\r\n",
        "Content-Length: 99999999999999999999999\r\n\r\n",
    };
    content_frame_reader_t *reader = content_frame_reader_new (1024);
    GString *long_header = g_string_new ("X-Long: ");
    gsize i;

    messages = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < G_N_ELEMENTS (broken); i++)
    {
        g_assert_false (
            content_frame_reader_feed (reader, broken[i], strlen (broken[i]), collect, NULL));
        g_assert_cmpuint (messages->len, ==, 0);
        // and the reader is clean for what comes after
        g_assert_true (
            content_frame_reader_feed (reader, "Content-Length: 2\r\n\r\n{}", 23, collect, NULL));
        g_assert_cmpuint (messages->len, ==, 1);
        g_ptr_array_set_size (messages, 0);
    }

    // a header that does not end
    while (long_header->len <= CONTENT_FRAME_MAX_HEADER)
        g_string_append_c (long_header, 'x');
    g_assert_false (
        content_frame_reader_feed (reader, long_header->str, long_header->len, collect, NULL));
    g_string_free (long_header, TRUE);
    g_ptr_array_free (messages, TRUE);
    content_frame_reader_free (reader);
}

/* --------------------------------------------------------------------------------------------- */

static content_frame_reader_t *resetting;

static void
collect_and_reset (const char *body, gsize len, void *data)
{
    collect (body, len, data);
    content_frame_reader_reset (resetting);
}

/* A stream closed from the handler of a message: what came after it is not read */
static void
test_reset_from_handler (void)
{
    const char *stream = "Content-Length: 2\r\n\r\n{}Content-Length: 2\r\n\r\n[]";

    resetting = content_frame_reader_new (1024);
    messages = g_ptr_array_new_with_free_func (g_free);
    g_assert_true (
        content_frame_reader_feed (resetting, stream, strlen (stream), collect_and_reset, NULL));
    g_assert_cmpuint (messages->len, ==, 1);
    g_ptr_array_free (messages, TRUE);
    content_frame_reader_free (resetting);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/content-frame/split", test_split);
    g_test_add_func ("/content-frame/byte-by-byte", test_byte_by_byte);
    g_test_add_func ("/content-frame/many-in-one", test_many_in_one);
    g_test_add_func ("/content-frame/broken", test_broken);
    g_test_add_func ("/content-frame/reset-from-handler", test_reset_from_handler);
    return g_test_run ();
}
