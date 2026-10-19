/*
   The fuzzy match of a name by what is typed, for the symbols of ctags and the completion of the
   language servers.

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

#include <string.h>

#include "lib/global.h"

#include "fuzzy.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define SCORE_MATCH         1 /* a character found */
#define SCORE_WORD_BOUNDARY 5 /* found at the start of a word of the name */
#define SCORE_CONSECUTIVE   8 /* found right after the character before it */

/* the longest name and query the best way is looked for in; past them, the first way */
#define FUZZY_MAX_NAME  512
#define FUZZY_MAX_QUERY 64

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Whether a word of @name starts at @pos: its start, after '_' or another character that is no
   letter nor digit, an upper case letter after a lower case one */
static gboolean
fuzzy_word_start (const char *name, int pos)
{
    if (pos == 0)
        return TRUE;
    if (!g_ascii_isalnum (name[pos - 1]) && g_ascii_isalnum (name[pos]))
        return TRUE;
    return g_ascii_islower (name[pos - 1]) && g_ascii_isupper (name[pos]);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
fuzzy_same (char a, char b)
{
    return g_ascii_tolower (a) == g_ascii_tolower (b);
}

/* --------------------------------------------------------------------------------------------- */

/* The first way, character after character: for what is too long for the best one */
static int
fuzzy_first_way (const char *name, const char *query, GArray *positions)
{
    int score = 0, prev = -1, pos;
    const char *q = query;

    for (pos = 0; name[pos] != '\0' && *q != '\0'; pos++)
        if (fuzzy_same (name[pos], *q))
        {
            score += SCORE_MATCH;
            if (prev == pos - 1)
                score += SCORE_CONSECUTIVE;
            if (fuzzy_word_start (name, pos))
                score += SCORE_WORD_BOUNDARY;
            if (positions != NULL)
                g_array_append_val (positions, pos);
            prev = pos;
            q++;
        }
    if (*q != '\0')
    {
        if (positions != NULL)
            g_array_set_size (positions, 0);
        return 0;
    }
    return score;
}

/* --------------------------------------------------------------------------------------------- */

/* The score of the best way, as fuzzy_score () has it, with the row of the characters of the
   query before the one of each, and its own: a match of every key typed in a list of names */
static int
fuzzy_best_score (const char *name, int n, const char *query, int m)
{
    int rows[2][FUZZY_MAX_NAME];
    int *prev = rows[0], *cur = rows[1];
    int i, j, score = 0;

    for (j = 0; j < m; j++)
    {
        int before = -1;  // the best of the row before up to i - 2

        for (i = 0; i < n; i++)
        {
            if (j > 0 && i >= 2 && prev[i - 2] > before)
                before = prev[i - 2];
            cur[i] = -1;
            if (!fuzzy_same (name[i], query[j]))
                continue;
            if (j == 0)
                cur[i] = 0;
            else
            {
                if (i >= 1 && prev[i - 1] >= 0)
                    cur[i] = prev[i - 1] + SCORE_CONSECUTIVE;
                if (before >= 0 && before > cur[i])
                    cur[i] = before;
                if (cur[i] < 0)
                    continue;
            }
            cur[i] += SCORE_MATCH + (fuzzy_word_start (name, i) ? SCORE_WORD_BOUNDARY : 0);
        }
        {
            int *t = prev;

            prev = cur;
            cur = t;
        }
    }
    for (i = 0; i < n; i++)
        score = MAX (score, prev[i]);
    return score;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
fuzzy_match (const char *name, const char *query)
{
    const char *q;

    if (name == NULL || query == NULL)
        return FALSE;
    for (q = query; *name != '\0' && *q != '\0'; name++)
        if (fuzzy_same (*name, *q))
            q++;
    return *q == '\0';
}

/* --------------------------------------------------------------------------------------------- */

int
fuzzy_score (const char *name, const char *query, GArray *positions)
{
    int n, m, i, j, best_i = -1, score = 0;
    int *best, *from;

    if (positions != NULL)
        g_array_set_size (positions, 0);
    if (name == NULL || query == NULL || *query == '\0' || !fuzzy_match (name, query))
        return 0;
    n = (int) strlen (name);
    m = (int) strlen (query);
    if (n > FUZZY_MAX_NAME || m > FUZZY_MAX_QUERY)
        return fuzzy_first_way (name, query, positions);
    // the score alone: two rows of the table, no memory taken
    if (positions == NULL)
        return fuzzy_best_score (name, n, query, m);

    /* best[j * n + i]: the best score of query[0..j] with query[j] found at name[i], -1 when it
       cannot be; from[]: where query[j - 1] is found then */
    best = g_new (int, (gsize) n *(gsize) m);
    from = g_new (int, (gsize) n *(gsize) m);
    for (j = 0; j < m; j++)
    {
        int before = -1, before_at = -1;  // the best of row j - 1 up to i - 2

        for (i = 0; i < n; i++)
        {
            const int k = j * n + i;

            best[k] = -1;
            from[k] = -1;
            if (j > 0 && i >= 2 && best[k - n - 2] > before)
            {
                before = best[k - n - 2];
                before_at = i - 2;
            }
            if (!fuzzy_same (name[i], query[j]))
                continue;
            if (j == 0)
                best[k] = 0;
            else
            {
                // right after the one before, or anywhere before it
                if (i >= 1 && best[k - n - 1] >= 0)
                {
                    best[k] = best[k - n - 1] + SCORE_CONSECUTIVE;
                    from[k] = i - 1;
                }
                if (before >= 0 && before > best[k])
                {
                    best[k] = before;
                    from[k] = before_at;
                }
                if (best[k] < 0)
                    continue;
            }
            best[k] += SCORE_MATCH + (fuzzy_word_start (name, i) ? SCORE_WORD_BOUNDARY : 0);
        }
    }
    for (i = 0; i < n; i++)
        if (best[(m - 1) * n + i] > score)
        {
            score = best[(m - 1) * n + i];
            best_i = i;
        }
    if (positions != NULL && best_i >= 0)
    {
        g_array_set_size (positions, (guint) m);
        for (j = m - 1, i = best_i; j >= 0; j--)
        {
            g_array_index (positions, int, j) = i;
            i = from[j * n + i];
        }
    }
    g_free (best);
    g_free (from);
    return score;
}

/* --------------------------------------------------------------------------------------------- */
