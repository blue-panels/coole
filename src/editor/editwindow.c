/*
   A window of the editor screen.

   Copyright (C) 1996-2026
   Free Software Foundation, Inc.

   Written by:
   Paul Sheer, 1996, 1997
   Andrew Borodin <aborodin@vmail.ru> 2012-2024
   Ilia Maslakov <il.smind@gmail.com> 2010-2012, 2026

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
 *  \brief Source: a window of the editor screen
 */

#include <config.h>

#include "lib/global.h"

#include "lib/tty/tty.h"    // LINES, COLS
#include "lib/tty/color.h"  // tty_setcolor()
#include "lib/skin.h"
#include "lib/strutil.h"  // str_term_trim()
#include "lib/widget.h"
#include "lib/keybind.h"  // keybind_lookup_keymap_command()

#include "src/keymap.h"  // editor_map

#include "edit.h"       // MCEDIT_HELP_FILE
#include "edit-impl.h"  // edit_widget_is_editor()
#include "editwindow.h"
#include "editdock.h"

/*** global variables ****************************************************************************/

char *edit_window_state_char = NULL;
char *edit_window_close_char = NULL;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

static int edit_window_room_edge (const WRect *r, edit_window_room_side_t side);

static void edit_window_place_bars (WEditWindow *win);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
edit_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WEditWindow *win = EDIT_WINDOW (w);
    cb_ret_t ret;

    // a scrollbar has moved: the class moves the view
    if (msg == MSG_NOTIFY && sender != NULL
        && (sender == WIDGET (win->vbar) || sender == WIDGET (win->hbar)))
    {
        if (win->klass->scrolled != NULL)
            win->klass->scrolled (win, sender == WIDGET (win->vbar),
                                  scrollbar_get_pos (SCROLLBAR (sender)));
        return MSG_HANDLED;
    }

    /* A window that is no file, being moved or resized, takes the keys of the editor for that: the
       class would take them for its own, the terminal giving them to the shell.  A file window
       has them in its own keymap */
    if (msg == MSG_KEY && win->drag_state != EDIT_WINDOW_DRAG_NONE && !edit_widget_is_editor (w))
    {
        const long command = keybind_lookup_keymap_command (editor_map, parm);

        if (command == CK_Quit || command == CK_Cancel)
            edit_window_restore_size (win);
        else
            (void) edit_window_handle_move_resize (win, command);
        return MSG_HANDLED;
    }

    // the same for the commands of the button bar: they would do what the class does with them
    if (msg == MSG_ACTION && win->drag_state != EDIT_WINDOW_DRAG_NONE && !edit_widget_is_editor (w))
    {
        if (parm == CK_Quit || parm == CK_Cancel)
            edit_window_restore_size (win);
        return MSG_HANDLED;
    }

    ret = win->klass->callback (w, sender, msg, parm, data);

    // the room made for it is forgotten with it
    if (msg == MSG_DESTROY && win->rooms != NULL)
        g_clear_pointer (&win->rooms, g_array_unref);

    // the frame has moved: its scrollbars with it
    if (msg == MSG_RESIZE)
        edit_window_place_bars (win);

    /* The button bar is the one of the window with the focus: its class has put its labels on
       the bar, and the bar shows them at once, whatever gave the window the focus. */
    if (msg == MSG_FOCUS && w->owner != NULL)
    {
        WButtonBar *bb = buttonbar_find (DIALOG (w->owner));

        if (bb != NULL)
            widget_draw (WIDGET (bb));
    }

    return ret;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Put the scrollbars on the frame: the vertical one down the right side between the corners, the
 * horizontal one along the bottom from the column of the class to before the corner that resizes
 * the window.  A fullscreen window has no frame, and hides them.
 */

static void
edit_window_place_bars (WEditWindow *win)
{
    const WRect *r = &CONST_WIDGET (win)->rect;
    const gboolean shown = (win->fullscreen == 0);

    if (win->vbar != NULL)
    {
        WRect br = { r->y + 1, r->x + r->cols - 1, MAX (1, r->lines - 2), 1 };

        widget_set_size_rect (WIDGET (win->vbar), &br);
        widget_set_visibility (WIDGET (win->vbar), shown);
    }
    if (win->hbar != NULL)
    {
        const int x = win->klass->hbar_x;
        WRect br = { r->y + r->lines - 1, r->x + x, 1, MAX (1, r->cols - 2 - x) };

        widget_set_size_rect (WIDGET (win->hbar), &br);
        widget_set_visibility (WIDGET (win->hbar), shown && r->cols - 2 - x >= 4);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Mouse events: the scrollbars first, then the frame and the class, then the other widgets of the
 * window.  A scrollbar that holds the mouse, its thumb dragged, takes the events wherever they are.
 */

static int
edit_window_mouse_handler (Widget *w, Gpm_Event *event)
{
    WEditWindow *win = EDIT_WINDOW (w);
    Widget *bars[] = { WIDGET (win->vbar), WIDGET (win->hbar) };
    size_t i;
    int mou;

    for (i = 0; i < G_N_ELEMENTS (bars); i++)
        if (bars[i] != NULL && widget_get_state (bars[i], WST_VISIBLE)
            && (bars[i]->mouse.capture || mouse_global_in_widget (event, bars[i])))
        {
            mou = mouse_handle_event (bars[i], event);
            if (mou != MOU_UNHANDLED)
                return mou;
        }

    mou = mouse_handle_event (w, event);
    if (mou != MOU_UNHANDLED)
        return mou;

    return group_handle_mouse_event (w, event);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw the window and the button bar it may overlap.
 */

static void
edit_window_redraw (WEditWindow *win)
{
    Widget *w = WIDGET (win);

    widget_draw (w);
    widget_draw (WIDGET (buttonbar_find (DIALOG (w->owner))));
}

/* --------------------------------------------------------------------------------------------- */
/*
 * Sticky windows.  With edit_options.sticky_windows on, windows whose frames stand next to each
 * other, the left column of one right after the right column of the other (or the top row right
 * after the bottom row), resize together: the edge of one moves the edge of its neighbors, and
 * nothing comes apart.  Nothing is kept of it: the neighbors are found from where the windows are.
 *
 * An edge is the column (or the row) of the frames of the windows before it, with those windows
 * and the ones right after it, in one run along it: a window after it shares part of a side with a
 * window before it, and a window on either side that touches one on the same side, one above the
 * other, carries the run on.  A window that only meets another at a corner is no part of it.  An
 * edge with no window after it is the window's own.
 */

typedef struct
{
    gboolean vertical;  // a column between windows side by side, else a row between ones stacked
    int at;             // the last column (row) of the frames of the windows before it
    GPtrArray *before;  // WEditWindow: the ones that grow when it moves forward
    GPtrArray *after;   // WEditWindow: the ones that shrink and move
} sticky_edge_t;

/* --------------------------------------------------------------------------------------------- */

static gboolean
sticky_window (const Widget *w)
{
    return edit_window_is_window (w) && widget_get_state (w, WST_VISIBLE)
        && CONST_EDIT_WINDOW (w)->fullscreen == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The span of a window along an edge: its rows for a column, its columns for a row */
static void
sticky_span (const Widget *w, gboolean vertical, int *from, int *to)
{
    *from = vertical ? w->rect.y : w->rect.x;
    *to = *from + (vertical ? w->rect.lines : w->rect.cols) - 1;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether o shares part of its span with one of the windows of list, or, with touch, touches one */
static gboolean
sticky_meets (const GPtrArray *list, const Widget *o, gboolean vertical, gboolean touch)
{
    const int slack = touch ? 1 : 0;
    int from, to;
    guint i;

    sticky_span (o, vertical, &from, &to);
    for (i = 0; i < list->len; i++)
    {
        int lfrom, lto;

        sticky_span (CONST_WIDGET (g_ptr_array_index (list, i)), vertical, &lfrom, &lto);
        if (from <= lto + slack && to >= lfrom - slack)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The edge after the right side (vertical) or the bottom (horizontal) of the frame of win */
static void
sticky_edge_find (WEditWindow *win, gboolean vertical, sticky_edge_t *e)
{
    const Widget *w = CONST_WIDGET (win);
    gboolean grown;

    e->vertical = vertical;
    e->at = vertical ? w->rect.x + w->rect.cols - 1 : w->rect.y + w->rect.lines - 1;
    e->before = g_ptr_array_new ();
    e->after = g_ptr_array_new ();
    g_ptr_array_add (e->before, win);

    // the run along the edge grows by every window that comes into it, till none does
    do
    {
        const GList *l;

        grown = FALSE;
        for (l = w->owner->widgets; l != NULL; l = g_list_next (l))
        {
            Widget *o = WIDGET (l->data);
            WEditWindow *ow = EDIT_WINDOW (o);
            int first, last;

            if (!sticky_window (o) || g_ptr_array_find (e->before, ow, NULL)
                || g_ptr_array_find (e->after, ow, NULL))
                continue;

            first = vertical ? o->rect.x : o->rect.y;
            last = first + (vertical ? o->rect.cols : o->rect.lines) - 1;

            if (last == e->at
                && (sticky_meets (e->after, o, vertical, FALSE)
                    || sticky_meets (e->before, o, vertical, TRUE)))
                g_ptr_array_add (e->before, ow);
            else if (first == e->at + 1
                     && (sticky_meets (e->before, o, vertical, FALSE)
                         || sticky_meets (e->after, o, vertical, TRUE)))
                g_ptr_array_add (e->after, ow);
            else
                continue;

            grown = TRUE;
        }
    }
    while (grown);

    // no neighbor after it: an edge of the window alone, the windows in line with it stay
    if (e->after->len == 0)
        g_ptr_array_set_size (e->before, 1);
}

/* --------------------------------------------------------------------------------------------- */

static void
sticky_edge_free (sticky_edge_t *e)
{
    g_ptr_array_free (e->before, TRUE);
    g_ptr_array_free (e->after, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* How far the edge can move of d: no window smaller than its smallest size, no window grown past
   the area a or over a window that is no part of the edge */
static int
sticky_edge_clamp (const sticky_edge_t *e, const WRect *a, int d)
{
    const WGroup *g = CONST_WIDGET (g_ptr_array_index (e->before, 0))->owner;
    const int end = e->vertical ? a->x + a->cols - 1 : a->y + a->lines - 1;
    const GList *l;
    guint i;

    if (d > 0)
        d = MIN (d, MAX (0, end - e->at));

    for (i = 0; i < e->before->len; i++)
    {
        const WEditWindow *bw = EDIT_WINDOW (g_ptr_array_index (e->before, i));
        const WRect *r = &CONST_WIDGET (bw)->rect;

        d = MAX (d,
                 (e->vertical ? bw->klass->min_cols - r->cols : bw->klass->min_lines - r->lines));
    }
    for (i = 0; i < e->after->len; i++)
    {
        const WEditWindow *aw = EDIT_WINDOW (g_ptr_array_index (e->after, i));
        const WRect *r = &CONST_WIDGET (aw)->rect;

        d = MIN (d,
                 (e->vertical ? r->cols - aw->klass->min_cols : r->lines - aw->klass->min_lines));
    }

    // the windows in the way: the ones before stop short of them, and so do the ones after
    for (l = g->widgets; l != NULL; l = g_list_next (l))
    {
        const Widget *o = CONST_WIDGET (l->data);
        int first, last;

        if (!sticky_window (o) || g_ptr_array_find (e->before, o, NULL)
            || g_ptr_array_find (e->after, o, NULL))
            continue;
        first = e->vertical ? o->rect.x : o->rect.y;
        last = first + (e->vertical ? o->rect.cols : o->rect.lines) - 1;

        if (d > 0 && first > e->at && sticky_meets (e->before, o, e->vertical, FALSE))
            d = MIN (d, first - e->at - 1);
        else if (d < 0 && last <= e->at && sticky_meets (e->after, o, e->vertical, FALSE))
            d = MAX (d, last - e->at);
    }

    return d;
}

/* --------------------------------------------------------------------------------------------- */

static void
sticky_edge_move (const sticky_edge_t *e, int d)
{
    const gboolean shared = (e->after->len != 0);
    guint i;

    for (i = 0; i < e->before->len; i++)
    {
        WEditWindow *bw = EDIT_WINDOW (g_ptr_array_index (e->before, i));
        WRect r = WIDGET (bw)->rect;

        if (e->vertical)
        {
            r.cols += d;
            bw->moved_vedge = bw->moved_vedge || shared;
        }
        else
        {
            r.lines += d;
            bw->moved_hedge = bw->moved_hedge || shared;
        }
        widget_set_size_rect (WIDGET (bw), &r);
    }
    for (i = 0; i < e->after->len; i++)
    {
        Widget *aw = WIDGET (g_ptr_array_index (e->after, i));
        WRect r = aw->rect;

        if (e->vertical)
        {
            r.x += d;
            r.cols -= d;
        }
        else
        {
            r.y += d;
            r.lines -= d;
        }
        widget_set_size_rect (aw, &r);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Mark the windows of the edges as dragged along with win, and no other window of the screen */
static void
sticky_mark (WEditWindow *win, const sticky_edge_t *edges, int n)
{
    GList *l;
    int k;

    for (l = WIDGET (win)->owner->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (WIDGET (l->data)))
            EDIT_WINDOW (l->data)->dragged_along = 0;

    for (k = 0; k < n; k++)
    {
        guint i;

        if (edges[k].after->len == 0)
            continue;
        for (i = 0; i < edges[k].before->len; i++)
            EDIT_WINDOW (g_ptr_array_index (edges[k].before, i))->dragged_along = 1;
        for (i = 0; i < edges[k].after->len; i++)
            EDIT_WINDOW (g_ptr_array_index (edges[k].after, i))->dragged_along = 1;
    }
    win->dragged_along = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Resize win by dx columns and dy rows at its bottom right corner, its sticky neighbors along: an
   edge of it with windows after it moves with them, and a side that was on the edge of the area
   when the resize began stays there */
static void
sticky_resize (WEditWindow *win, int dx, int dy)
{
    Widget *w = WIDGET (win);
    sticky_edge_t edges[2];
    WRect a;
    int k;

    edit_window_area (DIALOG (w->owner), &a);
    sticky_edge_find (win, TRUE, &edges[0]);
    sticky_edge_find (win, FALSE, &edges[1]);

    for (k = 0; k < 2; k++)
    {
        const sticky_edge_t *e = &edges[k];
        int d = e->vertical ? dx : dy;

        if (d == 0 || (e->vertical ? win->pin_right : win->pin_bottom))
            continue;

        d = sticky_edge_clamp (e, &a, d);
        if (d != 0)
        {
            sticky_edge_move (e, d);
            // an edge of its own is the user's resize of it
            if (e->after->len == 0 && e->vertical)
                win->drag_own_dx += d;
            else if (e->after->len == 0)
                win->drag_own_dy += d;
        }
    }

    sticky_mark (win, edges, 2);
    sticky_edge_free (&edges[0]);
    sticky_edge_free (&edges[1]);
}

/* --------------------------------------------------------------------------------------------- */

/* No window of the screen is drawn as dragged along */
static void
sticky_unmark (WGroup *g)
{
    GList *l;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (WIDGET (l->data)))
            EDIT_WINDOW (l->data)->dragged_along = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* A move or resize of win begins: where every window of the screen is, to go back to */
static void
drag_begin (WEditWindow *win)
{
    GList *l;

    for (l = WIDGET (win)->owner->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (WIDGET (l->data)))
        {
            WEditWindow *ow = EDIT_WINDOW (l->data);

            ow->drag_rect = WIDGET (ow)->rect;
            ow->moved_vedge = ow->moved_hedge = 0;
            ow->dragged_along = 0;
        }

    win->drag_open = 1;
    win->drag_own = 0;
    win->drag_own_dx = win->drag_own_dy = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The resize begins, from where win is now: which sides of it stay on the edge of the screen;
   with sticky windows, the neighbors that will resize along drawn as dragged */
static void
resize_begin (WEditWindow *win)
{
    Widget *w = WIDGET (win);
    WRect a;

    edit_window_area (DIALOG (w->owner), &a);
    win->pin_right = (w->rect.x + w->rect.cols == a.x + a.cols);
    win->pin_bottom = (w->rect.y + w->rect.lines == a.y + a.lines);

    if (edit_options.sticky_windows)
        sticky_resize (win, 0, 0);
    else
        sticky_unmark (w->owner);
    widget_draw (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */

/* The move or resize of win ends.  Kept: a window that made room for another keeps being in it
   though an edge it shares with neighbors has moved its edge of the room, so that hiding the other
   puts it back as before; and win changed by itself counts as moved by the user.  Not kept: every
   window goes back where it was when it began.  Either way no window is drawn as dragged any
   more */
static void
drag_end (WEditWindow *win, gboolean keep)
{
    Widget *w = WIDGET (win);
    WGroup *g = w->owner;
    const gboolean moved = win->drag_state == EDIT_WINDOW_DRAG_MOVE;
    GList *l;

    win->drag_state = EDIT_WINDOW_DRAG_NONE;
    w->mouse.forced_capture = FALSE;

    if (g == NULL || win->drag_open == 0)
        return;
    win->drag_open = 0;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
    {
        Widget *wl = WIDGET (l->data);
        WEditWindow *ow;

        if (!edit_window_is_window (wl))
            continue;
        ow = EDIT_WINDOW (wl);

        if (keep && ow->rooms != NULL)
        {
            guint i;

            for (i = 0; i < ow->rooms->len; i++)
            {
                edit_window_room_t *room = &g_array_index (ow->rooms, edit_window_room_t, i);
                Widget *m = widget_find_by_id (WIDGET (g), room->id);
                WEditWindow *mw;

                if (m == NULL || !edit_window_is_window (m))
                    continue;
                mw = EDIT_WINDOW (m);
                if (room->side == EDIT_WINDOW_ROOM_ABOVE ? mw->moved_hedge : mw->moved_vedge)
                    room->edge_after = edit_window_room_edge (&m->rect, room->side);
            }
        }
        else if (!keep && ow->fullscreen == 0 && !rects_are_equal (&wl->rect, &ow->drag_rect))
            widget_set_size_rect (wl, &ow->drag_rect);
    }

    // an edge of its own moved and moved back is no change
    if (keep
        && ((win->drag_own != 0 && !rects_are_equal (&w->rect, &win->drag_rect))
            || win->drag_own_dx != 0 || win->drag_own_dy != 0))
        win->user_moves++;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (WIDGET (l->data)))
        {
            WEditWindow *ow = EDIT_WINDOW (l->data);

            ow->dragged_along = ow->moved_vedge = ow->moved_hedge = 0;
        }
    win->pin_right = win->pin_bottom = 0;
    win->drag_own = 0;
    win->drag_own_dx = win->drag_own_dy = 0;

    // a window of a dock moved leaves it; resized, the dock takes its size
    if (keep && !rects_are_equal (&w->rect, &win->drag_rect)
        && edit_dock_side (win) != EDIT_DOCK_NONE)
        edit_dock_dragged (win, moved);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Move window by one row or column in any direction.
 *
 * @param win     window
 * @param command direction (CK_Up, CK_Down, CK_Left, CK_Right)
 */

static void
edit_window_move (WEditWindow *win, long command)
{
    Widget *w = WIDGET (win);
    WRect r = w->rect;
    WRect a;

    edit_window_area (DIALOG (w->owner), &a);

    switch (command)
    {
    case CK_Up:
        if (r.y > a.y)
            r.y--;
        break;
    case CK_Down:
        if (r.y < a.y + a.lines - 1)
            r.y++;
        break;
    case CK_Left:
        if (r.x + a.cols > a.x)
            r.x--;
        break;
    case CK_Right:
        if (r.x < a.x + a.cols)
            r.x++;
        break;
    default:
        return;
    }

    if (rects_are_equal (&r, &w->rect))
        return;

    // the widgets of the window move with it
    widget_set_size_rect (w, &r);
    win->drag_own = 1;
    widget_draw (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Resize window by one row or column in any direction.
 *
 * @param win     window
 * @param command direction (CK_Up, CK_Down, CK_Left, CK_Right)
 */

static void
edit_window_resize (WEditWindow *win, long command)
{
    Widget *w = WIDGET (win);
    WRect r = w->rect;
    WRect a;

    if (edit_options.sticky_windows)
    {
        sticky_resize (win,
                       command == CK_Left        ? -1
                           : command == CK_Right ? 1
                                                 : 0,
                       command == CK_Up         ? -1
                           : command == CK_Down ? 1
                                                : 0);
        widget_draw (WIDGET (w->owner));
        return;
    }

    edit_window_area (DIALOG (w->owner), &a);

    switch (command)
    {
    case CK_Up:
        if (r.lines > win->klass->min_lines)
            r.lines--;
        break;
    case CK_Down:
        if (r.y + r.lines < a.y + a.lines)
            r.lines++;
        break;
    case CK_Left:
        if (r.cols > win->klass->min_cols)
            r.cols--;
        break;
    case CK_Right:
        if (r.x + r.cols < a.x + a.cols)
            r.cols++;
        break;
    default:
        return;
    }

    if (rects_are_equal (&r, &w->rect))
        return;

    // the widgets of the window resize with it
    widget_set_size_rect (w, &r);
    win->drag_own = 1;
    widget_draw (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Handle move/resize mouse events.
 */

static void
edit_window_mouse_move_resize (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    WEditWindow *win = EDIT_WINDOW (w);
    WRect r = w->rect;
    WRect a;
    int global_x, global_y;

    if (msg == MSG_MOUSE_UP)
    {
        // Exit move/resize mode
        edit_window_handle_move_resize (win, CK_Enter);
        return;
    }

    if (msg != MSG_MOUSE_DRAG)
        /**
         * We ignore any other events. Specifically, MSG_MOUSE_DOWN.
         *
         * When the move/resize is initiated by the menu, we let the user
         * stop it by clicking with the mouse. Which is why we don't want
         * a mouse down to affect the window.
         */
        return;

    // Convert point to global coordinates for easier calculations.
    global_x = event->x + r.x;
    global_y = event->y + r.y;

    // Clamp the point to the area of the windows.
    edit_window_area (DIALOG (w->owner), &a);
    global_y = CLAMP (global_y, a.y, a.y + a.lines - 1);
    global_x = CLAMP (global_x, a.x, a.x + a.cols - 1);

    if (win->drag_state == EDIT_WINDOW_DRAG_MOVE)
    {
        r.y = global_y;
        r.x = global_x - win->drag_state_start;
    }
    else if (win->drag_state == EDIT_WINDOW_DRAG_RESIZE && edit_options.sticky_windows)
    {
        // the corner goes under the mouse, the sticky neighbors along
        sticky_resize (win, global_x - (r.x + r.cols - 1), global_y - (r.y + r.lines - 1));
        widget_draw (WIDGET (w->owner));
        return;
    }
    else if (win->drag_state == EDIT_WINDOW_DRAG_RESIZE)
    {
        r.lines = MAX (win->klass->min_lines, global_y - r.y + 1);
        r.cols = MAX (win->klass->min_cols, global_x - r.x + 1);
    }

    if (rects_are_equal (&r, &w->rect))
        return;

    // the widgets of the window move and resize with it
    widget_set_size_rect (w, &r);
    win->drag_own = 1;

    // We draw the whole dialog because dragging/resizing exposes area beneath
    widget_draw (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Handle mouse events of a window: its frame here, the rest in the class.
 *
 * @param w Widget object (the window)
 * @param msg mouse event message
 * @param event mouse event data
 */

static void
edit_window_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    WEditWindow *win = EDIT_WINDOW (w);
    // location of 'Close' and 'Toggle fullscreen' pictograms
    const int close_x = (w->rect.cols - 1) - 2 - 1;
    const int toggle_fullscreen_x = close_x - 3;
    // the top line of the frame; a fullscreen window has no frame
    const gboolean on_title = (win->fullscreen == 0 && event->y == 0);

    if (win->drag_state != EDIT_WINDOW_DRAG_NONE)
    {
        // window is being resized/moved
        edit_window_mouse_move_resize (w, msg, event);
        return;
    }

    /* If it's the last line on the screen, we abort the event to make the
     * system channel it to the overlapping buttonbar instead. We have to do
     * this because a window has the WOP_TOP_SELECT flag, which makes it above
     * the buttonbar in Z-order. */
    if (msg == MSG_MOUSE_DOWN && (event->y + w->rect.y == LINES - 1))
    {
        event->result.abort = TRUE;
        return;
    }

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        widget_select (w);

        if (on_title)
        {
            if (event->x >= close_x - 1 && event->x <= close_x + 1)
                ;  // do nothing (see MSG_MOUSE_CLICK)
            else if (event->x >= toggle_fullscreen_x - 1 && event->x <= toggle_fullscreen_x + 1)
                ;  // do nothing (see MSG_MOUSE_CLICK)
            else if (edit_dock_tab_click (win, event->x))
                ;  // a tab of the bottom: seen
            else if (win->klass->title_click != NULL && win->klass->title_click (win, event->x))
                ;  // something of the class in the title: a tab of its own
            else
            {
                // start window move
                edit_window_handle_move_resize (win, CK_WindowMove);
                win->drag_state_start = event->x;
            }
            return;
        }

        if (win->fullscreen == 0 && event->y == w->rect.lines - 1 && event->x == w->rect.cols - 1)
        {
            // bottom-right corner -- start window resize
            edit_window_handle_move_resize (win, CK_WindowResize);
            return;
        }
        break;

    case MSG_MOUSE_CLICK:
        if (on_title)
        {
            if (event->x >= close_x - 1 && event->x <= close_x + 1)
                send_message (w->owner, NULL, MSG_ACTION, CK_Close, NULL);
            else if (event->x >= toggle_fullscreen_x - 1 && event->x <= toggle_fullscreen_x + 1)
                edit_window_toggle_fullscreen (win);
            else if (event->count == GPM_DOUBLE)
                // double click on top line (toggle fullscreen)
                edit_window_toggle_fullscreen (win);
            return;
        }
        break;

    default:
        break;
    }

    // the top line of the frame is the frame's, whatever comes there
    if (on_title)
        return;

    if (win->klass->mouse_callback != NULL)
        win->klass->mouse_callback (w, msg, event);
    else
        // the widgets of the window take it
        event->result.abort = TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get hotkey by number.
 *
 * @param n number
 * @return hotkey
 */

static unsigned char
get_hotkey (int n)
{
    return (n <= 9) ? '0' + n : 'a' + n - 10;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/**
 * Make a window of a class: fullscreen, with @r to go back to.
 *
 * @param win   window to fill in
 * @param r     location of the window when it is not fullscreen
 * @param klass what the window does on its own
 */

void
edit_window_init (WEditWindow *win, const WRect *r, const edit_window_class_t *klass)
{
    Widget *w = WIDGET (win);

    group_init (GROUP (win), r, edit_window_callback, edit_window_mouse_callback);
    w->mouse_handler = edit_window_mouse_handler;
    w->options |= WOP_SELECTABLE | WOP_TOP_SELECT | WOP_WANT_CURSOR;
    win->klass = klass;
    win->drag_state = EDIT_WINDOW_DRAG_NONE;
    win->fullscreen = 1;
    edit_window_save_size (win);

    // the scrollbars of the frame, placed where the frame is
    if (klass->vbar)
    {
        win->vbar = scrollbar_new (0, 0, 1, SCROLLBAR_VERTICAL);
        group_add_widget (GROUP (win), win->vbar);
    }
    if (klass->hbar_x > 0)
    {
        win->hbar = scrollbar_new (0, 0, 1, SCROLLBAR_HORIZONTAL);
        group_add_widget (GROUP (win), win->hbar);
    }
    edit_window_place_bars (win);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_window_set_scroll (WEditWindow *win, gboolean vertical, long total, long visible, long pos)
{
    WScrollBar *b = vertical ? win->vbar : win->hbar;

    if (b != NULL)
        scrollbar_set_range (b, total, visible, pos);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_window_draw_bars (WEditWindow *win, int color)
{
    WScrollBar *bars[] = { win->vbar, win->hbar };
    size_t i;

    // where the frame is now, and whether there is one
    edit_window_place_bars (win);

    for (i = 0; i < G_N_ELEMENTS (bars); i++)
        if (bars[i] != NULL && widget_get_state (WIDGET (bars[i]), WST_VISIBLE))
        {
            scrollbar_set_color (bars[i], color);
            send_message (WIDGET (bars[i]), NULL, MSG_DRAW, 0, NULL);
        }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Check if widget is a window of the editor screen.
 *
 * @param w probably window object
 * @return TRUE if widget is a window, FALSE otherwise
 */

gboolean
edit_window_is_window (const Widget *w)
{
    return (w != NULL && w->callback == edit_window_callback);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Put a window on the screen and select it; the caller draws the screen. The screen owns the window
 * from now on: it is destroyed with the screen or when it is closed, and its class learns of that
 * by MSG_DESTROY.
 *
 * @param h   editor screen
 * @param win window made with edit_window_init()
 */

void
edit_window_add (WDialog *h, WEditWindow *win)
{
    Widget *w = WIDGET (win);

    // a fullscreen window follows the size of the screen, another one keeps its place
    group_add_widget_autopos (GROUP (h), w,
                              win->fullscreen != 0 ? WPOS_KEEP_ALL : WPOS_KEEP_DEFAULT, NULL);

    // a window put on a running screen comes up with it, and the widgets of the window too
    if (widget_get_state (WIDGET (h), WST_ACTIVE))
        widget_set_state (w, WST_ACTIVE, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Take a window off the screen and destroy it. The topmost visible window left is selected: the
 * group alone would make current the widget after the window, which need not be a window.
 *
 * @param win window to destroy
 */

void
edit_window_destroy (WEditWindow *win)
{
    Widget *w = WIDGET (win);
    WGroup *g = w->owner;
    Widget *top = NULL;
    GList *l;

    // a file window: its plugins are told while it is on the screen, where they find it
    if (edit_widget_is_editor (w))
        edit_plugins_tell_closed (EDIT (w));
    // a window stops moving or resizing when it goes, its neighbors left where they are
    drag_end (win, TRUE);
    // the others of its dock take its room
    (void) edit_dock_remove (win);
    group_remove_widget (w);
    widget_destroy (w);

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (CONST_WIDGET (l->data))
            && widget_get_state (WIDGET (l->data), WST_VISIBLE))
            top = WIDGET (l->data);

    if (top != NULL)
        widget_select (top);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * End a move or resize of a window where it is: the window goes, or something else is done
 * with it.
 *
 * @param win window
 */

void
edit_window_drag_end (WEditWindow *win)
{
    drag_end (win, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_window_show (WEditWindow *win)
{
    Widget *w = WIDGET (win);

    widget_show (w);
    widget_select (w);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_window_hide (WEditWindow *win)
{
    Widget *w = WIDGET (win);

    // a window stops moving or resizing when it goes, its neighbors left where they are
    drag_end (win, TRUE);
    widget_hide (w);
}

/* --------------------------------------------------------------------------------------------- */
/* The edge of a window a room moves: its bottom, its right side or its left side */
static int
edit_window_room_edge (const WRect *r, edit_window_room_side_t side)
{
    switch (side)
    {
    case EDIT_WINDOW_ROOM_ABOVE:
        return r->y + r->lines - 1;
    case EDIT_WINDOW_ROOM_LEFT:
        return r->x + r->cols - 1;
    default:
        return r->x;
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Make room for a window that does not fill the screen: when @win takes all the height of the
 * screen, to the left of it, or to the right of it when it is on the left edge; else above it.
 * The topmost fullscreen window stops being fullscreen and takes that area; every other window
 * that goes into @win shrinks out of it, its side (or its bottom) put next to @win.  Nothing is
 * done when the fullscreen window has no room; a window that would become smaller than its
 * smallest size is left as it is.  The edge each window moved is remembered, to be put back.
 *
 * @param win window to make room for
 */

void
edit_window_make_room (WEditWindow *win)
{
    Widget *w = WIDGET (win);
    WGroup *g = w->owner;
    WEditWindow *top = NULL;
    edit_window_room_side_t side;
    gboolean full_height;
    WRect a;
    GList *l;

    if (g == NULL || win->fullscreen != 0 || win->rooms != NULL)
        return;

    edit_window_area (DIALOG (g), &a);
    /* a window of all the height: the room is on the side of it the screen goes on, else above
       it */
    full_height = w->rect.y <= a.y && w->rect.y + w->rect.lines >= a.y + a.lines;
    if (full_height && w->rect.x > a.x)
        side = EDIT_WINDOW_ROOM_LEFT;
    else if (full_height && w->rect.x + w->rect.cols < a.x + a.cols)
        side = EDIT_WINDOW_ROOM_RIGHT;
    else
        side = EDIT_WINDOW_ROOM_ABOVE;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
    {
        Widget *wl = WIDGET (l->data);

        if (wl != w && edit_window_is_window (wl) && widget_get_state (wl, WST_VISIBLE)
            && EDIT_WINDOW (wl)->fullscreen != 0)
            top = EDIT_WINDOW (wl);
    }

    // a fullscreen window with no room stays as it is, and so does everything else
    if (top != NULL
        && (side == EDIT_WINDOW_ROOM_LEFT ? w->rect.x - a.x < top->klass->min_cols
                : side == EDIT_WINDOW_ROOM_RIGHT
                ? a.x + a.cols - (w->rect.x + w->rect.cols) < top->klass->min_cols
                : w->rect.y - a.y < top->klass->min_lines))
        return;

    win->rooms = g_array_new (FALSE, FALSE, sizeof (edit_window_room_t));

    for (l = g->widgets; l != NULL; l = g_list_next (l))
    {
        Widget *wl = WIDGET (l->data);
        WEditWindow *ow;
        edit_window_room_t room;
        WRect r;

        if (wl == w || !edit_window_is_window (wl) || !widget_get_state (wl, WST_VISIBLE))
            continue;
        ow = EDIT_WINDOW (wl);

        if (ow == top)
        {
            r = a;
            if (side == EDIT_WINDOW_ROOM_LEFT)
                r.cols = w->rect.x - a.x;
            else if (side == EDIT_WINDOW_ROOM_RIGHT)
            {
                r.x = w->rect.x + w->rect.cols;
                r.cols = a.x + a.cols - r.x;
            }
            else
                r.lines = w->rect.y - a.y;
            // fullscreen again when it takes the whole screen again
            ow->room_fullscreen = 1;
            ow->room_loc_prev = ow->loc_prev;
        }
        else if (ow->fullscreen != 0)
            continue;  // under the top one, unseen
        else
        {
            // a window that goes into @win shrinks out of it, if it starts before it
            r = wl->rect;
            if (!rects_are_overlapped (&wl->rect, &w->rect))
                continue;
            if (side == EDIT_WINDOW_ROOM_LEFT)
            {
                if (wl->rect.x >= w->rect.x)
                    continue;
                r.cols = w->rect.x - wl->rect.x;
            }
            else if (side == EDIT_WINDOW_ROOM_RIGHT)
            {
                // a window that goes into @win shrinks out of it, if it ends after it
                if (wl->rect.x + wl->rect.cols <= w->rect.x + w->rect.cols)
                    continue;
                r.x = w->rect.x + w->rect.cols;
                r.cols = wl->rect.x + wl->rect.cols - r.x;
            }
            else
            {
                if (wl->rect.y >= w->rect.y)
                    continue;
                r.lines = w->rect.y - wl->rect.y;
            }
            if (r.cols < ow->klass->min_cols || r.lines < ow->klass->min_lines)
                continue;
        }

        room.id = wl->id;
        room.side = side;
        {
            const WRect *before = ow == top ? &a : &wl->rect;

            room.edge_before = edit_window_room_edge (before, side);
        }
        room.edge_after = edit_window_room_edge (&r, side);
        room.user_moves = ow->user_moves;
        g_array_append_val (win->rooms, room);

        ow->fullscreen = 0;
        wl->pos_flags = WPOS_KEEP_DEFAULT;
        widget_set_size_rect (wl, &r);
    }

    if (win->rooms->len == 0)
        g_clear_pointer (&win->rooms, g_array_unref);

    widget_draw (WIDGET (g));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Give back the room edit_window_make_room() made: the edge each window moved for it goes back,
 * the other sides staying where they are, so that the rooms can be given back in any order; a
 * window that was fullscreen is again when it takes the whole screen.  A window whose edge the
 * user has moved since, or that has been made fullscreen, is left as it is.
 *
 * @param win window the room was made for
 */

void
edit_window_give_room_back (WEditWindow *win)
{
    Widget *w = WIDGET (win);
    GArray *rooms = win->rooms;
    WRect a;
    guint i;
    GList *l;

    // a window of a dock: the others of the dock take its room
    if (edit_dock_remove (win))
        return;

    win->rooms = NULL;

    /* a window of a column under this one takes its place, and the rooms with it: the others
       stay as they are */
    for (l = w->owner != NULL ? w->owner->widgets : NULL; l != NULL; l = g_list_next (l))
    {
        Widget *wl = WIDGET (l->data);

        if (wl != w && edit_window_is_window (wl) && widget_get_state (wl, WST_VISIBLE)
            && EDIT_WINDOW (wl)->fullscreen == 0 && wl->rect.x == w->rect.x
            && wl->rect.cols == w->rect.cols && wl->rect.y == w->rect.y + w->rect.lines)
        {
            WEditWindow *below = EDIT_WINDOW (wl);
            WRect r = wl->rect;

            r.lines += r.y - w->rect.y;
            r.y = w->rect.y;
            widget_set_size_rect (wl, &r);
            if (rooms != NULL)
            {
                if (below->rooms == NULL)
                    below->rooms = rooms;
                else
                {
                    g_array_append_vals (below->rooms, rooms->data, rooms->len);
                    g_array_unref (rooms);
                }
            }
            widget_draw (WIDGET (w->owner));
            return;
        }
    }

    if (rooms == NULL || w->owner == NULL)
    {
        if (rooms != NULL)
            g_array_unref (rooms);
        return;
    }

    edit_window_area (DIALOG (w->owner), &a);

    for (i = 0; i < rooms->len; i++)
    {
        const edit_window_room_t *room = &g_array_index (rooms, edit_window_room_t, i);
        Widget *wt = widget_find_by_id (WIDGET (w->owner), room->id);
        WEditWindow *ow;
        WRect r;

        if (wt == NULL || wt == w || !edit_window_is_window (wt))
            continue;
        ow = EDIT_WINDOW (wt);
        r = wt->rect;
        if (ow->fullscreen != 0 || ow->user_moves != room->user_moves
            || edit_window_room_edge (&r, room->side) != room->edge_after)
            continue;

        if (room->side == EDIT_WINDOW_ROOM_ABOVE)
            r.lines = room->edge_before - r.y + 1;
        else if (room->side == EDIT_WINDOW_ROOM_LEFT)
            r.cols = room->edge_before - r.x + 1;
        else
        {
            r.cols += r.x - room->edge_before;
            r.x = room->edge_before;
        }

        if (ow->room_fullscreen != 0 && rects_are_equal (&r, &a))
        {
            ow->fullscreen = 1;
            ow->room_fullscreen = 0;
            ow->loc_prev = ow->room_loc_prev;
            wt->pos_flags = WPOS_KEEP_ALL;
        }
        widget_set_size_rect (wt, &r);
    }

    g_array_unref (rooms);
    widget_draw (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_window_area (const WDialog *h, WRect *r)
{
    *r = CONST_WIDGET (h)->rect;
    rect_grow (r, -1, 0);
}

/* --------------------------------------------------------------------------------------------- */

/* A window being fit into the area, and where it starts from */
typedef struct
{
    Widget *w;
    WRect from;
} fit_window_t;

/* The area the windows were first put in, before the screen was resized, while they stay as the
   resizes put them */
static WRect fit_base_area;

/* --------------------------------------------------------------------------------------------- */

static void
fit_add (GArray *v, int b)
{
    guint i;

    for (i = 0; i < v->len && g_array_index (v, int, i) < b; i++)
        ;
    if (i == v->len || g_array_index (v, int, i) != b)
        g_array_insert_val (v, i, b);
}

/* --------------------------------------------------------------------------------------------- */

static int
fit_lookup (const GArray *v, const GArray *map, int b)
{
    guint i;

    for (i = 0; i < v->len; i++)
        if (g_array_index (v, int, i) == b)
            return g_array_index (map, int, i);
    return b;
}

/* --------------------------------------------------------------------------------------------- */

/* The place each boundary of the windows (a first column, or the one after the last) goes along
   one axis when the area ends at @end instead of @old_end: what was at the old end goes to the new
   one, what is past the new end comes to it, and the windows before make way for those after,
   down to their smallest size.  Returns the boundaries, sorted, and their places in @map */
static GArray *
fit_axis (const GArray *wins, gboolean vertical, int start, int old_end, int end, GArray **map)
{
    GArray *v, *m;
    guint i, k;

    v = g_array_new (FALSE, FALSE, sizeof (int));
    for (k = 0; k < wins->len; k++)
    {
        const WRect *r = &g_array_index (wins, fit_window_t, k).from;

        fit_add (v, vertical ? r->y : r->x);
        fit_add (v, vertical ? r->y + r->lines : r->x + r->cols);
    }
    fit_add (v, old_end);

    m = g_array_sized_new (FALSE, TRUE, sizeof (int), v->len);
    g_array_set_size (m, v->len);

    for (i = v->len; i-- > 0;)
    {
        const int b = g_array_index (v, int, i);
        int p = (b == old_end) ? end : MIN (b, end);

        // no further than the boundary after it
        if (i + 1 < v->len)
            p = MIN (p, g_array_index (m, int, i + 1));

        // each window that starts here keeps its smallest size
        for (k = 0; k < wins->len; k++)
        {
            const fit_window_t *f = &g_array_index (wins, fit_window_t, k);
            const WEditWindow *ow = CONST_EDIT_WINDOW (f->w);
            const int first = vertical ? f->from.y : f->from.x;
            const int last = first + (vertical ? f->from.lines : f->from.cols);

            if (first == b)
                p = MIN (p,
                         fit_lookup (v, m, last)
                             - (vertical ? ow->klass->min_lines : ow->klass->min_cols));
        }

        // out of the area before it: left as it is
        g_array_index (m, int, i) = (b >= start) ? MAX (p, start) : b;
    }

    *map = m;
    return v;
}

/* --------------------------------------------------------------------------------------------- */

/* Where @edge (the last row or column of a frame) of a room goes: to where the window whose edge
   it was has its edge now */
static int
fit_room_edge (const GArray *wins, gboolean vertical, int edge, int old_end, int end)
{
    guint k;

    if (edge + 1 == old_end)
        return end - 1;

    for (k = 0; k < wins->len; k++)
    {
        const fit_window_t *f = &g_array_index (wins, fit_window_t, k);
        const WRect *was = &f->w->rect;
        const WRect *now = &EDIT_WINDOW (f->w)->fit_done;

        if (vertical && was->y + was->lines - 1 == edge)
            return now->y + now->lines - 1;
        if (!vertical && was->x + was->cols - 1 == edge)
            return now->x + now->cols - 1;
    }
    return MIN (edge, end - 1);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Fit the windows into the area of the editor screen after the screen has been resized: the
 * windows at the right (bottom) of the old area stay at the right of the new one, growing or
 * shrinking, and the windows before them make way down to their smallest size; windows that stand
 * side by side stay so.  While nobody moves them, the screen grown back gets the windows back as
 * they were.  A fullscreen window follows the screen by itself.
 *
 * @param h editor dialog
 * @param old the area before the screen was resized
 */

void
edit_window_fit_area (WDialog *h, const WRect *old)
{
    GArray *wins, *vx, *vy, *mx, *my;
    gboolean as_put = TRUE;
    WRect a, from;
    GList *l;
    guint i, k;

    edit_window_area (h, &a);
    if (a.lines == old->lines && a.cols == old->cols)
        return;

    // a move or resize ends where it is: what it would go back to is of the old screen
    for (l = GROUP (h)->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (WIDGET (l->data)))
            drag_end (EDIT_WINDOW (l->data), TRUE);

    wins = g_array_new (FALSE, FALSE, sizeof (fit_window_t));
    for (l = GROUP (h)->widgets; l != NULL; l = g_list_next (l))
    {
        fit_window_t f;

        f.w = WIDGET (l->data);
        // hidden ones too: shown again, they have to fit
        if (!edit_window_is_window (f.w) || EDIT_WINDOW (f.w)->fullscreen != 0)
            continue;
        f.from = f.w->rect;
        g_array_append_val (wins, f);
        if (EDIT_WINDOW (f.w)->fit_valid == 0
            || !rects_are_equal (&f.w->rect, &EDIT_WINDOW (f.w)->fit_done))
            as_put = FALSE;
    }

    // the windows as the last resizes put them: from where they were first
    from = *old;
    if (as_put && wins->len != 0)
    {
        from = fit_base_area;
        for (k = 0; k < wins->len; k++)
        {
            fit_window_t *f = &g_array_index (wins, fit_window_t, k);

            f->from = EDIT_WINDOW (f->w)->fit_base;
        }
    }
    else
        fit_base_area = *old;

    vx = fit_axis (wins, FALSE, a.x, from.x + from.cols, a.x + a.cols, &mx);
    vy = fit_axis (wins, TRUE, a.y, from.y + from.lines, a.y + a.lines, &my);

    for (k = 0; k < wins->len; k++)
    {
        fit_window_t *f = &g_array_index (wins, fit_window_t, k);
        WEditWindow *ow = EDIT_WINDOW (f->w);
        WRect r;

        r.x = fit_lookup (vx, mx, f->from.x);
        r.cols = MAX (1, fit_lookup (vx, mx, f->from.x + f->from.cols) - r.x);
        r.y = fit_lookup (vy, my, f->from.y);
        r.lines = MAX (1, fit_lookup (vy, my, f->from.y + f->from.lines) - r.y);

        ow->fit_base = f->from;
        ow->fit_done = r;
        ow->fit_valid = 1;
    }

    // the edges the rooms were made with move along, to be given back
    for (k = 0; k < wins->len; k++)
    {
        WEditWindow *ow = EDIT_WINDOW (g_array_index (wins, fit_window_t, k).w);

        for (i = 0; ow->rooms != NULL && i < ow->rooms->len; i++)
        {
            edit_window_room_t *room = &g_array_index (ow->rooms, edit_window_room_t, i);
            const gboolean above = room->side == EDIT_WINDOW_ROOM_ABOVE;
            const int old_end = above ? old->y + old->lines : old->x + old->cols;
            const int end = above ? a.y + a.lines : a.x + a.cols;

            room->edge_before = fit_room_edge (wins, above, room->edge_before, old_end, end);
            room->edge_after = fit_room_edge (wins, above, room->edge_after, old_end, end);
        }
    }

    for (k = 0; k < wins->len; k++)
    {
        Widget *wl = g_array_index (wins, fit_window_t, k).w;

        if (!rects_are_equal (&wl->rect, &EDIT_WINDOW (wl)->fit_done))
            widget_set_size_rect (wl, &EDIT_WINDOW (wl)->fit_done);
    }

    g_array_free (my, TRUE);
    g_array_free (mx, TRUE);
    g_array_free (vy, TRUE);
    g_array_free (vx, TRUE);
    g_array_free (wins, TRUE);

    // the docks keep their share of the screen
    edit_dock_arrange (h);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Save current window size.
 *
 * @param win window
 */

void
edit_window_save_size (WEditWindow *win)
{
    win->loc_prev = WIDGET (win)->rect;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Restore saved window size.
 *
 * @param win window
 */

void
edit_window_restore_size (WEditWindow *win)
{
    Widget *w = WIDGET (win);

    // a move or resize given up: the neighbors go back too
    drag_end (win, FALSE);
    widget_set_size_rect (w, &win->loc_prev);
    widget_draw (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Handle move/resize events.
 *
 * @param win     window
 * @param command action id
 * @return TRUE if the action was handled, FALSE otherwise
 */

gboolean
edit_window_handle_move_resize (WEditWindow *win, long command)
{
    Widget *w = WIDGET (win);
    gboolean ret = FALSE;

    if (win->fullscreen != 0)
    {
        drag_end (win, TRUE);
        return ret;
    }

    switch (win->drag_state)
    {
    case EDIT_WINDOW_DRAG_NONE:
        // possible start move/resize
        switch (command)
        {
        case CK_WindowMove:
            win->drag_state = EDIT_WINDOW_DRAG_MOVE;
            edit_window_save_size (win);
            drag_begin (win);
            edit_window_redraw (win);  // redraw frame and status
            /**
             * If a user initiates a move by the menu, not by the mouse, we
             * make a subsequent mouse drag pull the frame from its middle.
             * (We can instead choose '0' to pull it from the corner.)
             */
            win->drag_state_start = w->rect.cols / 2;
            ret = TRUE;
            break;
        case CK_WindowResize:
            win->drag_state = EDIT_WINDOW_DRAG_RESIZE;
            edit_window_save_size (win);
            drag_begin (win);
            resize_begin (win);
            ret = TRUE;
            break;
        default:
            break;
        }
        break;

    case EDIT_WINDOW_DRAG_MOVE:
        switch (command)
        {
        case CK_WindowResize:
            win->drag_state = EDIT_WINDOW_DRAG_RESIZE;
            resize_begin (win);
            ret = TRUE;
            break;
        case CK_Up:
        case CK_Down:
        case CK_Left:
        case CK_Right:
            edit_window_move (win, command);
            ret = TRUE;
            break;
        case CK_Enter:
        case CK_WindowMove:
            drag_end (win, TRUE);
            edit_window_redraw (win);  // redraw frame and status
            MC_FALLTHROUGH;
        default:
            ret = TRUE;
            break;
        }
        break;

    case EDIT_WINDOW_DRAG_RESIZE:
        switch (command)
        {
        case CK_WindowMove:
            // the window goes alone; Esc still puts the neighbors back
            win->drag_state = EDIT_WINDOW_DRAG_MOVE;
            sticky_unmark (w->owner);
            widget_draw (WIDGET (w->owner));
            ret = TRUE;
            break;
        case CK_Up:
        case CK_Down:
        case CK_Left:
        case CK_Right:
            edit_window_resize (win, command);
            ret = TRUE;
            break;
        case CK_Enter:
        case CK_WindowResize:
            // the neighbors resized along are drawn as before
            drag_end (win, TRUE);
            widget_draw (WIDGET (w->owner));
            MC_FALLTHROUGH;
        default:
            ret = TRUE;
            break;
        }
        break;

    default:
        break;
    }

    /**
     * - We let the user stop a resize/move operation by clicking with the
     *   mouse anywhere. ("clicking" = pressing and releasing a button.)
     * - We let the user perform a resize/move operation by a mouse drag
     *   initiated anywhere.
     *
     * "Anywhere" means: inside or outside the window. We make this happen
     * with the 'forced_capture' flag.
     */
    w->mouse.forced_capture = (win->drag_state != EDIT_WINDOW_DRAG_NONE);

    return ret;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Toggle window fullscreen mode.
 *
 * @param win window
 */

void
edit_window_toggle_fullscreen (WEditWindow *win)
{
    Widget *w = WIDGET (win);

    // a move or resize ends where it is: the window has gone elsewhere
    drag_end (win, TRUE);
    // out of its dock, or out of the room the docks leave
    (void) edit_dock_remove (win);
    if (win->dock_fill != 0)
    {
        win->dock_fill = 0;
        win->fullscreen = 0;
    }
    win->fullscreen = win->fullscreen != 0 ? 0 : 1;
    // the user has decided: no room gives it the screen back any more
    win->room_fullscreen = 0;

    if (win->fullscreen == 0)
    {
        edit_window_restore_size (win);
        // do not follow screen size on resize
        w->pos_flags = WPOS_KEEP_DEFAULT;
    }
    else
    {
        WRect r;

        edit_window_save_size (win);
        edit_window_area (DIALOG (w->owner), &r);
        widget_set_size_rect (w, &r);
        // follow screen size on resize
        w->pos_flags = WPOS_KEEP_ALL;
        edit_window_redraw (win);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * The color of the frame of a window.
 *
 * @param win    window
 * @param active TRUE if the window is focused
 */

int
edit_window_frame_color (const WEditWindow *win, gboolean active)
{
    if (win->fullscreen != 0)
        return STATUSBAR_COLOR;

    return win->drag_state != EDIT_WINDOW_DRAG_NONE || win->dragged_along != 0
        ? EDITOR_FRAME_DRAG_COLOR
        : active ? EDITOR_FRAME_ACTIVE_COLOR
                 : EDITOR_FRAME_COLOR;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw a frame around a window that is not fullscreen.
 *
 * @param win    window
 * @param color  color pair
 * @param active TRUE if window is focused
 */

void
edit_window_draw_frame (const WEditWindow *win, int color, gboolean active)
{
    const Widget *w = CONST_WIDGET (win);

    // draw a frame around the window
    tty_setcolor (color);
    // draw double frame for active window if skin supports that
    tty_draw_box (w->rect.y, w->rect.x, w->rect.lines, w->rect.cols, !active);
    // draw a drag marker
    if (win->drag_state == EDIT_WINDOW_DRAG_NONE)
    {
        tty_setcolor (EDITOR_FRAME_DRAG_COLOR);
        widget_gotoyx (w, w->rect.lines - 1, w->rect.cols - 1);
        tty_print_char (mc_tty_frm[MC_TTY_FRM_RIGHTBOTTOM]);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw a window control buttons: on the frame, or on the menu line when fullscreen.
 *
 * @param win   window
 * @param color color pair
 */

void
edit_window_draw_icons (const WEditWindow *win, int color)
{
    const Widget *w = CONST_WIDGET (win);
    char tmp[17];

    tty_setcolor (color);
    if (win->fullscreen != 0)
        widget_gotoyx (w->owner, 0, WIDGET (w->owner)->rect.cols - 6);
    else
        widget_gotoyx (w, 0, w->rect.cols - 8);
    g_snprintf (tmp, sizeof (tmp), "[%s][%s]", edit_window_state_char, edit_window_close_char);
    tty_print_string (tmp);
    // a window of the bottom with others there: their tabs over its title
    if (win->fullscreen == 0)
        edit_dock_draw_tabs (win, color);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_window_list (const WDialog *h)
{
    const WGroup *g = CONST_GROUP (h);
    size_t dlg_num = 0;
    int lines, cols;
    Listbox *listbox;
    GList *l;
    Widget *selected;
    int i = 0;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (CONST_WIDGET (l->data)))
            dlg_num++;

    lines = MIN ((size_t) (LINES * 2 / 3), dlg_num);
    cols = COLS * 2 / 3;

    listbox = listbox_window_new (lines, cols, _ ("Open files"), "[Open files]");
    listbox->dlg->help_file = MCEDIT_HELP_FILE;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (CONST_WIDGET (l->data)))
        {
            const WEditWindow *win = CONST_EDIT_WINDOW (l->data);
            gboolean modified;
            char *title, *item;

            modified = win->klass->is_modified != NULL && win->klass->is_modified (win);
            title = win->klass->get_title (win);
            item = g_strdup_printf ("%c%s", modified ? '*' : ' ', title);
            g_free (title);

            listbox_add_item (listbox->list, LISTBOX_APPEND_AT_END, get_hotkey (i++),
                              str_term_trim (item, WIDGET (listbox->list)->rect.cols - 2), l->data,
                              FALSE);
            g_free (item);
        }

    selected = listbox_run_with_data (listbox, g->current->data);
    if (selected != NULL)
        edit_window_show (EDIT_WINDOW (selected));
}

/* --------------------------------------------------------------------------------------------- */
