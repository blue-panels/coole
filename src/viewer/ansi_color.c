/*
   The colors of ANSI text, in the colors of the skin.

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

/** \file
 *  \brief Source: the colors of ANSI text, in the colors of the skin
 *
 *  In mc this is a part of the viewer (ascii.c); coole has it in the program, for the terminal
 *  and for the windows that show text with ANSI colors.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/tty/tty.h"
#include "lib/tty/color.h"
#include "lib/tty/color-internal.h"  // tty_color_get_name_by_index()

#include "ansi_color.h"

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
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/**
 * Convert ANSI parser color state to a tty color pair index.
 *
 * Maps the fg/bg/bold/underline from the ANSI parser into MC's color system.
 * When all attributes are default, returns the normal color of @colors to avoid
 * unnecessary color pair allocation.
 */
int
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
