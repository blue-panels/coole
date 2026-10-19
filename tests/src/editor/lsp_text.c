/*
   src/plugins/lsp - tests for the places of a text as a language server has them

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
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "src/plugins/lsp/lsp-text.h"

/* --------------------------------------------------------------------------------------------- */

static void
check_position (const char *text, gsize offset, lsp_encoding_t encoding, guint line,
                guint character)
{
    guint l = 99, c = 99;
    gsize back = 0;

    g_assert_true (lsp_text_position (text, strlen (text), offset, encoding, &l, &c));
    g_assert_cmpuint (l, ==, line);
    g_assert_cmpuint (c, ==, character);
    g_assert_true (lsp_text_offset (text, strlen (text), l, c, encoding, &back));
    g_assert_cmpuint (back, ==, offset);
}

/* --------------------------------------------------------------------------------------------- */

/* "a", a face beyond the basic plane (four bytes of UTF-8, two units of UTF-16), "b" */
static void
test_positions (void)
{
    const char *face = "a\xf0\x9f\x98\x80"
                       "b";
    const char *lines = "x\r\ny\rz\n\xd0\xb6w";
    gsize offset = 0;

    check_position (face, 0, LSP_ENCODING_UTF16, 0, 0);
    check_position (face, 1, LSP_ENCODING_UTF16, 0, 1);
    check_position (face, 5, LSP_ENCODING_UTF16, 0, 3);
    check_position (face, 5, LSP_ENCODING_UTF8, 0, 5);
    check_position (face, 6, LSP_ENCODING_UTF16, 0, 4);

    // "\r\n", "\r" and "\n" end lines
    check_position (lines, 0, LSP_ENCODING_UTF16, 0, 0);
    check_position (lines, 1, LSP_ENCODING_UTF16, 0, 1);
    check_position (lines, 3, LSP_ENCODING_UTF16, 1, 0);
    check_position (lines, 5, LSP_ENCODING_UTF16, 2, 0);
    check_position (lines, 7, LSP_ENCODING_UTF16, 3, 0);
    check_position (lines, 9, LSP_ENCODING_UTF16, 3, 1);
    check_position (lines, 9, LSP_ENCODING_UTF8, 3, 2);
    check_position (lines, 10, LSP_ENCODING_UTF16, 3, 2);

    // inside "\r\n" is its start, inside a character too
    {
        guint l, c;

        g_assert_true (lsp_text_position (lines, strlen (lines), 2, LSP_ENCODING_UTF16, &l, &c));
        g_assert_cmpuint (l, ==, 0);
        g_assert_cmpuint (c, ==, 1);
        g_assert_true (lsp_text_position (face, strlen (face), 3, LSP_ENCODING_UTF16, &l, &c));
        g_assert_cmpuint (c, ==, 1);
        g_assert_false (lsp_text_position (face, strlen (face), 7, LSP_ENCODING_UTF16, &l, &c));
    }

    // a character past the end of its line is the end of the line
    g_assert_true (lsp_text_offset (lines, strlen (lines), 0, 100, LSP_ENCODING_UTF16, &offset));
    g_assert_cmpuint (offset, ==, 1);
    // the line after the last end of line is there, empty; the one after it is not
    g_assert_true (lsp_text_offset ("ab\n", 3, 1, 0, LSP_ENCODING_UTF16, &offset));
    g_assert_cmpuint (offset, ==, 3);
    g_assert_false (lsp_text_offset ("ab\n", 3, 2, 0, LSP_ENCODING_UTF16, &offset));
    g_assert_false (lsp_text_offset ("ab", 2, -1, 0, LSP_ENCODING_UTF16, &offset));
    g_assert_false (lsp_text_offset ("ab", 2, 0, -1, LSP_ENCODING_UTF16, &offset));
    // half of a character beyond the basic plane
    g_assert_false (lsp_text_offset (face, strlen (face), 0, 2, LSP_ENCODING_UTF16, &offset));
    g_assert_false (lsp_text_offset (face, strlen (face), 0, 3, LSP_ENCODING_UTF8, &offset));
    // an empty text has its one empty line
    g_assert_true (lsp_text_offset ("", 0, 0, 5, LSP_ENCODING_UTF16, &offset));
    g_assert_cmpuint (offset, ==, 0);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_uri (void)
{
    char *dir = g_dir_make_tmp ("lsp-text-XXXXXX", NULL);
    char *real = lsp_canonical_path (dir);
    char *file = g_build_filename (dir, "a b%#\xd0\xb6.c", (char *) NULL);
    char *link = g_build_filename (dir, "link.c", (char *) NULL);
    char *uri, *back, *expected;

    g_assert_true (g_file_set_contents (file, "x", 1, NULL));
    g_assert_cmpint (symlink (file, link), ==, 0);

    uri = lsp_uri_from_path (file);
    g_assert_nonnull (uri);
    g_assert_true (g_str_has_prefix (uri, "file:///"));
    g_assert_nonnull (strstr (uri, "a%20b%25%23%D0%B6.c"));
    back = lsp_path_from_uri (uri);
    expected = g_build_filename (real, "a b%#\xd0\xb6.c", (char *) NULL);
    g_assert_cmpstr (back, ==, expected);
    g_free (back);

    // a link is the file it leads to
    back = lsp_uri_from_path (link);
    g_assert_cmpstr (back, ==, uri);
    g_free (back);
    g_free (uri);

    // a file not written yet is in its directory
    back = g_build_filename (dir, "new.c", (char *) NULL);
    uri = lsp_canonical_path (back);
    g_free (back);
    back = g_build_filename (real, "new.c", (char *) NULL);
    g_assert_cmpstr (uri, ==, back);
    g_free (back);
    g_free (uri);

    g_assert_null (lsp_canonical_path ("relative.c"));
    back = lsp_path_from_uri ("file://localhost/tmp/x.c");
    g_assert_cmpstr (back, ==, "/tmp/x.c");
    g_free (back);
    g_assert_null (lsp_path_from_uri ("file://elsewhere/tmp/x.c"));
    g_assert_null (lsp_path_from_uri ("https://example.org/x.c"));

    (void) g_remove (link);
    (void) g_remove (file);
    (void) g_rmdir (dir);
    g_free (expected);
    g_free (link);
    g_free (file);
    g_free (real);
    g_free (dir);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/lsp/text-positions", test_positions);
    g_test_add_func ("/lsp/text-uri", test_uri);
    return g_test_run ();
}
