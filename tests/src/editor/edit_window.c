/*
   src/editor - tests for the windows of the editor screen

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"
#include "src/editor/editdock.h"

/* A window that is not a file: the windows of the screen are not the editor's alone */
typedef struct
{
    WEditWindow window;
    int closed;
} test_window_t;

/* A dialog, not a bare group: a window reaches its owner as one */
static WDialog owner;
static test_window_t *test_win;

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
test_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    return group_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

static char *
test_window_get_title (const WEditWindow *win)
{
    (void) win;
    return g_strdup ("test");
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_window_close (WEditWindow *win)
{
    ((test_window_t *) win)->closed++;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static const edit_window_class_t test_window_class = {
    .callback = test_window_callback,
    .mouse_callback = NULL,
    .get_title = test_window_get_title,
    .is_modified = NULL,
    .close = test_window_close,
    .min_lines = 4,
    .min_cols = 6,
};

/* --------------------------------------------------------------------------------------------- */

static void
test_assert_rect (const WRect *r, int y, int x, int lines, int cols)
{
    ck_assert_int_eq (r->y, y);
    ck_assert_int_eq (r->x, x);
    ck_assert_int_eq (r->lines, lines);
    ck_assert_int_eq (r->cols, cols);
}

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    WRect r;

    str_init_strings (NULL);
    edit_options.filesize_threshold = (char *) "64M";

    memset (&owner, 0, sizeof (owner));
    rect_init (&r, 0, 0, 24, 80);
    group_init (&owner.group, &r, NULL, NULL);

    test_win = g_new0 (test_window_t, 1);
    rect_init (&r, 5, 5, 10, 30);
    edit_window_init (&test_win->window, &r, &test_window_class);
    group_add_widget (&owner.group, WIDGET (test_win));
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    group_remove_widget (WIDGET (test_win));
    // the widgets of the window go with it
    send_message (test_win, NULL, MSG_DESTROY, 0, NULL);
    g_free (test_win);
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

/* A new window is fullscreen and keeps the location it was made with to go back to */
START_TEST (test_window_init)
{
    const Widget *w = CONST_WIDGET (test_win);

    ck_assert (edit_window_is_window (w));
    ck_assert (!edit_widget_is_editor (w));
    ck_assert_int_eq (test_win->window.fullscreen, 1);
    ck_assert_int_eq (test_win->window.drag_state, EDIT_WINDOW_DRAG_NONE);
    test_assert_rect (&test_win->window.loc_prev, 5, 5, 10, 30);
    ck_assert (widget_get_options (w, WOP_SELECTABLE));
    ck_assert (widget_get_options (w, WOP_TOP_SELECT));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The windows take all of the screen but the menu bar and the button bar */
START_TEST (test_window_area)
{
    WRect a;

    edit_window_area (&owner, &a);
    test_assert_rect (&a, 1, 0, 22, 80);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Fullscreen fills the area and going back restores where the window was */
START_TEST (test_window_toggle_fullscreen)
{
    WEditWindow *win = &test_win->window;
    const WRect *r = &WIDGET (win)->rect;

    edit_window_toggle_fullscreen (win);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (r, 5, 5, 10, 30);

    edit_window_toggle_fullscreen (win);
    ck_assert_int_eq (win->fullscreen, 1);
    test_assert_rect (r, 1, 0, 22, 80);
    test_assert_rect (&win->loc_prev, 5, 5, 10, 30);

    edit_window_toggle_fullscreen (win);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (r, 5, 5, 10, 30);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A fullscreen window is neither moved nor resized */
START_TEST (test_window_fullscreen_does_not_move)
{
    WEditWindow *win = &test_win->window;

    ck_assert (!edit_window_handle_move_resize (win, CK_WindowMove));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_NONE);
    ck_assert (!edit_window_handle_move_resize (win, CK_WindowResize));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_NONE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Moving stops at the menu bar, and Enter ends it */
START_TEST (test_window_move)
{
    WEditWindow *win = &test_win->window;
    const WRect *r = &WIDGET (win)->rect;
    int i;

    edit_window_toggle_fullscreen (win);

    ck_assert (edit_window_handle_move_resize (win, CK_WindowMove));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_MOVE);
    ck_assert (WIDGET (win)->mouse.forced_capture);

    for (i = 0; i < 10; i++)
        ck_assert (edit_window_handle_move_resize (win, CK_Up));
    ck_assert (edit_window_handle_move_resize (win, CK_Right));
    test_assert_rect (r, 1, 6, 10, 30);

    // any other key is taken while the window moves
    ck_assert (edit_window_handle_move_resize (win, CK_Home));

    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_NONE);
    ck_assert (!WIDGET (win)->mouse.forced_capture);
    ck_assert (!edit_window_handle_move_resize (win, CK_Up));
    test_assert_rect (r, 1, 6, 10, 30);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Resizing stops at the size of the class and at the edge of the area; cancelling it puts the
   window back */
START_TEST (test_window_resize)
{
    WEditWindow *win = &test_win->window;
    const WRect *r = &WIDGET (win)->rect;
    int i;

    edit_window_toggle_fullscreen (win);

    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_RESIZE);

    for (i = 0; i < 100; i++)
    {
        ck_assert (edit_window_handle_move_resize (win, CK_Up));
        ck_assert (edit_window_handle_move_resize (win, CK_Right));
    }
    test_assert_rect (r, 5, 5, 4, 75);

    for (i = 0; i < 100; i++)
    {
        ck_assert (edit_window_handle_move_resize (win, CK_Down));
        ck_assert (edit_window_handle_move_resize (win, CK_Left));
    }
    test_assert_rect (r, 5, 5, 18, 6);

    // resize turns into move and back
    ck_assert (edit_window_handle_move_resize (win, CK_WindowMove));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_MOVE);
    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_RESIZE);

    edit_window_restore_size (win);
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_NONE);
    ck_assert (!WIDGET (win)->mouse.forced_capture);
    test_assert_rect (r, 5, 5, 10, 30);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A window keeps its place on the screen unless it is fullscreen, which follows the screen */
START_TEST (test_window_add)
{
    test_window_t *other;
    WRect r;

    other = g_new0 (test_window_t, 1);
    rect_init (&r, 16, 0, 7, 80);
    edit_window_init (&other->window, &r, &test_window_class);
    other->window.fullscreen = 0;
    edit_window_add (&owner, &other->window);

    ck_assert_ptr_eq (WIDGET (other)->owner, GROUP (&owner));
    ck_assert_int_eq (WIDGET (other)->pos_flags, WPOS_KEEP_DEFAULT);
    test_assert_rect (&WIDGET (other)->rect, 16, 0, 7, 80);

    group_remove_widget (WIDGET (other));
    g_free (other);

    group_remove_widget (WIDGET (test_win));
    edit_window_add (&owner, &test_win->window);
    ck_assert_int_eq (WIDGET (test_win)->pos_flags, WPOS_KEEP_ALL);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A hidden window stays as it was, and shown it comes on top and is selected */
START_TEST (test_window_hide_show)
{
    WEditWindow *win = &test_win->window;
    test_window_t *other;
    WRect r;

    edit_window_toggle_fullscreen (win);
    ck_assert (edit_window_handle_move_resize (win, CK_WindowMove));

    other = g_new0 (test_window_t, 1);
    rect_init (&r, 1, 0, 22, 80);
    edit_window_init (&other->window, &r, &test_window_class);
    edit_window_add (&owner, &other->window);

    edit_window_hide (win);
    ck_assert (!widget_get_state (WIDGET (win), WST_VISIBLE));
    ck_assert_int_eq (win->drag_state, EDIT_WINDOW_DRAG_NONE);
    ck_assert (!WIDGET (win)->mouse.forced_capture);
    ck_assert (!widget_is_focusable (WIDGET (win)));

    edit_window_show (win);
    ck_assert (widget_get_state (WIDGET (win), WST_VISIBLE));
    ck_assert_ptr_eq (owner.group.current->data, win);
    ck_assert_ptr_eq (g_list_last (owner.group.widgets)->data, win);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (&WIDGET (win)->rect, 5, 5, 10, 30);

    group_remove_widget (WIDGET (other));
    g_free (other);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The widgets of a window move and resize with it */
START_TEST (test_window_holds_widgets)
{
    WEditWindow *win = &test_win->window;
    Widget *child;
    WRect r;
    int i;

    edit_window_toggle_fullscreen (win);

    // placed in the window, one cell in from its corner
    child = g_new0 (Widget, 1);
    rect_init (&r, 1, 1, 8, 28);
    widget_init (child, &r, widget_default_callback, NULL);
    group_add_widget_autopos (GROUP (win), child, WPOS_KEEP_ALL, NULL);
    test_assert_rect (&child->rect, 6, 6, 8, 28);

    ck_assert (edit_window_handle_move_resize (win, CK_WindowMove));
    for (i = 0; i < 2; i++)
        ck_assert (edit_window_handle_move_resize (win, CK_Down));
    ck_assert (edit_window_handle_move_resize (win, CK_Right));
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    test_assert_rect (&WIDGET (win)->rect, 7, 6, 10, 30);
    test_assert_rect (&child->rect, 8, 7, 8, 28);

    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    ck_assert (edit_window_handle_move_resize (win, CK_Right));
    ck_assert (edit_window_handle_move_resize (win, CK_Up));
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    test_assert_rect (&WIDGET (win)->rect, 7, 6, 9, 31);
    test_assert_rect (&child->rect, 8, 7, 7, 29);

    edit_window_toggle_fullscreen (win);
    test_assert_rect (&child->rect, 2, 1, 20, 78);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A fullscreen window makes room above a window at the bottom, and takes the screen back; one the
   user has moved since is left where it is */
START_TEST (test_window_make_room)
{
    WEditWindow *win = &test_win->window;
    test_window_t *bottom;
    WRect r;

    // fullscreen, and at 5,5 when it is not
    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 80);

    bottom = g_new0 (test_window_t, 1);
    rect_init (&r, 17, 0, 6, 80);
    edit_window_init (&bottom->window, &r, &test_window_class);
    bottom->window.fullscreen = 0;
    edit_window_add (&owner, &bottom->window);

    edit_window_make_room (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 16, 80);
    ck_assert_int_eq (WIDGET (win)->pos_flags, WPOS_KEEP_DEFAULT);

    edit_window_give_room_back (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 1);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 80);
    test_assert_rect (&win->loc_prev, 5, 5, 10, 30);
    ck_assert_int_eq (WIDGET (win)->pos_flags, WPOS_KEEP_ALL);

    // moved by the user in between: it stays where the user put it
    edit_window_make_room (&bottom->window);
    ck_assert (edit_window_handle_move_resize (win, CK_WindowMove));
    ck_assert (edit_window_handle_move_resize (win, CK_Right));
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    edit_window_give_room_back (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (&WIDGET (win)->rect, 1, 1, 16, 80);

    group_remove_widget (WIDGET (bottom));
    g_free (bottom);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A window of the screen not fullscreen, at y, x, of lines and cols; the caller frees it */
static test_window_t *
sticky_window_new (int y, int x, int lines, int cols)
{
    test_window_t *t = g_new0 (test_window_t, 1);
    WRect r;

    rect_init (&r, y, x, lines, cols);
    edit_window_init (&t->window, &r, &test_window_class);
    t->window.fullscreen = 0;
    edit_window_add (&owner, &t->window);
    return t;
}

/* --------------------------------------------------------------------------------------------- */

static void
sticky_window_free (test_window_t *t)
{
    group_remove_widget (WIDGET (t));
    send_message (t, NULL, MSG_DESTROY, 0, NULL);
    g_free (t);
}

/* --------------------------------------------------------------------------------------------- */

/* Resize win by keys: n steps of the command */
static void
sticky_keys (WEditWindow *win, long command, int n)
{
    int i;

    for (i = 0; i < n; i++)
        ck_assert (edit_window_handle_move_resize (win, command));
}

/* --------------------------------------------------------------------------------------------- */

/* Two windows side by side: the edge between them moves as one, as far as the smallest size */
START_TEST (test_window_sticky_side_by_side)
{
    test_window_t *a = sticky_window_new (1, 0, 22, 40);
    test_window_t *b = sticky_window_new (1, 40, 22, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    // the neighbor that resizes along is drawn as dragged
    ck_assert_int_eq (b->window.dragged_along, 1);

    sticky_keys (&a->window, CK_Right, 5);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 45);
    test_assert_rect (&WIDGET (b)->rect, 1, 45, 22, 35);
    sticky_keys (&a->window, CK_Left, 3);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 42);
    test_assert_rect (&WIDGET (b)->rect, 1, 42, 22, 38);

    // as far as the smallest size of the neighbor
    sticky_keys (&a->window, CK_Right, 100);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 74);
    test_assert_rect (&WIDGET (b)->rect, 1, 74, 22, 6);

    // the bottom on the bottom of the screen does not move
    sticky_keys (&a->window, CK_Up, 3);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 74);

    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    ck_assert_int_eq (b->window.dragged_along, 0);

    // without sticky windows a window resizes alone
    edit_options.sticky_windows = FALSE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Left, 4);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 70);
    test_assert_rect (&WIDGET (b)->rect, 1, 74, 22, 6);

    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Four windows: the corner where they meet moves both edges, and all four resize */
