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

/* The absolute path of a file with its links resolved, as far as the file is there: a new file
   in a directory that is there gets the directory's path.  NULL for a relative @path. */
char *lsp_canonical_path (const char *path);
/* The URI of a file, from its canonical path; NULL when it has none */
char *lsp_uri_from_path (const char *path);
/* The path of a "file:" URI on this host; NULL for any other */
char *lsp_path_from_uri (const char *uri);

/*** inline functions ****************************************************************************/

#endif
