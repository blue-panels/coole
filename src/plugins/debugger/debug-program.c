/*
   The debugger plugin: the terminal of the program, its tab Program, and the console.

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
#ifdef ENABLE_MCTERM
#include <sys/ioctl.h>
#ifdef __linux__
#include <sys/timerfd.h>
#endif
#include <termios.h>
#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#endif

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

static GVariant *
debug_viewer_call (debugger_t *debug, const char *method, GVariant *args)
{
    GError *error = NULL;
    GVariant *reply;

    reply = debug->host->service_call (debug->host, "viewer", method, args, &error);
    if (error != NULL)
        g_error_free (error);
    return reply;
}

static void
debug_text_show (debugger_t *debug, const char *title, const char *content, gint64 *window_id)
{
    GVariantDict dict;
    GVariant *reply;

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "text", "s", content);
    if (*window_id == 0)
    {
        g_variant_dict_insert (&dict, "title", "s", title);
        // under the source, the panel of the debugger keeping the right
        g_variant_dict_insert (&dict, "place", "s", "bottom");
        g_variant_dict_insert (&dict, "size", "i", 25);
        g_variant_dict_insert (&dict, "focus", "b", FALSE);
        reply = debug_viewer_call (debug, "open", g_variant_dict_end (&dict));
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "id", "x", window_id);
            g_variant_unref (reply);
        }
        return;
    }

    g_variant_dict_insert (&dict, "id", "x", *window_id);
    reply = debug_viewer_call (debug, "set_text", g_variant_dict_end (&dict));
    if (reply != NULL)
        g_variant_unref (reply);
    else
        *window_id = 0;
}

void
debug_output_show (debugger_t *debug)
{
    GVariantDict dict;
    GVariant *reply;
    gint64 lines = 1;
    const char *p;

    debug_text_show (debug, _ ("Debug console"), debug->console->str, &debug->console_window);
    if (debug->console_window == 0)
        return;
    // the last of it in sight
    for (p = debug->console->str; *p != '\0'; p++)
        if (*p == '\n' && p[1] != '\0')
            lines++;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", debug->console_window);
    g_variant_dict_insert (&dict, "line", "x", lines);
    reply = debug_viewer_call (debug, "scroll_to", g_variant_dict_end (&dict));
    if (reply != NULL)
        g_variant_unref (reply);
}

static void
debug_text_raise (debugger_t *debug, gint64 window_id)
{
    GVariantDict dict;
    GVariant *reply;

    if (window_id == 0)
        return;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", window_id);
    g_variant_dict_insert (&dict, "focus", "b", TRUE);
    reply = debug_viewer_call (debug, "show", g_variant_dict_end (&dict));
    if (reply != NULL)
        g_variant_unref (reply);
}

void
debug_output_append (debugger_t *debug, const char *text_value)
{
    // the output of the program and what GDB says go to one console, as they come
    debug_output_console (debug, text_value, FALSE);
}

/* What GDB says of itself: kept for the console, @line ends a line of its own */
void
debug_output_console (debugger_t *debug, const char *text_value, gboolean line)
{
    char *valid = g_utf8_make_valid (text_value, -1);

    g_string_append (debug->console, valid);
    g_free (valid);
    if (line && debug->console->len > 0 && debug->console->str[debug->console->len - 1] != '\n')
        g_string_append_c (debug->console, '\n');
    if (debug->console->len > 100000)
        g_string_erase (debug->console, 0, debug->console->len - 100000);
    if (debug->console_window != 0 || debug_session_live (debug))
    {
        const gboolean made = debug->console_window == 0;

        debug_output_show (debug);
        // the console made while the program runs does not hide its terminal
        if (made && debug->state != DEBUG_STOPPED)
            debug_program_show (debug);
    }
}

void
debug_pty_close (debugger_t *debug)
{
    if (debug->pty_master >= 0)
    {
        delete_select_channel (debug->pty_master);
        close (debug->pty_master);
        debug->pty_master = -1;
    }
    if (debug->pty_slave >= 0)
    {
        close (debug->pty_slave);
        debug->pty_slave = -1;
    }
    g_clear_pointer (&debug->pty_name, g_free);
    debug->program_terminal = FALSE;
}

/* The tab Program in front, the focus where it is or, with @focus, there: the program runs,
   writes there and may wait for what is typed there, which the console would hide */
static void
debug_program_raise (debugger_t *debug, gboolean focus)
{
    GVariantDict args;
    GVariant *reply;

    if (!debug->program_terminal)
        return;
    g_variant_dict_init (&args, NULL);
    g_variant_dict_insert (&args, "focus", "b", focus);
    reply = debug->host->service_call (debug->host, "terminal", "show_program",
                                       g_variant_dict_end (&args), NULL);
    if (reply != NULL)
        g_variant_unref (reply);
}

