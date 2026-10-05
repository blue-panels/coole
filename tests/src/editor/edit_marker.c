/*
   src/editor - tests for the gutter marks of the plugins

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

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/charsets.h"
#include "src/selcodepage.h"

#include "src/editor/editwidget.h"
#include "src/editor/edit-impl.h"

/* A dialog, not a bare group: the editor reaches its owner as one */
static WDialog owner;
static WEdit *test_edit;

/* no colour keys: the tests run with no skin */
static const mc_ep_marker_kind_t low = { "test.low", NULL, "L", "l", NULL, NULL, NULL, 1, NULL };
static const mc_ep_marker_kind_t high = { "test.high", NULL, "H", "h", NULL, NULL, NULL, 2, NULL };

/* --------------------------------------------------------------------------------------------- */
/* @Mock */
void
message (int flags, const char *title, const char *text, ...)
{
    (void) flags;
    (void) title;
    (void) text;
}

/* --------------------------------------------------------------------------------------------- */
/* @Mock */
void
status_msg_init (status_msg_t *sm, const char *title, double delay, status_msg_cb init_cb,
                 status_msg_update_cb update_cb, status_msg_cb deinit_cb)
{
    (void) sm;
    (void) title;
    (void) delay;
    (void) init_cb;
    (void) update_cb;
    (void) deinit_cb;
}

/* --------------------------------------------------------------------------------------------- */
/* @Mock */
void
status_msg_deinit (status_msg_t *sm)
{
    (void) sm;
}

/* --------------------------------------------------------------------------------------------- */

static void
insert_text (const char *text)
{
    for (; *text != '\0'; text++)
        edit_insert (test_edit, (unsigned char) *text);
}

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    WRect r;

    str_init_strings (NULL);

    mc_global.sysconfig_dir = (char *) TEST_SHARE_DIR;
    load_codepages_list ();

    edit_options.filesize_threshold = (char *) "64M";

    rect_init (&r, 0, 0, 24, 80);
    test_edit = edit_init (NULL, &r, NULL);
    memset (&owner, 0, sizeof (owner));
    group_add_widget (&owner.group, WIDGET (test_edit));

    mc_global.source_codepage = 0;
    mc_global.display_codepage = 0;
    cp_source = "ASCII";
    cp_display = "ASCII";

    do_set_codepage (0);
    edit_set_codeset (test_edit);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    edit_clean (test_edit);
    group_remove_widget (test_edit);
    g_free (test_edit);

    free_codepages_list ();
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kind_same_name_same_id)
{
    // given
    const int a = edit_marker_kind_register (&low);
    const int b = edit_marker_kind_register (&high);

    // then
    ck_assert_int_ge (a, 0);
    ck_assert_int_ne (a, b);
    ck_assert_int_eq (edit_marker_kind_register (&low), a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_find_higher_priority)
{
    const char *glyph = NULL;
    int glyph_color, line_color;

    // given
    book_mark_insert (test_edit, 3, EDIT_MARKER_BASE + edit_marker_kind_register (&high));
    book_mark_insert (test_edit, 3, EDIT_MARKER_BASE + edit_marker_kind_register (&low));

    // then
    ck_assert (edit_marker_find (test_edit, 3, &glyph, &glyph_color, &line_color));
    ck_assert_str_eq (glyph, "h");
    ck_assert_int_eq (line_color, 0);
    ck_assert (!edit_marker_find (test_edit, 2, &glyph, &glyph_color, &line_color));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_bookmark_is_no_marker)
{
    const char *glyph = NULL;
    int glyph_color, line_color;

    // given
    book_mark_insert (test_edit, 1, 7);

    // then
    ck_assert (!edit_marker_is (7));
    ck_assert (!edit_marker_find (test_edit, 1, &glyph, &glyph_color, &line_color));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_marker_moves_with_its_line)
{
    const int kind = EDIT_MARKER_BASE + edit_marker_kind_register (&low);
    const char *glyph = NULL;
    int glyph_color, line_color;

    // given
    insert_text ("one\ntwo\nthree\n");
    book_mark_insert (test_edit, 2, kind);

    // when: a line goes in above it
    edit_cursor_move (test_edit, -test_edit->buffer.curs1);
    insert_text ("zero\n");

    // then
    ck_assert (!edit_marker_find (test_edit, 2, &glyph, &glyph_color, &line_color));
    ck_assert (edit_marker_find (test_edit, 3, &glyph, &glyph_color, &line_color));
    ck_assert (book_mark_query_color (test_edit, 3, kind));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_first_is_head)
{
    const int kind = EDIT_MARKER_BASE + edit_marker_kind_register (&low);
    const edit_book_mark_t *p;
    int count = 0;

    // given: two marks on line 0, which book_mark_find (0) would give only the last of
    book_mark_insert (test_edit, 0, kind);
    book_mark_insert (test_edit, 0, kind);
    book_mark_insert (test_edit, 5, kind);

    // when
    for (p = edit_book_mark_first (test_edit); p != NULL; p = p->next)
        if (p->c == kind)
            count++;

    // then
    ck_assert_int_eq (count, 3);
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
    tcase_add_test (tc_core, test_kind_same_name_same_id);
    tcase_add_test (tc_core, test_find_higher_priority);
    tcase_add_test (tc_core, test_bookmark_is_no_marker);
    tcase_add_test (tc_core, test_marker_moves_with_its_line);
    tcase_add_test (tc_core, test_first_is_head);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
