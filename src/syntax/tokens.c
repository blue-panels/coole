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
 *
 *  The colors of a token are looked up as Sublime Text matches its selectors:
 *  the longest part of the scope that is named wins, keyword.control before
 *  keyword, and for a token in a context of a token the two together before
 *  the token alone, "string constant.character.escape" before
 *  constant.character.escape.  The lookup asks whoever drawing the colors set
 *  it, the skin; what it does not name comes from the colors of mcedit below.
 */

#include <config.h>

#include <string.h>

#include "tokens.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *key;
    const char *fg;
    const char *bg;
} syntax_token_classic_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* the colors mcedit gave these kinds of text, for what the lookup leaves out */
static const syntax_token_classic_t classic[] = {
    { "comment", "brown", NULL },
    { "keyword", "yellow", NULL },
    { "storage", "yellow", NULL },
    // the types of mermaid diagrams and their arrows, as the mermaid rules had them
    { "storage.type.diagram", "brightmagenta", NULL },
    { "keyword.operator.arrow", "brightred", NULL },
    { "constant.language", "yellow", NULL },
    { "variable.language", "yellow", NULL },
    { "keyword.other.todo", "brightred", NULL },
    { "support", "brightred", NULL },
    { "support.type.property.name", "yellow", NULL },
    { "entity.name", "brightcyan", NULL },
    { "string", "green", NULL },
    { "constant", "brightgreen", NULL },
    { "punctuation", "brightcyan", NULL },
    { "punctuation.terminator", "brightmagenta", NULL },
    { "meta.preprocessor", "brightred", NULL },
    { "meta.preprocessor string", "red", NULL },
    { "meta.preprocessor punctuation", "yellow", NULL },
    { "meta.preprocessor comment", "brown", NULL },
    { "markup.heading", "brightcyan", NULL },
    { "markup.bold", "white", NULL },
    { "markup.italic", "yellow", NULL },
    { "markup.underline.link", "yellow", NULL },
    { "markup.list", "yellow", NULL },
    { "markup.quote", "green", NULL },
    { "markup.raw", "green", NULL },
    { "markup.inserted", "brightgreen", NULL },
    { "markup.deleted", "brightred", NULL },
    { "markup.changed", "yellow", NULL },
    { "meta.diff.range", "brightcyan", NULL },
    { "meta.diff.header", "brightmagenta", NULL },
    { "invalid", "white", "red" },
};

static syntax_token_lookup_fn token_lookup = NULL;
static void *token_lookup_data = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
classic_lookup (const char *key, char **fg, char **bg, char **attrs, void *data)
{
    size_t i;

    (void) data;

    for (i = 0; i < G_N_ELEMENTS (classic); i++)
        if (strcmp (classic[i].key, key) == 0)
        {
            *fg = g_strdup (classic[i].fg);
            *bg = g_strdup (classic[i].bg);
            *attrs = NULL;
            return TRUE;
        }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/** a.b.c, a.b, a: the scope and the shorter scopes it is a kind of, longest first */
static GPtrArray *
scope_prefixes (const char *token)
{
    GPtrArray *prefixes;
    char *scope;
    char *dot;

    prefixes = g_ptr_array_new_with_free_func (g_free);
    if (token == NULL)
        return prefixes;

    scope = syntax_token_scope (token);
    while (TRUE)
    {
        g_ptr_array_add (prefixes, g_strdup (scope));
        dot = strrchr (scope, '.');
        if (dot == NULL)
            break;
        *dot = '\0';
    }
    g_free (scope);

    return prefixes;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lookup_in (syntax_token_lookup_fn lookup, void *data, const GPtrArray *ctx, const GPtrArray *own,
           char **fg, char **bg, char **attrs)
{
    guint i, j;

    for (i = 0; i < own->len; i++)
    {
        const char *scope = g_ptr_array_index (own, i);

        for (j = 0; j < ctx->len; j++)
        {
            char *key;
            gboolean found;

            key = g_strconcat (g_ptr_array_index (ctx, j), " ", scope, (char *) NULL);
            found = lookup (key, fg, bg, attrs, data);
            g_free (key);
            if (found)
                return TRUE;
        }

        if (lookup (scope, fg, bg, attrs, data))
            return TRUE;
    }

    return FALSE;
}

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

/**
 * Where the colors of the tokens come from from now on, the skin of the program; NULL
 * leaves the colors of mcedit alone.
 */

void
syntax_token_set_lookup (syntax_token_lookup_fn lookup, void *data)
{
    token_lookup = lookup;
    token_lookup_data = data;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * The colors of @token standing in a context of @ctx_token (NULL for none): the ones
 * the lookup gives, else those of mcedit, else NULLs, the colors of the text around.
 * The strings are new, to be freed with g_free().
 *
 * @return TRUE when the colors are the lookup's, the skin's, not mcedit's
 */

gboolean
syntax_token_resolve (const char *ctx_token, const char *token, char **fg, char **bg, char **attrs)
{
    GPtrArray *ctx, *own;
    gboolean found = FALSE;

    *fg = NULL;
    *bg = NULL;
    *attrs = NULL;

    ctx = scope_prefixes (ctx_token);
    own = scope_prefixes (token);

    if (token_lookup != NULL)
        found = lookup_in (token_lookup, token_lookup_data, ctx, own, fg, bg, attrs);
    if (!found)
        lookup_in (classic_lookup, NULL, ctx, own, fg, bg, attrs);

    g_ptr_array_free (ctx, TRUE);
    g_ptr_array_free (own, TRUE);

    return found;
}

/* --------------------------------------------------------------------------------------------- */
