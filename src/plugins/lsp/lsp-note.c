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

#include "lsp-markdown.h"
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

/* Whether @c is a mark of the text, of no width */
static inline gboolean
lsp_note_is_mark (char c)
{
    return c == LSP_NOTE_HOT_ON || c == LSP_NOTE_HOT_OFF;
}

/* --------------------------------------------------------------------------------------------- */

/* The columns of a line of the text, its marks taken away */
static int
lsp_note_width (const char *line)
{
    GString *seen = g_string_new (NULL);
    int width;

    if (*line == LSP_NOTE_HEADING || *line == LSP_NOTE_CODE || *line == LSP_NOTE_RULE)
        line++;
    for (; *line != '\0'; line++)
        if (!lsp_note_is_mark (*line))
            g_string_append_c (seen, *line);
    width = str_term_width1 (seen->str);
    g_string_free (seen, TRUE);
    return width;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a line may be broken after the character @c: a space, or what ends a part of code */
static inline gboolean
lsp_note_breaks_after (char c)
{
    return strchr (" ,;()[]{}*&|-+=/.:<>", c) != NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of @text in @cols columns: a line too wide goes on under itself, broken after the
   last space or the last end of a part of code that fits, in a word only when there is none;
   a heading or code still, the code moved in by two columns, what is hot still hot */
static GPtrArray *
lsp_note_wrap (const char *text, int cols)
{
    GPtrArray *lines = g_ptr_array_new_with_free_func (g_free);
    char **parts = g_strsplit (text, "\n", -1);
    int i;

    for (i = 0; parts[i] != NULL; i++)
    {
        const char *p = parts[i];
        const char kind =
            *p == LSP_NOTE_HEADING || *p == LSP_NOTE_CODE || *p == LSP_NOTE_RULE ? *p : '\0';
        gboolean hot = FALSE;

        if (kind == LSP_NOTE_RULE)
        {
            g_ptr_array_add (lines, g_strdup (p));
            continue;
        }
        if (kind != '\0')
            p++;
        do
        {
            GString *line = g_string_new (NULL);
            const gboolean going_on = p != parts[i] + (kind != '\0' ? 1 : 0);
            int used = 0, taken = 0;
            // the last place it may be broken: in the line made, in the text, what is hot there
            gsize break_len = 0;
            const char *break_at = NULL;
            gboolean break_hot = FALSE;

            if (kind != '\0')
                g_string_append_c (line, kind);
            // code going on under itself is moved in; text going on starts at a word
            if (going_on && kind == LSP_NOTE_CODE)
            {
                g_string_append (line, "  ");
                used = 2;
            }
            else if (going_on)
                while (*p == ' ')
                    p++;
            // what went on under itself was spaces
            if (going_on && *p == '\0')
            {
                g_string_free (line, TRUE);
                break;
            }
            if (hot)
                g_string_append_c (line, LSP_NOTE_HOT_ON);
            while (*p != '\0')
            {
                const char *next;
                int width;

                if (lsp_note_is_mark (*p))
                {
                    hot = *p == LSP_NOTE_HOT_ON;
                    g_string_append_c (line, *p++);
                    continue;
                }
                next = str_cget_next_char_safe (p);
                width = str_term_char_width (p);
                // a line takes a character at least
                if (used + width > cols && taken > 0)
                {
                    if (break_at != NULL)
                    {
                        g_string_truncate (line, break_len);
                        p = break_at;
                        hot = break_hot;
                    }
                    break;
                }
                used += width;
                taken++;
                g_string_append_len (line, p, next - p);
                if (lsp_note_breaks_after (*p))
                {
                    break_len = line->len;
                    break_at = next;
                    break_hot = hot;
                }
                p = next;
            }
            if (hot && *p != '\0')
                g_string_append_c (line, LSP_NOTE_HOT_OFF);
            g_ptr_array_add (lines, g_string_free (line, FALSE));
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

/* The line @line of the text at the row @row of the box, @cols columns from the column 2 */
static void
lsp_note_draw_line (Widget *w, int row, const char *line, int cols)
{
    const int normal = dialog_colors[DLG_COLOR_NORMAL];
    const int hot = dialog_colors[DLG_COLOR_HOT_NORMAL];
    int color = normal, used = 0;

    if (*line == LSP_NOTE_RULE)
    {
        // across the box, as the line of a dialog
        tty_setcolor (normal);
        tty_draw_hline (w->rect.y + row, w->rect.x + 1, mc_tty_frm[MC_TTY_FRM_HORIZ],
                        w->rect.cols - 2);
        widget_gotoyx (w, row, 0);
        tty_print_char (mc_tty_frm[MC_TTY_FRM_LEFTMIDDLE]);
        widget_gotoyx (w, row, w->rect.cols - 1);
        tty_print_char (mc_tty_frm[MC_TTY_FRM_RIGHTMIDDLE]);
        return;
    }
    if (*line == LSP_NOTE_CODE)
    {
        // code as in the editor, on its colors, across the box
        color = EDITOR_NORMAL_COLOR;
        tty_setcolor (color);
        tty_draw_hline (w->rect.y + row, w->rect.x + 1, ' ', w->rect.cols - 2);
        line++;
    }
    else if (*line == LSP_NOTE_HEADING)
    {
        color = dialog_colors[DLG_COLOR_TITLE];
        line++;
    }

    widget_gotoyx (w, row, 2);
    while (*line != '\0' && used < cols)
    {
        const char *end = line;
        char *run;
        int width;

        if (lsp_note_is_mark (*line))
        {
            // what is hot in a heading or in code keeps its color
            if (color == normal || color == hot)
                color = *line == LSP_NOTE_HOT_ON ? hot : normal;
            line++;
            continue;
        }
        while (*end != '\0' && !lsp_note_is_mark (*end))
            end++;
        run = g_strndup (line, (gsize) (end - line));
        width = str_term_width1 (run);
        tty_setcolor (color);
        if (width > cols - used)
        {
            tty_print_string (str_fit_to_term (run, cols - used, J_LEFT_FIT));
            width = cols - used;
        }
        else
            tty_print_string (run);
        used += width;
        g_free (run);
        line = end;
    }
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

    // a column of room at each side, as in a dialog
    for (r = 0; r < rows && note->top + r < (int) note->lines->len; r++)
        lsp_note_draw_line (w, 1 + r, g_ptr_array_index (note->lines, note->top + r), inner - 2);

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
    int cols = 0, code_cols = 0, below, above, rows;
    char **parts;
    WRect r;
    int i;

    /* as wide as the widest line, the frame and a column of room at each side, the bar being on
       the frame; the text in LSP_NOTE_MAX_COLS at most, code in the screen, as little of it going
       on under itself as can be */
    parts = g_strsplit (text, "\n", -1);
    for (i = 0; parts[i] != NULL; i++)
        if (*parts[i] == LSP_NOTE_CODE)
            code_cols = MAX (code_cols, lsp_note_width (parts[i]));
        else
            cols = MAX (cols, lsp_note_width (parts[i]));
    g_strfreev (parts);
    if (title != NULL)
        cols = MAX (cols, str_term_width1 (title) + 4);
    cols = MAX (MIN (cols + 4, LSP_NOTE_MAX_COLS), code_cols + 4);
    cols = CLAMP (cols, 20, COLS);

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
