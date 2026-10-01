/*
   A window of the editor screen that shows text it is given.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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
 *  \brief Source: a window of the editor screen that shows text it is given, the window of the
 *  viewer plugin
 *
 *  The text is what a program prints for a terminal or a pager: overstrikes, the way nroff
 *  marks bold (c\bc) and underlined (_\bc) letters, and ANSI colors (ESC [ ... m). A line of the
 *  text is a line of the window; one wider than the window is scrolled sideways.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/strutil.h"  // str_term_trim()
#include "lib/util.h"     // Q_()
#include "lib/widget.h"

#include "src/keymap.h"  // editor_map
#include "src/editor/editwindow.h"
#include "src/viewer/ansi.h"
#include "src/viewer/ansi_color.h"

#include "textwindow.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define TEXT_WINDOW(x)       ((WTextWindow *) (x))
#define CONST_TEXT_WINDOW(x) ((const WTextWindow *) (x))

/* How far Left and Right scroll the text sideways */
#define TEXT_WINDOW_HSTEP 8

/* How far the mouse wheel scrolls */
#define TEXT_WINDOW_WHEEL 2

/*** file scope type declarations ****************************************************************/

enum
{
    TW_BOLD = 1 << 0,
    TW_UNDERLINE = 1 << 1,
    TW_ITALIC = 1 << 2,
    TW_REVERSE = 1 << 3,
    TW_DIM = 1 << 4,
    TW_BLINK = 1 << 5,
    TW_CONCEAL = 1 << 6
};

/* A character of the text, with how it is drawn */
typedef struct
{
    gunichar ch;
    guint8 width;  // columns it takes: 1, or 2 for a wide character
    guint8 attrs;  // TW_*
    gint16 fg;     // the ANSI colors, MCVIEW_ANSI_COLOR_DEFAULT for none
    gint16 bg;
} tw_cell_t;

typedef struct
{
    GArray *lines;  // GArray of tw_cell_t, one for every line of the text
    long width;     // the columns of its widest line
    long top;       // the line of the text at the top of the view
    int left;       // the column of the text at the left of the view
} tw_text_t;

typedef struct
{
    WEditWindow window;
    char *title;
    tw_text_t text;
    long cur_line;  // the cursor: the line of the text, and the column in it
    long cur_col;
    long want_col;  // the column it goes back to on a line long enough
    void (*on_destroy) (void *data);
    void *on_destroy_data;
} WTextWindow;

/*** forward declarations (file scope functions) *************************************************/

static cb_ret_t text_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm,
                                      void *data);
static void text_window_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event);
static char *text_window_get_title (const WEditWindow *win);
static gboolean text_window_close (WEditWindow *win);
static void text_window_scroll_bar (WEditWindow *win, gboolean vertical, long pos);

/*** file scope variables ************************************************************************/

