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
#include "src/plugins/build/build-presets.h"

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

    // cargo build --message-format=short
    d = build_parse_diagnostic ("src/main.rs:3:9: error[E0425]: cannot find value `x`", "/r");
    g_assert_nonnull (d);
    g_assert_cmpstr (d->file, ==, "/r/src/main.rs");
    g_assert_true (d->error);
    g_assert_cmpstr (d->message, ==, "cannot find value `x`");
    build_diagnostic_free (d);

    g_assert_null (build_parse_diagnostic ("[12/30] Compiling C object a.o", "/b"));
    g_assert_null (build_parse_diagnostic ("ninja: build stopped: subcommand failed.", "/b"));
}

static void
test_presets (void)
{
    const char *user = "[rust]\nmarkers = Cargo.toml\ngdb = my-gdb\n\n"
                       "[zig]\nextensions = .zig\nbuild = zig build\n";
    char *system = NULL, *root, *program;
    GPtrArray *presets;
    const build_preset_t *p;
    GError *error = NULL;

    g_assert_true (g_file_get_contents (PRESETS_INI, &system, NULL, NULL));

    // those of coole alone
    presets = build_presets_parse (NULL, NULL, system, NULL, &error);
    g_assert_no_error (error);
    root = at ("presets");
    write_file ("presets/Cargo.toml", "[package]\nname = \"try-rust\"\nversion = \"0.1.0\"\n");
    p = build_presets_find (presets, root, "/x/notes.py");
    g_assert_nonnull (p);
    g_assert_cmpstr (p->id, ==, "rust");
    g_assert_cmpstr (p->gdb[0], ==, "rust-gdb");
    g_assert_cmpstr (p->gdb[1], ==, "gdb");
    g_assert_cmpstr (p->build, ==, "cargo build --message-format=short");
    g_assert_true (g_strv_contains ((const char *const *) p->hide, "target/*/deps/*"));
    program = build_presets_expand (p->program, root);
    g_assert_cmpstr (program, ==, "target/debug/try-rust");
    g_free (program);
    // no marker: the ending of the file
    p = build_presets_find (presets, "/nowhere", "/x/notes.py");
    g_assert_nonnull (p);
    g_assert_cmpstr (p->id, ==, "python");
    g_assert_cmpstr (p->adapter, ==, "python3 -m debugpy.adapter");
    g_assert_null (p->gdb);
    p = build_presets_find (presets, NULL, "/x/main.go");
    g_assert_cmpstr (p->launch, ==, "{\"mode\": \"debug\"}");
    g_assert_cmpstr (build_presets_find (presets, NULL, "/x/a.cpp")->id, ==, "c");
    g_assert_null (build_presets_find (presets, "/nowhere", "/x/README"));
    g_assert_null (build_presets_find (presets, "/nowhere", NULL));
    g_ptr_array_free (presets, TRUE);

    // the user first: a group of the name replaces that of coole, a new one is added
    presets = build_presets_parse (NULL, user, system, NULL, &error);
    g_assert_no_error (error);
    p = build_presets_find (presets, root, NULL);
    g_assert_cmpstr (p->id, ==, "rust");
    g_assert_cmpstr (p->gdb[0], ==, "my-gdb");
    g_assert_null (p->build);
    g_assert_cmpstr (build_presets_find (presets, NULL, "/x/a.zig")->build, ==, "zig build");
    g_assert_cmpstr (build_presets_find (presets, NULL, "/x/a.py")->id, ==, "python");
    g_ptr_array_free (presets, TRUE);

    // a wrong file of the user is left out, and said
    presets = build_presets_parse (NULL, "[rust\n", system, NULL, &error);
    g_assert_nonnull (error);
    g_clear_error (&error);
    g_assert_cmpstr (build_presets_find (presets, root, NULL)->gdb[0], ==, "rust-gdb");
    g_ptr_array_free (presets, TRUE);

    // the copy of a project: its language whatever its markers, and first by the endings
    presets = build_presets_parse (NULL, NULL, system, NULL, &error);
    program = build_presets_project_text (build_presets_find (presets, root, NULL), FALSE);
    g_assert_null (strstr (program, "\nmarkers="));
    g_assert_null (strstr (program, "\nbuild="));
    g_assert_nonnull (strstr (program, "language=rust"));
    g_assert_nonnull (strstr (program, "gdb=rust-gdb;gdb;"));
    g_ptr_array_free (presets, TRUE);
    {
        char *text = g_strconcat (program, "command=make me\n", (char *) NULL);
        GError *project_error = NULL;

        presets = build_presets_parse (text, user, system, &project_error, &error);
        g_assert_no_error (project_error);
        g_assert_no_error (error);
        p = build_presets_find (presets, "/nowhere", "/x/a.py");
        g_assert_true (p->project);
        g_assert_cmpstr (p->id, ==, "rust");
        // the copy, not the rust of the user
        g_assert_cmpstr (p->gdb[0], ==, "rust-gdb");
        g_assert_cmpstr (build_presets_find (presets, NULL, "/x/a.rs")->gdb[0], ==, "rust-gdb");
        g_assert_cmpstr (build_presets_find (presets, NULL, "/x/a.py")->id, ==, "python");
        g_ptr_array_free (presets, TRUE);
        g_free (text);

        // a file of the project with the command alone is no copy
        presets = build_presets_parse ("[project]\ncommand=make\n", NULL, system, NULL, NULL);
        g_assert_cmpstr (build_presets_find (presets, "/nowhere", "/x/a.py")->id, ==, "python");
        g_ptr_array_free (presets, TRUE);

        presets = build_presets_parse ("[project\n", NULL, system, &project_error, NULL);
        g_assert_nonnull (project_error);
        g_clear_error (&project_error);
        g_ptr_array_free (presets, TRUE);
    }
    g_free (program);

    // ${package} without a package
    g_assert_null (build_presets_expand ("target/debug/${package}", "/nowhere"));
    program = build_presets_expand ("bin/app", "/nowhere");
    g_assert_cmpstr (program, ==, "bin/app");
    g_free (program);

    g_free (root);
    g_free (system);
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
    g_test_add_func ("/build/presets", test_presets);
    result = g_test_run ();
    remove_tree (top);
    g_free (top);
    return result;
}
