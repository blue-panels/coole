/** \file editnav.h
 *  \brief Header: the places the jumps of the editor go from and to
 *
 *  Every jump to a place of a file, to a definition, a use, a problem, a tag, records the place
 *  it leaves and the place it reaches, as a browser does its pages: Go back and Go forward walk
 *  them, a jump after Go back drops what was ahead, and the Navigation history lists them.  A
 *  file open in a window is shown in it, the cursor put on the place: it is not loaded again,
 *  its undo kept.
 */

#ifndef MC__EDIT_NAV_H
#define MC__EDIT_NAV_H

#include "lib/global.h"

#include "edit.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* the places recorded at most */
#define EDIT_NAV_MAX 100

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The cursor to @column, from 0, of the line it is on, and the view following it, as a search
   puts it on what it found.  @tab_width 0: @column counts bytes; else it counts the columns of
   the screen as a compiler does, a tab going to the next multiple of @tab_width and a wide
   character taking two.  Past the end of the line: the end. */
void edit_cursor_to_column (WEdit *edit, long column, int tab_width);
/* From where @edit is to @file, at @line from 1 and @column bytes into the line: the window of
   the file to the front when one has it, else the file loaded in @edit, or in a new window when
   @edit has changes not saved.  FALSE when the file cannot be opened. */
gboolean edit_nav_jump (WEdit *edit, const char *file, long line, long column);
/* To the place before, after; FALSE when there is none */
gboolean edit_nav_back (WEdit *edit);
gboolean edit_nav_forward (WEdit *edit);
/* The list of the places, the one chosen gone to */
void edit_nav_history (WEdit *edit);
/* The places forgotten: the editor ends */
void edit_nav_free (void);

/*** inline functions ****************************************************************************/

#endif
