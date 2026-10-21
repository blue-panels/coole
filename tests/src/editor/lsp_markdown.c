/*
   src/plugins/lsp - tests for the Markdown of the language servers in a note

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

#include <glib.h>

#include "src/plugins/lsp/lsp-markdown.h"

/* --------------------------------------------------------------------------------------------- */

/* The note of @markdown with its marks seen: { } around what is hot, # a heading, | code, - a
   rule */
static char *
note_of (const char *markdown)
{
    char *note = lsp_markdown_note (markdown);
    char *p;

    for (p = note; *p != '\0'; p++)
        switch (*p)
        {
        case LSP_NOTE_HOT_ON:
            *p = '{';
            break;
        case LSP_NOTE_HOT_OFF:
            *p = '}';
            break;
        case LSP_NOTE_HEADING:
            *p = '#';
            break;
        case LSP_NOTE_CODE:
            *p = '|';
            break;
        case LSP_NOTE_RULE:
            *p = '-';
            break;
        default:
            break;
        }
    return note;
}

/* --------------------------------------------------------------------------------------------- */

static void
check (const char *markdown, const char *expected)
{
    char *note = note_of (markdown);

    g_assert_cmpstr (note, ==, expected);
    g_free (note);
}

/* --------------------------------------------------------------------------------------------- */

/* What clangd says of a macro */
static void
test_clangd (void)
{
    check ("### macro `SIMPLE_STATUS_MSG`  \n"
           "provided by `\"lib/widget/wtools.h\"`  \n"
           "\n"
           "---\n"
           "Type: `simple_status_msg_t *` (aka `struct simple_status_msg_t *`)  \n"
           "\n"
           "---\n"
           "```cpp\n"
           "#define SIMPLE_STATUS_MSG(x) ((simple_status_msg_t *) (x))\n"
           "\n"
           "// Expands to\n"
           "((simple_status_msg_t *) (sm))\n"
           "```",
           "#macro {SIMPLE_STATUS_MSG}\n"
           "provided by {\"lib/widget/wtools.h\"}\n"
           "-\n"
           "Type: {simple_status_msg_t *} (aka {struct simple_status_msg_t *})\n"
           "-\n"
           "|\n"
           "|#define SIMPLE_STATUS_MSG(x) ((simple_status_msg_t *) (x))\n"
           "|\n"
           "|// Expands to\n"
           "|((simple_status_msg_t *) (sm))\n"
           "|");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_inline (void)
{
    // escapes, strong, links; one star, an underscore and a lone backquote are text
    check ("a\\_b \\*c\\* **strong `x` here** [link](http://x) char *p snake_case `open",
           "a_b *c* {strong x here} link char *p snake_case `open");
    // a run of backquotes holds fewer ones, the spaces around it go
    check ("``a ` b`` and ` c `", "{a ` b} and {c}");
    // the arrow of clangd before what a function gives, in words; elsewhere it stays
    check ("\xe2\x86\x92 `int`  \nx \xe2\x86\x92 y", "Returns: {int}\nx \xe2\x86\x92 y");
    // a heading closed by hashes, a line of hashes that is no heading
    check ("## Title ##\n#######x\n#tag", "#Title\n#######x\n#tag");
}

/* --------------------------------------------------------------------------------------------- */

static void
test_lines (void)
{
    // empty lines: none at the ends, nor two together, nor around a rule, nor two rules
    check ("\n\na\n\n\nb\n\n***\n\n- - -\n\nc\n\n---\n\n", "a\n\nb\n-\nc");
    // the dashes of a comment of the source, escaped, go, alone or before the documentation
    check ("a\n\n\\-----------  \nb\n\\=\\=\\=", "a\n\nb");
    check ("\\------------ Return index of the filter", "Return index of the filter");
    // two are no ornament
    check ("\\-- two", "-- two");
    // a fence of tildes, the code inside as it is, a line of a list stays
    check ("~~~\n  **x**  \n~~~\n- item", "|\n|  **x**  \n|\n- item");
    // a fence not closed takes the rest
    check ("text\n```\ncode", "text\n|\n|code");
    check ("", "");
}

/* --------------------------------------------------------------------------------------------- */

/* The source of a definition for what the server prints of it */
static void
test_with_code (void)
{
    static const char hover[] = "### type-alias `t`\n---\n```c\ntypedef struct s t\n```\nafter";
    static const char source[] = "typedef struct s\n{\n    int a;\n} t;\n\n";
    char *made;

    made = lsp_markdown_with_code (hover, source, 10);
    g_assert_cmpstr (
        made, ==,
        "### type-alias `t`\n---\n```c\ntypedef struct s\n{\n    int a;\n} t;\n```\nafter");
    g_free (made);
    // cut, the rest said
    made = lsp_markdown_with_code (hover, source, 2);
    g_assert_cmpstr (made, ==,
                     "### type-alias `t`\n---\n```c\ntypedef struct s\n{\n...\n```\nafter");
    g_free (made);
    // what the server prints has more: the expansion of a macro stays
    made = lsp_markdown_with_code ("```\n#define M 1\n\n// Expands to\n1\n```", "#define M 1", 10);
    g_assert_cmpstr (made, ==, "```\n#define M 1\n\n// Expands to\n1\n```");
    g_free (made);
    // no block: nothing to replace
    made = lsp_markdown_with_code ("text", "a\nb", 10);
    g_assert_cmpstr (made, ==, "text");
    g_free (made);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/lsp/markdown-clangd", test_clangd);
    g_test_add_func ("/lsp/markdown-inline", test_inline);
    g_test_add_func ("/lsp/markdown-lines", test_lines);
    g_test_add_func ("/lsp/markdown-with-code", test_with_code);
    return g_test_run ();
}

/* --------------------------------------------------------------------------------------------- */