void
debug_program_show (debugger_t *debug)
{
    debug_program_raise (debug, FALSE);
}

#ifdef __linux__
/* The program still runs a while after a continue or a step: what is typed is for it, its
   question maybe, and the keys of the tab work as in a file when it stops */
static int
debug_run_timer_ready (int fd, void *data)
{
    debugger_t *debug = (debugger_t *) data;
    guint64 count;

    if (read (fd, &count, sizeof (count)) < 0 && errno != EAGAIN && errno != EINTR)
        return 0;
    if (debug->state == DEBUG_RUNNING)
        debug_program_raise (debug, TRUE);
    return 0;
}
#endif

/* The timer of a run, @arm at its start, not at its stop; with no timerfd the tab takes the
   focus at the start */
void
debug_run_timer (debugger_t *debug, gboolean arm)
{
#ifdef __linux__
    struct itimerspec when = { { 0, 0 }, { 0, 0 } };

    if (!debug->program_terminal)
        return;
    if (debug->run_timer < 0 && arm)
    {
        debug->run_timer = timerfd_create (CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (debug->run_timer < 0)
        {
            debug_program_raise (debug, TRUE);
            return;
        }
        add_select_channel (debug->run_timer, debug_run_timer_ready, debug);
    }
    if (debug->run_timer < 0)
        return;
    if (arm)
        when.it_value.tv_nsec = 300 * 1000 * 1000;
    (void) timerfd_settime (debug->run_timer, 0, &when, NULL);
#else
    if (arm)
        debug_program_raise (debug, TRUE);
#endif
}

/* Whether the tab Program has the focus */
gboolean
debug_program_current (debugger_t *debug)
{
    GVariant *reply;
    gboolean current = FALSE;

    if (!debug->program_terminal)
        return FALSE;
    reply =
        debug->host->service_call (debug->host, "terminal", "program_current",
                                   g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0), NULL);
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "current", "b", &current);
        g_variant_unref (reply);
    }
    return current;
}

