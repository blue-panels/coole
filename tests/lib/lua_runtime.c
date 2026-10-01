/*
   lib - tests for the Lua runtime extension

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

   This file is part of coole, a text editor based on GNU Midnight Commander.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/lib"

#include "tests/mctest.h"

#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "lib/event.h"
#include "lib/fileloc.h"
#include "lib/extension-runtime.h"
#include "lib/plugin-service.h"
#include "lib/runtime-events.h"
#include "lib/strutil.h"

/*** global variables ****************************************************************************/

static GError *error = NULL;
static char *test_root = NULL;
static char *config_dir = NULL;
static char *data_dir = NULL;
static char *system_scripts_dir = NULL;
static char *user_scripts_dir = NULL;
static char *system_editor_scripts_dir = NULL;
static char *system_modules_dir = NULL;
static char *user_modules_dir = NULL;
static char *user_editor_scripts_dir = NULL;
static char *output_path = NULL;
static char *ui_status_text = NULL;
static char *ui_message_title = NULL;
static char *ui_message_text = NULL;
static guint ui_dialog_count = 0;
static char *runtime_error_runtime = NULL;
static char *runtime_error_package = NULL;
static char *runtime_error_summary = NULL;
static char *runtime_error_details = NULL;
static mc_runtime_error_phase_t runtime_error_phase;
static guint runtime_error_count = 0;
static char *object_editor_insert_text = NULL;
static char *object_editor_replacement = NULL;
static char *object_editor_range_replacement = NULL;
static char *process_shell_command = NULL;
static guint64 object_editor_range_from = 0;
static guint64 object_editor_range_to = 0;
static guint64 object_editor_line = 0;
static guint64 object_editor_column = 0;
static guint64 object_editor_typed_revision = 0;
static guint object_editor_typed_changes = 0;
static guint64 object_editor_selection_revision = 0;
static guint enumerated_lua_packages = 0;
static guint enumerated_lua_runtimes = 0;
static gboolean enumerated_disabled_beta = FALSE;
static guint enumerated_lua_all_packages = 0;
static guint enumerated_lua_editor_packages = 0;
static gboolean enumerated_lua_editor_global = FALSE;
static gboolean enumerated_lua_editor_user = FALSE;
static guint enumerated_lua_actions = 0;
static gboolean enumerated_lua_consume_action = FALSE;
static guint enumerated_lua_menu_actions = 0;
static gboolean enumerated_lua_drawing_action = FALSE;
static guint screen_run_count = 0;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

static void
remove_tree (const char *path)
{
    GDir *directory;
    const char *entry;

    directory = g_dir_open (path, 0, NULL);
    if (directory == NULL)
    {
        (void) g_remove (path);
        return;
    }

    while ((entry = g_dir_read_name (directory)) != NULL)
    {
        char *child = g_build_filename (path, entry, (char *) NULL);

        if (g_file_test (child, G_FILE_TEST_IS_DIR))
            remove_tree (child);
        else
            (void) g_remove (child);
        g_free (child);
    }

    g_dir_close (directory);
    (void) g_rmdir (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
write_file (const char *path, const char *contents)
{
    mctest_assert_true (g_file_set_contents (path, contents, -1, &error));
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

/* A screen host that pulls two pages, presses a key, runs an action, and closes. */
static gboolean
test_screen_run (mc_runtime_plugin_context_t *context,
                 const mc_runtime_screen_descriptor_t *descriptor, const char **screen_error)
{
    mc_runtime_screen_request_t request = { .struct_size = sizeof (request) };
    mc_runtime_screen_response_t response;
    const mc_runtime_screen_table_t *table = NULL;

    (void) screen_error;
    ck_assert_uint_ge (descriptor->struct_size, sizeof (*descriptor));
    ck_assert_str_eq (descriptor->spec->title, "Screen test");
    ck_assert_str_eq (descriptor->spec->help_node, "[Test]");
    ck_assert_str_eq (descriptor->spec->status, "ready");
    ck_assert_int_eq (descriptor->spec->palette, MC_RUNTIME_SCREEN_PALETTE_EDITOR);
    ck_assert_uint_eq (descriptor->spec->rows_count, 2);
    ck_assert_uint_eq (descriptor->spec->rows[0].height, 1);
    ck_assert_uint_eq (descriptor->spec->rows[0].cells_count, 1);
    ck_assert_int_eq (descriptor->spec->rows[0].cells[0].control->type, MC_RUNTIME_DIALOG_LABEL);
    ck_assert_uint_eq (descriptor->spec->rows[1].weight, 1);
    ck_assert_uint_eq (descriptor->spec->rows[1].cells_count, 2);
    ck_assert_int_eq (descriptor->spec->rows[1].cells[0].control->type, MC_RUNTIME_DIALOG_TABLE);
    ck_assert_uint_eq (descriptor->spec->rows[1].cells[0].weight, 2);
    ck_assert_int_eq (descriptor->spec->rows[1].cells[1].control->type, MC_RUNTIME_DIALOG_TEXT);
    ck_assert_uint_eq (descriptor->spec->rows[1].cells[1].width, 20);
    ck_assert_str_eq (descriptor->spec->rows[1].cells[1].control->text, "card");
    ck_assert_uint_eq (descriptor->spec->keys_count, 2);
    ck_assert_str_eq (descriptor->spec->keys[0].key, "f2");
    ck_assert_str_eq (descriptor->spec->keys[0].action, "hello");
    table = descriptor->spec->rows[1].cells[0].control->table;
    ck_assert_ptr_nonnull (table);
    ck_assert_uint_eq (table->columns_count, 2);
    ck_assert_str_eq (table->columns[1].id, "n");
    ck_assert_int_eq (table->columns[1].align, MC_RUNTIME_SCREEN_ALIGN_RIGHT);
    ck_assert_int_eq (table->row_count, 5);

    /* rows 0..2: the cells, a colored one among them */
    request.control_id = descriptor->spec->rows[1].cells[0].control->id;
    request.first = 0;
    request.count = 3;
    memset (&response, 0, sizeof (response));
    response.struct_size = sizeof (response);
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_ROWS,
                                     &request, &response, NULL));
    ck_assert_uint_eq (response.rows_count, 3);
    ck_assert_uint_eq (response.columns_count, 2);
    ck_assert_str_eq (response.cells[0].text, "row 0");
    ck_assert_str_eq (response.cells[1].text, "0");
    ck_assert_str_eq (response.cells[4].text, "row 2");
    ck_assert_str_eq (response.cells[5].color, "red");
    descriptor->response_free (context, &response);

    /* the last page comes back short */
    request.first = 3;
    request.count = 3;
    memset (&response, 0, sizeof (response));
    response.struct_size = sizeof (response);
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_ROWS,
                                     &request, &response, NULL));
    ck_assert_uint_eq (response.rows_count, 2);
    descriptor->response_free (context, &response);

    /* a key the script takes, one it does not */
    request.control_id = descriptor->spec->rows[1].cells[0].control->id;
    request.row = 1;
    request.column = 0;
    request.key = 'x';
    request.key_name = "x";
    memset (&response, 0, sizeof (response));
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_KEY,
                                     &request, &response, NULL));
    ck_assert (response.handled);
    request.key = 'y';
    request.key_name = "y";
    memset (&response, 0, sizeof (response));
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_KEY,
                                     &request, &response, NULL));
    ck_assert (!response.handled);

    /* Enter on row 4, then the action that asks to close */
    request.row = 4;
    memset (&response, 0, sizeof (response));
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_ENTER,
                                     &request, &response, NULL));
    request.action = "hello";
    memset (&response, 0, sizeof (response));
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_ACTION,
                                     &request, &response, NULL));
    ck_assert (response.close);
    memset (&response, 0, sizeof (response));
    ck_assert (descriptor->dispatch (context, descriptor->screen_id, MC_RUNTIME_SCREEN_CLOSE,
                                     &request, &response, NULL));
    screen_run_count++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A package that owns a settings dialog: the settings hook makes it
   configurable from Manage Plugins. */
