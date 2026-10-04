#include <config.h>

/* -Dassert=false sets G_DISABLE_ASSERT in config.h: g_test_init () would then end the test and
   g_assert () check nothing, and the checks are the point of a test */
#undef G_DISABLE_ASSERT

#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "src/plugins/project/project-core.h"

static char *top = NULL;

static void
touch (const char *rel)
{
    char *path = g_build_filename (top, rel, (char *) NULL);
    char *dir = g_path_get_dirname (path);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, "", 0, NULL));
    g_free (dir);
    g_free (path);
}

static char *
at (const char *rel)
{
    return g_build_filename (top, rel, (char *) NULL);
}

static void
test_find_root (void)
{
    char *file, *root, *expected;

    // the outermost build file, the nested ones of the subdirectories aside
    touch ("meson/meson.build");
    touch ("meson/src/meson.build");
    touch ("meson/src/main.c");
    file = at ("meson/src/main.c");
    root = project_find_root (file);
    expected = at ("meson");
    g_assert_cmpstr (root, ==, expected);
    g_free (root);
    g_free (expected);
    g_free (file);

    // .git wins over a build file further up
    touch ("repo/meson.build");
    touch ("repo/lib/.git");
    touch ("repo/lib/a.c");
    file = at ("repo/lib/a.c");
    root = project_find_root (file);
    expected = at ("repo/lib");
    g_assert_cmpstr (root, ==, expected);
    g_free (root);
    g_free (expected);
    g_free (file);

    // nothing: the directory of the file
    touch ("loose/x.txt");
    file = at ("loose/x.txt");
    root = project_find_root (file);
    expected = at ("loose");
    g_assert_cmpstr (root, ==, expected);
    g_free (root);
    g_free (expected);
    g_free (file);
}

static void
test_list_files (void)
{
    char *root = at ("walk");
    GPtrArray *files;

    touch ("walk/b.c");
    touch ("walk/a/x.h");
    touch ("walk/.hidden/y.c");
    touch ("walk/node_modules/z.js");
    touch ("walk/out/meson-private/coredata.dat");
    touch ("walk/out/obj.o");
    files = project_list_files (root);
    g_assert_nonnull (files);
    g_assert_cmpuint (files->len, ==, 2);
    g_assert_cmpstr (g_ptr_array_index (files, 0), ==, "a/x.h");
    g_assert_cmpstr (g_ptr_array_index (files, 1), ==, "b.c");
    g_ptr_array_free (files, TRUE);
    g_free (root);
}

static void
test_match_score (void)
{
    const int name = project_match_score ("src/editor/editwidget.c", "edwi");
    const int deep = project_match_score ("src/editor/editwidget.c", "srced");

    g_assert_cmpint (name, >, 0);
    g_assert_cmpint (deep, >, 0);
    g_assert_cmpint (project_match_score ("src/editor/editwidget.c", "xyz"), ==, 0);
    // in the last part of the name is better than across the directories
    g_assert_cmpint (project_match_score ("src/calc.c", "calc"), >,
                     project_match_score ("calc/src/main.c", "calc"));
    // the start of the name is better than inside it
    g_assert_cmpint (project_match_score ("lib/main.c", "ma"), >,
                     project_match_score ("lib/format.c", "ma"));
    // with a slash, the whole name
    g_assert_cmpint (project_match_score ("src/editor/edit.c", "editor/ed"), >, 0);
    g_assert_cmpint (project_match_score ("a.c", ""), >, 0);
    // the best fit of the letters, not the first one: editwidget before a long name
    g_assert_cmpint (
        project_match_score ("src/editor/editwidget.c", "edwi"), >,
        project_match_score ("tests/src/editor/edit_complete_word_cmd_test_data.txt.in", "edwi"));
    g_assert_cmpint (project_match_score ("a/b_c_d_e_f_g_h.c", "ah"), >, 0);
}

static void
test_alternate (void)
{
    char *root = at ("alt");
    char *source, *other, *expected;
    GPtrArray *files;

    touch ("alt/src/calc.c");
    touch ("alt/include/calc.h");
    touch ("alt/src/near.c");
    touch ("alt/src/near.h");
    files = project_list_files (root);

    source = at ("alt/src/near.c");
    other = project_alternate_file (source, root, files);
    expected = at ("alt/src/near.h");
    g_assert_cmpstr (other, ==, expected);
    g_free (other);
    g_free (expected);
    g_free (source);

    source = at ("alt/src/calc.c");
    other = project_alternate_file (source, root, files);
    expected = at ("alt/include/calc.h");
    g_assert_cmpstr (other, ==, expected);
    g_free (other);
    g_free (expected);
    g_free (source);

    source = at ("alt/include/calc.h");
    other = project_alternate_file (source, root, files);
    expected = at ("alt/src/calc.c");
    g_assert_cmpstr (other, ==, expected);
    g_free (other);
    g_free (expected);
    g_free (source);

    g_assert_null (project_alternate_file ("/x/readme.txt", root, files));
    g_ptr_array_free (files, TRUE);
    g_free (root);
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
    top = g_dir_make_tmp ("project-core-XXXXXX", NULL);
    g_assert_nonnull (top);
    g_test_add_func ("/project/find-root", test_find_root);
    g_test_add_func ("/project/list-files", test_list_files);
    g_test_add_func ("/project/match-score", test_match_score);
    g_test_add_func ("/project/alternate", test_alternate);
    result = g_test_run ();
    remove_tree (top);
    g_free (top);
    return result;
}
