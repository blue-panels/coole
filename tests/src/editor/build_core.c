/*
   src/plugins/build - tests for the core of the build plugin

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

#include "src/plugins/build/build-core.h"

static char *top = NULL;

static char *
at (const char *rel)
{
    return g_build_filename (top, rel, (char *) NULL);
}

static void
write_file (const char *rel, const char *text)
{
    char *path = at (rel);
    char *dir = g_path_get_dirname (path);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, text, -1, NULL));
    g_free (dir);
    g_free (path);
}

static void
test_detect (void)
{
    char *root = at ("proj");
    char *build = at ("proj-build");
    char *json;
    build_info_t *info;

    // a meson build next to the project, which names it as its source
    write_file ("proj/meson.build", "project('x', 'c')\n");
    json =
        g_strdup_printf ("{\"directories\": {\"source\": \"%s\", \"build\": \"%s\"}}", root, build);
    write_file ("proj-build/meson-info/meson-info.json", json);
    g_free (json);
    // and one of another project
    write_file ("other-build/meson-info/meson-info.json",
                "{\"directories\": {\"source\": \"/elsewhere\"}}");

    info = build_detect (root);
    g_assert_nonnull (info);
    g_assert_cmpint (info->system, ==, BUILD_MESON);
    g_assert_cmpstr (info->build_dir, ==, build);
    g_assert_nonnull (strstr (info->command, "meson compile -C"));
    build_info_free (info);

    // make
    write_file ("mk/Makefile", "all:\n");
    g_free (root);
    root = at ("mk");
    info = build_detect (root);
    g_assert_nonnull (info);
    g_assert_cmpint (info->system, ==, BUILD_MAKE);
    g_assert_cmpstr (info->command, ==, "make");
    build_info_free (info);

    // nothing
    write_file ("bare/a.c", "");
    g_free (root);
    root = at ("bare");
    g_assert_null (build_detect (root));

    g_free (root);
    g_free (build);
}

static gboolean
compile (const char *flags, const char *out)
{
    char *src = at ("prog/main.c");
    char *command;
    gint status = 0;
    gboolean ok;

    command = g_strdup_printf ("cc %s -o '%s' '%s'", flags, out, src);
    ok = g_spawn_command_line_sync (command, NULL, NULL, &status, NULL) && status == 0;
    g_free (command);
    g_free (src);
    return ok;
}

static gboolean
is_elf (const char *path)
{
    char *data = NULL;
    gsize len = 0;
    gboolean elf;

    elf = g_file_get_contents (path, &data, &len, NULL) && len >= 4
        && memcmp (data, "\177ELF", 4) == 0;
    g_free (data);
    return elf;
}

static void
test_elf (void)
{
    char *debug = at ("prog/out/debug");
    char *fast = at ("prog/out/fast");
    char *dir = at ("prog");
    build_elf_t elf;
    GPtrArray *programs;

    write_file ("prog/main.c", "int main (void) { return 0; }\n");
    write_file ("prog/out/.keep", "");
    // the flags in the debug information, which clang leaves out by default
    if (g_find_program_in_path ("cc") == NULL || !compile ("-g -O0 -grecord-gcc-switches", debug)
        || !compile ("-g -O2 -grecord-gcc-switches", fast))
    {
        g_test_skip ("no C compiler");
        goto out;
    }
    if (!is_elf (debug))
    {
        g_test_skip ("the C compiler makes no ELF");
        goto out;
    }

    g_assert_true (build_elf_read (debug, &elf));
    g_assert_true (elf.executable);
    g_assert_true (elf.debug_info);
    g_assert_false (elf.optimized);

    g_assert_true (build_elf_read (fast, &elf));
    g_assert_true (elf.debug_info);
    g_assert_true (elf.optimized);

    g_assert_false (build_elf_read (dir, &elf));

    programs = build_find_programs (dir);
    g_assert_cmpuint (programs->len, ==, 2);
    g_ptr_array_free (programs, TRUE);

out:
    g_free (debug);
    g_free (fast);
    g_free (dir);
}

static void
test_diagnostic (void)
{
    build_diagnostic_t *d;

    d = build_parse_diagnostic ("../coole/src/a.c:12:5: error: 'x' undeclared", "/b/coole-build");
    g_assert_nonnull (d);
    g_assert_cmpstr (d->file, ==, "/b/coole/src/a.c");
    g_assert_cmpint (d->line, ==, 12);
    g_assert_cmpint (d->column, ==, 5);
    g_assert_true (d->error);
    g_assert_cmpstr (d->message, ==, "'x' undeclared");
    build_diagnostic_free (d);

    d = build_parse_diagnostic ("/x/b.h:3: warning: unused", NULL);
    g_assert_nonnull (d);
    g_assert_false (d->error);
    g_assert_cmpint (d->column, ==, 0);
    build_diagnostic_free (d);

    g_assert_null (build_parse_diagnostic ("[12/30] Compiling C object a.o", "/b"));
    g_assert_null (build_parse_diagnostic ("ninja: build stopped: subcommand failed.", "/b"));
}

static void
remove_tree (const char *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    const char *name;

    if (dir != NULL)
    {
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            char *child = g_build_filename (path, name, (char *) NULL);

            remove_tree (child);
            g_free (child);
        }
        g_dir_close (dir);
    }
    (void) g_remove (path);
}

int
main (int argc, char **argv)
{
    int result;

    g_test_init (&argc, &argv, NULL);
    top = g_dir_make_tmp ("build-core-XXXXXX", NULL);
    g_assert_nonnull (top);
    g_test_add_func ("/build/detect", test_detect);
    g_test_add_func ("/build/elf", test_elf);
    g_test_add_func ("/build/diagnostic", test_diagnostic);
    result = g_test_run ();
    remove_tree (top);
    g_free (top);
    return result;
}