START_TEST (test_window_sticky_four)
{
    test_window_t *a = sticky_window_new (1, 0, 11, 40);
    test_window_t *b = sticky_window_new (1, 40, 11, 40);
    test_window_t *c = sticky_window_new (12, 0, 11, 40);
    test_window_t *d = sticky_window_new (12, 40, 11, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 2);
    sticky_keys (&a->window, CK_Down, 3);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));

    test_assert_rect (&WIDGET (a)->rect, 1, 0, 14, 42);
    test_assert_rect (&WIDGET (b)->rect, 1, 42, 14, 38);
    test_assert_rect (&WIDGET (c)->rect, 15, 0, 8, 42);
    test_assert_rect (&WIDGET (d)->rect, 15, 42, 8, 38);

    // the corner of B on the right of the screen moves the row between them alone
    ck_assert (edit_window_handle_move_resize (&b->window, CK_WindowResize));
    sticky_keys (&b->window, CK_Right, 2);
    sticky_keys (&b->window, CK_Up, 1);
    ck_assert (edit_window_handle_move_resize (&b->window, CK_Enter));
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 13, 42);
    test_assert_rect (&WIDGET (b)->rect, 1, 42, 13, 38);
    test_assert_rect (&WIDGET (c)->rect, 14, 0, 9, 42);
    test_assert_rect (&WIDGET (d)->rect, 14, 42, 9, 38);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (d);
    sticky_window_free (c);
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Two windows above one: the column between the two leaves the one below as it is */
START_TEST (test_window_sticky_two_over_one)
{
    test_window_t *a = sticky_window_new (1, 0, 11, 40);
    test_window_t *b = sticky_window_new (1, 40, 11, 40);
    test_window_t *c = sticky_window_new (12, 0, 11, 80);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 1);
    sticky_keys (&a->window, CK_Down, 2);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));

    test_assert_rect (&WIDGET (a)->rect, 1, 0, 13, 41);
    test_assert_rect (&WIDGET (b)->rect, 1, 41, 13, 39);
    test_assert_rect (&WIDGET (c)->rect, 14, 0, 9, 80);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (c);
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The room made for a window keeps being its room though the edge between them has moved */
START_TEST (test_window_sticky_keeps_the_room)
{
    WEditWindow *win = &test_win->window;
    test_window_t *bottom;

    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);
    bottom = sticky_window_new (17, 0, 6, 80);
    edit_window_make_room (&bottom->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 16, 80);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    sticky_keys (win, CK_Down, 1);
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 17, 80);
    test_assert_rect (&WIDGET (bottom)->rect, 18, 0, 5, 80);

    edit_window_give_room_back (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 1);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (bottom);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A file and the Preview side by side, then the terminal below: both make room for it, and both
   go back when it goes */
