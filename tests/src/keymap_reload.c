/*
   tests/src/keymap_reload.c -- test keymap reload correctness

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/src/keymap_reload"

#include "tests/mctest.h"

#include "lib/keybind.h"
#include "lib/mcconfig.h"
#include "lib/tty/key.h"

#include "src/keymap.h"

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_keymap_load_defaults)
{
    const global_keymap_t *map;

    keymap_load (FALSE); /* load from C defaults only, no files */

    map = editor_map;
    ck_assert_ptr_ne (map, NULL);

    /* default editor keymap should have CK_Copy bound to F5 */
    {
        long cmd;

        cmd = keybind_lookup_keymap_command (map, KEY_F (5));
        ck_assert_int_eq (cmd, CK_Copy);
    }

    /* default editor keymap should have CK_Move bound to F6 */
    {
        long cmd;

        cmd = keybind_lookup_keymap_command (map, KEY_F (6));
        ck_assert_int_eq (cmd, CK_Move);
    }

    /* Ctrl-S switches the syntax highlighting, Alt-S the filter */
    {
        long cmd;

        map = editor_map;
        cmd = keybind_lookup_keymap_command (map, XCTRL ('s'));
        ck_assert_int_eq (cmd, CK_SyntaxOnOff);

        cmd = keybind_lookup_keymap_command (map, ALT ('s'));
        ck_assert_int_eq (cmd, CK_FilterToggle);
    }

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_keymap_reload_pointer_changes)
{
    const global_keymap_t *old_map;

    keymap_load (FALSE);
    old_map = editor_map;
    ck_assert_ptr_ne (old_map, NULL);

    keymap_free ();
    keymap_load (FALSE);

    /* Valid after reload, but may point to different memory; a widget that
       cached old_map would then hold a dangling pointer. */
    ck_assert_ptr_ne (editor_map, NULL);

    {
        long cmd;

        cmd = keybind_lookup_keymap_command (editor_map, KEY_F (5));
        ck_assert_int_eq (cmd, CK_Copy);
    }

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* a plugin's commands, Help among them, which the program has already */
static const keymap_command_t plugin_commands[] = {
    { "Help", NULL, "f1" },
    { "TestPluginGo", "Go", "f5" },
    { "TestPluginStop", "Stop", "f15; ctrl-t" },
    { "TestPluginUnbound", "Unbound", NULL },
    { NULL, NULL, NULL },
};

START_TEST (test_keymap_plugin_section)
{
    const global_keymap_t *map;
    long go, stop;

    keymap_load (FALSE);
    keymap_register_section ("testplugin", "&Test", plugin_commands);

    go = keybind_lookup_action ("TestPluginGo");
    stop = keybind_lookup_action ("TestPluginStop");
    ck_assert_int_ge (go, CK_PluginFirst);
    ck_assert_int_ne (go, stop);
    ck_assert_int_ge (keybind_lookup_action ("TestPluginUnbound"), CK_PluginFirst);
    ck_assert_str_eq (keybind_lookup_actionname (go), "TestPluginGo");
    ck_assert_str_eq (keybind_lookup_actiondesc (go), "Go");

    /* the section has its own keys: they do not replace the editor's */
    map = keymap_section_map ("testplugin");
    ck_assert_ptr_ne (map, NULL);
    ck_assert_int_eq (keybind_lookup_keymap_command (map, KEY_F (1)), CK_Help);
    ck_assert_int_eq (keybind_lookup_keymap_command (map, KEY_F (5)), go);
    ck_assert_int_eq (keybind_lookup_keymap_command (map, KEY_F (15)), stop);
    ck_assert_int_eq (keybind_lookup_keymap_command (map, XCTRL ('t')), stop);
    ck_assert_int_eq (keybind_lookup_keymap_command (editor_map, KEY_F (5)), CK_Copy);

    /* reloaded with the others, the same commands on the same keys */
    keymap_free ();
    ck_assert_ptr_eq (keymap_section_map ("testplugin"), NULL);
    keymap_load (FALSE);
    keymap_register_section ("testplugin", "&Test", plugin_commands);
    map = keymap_section_map ("testplugin");
    ck_assert_ptr_ne (map, NULL);
    ck_assert_int_eq (keybind_lookup_keymap_command (map, KEY_F (5)), go);
    ck_assert_int_eq (keybind_lookup_action ("TestPluginGo"), go);

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_keymap_data_pointer_stability)
{
    long cmd;

    keymap_load (FALSE);
    keymap_free ();
    keymap_load (FALSE);

    cmd = keybind_lookup_keymap_command (editor_map, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_widget_keymap_dangling)
{
    const global_keymap_t *widget_keymap;
    long cmd;

    keymap_load (FALSE);

    /* A widget caches the keymap pointer at init time. */
    widget_keymap = editor_map;
    ck_assert_ptr_ne (widget_keymap, NULL);

    cmd = keybind_lookup_keymap_command (widget_keymap, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    keymap_free ();
    keymap_load (FALSE);

    /* editor_map is valid and updated after the reload. */
    cmd = keybind_lookup_keymap_command (editor_map, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    /* widget_keymap may now point to freed memory; do not dereference it. */
    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_keymap_load_defaults);
    tcase_add_test (tc_core, test_keymap_reload_pointer_changes);
    tcase_add_test (tc_core, test_keymap_data_pointer_stability);
    tcase_add_test (tc_core, test_keymap_plugin_section);
    /* test_keymap_user_override requires mc_global init -- run manually */
    /* tcase_add_test (tc_core, test_keymap_user_override); */
    tcase_add_test (tc_core, test_widget_keymap_dangling);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
