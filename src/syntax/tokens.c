/*
   Lexical scanner for coole
   Scope tokens

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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

/** \file tokens.c
 *  \brief Source: scope tokens, the colors that say what the text is
 *
 *  A rule of a .syntax file may name, where a foreground color stands, a token
 *  such as KEYWORD_CONTROL instead of a color such as yellow: what the text is,
 *  not how it looks.  The look is the skin's.  A token is written in capitals,
 *  digits and underscores, and is read as a scope, the token in lower case with
 *  dots for the underscores: KEYWORD_CONTROL_IMPORT is keyword.control.import,
 *  a kind of keyword.control, which is a kind of keyword.
 */

#include <config.h>

#include "tokens.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * Whether a color word of a rule is a scope token: capitals, digits and
 * underscores, with a capital among them.  No color name is spelled so.
 */

gboolean
syntax_token_is (const char *word)
{
    gboolean capital = FALSE;

    if (word == NULL || *word == '\0')
        return FALSE;

    for (; *word != '\0'; word++)
    {
        if (g_ascii_isupper (*word))
            capital = TRUE;
        else if (!g_ascii_isdigit (*word) && *word != '_')
            return FALSE;
    }

    return capital;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * The scope a token stands for: KEYWORD_CONTROL_IMPORT is keyword.control.import.
 *
 * @return a new string, to be freed with g_free()
 */

char *
syntax_token_scope (const char *token)
{
    char *scope, *p;

    scope = g_ascii_strdown (token, -1);
    for (p = scope; *p != '\0'; p++)
        if (*p == '_')
            *p = '.';

    return scope;
}

/* --------------------------------------------------------------------------------------------- */
