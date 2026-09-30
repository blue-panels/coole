/*
   The cells of the terminal drawn on the screen.

   Copyright (C) 1994-2026
   Free Software Foundation, Inc.

   Written by:
   Andrew Borodin <aborodin@vmail.ru>, 2009-2022
   Ilia Maslakov <il.smind@gmail.com>, 2009, 2010, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/** ile
 *  rief Source: the cells of the terminal drawn on the screen
 *
 *  In mc these functions are a part of the viewer (ascii.c and display.c); coole has the
 *  terminal without the viewer.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/tty/tty.h"
#include "lib/tty/color.h"

#include "ansi_color.h"
#include "vterm.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
mcview_render_terminal_canvas (const mcview_terminal_buffer_t *buf, int top_row, int screen_y,
                               int screen_x, int rows, int cols,
                               const mcview_canvas_colors_t *colors)
{
    int row, col;

    tty_setcolor (colors->normal);

    for (row = 0; row < rows; row++)
    {
        int canvas_row = top_row + row;

        for (col = 0; col < cols; col++)
        {
            const mcview_vterm_cell_t *cell;

            /* Position explicitly per column so that ambiguous-width characters
             * printed as 2-wide by tty_print_anychar() do not shift subsequent
             * cells to the right on screen. */
            tty_gotoyx (screen_y + row, screen_x + col);

            cell = mcview_terminal_buffer_get (buf, canvas_row, col);

            if (cell != NULL && cell->ch != 0 && cell->ch != MCVIEW_VTERM_WIDE_TAIL)
            {
                const mcview_cell_attr_t *a = &cell->attr;
                const mcview_vterm_cell_t *next =
                    mcview_terminal_buffer_get (buf, canvas_row, col + 1);
                const gboolean wide = g_unichar_iswide (cell->ch);
                const gboolean whole =
                    wide && col + 1 < cols && next != NULL && next->ch == MCVIEW_VTERM_WIDE_TAIL;
                mcview_ansi_state_t tmp;

                mcview_ansi_state_init (&tmp);
                tmp.fg = a->fg;
                tmp.bg = a->bg;
                tmp.bold = a->bold;
                tmp.dim = a->dim;
                tmp.italic = a->italic;
                tmp.underline = a->underline;
                tmp.blink = a->blink;
                tmp.reverse = a->reverse;
                tty_setcolor (mcview_ansi_color_of (&tmp, colors));
                if (a->conceal)
                {
                    tty_print_char (' ');
                    if (whole)
                        tty_print_char (' ');
                }
                else
                    // Half a wide character is not drawn.
                    tty_print_anychar (wide && !whole ? ' ' : cell->ch);
                // The right half is drawn with it; a cell printed there would erase it.
                if (whole)
                    col++;
            }
            else
            {
                tty_setcolor (colors->normal);
                tty_print_char (' ');
            }
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_preload_canvas_colors (const mcview_canvas_colors_t *colors)
{
#ifdef HAVE_SLANG
    /* S-Lang clears and redraws the whole screen at the next refresh after any new color pair.
       Before the first draw that costs nothing; later it blinks the screen. */
    mcview_ansi_state_t ansi;
    int fg, bg, bold;

    mcview_ansi_state_init (&ansi);

    for (bold = 0; bold <= 1; bold++)
        for (fg = MCVIEW_ANSI_COLOR_DEFAULT; fg < 16; fg++)
            for (bg = MCVIEW_ANSI_COLOR_DEFAULT; bg < 16; bg++)
            {
                ansi.fg = fg;
                ansi.bg = bg;
                ansi.bold = bold != 0;
                (void) mcview_ansi_color_of (&ansi, colors);
            }
#else
    (void) colors;
#endif
}

/* --------------------------------------------------------------------------------------------- */
