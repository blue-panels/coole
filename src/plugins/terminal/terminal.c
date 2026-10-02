/*
   The terminal plugin of the editor: a shell in a window of the editor screen.

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
 *  \brief Source: the terminal plugin of the editor
 *
 *  A module the editor loads from its directory of plugins. Ctrl-O shows a window with a shell,
 *  the terminal of mc, at the bottom of the screen, and hides it again as it is. The window is a
 * window of the editor screen like any other: it is moved, resized, shown full screen, listed and
 * closed the same way.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/strutil.h"  // str_term_trim()
#include "lib/mcconfig.h"
#include "lib/util.h"  // MC_PTR_FREE
#include "lib/widget.h"
#include "lib/editor-plugin.h"

#include "src/keymap.h"  // mcterm_map
#include "src/editor/editwindow.h"
#include "src/mcterm/mcterm.h"
#include "src/mcterm/mcterm_cwd.h"

#include "terminal.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* The height of a new terminal window, in percent of the screen */
#define TERMINAL_HEIGHT_KEY     "window_height"
#define TERMINAL_HEIGHT_DEFAULT 25

/*** file scope type declarations ****************************************************************/

typedef struct terminal_plugin_t terminal_plugin_t;

/* The window of the terminal: a window of the editor screen that holds the terminal */
typedef struct
{
    WEditWindow window;
    WMcTerm *term;
    terminal_plugin_t *plugin;
} terminal_window_t;

/* The plugin in one editor screen */
struct terminal_plugin_t
{
    mc_editor_host_t *host;
    terminal_window_t *win;  // NULL until Ctrl-O, and after the window is destroyed
};

/*** forward declarations (file scope functions) *************************************************/

static cb_ret_t terminal_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm,
                                          void *data);
static char *terminal_window_get_title (const WEditWindow *win);
static gboolean terminal_window_is_modified (const WEditWindow *win);
static gboolean terminal_window_close (WEditWindow *win);
static gboolean terminal_window_ok_to_quit (WEditWindow *win);
static void terminal_window_scroll_bar (WEditWindow *win, gboolean vertical, long pos);

/*** file scope variables ************************************************************************/

