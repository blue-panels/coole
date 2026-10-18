/*
   Lexical scanner for coole
   Header: scope tokens

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

/** \file tokens.h
 *  \brief Header: scope tokens, the colors that say what the text is
 */

#ifndef MC__SYNTAX_TOKENS_H
#define MC__SYNTAX_TOKENS_H

#include "lib/global.h"

/**
 * Where the colors of the tokens come from: the colors @key gives, as names, or FALSE
 * when it gives none.  @key is a scope, keyword.control, or the scope of a context and
 * a scope, "string constant.character.escape".
 */
typedef gboolean (*syntax_token_lookup_fn) (const char *key, char **fg, char **bg, char **attrs,
                                            void *data);

/*** declarations of public functions ************************************************************/

gboolean syntax_token_is (const char *word);
char *syntax_token_scope (const char *token);

void syntax_token_set_lookup (syntax_token_lookup_fn lookup, void *data);
gboolean syntax_token_resolve (const char *ctx_token, const char *token, char **fg, char **bg,
                               char **attrs);

#endif