START_TEST (test_window_make_room_from_two)
{
    WEditWindow *win = &test_win->window;
    test_window_t *right = sticky_window_new (1, 50, 22, 30);
    test_window_t *bottom;

    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);
    edit_window_make_room (&right->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 50);

    bottom = sticky_window_new (17, 0, 6, 80);
    edit_window_make_room (&bottom->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 16, 50);
    test_assert_rect (&WIDGET (right)->rect, 1, 50, 16, 30);

    edit_window_give_room_back (&bottom->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 50);
    test_assert_rect (&WIDGET (right)->rect, 1, 50, 22, 30);

    edit_window_give_room_back (&right->window);
    ck_assert_int_eq (win->fullscreen, 1);

    sticky_window_free (bottom);
    sticky_window_free (right);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A tree on the left edge: the file moves to the right of it, the Preview on the right keeps its
   place, and both go back when the tree goes */
START_TEST (test_window_make_room_on_the_right)
{
    WEditWindow *win = &test_win->window;
    test_window_t *right = sticky_window_new (1, 50, 22, 30);
    test_window_t *tree;

    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);
    edit_window_make_room (&right->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 50);

    tree = sticky_window_new (1, 0, 22, 20);
    edit_window_make_room (&tree->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 20, 22, 30);
    test_assert_rect (&WIDGET (right)->rect, 1, 50, 22, 30);

    edit_window_give_room_back (&tree->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 50);

    edit_window_give_room_back (&right->window);
    ck_assert_int_eq (win->fullscreen, 1);

    // alone with the file: the file takes all the rest
    edit_window_make_room (&tree->window);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (&WIDGET (win)->rect, 1, 20, 22, 60);
    edit_window_give_room_back (&tree->window);
    ck_assert_int_eq (win->fullscreen, 1);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 80);

    sticky_window_free (tree);
    sticky_window_free (right);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Esc puts the neighbors back too, and draws them as before */
