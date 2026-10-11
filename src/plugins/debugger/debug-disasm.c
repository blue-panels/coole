/*
   The debugger plugin: the window of the instructions, and the breakpoints on them.

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

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"

#include "src/keymap.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"

#include "lib/editor-plugin.h"
#include "src/plugins/project/project-core.h"  // project_find_root (), without the project plugin
#include "debug-backend.h"
#include "debugger-int.h"

/*** forward declarations (file scope functions) */
/*** end of forward declarations */

/*** file scope functions *********************************************************************/

/* --------------------------------------------------------------------------------------------- */
/* The window of the instructions */
/* --------------------------------------------------------------------------------------------- */

/* A row of the window: a source line, or an instruction */
typedef struct
{
    gboolean source;
    guint index;  // of the instruction
    char *text;
} debug_disasm_row_t;

static void
debug_disasm_row_free (gpointer p)
{
    debug_disasm_row_t *row = (debug_disasm_row_t *) p;

    g_free (row->text);
    g_free (row);
}

/* The number of an address, 0 when it is none */
static guint64
debug_address_value (const char *address)
{
    return address != NULL ? g_ascii_strtoull (address, NULL, 16) : 0;
}

static debug_address_breakpoint_t *
debug_address_breakpoint_at (const debugger_t *debug, const char *address, guint *index)
{
    const guint64 value = debug_address_value (address);
    guint i;

    for (i = 0; value != 0 && i < debug->address_breakpoints->len; i++)
    {
        debug_address_breakpoint_t *bp = g_ptr_array_index (debug->address_breakpoints, i);

        if (debug_address_value (bp->address) == value)
        {
            if (index != NULL)
                *index = i;
            return bp;
        }
    }
    return NULL;
}

/* A line of a source file, without its indentation; NULL when it cannot be read */
static const char *
debug_source_line (debugger_t *debug, const char *file, long line)
{
    if (g_strcmp0 (debug->source_file, file) != 0)
    {
        char *text = NULL;

        g_clear_pointer (&debug->source_lines, g_strfreev);
        g_free (debug->source_file);
        debug->source_file = g_strdup (file);
        if (file != NULL && g_file_get_contents (file, &text, NULL, NULL))
            debug->source_lines = g_strsplit (text, "\n", -1);
        g_free (text);
    }
    if (debug->source_lines == NULL || line <= 0
        || line > (long) g_strv_length (debug->source_lines))
        return NULL;
    return debug->source_lines[line - 1] + strspn (debug->source_lines[line - 1], " \t");
}

/* What the window shows: each source line before its instructions, the instructions with the
   mark of the program counter and those of the breakpoints */
static GPtrArray *
debug_disasm_rows (debugger_t *debug)
{
    GPtrArray *rows = g_ptr_array_new_with_free_func (debug_disasm_row_free);
    const guint64 pc = debug_address_value (debug->disasm_address);
    const char *file = NULL;
    long line = 0;
    guint i;

    for (i = 0; i < debug->instructions->len; i++)
    {
        const debug_instruction_t *insn = g_ptr_array_index (debug->instructions, i);
        const gboolean here = debug_address_value (insn->address) == pc;
        const gboolean bp = debug_address_breakpoint_at (debug, insn->address, NULL) != NULL;
        const char *glyph = here && bp ? debug->glyphs[DEBUG_MARK_EXEC_BREAKPOINT]
            : here                     ? debug->glyphs[DEBUG_MARK_EXEC]
            : bp                       ? debug->glyphs[DEBUG_MARK_BREAKPOINT]
                                       : " ";
        debug_disasm_row_t *row;

        if (insn->file != NULL && (g_strcmp0 (insn->file, file) != 0 || insn->line != line))
        {
            const char *text = debug_source_line (debug, insn->file, insn->line);

            file = insn->file;
            line = insn->line;
            row = g_new0 (debug_disasm_row_t, 1);
            row->source = TRUE;
            row->index = i;
            row->text =
                g_strdup_printf ("%s:%ld  %s", x_basename (file), line, text != NULL ? text : "");
            g_ptr_array_add (rows, row);
        }
        row = g_new0 (debug_disasm_row_t, 1);
        row->index = i;
        if (insn->func != NULL)
            row->text = g_strdup_printf ("%s %s <%s+%ld>  %s", glyph, insn->address, insn->func,
                                         insn->offset, insn->text != NULL ? insn->text : "");
        else
            row->text = g_strdup_printf ("%s %s  %s", glyph, insn->address,
                                         insn->text != NULL ? insn->text : "");
        g_ptr_array_add (rows, row);
    }
    return rows;
}

