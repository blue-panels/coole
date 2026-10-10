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

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_bracketed_paste_mode);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
