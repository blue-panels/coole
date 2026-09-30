/** \file ansi_color.h
 *  \brief Header: the colors of ANSI text, in the colors of the skin
 */

#ifndef MC__VIEWER_ANSI_COLOR_H
#define MC__VIEWER_ANSI_COLOR_H

#include "lib/global.h"

#include "ansi.h"

/*** declarations of public functions ************************************************************/

/* The color pair that draws text with the SGR state @ansi on the skin section of @colors */
int mcview_ansi_color_of (const mcview_ansi_state_t *ansi, const mcview_canvas_colors_t *colors);

#endif
