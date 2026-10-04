/*
   The docks of the editor screen.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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
 *  \brief Source: the docks of the editor screen
 *
 *  The windows of the plugins stand in docks: the column at the right, one window above the
 *  other, all the height of the screen; and the row at the bottom, under the windows of the
 *  files, where the windows are tabs and one of them is seen.  The windows of the files that were
 *  fullscreen fill the rest, and are fullscreen again when the docks are empty.
 */

#include <config.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/strutil.h"
#include "lib/widget.h"

#include "editdock.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define DOCK_RIGHT_MIN  16
#define DOCK_BOTTOM_MIN 4
// the room the windows of the files keep at least
#define DOCK_CENTER_COLS  20
#define DOCK_CENTER_LINES 6

/*** file scope type declarations ****************************************************************/

/* a window of the column at the right, and its share of the height */
typedef struct
{
    WEditWindow *win;
    int share;
} dock_entry_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GArray *right = NULL;      // dock_entry_t, from the top
static GPtrArray *bottom = NULL;  // WEditWindow, the tabs in their order
static WEditWindow *tab = NULL;   // the one of them seen
// the sizes of the docks, in percent of the screen; set: not taken from the window that comes
static int right_pct = 30;
static int bottom_pct = 25;
static gboolean right_set = FALSE;
static gboolean bottom_set = FALSE;
static gboolean arranging = FALSE;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
dock_right_index (const WEditWindow *win)
{
    guint i;

    for (i = 0; right != NULL && i < right->len; i++)
        if (g_array_index (right, dock_entry_t, i).win == win)
            return (int) i;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
dock_bottom_index (const WEditWindow *win)
{
    guint i;

    for (i = 0; bottom != NULL && i < bottom->len; i++)
        if (g_ptr_array_index (bottom, i) == win)
            return (int) i;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static void
dock_place (WEditWindow *win, WRect *r)
{
    Widget *w = WIDGET (win);

    win->fullscreen = 0;
    win->dock_fill = 0;
    w->pos_flags = WPOS_KEEP_DEFAULT;
    if (!rects_are_equal (&w->rect, r))
        widget_set_size_rect (w, r);
}

/* --------------------------------------------------------------------------------------------- */

/* The title of a tab, a copy */
static char *
dock_tab_title (const WEditWindow *win)
{
    return win->klass->get_title != NULL ? win->klass->get_title (win) : g_strdup ("?");
}

/* --------------------------------------------------------------------------------------------- */

static WDialog *
dock_dialog (const WEditWindow *win)
{
    const WGroup *g = CONST_WIDGET (win)->owner;

    return g != NULL ? DIALOG (g) : NULL;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
edit_dock_add (WEditWindow *win, edit_dock_side_t side, int size)
{
    WDialog *h = dock_dialog (win);
    WRect a;

    if (h == NULL || side == EDIT_DOCK_NONE)
        return;
    (void) edit_dock_remove (win);
    edit_window_area (h, &a);

    if (side == EDIT_DOCK_RIGHT)
    {
        dock_entry_t e = { win, 100 };

        if (right == NULL)
            right = g_array_new (FALSE, FALSE, sizeof (dock_entry_t));
        if (right->len == 0 && !right_set && size > 0 && a.cols > 0)
            right_pct = CLAMP (size * 100 / a.cols, 10, 70);
        // under the lowest one, with more of the height than it: 60 to 40
        if (right->len != 0)
            e.share = MAX (1, g_array_index (right, dock_entry_t, right->len - 1).share * 3 / 2);
        g_array_append_val (right, e);
    }
    else
    {
        if (bottom == NULL)
            bottom = g_ptr_array_new ();
        if (bottom->len == 0 && !bottom_set && size > 0 && a.lines > 0)
            bottom_pct = CLAMP (size * 100 / a.lines, 10, 70);
        g_ptr_array_add (bottom, win);
        tab = win;
    }
    edit_dock_arrange (h);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_dock_remove (WEditWindow *win)
{
    WDialog *h = dock_dialog (win);
    int i;

    i = dock_right_index (win);
    if (i >= 0)
        g_array_remove_index (right, (guint) i);
    else
    {
        i = dock_bottom_index (win);
        if (i < 0)
            return FALSE;
        g_ptr_array_remove_index (bottom, (guint) i);
        if (tab == win)
            tab = bottom->len != 0 ? g_ptr_array_index (bottom, MIN ((guint) i, bottom->len - 1))
                                   : NULL;
    }
    if (h != NULL)
        edit_dock_arrange (h);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

edit_dock_side_t
edit_dock_side (const WEditWindow *win)
{
    if (dock_right_index (win) >= 0)
        return EDIT_DOCK_RIGHT;
    if (dock_bottom_index (win) >= 0)
        return EDIT_DOCK_BOTTOM;
    return EDIT_DOCK_NONE;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_arrange (WDialog *h)
{
    WRect a, r, c;
    int cols = 0, lines = 0;
    guint i;
    GList *l;

    if (h == NULL || arranging)
        return;
    arranging = TRUE;
    edit_window_area (h, &a);

    if (right != NULL && right->len != 0)
        cols = CLAMP (a.cols * right_pct / 100, DOCK_RIGHT_MIN,
                      MAX (DOCK_RIGHT_MIN, a.cols - DOCK_CENTER_COLS));
    if (bottom != NULL && bottom->len != 0)
        lines = CLAMP (a.lines * bottom_pct / 100, DOCK_BOTTOM_MIN,
                       MAX (DOCK_BOTTOM_MIN, a.lines - DOCK_CENTER_LINES));

    // the column at the right: the height by the shares
    if (cols != 0)
    {
        int total = 0, y = a.y;

        for (i = 0; i < right->len; i++)
            total += g_array_index (right, dock_entry_t, i).share;
        for (i = 0; i < right->len; i++)
        {
            WEditWindow *win = g_array_index (right, dock_entry_t, i).win;
            const int left = a.y + a.lines - y;

            r.x = a.x + a.cols - cols;
            r.cols = cols;
            r.y = y;
            r.lines = i + 1 == right->len
                ? left
                : MAX (win->klass->min_lines,
                       a.lines * g_array_index (right, dock_entry_t, i).share / MAX (total, 1));
            r.lines = MAX (1, MIN (r.lines, left - (int) (right->len - i - 1)));
            dock_place (win, &r);
            if (!widget_get_state (WIDGET (win), WST_VISIBLE))
                widget_show (WIDGET (win));
            y += r.lines;
        }
    }

    // the row at the bottom: one tab seen
    if (lines != 0)
    {
        r.x = a.x;
        r.cols = a.cols - cols;
        r.y = a.y + a.lines - lines;
        r.lines = lines;
        if (tab == NULL || dock_bottom_index (tab) < 0)
            tab = g_ptr_array_index (bottom, bottom->len - 1);
        for (i = 0; i < bottom->len; i++)
        {
            WEditWindow *win = g_ptr_array_index (bottom, i);

            dock_place (win, &r);
            if (win == tab)
                widget_show (WIDGET (win));
            else if (widget_get_state (WIDGET (win), WST_VISIBLE))
                widget_hide (WIDGET (win));
        }
    }

    // the windows of the files that were fullscreen: the rest of the screen
    c = a;
    c.cols -= cols;
    c.lines -= lines;
    for (l = GROUP (h)->widgets; l != NULL; l = g_list_next (l))
    {
        Widget *wl = WIDGET (l->data);
        WEditWindow *ow;

        if (!edit_window_is_window (wl) || edit_dock_side (EDIT_WINDOW (wl)) != EDIT_DOCK_NONE)
            continue;
        ow = EDIT_WINDOW (wl);
        if (cols == 0 && lines == 0)
        {
            if (ow->dock_fill != 0)
            {
                ow->dock_fill = 0;
                ow->fullscreen = 1;
                wl->pos_flags = WPOS_KEEP_ALL;
                widget_set_size_rect (wl, &a);
            }
        }
        else if (ow->fullscreen != 0 || ow->dock_fill != 0)
        {
            ow->fullscreen = 0;
            ow->dock_fill = 1;
            wl->pos_flags = WPOS_KEEP_DEFAULT;
            if (!rects_are_equal (&wl->rect, &c))
                widget_set_size_rect (wl, &c);
        }
    }

    arranging = FALSE;
    widget_draw (WIDGET (h));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_tab_select (WEditWindow *win)
{
    WDialog *h = dock_dialog (win);

    if (h == NULL || dock_bottom_index (win) < 0)
        return;
    tab = win;
    edit_dock_arrange (h);
    widget_select (WIDGET (win));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_tab_step (WDialog *h, int step)
{
    int i;

    (void) h;
    if (bottom == NULL || bottom->len == 0)
        return;
    i = tab != NULL ? dock_bottom_index (tab) : 0;
    i = (i + step + (int) bottom->len) % (int) bottom->len;
    edit_dock_tab_select (g_ptr_array_index (bottom, i));
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_dock_tab_click (WEditWindow *win, int x)
{
    int at = 1;
    guint i;

    if (dock_bottom_index (win) < 0 || bottom->len < 2)
        return FALSE;
    for (i = 0; i < bottom->len; i++)
    {
        WEditWindow *t = g_ptr_array_index (bottom, i);
        char *title = dock_tab_title (t);
        const int width = str_term_width1 (title) + 3;

        g_free (title);
        if (x >= at && x < at + width)
        {
            if (t != win)
                edit_dock_tab_select (t);
            return TRUE;
        }
        at += width;
    }
    // the rest of the title of a tab is no handle to move it by
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_draw_tabs (const WEditWindow *win, int color)
{
    const Widget *w = CONST_WIDGET (win);
    const gboolean focused = widget_get_state (w, WST_FOCUSED);
    // the icons of the window keep the end of the title
    const int end = w->rect.cols - 9;
    int at = 1;
    guint i;

    if (dock_bottom_index (win) < 0 || bottom->len < 2 || end <= at)
        return;
    tty_setcolor (color);
    tty_draw_hline (w->rect.y, w->rect.x + 1,
                    mc_tty_frm[focused ? MC_TTY_FRM_DHORIZ : MC_TTY_FRM_HORIZ], end - 1);
    for (i = 0; i < bottom->len && at < end; i++)
    {
        const WEditWindow *t = g_ptr_array_index (bottom, i);
        char *title = dock_tab_title (t);
        char *label = g_strdup_printf (t == win ? "[%s]" : " %s ", title);

        widget_gotoyx (w, 0, at);
        tty_print_string (str_trunc (label, end - at));
        at += str_term_width1 (title) + 3;
        g_free (label);
        g_free (title);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_dragged (WEditWindow *win, gboolean moved)
{
    WDialog *h = dock_dialog (win);
    const Widget *w = CONST_WIDGET (win);
    WRect a;
    int i;

    if (h == NULL)
        return;
    if (moved)
    {
        // a window moved out of its dock stands by itself where it was put
        (void) edit_dock_remove (win);
        return;
    }
    edit_window_area (h, &a);
    i = dock_right_index (win);
    if (i >= 0)
    {
        guint k;

        right_pct = CLAMP (w->rect.cols * 100 / MAX (a.cols, 1), 10, 70);
        right_set = TRUE;
        // the heights as they are now are the shares
        for (k = 0; k < right->len; k++)
        {
            dock_entry_t *e = &g_array_index (right, dock_entry_t, k);

            e->share = MAX (1, CONST_WIDGET (e->win)->rect.lines);
        }
    }
    else if (dock_bottom_index (win) >= 0)
    {
        bottom_pct = CLAMP (w->rect.lines * 100 / MAX (a.lines, 1), 10, 70);
        bottom_set = TRUE;
    }
    edit_dock_arrange (h);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
edit_dock_windows (edit_dock_side_t side)
{
    GPtrArray *list = g_ptr_array_new ();
    guint i;

    if (side == EDIT_DOCK_RIGHT)
        for (i = 0; right != NULL && i < right->len; i++)
            g_ptr_array_add (list, g_array_index (right, dock_entry_t, i).win);
    else if (side == EDIT_DOCK_BOTTOM)
        for (i = 0; bottom != NULL && i < bottom->len; i++)
            g_ptr_array_add (list, g_ptr_array_index (bottom, i));
    return list;
}

/* --------------------------------------------------------------------------------------------- */

WEditWindow *
edit_dock_tab (void)
{
    return tab;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_get_sizes (int *right_size, int *bottom_size)
{
    *right_size = right_pct;
    *bottom_size = bottom_pct;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_set_sizes (int right_size, int bottom_size)
{
    if (right_size > 0)
    {
        right_pct = CLAMP (right_size, 10, 70);
        right_set = TRUE;
    }
    if (bottom_size > 0)
    {
        bottom_pct = CLAMP (bottom_size, 10, 70);
        bottom_set = TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

int
edit_dock_share (const WEditWindow *win)
{
    const int i = dock_right_index (win);

    return i >= 0 ? g_array_index (right, dock_entry_t, i).share : 0;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_set_share (WEditWindow *win, int share)
{
    const int i = dock_right_index (win);

    if (i >= 0 && share > 0)
        g_array_index (right, dock_entry_t, i).share = share;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_dock_order (edit_dock_side_t side, const GPtrArray *order)
{
    guint i, to = 0;

    for (i = 0; order != NULL && i < order->len; i++)
    {
        WEditWindow *win = g_ptr_array_index (order, i);
        int at;

        if (side == EDIT_DOCK_RIGHT && (at = dock_right_index (win)) >= 0)
        {
            const dock_entry_t e = g_array_index (right, dock_entry_t, at);

            g_array_remove_index (right, (guint) at);
            g_array_insert_val (right, to++, e);
        }
        else if (side == EDIT_DOCK_BOTTOM && (at = dock_bottom_index (win)) >= 0)
        {
            g_ptr_array_remove_index (bottom, (guint) at);
            g_ptr_array_insert (bottom, (gint) to++, win);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */
