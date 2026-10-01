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

#include "edit.h"  // MCEDIT_HELP_FILE
#include "editwindow.h"

/*** global variables ****************************************************************************/

char *edit_window_state_char = NULL;
char *edit_window_close_char = NULL;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
edit_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    return EDIT_WINDOW (w)->klass->callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Mouse events: the frame and the class first, then the widgets of the window.
 */

static int
edit_window_mouse_handler (Widget *w, Gpm_Event *event)
{
    int mou;

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

    // the widgets of the window move with it
    widget_set_size_rect (w, &r);
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

    // the widgets of the window resize with it
    widget_set_size_rect (w, &r);
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
    else if (win->drag_state == EDIT_WINDOW_DRAG_RESIZE)
    {
        r.lines = MAX (win->klass->min_lines, global_y - r.y + 1);
        r.cols = MAX (win->klass->min_cols, global_x - r.x + 1);
    }

    // the widgets of the window move and resize with it
    widget_set_size_rect (w, &r);

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

void
edit_window_area (const WDialog *h, WRect *r)
{
    *r = CONST_WIDGET (h)->rect;
    rect_grow (r, -1, 0);
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

    win->drag_state = EDIT_WINDOW_DRAG_NONE;
    w->mouse.forced_capture = FALSE;
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
        win->drag_state = EDIT_WINDOW_DRAG_NONE;
        w->mouse.forced_capture = FALSE;
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
            edit_window_redraw (win);  // redraw frame and status
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
            win->drag_state = EDIT_WINDOW_DRAG_NONE;
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
            win->drag_state = EDIT_WINDOW_DRAG_MOVE;
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
            win->drag_state = EDIT_WINDOW_DRAG_NONE;
            edit_window_redraw (win);  // redraw frame and status
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

    win->fullscreen = win->fullscreen != 0 ? 0 : 1;

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

    return win->drag_state != EDIT_WINDOW_DRAG_NONE ? EDITOR_FRAME_DRAG_COLOR
        : active                                    ? EDITOR_FRAME_ACTIVE_COLOR
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
        widget_select (selected);
}

/* --------------------------------------------------------------------------------------------- */