#ifdef ENABLE_MCTERM
static int
debug_pty_ready (int fd, void *data)
{
    debugger_t *debug = (debugger_t *) data;
    char buf[4096];
    ssize_t n = read (fd, buf, sizeof (buf) - 1);

    if (n > 0)
    {
        buf[n] = '\0';
        debug_output_append (debug, buf);
        tty_refresh ();
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
        debug_pty_close (debug);
    return 0;
}

gboolean
debug_pty_open (debugger_t *debug)
{
    char name[256];
    struct winsize size = { 24, 80, 0, 0 };

    if (openpty (&debug->pty_master, &debug->pty_slave, name, NULL, &size) < 0)
        return FALSE;
    debug->pty_name = g_strdup (name);
    (void) fcntl (debug->pty_master, F_SETFL, fcntl (debug->pty_master, F_GETFL) | O_NONBLOCK);
    add_select_channel (debug->pty_master, debug_pty_ready, debug);
    return TRUE;
}
#endif

#ifdef ENABLE_MCTERM
/* The terminal window of the program, from the plugin terminal: its tty is the program's; FALSE
   when the plugin is not there */
gboolean
debug_terminal_open (debugger_t *debug)
{
    GVariant *reply;
    GVariantDict args;
    const char *tty = NULL;

    // the keys of the tab go to the debugger first: those to leave it, those to step when stopped
    g_variant_dict_init (&args, NULL);
    if (debug->service)
        g_variant_dict_insert (&args, "keys_to", "s", DEBUG_SERVICE);
    reply = debug->host->service_call (debug->host, "terminal", "program",
                                       g_variant_dict_end (&args), NULL);
    if (reply == NULL)
        return FALSE;
    if (g_variant_lookup (reply, "tty", "&s", &tty))
        debug->pty_name = g_strdup (tty);
    g_variant_unref (reply);
    debug->program_terminal = debug->pty_name != NULL;
    return debug->pty_name != NULL;
}
#endif

/* What the debugger answers to a command typed for it: its words come on the console stream */
static void
debug_reply_console (void *ui, const debug_reply_t *reply, void *data)
{
    (void) data;
    if (!reply->ok && reply->msg != NULL)
        debug_output_console ((debugger_t *) ui, reply->msg, TRUE);
}

/* A command of GDB itself, typed in: what the panel does not have */
void
debug_gdb_command (debugger_t *debug)
{
    char *command, *echo;

    if (!debug_alive (debug))
    {
        debug_error (debug, _ ("GDB is not running: Start the program first."));
        return;
    }
    command =
        input_dialog (_ ("GDB command"), _ ("Command:"), "gdb-command", "", INPUT_COMPLETE_NONE);
    if (command == NULL || *g_strstrip (command) == '\0')
    {
        g_free (command);
        return;
    }
    echo = g_strdup_printf ("(gdb) %s\n", command);
    debug_output_console (debug, echo, FALSE);
    g_free (echo);
    (void) debug->backend->ops->console (debug->backend, command, debug_reply_console, NULL, NULL);
    g_free (command);
}

/* A key of the tab Program: the one to the panel; and while the program is stopped, when it reads
   nothing, those that run it on.  The others are the program's */
gboolean
debug_program_key (debugger_t *debug, int key)
{
    const int cmd = debug_command_of_key (debug, key);

    if (cmd == DEBUG_CMD_PANEL)
        return debug_run_command (debug, cmd, NULL);
    if (debug->state != DEBUG_STOPPED)
        return FALSE;
    switch (cmd)
    {
    case DEBUG_CMD_START_CONTINUE:
    case DEBUG_CMD_STEP_INTO:
    case DEBUG_CMD_STEP_OVER:
    case DEBUG_CMD_STEP_OUT:
    case DEBUG_CMD_STEP_INSTRUCTION:
    case DEBUG_CMD_NEXT_INSTRUCTION:
    case DEBUG_CMD_STOP:
        return debug_run_command (debug, cmd, NULL);
    default:
        return FALSE;
    }
}

mc_ep_result_t
debug_show_output (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    debug_output_show (debug);
    debug_text_raise (debug, debug->console_window);
    return MC_EPR_OK;
}

mc_ep_result_t
debug_send_input (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *line;
    size_t len, sent = 0;

    (void) edit;
    if (debug->pty_master < 0 || debug->state != DEBUG_RUNNING)
        return MC_EPR_FAILED;
    line = input_dialog (_ ("Program input"), _ ("Send a line to the program:"), NULL, "",
                         INPUT_COMPLETE_NONE);
    if (line == NULL)
        return MC_EPR_FAILED;
    len = strlen (line);
    while (sent < len)
    {
        ssize_t n = write (debug->pty_master, line + sent, len - sent);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        sent += (size_t) n;
    }
    if (sent == len && write (debug->pty_master, "\n", 1) != 1)
        sent = 0;
    g_free (line);
    return sent == len ? MC_EPR_OK : MC_EPR_FAILED;
}

mc_ep_window_state_t
debug_console_state (void *data)
{
    debugger_t *debug = (debugger_t *) data;
    GVariantDict dict;
    GVariant *reply;
    gboolean visible = FALSE, focused = FALSE;

    if (debug->console_window == 0)
        return MC_EP_WINDOW_CLOSED;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", debug->console_window);
    reply = debug_viewer_call (debug, "info", g_variant_dict_end (&dict));
    if (reply == NULL)
    {
        // closed by the user
        debug->console_window = 0;
        return MC_EP_WINDOW_CLOSED;
    }
    (void) g_variant_lookup (reply, "visible", "b", &visible);
    (void) g_variant_lookup (reply, "focused", "b", &focused);
    g_variant_unref (reply);
    return !visible ? MC_EP_WINDOW_CLOSED : focused ? MC_EP_WINDOW_FOCUSED : MC_EP_WINDOW_OPEN;
}

void
debug_console_show (void *data)
{
    debugger_t *debug = (debugger_t *) data;
    GVariantDict dict;
    GVariant *reply;

    (void) debug_show_output (data, NULL);
    if (debug->console_window == 0)
        return;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", debug->console_window);
    g_variant_dict_insert (&dict, "focus", "b", TRUE);
    reply = debug_viewer_call (debug, "show", g_variant_dict_end (&dict));
    if (reply != NULL)
        g_variant_unref (reply);
}

void
debug_console_close (void *data)
{
    debugger_t *debug = (debugger_t *) data;
    GVariantDict dict;
    GVariant *reply;

    if (debug->console_window == 0)
        return;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", debug->console_window);
    reply = debug_viewer_call (debug, "close", g_variant_dict_end (&dict));
    if (reply != NULL)
        g_variant_unref (reply);
    debug->console_window = 0;
}

void *
debug_console_window (void *data)
{
    debugger_t *debug = (debugger_t *) data;
    GVariantDict dict;
    GVariant *reply;
    guint64 window = 0;

    if (debug_console_state (data) == MC_EP_WINDOW_CLOSED)
        return NULL;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", debug->console_window);
    reply = debug_viewer_call (debug, "info", g_variant_dict_end (&dict));
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "window", "t", &window);
        g_variant_unref (reply);
    }
    return (void *) (gsize) window;
}

mc_ep_result_t
debug_act_gdb_command (void *data, void *edit)
{
    (void) edit;
    debug_gdb_command ((debugger_t *) data);
    return MC_EPR_OK;
}
