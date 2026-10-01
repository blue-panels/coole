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

    // the fullscreen window that made room for this one, 0 when none did
    unsigned long room_id;
    WRect room_rect;      // where that window was put
    WRect room_loc_prev;  // where that window goes back to when it is not fullscreen
};

/*** global variables defined in .c file *********************************************************/

extern char *edit_window_state_char;
extern char *edit_window_close_char;

/*** declarations of public functions ************************************************************/

void edit_window_init (WEditWindow *win, const WRect *r, const edit_window_class_t *klass);
gboolean edit_window_is_window (const Widget *w);

/* Put a window on the screen; the screen owns it from now on */
void edit_window_add (WDialog *h, WEditWindow *win);
/* Take a window off the screen and destroy it; the topmost window left is selected */
void edit_window_destroy (WEditWindow *win);
/* Show a window on top of the others and select it */
void edit_window_show (WEditWindow *win);
/* Hide a window as it is; the next window is selected */
void edit_window_hide (WEditWindow *win);
/* The fullscreen window under @win is made a window in the area to the left of @win, when @win
   takes all the height, or above it, and edit_window_give_room_back() makes it fullscreen again */
void edit_window_make_room (WEditWindow *win);
void edit_window_give_room_back (WEditWindow *win);

/* The part of the editor screen the windows take: all but the menu bar and the button bar */
void edit_window_area (const WDialog *h, WRect *r);

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