static void
create_settings_script (void)
{
    char *root = g_build_filename (user_editor_scripts_dir, "with-settings", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    char *mark_path = g_build_filename (root, "shown.txt", (char *) NULL);
    char *help_path = g_build_filename (root, "help.md", (char *) NULL);
    char *script;

    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    write_file (ini_path,
                "[Lua]\nid=with-settings\napi_version=1\nname=With settings\nentry=init.lua\n");
    write_file (help_path, "[Probe]\n\nThe settings of the probe.\n");
    script = g_strdup_printf ("assert(mc.settings(function()\n"
                              "  local f = assert(io.open('%s', 'a'))\n"
                              "  f:write('shown\\n')\n"
                              "  f:close()\n"
                              "  mc.ui.dialog {\n"
                              "    title = 'Settings probe',\n"
                              "    help = { file = 'help.md', node = '[Probe]' },\n"
                              "    controls = {{ id = 'ok', type = 'button', label = '&OK',\n"
                              "                  default = true }},\n"
                              "  }\n"
                              "end))\n",
                              mark_path);
    write_file (entry_path, script);
    g_free (script);
    g_free (help_path);
    g_free (mark_path);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_script (const char *parent, const char *id, const char *label, gboolean unregister)
{
    char *root;
    char *library_dir;
    char *ini_path;
    char *entry_path;
    char *module_path;
    char *ini;
    char *script;
    char *module;

    root = g_build_filename (parent, id, (char *) NULL);
    library_dir = g_build_filename (root, "lib", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (library_dir, 0700), 0);

    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    module_path = g_build_filename (library_dir, "format.lua", (char *) NULL);
    ini = g_strdup_printf ("[Lua]\nid=%s\napi_version=1\nname=%s\nentry=init.lua\n", id, id);

    if (unregister)
        script = g_strdup_printf ("local token = mc.on(\"startup\", function (ev)\n"
                                  "    local stream = assert(io.open(\"%s\", \"a\"))\n"
                                  "    stream:write(\"%s:\" .. ev.data_dir .. \"\\n\")\n"
                                  "    stream:close()\n"
                                  "end)\n"
                                  "assert(mc.off(token))\n",
                                  output_path, label);
    else
        script = g_strdup_printf ("local label = require(\"format\")\n"
                                  "mc.on(\"startup\", function (ev)\n"
                                  "    local stream = assert(io.open(\"%s\", \"a\"))\n"
                                  "    stream:write(label .. \":\" .. ev.data_dir .. \"\\n\")\n"
                                  "    stream:close()\n"
                                  "end)\n",
                                  output_path);

    write_file (ini_path, ini);
    write_file (entry_path, script);
    module = g_strdup_printf ("return \"%s\"\n", label);
    write_file (module_path, module);

    g_free (module);
    g_free (script);
    g_free (ini);
    g_free (module_path);
    g_free (entry_path);
    g_free (ini_path);
    g_free (library_dir);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_editor_script (const char *parent, const char *id)
{
    char *root;
    char *ini_path;
    char *entry_path;
    char *ini;

    root = g_build_filename (parent, id, (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    ini = g_strdup_printf ("[Lua]\nid=%s\napi_version=1\nname=%s\nentry=init.lua\n", id, id);
    write_file (ini_path, ini);
    write_file (entry_path, "mc.on(\"editor.save\", function (ev) end)\n");

    g_free (ini);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_test_packages (void)
{
    (void) g_remove (output_path);

    create_script (system_editor_scripts_dir, "alpha", "system-alpha", FALSE);
    create_script (user_editor_scripts_dir, "alpha", "user-alpha", FALSE);
    create_script (user_editor_scripts_dir, "beta", "user-beta", FALSE);
    create_script (user_editor_scripts_dir, "off", "off", TRUE);
    create_editor_script (system_editor_scripts_dir, "editor-global");
    create_editor_script (user_editor_scripts_dir, "editor-user");
}

/* --------------------------------------------------------------------------------------------- */

static void
create_event_shape_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;
    char *script;

    root = g_build_filename (user_editor_scripts_dir, "event-shapes", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=event-shapes\n"
                "api_version=1\n"
                "name=Event snapshot checks\n"
                "entry=init.lua\n");

    script = g_strdup_printf (
        "if os.getenv(\"MC_LUA_TEST_EVENT_SHAPES\") ~= \"1\" then return end\n"
        "local function record(value)\n"
        "    local stream = assert(io.open(\"%s\", \"a\"))\n"
        "    stream:write(value .. \"\\n\")\n"
        "    stream:close()\n"
        "end\n"
        "local none, none_error = mc.on(\"panel.chdir\", function() end)\n"
        "assert(none == nil and none_error:find(\"unknown event\"))\n"
        "none, none_error = mc.on(\"viewer.open\", function() end)\n"
        "assert(none == nil and none_error:find(\"unknown event\"))\n"
        "mc.on(\"startup\", function(ev)\n"
        "    assert(ev.run_mode == nil and ev.config_dir ~= nil and ev.data_dir == \"data\")\n"
        "    record(\"startup\")\n"
        "end)\n"
        "mc.on(\"shutdown\", function(ev)\n"
        "    assert(ev.reason == \"quit\")\n"
        "    record(\"shutdown\")\n"
        "end)\n"
        "mc.on(\"editor.open\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.path == \"/new/edit\")\n"
        "    assert(ev.readonly and ev.line == 4 and ev.column == 9)\n"
        "    record(\"editor-open\")\n"
        "end)\n"
        "mc.on(\"editor.save\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.path == \"/new/edit\")\n"
        "    assert(ev.previous_path == \"/old/edit\" and ev.save_as)\n"
        "    record(\"editor-save\")\n"
        "end)\n"
        "mc.on(\"editor.key\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.key.name == \"Ctrl-S\")\n"
        "    assert(ev.key.code == 19 and ev.key.text == nil and ev.key.modifiers.ctrl)\n"
        "    record(\"editor-key\")\n"
        "    return mc.CONSUME\n"
        "end)\n"
        "mc.on(\"editor.change\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.path == \"/new/edit\")\n"
        "    assert(ev.revision == 12)\n"
        "    record(\"editor-change\")\n"
        "end)\n"
        "mc.on(\"editor.cursor\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.path == \"/new/edit\")\n"
        "    assert(ev.line == 7 and ev.column == 3)\n"
        "    record(\"editor-cursor\")\n"
        "end)\n",
        output_path);
    write_file (entry_path, script);

    g_free (script);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_ui_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;

    root = g_build_filename (user_editor_scripts_dir, "ui-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=ui-test\n"
                "api_version=1\n"
                "name=UI host service check\n"
                "entry=init.lua\n");
    write_file (
        entry_path,
        "if os.getenv(\"MC_LUA_TEST_UI\") ~= \"1\" then return end\n"
        "mc.on(\"startup\", function()\n"
        "    assert(mc.ui.status(\"Lua status\"))\n"
        "    assert(mc.ui.message(\"Lua title\", \"Lua message\"))\n"
        "    assert(mc.ui.indicator { id = \"ui-test\", area = \"editor\", text = \"[UI]\", "
        "priority = 25 })\n"
        "end)\n"
        "mc.on(\"editor.key\", function()\n"
        "    local result = assert(mc.ui.dialog {\n"
        "        title = \"Base64 tools\",\n"
        "        controls = {\n"
        "            { type = \"label\", text = \"Selected text\" },\n"
        "            { id = \"operation\", type = \"select\", label = \"Operation\", value = "
        "\"decode\", options = {\n"
        "                { id = \"decode\", label = \"Decode\" },\n"
        "                { id = \"encode\", label = \"Encode\" },\n"
        "            } },\n"
        "            { id = \"line_width\", type = \"input\", value = \"0\",\n"
        "              history = \"lua-test-input\",\n"
        "              complete_on_tab = true,\n"
        "              completion = { \"commands\", \"files\", \"shell\" } },\n"
        "            { id = \"omit_padding\", type = \"checkbox\", label = \"Omit padding\", value "
        "= false },\n"
        "            { type = \"hbox\", controls = {\n"
        "                { id = \"run\", type = \"button\", label = \"&Run\", default = true },\n"
        "                { id = \"close\", type = \"button\", label = \"&Close\", cancel = true "
        "},\n"
        "            } },\n"
        "        },\n"
        "    })\n"
        "    assert(result.button == \"run\")\n"
        "    assert(result.values.operation == \"encode\")\n"
        "    assert(result.values.line_width == \"76\")\n"
        "    assert(result.values.omit_padding == true)\n"
        "end)\n");

    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_error_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;

    root = g_build_filename (user_editor_scripts_dir, "error-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=error-test\n"
                "api_version=1\n"
                "name=Error boundary check\n"
                "entry=init.lua\n");
    write_file (entry_path,
                "if os.getenv(\"MC_LUA_TEST_ERROR\") ~= \"1\" then return end\n"
                "mc.on(\"startup\", function() error(\"expected test error\") end)\n");

    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_object_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;

    root = g_build_filename (user_editor_scripts_dir, "object-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=object-test\n"
                "api_version=1\n"
                "name=Object API checks\n"
                "entry=init.lua\n");
    write_file (
        entry_path,
        "if os.getenv(\"MC_LUA_TEST_OBJECTS\") ~= \"1\" then return end\n"
        "local unavailable, unavailable_error = mc.editor.current()\n"
        "assert(unavailable == nil and unavailable_error == \"no active runtime context\")\n"
        "assert(mc.panel == nil and mc.viewer == nil and mc.panel_provider == nil)\n"
        "assert(mc.viewer_source == nil and mc.source == nil and mc.file_handler == nil)\n"
        "assert(mc.ui.open_viewer == nil and mc.ui.open_diff == nil)\n"
        "mc.on(\"startup\", function()\n"
        "    local editor = assert(mc.editor.current())\n"
        "    assert(editor:path() == \"/editor\")\n"
        "    local info = editor:info()\n"
        "    assert(info.path == \"/editor\" and info.name == \"editor\")\n"
        "    assert(info.modified and not info.readonly and info.revision == 7)\n"
        "    assert(info.byte_length == 4 and info.line_count == 1)\n"
        "    local selection = editor:selection()\n"
        "    assert(selection.kind == \"column\" and selection.revision == 7)\n"
        "    assert(selection.anchor.offset == 1 and selection.anchor.line == 1)\n"
        "    assert(selection.cursor.offset == 8 and selection.cursor.column == 4)\n"
        "    assert(#selection.ranges == 2)\n"
        "    assert(selection.ranges[1].from == 1 and selection.ranges[1].to == 3)\n"
        "    assert(selection.ranges[2].from == 6 and selection.ranges[2].to == 8)\n"
        "    assert(selection.text == \"bc\\ngh\" and not selection.text_truncated)\n"
        "    local edit_result = assert(editor:replace_selection(\"BC\\nGH\"))\n"
        "    assert(edit_result.revision == 8 and edit_result.cursor.offset == 5)\n"
        "    local range_result = assert(editor:replace(1, 3, \"xy\"))\n"
        "    assert(range_result.revision == 9 and range_result.cursor.offset == 3)\n"
        "    local line, column = editor:cursor()\n"
        "    assert(line == 2 and column == 3 and not editor:is_readonly())\n"
        "    assert(editor:tab_width() == 8)\n"
        "    assert(editor:get_text(1, 4) == \"text\")\n"
        "    assert(editor:text { from = 1, to = 3, revision = 7 } == \"yp\")\n"
        "    local typed = assert(editor:replace({ from = 1, to = 3, revision = 7 }, \"zz\"))\n"
        "    assert(typed.revision == 10)\n"
        "    local transaction = assert(editor:edit { revision = 10, changes = {\n"
        "        { from = 0, to = 1, text = \"A\" },\n"
        "        { from = 3, to = 4, text = \"Z\" },\n"
        "    }, cursor = { offset = 2 } })\n"
        "    assert(transaction.revision == 11 and transaction.cursor.offset == 2)\n"
        "    assert(mc.ui.text_width(\"漢\") == 2)\n"
        "    local scan = assert(mc.syntax.scan(\"int x;\", { type = \"C Program\" }))\n"
        "    assert(scan.type == \"Tested\" and #scan.runs == 1 and #scan.colors == 2)\n"
        "    assert(scan.runs[1].offset == 1 and scan.runs[1].length == 6)\n"
        "    assert(scan.colors[scan.runs[1].color].fg == \"yellow\")\n"
        "    assert(scan.colors[scan.runs[1].color].attrs == \"bold\")\n"
        "    assert(editor:selected_text() == \"U2V0\")\n"
        "    assert(editor:set_cursor(3, 4))\n"
        "    assert(editor:insert(\"!\"))\n"
        "    assert(editor:save())\n"
        "end)\n"
        "mc.on(\"editor.save\", function(ev)\n"
        "    local path, closed_error = ev.editor:path()\n"
        "    assert(path == nil and closed_error == \"closed\")\n"
        "    assert(mc.ui.status(\"save status\"))\n"
        "end)\n");

    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_macro_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;
    char *script;

    root = g_build_filename (user_editor_scripts_dir, "macro-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=macro-test\n"
                "api_version=1\n"
                "name=Macro API checks\n"
                "entry=init.lua\n"
                "provides=macros\n");

    script = g_strdup_printf (
        "if os.getenv(\"MC_LUA_TEST_MACRO\") ~= \"1\" then return end\n"
        "local function record(value)\n"
        "    local stream = assert(io.open(\"%s\", \"a\"))\n"
        "    stream:write(value .. \"\\n\")\n"
        "    stream:close()\n"
        "end\n"
        "assert(mc.macro {\n"
        "    id = \"consume-f11\",\n"
        "    area = \"editor\",\n"
        "    key = \"F11\",\n"
        "    description = \"Consume F11\",\n"
        "    action = function(ev)\n"
        "        assert(ev.name == \"editor.key\" and ev.key.name == \"F11\")\n"
        "        assert(type(ev.editor) == \"userdata\" and "
        "ev.editor:selected_text() == \"U2V0\")\n"
        "        record(\"macro-consume\")\n"
        "        return mc.CONSUME\n"
        "    end,\n"
        "})\n"
        "assert(mc.macro {\n"
        "    id = \"pass-f10\",\n"
        "    area = \"editor\",\n"
        "    key = \"F10\",\n"
        "    description = \"Pass F10\",\n"
        "    action = function()\n"
        "        record(\"macro-pass\")\n"
        "        return mc.PASS\n"
        "    end,\n"
        "})\n"
        "assert(mc.macro {\n"
        "    id = \"menu-only\",\n"
        "    area = \"editor\",\n"
        "    description = \"Menu only\",\n"
        "    menu = { path = \"Drawing\", label = \"Draw test line\", "
        "position = 25 },\n"
        "    action = function()\n"
        "        local result = assert(mc.process.run { command = \"printf test\" })\n"
        "        assert(result.stdout == \"process output\" and "
        "result.stderr == \"warning\" and result.exit_code == 7)\n"
        "        record(\"menu-only\") return mc.CONSUME\n"
        "    end,\n"
        "})\n"
        "assert(mc.macro {\n"
        "    id = \"hidden-f9\",\n"
        "    area = \"editor\",\n"
        "    key = \"F9\",\n"
        "    description = \"Hidden F9\",\n"
        "    listed = false,\n"
        "    action = function() return mc.CONSUME end,\n"
        "})\n",
        output_path);
    write_file (entry_path, script);

    g_free (script);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_ui_status (const char *text)
{
    g_free (ui_status_text);
    ui_status_text = g_strdup (text);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_ui_message (const char *title, const char *text)
{
    g_free (ui_message_title);
    g_free (ui_message_text);
    ui_message_title = g_strdup (title);
    ui_message_text = g_strdup (text);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_ui_dialog (const mc_runtime_dialog_t *dialog, mc_runtime_dialog_result_t *result,
                const char **dialog_error)
{
    const mc_runtime_dialog_control_t *buttons;

    (void) dialog_error;
    ck_assert_ptr_nonnull (dialog);

    /* the dialog a package opens for its settings carries what F1 shows */
    if (g_strcmp0 (dialog->title, "Settings probe") == 0)
    {
        char *expected =
            g_build_filename (user_editor_scripts_dir, "with-settings", "help.md", (char *) NULL);

        ck_assert_str_eq (dialog->help_node, "[Probe]");
        ck_assert_str_eq (dialog->help_file, expected);
        g_free (expected);
        result->button_id = g_strdup ("ok");
        ui_dialog_count++;
        return TRUE;
    }

    ck_assert_str_eq (dialog->title, "Base64 tools");
    ck_assert_int_eq ((int) dialog->controls_count, 5);
    ck_assert_int_eq ((int) dialog->controls[0].type, (int) MC_RUNTIME_DIALOG_LABEL);
    ck_assert_str_eq (dialog->controls[0].text, "Selected text");
    ck_assert_int_eq ((int) dialog->controls[1].type, (int) MC_RUNTIME_DIALOG_SELECT);
    ck_assert_str_eq (dialog->controls[1].value, "decode");
    ck_assert_int_eq ((int) dialog->controls[1].options_count, 2);
    ck_assert_int_eq ((int) dialog->controls[2].type, (int) MC_RUNTIME_DIALOG_INPUT);
    ck_assert_str_eq (dialog->controls[2].value, "0");
    ck_assert_str_eq (dialog->controls[2].text, "lua-test-input");
    ck_assert (dialog->controls[2].checked);
    ck_assert_int_eq ((int) dialog->controls[2].options_count, 3);
    ck_assert_str_eq (dialog->controls[2].options[0].id, "commands");
    ck_assert_str_eq (dialog->controls[2].options[1].id, "files");
    ck_assert_str_eq (dialog->controls[2].options[2].id, "shell");
    ck_assert_int_eq ((int) dialog->controls[3].type, (int) MC_RUNTIME_DIALOG_CHECKBOX);
    ck_assert (!dialog->controls[3].checked);
    ck_assert_int_eq ((int) dialog->controls[4].type, (int) MC_RUNTIME_DIALOG_HBOX);
    buttons = dialog->controls[4].controls;
    ck_assert_int_eq ((int) dialog->controls[4].controls_count, 2);
    ck_assert_str_eq (buttons[0].id, "run");
    ck_assert (buttons[0].default_button);
    ck_assert_str_eq (buttons[1].id, "close");
    ck_assert (buttons[1].cancel_button);

    result->button_id = g_strdup ("run");
    result->values_count = 3;
    result->values = g_new0 (mc_runtime_dialog_value_t, result->values_count);
    result->values[0].id = g_strdup ("operation");
    result->values[0].value = g_strdup ("encode");
    result->values[1].id = g_strdup ("line_width");
    result->values[1].value = g_strdup ("76");
    result->values[2].id = g_strdup ("omit_padding");
    result->values[2].is_boolean = TRUE;
    result->values[2].checked = TRUE;
    ui_dialog_count++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_dialog_result_free (mc_runtime_dialog_result_t *result)
{
    guint i;

    g_free (result->button_id);
    for (i = 0; i < result->values_count; i++)
    {
        g_free ((char *) result->values[i].id);
        g_free ((char *) result->values[i].value);
    }
    g_free (result->values);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_process_run_shell (const char *command, gsize max_output, mc_runtime_process_result_t *result,
                        const char **out_error)
{
    (void) max_output;
    if (out_error != NULL)
        *out_error = NULL;
    g_free (process_shell_command);
    process_shell_command = g_strdup (command);
    memset (result, 0, sizeof (*result));
    result->out.data = g_strdup ("process output");
    result->out.length = strlen (result->out.data);
    result->err.data = g_strdup ("warning");
    result->err.length = strlen (result->err.data);
    result->exit_code = 7;
    return TRUE;
}

static void
test_process_result_free (mc_runtime_process_result_t *result)
{
    g_free (result->out.data);
    g_free (result->err.data);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

static void
test_runtime_log (const char *source, const char *level, const char *message)
{
    (void) source;
    (void) level;
    (void) message;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_runtime_error (const char *runtime_name, const char *package_id,
                    mc_runtime_error_phase_t phase, const char *summary, const char *details)
{
    g_free (runtime_error_runtime);
    g_free (runtime_error_package);
    g_free (runtime_error_summary);
    g_free (runtime_error_details);
    runtime_error_runtime = g_strdup (runtime_name);
    runtime_error_package = g_strdup (package_id);
    runtime_error_phase = phase;
    runtime_error_summary = g_strdup (summary);
    runtime_error_details = g_strdup (details);
    runtime_error_count++;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_package (const char *runtime_name, const char *id, const char *display_name,
                            gboolean enabled, gpointer user_data)
{
    (void) display_name;
    (void) user_data;

    if (g_strcmp0 (runtime_name, "lua") != 0)
        return;

    enumerated_lua_packages++;
    if (g_strcmp0 (id, "beta") == 0 && !enabled)
        enumerated_disabled_beta = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_runtime (const char *runtime_name, const char *display_name, guint abi_version,
                            guint64 capability_flags, guint64 required_host_capabilities,
                            gpointer user_data)
{
    (void) capability_flags;
    (void) required_host_capabilities;
    (void) user_data;

    ck_assert_str_eq (runtime_name, "lua");
    ck_assert_str_eq (display_name, "Lua engine");
    ck_assert_int_eq ((int) abi_version, MC_RUNTIME_PLUGIN_ABI_VERSION);
    enumerated_lua_runtimes++;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_package_details (const char *runtime_name, const char *id,
                                    const char *display_name, const char *workspace,
                                    const char *origin, const char *directory, gboolean enabled,
                                    gpointer user_data)
{
    (void) display_name;
    (void) user_data;

    (void) enabled;

    if (g_strcmp0 (runtime_name, "lua") != 0)
        return;

    /* every package serves the editor */
    enumerated_lua_all_packages++;
    if (g_strcmp0 (workspace, "editor") == 0)
        enumerated_lua_editor_packages++;
    if (g_strcmp0 (id, "editor-global") == 0)
    {
        ck_assert_str_eq (origin, "global");
        ck_assert (g_str_has_suffix (directory, "/editor-global"));
        enumerated_lua_editor_global = TRUE;
    }
    else if (g_strcmp0 (id, "editor-user") == 0)
    {
        ck_assert_str_eq (origin, "user");
        ck_assert (g_str_has_suffix (directory, "/editor-user"));
        enumerated_lua_editor_user = TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_action (const char *runtime_name, const char *id, const char *label,
                           const char *shortcut, gpointer user_data)
{
    (void) user_data;

    ck_assert_str_eq (runtime_name, "lua");
    ck_assert_ptr_nonnull (id);
    ck_assert_ptr_nonnull (label);
    enumerated_lua_actions++;
    if (g_strcmp0 (id, "macro-test:consume-f11") == 0)
    {
        ck_assert_str_eq (label, "Consume F11");
        ck_assert_str_eq (shortcut, "F11");
        enumerated_lua_consume_action = TRUE;
    }
    else if (g_strcmp0 (id, "macro-test:menu-only") == 0)
    {
        ck_assert_str_eq (label, "Menu only");
        ck_assert_ptr_null (shortcut);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_menu_action (const char *runtime_name, const char *id, const char *menu_path,
                                const char *label, const char *shortcut, gint position,
                                gpointer user_data)
{
    (void) user_data;

    ck_assert_str_eq (runtime_name, "lua");
    enumerated_lua_menu_actions++;
    if (g_strcmp0 (id, "macro-test:menu-only") == 0)
    {
        ck_assert_str_eq (menu_path, "Drawing");
        ck_assert_str_eq (label, "Draw test line");
        ck_assert_ptr_null (shortcut);
        ck_assert_int_eq (position, 25);
        enumerated_lua_drawing_action = TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_fail (const char **object_error, const char *message)
{
    if (object_error != NULL)
        *object_error = message;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_handle_is (const mc_runtime_handle_t *handle, mc_runtime_handle_kind_t kind, guint64 id)
{
    return handle != NULL && handle->kind == kind && handle->id == id && handle->generation == 1;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
test_object_editor_current (void)
{
    return (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_path (const mc_runtime_handle_t *editor, mc_runtime_string_t *path,
                         const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    path->data = g_strdup ("/editor");
    path->length = strlen (path->data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_info (const mc_runtime_handle_t *editor, mc_runtime_editor_info_t *info,
                         const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    memset (info, 0, sizeof (*info));
    info->path = g_strdup ("/editor");
    info->path_length = strlen (info->path);
    info->name = g_strdup ("editor");
    info->name_length = strlen (info->name);
    info->has_path = TRUE;
    info->modified = TRUE;
    info->readonly = FALSE;
    info->revision = 7;
    info->byte_length = 4;
    info->line_count = 1;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_editor_info_free (mc_runtime_editor_info_t *info)
{
    g_free (info->path);
    g_free (info->name);
    memset (info, 0, sizeof (*info));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_selection (const mc_runtime_handle_t *editor,
                              mc_runtime_editor_selection_t *selection, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    memset (selection, 0, sizeof (*selection));
    selection->kind = MC_RUNTIME_EDITOR_SELECTION_COLUMN;
    selection->revision = 7;
    selection->anchor = (mc_runtime_editor_position_t) { 1, 1, 2 };
    selection->cursor = (mc_runtime_editor_position_t) { 8, 2, 4 };
    selection->ranges_count = 2;
    selection->ranges = g_new (mc_runtime_editor_range_t, 2);
    selection->ranges[0] = (mc_runtime_editor_range_t) { 1, 3 };
    selection->ranges[1] = (mc_runtime_editor_range_t) { 6, 8 };
    selection->text = g_strdup ("bc\ngh");
    selection->text_length = 5;
    selection->has_text = TRUE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_editor_selection_free (mc_runtime_editor_selection_t *selection)
{
    g_free (selection->ranges);
    g_free (selection->text);
    memset (selection, 0, sizeof (*selection));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_replace_selection (const mc_runtime_handle_t *editor, const char *text,
                                      gsize text_length, mc_runtime_editor_edit_result_t *result,
                                      const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    g_free (object_editor_replacement);
    object_editor_replacement = g_strndup (text, text_length);
    result->revision = 8;
    result->cursor = (mc_runtime_editor_position_t) { 5, 1, 6 };
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_replace (const mc_runtime_handle_t *editor, guint64 from, guint64 to,
                            const char *text, gsize text_length,
                            mc_runtime_editor_edit_result_t *result, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    object_editor_range_from = from;
    object_editor_range_to = to;
    g_free (object_editor_range_replacement);
    object_editor_range_replacement = g_strndup (text, text_length);
    result->revision = 9;
    result->cursor = (mc_runtime_editor_position_t) { 3, 1, 4 };
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_text (const mc_runtime_handle_t *editor, const mc_runtime_editor_range_t *range,
                         gboolean has_revision, guint64 revision, mc_runtime_string_t *text,
                         const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");
    ck_assert (range != NULL && range->from == 1 && range->to == 3);
    ck_assert (has_revision && revision == 7);
    text->data = g_strdup ("yp");
    text->length = 2;
    return TRUE;
}

static gboolean
test_object_editor_edit (const mc_runtime_handle_t *editor,
                         const mc_runtime_editor_edit_t *edit_spec,
                         mc_runtime_editor_edit_result_t *result, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");
    object_editor_typed_revision = edit_spec->revision;
    object_editor_typed_changes = edit_spec->changes_count;
    result->revision = edit_spec->changes_count == 1 ? 10 : 11;
    result->cursor =
        (mc_runtime_editor_position_t) { edit_spec->has_cursor ? edit_spec->cursor.offset : 3, 1,
                                         3 };
    return TRUE;
}

static gboolean
test_object_editor_replace_selection_v2 (const mc_runtime_handle_t *editor, guint64 revision,
                                         const char *text, gsize text_length,
                                         mc_runtime_editor_edit_result_t *result,
                                         const char **object_error)
{
    object_editor_selection_revision = revision;
    return test_object_editor_replace_selection (editor, text, text_length, result, object_error);
}

/* A terminal of 256 colors over the black background of a dark skin. */
static gboolean
test_tty_info (const char *section, mc_runtime_tty_info_t *info, const char **object_error)
{
    (void) section;
    (void) object_error;

    info->colors = 256;
    info->fg = "white";
    info->bg = "black";
    return TRUE;
}

static gboolean
test_syntax_scan (const char *text, gsize text_length, const char *type, const char *filename,
                  mc_runtime_syntax_result_t *result, const char **object_error)
{
    const char *eol;
    gsize first;

    (void) object_error;
    (void) filename;

    memset (result, 0, sizeof (*result));
    result->struct_size = sizeof (*result);

    if (type != NULL)
    {
        // what the script asks for by name
        ck_assert_int_eq ((int) text_length, 6);
        ck_assert_int_eq (memcmp (text, "int x;", 6), 0);
        ck_assert_str_eq (type, "C Program");
    }

    /* Enough of a rule set to see in the output: the first line is colored,
       the rest is not. */
    result->type = g_strdup ("Tested");
    result->colors_count = 2;
    result->colors = g_new0 (mc_runtime_syntax_color_t, 2);
    result->colors[1].fg = g_strdup ("yellow");
    result->colors[1].attrs = g_strdup ("bold");

    eol = memchr (text, '\n', text_length);
    first = eol != NULL ? (gsize) (eol - text) : text_length;
    result->runs_count = first < text_length ? 2 : 1;
    result->runs = g_new0 (mc_runtime_syntax_run_t, 2);
    result->runs[0].offset = 0;
    result->runs[0].length = first;
    result->runs[0].color = 1;
    if (result->runs_count == 2)
    {
        result->runs[1].offset = first;
        result->runs[1].length = text_length - first;
        result->runs[1].color = 0;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_syntax_result_free (mc_runtime_syntax_result_t *result)
{
    gsize i;

    for (i = 0; i < result->colors_count; i++)
    {
        g_free ((char *) result->colors[i].fg);
        g_free ((char *) result->colors[i].bg);
        g_free ((char *) result->colors[i].attrs);
    }
    g_free (result->colors);
    g_free (result->runs);
    g_free ((char *) result->type);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_ui_text_width (const char *text, gsize text_length, guint *width, const char **object_error)
{
    (void) object_error;
    ck_assert_int_eq ((int) text_length, 3);
    ck_assert_int_eq (memcmp (text, "漢", 3), 0);
    *width = 2;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_cursor (const mc_runtime_handle_t *editor, guint64 *line, guint64 *column,
                           const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    *line = 2;
    *column = 3;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_set_cursor (const mc_runtime_handle_t *editor, guint64 line, guint64 column,
                               const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    object_editor_line = line;
    object_editor_column = column;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_is_readonly (const mc_runtime_handle_t *editor, gboolean *readonly,
                                const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    *readonly = FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_tab_width (const mc_runtime_handle_t *editor, guint *tab_width,
                              const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    *tab_width = 8;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_get_text (const mc_runtime_handle_t *editor, gint64 from, gint64 to,
                             mc_runtime_string_t *text, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");
    if (from != 1 || to != 4)
        return test_object_fail (object_error, "invalid_range");

    text->data = g_strdup ("text");
    text->length = 4;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_selected_text (const mc_runtime_handle_t *editor, mc_runtime_string_t *text,
                                  const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    text->data = g_strdup ("U2V0");
    text->length = 4;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_insert (const mc_runtime_handle_t *editor, const char *text,
                           const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    g_free (object_editor_insert_text);
    object_editor_insert_text = g_strdup (text);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_save (const mc_runtime_handle_t *editor, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_string_free (mc_runtime_string_t *string)
{
    if (string == NULL)
        return;

    g_free (string->data);
    string->data = NULL;
    string->length = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void copy_script_tree (const char *source_dir, const char *target_dir);

static void
setup (void)
{
    static const mc_runtime_host_services_v1_t services = {
        .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
        .struct_size = sizeof (mc_runtime_host_services_v1_t),
        .ui_status = test_ui_status,
        .ui_message = test_ui_message,
        .log = test_runtime_log,
        .runtime_error = test_runtime_error,
        .ui_dialog = test_ui_dialog,
        .dialog_result_free = test_dialog_result_free,
        .editor_current = test_object_editor_current,
        .editor_path = test_object_editor_path,
        .editor_cursor = test_object_editor_cursor,
        .editor_set_cursor = test_object_editor_set_cursor,
        .editor_is_readonly = test_object_editor_is_readonly,
        .editor_get_text = test_object_editor_get_text,
        .editor_insert = test_object_editor_insert,
        .editor_save = test_object_editor_save,
        .string_free = test_object_string_free,
        .editor_selected_text = test_object_editor_selected_text,
        .editor_info = test_object_editor_info,
        .editor_info_free = test_object_editor_info_free,
        .editor_selection = test_object_editor_selection,
        .editor_selection_free = test_object_editor_selection_free,
        .editor_replace_selection = test_object_editor_replace_selection,
        .editor_replace = test_object_editor_replace,
        .process_run_shell = test_process_run_shell,
        .process_result_free = test_process_result_free,
        .editor_tab_width = test_object_editor_tab_width,
        .editor_text = test_object_editor_text,
        .editor_edit = test_object_editor_edit,
        .editor_replace_selection_v2 = test_object_editor_replace_selection_v2,
        .ui_text_width = test_ui_text_width,
        .syntax_scan = test_syntax_scan,
        .syntax_result_free = test_syntax_result_free,
        .tty_info = test_tty_info,
        .screen_run = test_screen_run,
    };

    error = NULL;
    g_unsetenv ("COOLE_NO_LUA");
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
    g_unsetenv ("MC_LUA_TEST_UI");
    g_unsetenv ("MC_LUA_TEST_ERROR");
    g_unsetenv ("MC_LUA_TEST_OBJECTS");
    g_unsetenv ("MC_LUA_TEST_MACRO");
    g_clear_pointer (&ui_status_text, g_free);
    g_clear_pointer (&ui_message_title, g_free);
    g_clear_pointer (&ui_message_text, g_free);
    g_clear_pointer (&runtime_error_runtime, g_free);
    g_clear_pointer (&runtime_error_package, g_free);
    g_clear_pointer (&runtime_error_summary, g_free);
    g_clear_pointer (&runtime_error_details, g_free);
    runtime_error_phase = MC_RUNTIME_ERROR_PHASE_STARTUP;
    runtime_error_count = 0;
    ui_dialog_count = 0;
    g_clear_pointer (&object_editor_insert_text, g_free);
    g_clear_pointer (&object_editor_replacement, g_free);
    g_clear_pointer (&object_editor_range_replacement, g_free);
    g_clear_pointer (&process_shell_command, g_free);
    object_editor_range_from = 0;
    object_editor_range_to = 0;
    object_editor_line = 0;
    object_editor_column = 0;
    object_editor_typed_revision = 0;
    object_editor_typed_changes = 0;
    object_editor_selection_revision = 0;
    enumerated_lua_packages = 0;
    enumerated_lua_runtimes = 0;
    enumerated_disabled_beta = FALSE;
    enumerated_lua_all_packages = 0;
    enumerated_lua_editor_packages = 0;
    enumerated_lua_editor_global = FALSE;
    enumerated_lua_editor_user = FALSE;
    enumerated_lua_actions = 0;
    enumerated_lua_consume_action = FALSE;
    enumerated_lua_menu_actions = 0;
    enumerated_lua_drawing_action = FALSE;
    screen_run_count = 0;
    {
        char *prefs_path =
            g_build_filename (config_dir, MC_USERCONF_DIR, "plugins.ini", (char *) NULL);
        char *ini_path = g_build_filename (config_dir, MC_USERCONF_DIR, "ini", (char *) NULL);

        (void) g_remove (prefs_path);
        (void) g_remove (ini_path);
        g_free (ini_path);
        g_free (prefs_path);
    }
    remove_tree (system_scripts_dir);
    remove_tree (user_scripts_dir);
    // the shared modules, as they ship
    remove_tree (system_modules_dir);
    remove_tree (user_modules_dir);
    copy_script_tree (TEST_LUA_MODULES_DIR, system_modules_dir);
    create_test_packages ();
    create_event_shape_script ();
    create_ui_script ();
    create_error_script ();
    create_object_script ();

    ck_assert_msg (mc_event_init (&error), "Failed to initialize event transport: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_msg (mc_runtime_events_init (&error), "Failed to initialize runtime events: %s",
                   error != NULL ? error->message : "unknown error");
    mc_runtime_plugins_set_host_services (&services);
    mc_runtime_plugins_set_directory_for_tests (TEST_LUA_RUNTIME_DIR);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    char *indicators;

    mc_runtime_plugins_shutdown ();
    indicators = mc_runtime_ui_indicators_compose ("editor", 80);
    ck_assert_str_eq (indicators, "");
    g_free (indicators);
    mc_runtime_events_deinit ();
    g_clear_error (&error);
    ck_assert_msg (mc_event_deinit (&error), "Failed to deinitialize event transport: %s",
                   error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_snapshot_t *
startup_snapshot_new (void)
{
    mc_runtime_event_snapshot_t *snapshot;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_STARTUP);
    snapshot->data.startup.config_dir = g_strdup (config_dir);
    snapshot->data.startup.data_dir = g_strdup ("data");

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_loads_user_override_and_callbacks)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *contents = NULL;

    mctest_assert_true (mc_runtime_plugins_load (&error));
    ck_assert_int_eq ((int) mc_runtime_plugins_count (), 1);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:data\nuser-beta:data\n");
    g_free (contents);
}
END_TEST

static void
create_screen_script (void)
{
    char *root = g_build_filename (user_editor_scripts_dir, "screen", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);

    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    write_file (ini_path,
                "[Lua]\nid=screen\napi_version=1\nname=Screen\nentry=init.lua\n"
                "provides=macros\n");
    write_file (
        entry_path,
        "seen = {}\n"
        "local bad,err=mc.ui.screen {palette='unknown'}\n"
        "assert(bad==nil and err=='invalid_screen')\n"
        "local function show()\n"
        "local s = assert(mc.ui.screen {\n"
        " title = 'Screen test', palette = 'editor', status = 'ready', "
        "help = { node = '[Test]' },\n"
        " layout = {\n"
        "  { height = 1, { type = 'label', text = 'head' } },\n"
        "  { weight = 1,\n"
        "    { weight = 2, id = 'grid', type = 'table', row_count = 5,\n"
        "      columns = { { id = 'name', title = 'Name', expands = true },\n"
        "                  { id = 'n', title = 'N', align = 'right' } },\n"
        "      rows = function(first, count)\n"
        "        local rows = {}\n"
        "        for r = first, math.min(first + count, 5) - 1 do\n"
        "          rows[#rows + 1] = { 'row ' .. r, r == 2 and { text = tostring(r), color = 'red' "
        "} "
        "or r }\n"
        "        end\n"
        "        return rows\n"
        "      end },\n"
        "    { width = 20, id = 'card', type = 'text', text = 'card' } },\n"
        " },\n"
        " keys = { { key = 'f2', label = 'Hello', action = 'hello' },\n"
        "          { key = 'f10', label = 'Quit', action = 'close' } },\n"
        " on_key = function(scr, ev) seen.key = (seen.key or '') .. ev.key.name;"
        " seen.row = ev.row; seen.control = ev.control; return ev.key.name == 'x' end,\n"
        " on_enter = function(scr, ev) seen.enter = ev.row end,\n"
        " on_action = function(scr, id, ev) seen.action = id; return { close = true } end,\n"
        " on_close = function(scr) seen.closed = true end,\n"
        "})\n"
        "assert(s:run())\n"
        "assert(seen.key == 'xy' and seen.row == 1 and seen.control == 'grid', 'key event')\n"
        "assert(seen.enter == 4, 'enter event')\n"
        "assert(seen.action == 'hello', 'action event')\n"
        "assert(seen.closed, 'close event')\n"
        "end\n"
        "assert(mc.macro { id = 'show', area = 'editor', description = 'Show the screen',\n"
        " action = function() show(); return mc.CONSUME end })\n");
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

START_TEST (test_lua_runtime_screen)
{
    const char *action_error = NULL;

    create_screen_script ();
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    mctest_assert_true (
        mc_runtime_plugins_invoke_action ("lua", "editor", "screen:show", &action_error));
    ck_assert_ptr_null (action_error);
    ck_assert_msg (runtime_error_count == 0, "screen script failed: %s / %s",
                   runtime_error_summary != NULL ? runtime_error_summary : "",
                   runtime_error_details != NULL ? runtime_error_details : "");
    ck_assert_uint_eq (screen_run_count, 1);
}
END_TEST

/* A service of C the scripts call: echo gives back its arguments, ping tells "pong" */
static GVariant *
test_echo_service (void *data, const char *method, GVariant *args, GError **err)
{
    (void) data;

    if (strcmp (method, "echo") == 0)
        return g_variant_ref (args);
    if (strcmp (method, "ping") == 0)
    {
        mc_service_emit ("echo", "pong", g_variant_ref (args));
        return g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
    }
    g_set_error (err, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD, "no method %s", method);
    return NULL;
}

static void
create_service_script (void)
{
    char *root = g_build_filename (user_editor_scripts_dir, "service", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);

    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    write_file (ini_path,
                "[Lua]\nid=service\napi_version=1\nname=Service\nentry=init.lua\n"
                "provides=macros\n");
    write_file (
        entry_path,
        "local echo = mc.service('echo')\n"
        "local seen, seen_name\n"
        "local function run()\n"
        "local r = assert(echo:call('echo', { text = 'hi', n = 42, x = 1.5, flag = true,\n"
        "  list = { 'a', 'b' }, sub = { deep = { v = 'd' } }, bytes = 'a\\0\\255' }))\n"
        "assert(r.text == 'hi' and r.n == 42 and r.x == 1.5 and r.flag == true, 'scalars')\n"
        "assert(r.list[1] == 'a' and r.list[2] == 'b', 'list')\n"
        "assert(r.sub.deep.v == 'd', 'nested')\n"
        "assert(r.bytes == 'a\\0\\255', 'bytes')\n"
        "local none, err = echo:call('other')\n"
        "assert(none == nil and err:find('no method'), 'method error')\n"
        "local id = assert(echo:on('pong', function(args, name) seen = args.v; seen_name = name "
        "end))\n"
        "assert(echo:call('ping', { v = 7 }))\n"
        "assert(seen == 7 and seen_name == 'pong', 'signal')\n"
        "assert(echo:off(id) and not echo:off(id), 'off')\n"
        "assert(echo:call('ping', { v = 8 }))\n"
        "assert(seen == 7, 'no signal after off')\n"
        "local missing, merr = mc.service('none'):call('x')\n"
        "assert(missing == nil and merr == 'not_found', 'not found')\n"
        "end\n"
        "assert(mc.macro { id = 'run', area = 'editor', description = 'Call the service',\n"
        " action = function() run(); return mc.CONSUME end })\n");
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* A script calls a service, with the values of Lua going and coming, and hears its signals */
START_TEST (test_lua_runtime_calls_services)
{
    const char *action_error = NULL;

    ck_assert (mc_service_register ("echo", test_echo_service, NULL, NULL));
    create_service_script ();
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    mctest_assert_true (
        mc_runtime_plugins_invoke_action ("lua", "editor", "service:run", &action_error));
    ck_assert_ptr_null (action_error);
    ck_assert_msg (runtime_error_count == 0, "service script failed: %s / %s",
                   runtime_error_summary != NULL ? runtime_error_summary : "",
                   runtime_error_details != NULL ? runtime_error_details : "");
    mc_service_shutdown ();
}
END_TEST

/* The view of JSON of the Preview: its lib/, as it ships, read by a script that checks it */
START_TEST (test_lua_render_json_view)
{
    char *source =
        g_build_filename (TEST_LUA_EDITOR_SCRIPTS_DIR, "render-json", "lib", (char *) NULL);
    char *root = g_build_filename (user_editor_scripts_dir, "json-check", (char *) NULL);
    char *lib = g_build_filename (root, "lib", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);

    copy_script_tree (source, lib);
    // a package of the user takes the shared modules of the user
    copy_script_tree (TEST_LUA_MODULES_DIR, user_modules_dir);
    write_file (ini_path, "[Lua]\nid=json-check\napi_version=1\nname=JSON check\nentry=init.lua\n");
    write_file (entry_path,
                "local parse = require('jsonparse').parse\n"
                "local jv = require('jsonview')\n"
                "local function view(text, width)\n"
                "  local roots = assert(parse(text))\n"
                "  return jv.view(roots, { width = width or 60 })\n"
                "end\n"
                "-- order of the keys, comments, a comma before the bracket, sizes\n"
                "local v = view('{\\n\"b\": 1, // x\\n\"a\": [1, 2],\\n\"c\": {\"d\": true,},\\n'\n"
                "  .. '\"e\": {\"k1\":1,\"k2\":2,\"k3\":3,\"k4\":4,\"k5\":5}\\n}')\n"
                "assert(v.lines[1] == '{', v.lines[1])\n"
                "assert(v.lines[2] == '  \"b\": 1,' and v.src[2] == 2, v.lines[2])\n"
                "assert(v.lines[3] == '  \"a\": [1, 2],' and v.src[3] == 3, v.lines[3])\n"
                "assert(v.lines[4] == '  \"c\": { \"d\": true },', v.lines[4])\n"
                "assert(v.lines[5] == '  \"e\": {  /* 5 keys */', v.lines[5])\n"
                "assert(v.lines[#v.lines] == '}' and v.src[#v.src] == 6)\n"
                "assert(jv.find(v, 3) == 2, 'find')\n"
                "-- an array of objects is a table; a missing key is an empty cell\n"
                "v = view('[{\"id\": 1, \"n\": \"a\"},\\n{\"id\": 22}]')\n"
                "assert(v.lines[2] == '  ┌────┬───┐', v.lines[2])\n"
                "assert(v.lines[3] == '  │ id │ n │', v.lines[3])\n"
                "assert(v.lines[5] == '  │  1 │ a │' and v.src[5] == 1, v.lines[5])\n"
                "assert(v.lines[6] == '  │ 22 │   │' and v.src[6] == 2, v.lines[6])\n"
                "-- a long array is cut short\n"
                "jv.MAX_ITEMS = 3\n"
                "v = view('[[1],[2],[3],[4],[5]]', 10)\n"
                "assert(v.lines[1] == '[  /* 5 values */', v.lines[1])\n"
                "assert(v.lines[5] == '  /* … 2 more values */', v.lines[5])\n"
                "-- JSON Lines: a value to a line\n"
                "v = view('{\"a\": 1}\\n{\"a\": 2}\\n')\n"
                "assert(#v.lines == 2 and v.lines[2] == '{ \"a\": 2 }' and v.src[2] == 2)\n"
                "-- where it does not read\n"
                "local none, err = parse('{\"a\": 1,\\n \"b\": [1\\n}')\n"
                "assert(none == nil and err.line == 3 and err.column == 1, err.message)\n"
                "-- base64: what it holds, beside its first letters; a hash is left alone\n"
                "local b64 = require('base64text')\n"
                "local roots = assert(parse('{\"t\": \"aGVsbG8sIHRoaXMgaXMgYS50eHQ=\", "
                "\"h\": \"abcd1234abcd1234abcd1234\"}'))\n"
                "v = jv.view(roots, { width = 70, blob = b64 })\n"
                "assert(v.lines[2] == '  \"t\": \"aGVsbG8sIHRo…\",  (base64, 20 B: hello, this is "
                "a.txt)', v.lines[2])\n"
                "assert(v.lines[3] == '  \"h\": \"abcd1234abcd1234abcd1234\"', v.lines[3])\n"
                "assert(b64.inspect('iVBORw0KGgoAAAAAAAAAAAAA').what == 'PNG image')\n"
                "assert(b64.inspect('data:image/svg+xml;base64,PHN2Zy8+').what == 'svg+xml')\n");

    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_msg (runtime_error_count == 0, "the JSON view failed: %s / %s",
                   runtime_error_summary != NULL ? runtime_error_summary : "",
                   runtime_error_details != NULL ? runtime_error_details : "");

    g_free (entry_path);
    g_free (ini_path);
    g_free (lib);
    g_free (root);
    g_free (source);
}
END_TEST

/* The view of XML of the Preview: its lib/, as it ships, read by a script that checks it */
START_TEST (test_lua_render_xml_view)
{
    char *source =
        g_build_filename (TEST_LUA_EDITOR_SCRIPTS_DIR, "render-xml", "lib", (char *) NULL);
    char *root = g_build_filename (user_editor_scripts_dir, "xml-check", (char *) NULL);
    char *lib = g_build_filename (root, "lib", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);

    copy_script_tree (source, lib);
    // a package of the user takes the shared modules of the user
    copy_script_tree (TEST_LUA_MODULES_DIR, user_modules_dir);
    write_file (ini_path, "[Lua]\nid=xml-check\napi_version=1\nname=XML check\nentry=init.lua\n");
    write_file (
        entry_path,
        "local parse = require('xmlparse').parse\n"
        "local xv = require('xmlview')\n"
        "local function view(text, width)\n"
        "  local nodes = assert(parse(text))\n"
        "  return xv.view(nodes, { width = width or 60 })\n"
        "end\n"
        "-- a tree; a short text on the line of its element; entities; lines of the file\n"
        "local v = view('<?xml version=\"1.0\"?>\\n<a k=\"1\">\\n<b>x &amp; y</b>\\n<c/>\\n</a>')\n"
        "assert(v.lines[1] == '<?xml version=\"1.0\"?>', v.lines[1])\n"
        "assert(v.lines[2] == '<a k=\"1\">' and v.src[2] == 2, v.lines[2])\n"
        "assert(v.lines[3] == '  <b>x & y</b>' and v.src[3] == 3, v.lines[3])\n"
        "assert(v.lines[4] == '  <c/>' and v.src[4] == 4, v.lines[4])\n"
        "assert(v.lines[5] == '</a>' and v.src[5] == 5, v.lines[5])\n"
        "assert(xv.find(v, 4) == 3, 'find')\n"
        "-- a run of one name of attributes and plain elements is a table\n"
        "v = view('<l>\\n<p n=\"a\"><s>12</s></p>\\n<p n=\"bb\"/>\\n</l>')\n"
        "assert(v.lines[2] == '  <!-- 2 × <p> -->', v.lines[2])\n"
        "assert(v.lines[4] == '  │ n  │ s  │', v.lines[4])\n"
        "assert(v.lines[6] == '  │ a  │ 12 │' and v.src[6] == 2, v.lines[6])\n"
        "assert(v.lines[7] == '  │ bb │    │' and v.src[7] == 3, v.lines[7])\n"
        "-- a long run is cut short; the elements are counted\n"
        "xv.MAX_ITEMS = 2\n"
        "v = view('<l><i>1</i><i>2</i><i>3</i><i>4</i><i>5</i></l>')\n"
        "assert(v.lines[1] == '<l>  <!-- 5 elements -->', v.lines[1])\n"
        "assert(v.lines[4] == '  <!-- … 3 more <i> -->', v.lines[4])\n"
        "-- attributes that do not fit go one to a line\n"
        "v = view('<w aaaa=\"1111\" bbbb=\"2222\" cccc=\"3333\"/>', 20)\n"
        "assert(v.lines[1] == '<w' and v.lines[4] == '    cccc=\"3333\"/>', v.lines[4])\n"
        "-- where it does not read\n"
        "local none, err = parse('<a>\\n<b></a>')\n"
        "assert(none == nil and err.line == 2 and err.column == 4, err.message)\n"
        "-- base64 in a text and in an attribute\n"
        "local nodes = assert(parse('<a><t>aGVsbG8sIHRoaXMgaXMgYS50eHQ=</t>"
        "<i src=\"data:image/svg+xml;base64,PHN2ZyB4bWxucz0iIi8+\"/></a>'))\n"
        "v = xv.view(nodes, { width = 70, blob = require('base64text') })\n"
        "assert(v.lines[2] == '  <t>aGVsbG8sIHRo…</t>  (base64, 20 B: hello, this is a.txt)', "
        "v.lines[2])\n"
        "assert(v.lines[3] == '  <i src=\"data:image/svg+xml;base64,PHN2ZyB4bWxu…\" "
        "(svg+xml, 15 B: <svg xmlns=\"\"/>)/>', v.lines[3])\n");

    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_msg (runtime_error_count == 0, "the XML view failed: %s / %s",
                   runtime_error_summary != NULL ? runtime_error_summary : "",
                   runtime_error_details != NULL ? runtime_error_details : "");

    g_free (entry_path);
    g_free (ini_path);
    g_free (lib);
    g_free (root);
    g_free (source);
}
END_TEST

/* A package offers its settings on request. */
START_TEST (test_lua_package_settings_are_shown_on_request)
{
    const char *settings_error = NULL;
    char *mark_path;
    char *contents = NULL;

    create_settings_script ();
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");

    mctest_assert_true (
        mc_runtime_plugins_configure_package ("lua", "with-settings", &settings_error));
    ck_assert_ptr_null (settings_error);
    mctest_assert_true (
        mc_runtime_plugins_configure_package ("lua", "with-settings", &settings_error));

    /* a package that registered no dialog says so, and nothing of it is run */
    ck_assert (!mc_runtime_plugins_configure_package ("lua", "beta", &settings_error));
    ck_assert_str_eq (settings_error, "settings_not_found");
    ck_assert (!mc_runtime_plugins_configure_package ("lua", "no-such-package", &settings_error));
    ck_assert_str_eq (settings_error, "package_not_found");

    /* the handler ran twice, once for each call that found it */
    mark_path =
        g_build_filename (user_editor_scripts_dir, "with-settings", "shown.txt", (char *) NULL);
    mctest_assert_true (g_file_get_contents (mark_path, &contents, NULL, &error));
    ck_assert_str_eq (contents, "shown\nshown\n");
    g_free (contents);
    g_free (mark_path);
}
END_TEST

START_TEST (test_lua_runtime_requires_known_workspace_directory)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *unknown_workspace_dir;
    char *contents = NULL;

    create_script (system_scripts_dir, "outside-workspace", "outside-workspace", FALSE);
    unknown_workspace_dir = g_build_filename (system_scripts_dir, "unknown", (char *) NULL);
    create_script (unknown_workspace_dir, "unknown-workspace", "unknown-workspace", FALSE);
    g_free (unknown_workspace_dir);
    /* only the editor directory is a workspace */
    unknown_workspace_dir = g_build_filename (user_scripts_dir, "mc", (char *) NULL);
    create_script (unknown_workspace_dir, "old-mc", "old-mc", FALSE);
    g_free (unknown_workspace_dir);
    unknown_workspace_dir = g_build_filename (user_scripts_dir, "viewer", (char *) NULL);
    create_script (unknown_workspace_dir, "old-viewer", "old-viewer", FALSE);
    g_free (unknown_workspace_dir);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:data\nuser-beta:data\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_uses_optional_ui_host_services)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_LUA_TEST_UI", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    ck_assert_str_eq (ui_status_text, "Lua status");
    ck_assert_str_eq (ui_message_title, "Lua title");
    ck_assert_str_eq (ui_message_text, "Lua message");
    {
        char *indicators = mc_runtime_ui_indicators_compose ("editor", 80);

        ck_assert_str_eq (indicators, "[UI]");
        g_free (indicators);
    }

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("F11");
    snapshot->data.editor_key.key.code = 11;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);
    ck_assert_int_eq ((int) ui_dialog_count, 1);
    g_unsetenv ("MC_LUA_TEST_UI");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_isolates_callback_errors)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_LUA_TEST_ERROR", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    ck_assert_int_eq ((int) runtime_error_count, 1);
    ck_assert_str_eq (runtime_error_runtime, "lua");
    ck_assert_str_eq (runtime_error_package, "error-test");
    ck_assert_int_eq ((int) runtime_error_phase, (int) MC_RUNTIME_ERROR_PHASE_STARTUP);
    ck_assert_str_eq (runtime_error_summary, "Lua startup callback failed");
    ck_assert_msg (strstr (runtime_error_details, "expected test error") != NULL,
                   "Lua traceback did not include the original error");
    g_unsetenv ("MC_LUA_TEST_ERROR");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_exposes_object_api_through_opaque_handles)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_LUA_TEST_OBJECTS", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    ck_assert_int_eq ((int) object_editor_line, 3);
    ck_assert_int_eq ((int) object_editor_column, 4);
    ck_assert_str_eq (object_editor_insert_text, "!");
    ck_assert_str_eq (object_editor_replacement, "BC\nGH");
    ck_assert_uint_eq (object_editor_range_from, 1);
    ck_assert_uint_eq (object_editor_range_to, 3);
    ck_assert_str_eq (object_editor_range_replacement, "xy");
    ck_assert_uint_eq (object_editor_selection_revision, 7);
    ck_assert_uint_eq (object_editor_typed_revision, 10);
    ck_assert_uint_eq (object_editor_typed_changes, 2);

    /* a handle the host no longer knows */
    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_SAVE);
    snapshot->data.editor_save.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 99, 1 };
    snapshot->data.editor_save.path = g_strdup ("/gone");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);
    ck_assert_str_eq (ui_status_text, "save status");

    g_unsetenv ("MC_LUA_TEST_OBJECTS");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_registers_editor_macros)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *contents = NULL;

    create_macro_script ();
    g_setenv ("MC_LUA_TEST_MACRO", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    mc_runtime_plugins_enumerate_actions ("editor", test_enumerate_lua_action, NULL);
    ck_assert_int_eq ((int) enumerated_lua_actions, 3);
    mctest_assert_true (enumerated_lua_consume_action);
    mc_runtime_plugins_enumerate_menu_actions ("editor", test_enumerate_lua_menu_action, NULL);
    ck_assert_int_eq ((int) enumerated_lua_menu_actions, 1);
    mctest_assert_true (enumerated_lua_drawing_action);
    {
        const char *action_error = NULL;

        mctest_assert_true (mc_runtime_plugins_invoke_action (
            "lua", "editor", "macro-test:pass-f10", &action_error));
        ck_assert_ptr_null (action_error);
        mctest_assert_true (mc_runtime_plugins_invoke_action (
            "lua", "editor", "macro-test:menu-only", &action_error));
        ck_assert_ptr_null (action_error);
        ck_assert_str_eq (process_shell_command, "printf test");
    }

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("F11");
    snapshot->data.editor_key.key.code = 11;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_true (snapshot->consumed);
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("f10");
    snapshot->data.editor_key.key.code = 10;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_false (snapshot->consumed);
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "macro-pass\nmenu-only\nmacro-consume\nmacro-pass\n");
    g_free (contents);
    g_unsetenv ("MC_LUA_TEST_MACRO");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_converts_all_domain_event_snapshots)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *contents = NULL;

    g_setenv ("MC_LUA_TEST_EVENT_SHAPES", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));
    (void) g_remove (output_path);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_OPEN);
    snapshot->data.editor_open.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_open.path = g_strdup ("/new/edit");
    snapshot->data.editor_open.readonly = TRUE;
    snapshot->data.editor_open.line = 4;
    snapshot->data.editor_open.column = 9;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_SAVE);
    snapshot->data.editor_save.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_save.path = g_strdup ("/new/edit");
    snapshot->data.editor_save.previous_path = g_strdup ("/old/edit");
    snapshot->data.editor_save.save_as = TRUE;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("Ctrl-S");
    snapshot->data.editor_key.key.code = 19;
    snapshot->data.editor_key.key.ctrl = TRUE;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_true (snapshot->consumed);
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_CHANGE);
    snapshot->data.editor_change.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_change.path = g_strdup ("/new/edit");
    snapshot->data.editor_change.revision = 12;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_CURSOR);
    snapshot->data.editor_cursor.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_cursor.path = g_strdup ("/new/edit");
    snapshot->data.editor_cursor.line = 7;
    snapshot->data.editor_cursor.column = 3;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_SHUTDOWN);
    snapshot->data.shutdown.reason = g_strdup ("quit");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents,
                      "user-alpha:data\nuser-beta:data\nstartup\neditor-open\neditor-save\n"
                      "editor-key\neditor-change\neditor-cursor\nshutdown\n");
    g_free (contents);
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_honors_per_package_disable)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *prefs_path;
    char *contents = NULL;

    prefs_path = g_build_filename (config_dir, MC_USERCONF_DIR, "plugins.ini", (char *) NULL);
    write_file (prefs_path, "[DisabledPlugins]\nlua/beta=true\n");
    g_free (prefs_path);

    mctest_assert_true (mc_runtime_plugins_load (&error));
    mc_runtime_plugins_enumerate_packages (test_enumerate_lua_package, NULL);
    ck_assert_int_gt ((int) enumerated_lua_packages, 0);
    ck_assert (enumerated_disabled_beta);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:data\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_enumerates_editor_package_details)
{
    mctest_assert_true (mc_runtime_plugins_load (&error));
    mc_runtime_plugins_enumerate_runtimes (test_enumerate_lua_runtime, NULL);
    mc_runtime_plugins_enumerate_package_details (test_enumerate_lua_package_details, NULL);

    ck_assert_int_eq ((int) enumerated_lua_runtimes, 1);
    ck_assert_int_gt ((int) enumerated_lua_all_packages, 2);
    ck_assert_int_eq ((int) enumerated_lua_editor_packages, (int) enumerated_lua_all_packages);
    ck_assert (enumerated_lua_editor_global);
    ck_assert (enumerated_lua_editor_user);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_rejects_insecure_package_paths)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *root;
    char *contents = NULL;

    create_script (user_editor_scripts_dir, "unsafe", "unsafe", FALSE);
    root = g_build_filename (user_editor_scripts_dir, "unsafe", (char *) NULL);
    ck_assert_int_eq (g_chmod (root, 0777), 0);
    g_free (root);

    mctest_assert_true (mc_runtime_plugins_load (&error));
    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:data\nuser-beta:data\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_honors_disable_environment)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("COOLE_NO_LUA", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));
    ck_assert_int_eq ((int) mc_runtime_plugins_count (), 0);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_false (g_file_test (output_path, G_FILE_TEST_EXISTS));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Copy a directory of scripts, whatever files and subdirectories it holds. */
static void
copy_script_tree (const char *source_dir, const char *target_dir)
{
    GDir *dir;
    const char *name;

    ck_assert_int_eq (g_mkdir_with_parents (target_dir, 0700), 0);
    dir = g_dir_open (source_dir, 0, &error);
    ck_assert_msg (dir != NULL, "%s: %s", source_dir,
                   error != NULL ? error->message : "cannot open");
    while ((name = g_dir_read_name (dir)) != NULL)
    {
        char *source = g_build_filename (source_dir, name, (char *) NULL);
        char *target = g_build_filename (target_dir, name, (char *) NULL);

        if (g_file_test (source, G_FILE_TEST_IS_DIR))
            copy_script_tree (source, target);
        else
        {
            char *contents = NULL;

            mctest_assert_true (g_file_get_contents (source, &contents, NULL, &error));
            g_clear_error (&error);
            write_file (target, contents);
            g_free (contents);
        }
        g_free (target);
        g_free (source);
    }
    g_dir_close (dir);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_count_shipped_package (const char *runtime_name, const char *id, const char *display_name,
                            const char *workspace, const char *origin, const char *directory,
                            gboolean enabled, gpointer user_data)
{
    static const char *const shipped[] = {
        "base64-decode",   "draw-table",     "format-paragraph", "insert-command-output",
        "insert-datetime", "insert-literal", "sort-selection",   "notify-editor-save",
        "render-markdown", "render-json",    "render-xml",
    };
    guint *count = (guint *) user_data;
    guint i;

    (void) display_name;
    (void) origin;
    (void) directory;

    if (g_strcmp0 (runtime_name, "lua") != 0)
        return;
    for (i = 0; i < G_N_ELEMENTS (shipped); i++)
        if (strcmp (id, shipped[i]) == 0)
        {
            ck_assert_str_eq (workspace, "editor");
            ck_assert (enabled);
            (*count)++;
        }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_count_action (const char *runtime_name, const char *id, const char *label,
                   const char *shortcut, gpointer user_data)
{
    guint *count = (guint *) user_data;

    (void) runtime_name;
    (void) id;
    (void) label;
    (void) shortcut;
    (*count)++;
}

/* --------------------------------------------------------------------------------------------- */

/* The scripts that come with coole, and the example, load in the editor
   workspace and register their actions without a single error. */
START_TEST (test_lua_runtime_loads_the_shipped_editor_scripts)
{
    guint packages = 0;
    guint actions = 0;

    copy_script_tree (TEST_LUA_EDITOR_SCRIPTS_DIR, system_editor_scripts_dir);
    copy_script_tree (TEST_LUA_EDITOR_EXAMPLES_DIR, user_editor_scripts_dir);
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_msg (runtime_error_count == 0, "a shipped script failed: %s / %s",
                   runtime_error_summary != NULL ? runtime_error_summary : "",
                   runtime_error_details != NULL ? runtime_error_details : "");

    mc_runtime_plugins_enumerate_package_details (test_count_shipped_package, &packages);
    ck_assert_uint_eq (packages, 11);
    mc_runtime_plugins_enumerate_actions ("editor", test_count_action, &actions);
    ck_assert_uint_ge (actions, 7);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;
    int result;

    str_init_strings ("UTF-8");
    test_root = g_dir_make_tmp ("coole-lua-runtime.XXXXXX", &error);
    if (test_root == NULL)
    {
        fprintf (stderr, "Could not create test directory: %s\n",
                 error != NULL ? error->message : "unknown error");
        g_clear_error (&error);
        return EXIT_FAILURE;
    }

    config_dir = g_build_filename (test_root, "config", (char *) NULL);
    system_scripts_dir = g_build_filename (test_root, "system-scripts", (char *) NULL);
    data_dir = g_build_filename (test_root, "data", (char *) NULL);
    user_scripts_dir =
        g_build_filename (data_dir, MC_USERCONF_DIR, "lua", "scripts", (char *) NULL);
    system_editor_scripts_dir = g_build_filename (system_scripts_dir, "editor", (char *) NULL);
    user_editor_scripts_dir = g_build_filename (user_scripts_dir, "editor", (char *) NULL);
    output_path = g_build_filename (test_root, "events.log", (char *) NULL);
    (void) g_mkdir_with_parents (config_dir, 0700);
    {
        char *mc_config_dir = g_build_filename (config_dir, MC_USERCONF_DIR, (char *) NULL);

        (void) g_mkdir_with_parents (mc_config_dir, 0700);
        g_free (mc_config_dir);
    }
    g_setenv ("XDG_CONFIG_HOME", config_dir, TRUE);
    g_setenv ("XDG_DATA_HOME", data_dir, TRUE);
    g_setenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR", system_scripts_dir, TRUE);
    system_modules_dir = g_build_filename (test_root, "system-modules", (char *) NULL);
    user_modules_dir = g_build_filename (data_dir, MC_USERCONF_DIR, "lua", "lib", (char *) NULL);
    g_setenv ("MC_LUA_TEST_SYSTEM_MODULES_DIR", system_modules_dir, TRUE);

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_lua_runtime_loads_user_override_and_callbacks);
    tcase_add_test (tc_core, test_lua_runtime_requires_known_workspace_directory);
    tcase_add_test (tc_core, test_lua_runtime_uses_optional_ui_host_services);
    tcase_add_test (tc_core, test_lua_runtime_isolates_callback_errors);
    tcase_add_test (tc_core, test_lua_runtime_exposes_object_api_through_opaque_handles);
    tcase_add_test (tc_core, test_lua_runtime_registers_editor_macros);
    tcase_add_test (tc_core, test_lua_runtime_converts_all_domain_event_snapshots);
    tcase_add_test (tc_core, test_lua_runtime_honors_per_package_disable);
    tcase_add_test (tc_core, test_lua_runtime_enumerates_editor_package_details);
    tcase_add_test (tc_core, test_lua_runtime_rejects_insecure_package_paths);
    tcase_add_test (tc_core, test_lua_runtime_honors_disable_environment);
    tcase_add_test (tc_core, test_lua_runtime_screen);
    tcase_add_test (tc_core, test_lua_runtime_calls_services);
    tcase_add_test (tc_core, test_lua_render_json_view);
    tcase_add_test (tc_core, test_lua_render_xml_view);
    tcase_add_test (tc_core, test_lua_package_settings_are_shown_on_request);
    tcase_add_test (tc_core, test_lua_runtime_loads_the_shipped_editor_scripts);

    result = mctest_run_all (tc_core);

    g_unsetenv ("COOLE_NO_LUA");
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
    g_unsetenv ("MC_LUA_TEST_UI");
    g_unsetenv ("MC_LUA_TEST_ERROR");
    g_unsetenv ("MC_LUA_TEST_OBJECTS");
    g_unsetenv ("MC_LUA_TEST_MACRO");
    g_unsetenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR");
    g_unsetenv ("XDG_DATA_HOME");
    remove_tree (test_root);
    g_free (object_editor_insert_text);
    g_free (object_editor_replacement);
    g_free (process_shell_command);
    g_free (object_editor_range_replacement);
    g_free (ui_message_text);
    g_free (ui_message_title);
    g_free (ui_status_text);
    g_free (runtime_error_details);
    g_free (runtime_error_summary);
    g_free (runtime_error_package);
    g_free (runtime_error_runtime);
    g_free (output_path);
    g_free (user_editor_scripts_dir);
    g_free (system_editor_scripts_dir);
    g_free (user_scripts_dir);
    g_free (system_scripts_dir);
    g_free (config_dir);
    g_free (data_dir);
    g_free (test_root);
    str_uninit_strings ();
    return result;
}

/* --------------------------------------------------------------------------------------------- */