static const edit_window_class_t terminal_window_class = {
    .callback = terminal_window_callback,
    .mouse_callback = NULL,  // the terminal takes the mouse itself
    .get_title = terminal_window_get_title,
    .is_modified = terminal_window_is_modified,
    .close = terminal_window_close,
    .ok_to_quit = terminal_window_ok_to_quit,
    .min_lines = 2 + 1,
    .min_cols = 2 + 8,
    // the history scrolls; nothing is wider than the window
    .vbar = TRUE,
    .hbar_x = 0,
    .scrolled = terminal_window_scroll_bar,
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The directory the shell is in, as it told; NULL when it did not. Caller frees */
static char *
terminal_window_cwd (const terminal_window_t *tw)
{
    const char *raw;
    char *path;
    const char *home;

    raw = mcterm_osc7_raw (tw->term);
    if (raw == NULL)
        return NULL;

    path = mcterm_osc7_uri_to_path (raw, mcterm_osc7_token (tw->term));
    if (path == NULL)
        return NULL;

    // the home directory is ~, as the shell shows it
    home = g_get_home_dir ();
    if (home != NULL && *home != '\0' && g_str_has_prefix (path, home)
        && (path[strlen (home)] == '\0' || path[strlen (home)] == G_DIR_SEPARATOR))
    {
        char *short_path;

        short_path = g_strconcat ("~", path + strlen (home), (char *) NULL);
        g_free (path);
        path = short_path;
    }

    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the shell runs a command now. A shell that does not tell where its prompt is is taken
   to run nothing: nothing could be said of it. */
static gboolean
terminal_window_busy (const terminal_window_t *tw)
{
    return (mcterm_is_alive (tw->term) && mcterm_osc7_capable (tw->term)
            && !mcterm_shell_at_prompt (tw->term));
}

/* --------------------------------------------------------------------------------------------- */

/* The title of the window: the directory of the shell and the command it runs, or what became
   of the shell. Caller frees */
static char *
terminal_window_title (const terminal_window_t *tw, int width)
{
    char *cwd, *command = NULL, *title;

    if (!mcterm_is_alive (tw->term))
        return g_strdup (_ ("Shell (exited)"));

    cwd = terminal_window_cwd (tw);

    if (terminal_window_busy (tw))
        command = mcterm_running_command (tw->term, MAX (width / 2, 8));

    if (cwd != NULL && command != NULL)
        title = g_strdup_printf ("%s: %s", cwd, command);
    else if (cwd != NULL)
        title = g_strdup (cwd);
    else if (command != NULL)
        title = g_strdup_printf ("%s: %s", _ ("Shell"), command);
    else
        title = g_strdup (_ ("Shell"));

    g_free (cwd);
    g_free (command);

    return title;
}

/* --------------------------------------------------------------------------------------------- */

/* The scrollbar of the history down the right side of the frame; nothing to show for a
   full-screen program, a filter of the output, or a history that fits */
static void
terminal_window_set_bar (terminal_window_t *tw)
{
    const WRect *w = &CONST_WIDGET (tw)->rect;
    const int rows = w->lines - 2;
    int history = 0, back = 0;

    if (!mcterm_scroll_state (tw->term, &history, &back))
        history = back = 0;
    edit_window_set_scroll (EDIT_WINDOW (tw), TRUE, (long) history + rows, rows,
                            (long) (history - back));
}

/* --------------------------------------------------------------------------------------------- */

/* The frame and the title of the window, or the top line of the screen when it is fullscreen */
static void
terminal_window_draw_frame (terminal_window_t *tw)
{
    const WEditWindow *win = &tw->window;
    const Widget *w = CONST_WIDGET (tw);
    const gboolean active = widget_get_state (w, WST_FOCUSED);
    const int color = edit_window_frame_color (win, active);
    char *title;

    if (win->fullscreen != 0)
    {
        const Widget *h = CONST_WIDGET (w->owner);
        const int cols = h->rect.cols - 7;  // the buttons at the right

        title = terminal_window_title (tw, cols);
        tty_setcolor (color);
        tty_draw_hline (h->rect.y, h->rect.x, ' ', h->rect.cols);
        widget_gotoyx (h, 0, 0);
        tty_print_string (str_term_trim (title, cols));
    }
    else
    {
        const int cols = w->rect.cols - 13;  // the corners, the brackets, the buttons and a line

        edit_window_draw_frame (win, color, active);
        title = terminal_window_title (tw, cols);
        if (cols > 0)
        {
            tty_setcolor (color);
            widget_gotoyx (w, 0, 2);
            tty_print_char ('[');
            tty_print_string (str_term_trim (title, cols));
            tty_print_char (']');
        }

        terminal_window_set_bar (tw);
        edit_window_draw_bars (EDIT_WINDOW (tw), color);
    }

    g_free (title);
    edit_window_draw_icons (win, color);
}

/* --------------------------------------------------------------------------------------------- */

/* The scrollbar of the frame has moved: the view of the history goes with it, pos rows from the
   oldest */
static void
terminal_window_scroll_bar (WEditWindow *win, gboolean vertical, long pos)
{
    terminal_window_t *tw = (terminal_window_t *) win;
    int history = 0, back = 0;

    if (vertical && mcterm_scroll_state (tw->term, &history, &back))
        (void) mcterm_scroll_by (tw->term, back - (int) (history - pos));
}

/* --------------------------------------------------------------------------------------------- */

/* The terminal takes the window but its frame; a fullscreen window has no frame */
static void
terminal_window_place_term (terminal_window_t *tw)
{
    const WRect *r = &WIDGET (tw)->rect;
    WRect tr = *r;

    if (tw->window.fullscreen == 0)
        rect_grow (&tr, -1, -1);

    widget_set_size_rect (WIDGET (tw->term), &tr);
}

/* --------------------------------------------------------------------------------------------- */

/* After the terminal has drawn itself: its title may have changed with it, and the cursor is
   put back */
static void
terminal_window_after_redraw (void *data)
{
    terminal_window_t *tw = (terminal_window_t *) data;
    Widget *w = WIDGET (tw);

    if (!widget_get_state (w, WST_VISIBLE))
        return;

    terminal_window_draw_frame (tw);

    // the cursor goes back where the window with the focus has it, not where the frame ended
    if (w->owner != NULL)
        (void) widget_update_cursor (WIDGET (w->owner));
}

/* --------------------------------------------------------------------------------------------- */

static void
terminal_window_set_buttonbar (terminal_window_t *tw)
{
    WButtonBar *bb = buttonbar_find (DIALOG (WIDGET (tw)->owner));
    const global_keymap_t *keymap = WIDGET (WIDGET (tw)->owner)->keymap;
    Widget *term = WIDGET (tw->term);

    if (bb == NULL)
        return;

    buttonbar_set_label (bb, 1, Q_ ("ButtonBar|Help"), keymap, NULL);
    buttonbar_set_label (bb, 2, Q_ ("ButtonBar|Copy"), mcterm_map, term);
    buttonbar_set_label (bb, 3, Q_ ("ButtonBar|Mark"), mcterm_map, term);
    buttonbar_set_label (bb, 4, Q_ ("ButtonBar|Filter"), mcterm_map, term);
    buttonbar_set_label (bb, 5, Q_ ("ButtonBar|UnFilt"), mcterm_map, term);
    buttonbar_set_label (bb, 6, Q_ ("ButtonBar|ClrAll"), mcterm_map, term);
    buttonbar_set_label (bb, 7, "", NULL, NULL);
    buttonbar_set_label (bb, 8, "", NULL, NULL);
    buttonbar_set_label (bb, 9, Q_ ("ButtonBar|PullDn"), keymap, NULL);
    buttonbar_set_label (bb, 10, Q_ ("ButtonBar|Quit"), keymap, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
terminal_window_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    terminal_window_t *tw = (terminal_window_t *) w;

    switch (msg)
    {
    case MSG_DRAW:
        terminal_window_draw_frame (tw);
        return group_default_callback (w, sender, msg, parm, data);

    case MSG_RESIZE:
        (void) group_default_callback (w, sender, msg, parm, data);
        terminal_window_place_term (tw);
        return MSG_HANDLED;

    case MSG_FOCUS:
        terminal_window_set_buttonbar (tw);
        MC_FALLTHROUGH;
    case MSG_UNFOCUS:
        // the frame shows whether the window has the focus
        if (widget_get_state (w, WST_VISIBLE))
            terminal_window_draw_frame (tw);
        return MSG_HANDLED;

    case MSG_KEY:
        /* The key goes to the terminal as a key, the way the screen of mc gives it: its keymap
           first, then the shell. Through the group it would be offered to the terminal as a
           hotkey first, which the terminal types into the shell as it is. */
        return send_message (tw->term, NULL, MSG_KEY, parm, NULL);

    case MSG_DESTROY:
        // the window goes, and the shell with it
        if (tw->plugin != NULL)
            tw->plugin->win = NULL;
        return group_default_callback (w, sender, msg, parm, data);

    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
terminal_window_get_title (const WEditWindow *win)
{
    const terminal_window_t *tw = (const terminal_window_t *) win;
    char *title, *item;

    title = terminal_window_title (tw, COLS);
    item = g_strdup_printf ("%s: %s", _ ("Terminal"), title);
    g_free (title);

    return item;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
terminal_window_is_modified (const WEditWindow *win)
{
    return terminal_window_busy ((const terminal_window_t *) win);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the shell may go with the command it runs */
static gboolean
terminal_window_ok_to_quit (WEditWindow *win)
{
    const terminal_window_t *tw = (const terminal_window_t *) win;
    char *command, *text;
    int answer;

    if (!terminal_window_busy (tw))
        return TRUE;

    command = mcterm_running_command (tw->term, COLS / 2);
    text = g_strdup_printf (_ ("The shell of the terminal runs\n%s\nStop it?"),
                            command != NULL ? command : _ ("a command"));
    answer = query_dialog (_ ("Terminal"), text, D_NORMAL, 2, _ ("&Yes"), _ ("&No"));
    g_free (text);
    g_free (command);

    return answer == 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
terminal_window_close (WEditWindow *win)
{
    if (!terminal_window_ok_to_quit (win))
        return FALSE;

    // the file window over the terminal takes the screen again
    edit_window_give_room_back (win);
    edit_window_destroy (win);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A new window at the bottom of the screen, or as @old is, with a shell in @start_dir */
static terminal_window_t *
terminal_window_new (terminal_plugin_t *tp, const terminal_window_t *old, const char *start_dir)
{
    terminal_window_t *tw;
    WRect r, tr;

    if (old != NULL)
        r = CONST_WIDGET (old)->rect;
    else
    {
        WRect a;
        int percent;

        tp->host->window_area (tp->host, &a);
        percent = mc_config_get_int (mc_global.main_config, CONFIG_TERMINAL_SECTION,
                                     TERMINAL_HEIGHT_KEY, TERMINAL_HEIGHT_DEFAULT);
        percent = CLAMP (percent, 10, 100);
        r = a;
        r.lines = MAX (terminal_window_class.min_lines, a.lines * percent / 100);
        r.y = a.y + a.lines - r.lines;
    }

    tw = g_new0 (terminal_window_t, 1);
    edit_window_init (&tw->window, &r, &terminal_window_class);
    if (old == NULL)
        tw->window.fullscreen = 0;
    else
    {
        tw->window.fullscreen = old->window.fullscreen;
        tw->window.loc_prev = old->window.loc_prev;
    }
    tw->plugin = tp;

    // placed relative to the window: inside the frame, or all of it when it is fullscreen
    if (tw->window.fullscreen != 0)
        rect_init (&tr, 0, 0, r.lines, r.cols);
    else
        rect_init (&tr, 1, 1, r.lines - 2, r.cols - 2);
    tw->term = mcterm_new (&tr, start_dir);
    if (tw->term == NULL)
    {
        send_message (tw, NULL, MSG_DESTROY, 0, NULL);
        g_free (tw);
        return NULL;
    }

    group_add_widget_autopos (GROUP (tw), WIDGET (tw->term), WPOS_KEEP_ALL, NULL);
    // there is no command line of the editor: the shell's own line editor takes the typing
    mcterm_set_typing_elsewhere (tw->term, FALSE);
    mcterm_set_scroll_allowed (tw->term, TRUE);
    mcterm_set_after_redraw_callback (tw->term, terminal_window_after_redraw, tw);

    return tw;
}

/* --------------------------------------------------------------------------------------------- */

/* The directory of the file being edited, where the shell starts. Caller frees */
static char *
terminal_start_dir (terminal_plugin_t *tp, void *edit)
{
    char *file, *dir;

    if (edit == NULL || tp->host->get_current_file == NULL)
        return NULL;

    file = tp->host->get_current_file (tp->host, edit);
    if (file == NULL)
        return NULL;

    dir = g_path_get_dirname (file);
    g_free (file);

    return dir;
}

/* --------------------------------------------------------------------------------------------- */

/* Ctrl-O: the window of the terminal shown as it was, or hidden as it is */
static mc_ep_result_t
terminal_toggle (terminal_plugin_t *tp, void *edit)
{
    terminal_window_t *tw = tp->win;

    if (tw != NULL && !mcterm_is_alive (tw->term))
    {
        terminal_window_t *fresh;
        char *dir;

        // the shell is gone: a new one in its place
        dir = terminal_start_dir (tp, edit);
        fresh = terminal_window_new (tp, tw, dir);
        g_free (dir);
        if (fresh == NULL)
        {
            tp->host->message (tp->host, D_ERROR, _ ("Terminal"), _ ("Cannot start the shell"));
            return MC_EPR_FAILED;
        }

        tp->host->window_give_room_back (tp->host, tw);
        edit_window_destroy (&tw->window);
        tp->win = fresh;
        tp->host->window_add (tp->host, fresh);
        tp->host->window_make_room (tp->host, fresh);
        return MC_EPR_OK;
    }

    if (tw == NULL)
    {
        char *dir;

        dir = terminal_start_dir (tp, edit);
        tw = terminal_window_new (tp, NULL, dir);
        g_free (dir);
        if (tw == NULL)
        {
            tp->host->message (tp->host, D_ERROR, _ ("Terminal"), _ ("Cannot start the shell"));
            return MC_EPR_FAILED;
        }

        tp->win = tw;
        tp->host->window_add (tp->host, tw);
        // the fullscreen file window goes above the terminal
        tp->host->window_make_room (tp->host, tw);
    }
    else if (!widget_get_state (WIDGET (tw), WST_VISIBLE))
    {
        tp->host->window_show (tp->host, tw);
        tp->host->window_make_room (tp->host, tw);
    }
    else if (!widget_get_state (WIDGET (tw), WST_FOCUSED))
        // on the screen, under another window: brought up
        tp->host->window_show (tp->host, tw);
    else
    {
        // the file window takes the screen again, as it was before the terminal came
        tp->host->window_give_room_back (tp->host, tw);
        tp->host->window_hide (tp->host, tw);
    }

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static void *
terminal_plugin_open (mc_editor_host_t *host, void *editor_dialog)
{
    terminal_plugin_t *tp;

    (void) editor_dialog;

    if (host->window_add == NULL || host->window_make_room == NULL)
        return NULL;

    mcterm_load_options ();
    /* The editor has no panels to show: an mc started in the terminal runs there, and does not
       send the editor the SIGUSR1 that asks for them. */
    mcterm_set_nested_mc_request (FALSE);

    tp = g_new0 (terminal_plugin_t, 1);
    tp->host = host;

    return tp;
}

/* --------------------------------------------------------------------------------------------- */

static void
terminal_plugin_close (void *plugin_data)
{
    // the editor has destroyed the window before, and the shell with it
    g_free (plugin_data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
terminal_plugin_activate (void *plugin_data, void *edit)
{
    return terminal_toggle ((terminal_plugin_t *) plugin_data, edit);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
terminal_plugin_handle_action (void *plugin_data, long command, void *edit)
{
    if (command != CK_Shell)
        return MC_EPR_NOT_SUPPORTED;

    return terminal_toggle ((terminal_plugin_t *) plugin_data, edit);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t terminal_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "terminal",
    .display_name = "Terminal",
    .flags = MC_EPF_HAS_MENU,
    .open = terminal_plugin_open,
    .close = terminal_plugin_close,
    .activate = terminal_plugin_activate,
    .handle_action = terminal_plugin_handle_action,
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The entry of the module, which the editor calls when it loads it */
const mc_editor_plugin_t *
mc_editor_plugin_register (void)
{
    return &terminal_plugin;
}

/* --------------------------------------------------------------------------------------------- */
