/*
   src/plugins/lsp - tests for the configuration of the language servers

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

#include "src/plugins/lsp/lsp-config.h"

/* --------------------------------------------------------------------------------------------- */

static char *top;

/* A file under @top, with its directories, written with @text */
static char *
make (const char *relative, const char *text)
{
    char *path = g_build_filename (top, relative, (char *) NULL);
    char *dir = g_path_get_dirname (path);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
    if (text != NULL)
        g_assert_true (g_file_set_contents (path, text, -1, NULL));
    g_free (dir);
    return path;
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

static void
test_parse (void)
{
    GError *error = NULL;
    lsp_config_t *config;
    const lsp_backend_t *backend;
    const char *language = NULL;
    JsonNode *setting;

    config = lsp_config_parse (
        "{\"autostart\":\"always\",\"completion_on_trigger\":true,"
        "\"completion_on_arguments\":\"ide\",\"backends\":["
        "{\"id\":\"sh\",\"command\":[\"sh\",\"-c\",\"true\"],"
        "\"extensions\":{\".c\":\"c\",\".tar.c\":\"tarc\",\".C\":\"cpp\"},"
        "\"root_markers\":[\"*.proj\"],\"single_file\":false,\"autostart\":\"never\","
        "\"environment\":{\"A\":\"1\",\"B\":null},"
        "\"settings\":{\"a\":{\"b\":42}}},"
        "{\"id\":\"missing\",\"command\":[\"/no/such/"
        "program\"],\"extensions\":{\".py\":\"python\"}}"
        "]}",
        -1, &error);
    g_assert_no_error (error);
    g_assert_nonnull (config);
    g_assert_cmpint (config->autostart, ==, LSP_WHEN_ALWAYS);
    g_assert_cmpint (config->completion_on_trigger, ==, LSP_WHEN_ALWAYS);
    g_assert_cmpint (config->completion_on_arguments, ==, LSP_WHEN_IDE);
    g_assert_cmpuint (config->backends->len, ==, 2);

    backend = lsp_config_backend_for (config, "/x/y.c", &language);
    g_assert_nonnull (backend);
    g_assert_cmpstr (backend->id, ==, "sh");
    g_assert_cmpstr (language, ==, "c");
    g_assert_cmpint (lsp_backend_autostart (config, backend), ==, LSP_WHEN_NEVER);
    g_assert_false (backend->single_file);
    // the longest end counts, the case too
    g_assert_nonnull (lsp_config_backend_for (config, "/x/y.tar.c", &language));
    g_assert_cmpstr (language, ==, "tarc");
    g_assert_nonnull (lsp_config_backend_for (config, "/x/Y.C", &language));
    g_assert_cmpstr (language, ==, "cpp");
    // a backend whose program is not there serves nothing
    g_assert_null (lsp_config_backend_for (config, "/x/y.py", &language));
    g_assert_null (lsp_config_backend_for (config, "/x/y.txt", &language));

    setting = lsp_settings_lookup (backend->settings, "a.b");
    g_assert_nonnull (setting);
    g_assert_cmpint (json_node_get_int (setting), ==, 42);
    g_assert_null (lsp_settings_lookup (backend->settings, "a.c"));
    g_assert_null (lsp_settings_lookup (backend->settings, "a.b.c"));
    g_assert_true (lsp_settings_lookup (backend->settings, NULL) == backend->settings);
    lsp_config_free (config);

    // what is wrong is said, and nothing taken
    {
        static const char *const wrong[] = {
            "[]",
            "{\"autostart\":\"sometimes\"}",
            "{\"completion_on_trigger\":1}",
            "{\"backends\":{}}",
            "{\"backends\":[{\"command\":[\"x\"]}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[]}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[1]}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"],\"extensions\":{\".c\":1}}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"],\"startup\":1}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"],\"ascii_text\":\"js\"}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"],\"words\":\"const\"}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"],\"requests\":{}}]}",
            "{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"],\"requests\":[{\"params\":1}]}]}",
            // two backends of one id
            ("{\"backends\":[{\"id\":\"x\",\"command\":[\"x\"]},"
             "{\"id\":\"x\",\"command\":[\"y\"]}]}"),
            "{",
        };
        gsize i;

        for (i = 0; i < G_N_ELEMENTS (wrong); i++)
        {
            g_assert_null (lsp_config_parse (wrong[i], -1, &error));
            g_assert_nonnull (error);
            g_clear_error (&error);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A backend of the user takes the place of the system's one, and comes first */
static void
test_load (void)
{
    char *system = make ("etc/lsp.json",
                         "{\"autostart\":\"never\",\"backends\":["
                         "{\"id\":\"a\",\"command\":[\"sh\"],\"extensions\":{\".a\":\"a\"}},"
                         "{\"id\":\"b\",\"command\":[\"sh\"],\"extensions\":{\".x\":\"bx\"}}]}");
    char *user = make ("home/lsp.json",
                       "{\"backends\":["
                       "{\"id\":\"b\",\"command\":[\"sh\"],\"extensions\":{\".b\":\"b\"}},"
                       "{\"id\":\"c\",\"command\":[\"sh\"],\"extensions\":{\".a\":\"ca\"}}]}");
    char *broken = make ("home/broken.json", "{\"backends\":");
    GString *errors = g_string_new (NULL);
    lsp_config_t *config;
    const char *language;

    config = lsp_config_load (system, user, errors);
    g_assert_cmpstr (errors->str, ==, "");
    g_assert_cmpuint (config->backends->len, ==, 3);
    g_assert_cmpstr (((lsp_backend_t *) g_ptr_array_index (config->backends, 0))->id, ==, "b");
    g_assert_cmpstr (((lsp_backend_t *) g_ptr_array_index (config->backends, 1))->id, ==, "c");
    g_assert_cmpstr (((lsp_backend_t *) g_ptr_array_index (config->backends, 2))->id, ==, "a");
    // the user's file says no autostart: the system's
    g_assert_cmpint (config->autostart, ==, LSP_WHEN_NEVER);
    // nor completion_on_trigger: the default of both files, on a project of -D
    g_assert_cmpint (config->completion_on_trigger, ==, LSP_WHEN_IDE);
    // nor completion_on_arguments: never, a list at each call being in the way
    g_assert_cmpint (config->completion_on_arguments, ==, LSP_WHEN_NEVER);
    g_assert_cmpstr (lsp_config_backend_for (config, "/f.a", &language)->id, ==, "c");
    g_assert_null (lsp_config_backend_for (config, "/f.x", &language));
    lsp_config_free (config);

    // a broken file of the user leaves the system's
    config = lsp_config_load (system, broken, errors);
    g_assert_nonnull (strstr (errors->str, "broken.json"));
    g_assert_cmpuint (config->backends->len, ==, 2);
    g_assert_cmpint (config->autostart, ==, LSP_WHEN_NEVER);
    lsp_config_free (config);

    // no file at all: nothing to serve
    config = lsp_config_load (NULL, "/no/such/lsp.json", NULL);
    g_assert_cmpuint (config->backends->len, ==, 0);
    lsp_config_free (config);

    g_string_free (errors, TRUE);
    g_free (system);
    g_free (user);
    g_free (broken);
}

/* --------------------------------------------------------------------------------------------- */

static void
check_root (const lsp_backend_t *backend, const char *file, const char *project,
            const char *expected)
{
    char *path = g_build_filename (top, file, (char *) NULL);
    char *want = expected != NULL ? g_build_filename (top, expected, (char *) NULL) : NULL;
    char *project_path = project != NULL ? g_build_filename (top, project, (char *) NULL) : NULL;
    char *root = lsp_find_root (backend, path, project_path);

    g_assert_cmpstr (root, ==, want);
    g_free (root);
    g_free (project_path);
    g_free (want);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_root (void)
{
    lsp_config_t *config;
    const lsp_backend_t *backend;

    config = lsp_config_parse ("{\"backends\":[{\"id\":\"x\",\"command\":[\"sh\"],"
                               "\"root_markers\":[\"compile_commands.json\",\"*.proj\"]}]}",
                               -1, NULL);
    backend = g_ptr_array_index (config->backends, 0);

    g_free (make ("r/vcs/.git/HEAD", "x"));
    g_free (make ("r/vcs/sub/compile_commands.json", "[]"));
    g_free (make ("r/vcs/sub/deep/f.c", NULL));
    g_free (make ("r/vcs/pat/a.proj", "x"));
    g_free (make ("r/vcs/pat/in/f.c", NULL));
    g_free (make ("r/vcs/other/f.c", NULL));
    g_free (make ("r/coole/.coole", "x"));
    g_free (make ("r/coole/sub/compile_commands.json", "[]"));
    g_free (make ("r/coole/sub/f.c", NULL));
    g_free (make ("r/alone/f.c", NULL));

    // a marker of the backend before the repository
    check_root (backend, "r/vcs/sub/deep/f.c", NULL, "r/vcs/sub");
    check_root (backend, "r/vcs/pat/in/f.c", NULL, "r/vcs/pat");
    check_root (backend, "r/vcs/other/f.c", NULL, "r/vcs");
    // .coole before a marker nearer to the file
    check_root (backend, "r/coole/sub/f.c", NULL, "r/coole");
    // the project of -D before all
    check_root (backend, "r/coole/sub/f.c", "r", "r");
    // a project that does not hold the file is no root of it, nor one with a longer name
    check_root (backend, "r/vcs/other/f.c", "r/vc", "r/vcs");
    check_root (backend, "r/vcs/other/f.c", "r/coole", "r/vcs");
    lsp_config_free (config);

    // a single file: its directory, or none.  Under the temporary directory there may be a
    // repository of the system; then this part says nothing.
    config = lsp_config_parse ("{\"backends\":[{\"id\":\"x\",\"command\":[\"sh\"]},"
                               "{\"id\":\"y\",\"command\":[\"sh\"],\"single_file\":false}]}",
                               -1, NULL);
    {
        char *dir = g_build_filename (top, "r", "alone", (char *) NULL);
        char *outside = lsp_find_root (g_ptr_array_index (config->backends, 1), dir, NULL);

        if (outside == NULL)
        {
            check_root (g_ptr_array_index (config->backends, 0), "r/alone/f.c", NULL, "r/alone");
            check_root (g_ptr_array_index (config->backends, 1), "r/alone/f.c", NULL, NULL);
        }
        g_free (outside);
        g_free (dir);
    }
    lsp_config_free (config);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_command (void)
{
    char *command[] = { (char *) "clangd", (char *) "--compile-commands-dir=${build_dir}",
                        (char *) "${build_dir}/x:${build_dir}", (char *) "-j", NULL };
    char **expanded;

    expanded = lsp_command_expand (command, "/b");
    g_assert_cmpuint (g_strv_length (expanded), ==, 4);
    g_assert_cmpstr (expanded[1], ==, "--compile-commands-dir=/b");
    g_assert_cmpstr (expanded[2], ==, "/b/x:/b");
    g_assert_cmpstr (expanded[3], ==, "-j");
    g_strfreev (expanded);
    // no build directory: the arguments that name it are left out
    expanded = lsp_command_expand (command, NULL);
    g_assert_cmpuint (g_strv_length (expanded), ==, 2);
    g_assert_cmpstr (expanded[0], ==, "clangd");
    g_assert_cmpstr (expanded[1], ==, "-j");
    g_strfreev (expanded);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
started_query (const char *option, void *data)
{
    (void) data;
    return strcmp (option, "query") == 0;
}

static gboolean
started_none (const char *option, void *data)
{
    (void) option;
    (void) data;
    return FALSE;
}

static void
test_startup (void)
{
    static const char *json =
        "{\"backends\":["
        "{\"id\":\"ts\",\"command\":[\"sh\"],\"extensions\":{\".js\":\"javascript\"}},"
        "{\"id\":\"mongo\",\"command\":[\"sh\"],\"extensions\":{\".js\":\"javascript\"},"
        "\"startup\":\"query\",\"words\":[\"const\",\"print\"],\"ascii_text\":\"sql\","
        "\"requests\":[{\"method\":\"A\",\"params\":{\"x\":1}},"
        "{\"method\":\"B\"}]}]}";
    GError *error = NULL;
    lsp_config_t *config;
    const lsp_backend_t *backend;

    // coole -Q: the backend of the option first
    config = lsp_config_parse (json, -1, &error);
    g_assert_no_error (error);
    lsp_config_for_startup (config, started_query, NULL);
    backend = lsp_config_backend_for (config, "/q/a.js", NULL);
    g_assert_cmpstr (backend->id, ==, "mongo");
    g_assert_cmpuint (json_array_get_length (json_node_get_array (backend->requests)), ==, 2);
    g_assert_cmpuint (g_strv_length (backend->words), ==, 2);
    g_assert_cmpstr (backend->words[1], ==, "print");
    g_assert_cmpstr (backend->ascii_text, ==, "sql");
    lsp_config_free (config);

    // started otherwise: it serves nothing
    config = lsp_config_parse (json, -1, &error);
    g_assert_no_error (error);
    lsp_config_for_startup (config, started_none, NULL);
    backend = lsp_config_backend_for (config, "/q/a.js", NULL);
    g_assert_cmpstr (backend->id, ==, "ts");
    lsp_config_free (config);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_expand (void)
{
    JsonNode *node, *expanded;
    char *text;

    g_setenv ("LSP_TEST_URI", "mongodb://u:p@h/", TRUE);
    g_unsetenv ("LSP_TEST_NONE");
    node = json_from_string ("{\"a\":\"${env:LSP_TEST_URI}\",\"b\":[\"${command_dir}/..\",1,null],"
                             "\"c\":\"<${env:LSP_TEST_NONE}>\",\"d\":\"${other} $x\",\"e\":{}}",
                             NULL);
    expanded = lsp_config_expand (node, "/opt/m/bin");
    text = json_to_string (expanded, FALSE);
    g_assert_cmpstr (text, ==,
                     "{\"a\":\"mongodb://u:p@h/\",\"b\":[\"/opt/m/bin/..\",1,null],"
                     "\"c\":\"<>\",\"d\":\"${other} $x\",\"e\":{}}");
    g_free (text);
    json_node_unref (expanded);
    json_node_unref (node);
    g_assert_null (lsp_config_expand (NULL, NULL));
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    int result;

    g_test_init (&argc, &argv, NULL);
    top = g_dir_make_tmp ("lsp-config-XXXXXX", NULL);
    g_assert_nonnull (top);
    g_test_add_func ("/lsp/config-parse", test_parse);
    g_test_add_func ("/lsp/config-load", test_load);
    g_test_add_func ("/lsp/config-root", test_root);
    g_test_add_func ("/lsp/config-command", test_command);
    g_test_add_func ("/lsp/config-startup", test_startup);
    g_test_add_func ("/lsp/config-expand", test_expand);
    result = g_test_run ();
    remove_tree (top);
    g_free (top);
    return result;
}
