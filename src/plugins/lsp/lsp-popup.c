/*
   A list over the text that takes no focus: the completion of the language servers.

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

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/tty/tty.h"
#include "lib/tty/mouse.h"
#include "lib/widget.h"

#include "lsp-popup.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* the widest list, and the widest it may take of the screen */
#define LSP_POPUP_MAX_COLS 70
/* the columns of the name, at most, before what it is */
#define LSP_POPUP_MAX_LABEL 40

/*** file scope type declarations ****************************************************************/

/* The widget of the list: its group may destroy it, the editor ending, before the plugin is
   closed; the list then learns it is gone */
typedef struct
{
    Widget widget;
    lsp_popup_t *popup;
} lsp_popup_widget_t;

struct lsp_popup_t
{
    lsp_popup_widget_t *widget;  // NULL when there is none
    GPtrArray *items;
    int current;
    int top;
    WScrollBar *bar;
    lsp_popup_take_fn take;
    void *data;
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
lsp_popup_item_free (gpointer data)
{
    lsp_popup_item_t *item = (lsp_popup_item_t *) data;

    g_free (item->label);
    g_free (item->detail);
    if (item->hits != NULL)
        g_array_free (item->hits, TRUE);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_popup_rows (const lsp_popup_t *popup)
{
    return popup->widget != NULL ? MAX (WIDGET (popup->widget)->rect.lines - 2, 0) : 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the byte @at of the label is one that matches */
static gboolean
lsp_popup_hit (const lsp_popup_item_t *item, int at)
{
    guint i;

    for (i = 0; item->hits != NULL && i < item->hits->len; i++)
        if (g_array_index (item->hits, int, i) == at)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* The label in @cols columns at most, the matching characters in @hot: runs of bytes that match or
   do not, a run printed whole as far as it fits */
static int
lsp_popup_print_label (const lsp_popup_item_t *item, int cols, int color, int hot)
{
    const char *label = item->label;
    const int len = (int) strlen (label);
    int at = 0, used = 0;

    while (at < len && used < cols)
    {
        const gboolean hit = lsp_popup_hit (item, at);
        int end = at + 1;
        char *run;
        int width;

        // the bytes of a character go together, and those that match alike
        while (end < len
               && (((unsigned char) label[end] & 0xc0) == 0x80 || lsp_popup_hit (item, end) == hit))
            end++;
        run = g_strndup (label + at, (gsize) (end - at));
        width = str_term_width1 (run);
        tty_setcolor (hit ? hot : color);
        if (width > cols - used)
        {
            tty_print_string (str_fit_to_term (run, cols - used, J_LEFT_FIT));
            width = cols - used;
        }
        else
            tty_print_string (run);
        used += width;
        g_free (run);
        at = end;
    }
    return used;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_popup_draw_rows (lsp_popup_t *popup)
{
    Widget *w = WIDGET (popup->widget);
    const int rows = lsp_popup_rows (popup);
    const int inner = w->rect.cols - 2;
    int label_cols = 0, r;
    guint i;

    for (i = 0; i < popup->items->len; i++)
        label_cols = MAX (
            label_cols,
            str_term_width1 (((lsp_popup_item_t *) g_ptr_array_index (popup->items, i))->label));
    label_cols = MIN (label_cols, MIN (LSP_POPUP_MAX_LABEL, inner - 2));

    tty_setcolor (dialog_colors[DLG_COLOR_NORMAL]);
    tty_fill_region (w->rect.y, w->rect.x, w->rect.lines, w->rect.cols, ' ');
    tty_draw_box (w->rect.y, w->rect.x, w->rect.lines, w->rect.cols, TRUE);

    for (r = 0; r < rows; r++)
    {
        const int index = popup->top + r;
        const lsp_popup_item_t *item;
        const gboolean chosen = index == popup->current;
        // the choice as a focused field: its matches stay seen, as the hot keys of a dialog
        const int color = dialog_colors[chosen ? DLG_COLOR_FOCUS : DLG_COLOR_NORMAL];
        const int hot = dialog_colors[chosen ? DLG_COLOR_HOT_FOCUS : DLG_COLOR_HOT_NORMAL];
        if (index >= (int) popup->items->len)
            break;
        item = g_ptr_array_index (popup->items, index);
        tty_setcolor (color);
        tty_draw_hline (w->rect.y + 1 + r, w->rect.x + 1, ' ', inner);
        widget_gotoyx (w, 1 + r, 1);
        tty_setcolor (hot);
        tty_print_char (item->kind);
        tty_setcolor (color);
        tty_print_char (' ');
        (void) lsp_popup_print_label (item, label_cols, color, hot);
        if (item->detail != NULL && inner - label_cols - 4 > 3)
        {
            const int room = inner - label_cols - 4;
            const char *detail = str_fit_to_term (item->detail, room, J_LEFT_FIT);

            // what it is, quiet, after the names, at the right of the list
            tty_setcolor (chosen ? color : CORE_DISABLED_COLOR);
            widget_gotoyx (w, 1 + r, 1 + inner - str_term_width1 (detail));
            tty_print_string (detail);
        }
    }

    if (popup->bar != NULL)
    {
        WRect bar = { w->rect.y + 1, w->rect.x + w->rect.cols - 1, rows, 1 };

        widget_set_size_rect (WIDGET (popup->bar), &bar);
        scrollbar_set_range (popup->bar, (long) popup->items->len, rows, popup->top);
        scrollbar_set_color (popup->bar, dialog_colors[DLG_COLOR_FRAME]);
        send_message (WIDGET (popup->bar), NULL, MSG_DRAW, 0, NULL);
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
lsp_popup_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    lsp_popup_t *popup = ((lsp_popup_widget_t *) w)->popup;

    switch (msg)
    {
    case MSG_DRAW:
        if (popup != NULL)
            lsp_popup_draw_rows (popup);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (popup != NULL)
            popup->widget = NULL;
        return MSG_HANDLED;
    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The choice at the row @y of the list, -1 for none */
static int
lsp_popup_at (const lsp_popup_t *popup, int y)
{
    const int index = popup->top + y - 1;

    return y >= 1 && y <= lsp_popup_rows (popup) && index < (int) popup->items->len ? index : -1;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_popup_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    lsp_popup_t *popup = ((lsp_popup_widget_t *) w)->popup;
    int index;

    if (popup == NULL)
        return;
    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        index = lsp_popup_at (popup, event->y);
        if (index >= 0)
        {
            popup->current = index;
            lsp_popup_draw (popup);
        }
        break;
    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE && lsp_popup_at (popup, event->y) >= 0
            && popup->take != NULL)
            popup->take (popup->data);
        break;
    case MSG_MOUSE_SCROLL_UP:
        lsp_popup_move (popup, -1);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        lsp_popup_move (popup, 1);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

lsp_popup_t *
lsp_popup_new (lsp_popup_take_fn take, void *data)
{
    lsp_popup_t *popup = g_new0 (lsp_popup_t, 1);

    popup->items = g_ptr_array_new_with_free_func (lsp_popup_item_free);
    popup->take = take;
    popup->data = data;
    popup->bar = scrollbar_new (0, 0, 1, SCROLLBAR_VERTICAL);
    return popup;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_popup_free (lsp_popup_t *popup)
{
    if (popup == NULL)
        return;
    if (popup->widget != NULL)
    {
        Widget *w = WIDGET (popup->widget);

        // the editor ends: nothing to draw again
        if (w->owner != NULL)
            group_remove_widget (w);
        popup->widget->popup = NULL;
        widget_destroy (w);
    }
    g_ptr_array_free (popup->items, TRUE);
    widget_destroy (WIDGET (popup->bar));
    g_free (popup);
}

/* --------------------------------------------------------------------------------------------- */

lsp_popup_item_t *
lsp_popup_item_new (char kind, const char *label, const char *detail, GArray *hits, void *data)
{
    lsp_popup_item_t *item = g_new0 (lsp_popup_item_t, 1);

    item->kind = kind;
    item->label = g_strdup (label);
    item->detail = g_strdup (detail);
    item->hits = hits;
    item->data = data;
    return item;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_popup_set_items (lsp_popup_t *popup, GPtrArray *items)
{
    g_ptr_array_free (popup->items, TRUE);
    popup->items = items;
    g_ptr_array_set_free_func (popup->items, lsp_popup_item_free);
    popup->current = 0;
    popup->top = 0;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_popup_show (lsp_popup_t *popup, WGroup *group, int y, int x)
{
    Widget *w;
    const int rows = MIN ((int) popup->items->len, LSP_POPUP_LINES);
    int cols = 0, label_cols = 0, detail_cols = 0;
    WRect r;
    guint i;

    for (i = 0; i < popup->items->len; i++)
    {
        const lsp_popup_item_t *item = g_ptr_array_index (popup->items, i);

        label_cols = MAX (label_cols, str_term_width1 (item->label));
        if (item->detail != NULL)
            detail_cols = MAX (detail_cols, str_term_width1 (item->detail));
    }
    // a frame, the kind and a space, the name, two spaces and what it is, the bar
    cols =
        2 + 2 + MIN (label_cols, LSP_POPUP_MAX_LABEL) + (detail_cols > 0 ? 2 + detail_cols : 0) + 1;
    cols = MAX (cols, 20);
    cols = MIN (cols, MIN (LSP_POPUP_MAX_COLS, COLS));
    r.lines = rows + 2;
    r.cols = cols;
    // the kind letter under the start of the word
    r.x = MAX (MIN (x - 3, COLS - cols), 0);
    // under the line, or above it when the screen has no room under it; the button bar stays
    r.y = y + 1 + r.lines <= LINES - 1 ? y + 1 : MAX (y - r.lines, 0);

    if (popup->widget == NULL)
    {
        popup->widget = g_new0 (lsp_popup_widget_t, 1);
        popup->widget->popup = popup;
        widget_init (WIDGET (popup->widget), &r, lsp_popup_callback, lsp_popup_mouse);
        // the keys stay the file window's; over the windows, the docks too
        widget_set_options (WIDGET (popup->widget), WOP_SELECTABLE, FALSE);
        widget_set_options (WIDGET (popup->widget), WOP_OVERLAY, TRUE);
    }
    w = WIDGET (popup->widget);
    if (w->owner != NULL && w->owner != group)
        lsp_popup_hide (popup);
    widget_set_size_rect (w, &r);
    if (popup->current >= (int) popup->items->len)
        popup->current = 0;
    if (w->owner == NULL)
        group_add_widget (group, w);
    lsp_popup_draw (popup);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_popup_hide (lsp_popup_t *popup)
{
    Widget *w = popup->widget != NULL ? WIDGET (popup->widget) : NULL;
    WGroup *group = w != NULL ? w->owner : NULL;

    if (group == NULL)
        return;
    group_remove_widget (w);
    // what it covered
    widget_draw (WIDGET (group));
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_popup_shown (const lsp_popup_t *popup)
{
    return popup->widget != NULL && CONST_WIDGET (popup->widget)->owner != NULL;
}

/* --------------------------------------------------------------------------------------------- */

int
lsp_popup_top (const lsp_popup_t *popup)
{
    return lsp_popup_shown (popup) ? CONST_WIDGET (popup->widget)->rect.y : -1;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_popup_draw (lsp_popup_t *popup)
{
    if (lsp_popup_shown (popup))
    {
        lsp_popup_draw_rows (popup);
        tty_refresh ();
    }
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_popup_move (lsp_popup_t *popup, int lines)
{
    const int last = (int) popup->items->len - 1;
    const int rows = MAX (lsp_popup_rows (popup), 1);

    if (last < 0)
        return;
    popup->current = CLAMP (popup->current + lines, 0, last);
    if (popup->current < popup->top)
        popup->top = popup->current;
    else if (popup->current >= popup->top + rows)
        popup->top = popup->current - rows + 1;
    lsp_popup_draw (popup);
}

/* --------------------------------------------------------------------------------------------- */

lsp_popup_item_t *
lsp_popup_current (const lsp_popup_t *popup)
{
    return popup->current >= 0 && popup->current < (int) popup->items->len
        ? g_ptr_array_index (popup->items, popup->current)
        : NULL;
}

/* --------------------------------------------------------------------------------------------- */
