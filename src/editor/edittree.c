/*
   Editor: a window that shows a tree, an mctree of its owner.

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/** \file edittree.c
 *  \brief Source: a window of the editor that shows a tree, an mctree of its owner
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"

#include "src/editor/edittree.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The lines of the rows, in the frame */
static int
edit_tree_body_lines (const edit_tree_t *tree)
{
    return MAX (1, WIDGET (tree)->rect.lines - 2);
}

/* --------------------------------------------------------------------------------------------- */

/* The column of the mark of the row of @depth in the window: a column of room, then two for each
   level */
static int
edit_tree_mark_col (int depth)
{
    return 2 + depth * 2;
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_tree_draw (edit_tree_t *tree)
{
    WEditWindow *win = &tree->window;
    Widget *w = WIDGET (tree);
    const gboolean focused = widget_get_state (w, WST_FOCUSED);
    const int color = edit_window_frame_color (win, focused);
    const int lines = edit_tree_body_lines (tree);
    const int width = MAX (0, w->rect.cols - 2);
    const guint count = mctree_view_row_count (tree->view);
    char **message = tree->message != NULL ? g_strsplit (tree->message, "\n", -1) : NULL;
    int row;

    // the rows a page has, when they change alone: the wheel and the scrollbar move the rows
    // away from the cursor, and a draw does not bring them back
    if (tree->view->page_rows != lines)
        mctree_view_set_page_rows (tree->view, lines);
    edit_window_draw_frame (win, color, focused);
    tty_setcolor (color);
    widget_gotoyx (w, 0, 2);
    tty_printf ("[%s]", str_term_trim (tree->title, MAX (0, w->rect.cols - 10)));
    edit_window_draw_icons (win, color);

    for (row = 0; row < lines; row++)
    {
        const int index = tree->view->top + row;

        tty_setcolor (EDITOR_NORMAL_COLOR);
        tty_draw_hline (w->rect.y + row + 1, w->rect.x + 1, ' ', width);
        widget_gotoyx (w, row + 1, 1);
        if (message != NULL)
        {
            // the message in place of the rows, a line of it each
            if ((guint) row < g_strv_length (message))
                tty_print_string (str_fit_to_term (message[row], width, J_LEFT));
        }
        else if (index < (int) count)
        {
            const mctree_visible_row_t *r =
                &g_array_index (tree->view->rows, mctree_visible_row_t, index);
            const mctree_node_t *node = r->node;
            const int lead = edit_tree_mark_col (r->depth) - 1;
            GString *mark = g_string_new (NULL);
            GString *name = g_string_new (NULL);
            GString *info = g_string_new (NULL);
            int room, info_cols, mark_cols, row_color, name_color = -1, info_color = -1;

            // a node that does not open has room of a mark, as in the tree of the project
            if (mctree_node_expandable (node))
                g_string_append_printf (mark, "%s ",
                                        node->expanded ? tree->glyph_open : tree->glyph_closed);
            else
                g_string_append (mark, "  ");
            if (tree->ops->row != NULL)
                tree->ops->row (tree, node, name, info);
            else
            {
                g_string_append (name, node->key != NULL ? node->key : "");
                if (node->value != NULL)
                    g_string_append (info, node->value);
            }
            // opened, its children are read
            if (node->expanded && node->lazy)
                g_string_append (name, " ...");
            if (index == tree->view->cursor && focused)
            {
                row_color = EDITOR_MARKED_COLOR;
                tty_setcolor (row_color);
                tty_draw_hline (w->rect.y + row + 1, w->rect.x + 1, ' ', width);
                if (tree->ops->cursor_colors != NULL)
                    tree->ops->cursor_colors (tree, node, &name_color, &info_color);
            }
            else
            {
                row_color = tree->ops->bold != NULL && tree->ops->bold (tree, node)
                    ? EDITOR_BOLD_COLOR
                    : EDITOR_NORMAL_COLOR;
                if (tree->ops->colors != NULL)
                    tree->ops->colors (tree, node, &name_color, &info_color);
            }
            widget_gotoyx (w, row + 1, 1 + lead);
            // the name at the left, what there is to know of it at the right, when both fit
            // a column of room at the right too
            room = width - lead - 1;
            mark_cols = MIN (str_term_width1 (mark->str), MAX (0, room));
            room -= mark_cols;
            info_cols = info->len > 0 ? str_term_width1 (info->str) : 0;
            if (info_cols > 0 && info_cols + 1 + str_term_width1 (name->str) > room)
                info_cols = MIN (info_cols, MAX (0, room - str_term_width1 (name->str) - 1));
            tty_setcolor (row_color);
            tty_print_string (str_fit_to_term (mark->str, mark_cols, J_LEFT));
            if (room > 0)
            {
                tty_setcolor (name_color >= 0 ? name_color : row_color);
                tty_print_string (str_fit_to_term (
                    name->str, room - (info_cols > 0 ? info_cols + 1 : 0), J_LEFT));
                if (info_cols > 0)
                {
                    tty_setcolor (info_color >= 0 ? info_color : row_color);
                    widget_gotoyx (w, row + 1, width - info_cols);
                    tty_print_string (str_fit_to_term (info->str, info_cols, J_LEFT));
                }
            }
            g_string_free (mark, TRUE);
            g_string_free (name, TRUE);
            g_string_free (info, TRUE);
        }
    }
    g_strfreev (message);

    // the scrollbar on the frame: where the rows are, of how many
    if (win->fullscreen == 0)
    {
        edit_window_set_scroll (win, TRUE, (long) count, lines, tree->view->top);
        edit_window_draw_bars (win, color);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Enter on the node of the cursor: the owner's, else it opens or closes */
static void
edit_tree_enter (edit_tree_t *tree)
{
    mctree_node_t *node = mctree_view_current_node (tree->view);

    if (node == NULL)
        return;
    if (tree->ops->activate != NULL && tree->ops->activate (tree, node))
        return;
    if (mctree_node_expandable (node))
        (void) mctree_view_toggle_current (tree->view);
}

/* --------------------------------------------------------------------------------------------- */

/* A letter: the cursor on the next node whose name starts with it */
static gboolean
edit_tree_letter (edit_tree_t *tree, int key)
{
    const int count = (int) mctree_view_row_count (tree->view);
    int i;

    if (key <= ' ' || key >= 128 || count == 0)
        return FALSE;
    for (i = 1; i <= count; i++)
    {
        const int at = (tree->view->cursor + i) % count;
        const mctree_node_t *node = g_array_index (tree->view->rows, mctree_visible_row_t, at).node;

        if (node->key != NULL && g_ascii_tolower (node->key[0]) == g_ascii_tolower (key))
        {
            mctree_view_move (tree->view, at - tree->view->cursor);
            break;
        }
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_tree_key (edit_tree_t *tree, int key)
{
    mctree_view_t *view = tree->view;
    mctree_node_t *node = mctree_view_current_node (view);

    switch (key)
    {
    case KEY_UP:
        mctree_view_move (view, -1);
        break;
    case KEY_DOWN:
        mctree_view_move (view, 1);
        break;
    case KEY_PPAGE:
        mctree_view_page_up (view);
        break;
    case KEY_NPAGE:
        mctree_view_page_down (view);
        break;
    case KEY_HOME:
        mctree_view_home (view);
        break;
    case KEY_END:
        mctree_view_end (view);
        break;
    case KEY_RIGHT:
        (void) mctree_view_expand_current (view);
        break;
    case KEY_LEFT:
        (void) mctree_view_collapse_current (view);
        break;
    case '\n':
    case KEY_ENTER:
        edit_tree_enter (tree);
        break;
    default:
        if (tree->ops->key != NULL && tree->ops->key (tree, node, key))
            return TRUE;
        if (!edit_tree_letter (tree, key))
            return FALSE;
        break;
    }
    widget_draw (WIDGET (tree));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_tree_buttonbar (edit_tree_t *tree)
{
    WButtonBar *bb = buttonbar_find (DIALOG (WIDGET (tree)->owner));
    int i;

    if (bb == NULL)
        return;
    for (i = 2; i <= 10; i++)
        buttonbar_clear_label (bb, i, NULL);
    buttonbar_set_label_command (bb, 1, _ ("Help"), CK_Help, NULL);
    if (tree->ops->buttonbar != NULL)
        tree->ops->buttonbar (tree, bb);
    buttonbar_set_label_command (bb, 10, _ ("Close"), CK_Close, WIDGET (tree));
    widget_draw (WIDGET (bb));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_tree_close (WEditWindow *win)
{
    WDialog *dialog = DIALOG (WIDGET (win)->owner);

    edit_window_give_room_back (win);
    edit_window_destroy (win);
    widget_draw (WIDGET (dialog));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
edit_tree_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    edit_tree_t *tree = (edit_tree_t *) w;

    switch (msg)
    {
    case MSG_DRAW:
        edit_tree_draw (tree);
        return MSG_HANDLED;
    case MSG_FOCUS:
        edit_tree_buttonbar (tree);
        edit_tree_draw (tree);
        return MSG_HANDLED;
    case MSG_UNFOCUS:
        edit_tree_draw (tree);
        return MSG_HANDLED;
    case MSG_KEY:
        if (parm == KEY_F (10) || parm == ESC_CHAR)
        {
            (void) edit_tree_close (&tree->window);
            return MSG_HANDLED;
        }
        return edit_tree_key (tree, parm) ? MSG_HANDLED : MSG_NOT_HANDLED;
    case MSG_ACTION:
        if (parm == CK_Close)
        {
            (void) edit_tree_close (&tree->window);
            return MSG_HANDLED;
        }
        if (tree->ops->action != NULL
            && tree->ops->action (tree, mctree_view_current_node (tree->view), parm))
        {
            widget_draw (w);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;
    case MSG_CURSOR:
        widget_gotoyx (w, 1 + tree->view->cursor - tree->view->top, 1);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (tree->ops->closed != NULL)
            tree->ops->closed (tree);
        mctree_view_free (tree->view);
        g_free (tree->glyph_closed);
        g_free (tree->glyph_open);
        g_free (tree->title);
        g_free (tree->message);
        return group_default_callback (w, sender, msg, parm, data);
    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_tree_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    edit_tree_t *tree = (edit_tree_t *) w;
    mctree_view_t *view = tree->view;
    const int index = view->top + event->y - 1;
    const gboolean on_row = index >= 0 && index < (int) mctree_view_row_count (view);

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        widget_select (w);
        if (on_row && tree->message == NULL)
        {
            const mctree_visible_row_t *r =
                &g_array_index (view->rows, mctree_visible_row_t, index);
            const int mark = edit_tree_mark_col (r->depth);
            const char *glyph = r->node->expanded ? tree->glyph_open : tree->glyph_closed;

            mctree_view_move (view, index - view->cursor);
            // a click on the mark of a node opens or closes it
            if (mctree_node_expandable (r->node) && event->x >= mark
                && event->x < mark + str_term_width1 (glyph))
                (void) mctree_view_toggle_current (view);
        }
        widget_draw (w);
        break;
    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE && on_row && index == view->cursor && tree->message == NULL)
        {
            edit_tree_enter (tree);
            widget_draw (w);
        }
        break;
    case MSG_MOUSE_SCROLL_UP:
        view->top = MAX (0, view->top - 3);
        widget_draw (w);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        view->top = MAX (
            0,
            MIN ((int) mctree_view_row_count (view) - edit_tree_body_lines (tree), view->top + 3));
        widget_draw (w);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
edit_tree_title (const WEditWindow *win)
{
    return g_strdup (((const edit_tree_t *) win)->title);
}

/* --------------------------------------------------------------------------------------------- */

/* The scrollbar of the frame has moved: the rows go with it, the cursor stays where it is */
static void
edit_tree_scrolled (WEditWindow *win, gboolean vertical, long pos)
{
    edit_tree_t *tree = (edit_tree_t *) win;

    if (!vertical)
        return;
    tree->view->top = (int) CLAMP (
        pos, 0, MAX (0, (int) mctree_view_row_count (tree->view) - edit_tree_body_lines (tree)));
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

static const edit_window_class_t edit_tree_class = {
    .callback = edit_tree_callback,
    .mouse_callback = edit_tree_mouse,
    .get_title = edit_tree_title,
    .close = edit_tree_close,
    .min_lines = 5,
    .min_cols = 16,
    .vbar = TRUE,
    .scrolled = edit_tree_scrolled,
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

edit_tree_t *
edit_tree_new (const char *title, const edit_tree_ops_t *ops, void *data)
{
    edit_tree_t *tree = g_new0 (edit_tree_t, 1);
    const WRect rect = { 0, 0, 10, 30 };

    edit_window_init (&tree->window, &rect, &edit_tree_class);
    tree->window.fullscreen = 0;
    tree->title = g_strdup (title);
    tree->ops = ops;
    tree->data = data;
    tree->view = mctree_view_new ();
    // from the skin; ASCII when it has none or the terminal cannot show its own
    tree->glyph_closed = mc_skin_get ("widget-editor", "tree-closed-char", "+");
    tree->glyph_open = mc_skin_get ("widget-editor", "tree-open-char", "-");
    return tree;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_tree_set_title (edit_tree_t *tree, const char *title)
{
    g_free (tree->title);
    tree->title = g_strdup (title);
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_tree_set_model (edit_tree_t *tree, mctree_model_t *model)
{
    mctree_view_set_model (tree->view, model);
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_tree_rebuild (edit_tree_t *tree)
{
    const mctree_node_t *node = mctree_view_current_node (tree->view);
    char *path = node != NULL ? mctree_node_path (node) : NULL;

    mctree_view_rebuild (tree->view);
    if (path != NULL)
        (void) mctree_view_goto_path (tree->view, path);
    g_free (path);
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_tree_set_message (edit_tree_t *tree, const char *message)
{
    g_free (tree->message);
    tree->message = g_strdup (message);
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

mctree_node_t *
edit_tree_current (const edit_tree_t *tree)
{
    return mctree_view_current_node (tree->view);
}

/* --------------------------------------------------------------------------------------------- */