/* The cursor on an instruction, the nearest one in that direction */
static void
debug_disasm_cursor (debug_disasm_window_t *disasm, const GPtrArray *rows, int to, int step)
{
    const int last = (int) rows->len - 1;
    int i;

    to = CLAMP (to, 0, MAX (last, 0));
    for (i = to; i >= 0 && i <= last; i += step)
        if (!((debug_disasm_row_t *) g_ptr_array_index (rows, i))->source)
        {
            disasm->cursor = i;
            return;
        }
    for (i = to; i >= 0 && i <= last; i -= step)
        if (!((debug_disasm_row_t *) g_ptr_array_index (rows, i))->source)
        {
            disasm->cursor = i;
            return;
        }
    disasm->cursor = 0;
}

/* The row of the program counter, -1 when it is not among them */
static int
debug_disasm_pc_row (const debugger_t *debug, const GPtrArray *rows)
{
    const guint64 pc = debug_address_value (debug->disasm_address);
    guint i;

    for (i = 0; pc != 0 && i < rows->len; i++)
    {
        const debug_disasm_row_t *row = g_ptr_array_index (rows, i);
        const debug_instruction_t *insn = g_ptr_array_index (debug->instructions, row->index);

        if (!row->source && debug_address_value (insn->address) == pc)
            return (int) i;
    }
    return -1;
}

static void
debug_disasm_draw (debug_disasm_window_t *disasm)
{
    WEditWindow *win = &disasm->window;
    Widget *w = WIDGET (disasm);
    debugger_t *debug = disasm->debug;
    const gboolean focused = widget_get_state (w, WST_FOCUSED);
    const int color = edit_window_frame_color (win, focused);
    const int lines = MAX (1, w->rect.lines - 2);
    const int width = MAX (0, w->rect.cols - 2);
    GPtrArray *rows;
    int row;

    edit_window_draw_frame (win, color, focused);
    tty_setcolor (color);
    widget_gotoyx (w, 0, 2);
    tty_print_string (str_term_trim (_ ("[Disassembly]"), MAX (0, w->rect.cols - 10)));
    edit_window_draw_icons (win, color);
    if (debug == NULL)
        return;

    rows = debug_disasm_rows (debug);
    if (disasm->follow)
    {
        // the program counter in sight, a third of the way down
        const int pc = debug_disasm_pc_row (debug, rows);

        disasm->follow = FALSE;
        if (pc >= 0)
        {
            disasm->cursor = pc;
            disasm->top = MAX (0, pc - lines / 3);
        }
    }
    if (disasm->cursor >= (int) rows->len
        || (rows->len > 0
            && ((debug_disasm_row_t *) g_ptr_array_index (rows, disasm->cursor))->source))
        debug_disasm_cursor (disasm, rows, disasm->cursor, 1);
    if (disasm->cursor < disasm->top)
        disasm->top = disasm->cursor;
    else if (disasm->cursor >= disasm->top + lines)
        disasm->top = disasm->cursor - lines + 1;

    for (row = 0; row < lines; row++)
    {
        const int index = disasm->top + row;
        int c = EDITOR_NORMAL_COLOR;
        const char *text = "";

        if (rows->len == 0 && row == 0)
            text = debug->state == DEBUG_STOPPED ? _ ("No instructions here.")
                                                 : _ ("The program is not stopped.");
        else if (index < (int) rows->len)
        {
            const debug_disasm_row_t *r = g_ptr_array_index (rows, index);

            if (index == disasm->cursor && focused)
                c = EDITOR_MARKED_COLOR;
            else if (r->source)
                c = EDITOR_BOLD_COLOR;
            text = r->text;
        }
        tty_setcolor (EDITOR_NORMAL_COLOR);
        tty_draw_hline (w->rect.y + row + 1, w->rect.x + 1, ' ', width);
        tty_setcolor (c);
        widget_gotoyx (w, row + 1, 1);
        tty_print_string (str_fit_to_term (text, width, J_LEFT_FIT));
    }
    g_ptr_array_free (rows, TRUE);
}

