/*
   src/plugins/lsp - tests for what the setup of a project for clangd finds, and writes

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
#include <glib/gstdio.h>

#include "src/plugins/lsp/lsp-setup-core.h"

/* --------------------------------------------------------------------------------------------- */

static char *top;
static GPtrArray *files;

static void
put (const char *relative, const char *text)
{
    char *path = g_build_filename (top, relative, (char *) NULL);
    char *dir = g_path_get_dirname (path);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
    g_assert_true (g_file_set_contents (path, text, -1, NULL));
    g_ptr_array_add (files, g_strdup (relative));
    g_free (dir);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
remove_tree (const char *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    const char *name;

    while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
    {
        char *child = g_build_filename (path, name, (char *) NULL);

        if (g_file_test (child, G_FILE_TEST_IS_DIR))
            remove_tree (child);
        else
            (void) g_remove (child);
        g_free (child);
    }
    if (dir != NULL)
        g_dir_close (dir);
    (void) g_rmdir (path);
}

/* --------------------------------------------------------------------------------------------- */

static char *
joined (char **strv)
{
    char *out = g_strjoinv (",", strv);

    g_strfreev (strv);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_project (void)
{
    char *text;
    GPtrArray *shadowed;

    put ("lib/global.h", "#define G 1\n");
    put ("lib/widget.h", "#define W 1\n");
    put ("src/a.c",
         "#include <config.h>\n#include \"lib/global.h\"\n#include \"lib/widget.h\"\n"
         "#include \"a.h\"\n\nvoid\nmessage (int flags,\n         const char *text)\n{\n}\n"
         "\nstatic int\nhelper (void)\n{\n    return 0;\n}\n"
         "int counter = init (3);\nvoid declared (int x);\n");
    put ("src/a.h", "void message (int flags, const char *text);\n");
    put ("src/b.c", "#include \"lib/global.h\"\n\nint b (void) {\n  if (x)\n  {\n  }\n}\n");
    put ("tests/unit/t.c",
         "#include \"lib/widget.h\"\n\nvoid\nmessage (int flags, const char *text)\n"
         "{\n}\n\nint\nmain (void)\n{\n}\n");
    put ("src/test/u.c", "int\nhelper (void)\n{\n}\n");

    text = joined (lsp_setup_test_dirs (files));
    g_assert_cmpstr (text, ==, "src/test,tests");
    g_free (text);

    // a test of the tests is not counted; the first include weighs most
    {
        char **tests = lsp_setup_test_dirs (files);

        text = joined (lsp_setup_first_includes (top, files, tests, 5));
        g_assert_cmpstr (text, ==, "lib/global.h,lib/widget.h");
        g_free (text);
        text = joined (lsp_setup_first_includes (top, files, tests, 1));
        g_assert_cmpstr (text, ==, "lib/global.h");
        g_free (text);

        shadowed = lsp_setup_shadowed (top, files, tests);
        g_assert_cmpuint (shadowed->len, ==, 2);
        g_assert_cmpstr (g_ptr_array_index (shadowed, 0), ==, "helper");
        g_assert_cmpstr (g_ptr_array_index (shadowed, 1), ==, "message");
        g_ptr_array_free (shadowed, TRUE);
        g_strfreev (tests);
    }
    g_assert_true (lsp_setup_is_header ("x/y.hpp"));
    g_assert_false (lsp_setup_is_header ("x/y.c"));
}

/* --------------------------------------------------------------------------------------------- */

static void
test_errors (void)
{
    g_assert_cmpint (
        lsp_setup_count_errors (
            "I[1] Loaded\nE[19:39:02.821] [unknown_typename] Line 42: unknown type\n"
            "E[19:39:02.821] [expected] Line 7: expected\nE[1] Failed to resolve URI\n"),
        ==, 2);
    g_assert_cmpint (lsp_setup_count_errors (""), ==, 0);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_clangd (void)
{
    char *includes[] = { (char *) "lib/global.h", (char *) "lib/widget.h", NULL };
    char *skip[] = { (char *) "tests", (char *) "src/test", NULL };
    char *rules, *file, *again;

    g_assert_null (lsp_setup_rules (NULL, NULL, NULL));
    rules = lsp_setup_rules (includes, skip, NULL);
    g_assert_nonnull (
        strstr (rules, "Add: [-include, \"lib/global.h\", -include, \"lib/widget.h\"]"));
    g_assert_nonnull (strstr (rules, "PathMatch: (tests|src/test)/.*"));
    g_free (rules);
    // for the configuration of the user, from the root
    rules = lsp_setup_rules (includes, NULL, "/home/x/my.proj");
    g_assert_nonnull (strstr (rules, "PathMatch: /home/x/my\\.proj/.*\\.(h|hh|hpp|hxx)"));
    g_assert_null (strstr (rules, "Background"));
    g_free (rules);

    // a file of the user's, kept around the block, the block written again in its place
    rules = lsp_setup_rules (includes, skip, NULL);
    file = lsp_setup_clangd ("CompileFlags:\n  Add: [-Wall]\n", rules);
    g_assert_true (g_str_has_prefix (file, "CompileFlags:\n  Add: [-Wall]\n---\n# >>> coole"));
    g_assert_true (g_str_has_suffix (file, "# <<< coole\n"));
    again = lsp_setup_clangd (file, rules);
    g_assert_cmpstr (again, ==, file);
    g_free (again);
    // the block taken away: the user's as it was
    again = lsp_setup_clangd (file, NULL);
    g_assert_cmpstr (again, ==, "CompileFlags:\n  Add: [-Wall]\n");
    g_free (again);
    g_free (file);
    // nothing before: the block alone; after it, kept
    file = lsp_setup_clangd (NULL, rules);
    g_assert_true (g_str_has_prefix (file, "# >>> coole"));
    again = g_strconcat (file, "---\nDiagnostics:\n  Suppress: x\n", (char *) NULL);
    g_free (file);
    file = lsp_setup_clangd (again, NULL);
    g_assert_cmpstr (file, ==, "Diagnostics:\n  Suppress: x\n");
    g_free (file);
    g_free (again);
    // a block that lost its end: what follows its first line is the user's, kept
    file = lsp_setup_clangd ("A: 1\n---\n# >>> coole: x\nIf:\n  PathMatch: y\n", rules);
    g_assert_true (g_str_has_prefix (file, "A: 1\n---\n# >>> coole"));
    g_assert_true (g_str_has_suffix (file, "# <<< coole\n---\nIf:\n  PathMatch: y\n"));
    g_free (file);
    g_free (rules);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_cdb (void)
{
    char *includes[] = { (char *) "lib/global.h", NULL };
    char *out;

    out = lsp_setup_cdb_with (
        "[{\"directory\":\"/b\",\"command\":\"cc -c a.c\",\"file\":\"a.c\"},"
        "{\"directory\":\"/b\",\"arguments\":[\"cc\",\"b.c\"],\"file\":\"b.c\"}]",
        "/my proj", includes);
    g_assert_nonnull (strstr (out, "\"cc -c a.c -include '/my proj/lib/global.h'\""));
    g_assert_nonnull (strstr (out, "[\"cc\",\"b.c\",\"-include\",\"/my proj/lib/global.h\"]"));
    g_free (out);
    g_assert_null (lsp_setup_cdb_with ("{}", "/r", includes));
    g_assert_null (lsp_setup_cdb_with ("not json", "/r", includes));
}

int
main (int argc, char **argv)
{
    int result;

    g_test_init (&argc, &argv, NULL);
    top = g_dir_make_tmp ("lsp-setup-XXXXXX", NULL);
    files = g_ptr_array_new_with_free_func (g_free);
    g_test_add_func ("/lsp/setup-project", test_project);
    g_test_add_func ("/lsp/setup-errors", test_errors);
    g_test_add_func ("/lsp/setup-clangd", test_clangd);
    g_test_add_func ("/lsp/setup-cdb", test_cdb);
    result = g_test_run ();
    remove_tree (top);
    g_free (top);
    g_ptr_array_free (files, TRUE);
    return result;
}
