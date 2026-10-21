/** \file lsp-note.h
 *  \brief Header: a text over the file window, which takes no focus
 *
 *  What a language server says of a name, Hover: a box at the cursor, the text at its left as
 *  the server wrote it, the lines too wide for the screen going on under them.  Its headings,
 *  rules, code and names, marked as lsp-markdown.h has them, are drawn in the colors of the
 *  skin, the code on those of the editor.  The keys stay the window's but PgUp and PgDn, which
 *  scroll a text longer than the box, as the mouse wheel does; once the user reads it, the
 *  arrows, Home and End scroll it too.  The plugin hides the box at another key.
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

/* @text, lines of no control character but the marks of lsp-markdown.h, in @group over its other
   widgets, under the screen row @y from the column @x, above it when there is no room under it;
   drawn; @read_key, the key that has it read, said on its frame when it is longer than the box,
   NULL for none */
void lsp_note_show (lsp_note_t *note, WGroup *group, const char *title, const char *read_key,
                    const char *text, int y, int x);
/* Over the place it is shown at when @above, under it else, when there is room; @max_lines of
   the text at most at once, 0 for half of the screen */
void lsp_note_set_place (lsp_note_t *note, gboolean above, int max_lines);
/* Out of its group, what it covered drawn again */
void lsp_note_hide (lsp_note_t *note);
/* Whether the box is there, out of sight too, when the screen has no room for it */
gboolean lsp_note_there (const lsp_note_t *note);
gboolean lsp_note_shown (const lsp_note_t *note);
/* A key while the box is shown: PgUp and PgDn scroll it, and, once it is read, the arrows, Home
   and End; TRUE when the key was its */
gboolean lsp_note_key (lsp_note_t *note, int key);
/* The keys that move the cursor scroll the box now, until it goes */
void lsp_note_read (lsp_note_t *note);
gboolean lsp_note_reading (const lsp_note_t *note);

/*** inline functions ****************************************************************************/

#endif
