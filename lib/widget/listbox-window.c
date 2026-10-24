/*
   Widget based utility functions.

   Copyright (C) 1994-2025
   Free Software Foundation, Inc.

   Authors:
   Miguel de Icaza, 1994, 1995, 1996
   Radek Doulik, 1994, 1995
   Jakub Jelinek, 1995
   Andrej Borsenkow, 1995
   Andrew Borodin <aborodin@vmail.ru>, 2009, 2010, 2013

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

/** \file listbox-window.c
 *  \brief Source: Listbox widget, a listbox within dialog window
 */

#include <config.h>

#include <stdlib.h>

#include "lib/global.h"
#include "lib/tty/tty.h"  // COLS
#include "lib/skin.h"
#include "lib/strutil.h"  // str_term_width1()
#include "lib/widget.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

/* The screen has changed its size: the list takes as much of what it asked for as fits, and
   scrolls the rest */
static cb_ret_t
listbox_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    const int space = 4;
    const Listbox *listbox = (const Listbox *) DIALOG (w)->data.p;

    if (msg == MSG_RESIZE && listbox != NULL)
    {
        WRect r = w->rect;

        r.lines = MIN (listbox->lines, LINES - 6) + space;
        r.cols = MIN (listbox->cols, COLS - 6) + space;
        r.y = MAX (0, MIN (r.y, LINES - r.lines));
        r.x = MAX (0, MIN (r.x, COLS - r.cols));
        return dlg_default_callback (w, NULL, MSG_RESIZE, 0, &r);
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

Listbox *
listbox_window_centered_new (int center_y, int center_x, int lines, int cols, const char *title,
                             const char *help)
{
    const int space = 4;

    int xpos = 0, ypos = 0;
    Listbox *listbox;
    widget_pos_flags_t pos_flags = WPOS_TRYUP;

    int want_lines, want_cols;

    if (title != NULL)
    {
        int len;

        len = str_term_width1 (title) + 4;
        cols = MAX (cols, len);
    }

    // Adjust sizes
    want_lines = lines;
    want_cols = cols;
    lines = MIN (lines, LINES - 6);
    cols = MIN (cols, COLS - 6);

    // adjust position
    if ((center_y < 0) || (center_x < 0))
        pos_flags |= WPOS_CENTER;
    else
    {
        // Actually, this this is not used in MC.

        ypos = center_y;
        xpos = center_x;

        ypos -= lines / 2;
        xpos -= cols / 2;

        if (ypos + lines >= LINES)
            ypos = LINES - lines - space;
        if (ypos < 0)
            ypos = 0;

        if (xpos + cols >= COLS)
            xpos = COLS - cols - space;
        if (xpos < 0)
            xpos = 0;
    }

    listbox = g_new (Listbox, 1);

    listbox->lines = want_lines;
    listbox->cols = want_cols;
    listbox->dlg = dlg_create (TRUE, ypos, xpos, lines + space, cols + space, pos_flags, FALSE,
                               listbox_colors, listbox_window_callback, NULL, help, title);
    listbox->dlg->data.p = listbox;

    listbox->list = listbox_new (2, 2, lines, cols, FALSE, NULL);
    // it goes with the frame when the screen changes its size
    group_add_widget_autopos (GROUP (listbox->dlg), listbox->list, WPOS_KEEP_ALL, NULL);

    return listbox;
}

/* --------------------------------------------------------------------------------------------- */

Listbox *
listbox_window_new (int lines, int cols, const char *title, const char *help)
{
    return listbox_window_centered_new (-1, -1, lines, cols, title, help);
}

/* --------------------------------------------------------------------------------------------- */

/** Returns the number of the item selected */
int
listbox_run (Listbox *l)
{
    int val = -1;

    if (dlg_run (l->dlg) != B_CANCEL)
        val = l->list->current;
    widget_destroy (WIDGET (l->dlg));
    g_free (l);
    return val;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * A variant of listbox_run() which is more convenient to use when we
 * need to select arbitrary 'data'.
 *
 * @param select  the item to select initially, by its 'data'. Optional.
 * @return        the 'data' of the item selected, or NULL if none selected.
 */
void *
listbox_run_with_data (Listbox *l, const void *select)
{
    void *val = NULL;

    if (select != NULL)
        listbox_set_current (l->list, listbox_search_data (l->list, select));

    if (dlg_run (l->dlg) != B_CANCEL)
    {
        WLEntry *e;

        e = listbox_get_nth_entry (l->list, l->list->current);
        if (e != NULL)
        {
            /* The assert guards against returning a soon-to-be deallocated
             * pointer (as in listbox_add_item(..., TRUE)). */
            g_assert (!e->free_data);
            val = e->data;
        }
    }

    widget_destroy (WIDGET (l->dlg));
    g_free (l);
    return val;
}

/* --------------------------------------------------------------------------------------------- */
