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
#include "lib/tty/color-internal.h"  // tty_color_get_name_by_index()

#include "vterm.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
mcview_ansi_use_256 (void)
{
    static int use_256 = -1;

    if (use_256 < 0)
        use_256 = tty_use_256colors (NULL) ? 1 : 0;

    return use_256 != 0;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Convert ANSI parser color state to a tty color pair index.
 *
 * Maps the fg/bg/bold/underline from the ANSI parser into MC's color system.
 * When all attributes are default, returns the normal color of @colors to avoid
 * unnecessary color pair allocation.
 */
static int
mcview_ansi_color_of (const mcview_ansi_state_t *ansi, const mcview_canvas_colors_t *colors)
{
    tty_color_pair_t color;
    tty_color_pair_t *skin;
    char kname[BUF_TINY];
    char fg_buf[16], bg_buf[16], attr_buf[64];
    const char *fg_name;
    const char *bg_name;
    gboolean has_attrs;
    const gboolean underline = ansi->underline || ansi->link;
    const int fg = ansi->dim ? mcview_ansi_dim_color (ansi->fg, mcview_ansi_use_256 ()) : ansi->fg;

    has_attrs = ansi->bold || ansi->italic || underline || ansi->blink || ansi->reverse;

    // all defaults -> use the skin's normal color
    if (fg == MCVIEW_ANSI_COLOR_DEFAULT && ansi->bg == MCVIEW_ANSI_COLOR_DEFAULT && !has_attrs)
        return colors->normal;

    /* bold-only and underline-only map to the colors the skin has for them, and
       are built below by a skin that has none. */
    if (fg == MCVIEW_ANSI_COLOR_DEFAULT && ansi->bg == MCVIEW_ANSI_COLOR_DEFAULT && !ansi->italic
        && !ansi->blink && !ansi->reverse)
    {
        if (ansi->bold && underline && colors->bold_underline >= 0)
            return colors->bold_underline;
        if (ansi->bold && !underline && colors->bold >= 0)
            return colors->bold;
        if (underline && !ansi->bold && colors->underline >= 0)
            return colors->underline;
    }

    /* Retrieve the skin colors of the section so that ANSI-colored text
       inherits its fg/bg rather than the terminal's "default" colors. */
    g_snprintf (kname, sizeof (kname), "%s._default_", colors->section);
    skin = (tty_color_pair_t *) g_hash_table_lookup (mc_skin__default.colors, kname);
    if (skin == NULL)
        skin = (tty_color_pair_t *) g_hash_table_lookup (mc_skin__default.colors, "core._default_");

    // build fg color name
    if (fg != MCVIEW_ANSI_COLOR_DEFAULT)
    {
        fg_name = tty_color_get_name_by_index (fg);
        g_strlcpy (fg_buf, fg_name, sizeof (fg_buf));
        color.fg = fg_buf;
    }
    else
        color.fg = (skin != NULL) ? skin->fg : NULL;

    // build bg color name
    if (ansi->bg != MCVIEW_ANSI_COLOR_DEFAULT)
    {
        bg_name = tty_color_get_name_by_index (ansi->bg);
        g_strlcpy (bg_buf, bg_name, sizeof (bg_buf));
        color.bg = bg_buf;
    }
    else
        color.bg = (skin != NULL) ? skin->bg : NULL;

    // build attributes string dynamically
    if (has_attrs)
    {
        attr_buf[0] = '\0';
        if (ansi->bold)
            g_strlcat (attr_buf, "bold+", sizeof (attr_buf));
        if (ansi->italic)
            g_strlcat (attr_buf, "italic+", sizeof (attr_buf));
        if (underline)
            g_strlcat (attr_buf, "underline+", sizeof (attr_buf));
        if (ansi->blink)
            g_strlcat (attr_buf, "blink+", sizeof (attr_buf));
        if (ansi->reverse)
            g_strlcat (attr_buf, "reverse+", sizeof (attr_buf));
        // remove trailing '+'
        attr_buf[strlen (attr_buf) - 1] = '\0';
        color.attrs = attr_buf;
    }
    else
        color.attrs = NULL;

    color.pair_index = 0;

    return tty_try_alloc_color_pair (&color, TRUE);
}

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