/* The instruction under the cursor, NULL when there is none */
static const debug_instruction_t *
debug_disasm_current (const debug_disasm_window_t *disasm)
{
    debugger_t *debug = disasm->debug;
    GPtrArray *rows = debug_disasm_rows (debug);
    const debug_instruction_t *insn = NULL;

    if (disasm->cursor >= 0 && disasm->cursor < (int) rows->len)
    {
        const debug_disasm_row_t *row = g_ptr_array_index (rows, disasm->cursor);

        insn = g_ptr_array_index (debug->instructions, row->index);
    }
    g_ptr_array_free (rows, TRUE);
    return insn;
}

/* The moves of the cursor, Enter and F6; FALSE for a key that is none of them */
static gboolean
debug_disasm_key (debug_disasm_window_t *disasm, int key)
{
    debugger_t *debug = disasm->debug;
    GPtrArray *rows = debug_disasm_rows (debug);
    const int lines = MAX (1, WIDGET (disasm)->rect.lines - 2);
    gboolean handled = TRUE;

    switch (key)
    {
    case KEY_UP:
        debug_disasm_cursor (disasm, rows, disasm->cursor - 1, -1);
        break;
    case KEY_DOWN:
        debug_disasm_cursor (disasm, rows, disasm->cursor + 1, 1);
        break;
    case KEY_PPAGE:
        debug_disasm_cursor (disasm, rows, disasm->cursor - lines, -1);
        break;
    case KEY_NPAGE:
        debug_disasm_cursor (disasm, rows, disasm->cursor + lines, 1);
        break;
    case KEY_HOME:
        debug_disasm_cursor (disasm, rows, 0, 1);
        break;
    case KEY_END:
        debug_disasm_cursor (disasm, rows, (int) rows->len - 1, -1);
        break;
    case '\n':
    case KEY_ENTER:
    {
        const debug_instruction_t *insn = debug_disasm_current (disasm);

        // the source line of the instruction
        if (insn != NULL && debug_source_here (insn->file) && insn->line > 0)
            (void) debug->host->show_location (debug->host, insn->file, insn->line);
        else
            tty_beep ();
        break;
    }
    default:
        if (debug_command_of_key (debug, key) == DEBUG_CMD_TOGGLE_BREAKPOINT)
        {
            const debug_instruction_t *insn = debug_disasm_current (disasm);

            if (insn != NULL)
                debug_address_breakpoint_toggle (debug, insn->address);
        }
        else
            handled = FALSE;
        break;
    }
    g_ptr_array_free (rows, TRUE);
    if (handled && disasm->debug != NULL)
        widget_draw (WIDGET (disasm));
    return handled;
}

static void
debug_disasm_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    debug_disasm_window_t *disasm = (debug_disasm_window_t *) w;

    if (disasm->debug == NULL)
        return;
    switch (msg)
    {
    case MSG_MOUSE_DOWN:
    {
        GPtrArray *rows = debug_disasm_rows (disasm->debug);
        const int index = disasm->top + event->y - 1;

        widget_select (w);
        if (index >= 0 && index < (int) rows->len
            && !((debug_disasm_row_t *) g_ptr_array_index (rows, index))->source)
            disasm->cursor = index;
        g_ptr_array_free (rows, TRUE);
        widget_draw (w);
        break;
    }
    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE)
            (void) debug_disasm_key (disasm, '\n');
        break;
    case MSG_MOUSE_SCROLL_UP:
        disasm->top = MAX (0, disasm->top - 3);
        widget_draw (w);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        disasm->top += 3;
        widget_draw (w);
        break;
    default:
        break;
    }
}

