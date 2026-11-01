/*
   Tests for the modes of the terminal a program sets.

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

#define TEST_SUITE_NAME "/src/mcterm/vterm_modes"

#include "tests/mctest.h"

#include "src/viewer/vterm.h"
#include "src/viewer/terminal_buffer.h"

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static void
feed (mcview_vterm_t *vt, const char *data)
{
    const char *p;

    for (p = data; *p != '\0'; p++)
    {
        vterm_event_t ev = mcview_vterm_feed (vt, (unsigned char) *p);

        mcview_vterm_apply_event (vt, &ev);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gunichar
cell_ch (mcview_vterm_t *vt, int row, int col)
{
    const mcview_vterm_cell_t *cell;

    cell = mcview_terminal_buffer_get (mcview_vterm_buf (vt), row, col);
    return (cell != NULL) ? cell->ch : 0;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_bracketed_paste_mode)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 4, 20);
    mcview_vterm_reset (vt);

    ck_assert (!mcview_vterm_bracketed_paste (vt));
    feed (vt, "\033[?2004h");
    ck_assert (mcview_vterm_bracketed_paste (vt));
    feed (vt, "\033[?2004l");
    ck_assert (!mcview_vterm_bracketed_paste (vt));

    // every mode of the sequence is set, not only the first one
    feed (vt, "\033[?2004h");
    feed (vt, "\033[?1;2004l");
    ck_assert (!mcview_vterm_bracketed_paste (vt));
    feed (vt, "\033[?1;2004h");
    ck_assert (mcview_vterm_bracketed_paste (vt));
    ck_assert (mcview_vterm_app_cursor_keys (vt));

    // a query, a save and a restore of the modes change none of them
    feed (vt, "\033[?2004$p");
    feed (vt, "\033[?1;2004s");
    feed (vt, "\033[?1;2004r");
    ck_assert (mcview_vterm_bracketed_paste (vt));
    ck_assert (mcview_vterm_app_cursor_keys (vt));

    feed (vt, "\033[?2004h");
    mcview_vterm_reset (vt);
    ck_assert (!mcview_vterm_bracketed_paste (vt));

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* The reply of the terminal to what was fed last, or "" when there is none. */

static char *
feed_reply (mcview_vterm_t *vt, const char *data)
{
    GString *reply = g_string_new ("");
    size_t i;

    for (i = 0; data[i] != '\0'; i++)
    {
        vterm_event_t ev = mcview_vterm_feed (vt, (unsigned char) data[i]);

        if (ev.type == VTERM_REPLY && ev.reply != NULL)
            g_string_append (reply, ev.reply);
        mcview_vterm_apply_event (vt, &ev);
    }
    return g_string_free (reply, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static mcview_vterm_t *
kitty_vterm (void)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 20);
    mcview_vterm_reset (vt);
    return vt;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kitty_keyboard_query_push_pop)
{
    mcview_vterm_t *vt = kitty_vterm ();
    char *reply;

    reply = feed_reply (vt, "\033[?u");
    ck_assert_str_eq (reply, "\033[?0u");
    g_free (reply);

    feed (vt, "\033[>1u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 1);
    feed (vt, "\033[>31u");
    reply = feed_reply (vt, "\033[?u");
    ck_assert_str_eq (reply, "\033[?31u");
    g_free (reply);

    feed (vt, "\033[<u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 1);
    feed (vt, "\033[<5u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kitty_keyboard_set_modes)
{
    mcview_vterm_t *vt = kitty_vterm ();

    feed (vt, "\033[=5u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 5);
    feed (vt, "\033[=2;2u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 7);
    feed (vt, "\033[=4;3u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 3);
    feed (vt, "\033[=8;1u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 8);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kitty_keyboard_stack_per_screen)
{
    mcview_vterm_t *vt = kitty_vterm ();

    feed (vt, "\033[>1u");
    feed (vt, "\033[?1049h");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 0);
    feed (vt, "\033[>15u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 15);
    feed (vt, "\033[?1049l");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 1);

    mcview_vterm_reset (vt);
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kitty_keyboard_stack_limit)
{
    mcview_vterm_t *vt = kitty_vterm ();
    int i;

    for (i = 0; i < 40; i++)
        feed (vt, "\033[>3u");
    feed (vt, "\033[>9u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 9);
    for (i = 0; i < 40; i++)
        feed (vt, "\033[<u");
    ck_assert_uint_eq (mcview_vterm_kitty_flags (vt), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kitty_keyboard_leaves_the_cursor_alone)
{
    mcview_vterm_t *vt = kitty_vterm ();

    feed (vt, "\033[4;6H");
    feed (vt, "\033[>1u\033[=1;1u\033[?u\033[<u");
    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 3);
    ck_assert_int_eq (mcview_vterm_cursor_col (vt), 5);
    ck_assert_uint_eq (cell_ch (vt, 3, 5), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_bracketed_paste_mode);
    tcase_add_test (tc_core, test_kitty_keyboard_query_push_pop);
    tcase_add_test (tc_core, test_kitty_keyboard_set_modes);
    tcase_add_test (tc_core, test_kitty_keyboard_stack_per_screen);
    tcase_add_test (tc_core, test_kitty_keyboard_stack_limit);
    tcase_add_test (tc_core, test_kitty_keyboard_leaves_the_cursor_alone);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
