/*
   Main program of coole

   Copyright (C) 1994-2025
   Free Software Foundation, Inc.

   Written by:
   Miguel de Icaza, 1994, 1995, 1996, 1997
   Janne Kukonlehto, 1994, 1995
   Norbert Warmuth, 1997

   This file is part of coole,
   a text editor based on GNU Midnight Commander.

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

/** \file main.c
 *  \brief Source: this is a main module
 */

#include <config.h>

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/global.h"

#include "lib/event.h"
#include "lib/tty/tty.h"
#include "lib/tty/key.h"    // For init_key()
#include "lib/tty/mouse.h"  // init_mouse()
#include "lib/skin.h"
#include "lib/fileloc.h"
#include "lib/mcconfig.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/extension-runtime.h"
#include "lib/widget.h"

#include "editor/edit.h"  // edit_files(), edit_arg_free()

#include "events_init.h"
#include "args.h"
#include "runtime-host.h"

#include "keymap.h"
#include "setup.h"  // load_setup()

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/** POSIX version.  The only version we support.  */
static void
OS_Setup (void)
{
    mc_shell_init ();

    // This is the directory, where coole was installed, on Unix this is DATADIR
    // and can be overridden by the COOLE_DATADIR environment variable
    const char *datadir_env = g_getenv ("COOLE_DATADIR");

    if (datadir_env != NULL)
        mc_global.sysconfig_dir = g_strdup (datadir_env);
    else
        mc_global.sysconfig_dir = g_strdup (SYSCONFDIR);

    mc_global.share_data_dir = g_strdup (DATADIR);
}

/* --------------------------------------------------------------------------------------------- */

static void
free_macros (void)
{
    guint i;

    if (macros_list == NULL)
        return;

    for (i = 0; i < macros_list->len; i++)
    {
        macros_t *macros;

        macros = &g_array_index (macros_list, struct macros_t, i);
        if (macros != NULL && macros->macro != NULL)
            (void) g_array_free (macros->macro, TRUE);
    }
    (void) g_array_free (macros_list, TRUE);
    macros_list = NULL;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Run the editor on the files of the command line.
 *
 * @return TRUE if the editor ran, FALSE otherwise
 */

static gboolean
run_editor (void)
{
    gboolean ret;

    edit_stack_init ();

    tty_display_8bit (TRUE);

    const int baudrate = tty_baudrate ();
    if ((baudrate > 0 && baudrate < 9600) || mc_global.tty.slow_terminal)
        verbose = FALSE;

    ret = edit_files (mc_args__edit_files);
    events_publish_runtime_shutdown ("quit");

    // Program end
    mc_global.shutdown = TRUE;
    dialog_switch_shutdown ();

    if (auto_save_setup)
        (void) save_setup ();

    edit_stack_free ();

    tty_clear_screen ();

    return ret;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char *argv[])
{
    GError *mcerror = NULL;
    int exit_code = EXIT_FAILURE;
    const char *tmpdir = NULL;

#ifdef HAVE_SETLOCALE
    (void) setlocale (LC_ALL, "");
#endif
    (void) bindtextdomain (PACKAGE, LOCALEDIR);
    (void) textdomain (PACKAGE);

    // do this before args parsing
    str_init_strings (NULL);

    if (!mc_args_parse (&argc, &argv, PACKAGE, &mcerror))
    {
    startup_exit_falure:
        fprintf (stderr, _ ("Failed to run:\n%s\n"), mcerror->message);
        g_error_free (mcerror);
    startup_exit_ok:
        mc_shell_deinit ();
        str_uninit_strings ();
        return exit_code;
    }

    // do this before mc_args_show_info () to view paths in the --datadir-info output
    OS_Setup ();

    if (!g_path_is_absolute (mc_config_get_home_dir ()))
    {
        mc_propagate_error (&mcerror, 0, "%s: %s", _ ("Home directory path is not absolute"),
                            mc_config_get_home_dir ());
        events_deinit (NULL);
        goto startup_exit_falure;
    }

    if (!mc_args_show_info ())
    {
        exit_code = EXIT_SUCCESS;
        goto startup_exit_ok;
    }

    /* check terminal type
     * $TERM must be set and not empty
     * mc_global.tty.xterm_flag is used in init_key() and tty_init()
     * Do this after mc_args_parse() where mc_args__force_xterm is set up, and after
     * mc_args_show_info(), which answers --version and --datadir-info without a terminal.
     */
    mc_global.tty.xterm_flag = tty_check_xterm_compat (mc_args__force_xterm);

    if (!events_init (&mcerror))
        goto startup_exit_falure;

    runtime_host_services_init ();

    mc_config_init_config_paths (&mcerror);
    if (mcerror != NULL)
    {
        events_deinit (NULL);
        goto startup_exit_falure;
    }

    load_setup ();

    if (mc_args__no_lua || !mc_config_get_bool (mc_global.main_config, "Lua", "enabled", TRUE))
        mc_runtime_plugins_disable ("lua");

    if (!mc_runtime_plugins_load (&mcerror))
    {
        done_setup ();
        events_deinit (NULL);
        goto startup_exit_falure;
    }

    tmpdir = mc_tmpdir ();

    if (!mc_setup_by_args (argc, argv, &mcerror))
    {
        // normally, temporary directory should be empty
        (void) rmdir (tmpdir);

        mc_runtime_plugins_shutdown ();
        done_setup ();
        events_deinit (NULL);
        goto startup_exit_falure;
    }

    /* NOTE: This has to be called before tty_init or whatever routine
       calls any define_sequence */
    init_key ();

    // We need this, since ncurses endwin () doesn't restore the signals
    save_stop_handler ();

    // Set up the terminal size.
    tty_init (!mc_args__nomouse, mc_global.tty.xterm_flag);

    // Removing this from the X code let's us type C-c
    load_key_defs ();

    keymap_load (!mc_args__nokeymap);

    macros_list = g_array_new (TRUE, FALSE, sizeof (macros_t));

    tty_init_colors (mc_global.tty.disable_colors, mc_args__force_colors);

    mc_skin_init (NULL, &mcerror);
    dlg_set_default_colors ();
    input_set_default_colors ();

    mc_error_message (&mcerror, NULL);

    if (mc_global.tty.alternate_plus_minus)
        application_keypad_mode ();

    init_mouse ();

    /* Done after tty_enter_ca_mode (tty_init) because in VTE bracketed mode is
       separate for the normal and alternate screens */
    enable_bracketed_paste ();

    // Program main loop
    exit_code = run_editor () ? EXIT_SUCCESS : EXIT_FAILURE;

    disable_bracketed_paste ();

    disable_mouse ();

    keymap_free ();

    // normally, temporary directory should be empty
    (void) rmdir (tmpdir);

    mc_runtime_plugins_shutdown ();

    mc_skin_deinit ();
    tty_colors_done ();

    tty_shutdown ();

    done_setup ();

    if (mc_global.tty.alternate_plus_minus)
        numeric_keypad_mode ();

    mc_shell_deinit ();

    done_key ();

    free_macros ();

    str_uninit_strings ();

    g_list_free_full (mc_args__edit_files, (GDestroyNotify) edit_arg_free);

    mc_config_deinit_config_paths ();

    (void) events_deinit (&mcerror);
    if (mcerror != NULL)
    {
        fprintf (stderr, _ ("\nFailed while close:\n%s\n"), mcerror->message);
        g_error_free (mcerror);
        exit_code = EXIT_FAILURE;
    }

    (void) putchar ('\n');  // Hack to make shell's prompt start at left of screen

    return exit_code;
}

/* --------------------------------------------------------------------------------------------- */
