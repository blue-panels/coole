/*
   A text over the file window that takes no focus: what a language server says of a name.

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

#include "lsp-note.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* the widest box, and the most lines it shows at once */
#define LSP_NOTE_MAX_COLS  80
#define LSP_NOTE_MAX_LINES 15

/*** file scope type declarations ****************************************************************/

/* The widget of the box: its group may destroy it, the editor ending, before the plugin is
   closed; the box then learns it is gone */
typedef struct
{
    Widget widget;
    lsp_note_t *note;
} lsp_note_widget_t;

struct lsp_note_t
{
    lsp_note_widget_t *widget;  // NULL when there is none
    char *title;
    GPtrArray *lines;  // char *: the lines as they fit in the box
    int top;
    WScrollBar *bar;
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
lsp_note_rows (const lsp_note_t *note)
{
    return note->widget != NULL ? MAX (WIDGET (note->widget)->rect.lines - 2, 0) : 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of @text in @cols columns: a line too wide goes on under itself */
static GPtrArray *
lsp_note_wrap (const char *text, int cols)
{
    GPtrArray *lines = g_ptr_array_new_with_free_func (g_free);
    char **parts = g_strsplit (text, "\n", -1);
    int i;

    for (i = 0; parts[i] != NULL; i++)
    {
        const char *p = parts[i];

        do
        {
            const char *start = p;
            int used = 0;

            while (*p != '\0')
            {
                const char *next = str_cget_next_char_safe (p);
                const int width = str_term_char_width (p);

                if (used + width > cols && used > 0)
                    break;
                used += width;
                p = next;
            }
            g_ptr_array_add (lines, g_strndup (start, (gsize) (p - start)));
        }
        while (*p != '\0');
    }
    g_strfreev (parts);
    // the empty lines at the end say nothing
    while (lines->len > 1 && *(char *) g_ptr_array_index (lines, lines->len - 1) == '\0')
        g_ptr_array_remove_index (lines, lines->len - 1);
    return lines;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_note_draw_rows (lsp_note_t *note)
{
    Widget *w = WIDGET (note->widget);
    const int rows = lsp_note_rows (note);
    const int inner = w->rect.cols - 2;
    int r;

    tty_setcolor (dialog_colors[DLG_COLOR_NORMAL]);
    tty_fill_region (w->rect.y, w->rect.x, w->rect.lines, w->rect.cols, ' ');
    tty_draw_box (w->rect.y, w->rect.x, w->rect.lines, w->rect.cols, TRUE);
    if (note->title != NULL && inner > 4)
    {
        tty_setcolor (dialog_colors[DLG_COLOR_TITLE]);
        widget_gotoyx (w, 0, 2);
        tty_printf (" %s ", str_trunc (note->title, inner - 4));
    }

    tty_setcolor (dialog_colors[DLG_COLOR_NORMAL]);
    for (r = 0; r < rows && note->top + r < (int) note->lines->len; r++)
    {
        // a column of room at each side, as in a dialog
        widget_gotoyx (w, 1 + r, 2);
        tty_print_string (str_fit_to_term (g_ptr_array_index (note->lines, note->top + r),
                                           inner - 2, J_LEFT_FIT));
    }

    // a bar when there is more than is seen
    if ((int) note->lines->len > rows)
    {
        WRect bar = { w->rect.y + 1, w->rect.x + w->rect.cols - 1, rows, 1 };

        widget_set_size_rect (WIDGET (note->bar), &bar);
        scrollbar_set_range (note->bar, (long) note->lines->len, rows, note->top);
        scrollbar_set_color (note->bar, dialog_colors[DLG_COLOR_FRAME]);
        send_message (WIDGET (note->bar), NULL, MSG_DRAW, 0, NULL);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_note_draw (lsp_note_t *note)
{
    if (lsp_note_shown (note))
    {
        lsp_note_draw_rows (note);
        tty_refresh ();
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_note_scroll (lsp_note_t *note, int lines)
{
    const int last = MAX ((int) note->lines->len - lsp_note_rows (note), 0);

    note->top = CLAMP (note->top + lines, 0, last);
    lsp_note_draw (note);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
lsp_note_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    lsp_note_t *note = ((lsp_note_widget_t *) w)->note;

    switch (msg)
    {
    case MSG_DRAW:
        if (note != NULL)
            lsp_note_draw_rows (note);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (note != NULL)
            note->widget = NULL;
        return MSG_HANDLED;
    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_note_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    lsp_note_t *note = ((lsp_note_widget_t *) w)->note;

    (void) event;
    if (note == NULL)
        return;
    switch (msg)
    {
    case MSG_MOUSE_SCROLL_UP:
        lsp_note_scroll (note, -1);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        lsp_note_scroll (note, 1);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

lsp_note_t *
lsp_note_new (void)
{
    lsp_note_t *note = g_new0 (lsp_note_t, 1);

    note->lines = g_ptr_array_new_with_free_func (g_free);
    note->bar = scrollbar_new (0, 0, 1, SCROLLBAR_VERTICAL);
    return note;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_note_free (lsp_note_t *note)
{
    if (note == NULL)
        return;
    if (note->widget != NULL)
    {
        Widget *w = WIDGET (note->widget);

        // the editor ends: nothing to draw again
        if (w->owner != NULL)
            group_remove_widget (w);
        note->widget->note = NULL;
        widget_destroy (w);
    }
    g_ptr_array_free (note->lines, TRUE);
    g_free (note->title);
    widget_destroy (WIDGET (note->bar));
    g_free (note);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_note_show (lsp_note_t *note, WGroup *group, const char *title, const char *text, int y, int x)
{
    Widget *w;
    const int max_cols = MIN (LSP_NOTE_MAX_COLS, COLS);
    int cols = 0, below, above, rows;
    char **parts;
    WRect r;
    int i;

    // as wide as the widest line, the frame and a column of room at each side; the bar is on
    // the frame
    parts = g_strsplit (text, "\n", -1);
    for (i = 0; parts[i] != NULL; i++)
        cols = MAX (cols, str_term_width1 (parts[i]));
    g_strfreev (parts);
    if (title != NULL)
        cols = MAX (cols, str_term_width1 (title) + 4);
    cols = CLAMP (cols + 4, 20, max_cols);

    g_ptr_array_free (note->lines, TRUE);
    note->lines = lsp_note_wrap (text, cols - 4);
    note->top = 0;
    g_free (note->title);
    note->title = g_strdup (title);

    // under the line, or above it when there is more room there; the button bar stays
    below = LINES - 1 - (y + 1);
    above = y;
    rows = MIN ((int) note->lines->len, LSP_NOTE_MAX_LINES) + 2;
    r.cols = cols;
    r.x = MAX (MIN (x, COLS - cols), 0);
    if (rows <= below || below >= above)
    {
        r.lines = MIN (rows, below);
        r.y = y + 1;
    }
    else
    {
        r.lines = MIN (rows, above);
        r.y = y - r.lines;
    }
    if (r.lines < 3)
    {
        lsp_note_hide (note);
        return;
    }

    if (note->widget == NULL)
    {
        note->widget = g_new0 (lsp_note_widget_t, 1);
        note->widget->note = note;
        widget_init (WIDGET (note->widget), &r, lsp_note_callback, lsp_note_mouse);
        // the keys stay the file window's; over the windows, the docks too
        widget_set_options (WIDGET (note->widget), WOP_SELECTABLE, FALSE);
        widget_set_options (WIDGET (note->widget), WOP_OVERLAY, TRUE);
    }
    w = WIDGET (note->widget);
    if (w->owner != NULL && w->owner != group)
        lsp_note_hide (note);
    widget_set_size_rect (w, &r);
    if (w->owner == NULL)
        group_add_widget (group, w);
    lsp_note_draw (note);
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_note_hide (lsp_note_t *note)
{
    Widget *w = note->widget != NULL ? WIDGET (note->widget) : NULL;
    WGroup *group = w != NULL ? w->owner : NULL;

    if (group == NULL)
        return;
    group_remove_widget (w);
    // what it covered
    widget_draw (WIDGET (group));
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_note_shown (const lsp_note_t *note)
{
    return note->widget != NULL && CONST_WIDGET (note->widget)->owner != NULL;
}

/* --------------------------------------------------------------------------------------------- */
