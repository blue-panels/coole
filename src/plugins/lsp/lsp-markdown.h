/** \file lsp-markdown.h
 *  \brief Header: the Markdown of a language server as the text of a note
 *
 *  Hover comes in Markdown: headings, rules, code in backquotes and in fenced blocks.  The text
 *  a note shows is lines with marks in bytes no text of a server has, its controls being
 *  replaced before: a line may start with the mark of a heading or of code, or be a rule, and
 *  the names to be seen are between the marks of what is hot.
 */

#ifndef MC__LSP_MARKDOWN_H
#define MC__LSP_MARKDOWN_H

#include <glib.h>

/*** typedefs(not structures) and defined constants **********************************************/

/* in a line: what is between them is hot */
#define LSP_NOTE_HOT_ON  '\001'
#define LSP_NOTE_HOT_OFF '\002'
/* the first byte of a line: a heading, a line of code; a line that is a rule alone */
#define LSP_NOTE_HEADING '\003'
#define LSP_NOTE_CODE    '\004'
#define LSP_NOTE_RULE    '\005'

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The text of a note of @markdown, which has no control but the new lines: the headings and the
   lines of the fenced blocks marked, the rules of the server alone on their lines, the escaped
   dashes of a comment of the source gone, the code in backquotes and what is strong hot, the
   escapes and the marks of Markdown gone, the arrow clangd puts before what a function gives
   said "Returns:"; no empty line at the ends, nor two together, nor around a rule */
char *lsp_markdown_note (const char *markdown);

/*** inline functions ****************************************************************************/

#endif
