/** \file
 *  \brief Header: a window of the editor screen
 *
 *  A window is a widget of the editor screen with a frame of its own: it is moved and resized,
 *  shown full screen, listed and closed the same way whatever it holds. The file windows are
 *  windows; what a window shows and does besides is up to its class.
 *
 *  A window is a group, the way a window of Turbo Vision is: it may hold widgets of its own,
 *  which move and resize with it and get the focus with it. A file window holds none.
 */

#ifndef MC__EDIT_WINDOW_H
#define MC__EDIT_WINDOW_H

#include "lib/global.h"
#include "lib/widget.h"  // Widget

/*** typedefs(not structures) and defined constants **********************************************/

#define EDIT_WINDOW(x)       ((WEditWindow *) (x))
#define CONST_EDIT_WINDOW(x) ((const WEditWindow *) (x))

/*** enums ***************************************************************************************/

/*
 * State of a window
 * EDIT_WINDOW_DRAG_NONE   - window is in normal mode
 * EDIT_WINDOW_DRAG_MOVE   - window is being moved
 * EDIT_WINDOW_DRAG_RESIZE - window is being resized
 */
typedef enum
{
    EDIT_WINDOW_DRAG_NONE = 0,
    EDIT_WINDOW_DRAG_MOVE,
    EDIT_WINDOW_DRAG_RESIZE
} edit_window_drag_state_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct WEditWindow WEditWindow;

/* Where a window made room for another went */
typedef enum
{
    EDIT_WINDOW_ROOM_ABOVE,  // above the other: its bottom moved
    EDIT_WINDOW_ROOM_LEFT,   // to the left of it: its right side moved
    EDIT_WINDOW_ROOM_RIGHT   // to the right of it: its left side moved
} edit_window_room_side_t;

/* A window that made room for another: the edge of it that moved out of the way, where it was
   and where it was put */
typedef struct
{
    unsigned long id;
    edit_window_room_side_t side;
    int edge_before;          // the row (column) of its frame on that edge before
    int edge_after;           // and after
    unsigned int user_moves;  // the moves of the window by the user then
} edit_window_room_t;

/* What a kind of window does on its own */
typedef struct
{
    /* Messages to the window; the unhandled ones go to group_default_callback() for a window
       that holds widgets, to widget_default_callback() for one that draws itself. On MSG_FOCUS
       the class puts its labels on the button bar, and the screen shows them */
    widget_cb_fn callback;
    /* Mouse events inside the window that the frame does not take; NULL passes them to the
       widgets of the window */
    widget_mouse_cb_fn mouse_callback;
    /* The name of the window in the list of windows. Caller frees */
    char *(*get_title) (const WEditWindow *win);
    /* Whether the window holds changes that are not saved */
    gboolean (*is_modified) (const WEditWindow *win);
    /* Close the window, asking first when it has to. TRUE when it is closed and destroyed */
    gboolean (*close) (WEditWindow *win);
    /* The editor is about to end and the window is modified: ask whether it may go.
       TRUE when it may; NULL lets it go unasked */
    gboolean (*ok_to_quit) (WEditWindow *win);
    /* The smallest size the window can be resized to, with its frame */
    int min_lines;
    int min_cols;
    /* The scrollbars of the frame of a window that is not fullscreen: whether there is the vertical
       one down the right side, and the column of the bottom where the horizontal one starts, 0
       for none */
    gboolean vbar;
    int hbar_x;
    /* A scrollbar has moved: the view goes to pos, as near as it can, and the class gives the
       bars the range again (edit_window_set_scroll ()).  NULL for a window without bars */
    void (*scrolled) (WEditWindow *win, gboolean vertical, long pos);
    /* A click at column x of the title, the top line of the frame, where the frame has nothing of
       its own: TRUE when the class takes it (a tab of its own); else the window is moved.  NULL
       for a window whose title is the frame's alone */
    gboolean (*title_click) (WEditWindow *win, int x);
} edit_window_class_t;

