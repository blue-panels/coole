/*
   Execution routines for GNU Midnight Commander

   Copyright (C) 2003-2025
   Free Software Foundation, Inc.

   Written by:
   Slava Zanko <slavazanko@gmail.com>, 2013

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

/** \file  execute.c
 *  \brief Source: execution routines
 */

#include <config.h>

#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>  // geteuid()

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/tty/key.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "setup.h"  // clear_before_exec

#include "execute.h"

/*** global variables ****************************************************************************/

int pause_after_run = pause_on_dumb_terminals;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

MC_MOCKABLE void do_execute (const char *shell, const char *command, int flags);
MC_MOCKABLE void do_executev (const char *shell, int flags, char *const argv[]);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

static void
edition_post_exec (void)
{
    tty_enter_ca_mode ();

    // FIXME: Missing on slang endwin?
    tty_reset_prog_mode ();
    tty_flush_input ();

    tty_keypad (TRUE);
    tty_raw_mode ();
    channels_up ();
    enable_mouse ();
    enable_bracketed_paste ();
    if (mc_global.tty.alternate_plus_minus)
        application_keypad_mode ();
}

/* --------------------------------------------------------------------------------------------- */

static void
edition_pre_exec (void)
{
    if (clear_before_exec)
        tty_clear_screen ();
    else
    {
        if (!mc_global.tty.xterm_flag)
            printf ("\n\n");
    }

    channels_down ();
    disable_mouse ();
    disable_bracketed_paste ();

    tty_reset_shell_mode ();
    tty_keypad (FALSE);
    tty_reset_screen ();

    numeric_keypad_mode ();

    /* on xterms: maybe endwin did not leave the terminal on the shell
     * screen page: do it now.
     *
     * Do not move this before endwin: in some systems rmcup includes
     * a call to clear screen, so it will end up clearing the shell screen.
     */
    tty_exit_ca_mode ();
}

/* --------------------------------------------------------------------------------------------- */

static void
do_suspend_cmd (void)
{
    pre_exec ();

#ifdef SIGTSTP
    {
        struct sigaction sigtstp_action;

        memset (&sigtstp_action, 0, sizeof (sigtstp_action));
        /* Make sure that the SIGTSTP below will suspend us directly,
           without calling ncurses' SIGTSTP handler; we *don't* want
           ncurses to redraw the screen immediately after the SIGCONT */
        my_sigaction (SIGTSTP, &startup_handler, &sigtstp_action);

        kill (getpid (), SIGTSTP);

        // Restore previous SIGTSTP action
        my_sigaction (SIGTSTP, &sigtstp_action, NULL);
    }
#endif

    edition_post_exec ();
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
do_executev (const char *shell, int flags, char *const argv[])
{
    pre_exec ();

    if ((flags & EXECUTE_INTERNAL) == 0 && argv != NULL && *argv != NULL)
    {
        printf ("%s%s\n", (geteuid () == 0) ? "# " : "$ ", *argv);
        fflush (stdout);
    }
    my_systemv_flags (flags, shell, argv);

    if ((flags & EXECUTE_INTERNAL) == 0)
    {
        if ((pause_after_run == pause_always
             || (pause_after_run == pause_on_dumb_terminals && !mc_global.tty.xterm_flag))
            && quit == 0)
        {
            printf ("%s", _ ("Press any key to continue..."));
            fflush (stdout);
            tty_raw_mode ();
            get_key_code (0);
            printf ("\r\n");
            fflush (stdout);
        }
    }

    edition_post_exec ();

    do_refresh ();
}

/* --------------------------------------------------------------------------------------------- */

void
do_execute (const char *shell, const char *command, int flags)
{
    GPtrArray *args_array;

    args_array = g_ptr_array_new ();
    g_ptr_array_add (args_array, (char *) command);
    g_ptr_array_add (args_array, NULL);

    do_executev (shell, flags, (char *const *) args_array->pdata);

    g_ptr_array_free (args_array, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/** Set up the terminal before executing a program */

void
pre_exec (void)
{
    edition_pre_exec ();
}

/* --------------------------------------------------------------------------------------------- */
/* Executes a command */

void
shell_execute (const char *command, int flags)
{
    char *cmd = NULL;

    if ((flags & EXECUTE_HIDE) != 0)
    {
        cmd = g_strconcat (" ", command, (char *) NULL);
        flags ^= EXECUTE_HIDE;
    }

    do_execute (mc_global.shell->path, cmd != NULL ? cmd : command, flags | EXECUTE_AS_SHELL);

    g_free (cmd);
}

/* --------------------------------------------------------------------------------------------- */

void
toggle_terminal (void)
{
    static gboolean message_flag = TRUE;

    SIG_ATOMIC_VOLATILE_T was_sigwinch = 0;

    if (!(mc_global.tty.xterm_flag || output_starts_shell))
    {
        if (message_flag)
            message (D_ERROR, MSG_ERROR, _ ("Not an xterm;\nthe terminal cannot be shown."));
        message_flag = FALSE;
        return;
    }

    channels_down ();
    disable_mouse ();
    disable_bracketed_paste ();
    if (clear_before_exec)
        tty_clear_screen ();
    if (mc_global.tty.alternate_plus_minus)
        numeric_keypad_mode ();
#ifndef HAVE_SLANG
    /* With slang we don't want any of this, since there
     * is no raw_mode supported
     */
    tty_reset_shell_mode ();
#endif
    tty_noecho ();
    tty_keypad (FALSE);
    tty_reset_screen ();
    tty_exit_ca_mode ();
    tty_raw_mode ();

    if (output_starts_shell)
    {
        fprintf (stderr, _ ("Type 'exit' to return to the %s"), PACKAGE_NAME);
        fputs ("\n\r\n\r", stderr);

        my_system (EXECUTE_INTERNAL, mc_global.shell->path, NULL);
    }
    else
        get_key_code (0);

    tty_enter_ca_mode ();

    tty_reset_prog_mode ();
    tty_keypad (TRUE);

    enable_mouse ();
    enable_bracketed_paste ();
    channels_up ();
    if (mc_global.tty.alternate_plus_minus)
        application_keypad_mode ();

    was_sigwinch = tty_flush_winch ();

    if (was_sigwinch != 0 || tty_got_winch ())
        dialog_change_screen_size ();
    else
        repaint_screen ();
}

/* --------------------------------------------------------------------------------------------- */

/* event callback */
gboolean
execute_suspend (const gchar *event_group_name, const gchar *event_name, gpointer init_data,
                 gpointer data)
{
    (void) event_group_name;
    (void) event_name;
    (void) init_data;
    (void) data;

    do_suspend_cmd ();
    do_refresh ();

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
