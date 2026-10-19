/*
   src - tests for the fuzzy match of a name

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

#include "src/fuzzy.h"

/* --------------------------------------------------------------------------------------------- */

/* The characters of @name at the positions found, upper case, the others lower */
static char *
marked (const char *name, const char *query)
{
    GArray *positions = g_array_new (FALSE, FALSE, sizeof (int));
    char *out = g_ascii_strdown (name, -1);
    guint i;

    if (fuzzy_score (name, query, positions) > 0)
        for (i = 0; i < positions->len; i++)
            out[g_array_index (positions, int, i)] =
                g_ascii_toupper (out[g_array_index (positions, int, i)]);
    else
    {
        g_free (out);
        out = NULL;
    }
    g_array_free (positions, TRUE);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

static void
check (const char *name, const char *query, const char *expected)
{
    char *got = marked (name, query);

    g_assert_cmpstr (got, ==, expected);
    g_free (got);
}

/* --------------------------------------------------------------------------------------------- */

/* The best way, not the first: the start of a word, the characters one after the other */
static void
test_positions (void)
{
    check ("AddMongoDb", "db", "addmongoDB");
    check ("AddApiDatabasePolicy", "d", "addapiDatabasepolicy");
    check ("AddApiDatabasePolicy", "dp", "addapiDatabasePolicy");
    check ("make_global", "mkg", "MaKe_Global");
    check ("make_global", "glob", "make_GLOBal");
    check ("edit_cursor_move", "ecm", "Edit_Cursor_Move");
    check ("Width", "dw", NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_scores (void)
{
    // the score alone is that of the best way with its positions
    {
        static const char *const names[] = { "AddMongoDb", "make_global", "edit_cursor_move",
                                             "getWidth",   "WorkingId",   "aaaaabbbb" };
        static const char *const queries[] = { "db", "mkg", "ecm", "width", "wid", "ab" };
        GArray *positions = g_array_new (FALSE, FALSE, sizeof (int));
        gsize i, k;

        for (i = 0; i < G_N_ELEMENTS (names); i++)
            for (k = 0; k < G_N_ELEMENTS (queries); k++)
                g_assert_cmpint (fuzzy_score (names[i], queries[k], NULL), ==,
                                 fuzzy_score (names[i], queries[k], positions));
        g_array_free (positions, TRUE);
    }
    g_assert_cmpint (fuzzy_score ("Width", "", NULL), ==, 0);
    g_assert_cmpint (fuzzy_score (NULL, "x", NULL), ==, 0);
    // a word that starts so before one that has the characters somewhere
    g_assert_cmpint (fuzzy_score ("glob", "glob", NULL), >, fuzzy_score ("gxlxoxb", "glob", NULL));
    g_assert_cmpint (fuzzy_score ("make_global", "mg", NULL), >,
                     fuzzy_score ("imagine", "mg", NULL));
    g_assert_cmpint (fuzzy_score ("getWidth", "width", NULL), >,
                     fuzzy_score ("WorkingId", "wid", NULL));
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/fuzzy/positions", test_positions);
    g_test_add_func ("/fuzzy/scores", test_scores);
    return g_test_run ();
}
