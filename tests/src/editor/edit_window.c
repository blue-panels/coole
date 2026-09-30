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
    return widget_default_callback (w, sender, msg, parm, data);
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
    tcase_add_test (tc_core, test_editor_is_window);

    return mctest_run_all (tc_core);
}
