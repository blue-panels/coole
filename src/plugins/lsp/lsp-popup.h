/** \file lsp-popup.h
 *  \brief Header: a list over the text, which takes no focus
 *
 *  The list of a completion: it is drawn over the file window under the word typed, and the keys
 *  stay the window's; the plugin moves the choice and takes it.  An item has a letter of its
 *  kind, its name, with the characters of the name that match what is typed in another color,
 *  and what it is, at the right.
 */

#ifndef MC__LSP_POPUP_H
#define MC__LSP_POPUP_H

#include "lib/global.h"
#include "lib/widget.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* the items seen at once */
#define LSP_POPUP_LINES 10

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    char kind;     // a letter, ' ' for none
    char *label;   // what is shown
    char *detail;  // what it is, NULL for nothing
    GArray *hits;  // int: the bytes of the label that match what is typed; NULL for none
    void *data;    // of the plugin
} lsp_popup_item_t;

typedef struct lsp_popup_t lsp_popup_t;

/* An item taken by the mouse: a double click */
typedef void (*lsp_popup_take_fn) (void *data);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

lsp_popup_t *lsp_popup_new (lsp_popup_take_fn take, void *data);
void lsp_popup_free (lsp_popup_t *popup);

/* A new item; @label and @detail copied, @hits taken */
lsp_popup_item_t *lsp_popup_item_new (char kind, const char *label, const char *detail,
                                      GArray *hits, void *data);
/* The items, taken; the first chosen */
void lsp_popup_set_items (lsp_popup_t *popup, GPtrArray *items);

/* The list in @group, over its other widgets, under the screen row @y from the column @x where
   the word starts, above it when there is no room under it; drawn */
void lsp_popup_show (lsp_popup_t *popup, WGroup *group, int y, int x);
/* Out of its group, what it covered drawn again */
void lsp_popup_hide (lsp_popup_t *popup);
gboolean lsp_popup_shown (const lsp_popup_t *popup);
/* The first row of the screen the list is shown at; -1 when it is not shown */
int lsp_popup_top (const lsp_popup_t *popup);
void lsp_popup_draw (lsp_popup_t *popup);

/* The choice moved by @lines, a page for LSP_POPUP_LINES; it stops at the ends */
void lsp_popup_move (lsp_popup_t *popup, int lines);
/* The item chosen, NULL when there is none */
lsp_popup_item_t *lsp_popup_current (const lsp_popup_t *popup);

/*** inline functions ****************************************************************************/

#endif