static cb_ret_t
debug_disasm_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    debug_disasm_window_t *disasm = (debug_disasm_window_t *) w;

    switch (msg)
    {
    case MSG_DRAW:
        debug_disasm_draw (disasm);
        return MSG_HANDLED;
    case MSG_FOCUS:
        debug_window_buttonbar (disasm->debug, w);
        debug_disasm_draw (disasm);
        return MSG_HANDLED;
    case MSG_UNFOCUS:
        debug_disasm_draw (disasm);
        return MSG_HANDLED;
    case MSG_KEY:
        if (disasm->debug == NULL)
            return MSG_NOT_HANDLED;
        if (debug_disasm_key (disasm, parm))
            return MSG_HANDLED;
        // run to the cursor is of a line of a file
        if (debug_command_of_key (disasm->debug, parm) != DEBUG_CMD_RUN_TO_CURSOR
            && debug_run_command (disasm->debug, debug_command_of_key (disasm->debug, parm), NULL))
            return MSG_HANDLED;
        if (parm >= KEY_F (1) && parm <= KEY_F (10)
            && debug_command_of_key (disasm->debug, parm) != DEBUG_CMD_HELP)
            return MSG_HANDLED;
        return MSG_NOT_HANDLED;
    case MSG_ACTION:
        if (disasm->debug == NULL)
            return MSG_NOT_HANDLED;
        // the breakpoint of the button bar is on the instruction of the cursor
        if (debug_command (disasm->debug, parm) == DEBUG_CMD_TOGGLE_BREAKPOINT)
        {
            const debug_instruction_t *insn = debug_disasm_current (disasm);

            if (insn != NULL)
                debug_address_breakpoint_toggle (disasm->debug, insn->address);
            return MSG_HANDLED;
        }
        if (debug_command (disasm->debug, parm) != DEBUG_CMD_RUN_TO_CURSOR
            && debug_run_command (disasm->debug, debug_command (disasm->debug, parm), NULL))
            return MSG_HANDLED;
        return MSG_NOT_HANDLED;
    case MSG_CURSOR:
        widget_gotoyx (w, 1 + disasm->cursor - disasm->top, 1);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (disasm->debug != NULL)
            disasm->debug->disasm_window = NULL;
        return group_default_callback (w, sender, msg, parm, data);
    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

static char *
debug_disasm_title (const WEditWindow *win)
{
    (void) win;
    return g_strdup (_ ("Disassembly"));
}

static const edit_window_class_t debug_disasm_class = {
    .callback = debug_disasm_callback,
    .mouse_callback = debug_disasm_mouse,
    .get_title = debug_disasm_title,
    .close = debug_session_close_window,
    .min_lines = 4,
    .min_cols = 24,
};

/* The instructions the window shows came */
static void
debug_reply_disassemble (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;

    (void) data;
    g_ptr_array_unref (debug->instructions);
    debug->instructions = reply->instructions != NULL
        ? g_ptr_array_ref (reply->instructions)
        : g_ptr_array_new_with_free_func (debug_instruction_free);
    if (!reply->ok && reply->msg != NULL)
        debug_output_console (debug, reply->msg, TRUE);
    if (debug->disasm_window != NULL)
    {
        debug->disasm_window->follow = TRUE;
        widget_draw (WIDGET (debug->disasm_window));
    }
}

/* The instructions around the address of the frame, when the window is open: asked for when
   they are not those it shows already */
void
debug_disasm_refresh (debugger_t *debug)
{
    const guint64 pc = debug_address_value (debug->disasm_address);
    guint i;

    if (debug->disasm_window == NULL)
        return;
    debug->disasm_window->follow = TRUE;
    if (debug->state != DEBUG_STOPPED || pc == 0 || debug->backend == NULL
        || debug->backend->ops->disassemble == NULL)
    {
        widget_draw (WIDGET (debug->disasm_window));
        return;
    }
    for (i = 0; i < debug->instructions->len; i++)
        if (debug_address_value (
                ((debug_instruction_t *) g_ptr_array_index (debug->instructions, i))->address)
            == pc)
        {
            widget_draw (WIDGET (debug->disasm_window));
            return;
        }
    (void) debug->backend->ops->disassemble (debug->backend, debug->disasm_address,
                                             debug_reply_disassemble, NULL, NULL);
}

/* The window of the instructions, under the source; @focus: it takes the keys */
void
debug_disasm_show (debugger_t *debug, gboolean focus)
{
    debug_disasm_window_t *disasm;
    void *before = debug->host->window_current (debug->host);
    WRect area;

    if (debug->disasm_window != NULL)
    {
        debug->host->window_show (debug->host, debug->disasm_window);
        if (!focus && before != NULL && before != (void *) debug->disasm_window)
            debug->host->window_show (debug->host, before);
        debug_disasm_refresh (debug);
        return;
    }
    debug->host->window_area (debug->host, &area);
    disasm = g_new0 (debug_disasm_window_t, 1);
    edit_window_init (&disasm->window, &area, &debug_disasm_class);
    disasm->window.fullscreen = 0;
    disasm->debug = debug;
    debug->disasm_window = disasm;
    debug->host->window_add (debug->host, disasm);
    if (debug->host->window_dock_bottom != NULL)
        debug->host->window_dock_bottom (debug->host, disasm, CLAMP (area.lines * 30 / 100, 8, 20));
    if (!focus && before != NULL)
        debug->host->window_show (debug->host, before);
    // what it shows is of the stop before: asked for again
    g_ptr_array_set_size (debug->instructions, 0);
    debug_disasm_refresh (debug);
}

void
debug_address_breakpoint_free (gpointer p)
{
    debug_address_breakpoint_t *bp = (debug_address_breakpoint_t *) p;

    g_free (bp->address);
    g_free (bp->gdb_number);
    g_free (bp);
}

/* The window of the instructions and the panel show the breakpoints on instructions */
static void
debug_address_breakpoints_show (debugger_t *debug)
{
    if (debug->disasm_window != NULL)
        widget_draw (WIDGET (debug->disasm_window));
    debug_session_refresh (debug);
}

/* The debugger has taken a breakpoint on an instruction, or has refused it */
static void
debug_reply_address_breakpoint (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    for (i = 0; i < debug->address_breakpoints->len; i++)
    {
        debug_address_breakpoint_t *bp = g_ptr_array_index (debug->address_breakpoints, i);

        if (bp->pending_token != reply->request)
            continue;
        bp->pending_token = 0;
        if (reply->ok)
            bp->gdb_number = g_strdup (reply->id);
        else
        {
            if (reply->msg != NULL)
                debug_output_console (debug, reply->msg, TRUE);
            g_ptr_array_remove_index (debug->address_breakpoints, i);
        }
        debug_address_breakpoints_show (debug);
        return;
    }
}

void
debug_address_breakpoint_install (debugger_t *debug, debug_address_breakpoint_t *bp)
{
    g_clear_pointer (&bp->gdb_number, g_free);
    bp->pending_token = debug->backend->ops->break_address (
        debug->backend, bp->address, debug_reply_address_breakpoint, NULL, NULL);
}

/* A breakpoint on the instruction at @address, or none any more */
void
debug_address_breakpoint_toggle (debugger_t *debug, const char *address)
{
    debug_address_breakpoint_t *bp;
    guint i;

    bp = debug_address_breakpoint_at (debug, address, &i);
    if (bp != NULL)
    {
        if (bp->pending_token != 0)
        {
            debug_error (debug, _ ("Wait for GDB to confirm this breakpoint."));
            return;
        }
        if (bp->gdb_number != NULL && debug_alive (debug))
            (void) debug->backend->ops->break_delete (debug->backend, bp->gdb_number);
        g_ptr_array_remove_index (debug->address_breakpoints, i);
    }
    else
    {
        bp = g_new0 (debug_address_breakpoint_t, 1);
        bp->address = g_strdup (address);
        g_ptr_array_add (debug->address_breakpoints, bp);
        if (debug_session_live (debug) && debug->breakpoints_installed)
            debug_address_breakpoint_install (debug, bp);
    }
    debug_address_breakpoints_show (debug);
}

mc_ep_window_state_t
debug_disasm_state (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->disasm_window == NULL)
        return MC_EP_WINDOW_CLOSED;
    return debug->host->window_current (debug->host) == (void *) debug->disasm_window
        ? MC_EP_WINDOW_FOCUSED
        : MC_EP_WINDOW_OPEN;
}

void
debug_disasm_kind_show (void *data)
{
    debug_disasm_show ((debugger_t *) data, TRUE);
}

void
debug_disasm_kind_close (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->disasm_window != NULL)
        (void) debug->host->window_close (debug->host, debug->disasm_window);
}

void *
debug_disasm_window (void *data)
{
    return ((debugger_t *) data)->disasm_window;
}

/* Debug > Disassembly: the instructions of the frame, under the source */
mc_ep_result_t
debug_act_disassembly (void *data, void *edit)
{
    (void) edit;
    debug_disasm_show ((debugger_t *) data, TRUE);
    return MC_EPR_OK;
}
