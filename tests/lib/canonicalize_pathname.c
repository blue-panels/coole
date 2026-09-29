/*
   lib - canonicalize path

   Copyright (C) 2011-2025
   Free Software Foundation, Inc.

   Written by:
   Slava Zanko <slavazanko@gmail.com>, 2011, 2013

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

#define TEST_SUITE_NAME "/lib"

#include "tests/mctest.h"

#include "lib/strutil.h"
#include "lib/util.h"

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    str_init_strings (NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

/* @DataSource("test_canonicalize_path_ds") */
static const struct test_canonicalize_path_ds
{
    const char *input_path;
    const char *expected_path;
} test_canonicalize_path_ds[] = {
    {
        // 0. UNC path
        "//some_server/ww",
        "//some_server/ww",
    },
    {
        // 1. join slashes
        "///some_server/////////ww",
        "/some_server/ww",
    },
    {
        // 2. Collapse "/./" -> "/"
        "//some_server//.///////ww/./././.",
        "//some_server/ww",
    },
    {
        // 3. Remove leading "./"
        "./some_server/ww",
        "some_server/ww",
    },
    {
        // 4. some/.. -> .
        "some_server/..",
        ".",
    },
    {
        // 5. Collapse "/.." with the previous part of path
        "/some_server/ww/some_server/../ww/../some_server/..//ww/some_server/ww",
        "/some_server/ww/ww/some_server/ww",
    },
    {
        // 6. Remove trailing slashes
        "/some_server/ww///",
        "/some_server/ww",
    },
    {
        // 7. "/.." at the root
        "/../ww",
        "/ww",
    },
    {
        // 8. a relative ".." that cannot be collapsed
        "../../ww",
        "../../ww",
    },
    {
        // 9. the root stays
        "/",
        "/",
    },
};

/* @Test(dataSource = "test_canonicalize_path_ds") */
START_PARAMETRIZED_TEST (test_canonicalize_path, test_canonicalize_path_ds)
{
    // given
    char *actual_path;

    actual_path = g_strdup (data->input_path);

    // when
    canonicalize_pathname (actual_path);

    // then
    mctest_assert_str_eq (actual_path, data->expected_path) g_free (actual_path);
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
    mctest_add_parameterized_test (tc_core, test_canonicalize_path, test_canonicalize_path_ds);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
