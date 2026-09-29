/*
   src/usermenu - tests for usermenu

   Copyright (C) 2025
   Free Software Foundation, Inc.

   This file is part of coole,
   a text editor based on GNU Midnight Commander.

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

#define TEST_SUITE_NAME "/src/usermenu"

#include "tests/mctest.h"

#include "lib/charsets.h"

/* the name of the edited file comes from the test, not from an editor */
#define edit_get_file_name test_edit_get_file_name

#include "src/usermenu.c"

/* --------------------------------------------------------------------------------------------- */

static const char *const file_names[] = {
    "file_without_spaces",
    "file with spaces",
    "file_without_spaces_utf8_local_chars_ä_ü_ö_ß_",
    "file_without_spaces_utf8_nonlocal_chars_à_á_",
};

static const char *edited_file_name = NULL;

/* stands in for the editor the menu is shown for */
static Widget fake_editor;

/* --------------------------------------------------------------------------------------------- */

/* @Mock */
const char *
test_edit_get_file_name (const WEdit *edit)
{
    (void) edit;

    return edited_file_name;
}

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    easy_patterns = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    edited_file_name = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
setup_charset (const char *encoding, const gboolean utf8_display)
{
    str_init_strings (encoding);

    mc_global.sysconfig_dir = (char *) TEST_SHARE_DIR;
    load_codepages_list ();

    cp_source = encoding;
    cp_display = encoding;
    mc_global.source_codepage = get_codepage_index (encoding);
    mc_global.display_codepage = get_codepage_index (encoding);

    mc_global.utf8_display = utf8_display;
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown_charset (void)
{
    free_codepages_list ();
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

static const struct check_test_condition_ds
{
    const char *encoding;
    gboolean utf8_display;
    int current_file;
    gboolean expected_value;
} check_test_condition_ds[] = {
    // In fully UTF-8 environment, only file with real spaces should match
    { "UTF-8", TRUE, 0, FALSE },
    { "UTF-8", TRUE, 1, TRUE },
    { "UTF-8", TRUE, 2, FALSE },
    { "UTF-8", TRUE, 3, FALSE },

    // Last filename contains 0xa0 byte, which would be interpreted as a non-breaking space in
    // single-byte encodings
    { "KOI8-R", FALSE, 0, FALSE },
    { "KOI8-R", FALSE, 1, TRUE },
    { "KOI8-R", FALSE, 2, FALSE },
    { "KOI8-R", FALSE, 3, TRUE },
};

/* --------------------------------------------------------------------------------------------- */

START_PARAMETRIZED_TEST (check_test_condition, check_test_condition_ds)
{
    setup_charset (data->encoding, data->utf8_display);
    char *condition = g_strdup ("f [[:space:]] & t r");

    edited_file_name = file_names[data->current_file];

    gboolean result = FALSE;
    test_condition (&fake_editor, condition, &result);
    ck_assert_int_eq (result, data->expected_value);

    g_free (condition);
    teardown_charset ();
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (check_test_condition_without_file)
{
    char *condition;
    gboolean result = TRUE;

    setup_charset ("UTF-8", TRUE);
    condition = g_strdup ("f .*\n");

    // a new buffer has no name, and no pattern matches it
    test_condition (&fake_editor, condition, &result);
    ck_assert_int_eq (result, FALSE);

    g_free (condition);
    teardown_charset ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    // Add new tests here: ***************
    mctest_add_parameterized_test (tc_core, check_test_condition, check_test_condition_ds);
    tcase_add_test (tc_core, check_test_condition_without_file);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
