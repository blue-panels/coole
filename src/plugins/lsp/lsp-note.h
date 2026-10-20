/** \file lsp-note.h
 *  \brief Header: a text over the file window, which takes no focus
 *
 *  What a language server says of a name, Hover: a box at the cursor, the text at its left as
 *  the server wrote it, the lines too wide for the screen going on under them.  The keys stay
 *  the window's: the plugin hides the box at the first one.  The mouse wheel scrolls a text
 *  longer than the box.
 */

#ifndef MC__LSP_NOTE_H
#define MC__LSP_NOTE_H

#include "lib/global.h"
#include "lib/widget.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct lsp_note_t lsp_note_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

lsp_note_t *lsp_note_new (void);
void lsp_note_free (lsp_note_t *note);

/* @text, lines of no control character, in @group over its other widgets, under the screen row
   @y from the column @x, above it when there is no room under it; drawn */
void lsp_note_show (lsp_note_t *note, WGroup *group, const char *title, const char *text, int y,
                    int x);
/* Out of its group, what it covered drawn again */
void lsp_note_hide (lsp_note_t *note);
gboolean lsp_note_shown (const lsp_note_t *note);

/*** inline functions ****************************************************************************/

#endif
