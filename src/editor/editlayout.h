/** \file editlayout.h
 *  \brief Header: the layouts of the windows of the editor
 */

#ifndef MC__EDIT_LAYOUT_H
#define MC__EDIT_LAYOUT_H

#include "lib/global.h"
#include "lib/widget.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The editor is up: the windows of the project of the file as they were the last time, or
   those of the layout Debug for coole --debug */
void edit_layout_startup (void *dialog);
/* The editor ends: the windows of the project are kept for the next time */
void edit_layout_quit (WDialog *h);
/* Window > Layout...: one to put the windows as it has them, one to keep, the switch to Debug
   while debugging */
void edit_layout_dialog (WDialog *h);

/* The windows as the layout @name has them, those of the moment kept for edit_layout_pop() */
gboolean edit_layout_push (WDialog *h, const char *name);
void edit_layout_pop (WDialog *h);

/*** inline functions ****************************************************************************/

#endif /* MC__EDIT_LAYOUT_H */
