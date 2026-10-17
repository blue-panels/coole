/*
   tests/src/syntax/tokens.c -- unit tests for scope tokens and their colors

   Copyright (C) 2026
   Ilia Maslakov il.smind@gmail.com

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
   along with this program.  If not, see https://www.gnu.org/licenses/.
 */

#define TEST_SUITE_NAME "/src/syntax"

#include "tests/mctest.h"

#include <string.h>

#include "lib/global.h"
#include "src/syntax/tokens.h"

/* a skin of the tests: keys and "fg;bg" values, NULL-terminated */
static const char *const *skin = NULL;

/* --------------------------------------------------------------------------------------------- */

static gboolean
skin_lookup (const char *key, char **fg, char **bg, char **attrs, void *data)
{
    const char *const *k;

    (void) data;

    for (k = skin; *k != NULL; k += 2)
        if (strcmp (*k, key) == 0)
        {
            char **parts = g_strsplit (k[1], ";", 3);

            *fg = parts[0] != NULL && *parts[0] != '\0' ? g_strdup (parts[0]) : NULL;
            *bg = parts[0] != NULL && parts[1] != NULL && *parts[1] != '\0' ? g_strdup (parts[1])
                                                                              : NULL;
            *attrs = NULL;
            g_strfreev (parts);
            return TRUE;
        }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/** The foreground and background @token gets in @ctx, as "fg;bg" with empty parts. */
static char *
colors_of (const char *ctx, const char *token)
{
    char *fg, *bg, *attrs, *out;

    syntax_token_resolve (ctx, token, &fg, &bg, &attrs);
    out = g_strdup_printf ("%s;%s", fg != NULL ? fg : "", bg != NULL ? bg : "");
    g_free (fg);
    g_free (bg);
    g_free (attrs);

    return out;
}

/* --------------------------------------------------------------------------------------------- */

static void
check (const char *ctx, const char *token, const char *expected)
{
    char *got = colors_of (ctx, token);

    ck_assert_msg (strcmp (got, expected) == 0, "%s in %s: expected [%s], got [%s]", token,
                   ctx != NULL ? ctx : "-", expected, got);
    g_free (got);
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    syntax_token_set_lookup (NULL, NULL);
    skin = NULL;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_is_token)
{
    ck_assert (syntax_token_is ("KEYWORD_CONTROL"));
    ck_assert (syntax_token_is ("H1"));
    ck_assert (!syntax_token_is ("yellow"));
    ck_assert (!syntax_token_is ("rgb345"));
    ck_assert (!syntax_token_is ("color200"));
    ck_assert (!syntax_token_is ("Keyword"));
    ck_assert (!syntax_token_is ("_1"));
    ck_assert (!syntax_token_is (""));
    ck_assert (!syntax_token_is (NULL));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_scope)
{
    char *scope = syntax_token_scope ("KEYWORD_CONTROL_IMPORT");

    ck_assert_str_eq (scope, "keyword.control.import");
    g_free (scope);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_classic)
{
    // with no skin, the colors of mcedit: the kind of text named, or a shorter one
    check (NULL, "KEYWORD_CONTROL", "yellow;");
    check (NULL, "KEYWORD_OTHER_TODO", "brightred;");
    check (NULL, "STRING_QUOTED_DOUBLE", "green;");
    check (NULL, "INVALID_DEPRECATED", "white;red");
    // a string in a preprocessor line is red, elsewhere green
    check ("META_PREPROCESSOR", "STRING_QUOTED_DOUBLE", "red;");
    check ("COMMENT_LINE", "STRING_QUOTED_DOUBLE", "green;");
    // a kind of text mcedit had no color for keeps the color around it
    check (NULL, "VARIABLE_OTHER", ";");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_skin)
{
    static const char *const s[] = {
        "keyword", "red",
        "keyword.control", "magenta",
        "string", "cyan",
        "string constant.character.escape", "white;black",
        "constant", "blue",
        NULL,
    };

    skin = s;
    syntax_token_set_lookup (skin_lookup, NULL);

    // the longest part of the scope the skin names wins
    check (NULL, "KEYWORD_CONTROL_IMPORT", "magenta;");
    check (NULL, "KEYWORD_OTHER", "red;");
    // a context and a token together go before the token alone
    check ("STRING_QUOTED_DOUBLE", "CONSTANT_CHARACTER_ESCAPE", "white;black");
    check (NULL, "CONSTANT_CHARACTER_ESCAPE", "blue;");
    // what the skin names goes before mcedit's colors, a closer one of them too
    check ("META_PREPROCESSOR", "STRING_QUOTED_DOUBLE", "cyan;");
    // what the skin does not name is mcedit's
    check (NULL, "COMMENT_LINE", "brown;");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, NULL, teardown);

    tcase_add_test (tc_core, test_is_token);
    tcase_add_test (tc_core, test_scope);
    tcase_add_test (tc_core, test_classic);
    tcase_add_test (tc_core, test_skin);

    return mctest_run_all (tc_core);
}