struct WEditWindow
{
    WGroup group;
    const edit_window_class_t *klass;
    edit_window_drag_state_t drag_state;
    int drag_state_start;  // save cursor position before window moving
    // save location before move/resize or toggle to fullscreen
    WRect loc_prev;
    unsigned int fullscreen : 1;  // Is window fullscreen or not
    // resized along with the window dragged, its neighbors being sticky: drawn as dragged
    unsigned int dragged_along : 1;
    // a resize moved it at an edge it shares with neighbors: its right side (vertical) or its
    // bottom (horizontal)
    unsigned int moved_vedge : 1;
    unsigned int moved_hedge : 1;
    // its right side and its bottom were on the edge of the screen when the resize began: they
    // stay there
    unsigned int pin_right : 1;
    unsigned int pin_bottom : 1;
    // a move or resize of it is going on: Esc puts every window back where it was then
    unsigned int drag_open : 1;
    // the move or resize has changed it by itself, sticky windows off
    unsigned int drag_own : 1;
    // and with sticky windows on: how far it has moved the edges of its own, without neighbors
    int drag_own_dx;
    int drag_own_dy;
    // fullscreen till it gave room to another: it is again when it takes the whole screen again
    unsigned int room_fullscreen : 1;
    // fullscreen till the docks took part of the screen: it fills the rest, and is fullscreen again
    // when the docks are empty
    unsigned int dock_fill : 1;
    WRect room_loc_prev;  // where it goes back to then, when it is not fullscreen
    WRect drag_rect;      // where it was when the move or resize of a window began
    // where it was before the screen was resized, and where that put it: while the windows stay as
    // they were put, the next resize starts from the first, so that the screen grown back gets
    // them back
    WRect fit_base;
    WRect fit_done;
    unsigned int fit_valid : 1;
    // the moves and resizes of it by the user that changed it, but at an edge shared with sticky
    // neighbors
    unsigned int user_moves;

    // edit_window_room_t: the windows that made room for this one; NULL when none did
    GArray *rooms;

    // the scrollbars of the frame, widgets of the window; hidden while it is fullscreen
    WScrollBar *vbar;
    WScrollBar *hbar;
};

/*** global variables defined in .c file *********************************************************/

extern char *edit_window_state_char;
extern char *edit_window_close_char;

/*** declarations of public functions ************************************************************/

void edit_window_init (WEditWindow *win, const WRect *r, const edit_window_class_t *klass);

/* The range of a scrollbar of the window: the view of visible of total at pos */
void edit_window_set_scroll (WEditWindow *win, gboolean vertical, long total, long visible,
                             long pos);
/* Draw the scrollbars over the frame, after the frame */
void edit_window_draw_bars (WEditWindow *win, int color);
gboolean edit_window_is_window (const Widget *w);

/* Put a window on the screen; the screen owns it from now on */
void edit_window_add (WDialog *h, WEditWindow *win);
/* Take a window off the screen and destroy it; the topmost window left is selected */
void edit_window_destroy (WEditWindow *win);
/* Show a window on top of the others and select it */
void edit_window_show (WEditWindow *win);
/* Hide a window as it is; the next window is selected */
void edit_window_hide (WEditWindow *win);
void edit_window_drag_end (WEditWindow *win);
/* Room for @win: to the left of it, when it takes all the height, or above it.  The fullscreen
   window under it becomes a window there, and the other windows that go into it shrink out of it;
   edit_window_give_room_back() puts each back as it was, unless it has been moved since */
void edit_window_make_room (WEditWindow *win);
void edit_window_give_room_back (WEditWindow *win);

/* The part of the editor screen the windows take: all but the menu bar and the button bar */
void edit_window_area (const WDialog *h, WRect *r);
void edit_window_fit_area (WDialog *h, const WRect *old);

void edit_window_save_size (WEditWindow *win);
void edit_window_restore_size (WEditWindow *win);
gboolean edit_window_handle_move_resize (WEditWindow *win, long command);
void edit_window_toggle_fullscreen (WEditWindow *win);

int edit_window_frame_color (const WEditWindow *win, gboolean active);
void edit_window_draw_frame (const WEditWindow *win, int color, gboolean active);
void edit_window_draw_icons (const WEditWindow *win, int color);

void edit_window_list (const WDialog *h);

/*** inline functions ****************************************************************************/

#endif
