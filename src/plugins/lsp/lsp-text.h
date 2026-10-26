/** \file lsp-text.h
 *  \brief Header: the places of a text as a language server has them
 *
 *  A language server names a file by its URI and a place in it by a line and a character, both
 *  from 0; a character counts UTF-16 code units, or bytes when the server and the client agree on
 *  UTF-8.  The editor has a byte offset.  Lines end at "\n", "\r\n" or "\r".
 */

#ifndef MC__LSP_TEXT_H
#define MC__LSP_TEXT_H

#include <glib.h>

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

typedef enum
{
    LSP_ENCODING_UTF16 = 0,  // what a server has unless it says otherwise
    LSP_ENCODING_UTF8
} lsp_encoding_t;

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The line and the character of the byte @offset of @text, @len bytes; FALSE when @offset is past
   the end.  An offset inside a character is taken as that character's start. */
gboolean lsp_text_position (const char *text, gsize len, gsize offset, lsp_encoding_t encoding,
                            guint *line, guint *character);
/* The byte offset of @line and @character in @text; a character past the end of its line is the
   end of the line.  FALSE for a line past the end of the text, a negative number, or a character
   that falls inside a character of the text. */
gboolean lsp_text_offset (const char *text, gsize len, gint64 line, gint64 character,
                          lsp_encoding_t encoding, gsize *offset);

/* The text of a snippet of LSP as plain text, and the byte the cursor goes to in it: that of the
   first tab stop, "$1", "${1:x}", else of "$0", else the end.  Placeholders, the first of a
   choice and the defaults of variables are what they hold; with @drop, all from the first tab
   stop to the end of the last goes, the arguments of "f(${1:int a}, ${2:int b})", the cursor
   then where they were: "f()" with the cursor in it. */
char *lsp_snippet_plain (const char *snippet, gboolean drop, gsize *cursor);

/* @text, @len bytes of SQL, with each character not of ASCII of its comments and strings, those
   of "--", "/ * * /", '...', E'...' and $tag$...$tag$, made '_', two of them for one that is two
   units of UTF-16: its lines and their characters in UTF-16 stay those of @text.  The names, of
   "..." too, stay as they are. */
char *lsp_text_sql_ascii (const char *text, gsize len);

/* The absolute path of a file with its links resolved, as far as the file is there: a new file
   in a directory that is there gets the directory's path.  NULL for a relative @path. */
char *lsp_canonical_path (const char *path);
/* The URI of a file, from its canonical path; NULL when it has none */
char *lsp_uri_from_path (const char *path);
/* The path of a "file:" URI on this host; NULL for any other */
char *lsp_path_from_uri (const char *uri);

/*** inline functions ****************************************************************************/

#endif
