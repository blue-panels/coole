/** \file
 *  \brief Header: a window of the editor screen that shows text it is given
 *
 *  The text is what a program prints for a terminal or a pager: overstrikes (c\bc bold, _\bc
 *  underlined) and ANSI colors (ESC [ ... m). The window lays nothing out: a line of the text is
 *  a line of the window, and one wider than the window is scrolled sideways.
 */

#ifndef MC__VIEWER_TEXT_WINDOW_H
#define MC__VIEWER_TEXT_WINDOW_H

#include "lib/global.h"

#include "src/editor/editwindow.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* A window of text at @r, not fullscreen, titled @title. Caller adds it to the screen */
WEditWindow *edit_text_window_new (const WRect *r, const char *title);
gboolean edit_text_window_is (const Widget *w);

void edit_text_window_set_title (WEditWindow *win, const char *title);
/* Show @text, @len bytes of it; the view stays where it was as far as the text reaches */
void edit_text_window_set_text (WEditWindow *win, const char *text, gsize len);
/* Make @line, from 0, the top line of the view, as far as the text lets it */
void edit_text_window_scroll_to (WEditWindow *win, long line);

long edit_text_window_lines (const WEditWindow *win);
long edit_text_window_top (const WEditWindow *win);
/* The columns and lines the text has in the window, inside its frame */
int edit_text_window_text_cols (const WEditWindow *win);
int edit_text_window_text_lines (const WEditWindow *win);

/* @fn is called with @data when the window is destroyed */
void edit_text_window_on_destroy (WEditWindow *win, void (*fn) (void *data), void *data);

/*** inline functions ****************************************************************************/

#endif