static const edit_window_class_t text_window_class = {
    .callback = text_window_callback,
    .mouse_callback = text_window_mouse_callback,
    .get_title = text_window_get_title,
    .is_modified = NULL,
    .close = text_window_close,
    .ok_to_quit = NULL,
    .min_lines = 2 + 1,
    .min_cols = 2 + 8,
    .vbar = TRUE,
    .hbar_x = 1,
    .scrolled = text_window_scroll_bar,
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
tw_line_free (gpointer data)
{
    GArray **line = (GArray **) data;

    g_array_free (*line, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static GArray *
tw_lines_new (void)
{
    GArray *lines;

    lines = g_array_new (FALSE, FALSE, sizeof (GArray *));
    g_array_set_clear_func (lines, tw_line_free);

    return lines;
}

/* --------------------------------------------------------------------------------------------- */

static void
tw_line_add (GArray *lines)
{
    GArray *line;

    line = g_array_new (FALSE, FALSE, sizeof (tw_cell_t));
    g_array_append_val (lines, line);
}

/* --------------------------------------------------------------------------------------------- */

static guint8
tw_attrs_of (const mcview_ansi_state_t *st)
{
    guint8 attrs = 0;

    if (st->bold)
        attrs |= TW_BOLD;
    if (st->underline || st->link)
        attrs |= TW_UNDERLINE;
    if (st->italic)
        attrs |= TW_ITALIC;
    if (st->reverse)
        attrs |= TW_REVERSE;
    if (st->dim)
        attrs |= TW_DIM;
    if (st->blink)
        attrs |= TW_BLINK;
    if (st->conceal)
        attrs |= TW_CONCEAL;

    return attrs;
}

/* --------------------------------------------------------------------------------------------- */

/* A character comes to the line, or over its last one after a backspace: the overstrikes of
   nroff, c\bc for bold and _\bc for underlined */
static void
tw_put (GArray *line, gunichar ch, const mcview_ansi_state_t *st, gboolean over)
{
    tw_cell_t cell;

    if (over && line->len != 0)
    {
        tw_cell_t *last = &g_array_index (line, tw_cell_t, line->len - 1);

        if (last->ch == ch)
            last->attrs |= TW_BOLD;
        else if (last->ch == '_')
        {
            last->ch = ch;
            last->width = g_unichar_iswide (ch) ? 2 : 1;
            last->attrs |= TW_UNDERLINE;
        }
        else if (ch == '_')
            last->attrs |= TW_UNDERLINE;
        else
        {
            last->ch = ch;
            last->width = g_unichar_iswide (ch) ? 2 : 1;
        }
        return;
    }

    // a mark that joins the character before it takes no column of its own
    if (g_unichar_iszerowidth (ch))
        return;

    cell.ch = ch;
    cell.width = g_unichar_iswide (ch) ? 2 : 1;
    cell.attrs = tw_attrs_of (st);
    cell.fg = (gint16) st->fg;
    cell.bg = (gint16) st->bg;
    g_array_append_val (line, cell);
}

/* --------------------------------------------------------------------------------------------- */

static void
tw_parse (tw_text_t *text, const char *data, gsize len)
{
    mcview_ansi_state_t st;
    GArray *line;
    char utf8[8];
    int utf8_len = 0;
    gboolean over = FALSE;
    gsize i;

    if (text->lines != NULL)
        g_array_free (text->lines, TRUE);
    text->lines = tw_lines_new ();
    tw_line_add (text->lines);
    line = g_array_index (text->lines, GArray *, 0);

    mcview_ansi_state_init (&st);

    for (i = 0; i < len; i++)
    {
        const unsigned char c = (unsigned char) data[i];

        if (c == '\n')
        {
            tw_line_add (text->lines);
            line = g_array_index (text->lines, GArray *, text->lines->len - 1);
            utf8_len = 0;
            over = FALSE;
            continue;
        }

        if (mcview_ansi_parse_char (&st, c) == ANSI_RESULT_CONSUMED)
            continue;

        if (c == '\b')
            over = TRUE;
        else if (c == '\t')
        {
            const mcview_ansi_state_t plain = st;
            int col = 0;
            guint n;

            for (n = 0; n < line->len; n++)
                col += g_array_index (line, tw_cell_t, n).width;
            do
                tw_put (line, ' ', &plain, FALSE);
            while (++col % 8 != 0);
            over = FALSE;
        }
        else if (c < 0x20 || c == 0x7f)
            ;  // the other control characters draw nothing
        else
        {
            gunichar ch;

            if (utf8_len >= (int) sizeof (utf8))
                utf8_len = 0;
            utf8[utf8_len++] = (char) c;

            ch = g_utf8_get_char_validated (utf8, utf8_len);
            if (ch == (gunichar) -2)
                continue;  // more bytes of the character to come
            if (ch == (gunichar) -1)
                ch = 0xFFFD;
            utf8_len = 0;

            tw_put (line, ch, &st, over);
            over = FALSE;
        }
    }

    // a text that ends with a newline has no empty line after it
    if (text->lines->len > 1
        && g_array_index (text->lines, GArray *, text->lines->len - 1)->len == 0)
        g_array_set_size (text->lines, text->lines->len - 1);

    text->width = 0;
    for (i = 0; i < text->lines->len; i++)
    {
        const GArray *l = g_array_index (text->lines, GArray *, i);
        long cols = 0;
        guint n;

        for (n = 0; n < l->len; n++)
            cols += g_array_index (l, tw_cell_t, n).width;
        text->width = MAX (text->width, cols);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The part of the window the text takes: all of it but the frame */
static void
text_window_text_rect (const WTextWindow *tw, WRect *r)
{
    *r = CONST_WIDGET (tw)->rect;
    if (tw->window.fullscreen == 0)
        rect_grow (r, -1, -1);
}

/* --------------------------------------------------------------------------------------------- */

static long
text_window_max_top (const WTextWindow *tw)
{
    WRect r;
    long lines;

    text_window_text_rect (tw, &r);
    lines = tw->text.lines == NULL ? 0 : (long) tw->text.lines->len;

    return MAX (0, lines - r.lines);
}

/* --------------------------------------------------------------------------------------------- */

static int
text_window_max_left (const WTextWindow *tw)
{
    WRect r;

    text_window_text_rect (tw, &r);

    return (int) MAX (0, tw->text.width - r.cols);
}

/* --------------------------------------------------------------------------------------------- */

/* The scrollbars of the window over its lines and its columns */
static void
text_window_set_bars (WTextWindow *tw)
{
    WEditWindow *win = EDIT_WINDOW (tw);
    WRect r;

    text_window_text_rect (tw, &r);
    edit_window_set_scroll (win, TRUE, tw->text.lines == NULL ? 0 : (long) tw->text.lines->len,
                            r.lines, tw->text.top);
    edit_window_set_scroll (win, FALSE, tw->text.width, r.cols, tw->text.left);
}

/* --------------------------------------------------------------------------------------------- */

static void
text_window_draw_frame (WTextWindow *tw)
{
    const WEditWindow *win = &tw->window;
    const Widget *w = CONST_WIDGET (tw);
    const gboolean active = widget_get_state (w, WST_FOCUSED);
    const int color = edit_window_frame_color (win, active);
    const char *title = tw->title != NULL ? tw->title : "";

    if (win->fullscreen != 0)
    {
        const Widget *h = CONST_WIDGET (w->owner);

        tty_setcolor (color);
        tty_draw_hline (h->rect.y, h->rect.x, ' ', h->rect.cols);
        widget_gotoyx (h, 0, 0);
        tty_print_string (str_term_trim (title, h->rect.cols - 7));
    }
    else
    {
        const int cols = w->rect.cols - 13;  // the corners, the brackets, the buttons and a line

        edit_window_draw_frame (win, color, active);
        if (cols > 0)
        {
            tty_setcolor (color);
            widget_gotoyx (w, 0, 2);
            tty_print_char ('[');
            tty_print_string (str_term_trim (title, cols));
            tty_print_char (']');
        }
        text_window_set_bars (tw);
        edit_window_draw_bars (EDIT_WINDOW (tw), color);
    }

    edit_window_draw_icons (win, color);
}

/* --------------------------------------------------------------------------------------------- */

static void
text_window_draw_text (const WTextWindow *tw)
{
    /* The colors of the editor: the window stands beside the windows of the files.  Underlined
       text is drawn with the underline of the terminal, there being no color of the skin for it. */
    static const mcview_canvas_colors_t colors_init = { "editor", 0, 0, -1, -1 };
    mcview_canvas_colors_t colors = colors_init;
    WRect r;
    int row;

    colors.normal = EDITOR_NORMAL_COLOR;
    colors.bold = EDITOR_BOLD_COLOR;

    text_window_text_rect (tw, &r);

    for (row = 0; row < r.lines; row++)
    {
        const long n = tw->text.top + row;
        const GArray *line = NULL;
        int x = 0;  // the column of the text
        int col = 0;
        guint i;
        int last_color = -1;
        tw_cell_t last_cell;

        if (tw->text.lines != NULL && n < (long) tw->text.lines->len)
            line = g_array_index (tw->text.lines, GArray *, n);

        tty_setcolor (colors.normal);
        tty_draw_hline (r.y + row, r.x, ' ', r.cols);
        tty_gotoyx (r.y + row, r.x);

        memset (&last_cell, 0, sizeof (last_cell));

        for (i = 0; line != NULL && i < line->len && col < r.cols; i++)
        {
            const tw_cell_t *cell = &g_array_index (line, tw_cell_t, i);

            if (x + cell->width <= tw->text.left)
            {
                x += cell->width;
                continue;
            }

            if (last_color < 0 || cell->attrs != last_cell.attrs || cell->fg != last_cell.fg
                || cell->bg != last_cell.bg)
            {
                mcview_ansi_state_t st;

                mcview_ansi_state_init (&st);
                st.fg = cell->fg;
                st.bg = cell->bg;
                st.bold = (cell->attrs & TW_BOLD) != 0;
                st.underline = (cell->attrs & TW_UNDERLINE) != 0;
                st.italic = (cell->attrs & TW_ITALIC) != 0;
                st.reverse = (cell->attrs & TW_REVERSE) != 0;
                st.dim = (cell->attrs & TW_DIM) != 0;
                st.blink = (cell->attrs & TW_BLINK) != 0;
                last_color = mcview_ansi_color_of (&st, &colors);
                last_cell = *cell;
            }
            tty_setcolor (last_color);

            if (x < tw->text.left || col + cell->width > r.cols)
                // half of a wide character at an edge of the view
                tty_print_char (' ');
            else if ((cell->attrs & TW_CONCEAL) != 0)
            {
                tty_print_char (' ');
                if (cell->width == 2)
                    tty_print_char (' ');
            }
            else
                tty_print_anychar ((int) cell->ch);

            col += (x < tw->text.left) ? x + cell->width - tw->text.left : cell->width;
            x += cell->width;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
text_window_set_buttonbar (WTextWindow *tw)
{
    WButtonBar *bb = buttonbar_find (DIALOG (WIDGET (tw)->owner));
    const global_keymap_t *keymap = WIDGET (WIDGET (tw)->owner)->keymap;
    int i;

    if (bb == NULL)
        return;

    buttonbar_set_label (bb, 1, Q_ ("ButtonBar|Help"), keymap, NULL);
    for (i = 2; i <= 8; i++)
        buttonbar_set_label (bb, i, "", NULL, NULL);
    buttonbar_set_label (bb, 9, Q_ ("ButtonBar|PullDn"), keymap, NULL);
    buttonbar_set_label (bb, 10, Q_ ("ButtonBar|Quit"), keymap, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
text_window_scroll (WTextWindow *tw, long top, int left)
{
    tw->text.top = CLAMP (top, 0, text_window_max_top (tw));
    tw->text.left = CLAMP (left, 0, text_window_max_left (tw));
    widget_draw (WIDGET (tw));
}

/* --------------------------------------------------------------------------------------------- */

/* The columns of the line n of the text */
static long
tw_line_width (const tw_text_t *text, long n)
{
    const GArray *line;
    long cols = 0;
    guint i;

    if (text->lines == NULL || n < 0 || n >= (long) text->lines->len)
        return 0;
    line = g_array_index (text->lines, GArray *, n);
    for (i = 0; i < line->len; i++)
        cols += g_array_index (line, tw_cell_t, i).width;
    return cols;
}

/* --------------------------------------------------------------------------------------------- */

/* The column of the cell under col, so that the cursor does not stand in half a wide character */
static long
tw_cell_start (const tw_text_t *text, long n, long col)
{
    const GArray *line;
    long x = 0;
    guint i;

    if (text->lines == NULL || n < 0 || n >= (long) text->lines->len)
        return col;
    line = g_array_index (text->lines, GArray *, n);
    for (i = 0; i < line->len; i++)
    {
        const long w = g_array_index (line, tw_cell_t, i).width;

        if (col < x + w)
            return x;
        x += w;
    }
    return col;
}

/* --------------------------------------------------------------------------------------------- */

/* Put the cursor at the line and the column, within the text, and the view where it is seen */
static void
text_window_move_cursor (WTextWindow *tw, long line, long col, gboolean remember)
{
    const long lines = tw->text.lines == NULL ? 1 : MAX (1, (long) tw->text.lines->len);
    WRect r;
    long top = tw->text.top;
    long left = tw->text.left;

    text_window_text_rect (tw, &r);

    tw->cur_line = CLAMP (line, 0, lines - 1);
    tw->cur_col = tw_cell_start (&tw->text, tw->cur_line,
                                 CLAMP (col, 0, tw_line_width (&tw->text, tw->cur_line)));
    if (remember)
        tw->want_col = tw->cur_col;

    if (tw->cur_line < top)
        top = tw->cur_line;
    else if (tw->cur_line >= top + r.lines)
        top = tw->cur_line - r.lines + 1;
    if (tw->cur_col < left)
        left = MAX (0, tw->cur_col - TEXT_WINDOW_HSTEP);
    else if (tw->cur_col >= left + r.cols)
        left = tw->cur_col - r.cols + 1 + TEXT_WINDOW_HSTEP;

    // the cursor may stand past the widest line: the view goes as far as it
    tw->text.top = CLAMP (top, 0, text_window_max_top (tw));
    tw->text.left = (int) MAX (0, left);
    widget_draw (WIDGET (tw));
    // the frame drawn last left the terminal cursor on it
    if (widget_get_state (WIDGET (tw), WST_FOCUSED))
        widget_update_cursor (WIDGET (WIDGET (tw)->owner));
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
text_window_key (WTextWindow *tw, int key)
{
    WRect r;

    text_window_text_rect (tw, &r);

    switch (keybind_lookup_keymap_command (editor_map, key))
    {
    case CK_Up:
        text_window_move_cursor (tw, tw->cur_line - 1, tw->want_col, FALSE);
        break;
    case CK_Down:
        text_window_move_cursor (tw, tw->cur_line + 1, tw->want_col, FALSE);
        break;
    case CK_PageUp:
        tw->text.top = MAX (0, tw->text.top - MAX (1, r.lines - 1));
        text_window_move_cursor (tw, tw->cur_line - MAX (1, r.lines - 1), tw->want_col, FALSE);
        break;
    case CK_PageDown:
        tw->text.top = MIN (text_window_max_top (tw), tw->text.top + MAX (1, r.lines - 1));
        text_window_move_cursor (tw, tw->cur_line + MAX (1, r.lines - 1), tw->want_col, FALSE);
        break;
    case CK_Home:
        text_window_move_cursor (tw, tw->cur_line, 0, TRUE);
        break;
    case CK_End:
        text_window_move_cursor (tw, tw->cur_line, tw_line_width (&tw->text, tw->cur_line), TRUE);
        break;
    case CK_Top:
        text_window_move_cursor (tw, 0, 0, TRUE);
        break;
    case CK_Bottom:
        text_window_move_cursor (tw, G_MAXLONG, 0, TRUE);
        break;
    case CK_TopOnScreen:
        text_window_move_cursor (tw, tw->text.top, tw->want_col, FALSE);
        break;
    case CK_BottomOnScreen:
        text_window_move_cursor (tw, tw->text.top + r.lines - 1, tw->want_col, FALSE);
        break;
    case CK_Left:
        if (tw->cur_col > 0)
            text_window_move_cursor (tw, tw->cur_line, tw->cur_col - 1, TRUE);
        else if (tw->cur_line > 0)
            text_window_move_cursor (tw, tw->cur_line - 1, G_MAXLONG, TRUE);
        break;
    case CK_Right:
        if (tw->cur_col < tw_line_width (&tw->text, tw->cur_line))
        {
            // past the whole character under the cursor, wide or not
            long next = tw->cur_col + 1;

            while (next < tw_line_width (&tw->text, tw->cur_line)
                   && tw_cell_start (&tw->text, tw->cur_line, next) == tw->cur_col)
                next++;
            text_window_move_cursor (tw, tw->cur_line, next, TRUE);
        }
        else if (tw->text.lines != NULL && tw->cur_line + 1 < (long) tw->text.lines->len)
            text_window_move_cursor (tw, tw->cur_line + 1, 0, TRUE);
        break;
    // a view to read has no words to edit: the cursor goes sideways by the step of the view
    case CK_WordLeft:
        text_window_move_cursor (tw, tw->cur_line, tw->cur_col - TEXT_WINDOW_HSTEP, TRUE);
        break;
    case CK_WordRight:
        text_window_move_cursor (tw, tw->cur_line, tw->cur_col + TEXT_WINDOW_HSTEP, TRUE);
        break;
    default:
        return MSG_NOT_HANDLED;
    }

    return MSG_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
text_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WTextWindow *tw = TEXT_WINDOW (w);

    switch (msg)
    {
    case MSG_DRAW:
        text_window_draw_text (tw);
        text_window_draw_frame (tw);
        return MSG_HANDLED;

    case MSG_RESIZE:
        (void) group_default_callback (w, sender, msg, parm, data);
        // the lines at the bottom stay on the screen
        tw->text.top = MIN (tw->text.top, text_window_max_top (tw));
        tw->text.left = MIN (tw->text.left, text_window_max_left (tw));
        return MSG_HANDLED;

    case MSG_KEY:
        return text_window_key (tw, parm);

    case MSG_CURSOR:
    {
        WRect r;
        const long y = tw->cur_line - tw->text.top;
        const long x = tw->cur_col - tw->text.left;

        // the cursor of the text, where it is seen; else the top left, out of the way
        text_window_text_rect (tw, &r);
        if (y >= 0 && y < r.lines && x >= 0 && x < r.cols)
            tty_gotoyx (r.y + (int) y, r.x + (int) x);
        else
            tty_gotoyx (r.y, r.x);
        return MSG_HANDLED;
    }

    case MSG_FOCUS:
        text_window_set_buttonbar (tw);
        MC_FALLTHROUGH;
    case MSG_UNFOCUS:
        if (widget_get_state (w, WST_VISIBLE))
            text_window_draw_frame (tw);
        return MSG_HANDLED;

    case MSG_DESTROY:
        if (tw->on_destroy != NULL)
            tw->on_destroy (tw->on_destroy_data);
        if (tw->text.lines != NULL)
            g_array_free (tw->text.lines, TRUE);
        g_free (tw->title);
        return group_default_callback (w, sender, msg, parm, data);

    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A press on the text puts the cursor there.  FALSE when it is not on the text */
static gboolean
text_window_press_text (WTextWindow *tw, const mouse_event_t *event)
{
    const WRect *w = &WIDGET (tw)->rect;
    WRect r;
    int x, y;

    text_window_text_rect (tw, &r);
    // the press in the coordinates of the text
    x = event->x - (r.x - w->x);
    y = event->y - (r.y - w->y);
    if (x < 0 || x >= r.cols || y < 0 || y >= r.lines)
        return FALSE;

    text_window_move_cursor (tw, tw->text.top + y, tw->text.left + x, TRUE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
text_window_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    WTextWindow *tw = TEXT_WINDOW (w);

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        // the cursor goes where the text is pressed; the scrollbars take their own presses
        (void) text_window_press_text (tw, event);
        break;

    case MSG_MOUSE_SCROLL_UP:
        text_window_scroll (tw, tw->text.top - TEXT_WINDOW_WHEEL, tw->text.left);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        text_window_scroll (tw, tw->text.top + TEXT_WINDOW_WHEEL, tw->text.left);
        break;
    case MSG_MOUSE_SCROLL_LEFT:
        text_window_scroll (tw, tw->text.top, tw->text.left - TEXT_WINDOW_HSTEP);
        break;
    case MSG_MOUSE_SCROLL_RIGHT:
        text_window_scroll (tw, tw->text.top, tw->text.left + TEXT_WINDOW_HSTEP);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
text_window_get_title (const WEditWindow *win)
{
    const WTextWindow *tw = CONST_TEXT_WINDOW (win);

    return g_strdup (tw->title != NULL ? tw->title : "");
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
text_window_close (WEditWindow *win)
{
    // the window it made room for takes the screen again
    edit_window_give_room_back (win);
    edit_window_destroy (win);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A scrollbar of the frame has moved: the view goes with it */
static void
text_window_scroll_bar (WEditWindow *win, gboolean vertical, long pos)
{
    WTextWindow *tw = TEXT_WINDOW (win);

    if (vertical)
        text_window_scroll (tw, pos, tw->text.left);
    else
        text_window_scroll (tw, tw->text.top, (int) pos);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

WEditWindow *
edit_text_window_new (const WRect *r, const char *title)
{
    WTextWindow *tw;

    tw = g_new0 (WTextWindow, 1);
    edit_window_init (&tw->window, r, &text_window_class);
    tw->window.fullscreen = 0;
    tw->title = g_strdup (title);
    tw->text.lines = tw_lines_new ();

    return &tw->window;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_text_window_is (const Widget *w)
{
    return (edit_window_is_window (w) && CONST_EDIT_WINDOW (w)->klass == &text_window_class);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_text_window_set_title (WEditWindow *win, const char *title)
{
    WTextWindow *tw = TEXT_WINDOW (win);

    g_free (tw->title);
    tw->title = g_strdup (title);
    if (widget_get_state (WIDGET (tw), WST_VISIBLE))
        text_window_draw_frame (tw);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_text_window_set_text (WEditWindow *win, const char *text, gsize len)
{
    WTextWindow *tw = TEXT_WINDOW (win);

    tw_parse (&tw->text, text != NULL ? text : "", text != NULL ? len : 0);
    tw->text.top = MIN (tw->text.top, text_window_max_top (tw));
    tw->text.left = MIN (tw->text.left, text_window_max_left (tw));
    // the cursor stays on the text that came
    tw->cur_line = MIN (tw->cur_line, MAX (0, (long) tw->text.lines->len - 1));
    tw->cur_col = MIN (tw->cur_col, tw_line_width (&tw->text, tw->cur_line));
    widget_draw (WIDGET (tw));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_text_window_scroll_to (WEditWindow *win, long line)
{
    WTextWindow *tw = TEXT_WINDOW (win);

    text_window_scroll (tw, line, tw->text.left);
}

/* --------------------------------------------------------------------------------------------- */

long
edit_text_window_lines (const WEditWindow *win)
{
    const WTextWindow *tw = CONST_TEXT_WINDOW (win);

    return tw->text.lines == NULL ? 0 : (long) tw->text.lines->len;
}

/* --------------------------------------------------------------------------------------------- */

long
edit_text_window_top (const WEditWindow *win)
{
    return CONST_TEXT_WINDOW (win)->text.top;
}

/* --------------------------------------------------------------------------------------------- */

int
edit_text_window_text_cols (const WEditWindow *win)
{
    WRect r;

    text_window_text_rect (CONST_TEXT_WINDOW (win), &r);
    return MAX (0, r.cols);
}

/* --------------------------------------------------------------------------------------------- */

int
edit_text_window_text_lines (const WEditWindow *win)
{
    WRect r;

    text_window_text_rect (CONST_TEXT_WINDOW (win), &r);
    return MAX (0, r.lines);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_text_window_on_destroy (WEditWindow *win, void (*fn) (void *data), void *data)
{
    WTextWindow *tw = TEXT_WINDOW (win);

    tw->on_destroy = fn;
    tw->on_destroy_data = data;
}

/* --------------------------------------------------------------------------------------------- */
