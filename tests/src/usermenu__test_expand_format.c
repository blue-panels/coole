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

#include "src/vfs/local/local.h"

/* what the editor would say comes from the test */
#define edit_get_file_name     test_edit_get_file_name
#define edit_get_cursor_offset test_edit_get_cursor_offset
#define edit_get_curs_col      test_edit_get_curs_col
#define edit_get_syntax_type   test_edit_get_syntax_type

#include "src/usermenu.c"

#define FNAME_BASE "file with spaces"
#define FNAME_EXT  "sh"
#define FNAME      FNAME_BASE "." FNAME_EXT

/* stands in for the editor the menu is shown for */
static Widget fake_editor;

/* --------------------------------------------------------------------------------------------- */

/* @Mock */
const char *
test_edit_get_file_name (const WEdit *edit)
{
    (void) edit;

    return FNAME;
}

/* @Mock */
off_t
test_edit_get_cursor_offset (const WEdit *edit)
{
    (void) edit;

    return 42;
}

/* @Mock */
long
test_edit_get_curs_col (const WEdit *edit)
{
    (void) edit;

    return 3;
}

/* @Mock */
const char *
test_edit_get_syntax_type (const WEdit *edit)
{
    (void) edit;

    return "Shell Script";
}

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    str_init_strings (NULL);
    vfs_init ();
    vfs_init_localfs ();
    vfs_setup_work_dir ();
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    vfs_shut ();
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

static const struct check_expand_format_ds
{
    char macro;
    gboolean do_quote;
    const char *expected;  // NULL: the current directory
} check_expand_format_ds[] = {
    { '%', FALSE, "%" },       { 'f', FALSE, FNAME },      { 'p', FALSE, FNAME },
    { 'F', FALSE, FNAME },     { 'P', FALSE, FNAME },      { 'x', FALSE, FNAME_EXT },
    { 'X', FALSE, FNAME_EXT }, { 'n', FALSE, FNAME_BASE }, { 'N', FALSE, FNAME_BASE },
    { 's', FALSE, FNAME },     { 'S', FALSE, FNAME },      { 'f', TRUE, "file\\ with\\ spaces.sh" },
    { 'c', FALSE, "42" },      { 'i', FALSE, "   " },      { 'y', FALSE, "Shell Script" },
    { 'd', FALSE, NULL },      { 'D', FALSE, NULL },
};

/* --------------------------------------------------------------------------------------------- */

START_PARAMETRIZED_TEST (check_expand_format, check_expand_format_ds)
{
    char *result;

    result = expand_format (&fake_editor, data->macro, data->do_quote);

    if (data->expected != NULL)
        ck_assert_str_eq (data->expected, result);
    else
        ck_assert_str_eq (vfs_get_current_dir (), result);

    g_free (result);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    // Add new tests here: ***************
    mctest_add_parameterized_test (tc_core, check_expand_format, check_expand_format_ds);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
