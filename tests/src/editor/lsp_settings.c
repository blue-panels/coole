/*
   src/plugins/lsp - tests for the settings of the language servers as the dialog changes them

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

#include "src/plugins/lsp/lsp-settings.h"

/* --------------------------------------------------------------------------------------------- */

static char *top;

static char *
write_file (const char *name, const char *text)
{
    char *path = g_build_filename (top, name, (char *) NULL);

    if (text != NULL)
        g_assert_true (g_file_set_contents (path, text, -1, NULL));
    else
        (void) g_remove (path);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

static JsonObject *
backend (const char *json)
{
    JsonNode *node = json_from_string (json, NULL);
    JsonObject *object = json_object_ref (json_node_get_object (node));

    json_node_unref (node);
    return object;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_settings (void)
{
    char *system =
        write_file ("system.json",
                    "{\"autostart\":\"never\",\"backends\":["
                    "{\"id\":\"a\",\"command\":[\"a\"],\"extensions\":{\".a\":\"a\"}},"
                    "{\"id\":\"b\",\"command\":[\"b\"],\"extensions\":{\".b\":\"b\"}}]}");
    char *user = write_file ("sub/user.json", NULL);
    char *text = NULL;
    lsp_settings_t *settings;
    lsp_config_t *config;
    GPtrArray *ids;
    GError *error = NULL;

    // no file of the user yet: the system's
    settings = lsp_settings_load (system, user, &error);
    g_assert_no_error (error);
    g_assert_cmpint (lsp_settings_when (settings, "autostart"), ==, LSP_WHEN_NEVER);
    g_assert_cmpint (lsp_settings_when (settings, "completion_on_trigger"), ==, LSP_WHEN_IDE);
    // a file that does not say it has it never
    g_assert_cmpint (lsp_settings_when (settings, "completion_on_arguments"), ==, LSP_WHEN_NEVER);
    ids = lsp_settings_ids (settings);
    g_assert_cmpuint (ids->len, ==, 2);
    g_ptr_array_free (ids, TRUE);
    g_assert_true (lsp_settings_in_system (settings, "a"));
    g_assert_false (lsp_settings_changed (settings, "a"));

    // what is as the system has it is not written
    lsp_settings_set_when (settings, "autostart", LSP_WHEN_NEVER);
    lsp_settings_set_when (settings, "completion_on_arguments", LSP_WHEN_NEVER);
    lsp_settings_put (settings,
                      backend ("{\"id\":\"a\",\"command\":[\"a\"],"
                               "\"extensions\":{\".a\":\"a\"}}"));
    g_assert_false (lsp_settings_changed (settings, "a"));
    // what is not, is
    lsp_settings_set_when (settings, "completion_on_trigger", LSP_WHEN_ALWAYS);
    lsp_settings_put (settings,
                      backend ("{\"id\":\"b\",\"command\":[\"b\",\"-x\"],"
                               "\"extensions\":{\".b\":\"b\"},\"enabled\":false}"));
    lsp_settings_put (settings,
                      backend ("{\"id\":\"c\",\"command\":[\"sh\"],"
                               "\"extensions\":{\".c\":\"c\"}}"));
    g_assert_true (lsp_settings_changed (settings, "b"));
    g_assert_false (lsp_settings_changed (settings, "c"));
    g_assert_false (lsp_settings_in_system (settings, "c"));
    g_assert_true (lsp_settings_save (settings, &error));
    g_assert_no_error (error);
    lsp_settings_free (settings);

    g_assert_true (g_file_get_contents (user, &text, NULL, NULL));
    g_assert_null (strstr (text, "autostart"));
    g_assert_null (strstr (text, "completion_on_arguments"));
    g_assert_null (strstr (text, "\"a\""));
    g_assert_nonnull (strstr (text, "\"always\""));
    g_free (text);

    // the configuration has it so
    config = lsp_config_load (system, user, NULL);
    g_assert_cmpint (config->autostart, ==, LSP_WHEN_NEVER);
    g_assert_cmpint (config->completion_on_trigger, ==, LSP_WHEN_ALWAYS);
    g_assert_false (lsp_config_backend (config, "b")->enabled);
    g_assert_nonnull (lsp_config_backend (config, "c"));
    // a disabled backend serves nothing
    g_assert_null (lsp_config_backend_for (config, "/x/f.b", NULL));
    lsp_config_free (config);

    // again: the user's first, a change in its place, and back to the system's
    settings = lsp_settings_load (system, user, &error);
    ids = lsp_settings_ids (settings);
    g_assert_cmpstr (g_ptr_array_index (ids, 0), ==, "b");
    g_assert_cmpstr (g_ptr_array_index (ids, 1), ==, "c");
    g_assert_cmpstr (g_ptr_array_index (ids, 2), ==, "a");
    g_ptr_array_free (ids, TRUE);
    lsp_settings_put (settings,
                      backend ("{\"id\":\"b\",\"command\":[\"b\",\"-y\"],"
                               "\"extensions\":{\".b\":\"b\"}}"));
    ids = lsp_settings_ids (settings);
    g_assert_cmpstr (g_ptr_array_index (ids, 0), ==, "b");
    g_ptr_array_free (ids, TRUE);
    lsp_settings_drop (settings, "b");
    lsp_settings_drop (settings, "c");
    g_assert_false (lsp_settings_changed (settings, "b"));
    g_assert_null (lsp_settings_backend (settings, "c"));
    g_assert_true (lsp_settings_save (settings, NULL));
    lsp_settings_free (settings);
    g_assert_true (g_file_get_contents (user, &text, NULL, NULL));
    g_assert_null (strstr (text, "backends"));
    g_free (text);

    // a broken file of the user is not written over
    g_free (write_file ("sub/user.json", "{\"backends\":"));
    g_assert_null (lsp_settings_load (system, user, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    (void) g_remove (user);
    (void) g_remove (system);
    g_free (user);
    g_free (system);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    int result;
    char *sub;

    g_test_init (&argc, &argv, NULL);
    top = g_dir_make_tmp ("lsp-settings-XXXXXX", NULL);
    sub = g_build_filename (top, "sub", (char *) NULL);
    g_test_add_func ("/lsp/settings", test_settings);
    result = g_test_run ();
    (void) g_rmdir (sub);
    (void) g_rmdir (top);
    g_free (sub);
    g_free (top);
    return result;
}
