/*
   Fuzzy symbol matching for the ctags plugin.

   Copyright (C) 2025-2026
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

#include <config.h>

#include <string.h>

#include "lib/global.h"

#include "ctags-parser.h"
#include "src/fuzzy.h"

#include "ctags-fuzzy.h"

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    ctags_entry_t *entry;
    int score;
} fuzzy_result_t;

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static int
compare_fuzzy_result (const void *a, const void *b)
{
    const fuzzy_result_t *ra = (const fuzzy_result_t *) a;
    const fuzzy_result_t *rb = (const fuzzy_result_t *) b;

    return rb->score - ra->score; /* descending */
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_fuzzy_search (const GPtrArray *entries, const char *query)
{
    GArray *results;
    GPtrArray *out;
    guint i;

    if (entries == NULL || query == NULL || *query == '\0')
        return NULL;

    results = g_array_new (FALSE, FALSE, sizeof (fuzzy_result_t));

    for (i = 0; i < entries->len; i++)
    {
        ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (entries, i);
        int score;

        if (e->name == NULL)
            continue;

        score = fuzzy_score (e->name, query, NULL);
        if (score > 0)
        {
            fuzzy_result_t r;

            r.entry = e;
            r.score = score;
            g_array_append_val (results, r);
        }
    }

    if (results->len == 0)
    {
        g_array_free (results, TRUE);
        return NULL;
    }

    g_array_sort (results, compare_fuzzy_result);

    out = g_ptr_array_new ();
    for (i = 0; i < results->len; i++)
        g_ptr_array_add (out, g_array_index (results, fuzzy_result_t, i).entry);

    g_array_free (results, TRUE);
    return out;
}

/* --------------------------------------------------------------------------------------------- */