START_TEST (test_window_sticky_cancel)
{
    test_window_t *a = sticky_window_new (1, 0, 22, 40);
    test_window_t *b = sticky_window_new (1, 40, 22, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 10);
    test_assert_rect (&WIDGET (b)->rect, 1, 50, 22, 30);
    edit_window_restore_size (&a->window);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 40);
    test_assert_rect (&WIDGET (b)->rect, 1, 40, 22, 40);
    ck_assert_int_eq (b->window.dragged_along, 0);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A side grown to the edge of the screen in a resize comes back in it; a side on the edge when it
   began stays; a window partly off the screen shrinks */
START_TEST (test_window_sticky_screen_edge)
{
    test_window_t *a = sticky_window_new (1, 0, 10, 60);
    test_window_t *off = sticky_window_new (12, 50, 8, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 25);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 10, 80);
    sticky_keys (&a->window, CK_Left, 5);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 10, 75);
    sticky_keys (&a->window, CK_Right, 5);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));

    // on the edge when the resize began: it stays
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 5);
    sticky_keys (&a->window, CK_Left, 3);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 10, 80);

    ck_assert (edit_window_handle_move_resize (&off->window, CK_WindowResize));
    sticky_keys (&off->window, CK_Left, 5);
    ck_assert (edit_window_handle_move_resize (&off->window, CK_Enter));
    test_assert_rect (&WIDGET (off)->rect, 12, 50, 8, 35);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (off);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A window in the way, no part of the edge, is not run over: the edge stops at it, and from there
   on it stands next to the edge, a neighbor, and moves along.  One that meets the window only at
   a corner is no part of the edge */
