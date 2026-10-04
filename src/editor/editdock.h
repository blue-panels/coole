/** \file editdock.h
 *  \brief Header: the docks of the editor screen, where the windows of the plugins stand
 */

#ifndef MC__EDIT_DOCK_H
#define MC__EDIT_DOCK_H

#include "lib/global.h"
#include "lib/widget.h"

#include "editwindow.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/* Where a window stands: the column at the right, the row at the bottom, or among the windows
   of the files */
typedef enum
{
    EDIT_DOCK_NONE = 0,
    EDIT_DOCK_RIGHT,  // one above the other, all the height of the screen
    EDIT_DOCK_BOTTOM  // tabs: one of them seen, under the windows of the files
} edit_dock_side_t;

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Put a window, added already, in a dock: at the bottom of the column at the right, or as the
   tab seen at the bottom.  @size is the width of the column, or the height of the row, when
   the dock is made by it.  The windows are arranged again. */
void edit_dock_add (WEditWindow *win, edit_dock_side_t side, int size);
/* Take a window out of its dock, the others taking its room; FALSE when it was in none */
gboolean edit_dock_remove (WEditWindow *win);
edit_dock_side_t edit_dock_side (const WEditWindow *win);

/* The windows of the docks where they go, and those of the files in the rest of the screen:
   after a dock changes, and after the screen is resized */
void edit_dock_arrange (WDialog *h);

/* The tab seen at the bottom: @win, or the next (@step 1) or the previous one (-1) */
void edit_dock_tab_select (WEditWindow *win);
void edit_dock_tab_step (WDialog *h, int step);
/* A click on the title of a window: the tab there is seen; FALSE when it was no tab */
gboolean edit_dock_tab_click (WEditWindow *win, int x);
/* The tabs over the title of a window of the bottom, when there are several */
void edit_dock_draw_tabs (const WEditWindow *win, int color);

/* A window of a dock has been moved (it leaves the dock) or resized (the dock takes its size) */
void edit_dock_dragged (WEditWindow *win, gboolean moved);

/* The windows of a dock in their order, the tab seen of the bottom, and the sizes of the docks
   in percent of the screen; the shares of the column at the right, one for each window */
GPtrArray *edit_dock_windows (edit_dock_side_t side);
WEditWindow *edit_dock_tab (void);
void edit_dock_get_sizes (int *right, int *bottom);
void edit_dock_set_sizes (int right, int bottom);
int edit_dock_share (const WEditWindow *win);
void edit_dock_set_share (WEditWindow *win, int share);
/* Put the windows of a dock in this order: those of @order first, as they are there */
void edit_dock_order (edit_dock_side_t side, const GPtrArray *order);

/*** inline functions ****************************************************************************/

#endif /* MC__EDIT_DOCK_H */