START_TEST (test_window_sticky_in_the_way)
{
    test_window_t *a = sticky_window_new (1, 0, 11, 40);
    test_window_t *b = sticky_window_new (1, 40, 5, 40);
    test_window_t *e = sticky_window_new (6, 45, 6, 35);
    test_window_t *corner = sticky_window_new (12, 40, 11, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 10);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 11, 50);
    test_assert_rect (&WIDGET (b)->rect, 1, 50, 5, 30);
    test_assert_rect (&WIDGET (e)->rect, 6, 50, 6, 30);
    test_assert_rect (&WIDGET (corner)->rect, 12, 40, 11, 40);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (corner);
    sticky_window_free (e);
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The neighbors are drawn as before when the window is hidden, or the mode turned off, in a
   resize */
START_TEST (test_window_sticky_unmark)
{
    test_window_t *a = sticky_window_new (1, 0, 22, 40);
    test_window_t *b = sticky_window_new (1, 40, 22, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    ck_assert_int_eq (b->window.dragged_along, 1);
    edit_options.sticky_windows = FALSE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    ck_assert_int_eq (b->window.dragged_along, 0);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    ck_assert_int_eq (b->window.dragged_along, 1);
    edit_window_hide (&a->window);
    ck_assert_int_eq (b->window.dragged_along, 0);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The rooms are given back edge by edge: in any order, a column moved between keeps, and a side
   the user moved by itself is left as it is */
START_TEST (test_window_room_by_edge)
{
    WEditWindow *win = &test_win->window;
    test_window_t *right = sticky_window_new (1, 50, 22, 30);
    test_window_t *bottom;

    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);
    edit_window_make_room (&right->window);
    bottom = sticky_window_new (17, 0, 6, 80);
    edit_window_make_room (&bottom->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 16, 50);

    // the column between the file and the right one moved: it stays when the bottom goes
    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    sticky_keys (win, CK_Right, 5);
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    test_assert_rect (&WIDGET (right)->rect, 1, 55, 16, 25);

    // given back the other way round: the right one first, the bottom last
    edit_window_hide (&right->window);
    edit_window_give_room_back (&right->window);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 16, 80);
    edit_window_give_room_back (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 1);

    // a side of its own moved by the user: the window stays as the user left it
    edit_window_toggle_fullscreen (win);
    test_assert_rect (&WIDGET (win)->rect, 5, 5, 10, 30);
    edit_window_make_room (&bottom->window);
    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    sticky_keys (win, CK_Right, 3);
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    edit_window_give_room_back (&bottom->window);
    test_assert_rect (&WIDGET (win)->rect, 5, 5, 10, 33);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (bottom);
    sticky_window_free (right);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* No room for the fullscreen window: nothing moves */
START_TEST (test_window_room_none)
{
    WEditWindow *win = &test_win->window;
    test_window_t *floating = sticky_window_new (5, 50, 10, 20);
    test_window_t *bottom = sticky_window_new (3, 0, 20, 80);

    edit_window_make_room (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 1);
    test_assert_rect (&WIDGET (floating)->rect, 5, 50, 10, 20);
    ck_assert (bottom->window.rooms == NULL);

    sticky_window_free (bottom);
    sticky_window_free (floating);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The screen resized: the windows at its right and bottom stay there, the ones before them make
   way down to their smallest size, and the screen grown back gets them back as they were */
START_TEST (test_window_fit_area)
{
    test_window_t *a = sticky_window_new (1, 0, 16, 60);
    test_window_t *term = sticky_window_new (17, 0, 6, 60);
    test_window_t *pv = sticky_window_new (1, 60, 22, 20);
    WRect old;

    edit_window_area (&owner, &old);
    rect_init (&WIDGET (&owner)->rect, 0, 0, 12, 40);
    edit_window_fit_area (&owner, &old);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 6, 34);
    test_assert_rect (&WIDGET (term)->rect, 7, 0, 4, 34);
    test_assert_rect (&WIDGET (pv)->rect, 1, 34, 10, 6);

    edit_window_area (&owner, &old);
    rect_init (&WIDGET (&owner)->rect, 0, 0, 24, 80);
    edit_window_fit_area (&owner, &old);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 16, 60);
    test_assert_rect (&WIDGET (term)->rect, 17, 0, 6, 60);
    test_assert_rect (&WIDGET (pv)->rect, 1, 60, 22, 20);

    // moved in between: the next resize starts from where it is
    edit_window_area (&owner, &old);
    rect_init (&WIDGET (&owner)->rect, 0, 0, 12, 40);
    edit_window_fit_area (&owner, &old);
    ck_assert (edit_window_handle_move_resize (&pv->window, CK_WindowResize));
    sticky_keys (&pv->window, CK_Up, 1);
    ck_assert (edit_window_handle_move_resize (&pv->window, CK_Enter));
    edit_window_area (&owner, &old);
    rect_init (&WIDGET (&owner)->rect, 0, 0, 24, 80);
    edit_window_fit_area (&owner, &old);
    test_assert_rect (&WIDGET (pv)->rect, 1, 34, 9, 46);

    sticky_window_free (pv);
    sticky_window_free (term);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Esc, or a move or resize that changed nothing, is no move by the user: the room is given back */
START_TEST (test_window_drag_no_change)
{
    WEditWindow *win = &test_win->window;
    test_window_t *bottom = sticky_window_new (17, 0, 6, 80);
    const unsigned int moves = win->user_moves;

    edit_window_make_room (&bottom->window);
    ck_assert (edit_window_handle_move_resize (win, CK_WindowResize));
    sticky_keys (win, CK_Left, 3);
    edit_window_restore_size (win);
    ck_assert (edit_window_handle_move_resize (win, CK_WindowMove));
    sticky_keys (win, CK_Up, 2);
    ck_assert (edit_window_handle_move_resize (win, CK_Enter));
    ck_assert_uint_eq (win->user_moves, moves);

    edit_window_give_room_back (&bottom->window);
    ck_assert_int_eq (win->fullscreen, 1);

    sticky_window_free (bottom);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Esc puts the neighbors back however the resize went on: the mode turned on in it, or the resize
   turned into a move */
START_TEST (test_window_drag_cancel_any)
{
    test_window_t *a = sticky_window_new (1, 0, 22, 40);
    test_window_t *b = sticky_window_new (1, 40, 22, 40);

    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    edit_options.sticky_windows = TRUE;
    sticky_keys (&a->window, CK_Right, 4);
    test_assert_rect (&WIDGET (b)->rect, 1, 44, 22, 36);
    edit_window_restore_size (&a->window);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 40);
    test_assert_rect (&WIDGET (b)->rect, 1, 40, 22, 40);
    ck_assert_int_eq (b->window.dragged_along, 0);

    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 5);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowMove));
    ck_assert_int_eq (b->window.dragged_along, 0);
    edit_window_restore_size (&a->window);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 40);
    test_assert_rect (&WIDGET (b)->rect, 1, 40, 22, 40);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* An edge with no neighbor after it is the window's alone: one in line below stays */
START_TEST (test_window_sticky_alone)
{
    test_window_t *a = sticky_window_new (1, 0, 11, 40);
    test_window_t *b = sticky_window_new (12, 0, 11, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 3);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 11, 43);
    test_assert_rect (&WIDGET (b)->rect, 12, 0, 11, 40);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The window resized goes, or the screen is resized: the resize ends, the neighbors drawn as
   before */
START_TEST (test_window_drag_ends)
{
    test_window_t *a = sticky_window_new (1, 0, 22, 40);
    test_window_t *b = sticky_window_new (1, 40, 22, 40);
    WRect old;

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&b->window, CK_WindowResize));
    edit_window_area (&owner, &old);
    rect_init (&WIDGET (&owner)->rect, 0, 0, 20, 70);
    edit_window_fit_area (&owner, &old);
    ck_assert_int_eq (b->window.drag_state, EDIT_WINDOW_DRAG_NONE);
    ck_assert_int_eq (b->window.drag_open, 0);
    rect_init (&WIDGET (&owner)->rect, 0, 0, 24, 80);

    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    ck_assert_int_eq (b->window.dragged_along, 1);
    edit_window_destroy (&a->window);
    ck_assert_int_eq (b->window.dragged_along, 0);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (b);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* An edge of its own moved and moved back is no move by the user; the shared edge moved keeps the
   room */
START_TEST (test_window_drag_own_back)
{
    test_window_t *a = sticky_window_new (1, 0, 20, 40);
    test_window_t *p = sticky_window_new (17, 0, 6, 80);

    edit_window_make_room (&p->window);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 16, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 1);
    sticky_keys (&a->window, CK_Left, 1);
    sticky_keys (&a->window, CK_Down, 1);
    ck_assert (edit_window_handle_move_resize (&a->window, CK_Enter));
    ck_assert_uint_eq (a->window.user_moves, 0);

    edit_window_give_room_back (&p->window);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 20, 40);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (p);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Fullscreen in a sticky resize ends it where it is: back from it, the windows do not overlap */
START_TEST (test_window_drag_fullscreen)
{
    test_window_t *a = sticky_window_new (1, 0, 22, 40);
    test_window_t *b = sticky_window_new (1, 40, 22, 40);

    edit_options.sticky_windows = TRUE;
    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowResize));
    sticky_keys (&a->window, CK_Right, 4);
    edit_window_toggle_fullscreen (&a->window);
    ck_assert_int_eq (b->window.dragged_along, 0);
    edit_window_toggle_fullscreen (&a->window);
    test_assert_rect (&WIDGET (a)->rect, 1, 0, 22, 44);
    test_assert_rect (&WIDGET (b)->rect, 1, 44, 22, 36);

    edit_options.sticky_windows = FALSE;
    sticky_window_free (b);
    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The button bar in a move of a window that is no file: Quit gives the move up, the other commands
   wait */
START_TEST (test_window_drag_buttonbar)
{
    test_window_t *a = sticky_window_new (5, 5, 10, 30);

    ck_assert (edit_window_handle_move_resize (&a->window, CK_WindowMove));
    sticky_keys (&a->window, CK_Right, 3);
    ck_assert_int_eq (send_message (a, NULL, MSG_ACTION, CK_Copy, NULL), MSG_HANDLED);
    ck_assert_int_eq (a->window.drag_state, EDIT_WINDOW_DRAG_MOVE);
    ck_assert_int_eq (send_message (a, NULL, MSG_ACTION, CK_Quit, NULL), MSG_HANDLED);
    ck_assert_int_eq (a->window.drag_state, EDIT_WINDOW_DRAG_NONE);
    test_assert_rect (&WIDGET (a)->rect, 5, 5, 10, 30);

    sticky_window_free (a);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A window of all the height at the right edge: the fullscreen window goes to its left */
START_TEST (test_window_make_room_left)
{
    WEditWindow *win = &test_win->window;
    test_window_t *right;
    WRect r;

    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);

    right = g_new0 (test_window_t, 1);
    rect_init (&r, 1, 40, 22, 40);
    edit_window_init (&right->window, &r, &test_window_class);
    right->window.fullscreen = 0;
    edit_window_add (&owner, &right->window);

    edit_window_make_room (&right->window);
    ck_assert_int_eq (win->fullscreen, 0);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 40);

    edit_window_give_room_back (&right->window);
    ck_assert_int_eq (win->fullscreen, 1);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 80);

    group_remove_widget (WIDGET (right));
    g_free (right);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A window destroyed gives the focus to the window left on top, not to the widget after it */
START_TEST (test_window_destroy_selects_top)
{
    test_window_t *other;
    Widget *bg;
    WRect r;

    // a widget that is not a window before the windows, as the background of the editor is
    bg = g_new0 (Widget, 1);
    rect_init (&r, 0, 0, 24, 80);
    widget_init (bg, &r, widget_default_callback, NULL);
    group_add_widget_autopos (GROUP (&owner), bg, WPOS_KEEP_DEFAULT, owner.group.widgets->data);

    other = g_new0 (test_window_t, 1);
    rect_init (&r, 17, 0, 6, 80);
    edit_window_init (&other->window, &r, &test_window_class);
    edit_window_add (&owner, &other->window);
    widget_select (WIDGET (other));
    ck_assert_ptr_eq (owner.group.current->data, other);

    edit_window_destroy (&other->window);
    ck_assert_ptr_eq (owner.group.current->data, test_win);

    group_remove_widget (bg);
    g_free (bg);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A file window is a window of the editor class */
START_TEST (test_editor_is_window)
{
    WEdit *edit;
    WRect r;

    rect_init (&r, 1, 0, 22, 80);
    edit = edit_init (NULL, &r, NULL);
    ck_assert_ptr_nonnull (edit);
    ck_assert (edit_window_is_window (CONST_WIDGET (edit)));
    ck_assert (edit_widget_is_editor (CONST_WIDGET (edit)));
    ck_assert_ptr_eq (CONST_EDIT_WINDOW (edit)->klass, &edit_class);
    edit_clean (edit);
    g_free (edit);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* The docks: the column at the right, the tabs at the bottom, the window of the file that was
   fullscreen in the rest, and fullscreen again when they are empty */
START_TEST (test_window_docks)
{
    WEditWindow *win = &test_win->window;
    test_window_t *right, *b1, *b2;

    edit_window_toggle_fullscreen (win);
    edit_window_toggle_fullscreen (win);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 80);

    // a column of 24 of the 80 columns
    right = sticky_window_new (1, 60, 22, 20);
    edit_dock_add (&right->window, EDIT_DOCK_RIGHT, 24);
    test_assert_rect (&WIDGET (right)->rect, 1, 56, 22, 24);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 56);
    ck_assert_int_eq (win->fullscreen, 0);
    ck_assert_int_eq (win->dock_fill, 1);

    // two tabs at the bottom, under the file: the last one seen
    b1 = sticky_window_new (17, 0, 6, 80);
    edit_dock_add (&b1->window, EDIT_DOCK_BOTTOM, 6);
    b2 = sticky_window_new (17, 0, 6, 80);
    edit_dock_add (&b2->window, EDIT_DOCK_BOTTOM, 6);
    test_assert_rect (&WIDGET (b1)->rect, 18, 0, 5, 56);
    test_assert_rect (&WIDGET (b2)->rect, 18, 0, 5, 56);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 17, 56);
    ck_assert (widget_get_state (WIDGET (b2), WST_VISIBLE));
    ck_assert (!widget_get_state (WIDGET (b1), WST_VISIBLE));

    // the next tab, round
    edit_dock_tab_step (&owner, 1);
    ck_assert (widget_get_state (WIDGET (b1), WST_VISIBLE));
    ck_assert (!widget_get_state (WIDGET (b2), WST_VISIBLE));
    ck_assert (edit_dock_tab () == &b1->window);

    // the tabs go one by one, then the column: the file has the screen again
    ck_assert (edit_dock_remove (&b1->window));
    ck_assert (widget_get_state (WIDGET (b2), WST_VISIBLE));
    ck_assert (edit_dock_remove (&b2->window));
    ck_assert (!edit_dock_remove (&b2->window));
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 56);
    ck_assert (edit_dock_remove (&right->window));
    ck_assert_int_eq (win->fullscreen, 1);
    ck_assert_int_eq (win->dock_fill, 0);
    test_assert_rect (&WIDGET (win)->rect, 1, 0, 22, 80);

    sticky_window_free (b2);
    sticky_window_free (b1);
    sticky_window_free (right);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_window_init);
    tcase_add_test (tc_core, test_window_area);
    tcase_add_test (tc_core, test_window_toggle_fullscreen);
    tcase_add_test (tc_core, test_window_fullscreen_does_not_move);
    tcase_add_test (tc_core, test_window_move);
    tcase_add_test (tc_core, test_window_resize);
    tcase_add_test (tc_core, test_window_add);
    tcase_add_test (tc_core, test_window_hide_show);
    tcase_add_test (tc_core, test_window_holds_widgets);
    tcase_add_test (tc_core, test_window_make_room);
    tcase_add_test (tc_core, test_window_make_room_left);
    tcase_add_test (tc_core, test_window_sticky_side_by_side);
    tcase_add_test (tc_core, test_window_sticky_four);
    tcase_add_test (tc_core, test_window_sticky_two_over_one);
    tcase_add_test (tc_core, test_window_sticky_keeps_the_room);
    tcase_add_test (tc_core, test_window_make_room_from_two);
    tcase_add_test (tc_core, test_window_make_room_on_the_right);
    tcase_add_test (tc_core, test_window_sticky_cancel);
    tcase_add_test (tc_core, test_window_sticky_screen_edge);
    tcase_add_test (tc_core, test_window_sticky_in_the_way);
    tcase_add_test (tc_core, test_window_sticky_unmark);
    tcase_add_test (tc_core, test_window_room_by_edge);
    tcase_add_test (tc_core, test_window_room_none);
    tcase_add_test (tc_core, test_window_fit_area);
    tcase_add_test (tc_core, test_window_drag_no_change);
    tcase_add_test (tc_core, test_window_drag_cancel_any);
    tcase_add_test (tc_core, test_window_sticky_alone);
    tcase_add_test (tc_core, test_window_drag_ends);
    tcase_add_test (tc_core, test_window_drag_own_back);
    tcase_add_test (tc_core, test_window_drag_fullscreen);
    tcase_add_test (tc_core, test_window_drag_buttonbar);
    tcase_add_test (tc_core, test_window_destroy_selects_top);
    tcase_add_test (tc_core, test_editor_is_window);
    tcase_add_test (tc_core, test_window_docks);

    return mctest_run_all (tc_core);
}
