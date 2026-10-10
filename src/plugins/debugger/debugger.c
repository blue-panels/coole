/*
   The debugger plugin: runs a program of the project under GDB and steps
   through it in the source windows.

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
#ifdef ENABLE_DAP
#include <json-glib/json-glib.h>
#endif

static const mc_ep_command_t debug_commands[DEBUG_CMD_COUNT + 1] = {
    { "Help", NULL, "f1" },
    /* the keys of IntelliJ IDEA where the editor leaves them free: F9 and F10 stay the menu and
       Quit, F5 runs as in NetBeans */
    { "DebugStartContinue", N_ ("Start or continue debugging"), "f5; f19" },
    // F2, F9 and F10 stay Save, the menu and Quit
    { "DebugPause", N_ ("Pause debugging"), "f16" },
    { "DebugStepInto", N_ ("Step into"), "f7" },
    { "DebugStepOver", N_ ("Step over"), "f8" },
    { "DebugStepOut", N_ ("Step out"), "f18" },
    // Ctrl-F2 is Save as again while nothing is debugged
    { "DebugStop", N_ ("Stop debugging"), "f15; ctrl-f2" },
    { "DebugToggleBreakpoint", N_ ("Toggle breakpoint"), "f6; ctrl-f8; ctrl-b" },
    { "DebugRunToCursor", N_ ("Run to cursor"), "f4; alt-f9" },
    { "DebugEvaluate", N_ ("Evaluate expression"), "enter; alt-f8" },
    { "DebugLeave", N_ ("Leave step mode"), "esc" },
    { "DebugClose", N_ ("Close debug session window"), "f10" },
    // from any window: to the panel of the debugger, and back to the file
    { "DebugPanel", N_ ("Go to the panel of the debugger and back"), "alt-f5" },
    { "DebugIndex", N_ ("Index the symbols of the project"), "f3" },
    /* Shift-F8 is Step out; these two are the debugger's only while the program is stopped, the
       editor's Search and Replace again the rest of the time */
    { "DebugStepInstruction", N_ ("Step into by an instruction"), "f17; ctrl-f7" },
    { "DebugNextInstruction", N_ ("Step over by an instruction"), "f14" },
    { "DebugShowStop", N_ ("Show the line the program is stopped at"), "alt-f10" },
    // F6 puts a breakpoint, Alt-F6 its condition
    { "DebugBreakpointCondition", N_ ("Condition of the breakpoint"), "alt-f6" },
    { NULL, NULL, NULL },
};

static const mc_ep_marker_kind_t debug_mark_kinds[DEBUG_MARK_COUNT] = {
    { "debugger.breakpoint", "breakpoint-char", "\u25cf", "o", "breakpoint", "breakpointline", NULL,
      10, "brightred" },
    // GDB has not taken it yet, or has refused it
    { "debugger.breakpoint-pending", "breakpoint-pending-char", "\u25ca", "?", "breakpointpending",
      "breakpointline", NULL, 12, "red" },
    { "debugger.breakpoint-disabled", "breakpoint-disabled-char", "\u25cb", "-",
      "breakpointdisabled", NULL, NULL, 11, NULL },
    // the program stops there only when its condition is true
    { "debugger.breakpoint-condition", "breakpoint-condition-char", "\u25c9", "c", "breakpoint",
      "breakpointline", NULL, 10, "brightred" },
    { "debugger.exec", "exec-char", ">", ">", "execmark", "execline", "bookmarkfound", 20,
      "yellow" },
    { "debugger.exec-breakpoint", "exec-breakpoint-char", "\u2666", "@", "execmark", "execline",
      "bookmarkfound", 21, "brightred" },
};

enum
{
    DEBUG_ACT_OPEN_PROJECT,
    DEBUG_ACT_PROJECT_STATUS,
    DEBUG_ACT_CONFIGURE,
    DEBUG_ACT_NEW_CONFIGURATION,
    DEBUG_ACT_SELECT_CONFIGURATION,
    DEBUG_ACT_DELETE_CONFIGURATION,
    DEBUG_ACT_START,
    DEBUG_ACT_TOGGLE_BREAKPOINT,
    DEBUG_ACT_CONTINUE,
    DEBUG_ACT_PAUSE,
    DEBUG_ACT_NEXT,
    DEBUG_ACT_STEP,
    DEBUG_ACT_FINISH,
    DEBUG_ACT_OUTPUT,
    DEBUG_ACT_STACK,
    DEBUG_ACT_ADD_WATCH,
    DEBUG_ACT_REMOVE_WATCH,
    DEBUG_ACT_WATCHES,
    DEBUG_ACT_INPUT,
    DEBUG_ACT_SESSION,
    DEBUG_ACT_STOP,
    DEBUG_ACT_GDB_COMMAND,
    DEBUG_ACT_MODE,
    DEBUG_ACT_FUNCTION_BREAKPOINT,
    DEBUG_ACT_RUN_TO_FUNCTION,
    DEBUG_ACT_EVALUATE,
    DEBUG_ACT_RUN_TO_CURSOR,
    DEBUG_ACT_STEP_INSTRUCTION,
    DEBUG_ACT_NEXT_INSTRUCTION,
    DEBUG_ACT_DISASSEMBLY,
    DEBUG_ACT_SHOW_STOP,
    DEBUG_ACT_CONDITION
};

// the entry of the module
const mc_editor_plugin_t *mc_editor_plugin_register (void);

/*** forward declarations (file scope functions) */

static void debug_reply_watch (void *ui, const debug_reply_t *reply, void *data);
static gboolean debug_source_is_asm (const char *file);
static void debug_breakpoint_move (debugger_t *debug, const char *number, long line);
static gboolean debug_breakpoint_install (debugger_t *debug, debug_breakpoint_t *bp);
static void debug_breakpoints_dedup (debugger_t *debug);
static void debug_breakpoints_sync (debugger_t *debug);
static const char *debug_adapter_hint (const char *adapter);
static gboolean debug_launch_is_dap (const debug_launch_t *launch);
static mc_ep_result_t debug_start (void *data, void *edit);
static mc_ep_result_t debug_condition_at_cursor (debugger_t *debug, void *edit);
static mc_ep_result_t debug_toggle_breakpoint (void *data, void *edit);
static mc_ep_result_t debug_continue (void *data, void *edit);
static mc_ep_result_t debug_pause (void *data, void *edit);
static mc_ep_result_t debug_next (void *data, void *edit);
static mc_ep_result_t debug_step (void *data, void *edit);
static mc_ep_result_t debug_step_instruction (void *data, void *edit);
static mc_ep_result_t debug_next_instruction (void *data, void *edit);
static mc_ep_result_t debug_finish (void *data, void *edit);
static mc_ep_result_t debug_run_to_cursor (debugger_t *debug, void *edit);
static mc_ep_result_t debug_evaluate (debugger_t *debug, void *edit);
static mc_ep_result_t debug_watch_add (debugger_t *debug, const char *expression);
static mc_ep_result_t debug_stop (void *data, void *edit);

/*** end of forward declarations */

/*** file scope functions *********************************************************************/

/* The functions of a file, NULL when the plugin ctags is not there to give them */
void
debug_functions_free (gpointer p)
{
    if (p != NULL)
        g_ptr_array_unref ((GPtrArray *) p);
}

static void
debug_breakpoint_free (gpointer data)
{
    debug_breakpoint_t *bp = (debug_breakpoint_t *) data;

    g_free (bp->file);
    g_free (bp->gdb_number);
    g_free (bp->condition);
    g_free (bp);
}

static void
debug_launch_free (gpointer data)
{
    debug_launch_t *launch = (debug_launch_t *) data;

    g_free (launch->name);
    g_free (launch->executable);
    g_free (launch->arguments);
    g_free (launch->directory);
    g_free (launch->environment);
    g_free (launch->gdb_path);
    g_free (launch->backend);
    g_free (launch->adapter);
    g_free (launch->address);
    g_free (launch->launch_extra);
    g_free (launch);
}

static void
debug_watch_free (gpointer data)
{
    debug_watch_t *watch = (debug_watch_t *) data;

    g_free (watch->expression);
    g_free (watch->value);
    g_free (watch);
}

debug_launch_t *
debug_active_launch (const debugger_t *debug)
{
    return debug->active_launch < debug->launches->len
        ? g_ptr_array_index (debug->launches, debug->active_launch)
        : NULL;
}

/* The command of the debugger of that number, DEBUG_CMD_NONE for any other */
int
debug_command (const debugger_t *debug, long command)
{
    int i;

    if (command == CK_IgnoreKey)
        return DEBUG_CMD_NONE;
    for (i = 0; i < DEBUG_CMD_COUNT; i++)
        if (debug->commands[i] == command)
            return i;
    return DEBUG_CMD_NONE;
}

/* The command of the debugger a key is bound to */
int
debug_command_of_key (const debugger_t *debug, int key)
{
    return debug_command (debug,
                          debug->host->command_lookup (debug->host, DEBUG_KEYMAP_SECTION, key));
}

/* Step mode: while the program is stopped, or running after a step, a file window takes the
   debugger keys; what moves around the text is the editor's, what would change it is held back.
   Esc goes back to editing until the next stop. */
gboolean
debug_stepping (const debugger_t *debug)
{
    return !debug->step_left && (debug->state == DEBUG_STOPPED || debug->state == DEBUG_RUNNING);
}

/* The editor commands step mode leaves to the editor: they move, look, select or switch
   windows, and none of them changes the text. */
gboolean
debug_step_passes (long command)
{
    switch (command)
    {
    case CK_Up:
    case CK_Down:
    case CK_Left:
    case CK_Right:
    case CK_Home:
    case CK_End:
    case CK_PageUp:
    case CK_PageDown:
    case CK_HalfPageUp:
    case CK_HalfPageDown:
    case CK_Top:
    case CK_Bottom:
    case CK_TopOnScreen:
    case CK_BottomOnScreen:
    case CK_WordLeft:
    case CK_WordRight:
    case CK_ScrollUp:
    case CK_ScrollDown:
    case CK_ParagraphUp:
    case CK_ParagraphDown:
    case CK_Search:
    case CK_SearchContinue:
    case CK_Goto:
    case CK_Find:
    case CK_MatchBracket:
    case CK_Bookmark:
    case CK_BookmarkNext:
    case CK_BookmarkPrev:
    case CK_FoldToggle:
    case CK_UnfoldAll:
    case CK_FilterToggle:
    case CK_FilterWord:
    case CK_QuickFilter:
    case CK_Mark:
    case CK_MarkLeft:
    case CK_MarkRight:
    case CK_MarkUp:
    case CK_MarkDown:
    case CK_MarkToWordBegin:
    case CK_MarkToWordEnd:
    case CK_MarkToHome:
    case CK_MarkToEnd:
    case CK_MarkColumn:
    case CK_MarkWord:
    case CK_MarkLine:
    case CK_MarkAll:
    case CK_Unmark:
    case CK_MarkPageUp:
    case CK_MarkPageDown:
    case CK_MarkToFileBegin:
    case CK_MarkToFileEnd:
    case CK_MarkToPageBegin:
    case CK_MarkToPageEnd:
    case CK_MarkScrollUp:
    case CK_MarkScrollDown:
    case CK_MarkParagraphUp:
    case CK_MarkParagraphDown:
    case CK_MarkColumnPageUp:
    case CK_MarkColumnPageDown:
    case CK_MarkColumnLeft:
    case CK_MarkColumnRight:
    case CK_MarkColumnUp:
    case CK_MarkColumnDown:
    case CK_MarkColumnScrollUp:
    case CK_MarkColumnScrollDown:
    case CK_MarkColumnParagraphUp:
    case CK_MarkColumnParagraphDown:
    case CK_Store:
    case CK_Save:
    case CK_FilePrev:
    case CK_FileNext:
    case CK_EditFile:
    case CK_EditNew:
    case CK_Close:
    case CK_History:
    case CK_Help:
    case CK_Menu:
    case CK_Quit:
    case CK_Refresh:
    case CK_Shell:
    case CK_Options:
    case CK_OptionsAppearance:
    case CK_KeyBindings:
    case CK_ManagePlugins:
    case CK_ShowNumbers:
    case CK_ShowMargin:
    case CK_ShowTabTws:
    case CK_ShowControlChars:
    case CK_SyntaxOnOff:
    case CK_WindowMove:
    case CK_WindowResize:
    case CK_WindowFullscreen:
    case CK_WindowList:
    case CK_WindowSticky:
    case CK_WindowNext:
    case CK_WindowPrev:
        return TRUE;
    default:
        return FALSE;
    }
}

/* Run a command of the debugger; one that works on a line takes that of @edit, or of the topmost
   file window when @edit is NULL */
gboolean
debug_run_command (debugger_t *debug, int cmd, void *edit)
{
    void *file_window = edit != NULL ? edit : debug->host->window_top_file (debug->host);

    switch (cmd)
    {
    case DEBUG_CMD_START_CONTINUE:
        if (debug->state == DEBUG_STOPPED)
            (void) debug_continue (debug, edit);
        else if (debug->state == DEBUG_OFF || debug->state == DEBUG_FINISHED)
            (void) debug_start (debug, edit);
        return TRUE;
    case DEBUG_CMD_PAUSE:
        (void) debug_pause (debug, edit);
        return TRUE;
    case DEBUG_CMD_STEP_INTO:
        (void) debug_step (debug, edit);
        return TRUE;
    case DEBUG_CMD_STEP_OVER:
        (void) debug_next (debug, edit);
        return TRUE;
    case DEBUG_CMD_STEP_OUT:
        (void) debug_finish (debug, edit);
        return TRUE;
    case DEBUG_CMD_STEP_INSTRUCTION:
        (void) debug_step_instruction (debug, edit);
        return TRUE;
    case DEBUG_CMD_NEXT_INSTRUCTION:
        (void) debug_next_instruction (debug, edit);
        return TRUE;
    case DEBUG_CMD_STOP:
        if (debug_session_live (debug))
            (void) debug_stop (debug, edit);
        return TRUE;
    case DEBUG_CMD_TOGGLE_BREAKPOINT:
        (void) debug_toggle_breakpoint (debug, file_window);
        return TRUE;
    case DEBUG_CMD_RUN_TO_CURSOR:
        (void) debug_run_to_cursor (debug, file_window);
        return TRUE;
    case DEBUG_CMD_EVALUATE:
        (void) debug_evaluate (debug, edit);
        return TRUE;
    case DEBUG_CMD_LEAVE:
        debug->step_left = TRUE;
        debug_session_refresh (debug);
        return TRUE;
    case DEBUG_CMD_CLOSE:
        if (debug->session_window != NULL)
            (void) debug_session_close_window (&debug->session_window->window);
        return TRUE;
    case DEBUG_CMD_INDEX:
    {
        char *file =
            file_window != NULL ? debug->host->get_current_file (debug->host, file_window) : NULL;
        GVariantDict args;
        GVariant *reply;
        gboolean started = FALSE;

        g_variant_dict_init (&args, NULL);
        // a name that is no UTF-8 cannot go as a string: the project then
        if (file != NULL && g_utf8_validate (file, -1, NULL))
            g_variant_dict_insert (&args, "file", "s", file);
        else if (debug->project_dir != NULL)
            g_variant_dict_insert (&args, "root", "s", debug->project_dir);
        reply = debug->host->service_call (debug->host, "ctags", "reindex",
                                           g_variant_dict_end (&args), NULL);
        if (reply == NULL)
            debug_error (debug, _ ("The symbols are indexed by the plugin ctags, which is off."));
        else
        {
            (void) g_variant_lookup (reply, "started", "b", &started);
            g_variant_unref (reply);
            if (started)
                debug->host->message (debug->host, D_NORMAL, _ ("Debug"),
                                      _ ("The symbols of the project are indexed in the "
                                         "background."));
        }
        g_free (file);
        return TRUE;
    }
    case DEBUG_CMD_CONDITION:
        (void) debug_condition_at_cursor (debug, file_window);
        return TRUE;
    case DEBUG_CMD_SHOW_STOP:
        if (debug->state == DEBUG_STOPPED && debug->current_file != NULL
            && debug->host->show_location != NULL)
            (void) debug->host->show_location (debug->host, debug->current_file,
                                               debug->current_line);
        else
            tty_beep ();
        return TRUE;
    case DEBUG_CMD_PANEL:
        if (debug->session_window != NULL
            && debug->host->window_current (debug->host) == debug->session_window)
        {
            void *file = debug->host->window_top_file (debug->host);

            if (file != NULL)
                debug->host->window_show (debug->host, file);
        }
        else
            (void) debug_session_show (debug, NULL);
        return TRUE;
    default:
        return FALSE;
    }
}

static char *
debug_config_path (const debugger_t *debug)
{
    char *hash, *filename, *path;

    if (debug->project_dir == NULL)
        return NULL;
    hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, debug->project_dir, -1);
    filename = g_strconcat (hash, ".ini", NULL);
    path = g_build_filename (g_get_user_config_dir (), "coole", "debug", filename, NULL);
    g_free (filename);
    g_free (hash);
    return path;
}

/* The configurations kept in the project, which go with it */
static char *
debug_project_config_path (const debugger_t *debug)
{
    return debug->project_dir != NULL
        ? g_build_filename (debug->project_dir, ".coole", "debug.ini", (char *) NULL)
        : NULL;
}

/* A name in the project as it is kept there: from the root of the project */
static char *
debug_path_from_root (const debugger_t *debug, const char *path, gboolean relative)
{
    const gsize len = strlen (debug->project_dir);

    if (relative && path != NULL && strncmp (path, debug->project_dir, len) == 0
        && (path[len] == '/' || path[len] == '\0'))
        return g_strdup (path[len] == '\0' ? "." : path + len + 1);
    return g_strdup (path != NULL ? path : "");
}

static void
debug_launches_write (const debugger_t *debug, GKeyFile *keyfile, gboolean relative)
{
    guint i;

    g_key_file_set_integer (keyfile, "Debug", "launch_count", (gint) debug->launches->len);
    g_key_file_set_integer (keyfile, "Debug", "active_launch", (gint) debug->active_launch);
    for (i = 0; i < debug->launches->len; i++)
    {
        const debug_launch_t *launch = g_ptr_array_index (debug->launches, i);
        char *group = g_strdup_printf ("Launch %u", i);
        char *executable = debug_path_from_root (debug, launch->executable, relative);
        char *directory = debug_path_from_root (debug, launch->directory, relative);

        g_key_file_set_string (keyfile, group, "name", launch->name);
        g_key_file_set_string (keyfile, group, "executable", executable);
        g_key_file_set_string (keyfile, group, "arguments",
                               launch->arguments != NULL ? launch->arguments : "");
        g_key_file_set_string (keyfile, group, "directory", directory);
        g_key_file_set_string (keyfile, group, "environment",
                               launch->environment != NULL ? launch->environment : "");
        g_key_file_set_string (keyfile, group, "gdb_path",
                               launch->gdb_path != NULL ? launch->gdb_path : "gdb");
        g_key_file_set_boolean (keyfile, group, "build", launch->build);
        g_key_file_set_boolean (keyfile, group, "terminal", launch->terminal);
        g_key_file_set_string (keyfile, group, "backend",
                               launch->backend != NULL ? launch->backend : "gdb-mi");
        if (launch->adapter != NULL)
            g_key_file_set_string (keyfile, group, "adapter", launch->adapter);
        if (launch->address != NULL)
            g_key_file_set_string (keyfile, group, "address", launch->address);
        if (launch->launch_extra != NULL)
            g_key_file_set_string (keyfile, group, "launch_extra", launch->launch_extra);
        g_free (executable);
        g_free (directory);
        g_free (group);
    }
}

/* The configurations of a keyfile; names from the root of the project are made whole */
static void
debug_launches_read (debugger_t *debug, GKeyFile *keyfile)
{
    gint launch_count;
    gsize i;

    launch_count = g_key_file_has_key (keyfile, "Debug", "launch_count", NULL)
        ? g_key_file_get_integer (keyfile, "Debug", "launch_count", NULL)
        : -1;
    if (launch_count < 0 || launch_count > 1000)
    {
        // the first version kept one, in the Debug group
        debug_launch_t *legacy = g_new0 (debug_launch_t, 1);

        legacy->name = g_strdup (_ ("Default"));
        legacy->executable = g_key_file_get_string (keyfile, "Debug", "executable", NULL);
        legacy->arguments = g_key_file_get_string (keyfile, "Debug", "arguments", NULL);
        legacy->directory = g_key_file_get_string (keyfile, "Debug", "directory", NULL);
        if (legacy->directory == NULL)
            legacy->directory = g_strdup (debug->project_dir);
        if (legacy->executable != NULL)
            g_ptr_array_add (debug->launches, legacy);
        else
            debug_launch_free (legacy);
        return;
    }
    for (i = 0; i < (gsize) launch_count; i++)
    {
        debug_launch_t *launch = g_new0 (debug_launch_t, 1);
        char *group = g_strdup_printf ("Launch %u", (guint) i);
        char *executable, *directory;

        launch->name = g_key_file_get_string (keyfile, group, "name", NULL);
        executable = g_key_file_get_string (keyfile, group, "executable", NULL);
        launch->arguments = g_key_file_get_string (keyfile, group, "arguments", NULL);
        directory = g_key_file_get_string (keyfile, group, "directory", NULL);
        launch->environment = g_key_file_get_string (keyfile, group, "environment", NULL);
        launch->gdb_path = g_key_file_get_string (keyfile, group, "gdb_path", NULL);
        launch->build = g_key_file_get_boolean (keyfile, group, "build", NULL);
        // a configuration from before there was a terminal for the program has one
        launch->terminal = !g_key_file_has_key (keyfile, group, "terminal", NULL)
            || g_key_file_get_boolean (keyfile, group, "terminal", NULL);
        // a configuration from before the debug adapters has GDB
        launch->backend = g_key_file_get_string (keyfile, group, "backend", NULL);
        launch->adapter = g_key_file_get_string (keyfile, group, "adapter", NULL);
        launch->address = g_key_file_get_string (keyfile, group, "address", NULL);
        launch->launch_extra = g_key_file_get_string (keyfile, group, "launch_extra", NULL);
        if (executable != NULL && *executable != '\0')
            launch->executable = g_canonicalize_filename (executable, debug->project_dir);
        launch->directory = g_canonicalize_filename (
            directory != NULL && *directory != '\0' ? directory : ".", debug->project_dir);
        g_free (executable);
        g_free (directory);
        if (launch->name == NULL || launch->executable == NULL)
            debug_launch_free (launch);
        else
            g_ptr_array_add (debug->launches, launch);
        g_free (group);
    }
    launch_count = g_key_file_get_integer (keyfile, "Debug", "active_launch", NULL);
    if (launch_count >= 0 && launch_count < (gint) debug->launches->len)
        debug->active_launch = (guint) launch_count;
}

static gboolean
debug_keyfile_save (debugger_t *debug, GKeyFile *keyfile, const char *path, int mode)
{
    char *contents, *directory;
    gsize length;
    GError *error = NULL;
    gboolean saved;

    contents = g_key_file_to_data (keyfile, &length, NULL);
    directory = g_path_get_dirname (path);
    saved = g_mkdir_with_parents (directory, 0700) == 0
        && g_file_set_contents (path, contents, length, &error) && g_chmod (path, mode) == 0;
    if (!saved)
        debug->host->message (debug->host, D_ERROR, _ ("Debug"),
                              error != NULL ? error->message
                                            : _ ("Could not save the debug project settings."));
    g_clear_error (&error);
    g_free (directory);
    g_free (contents);
    return saved;
}

/* The configurations go to the project or to the settings of the user; the breakpoints and the
   watches, which are one's own, to the user's always */
void
debug_config_save (debugger_t *debug)
{
    GKeyFile *keyfile;
    char *path;
    char **locations, **expressions;
    guint i;

    path = debug_config_path (debug);
    if (path == NULL)
        return;
    debug_breakpoints_sync (debug);
    keyfile = g_key_file_new ();
    g_key_file_set_string (keyfile, "Debug", "project", debug->project_dir);
    g_key_file_set_boolean (keyfile, "Debug", "in_project", debug->launches_in_project);
    if (debug->launches_in_project)
    {
        GKeyFile *shared = g_key_file_new ();
        char *shared_path = debug_project_config_path (debug);

        debug_launches_write (debug, shared, TRUE);
        (void) debug_keyfile_save (debug, shared, shared_path, 0644);
        g_free (shared_path);
        g_key_file_free (shared);
    }
    else
        debug_launches_write (debug, keyfile, FALSE);
    locations = g_new0 (char *, debug->breakpoints->len + 1);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        locations[i] = g_strdup_printf ("%s:%ld", bp->file, bp->line);
    }
    g_key_file_set_string_list (keyfile, "Debug", "breakpoints", (const gchar *const *) locations,
                                debug->breakpoints->len);
    {
        GPtrArray *off = g_ptr_array_new ();

        for (i = 0; i < debug->breakpoints->len; i++)
            if (((debug_breakpoint_t *) g_ptr_array_index (debug->breakpoints, i))->disabled)
                g_ptr_array_add (off, locations[i]);
        g_key_file_set_string_list (keyfile, "Debug", "breakpoints_disabled",
                                    (const gchar *const *) off->pdata, off->len);
        g_ptr_array_free (off, TRUE);
    }
    {
        // a breakpoint and its condition, one after the other
        GPtrArray *conditions = g_ptr_array_new ();

        for (i = 0; i < debug->breakpoints->len; i++)
        {
            const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

            if (bp->condition != NULL)
            {
                g_ptr_array_add (conditions, locations[i]);
                g_ptr_array_add (conditions, bp->condition);
            }
        }
        if (conditions->len > 0)
            g_key_file_set_string_list (keyfile, "Debug", "breakpoint_conditions",
                                        (const gchar *const *) conditions->pdata, conditions->len);
        g_ptr_array_free (conditions, TRUE);
    }
    expressions = g_new0 (char *, debug->watches->len + 1);
    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        expressions[i] = g_strdup (watch->expression);
    }
    g_key_file_set_string_list (keyfile, "Debug", "watches", (const gchar *const *) expressions,
                                debug->watches->len);
    (void) debug_keyfile_save (debug, keyfile, path, 0600);
    g_strfreev (locations);
    g_strfreev (expressions);
    g_key_file_free (keyfile);
    g_free (path);
}

static void
debug_config_load (debugger_t *debug)
{
    GKeyFile *keyfile, *shared;
    char *path, *shared_path;
    char **locations, **expressions;
    gsize count, i;
    gboolean own_launches = FALSE;

    path = debug_config_path (debug);
    if (path == NULL)
        return;
    keyfile = g_key_file_new ();
    if (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
    {
        // Keep in the project turned off: the user's configurations, the project's aside
        own_launches = g_key_file_has_key (keyfile, "Debug", "in_project", NULL)
            && !g_key_file_get_boolean (keyfile, "Debug", "in_project", NULL);
        debug_launches_read (debug, keyfile);
        expressions = g_key_file_get_string_list (keyfile, "Debug", "watches", &count, NULL);
        for (i = 0; expressions != NULL && i < count; i++)
        {
            debug_watch_t *watch;

            if (expressions[i][0] == '\0')
                continue;
            watch = g_new0 (debug_watch_t, 1);
            watch->expression = g_strdup (expressions[i]);
            g_ptr_array_add (debug->watches, watch);
        }
        g_strfreev (expressions);
        locations = g_key_file_get_string_list (keyfile, "Debug", "breakpoints", &count, NULL);
        for (i = 0; locations != NULL && i < count; i++)
        {
            char *separator = strrchr (locations[i], ':');
            char *end = NULL;
            gint64 line;
            debug_breakpoint_t *bp;

            if (separator == NULL)
                continue;
            line = g_ascii_strtoll (separator + 1, &end, 10);
            if (end == separator + 1 || *end != '\0' || line <= 0 || line > G_MAXLONG)
                continue;
            bp = g_new0 (debug_breakpoint_t, 1);
            bp->file = g_strndup (locations[i], separator - locations[i]);
            bp->line = (long) line;
            g_ptr_array_add (debug->breakpoints, bp);
        }
        g_strfreev (locations);
        locations =
            g_key_file_get_string_list (keyfile, "Debug", "breakpoints_disabled", &count, NULL);
        for (i = 0; locations != NULL && i < count; i++)
        {
            guint k;

            for (k = 0; k < debug->breakpoints->len; k++)
            {
                debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, k);
                char *location = g_strdup_printf ("%s:%ld", bp->file, bp->line);

                if (strcmp (location, locations[i]) == 0)
                    bp->disabled = TRUE;
                g_free (location);
            }
        }
        g_strfreev (locations);
        locations =
            g_key_file_get_string_list (keyfile, "Debug", "breakpoint_conditions", &count, NULL);
        for (i = 0; locations != NULL && i + 1 < count; i += 2)
        {
            guint k;

            for (k = 0; k < debug->breakpoints->len; k++)
            {
                debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, k);
                char *location = g_strdup_printf ("%s:%ld", bp->file, bp->line);

                if (strcmp (location, locations[i]) == 0 && locations[i + 1][0] != '\0')
                {
                    g_free (bp->condition);
                    bp->condition = g_strdup (locations[i + 1]);
                }
                g_free (location);
            }
        }
        g_strfreev (locations);
    }
    g_key_file_free (keyfile);
    g_free (path);

    // the configurations of the project go before the user's own
    shared_path = debug_project_config_path (debug);
    shared = g_key_file_new ();
    debug->launches_in_project = FALSE;
    if (!own_launches && g_key_file_load_from_file (shared, shared_path, G_KEY_FILE_NONE, NULL))
    {
        g_ptr_array_set_size (debug->launches, 0);
        debug->active_launch = 0;
        debug_launches_read (debug, shared);
        debug->launches_in_project = TRUE;
    }
    g_key_file_free (shared);
    g_free (shared_path);
}

void
debug_error (debugger_t *debug, const char *message_text)
{
    debug->host->message (debug->host, D_ERROR, _ ("Debug"), message_text);
}

/* GDB is gone or starts again: it owes nothing */
static void
debug_requests_clear (debugger_t *debug)
{
    // the tokens of a GDB gone answer nothing any more
    g_array_set_size (debug->dropped_tokens, 0);
    if (debug->backend != NULL)
        debug->backend->ops->cancel (debug->backend);
    debug->eval_pending = FALSE;
}

gboolean
debug_alive (const debugger_t *debug)
{
    return debug->backend != NULL && debug->backend->ops->alive (debug->backend);
}

static void
debug_watches_show (debugger_t *debug)
{
    // the panel shows them
    debug_session_refresh (debug);
}

static void
debug_watches_refresh (debugger_t *debug)
{
    guint i;

    if (debug->state != DEBUG_STOPPED)
        return;
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        g_free (watch->value);
        watch->value = g_strdup (_ ("<evaluating>"));
        watch->pending_token = debug->backend->ops->evaluate (debug->backend, watch->expression,
                                                              debug_reply_watch, NULL, NULL);
        if (watch->pending_token == 0)
        {
            g_free (watch->value);
            watch->value = g_strdup (_ ("<GDB unavailable>"));
        }
    }
    debug_watches_show (debug);
}

static void
debug_watches_clear_values (debugger_t *debug)
{
    guint i;

    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        watch->pending_token = 0;
        g_clear_pointer (&watch->value, g_free);
    }
    debug_watches_show (debug);
}

/* Whether two names name the same file: GDB gives the real path, the editor the name the file
   was opened by */
static gboolean
debug_same_file (const char *a, const char *b)
{
    char *ra, *rb;
    gboolean same;

    if (a == NULL || b == NULL)
        return FALSE;
    if (strcmp (a, b) == 0)
        return TRUE;
    ra = realpath (a, NULL);
    rb = realpath (b, NULL);
    same = ra != NULL && rb != NULL && strcmp (ra, rb) == 0;
    free (ra);
    free (rb);
    return same;
}

gboolean
debug_session_live (const debugger_t *debug)
{
    return debug->state == DEBUG_STARTING || debug->state == DEBUG_RUNNING
        || debug->state == DEBUG_STOPPED;
}

int
debug_breakpoint_mark (const debugger_t *debug, const debug_breakpoint_t *bp)
{
    if (bp->disabled)
        return DEBUG_MARK_DISABLED;
    if (debug_session_live (debug) && (bp->gdb_number == NULL || bp->unverified))
        return DEBUG_MARK_PENDING;
    return bp->condition != NULL ? DEBUG_MARK_CONDITION : DEBUG_MARK_BREAKPOINT;
}

static gboolean
debug_is_breakpoint_mark (int mark)
{
    return mark == DEBUG_MARK_BREAKPOINT || mark == DEBUG_MARK_PENDING
        || mark == DEBUG_MARK_DISABLED || mark == DEBUG_MARK_CONDITION;
}

static debug_breakpoint_t *
debug_breakpoint_at (const debugger_t *debug, const char *file, long line, guint *index)
{
    guint i;

    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (bp->line == line && debug_same_file (bp->file, file))
        {
            if (index != NULL)
                *index = i;
            return bp;
        }
    }
    return NULL;
}

/* Put the marks of the breakpoints and of the current line in the gutter: of one file, or of all
   (file NULL).  The marks are taken off first, so call debug_breakpoints_sync() before, to keep
   what the editing of the text has moved. */
void
debug_marks_show (debugger_t *debug, const char *file)
{
    guint i;

    if (debug->host->set_marker == NULL)
        return;
    for (i = 0; i < DEBUG_MARK_COUNT; i++)
        debug->host->clear_markers (debug->host, file, debug->marks[i]);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (file == NULL || debug_same_file (bp->file, file))
            debug->host->set_marker (debug->host, bp->file, bp->line,
                                     debug->marks[debug_breakpoint_mark (debug, bp)], TRUE);
    }
    if (debug->current_file != NULL
        && (file == NULL || debug_same_file (debug->current_file, file)))
        debug->host->set_marker (
            debug->host, debug->current_file, debug->current_line,
            debug->marks[debug_breakpoint_at (debug, debug->current_file, debug->current_line, NULL)
                                 != NULL
                             ? DEBUG_MARK_EXEC_BREAKPOINT
                             : DEBUG_MARK_EXEC],
            TRUE);
}

static void
debug_clear_current (debugger_t *debug)
{
    char *file = debug->current_file;

    debug->current_file = NULL;
    debug->current_line = 0;
    if (file != NULL && debug->host->clear_markers != NULL)
    {
        debug->host->clear_markers (debug->host, file, debug->marks[DEBUG_MARK_EXEC]);
        debug->host->clear_markers (debug->host, file, debug->marks[DEBUG_MARK_EXEC_BREAKPOINT]);
    }
    g_free (file);
}

/* Whether @name stands in @line as a word of its own */
static gboolean
debug_line_names (const char *line, const char *name)
{
    const gsize len = strlen (name);
    const char *p;

    for (p = strstr (line, name); p != NULL; p = strstr (p + 1, name))
    {
        const gboolean before = p == line || !(g_ascii_isalnum (p[-1]) || p[-1] == '_');
        const gboolean after = !(g_ascii_isalnum (p[len]) || p[len] == '_');

        if (before && after)
            return TRUE;
    }
    return FALSE;
}

/* Whether a register stands in a line of assembler, by its name or by that of a part of it:
   rax as eax, ax, al; r8 as r8d, r8w, r8b.  What a comment says does not count. */
static gboolean
debug_line_names_register (const char *line, const char *reg)
{
    char *code = g_strndup (line, strcspn (line, "#;"));
    const gsize len = strlen (reg);
    gboolean named = debug_line_names (code, reg);

    // the registers of x86-64 that have parts of other names
    if (!named && len == 3 && reg[0] == 'r' && !g_ascii_isdigit (reg[1]))
    {
        const char e[] = { 'e', reg[1], reg[2], '\0' };
        const char w[] = { reg[1], reg[2], '\0' };
        const char low[] = { reg[1], reg[2] == 'x' ? 'l' : reg[2], reg[2] == 'x' ? '\0' : 'l',
                             '\0' };
        const char high[] = { reg[1], 'h', '\0' };

        named = debug_line_names (code, e) || debug_line_names (code, w)
            || debug_line_names (code, low) || (reg[2] == 'x' && debug_line_names (code, high));
    }
    else if (!named && reg[0] == 'r' && g_ascii_isdigit (reg[1]) && len <= 3)
    {
        const char *parts[] = { "d", "w", "b" };
        guint i;

        for (i = 0; i < G_N_ELEMENTS (parts) && !named; i++)
        {
            char *part = g_strconcat (reg, parts[i], (char *) NULL);

            named = debug_line_names (code, part);
            g_free (part);
        }
    }
    g_free (code);
    return named;
}

/* Whether a line of assembler is a label, where a run of instructions starts */
static gboolean
debug_line_is_label (const char *line)
{
    const char *p = line + strspn (line, " \t");

    if (!(g_ascii_isalpha (*p) || *p == '_' || *p == '.'))
        return FALSE;
    while (g_ascii_isalnum (*p) || *p == '_' || *p == '.' || *p == '$')
        p++;
    return *p == ':';
}

static void
debug_notes_clear (debugger_t *debug)
{
    if (debug->host->clear_line_notes != NULL)
    {
        debug->host->clear_line_notes (debug->host, NULL);
        debug->host->redraw (debug->host);
    }
}

/* The values of the local variables after the lines of the function that name them, from its
   start down to the line the program stopped on */
static void
debug_notes_show (debugger_t *debug)
{
    // in assembler the registers stand for the variables
    const gboolean asm_source = debug_source_is_asm (debug->current_file);
    const GPtrArray *values = asm_source ? debug->registers : debug->locals;
    char *text = NULL;
    char **lines;
    long line, first;

    debug_notes_clear (debug);
    if (debug->host->set_line_note == NULL || debug->state != DEBUG_STOPPED
        || debug->current_file == NULL || values->len == 0
        || !g_file_get_contents (debug->current_file, &text, NULL, NULL))
    {
        g_free (text);
        return;
    }
    lines = g_strsplit (text, "\n", -1);
    g_free (text);
    if (debug->current_line > (long) g_strv_length (lines))
    {
        g_strfreev (lines);
        return;
    }

    /* up to the start of the function: a line that opens with a brace, a label in assembler,
       40 lines at most */
    for (first = debug->current_line; first > 1 && first > debug->current_line - 40; first--)
        if (asm_source ? debug_line_is_label (lines[first - 1]) : lines[first - 1][0] == '{')
            break;

    for (line = first; line <= debug->current_line; line++)
    {
        GString *note = g_string_new (NULL);
        guint i;

        for (i = 0; i < values->len; i++)
        {
            const debug_variable_t *local = g_ptr_array_index (values, i);

            if (asm_source ? !debug_line_names_register (lines[line - 1], local->name)
                           : !debug_line_names (lines[line - 1], local->name))
                continue;
            if (note->len > 0)
                g_string_append (note, ", ");
            g_string_append_printf (note, "%s = %.40s", local->name, local->value);
        }
        if (note->len > 0)
            debug->host->set_line_note (debug->host, debug->current_file, line, note->str);
        g_string_free (note, TRUE);
    }
    g_strfreev (lines);
    debug->host->redraw (debug->host);
}

/* The call stack, the frames taken from the reply */
static void
debug_reply_stack (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    // the frames are the panel's now
    g_ptr_array_unref (debug->frames);
    debug->frames = reply->frames != NULL ? g_ptr_array_ref (reply->frames)
                                          : g_ptr_array_new_with_free_func (debug_frame_free);
    /* under _start of a program with no libc GDB has no frames to go on to, and makes some up:
       the stack ends at the first one with neither a function nor a source */
    for (i = 1; i < debug->frames->len; i++)
    {
        const debug_frame_t *frame = g_ptr_array_index (debug->frames, i);

        if (frame->file == NULL && frame->from == NULL
            && (frame->func == NULL || strcmp (frame->func, "??") == 0))
        {
            g_ptr_array_set_size (debug->frames, i);
            break;
        }
    }
    for (i = 0; i < debug->frames->len; i++)
    {
        debug_frame_t *frame = g_ptr_array_index (debug->frames, i);
        const char *func = frame->func != NULL ? frame->func : "?";

        if (frame->file != NULL)
            frame->label =
                g_strdup_printf ("#%ld %s  %s:%ld", frame->level, func, frame->file, frame->line);
        else
            frame->label = g_strdup_printf ("#%ld %s  %s", frame->level, func,
                                            frame->from != NULL ? frame->from : _ ("<no source>"));
    }
    debug_session_refresh (debug);
}

/* The local variables of the frame */
static void
debug_reply_variables (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;

    (void) data;
    g_ptr_array_unref (debug->locals);
    debug->locals = reply->variables != NULL ? g_ptr_array_ref (reply->variables)
                                             : g_ptr_array_new_with_free_func (debug_variable_free);
    debug_notes_show (debug);
    debug_session_refresh (debug);
}

/* The registers: the values of the stop before are kept, to show which have changed */
static void
debug_reply_registers (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    if (debug->registers_before != NULL)
        g_hash_table_destroy (debug->registers_before);
    debug->registers_before = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    for (i = 0; i < debug->registers->len; i++)
    {
        const debug_variable_t *reg = g_ptr_array_index (debug->registers, i);

        g_hash_table_insert (debug->registers_before, g_strdup (reg->name), g_strdup (reg->value));
    }
    g_ptr_array_unref (debug->registers);
    debug->registers = reply->registers != NULL
        ? g_ptr_array_ref (reply->registers)
        : g_ptr_array_new_with_free_func (debug_variable_free);
    if (!reply->ok && reply->msg != NULL)
        debug_output_console (debug, reply->msg, TRUE);
    if (debug_source_is_asm (debug->current_file))
        debug_notes_show (debug);
    debug_session_refresh (debug);
}

/* The registers of the frame, when the panel shows them */
static void
debug_registers_refresh (debugger_t *debug)
{
    if (debug->state != DEBUG_STOPPED || !debug->registers_shown || debug->backend == NULL
        || debug->backend->ops->registers == NULL)
        return;
    (void) debug->backend->ops->registers (debug->backend, debug_reply_registers, NULL, NULL);
}

/* The registers shown in the panel, or hidden: they are asked for only when shown */
void
debug_registers_toggle (debugger_t *debug)
{
    debug->registers_shown = !debug->registers_shown;
    debug_registers_refresh (debug);
    debug_session_refresh (debug);
}

/* The registers are of another program now: none changed */
static void
debug_registers_clear (debugger_t *debug)
{
    g_ptr_array_set_size (debug->registers, 0);
    g_clear_pointer (&debug->registers_before, g_hash_table_destroy);
}

/* The value of a watch */
static void
debug_reply_watch (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const gboolean failed = !reply->ok;
    guint i;

    (void) data;
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);
        const char *value;

        if (watch->pending_token != reply->request)
            continue;
        watch->pending_token = 0;
        value = failed ? reply->msg : reply->value;
        g_free (watch->value);
        watch->value = g_strdup (value != NULL ? value : _ ("<unavailable>"));
        debug_watches_show (debug);
        debug_session_refresh (debug);
        return;
    }
}

/* The value of an expression asked for with Enter */
static void
debug_reply_evaluate (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const char *expression = (const char *) data;
    const gboolean failed = !reply->ok;
    const char *value = failed ? reply->msg : reply->value;
    char *text_value;

    debug->eval_pending = FALSE;
    text_value =
        g_strdup_printf ("%s = %s", expression, value != NULL ? value : _ ("<unavailable>"));
    if (failed)
        debug_error (debug, text_value);
    else if (query_dialog (_ ("Evaluate"), text_value, D_NORMAL, 2, _ ("&OK"), _ ("Add &watch"))
             == 1)
        (void) debug_watch_add (debug, expression);
    g_free (text_value);
}

/* --------------------------------------------------------------------------------------------- */
/* A value looked into: a structure, an object or an array, its members a tree */
/* --------------------------------------------------------------------------------------------- */

typedef struct debug_node_t debug_node_t;

struct debug_node_t
{
    debug_variable_t *var;
    debug_node_t *parent;
    GPtrArray *members;  // debug_node_t, NULL until they are asked for
    gboolean open;
    gboolean asking;
    guint id;
    int depth;
};

/* The tree of the dialog open, NULL when none is */
typedef struct
{
    debugger_t *debug;
    debug_node_t *root;
    WListbox *list;
    GPtrArray *shown;  // debug_node_t in the order of the list
} debug_tree_t;

static debug_tree_t *debug_tree = NULL;
static guint debug_node_ids = 0;

static void
debug_node_free (gpointer p)
{
    debug_node_t *node = (debug_node_t *) p;

    debug_variable_free (node->var);
    if (node->members != NULL)
        g_ptr_array_free (node->members, TRUE);
    g_free (node);
}

static debug_node_t *
debug_node_new (const debug_variable_t *var, debug_node_t *parent)
{
    debug_node_t *node = g_new0 (debug_node_t, 1);

    node->var = g_new0 (debug_variable_t, 1);
    node->var->name = g_strdup (var->name);
    node->var->value = g_strdup (var->value);
    node->var->ref = g_strdup (var->ref);
    node->var->expression = g_strdup (var->expression);
    node->var->type = g_strdup (var->type);
    node->parent = parent;
    node->depth = parent != NULL ? parent->depth + 1 : 0;
    node->id = ++debug_node_ids;
    return node;
}

/* Whether an expression is a name, or ends as one: it takes a member without parentheses */
static gboolean
debug_expression_simple (const char *expression)
{
    const char *p;

    for (p = expression; *p != '\0'; p++)
        if (!(g_ascii_isalnum (*p) || *p == '_' || *p == '.' || *p == '[' || *p == ']'
              || (*p == '-' && p[1] == '>') || (*p == '>' && p > expression && p[-1] == '-')))
            return FALSE;
    return TRUE;
}

/* Whether a type of C is a pointer: "struct pt *", spaces after the star or not */
static gboolean
debug_type_is_pointer (const char *type)
{
    gsize len = strlen (type);

    while (len > 0 && g_ascii_isspace (type[len - 1]))
        len--;
    return len > 0 && type[len - 1] == '*';
}

/* The expression of a member, for a watch: the one the debugger gives, else made from that of
   the structure as C has it, s.x, p->x, a[3] */
static char *
debug_node_expression (const debug_node_t *node)
{
    char *parent, *expression;
    const char *type;

    if (node->var->expression != NULL)
        return g_strdup (node->var->expression);
    if (node->parent == NULL)
        return g_strdup (node->var->name);
    parent = debug_node_expression (node->parent);
    if (parent == NULL)
        return NULL;
    type = node->parent->var->type;
    if (node->var->name[0] != '\0'
        && strspn (node->var->name, "0123456789") == strlen (node->var->name))
        expression = g_strdup_printf (debug_expression_simple (parent) ? "%s[%s]" : "(%s)[%s]",
                                      parent, node->var->name);
    else if (type != NULL && debug_type_is_pointer (type))
        expression = g_strdup_printf (debug_expression_simple (parent) ? "%s->%s" : "(%s)->%s",
                                      parent, node->var->name);
    else
        expression = g_strdup_printf (debug_expression_simple (parent) ? "%s.%s" : "(%s).%s",
                                      parent, node->var->name);
    g_free (parent);
    return expression;
}

/* The nodes the list shows, from @node down */
static void
debug_tree_collect (GPtrArray *shown, debug_node_t *node)
{
    guint i;

    g_ptr_array_add (shown, node);
    if (!node->open || node->members == NULL)
        return;
    for (i = 0; i < node->members->len; i++)
        debug_tree_collect (shown, g_ptr_array_index (node->members, i));
}

/* The list made again from the tree, the cursor on the node it was on */
static void
debug_tree_fill (debug_tree_t *tree, const debug_node_t *current)
{
    guint i;

    g_ptr_array_set_size (tree->shown, 0);
    debug_tree_collect (tree->shown, tree->root);
    listbox_remove_list (tree->list);
    for (i = 0; i < tree->shown->len; i++)
    {
        const debug_node_t *node = g_ptr_array_index (tree->shown, i);
        const char *mark = node->var->ref == NULL ? " "
            : node->asking                        ? "~"
            : node->open                          ? "-"
                                                  : "+";
        char *text = g_strdup_printf ("%*s%s %s = %s", node->depth * 2, "", mark, node->var->name,
                                      node->var->value);

        listbox_add_item_take (tree->list, LISTBOX_APPEND_AT_END, 0, text, (void *) node, FALSE);
    }
    for (i = 0; i < tree->shown->len; i++)
        if (g_ptr_array_index (tree->shown, i) == current)
            listbox_set_current (tree->list, (int) i);
    widget_draw (WIDGET (tree->list));
}

static debug_node_t *
debug_tree_current (const debug_tree_t *tree)
{
    const int i = tree->list->current;

    return i >= 0 && i < (int) tree->shown->len ? g_ptr_array_index (tree->shown, i) : NULL;
}

static debug_node_t *
debug_node_find (debug_node_t *node, guint id)
{
    guint i;

    if (node->id == id)
        return node;
    for (i = 0; node->members != NULL && i < node->members->len; i++)
    {
        debug_node_t *found = debug_node_find (g_ptr_array_index (node->members, i), id);

        if (found != NULL)
            return found;
    }
    return NULL;
}

/* The members of a node have come: of a tree still open, or of none */
static void
debug_reply_members (void *ui, const debug_reply_t *reply, void *data)
{
    debug_node_t *node;
    guint i;

    (void) ui;
    if (debug_tree == NULL
        || (node = debug_node_find (debug_tree->root, GPOINTER_TO_UINT (data))) == NULL)
        return;
    node->asking = FALSE;
    node->members = g_ptr_array_new_with_free_func (debug_node_free);
    for (i = 0; reply->variables != NULL && i < reply->variables->len; i++)
        g_ptr_array_add (node->members,
                         debug_node_new (g_ptr_array_index (reply->variables, i), node));
    if (!reply->ok)
    {
        debug_variable_t why = { 0 };

        why.name = (char *) _ ("<error>");
        why.value = (char *) (reply->msg != NULL ? reply->msg : "");
        g_ptr_array_add (node->members, debug_node_new (&why, node));
    }
    debug_tree_fill (debug_tree, debug_tree_current (debug_tree));
}

/* A node opened, its members asked for the first time, or closed */
static void
debug_tree_toggle (debug_tree_t *tree, debug_node_t *node)
{
    if (node == NULL || node->var->ref == NULL)
        return;
    node->open = !node->open;
    if (node->open && node->members == NULL && !node->asking)
    {
        node->asking = tree->debug->backend->ops->children (tree->debug->backend, node->var->ref,
                                                            debug_reply_members,
                                                            GUINT_TO_POINTER (node->id), NULL)
            != 0;
        if (!node->asking)
            node->open = FALSE;
    }
    debug_tree_fill (tree, node);
}

static lcback_ret_t
debug_tree_activate (WListbox *l)
{
    (void) l;
    if (debug_tree != NULL)
        debug_tree_toggle (debug_tree, debug_tree_current (debug_tree));
    return LISTBOX_CONT;
}

static cb_ret_t
debug_tree_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_KEY && debug_tree != NULL && (parm == KEY_RIGHT || parm == KEY_LEFT))
    {
        debug_node_t *node = debug_tree_current (debug_tree);

        if (node == NULL)
            return MSG_HANDLED;
        if (parm == KEY_RIGHT && !node->open)
            debug_tree_toggle (debug_tree, node);
        else if (parm == KEY_LEFT && node->open)
            debug_tree_toggle (debug_tree, node);
        else if (parm == KEY_LEFT && node->parent != NULL)
            debug_tree_fill (debug_tree, node->parent);
        return MSG_HANDLED;
    }
    return dlg_default_callback (w, sender, msg, parm, data);
}

/* The value of an expression with members, a tree to open: Enter or Right opens a member, Left
   closes it; Add watch watches the member of the cursor */
static void
debug_tree_show (debugger_t *debug, const char *expression, const debug_variable_t *root)
{
    debug_tree_t tree = { 0 };
    const int dlg_w = MIN (COLS - 4, 90);
    const int list_h = MAX (5, MIN (LINES - 10, 20));
    const int dlg_h = list_h + 5;
    WDialog *dlg;
    debug_variable_t named = *root;
    debug_node_t *chosen;
    char *watch = NULL;

    named.name = (char *) expression;
    named.expression = root->expression != NULL ? root->expression : (char *) expression;
    tree.debug = debug;
    tree.root = debug_node_new (&named, NULL);
    tree.shown = g_ptr_array_new ();
    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, debug_tree_callback, NULL, "[Debugger]", _ ("Evaluate"));
    dlg->help_file = "debugger.md";
    tree.list = listbox_new (1, 1, list_h, dlg_w - 2, FALSE, debug_tree_activate);
    group_add_widget (GROUP (dlg), tree.list);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 - 14, B_USER, NORMAL_BUTTON, _ ("Add &watch"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 + 2, B_CANCEL, DEFPUSH_BUTTON, _ ("&Close"), NULL));
    debug_tree = &tree;
    // the first level open at once
    debug_tree_toggle (&tree, tree.root);

    if (dlg_run (dlg) == B_USER && (chosen = debug_tree_current (&tree)) != NULL)
        watch = debug_node_expression (chosen);
    debug_tree = NULL;
    widget_destroy (WIDGET (dlg));
    if (watch != NULL)
        (void) debug_watch_add (debug, watch);
    g_free (watch);
    // the variable object of GDB
    if (debug->backend != NULL && debug->backend->ops->release != NULL)
        debug->backend->ops->release (debug->backend, root->ref);
    debug_node_free (tree.root);
    g_ptr_array_free (tree.shown, TRUE);
}

/* The value of an expression in a dialog that adds it to the watches: a tree when it has
   members */
static void
debug_reply_inspect (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    const char *expression = (const char *) data;
    const debug_variable_t *root =
        reply->ok && reply->variables != NULL && reply->variables->len > 0
        ? g_ptr_array_index (reply->variables, 0)
        : NULL;
    char *text_value;

    debug->eval_pending = FALSE;
    if (root != NULL && root->ref != NULL)
    {
        debug_tree_show (debug, expression, root);
        return;
    }
    text_value = g_strdup_printf ("%s = %s", expression,
                                  root != NULL             ? root->value
                                      : reply->msg != NULL ? reply->msg
                                                           : _ ("<unavailable>"));
    if (root == NULL)
        debug_error (debug, text_value);
    else if (query_dialog (_ ("Evaluate"), text_value, D_NORMAL, 2, _ ("&OK"), _ ("Add &watch"))
             == 1)
        (void) debug_watch_add (debug, expression);
    g_free (text_value);
}

/* The debugger has taken a breakpoint, or has refused it */
static void
debug_reply_breakpoint (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    (void) data;
    // a breakpoint removed while the debugger was putting it: it goes from the debugger too
    for (i = 0; i < debug->dropped_tokens->len; i++)
        if (g_array_index (debug->dropped_tokens, unsigned int, i) == reply->request)
        {
            g_array_remove_index_fast (debug->dropped_tokens, i);
            if (reply->ok && reply->id != NULL)
                (void) debug->backend->ops->break_delete (debug->backend, reply->id);
            return;
        }
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (bp->pending_token != reply->request)
            continue;
        bp->pending_token = 0;
        if (!reply->ok)
        {
            // the mark stays the one of a breakpoint the debugger has not taken
            if (reply->msg != NULL)
                debug_output_console (debug, reply->msg, TRUE);
            debug_marks_show (debug, NULL);
            return;
        }
        g_free (bp->gdb_number);
        bp->gdb_number = g_strdup (reply->id);
        bp->unverified = reply->pending;
        if (reply->pending && reply->msg != NULL)
            debug_output_console (debug, reply->msg, TRUE);
        // GDB stops on the next line with code: the breakpoint goes there
        if (reply->line > 0)
        {
            bp->gdb_line = reply->line;
            if (bp->gdb_line != bp->line)
                debug_breakpoint_move (debug, bp->gdb_number, bp->gdb_line);
        }
        debug_marks_show (debug, NULL);
        return;
    }
}

/* The program has ended, or GDB has */
static void
debug_finished (debugger_t *debug)
{
    if (debug->state != DEBUG_OFF)
        debug->state = DEBUG_FINISHED;
    debug_run_timer (debug, FALSE);
    debug->run_started = 0;
    debug->breakpoints_installed = FALSE;
    debug_clear_current (debug);
    debug_marks_show (debug, NULL);
    debug_notes_clear (debug);
    g_ptr_array_set_size (debug->locals, 0);
    g_ptr_array_set_size (debug->frames, 0);
    debug_registers_clear (debug);
    g_clear_pointer (&debug->current_address, g_free);
    g_clear_pointer (&debug->disasm_address, g_free);
    g_ptr_array_set_size (debug->instructions, 0);
    if (debug->disasm_window != NULL)
        widget_draw (WIDGET (debug->disasm_window));
    debug_watches_clear_values (debug);
    debug_session_refresh (debug);
}

/* GDB could not run the program: the start is over, Start comes back */
static void
debug_reply_run (void *ui, const debug_reply_t *reply, void *data)
{
    debugger_t *debug = (debugger_t *) ui;

    (void) data;
    if (reply->ok)
        return;
    debug_error (debug, reply->msg != NULL ? reply->msg : _ ("GDB could not run the program."));
    debug_finished (debug);
}

/* Whether a source is of assembler: there the registers are the variables */
static gboolean
debug_source_is_asm (const char *file)
{
    return file != NULL
        && (g_str_has_suffix (file, ".s") || g_str_has_suffix (file, ".S")
            || g_str_has_suffix (file, ".asm"));
}

/* Whether the source of a frame is on this machine: the debug information of a library may name
   a file that is not here */
gboolean
debug_source_here (const char *file)
{
    return file != NULL && g_file_test (file, G_FILE_TEST_IS_REGULAR);
}

/* A step out of code with no source that GDB refuses, in the outermost frame for one: said in
   the console, no dialog */
static void
debug_reply_step_out (void *ui, const debug_reply_t *reply, void *data)
{
    (void) data;
    if (!reply->ok && reply->msg != NULL)
        debug_output_console ((debugger_t *) ui, reply->msg, TRUE);
}

/* The program has stopped: at a breakpoint, after a step, on a signal, or for good */
static void
debug_stopped (debugger_t *debug, const debug_stop_t *stop)
{
    const char *file = stop->file;
    long line = stop->line;
    gboolean keep_debug_focus;

    // a source that is not here is no source: nothing to open
    if (!debug_source_here (file))
    {
        file = NULL;
        line = 0;
    }
    /* a step that ends in code with no source goes on out of it, the way it came: a step out of
       main ends the program, a step into printf comes back to the call */
    if (file == NULL && stop->has_frame && debug->steps_out < 8 && !debug->instruction_step
        && (stop->reason == DEBUG_STOP_STEP || stop->reason == DEBUG_STOP_FINISH))
    {
        char *text_value = g_strdup_printf (_ ("No source of %s here: stepping out.\n"),
                                            stop->func != NULL ? stop->func : "?");

        debug->steps_out++;
        debug->state = DEBUG_STOPPED;
        debug_output_console (debug, text_value, FALSE);
        g_free (text_value);
        if (debug->backend->ops->exec (debug->backend, DEBUG_EXEC_FINISH, debug_reply_step_out,
                                       NULL, NULL)
            != 0)
            return;
    }
    debug->steps_out = 0;

    if (stop->reason == DEBUG_STOP_EXITED)
    {
        char *status = stop->exit_code != NULL
            ? g_strdup_printf (_ ("\nProgram exited with code %s.\n"), stop->exit_code)
            : g_strdup (_ ("\nProgram exited.\n"));

        debug_output_append (debug, status);
        g_free (status);
        debug_finished (debug);
        return;
    }

    if (stop->reason == DEBUG_STOP_SIGNAL)
    {
        char *text_value =
            g_strdup_printf (_ ("\nProgram received signal %s, %s.\n"),
                             stop->signal_name != NULL ? stop->signal_name : "?",
                             stop->signal_meaning != NULL ? stop->signal_meaning : "");

        debug_output_append (debug, text_value);
        g_free (text_value);
    }

    keep_debug_focus = debug->session_window != NULL
        && debug->host->window_current (debug->host) == debug->session_window;
    debug->state = DEBUG_STOPPED;
    debug->step_left = FALSE;
    debug_clear_current (debug);
    g_free (debug->current_func);
    debug->current_func = g_strdup (stop->func);
    g_free (debug->current_address);
    debug->current_address = g_strdup (stop->address);
    g_free (debug->disasm_address);
    debug->disasm_address = g_strdup (stop->address);
    // in code with no source the registers and the instructions are what there is to see
    if (file == NULL && stop->has_frame)
    {
        debug->registers_shown = TRUE;
        debug_disasm_show (debug, !keep_debug_focus);
    }
    // nor has assembler other variables to see
    if (debug_source_is_asm (file))
        debug->registers_shown = TRUE;
    if (file != NULL && line > 0 && debug->host->show_location != NULL)
        (void) debug->host->show_location (debug->host, file, line);
    if (keep_debug_focus && debug->session_window != NULL)
        debug->host->window_show (debug->host, debug->session_window);
    if (file != NULL && line > 0)
    {
        debug->current_file = g_strdup (file);
        debug->current_line = line;
    }
    if (file != NULL)
    {
        debug_breakpoints_sync (debug);
        debug_marks_show (debug, file);
    }
    (void) debug->backend->ops->select_frame (debug->backend, 0);
    (void) debug->backend->ops->stack (debug->backend, debug_reply_stack, NULL, NULL);
    (void) debug->backend->ops->variables (debug->backend, debug_reply_variables, NULL, NULL);
    debug_registers_refresh (debug);
    debug_disasm_refresh (debug);
    debug_watches_refresh (debug);
}

/* The breakpoint of GDB @number to @line, the marks of the text taken first: the sync may merge
   two breakpoints of one line, so the breakpoint is looked for after it */
static void
debug_breakpoint_move (debugger_t *debug, const char *number, long line)
{
    guint i;

    debug_breakpoints_sync (debug);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (g_strcmp0 (bp->gdb_number, number) == 0)
        {
            bp->line = line;
            debug_breakpoints_dedup (debug);
            debug_config_save (debug);
            return;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */
/* What the backend tells */
/* --------------------------------------------------------------------------------------------- */

static void
debug_event_output (void *ui, const char *text, debug_output_t kind)
{
    debugger_t *debug = (debugger_t *) ui;

    if (kind == DEBUG_OUTPUT_PROGRAM)
        debug_output_append (debug, text);
    else
        debug_output_console (debug, text, kind == DEBUG_OUTPUT_ERROR);
}

/* The program is loaded: the breakpoints go to the debugger, then it runs */
static void
debug_event_ready (void *ui)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    debug_breakpoints_sync (debug);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        g_clear_pointer (&bp->gdb_number, g_free);
        bp->pending_token = 0;
        bp->unverified = FALSE;
        (void) debug_breakpoint_install (debug, bp);
    }
    for (i = 0; i < debug->address_breakpoints->len; i++)
        debug_address_breakpoint_install (debug, g_ptr_array_index (debug->address_breakpoints, i));
    debug->breakpoints_installed = TRUE;
    debug_marks_show (debug, NULL);
    if (debug->backend->ops->exec (debug->backend, DEBUG_EXEC_RUN, debug_reply_run, NULL, NULL)
        == 0)
        debug->state = DEBUG_FINISHED;
}

static void
debug_event_start_failed (void *ui, const char *msg)
{
    debugger_t *debug = (debugger_t *) ui;

    debug->state = DEBUG_FINISHED;
    if (msg != NULL)
        debug_error (debug, msg);
    debug_session_refresh (debug);
}

static void
debug_event_running (void *ui)
{
    debugger_t *debug = (debugger_t *) ui;

    if (debug->state != DEBUG_RUNNING)
    {
        debug->run_started = g_get_monotonic_time ();
        debug_run_timer (debug, TRUE);
    }
    debug->state = DEBUG_RUNNING;
    debug_notes_clear (debug);
    debug_watches_clear_values (debug);
    debug_program_show (debug);
    debug_session_refresh (debug);
}

static void
debug_event_stopped (void *ui, const debug_stop_t *stop)
{
    debugger_t *debug = (debugger_t *) ui;
    // the program has had what was typed in its tab: the place it stops at is to be seen
    const gboolean from_program = debug_program_current (debug);

    debug->run_time = debug->run_started != 0 ? g_get_monotonic_time () - debug->run_started : 0;
    debug->run_started = 0;
    debug_run_timer (debug, FALSE);
    debug_stopped (debug, stop);
    if (from_program)
    {
        void *file = debug->host->window_top_file (debug->host);

        if (file != NULL)
            debug->host->window_show (debug->host, file);
    }
    debug_session_refresh (debug);
    widget_draw (WIDGET (debug->host->host_data));
}

static void
debug_event_exited (void *ui, gboolean gone)
{
    debugger_t *debug = (debugger_t *) ui;
    const debug_launch_t *launch = debug_active_launch (debug);

    if (gone)
        debug_requests_clear (debug);
    // an adapter that ends at once is not there to run, a module of Python missing say
    if (gone && debug->state == DEBUG_STARTING && launch != NULL && debug_launch_is_dap (launch))
    {
        const char *hint = debug_adapter_hint (launch->adapter);
        char *text_value = g_strdup_printf (
            _ ("The debug adapter ended before the program started: the Debug console says "
               "why.%s%s"),
            hint != NULL ? "\n" : "", hint != NULL ? hint : "");

        debug_finished (debug);
        debug_error (debug, text_value);
        g_free (text_value);
        return;
    }
    debug_finished (debug);
}

static void
debug_event_breakpoint_moved (void *ui, const char *id, long line)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    /* GDB says so at every hit, the count of them changed, with the line of the text the program
       was built from: only a line GDB has changed moves the breakpoint, not the one the edits
       of the text have moved it to since */
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (g_strcmp0 (bp->gdb_number, id) == 0 && bp->gdb_line != line)
        {
            bp->gdb_line = line;
            debug_breakpoint_move (debug, id, bp->gdb_line);
            debug_marks_show (debug, NULL);
            return;
        }
    }
}

static void
debug_event_breakpoint_verified (void *ui, const char *id, long line)
{
    debugger_t *debug = (debugger_t *) ui;
    guint i;

    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (g_strcmp0 (bp->gdb_number, id) != 0)
            continue;
        bp->unverified = FALSE;
        if (line > 0 && line != bp->gdb_line)
        {
            bp->gdb_line = line;
            debug_breakpoint_move (debug, id, line);
        }
        debug_marks_show (debug, NULL);
        debug_session_refresh (debug);
        return;
    }
}

static void
debug_event_error (void *ui, const char *msg, gboolean unasked)
{
    debugger_t *debug = (debugger_t *) ui;

    debug_error (debug, msg);
    if (unasked && debug->state == DEBUG_STARTING)
        debug_finished (debug);
}

static void
debug_event_flush (void *ui)
{
    (void) ui;
    tty_refresh ();
}

static const debug_backend_events_t debug_events = {
    .output = debug_event_output,
    .ready = debug_event_ready,
    .start_failed = debug_event_start_failed,
    .running = debug_event_running,
    .stopped = debug_event_stopped,
    .exited = debug_event_exited,
    .breakpoint_moved = debug_event_breakpoint_moved,
    .breakpoint_verified = debug_event_breakpoint_verified,
    .error = debug_event_error,
    .flush = debug_event_flush,
};

static gboolean
debug_breakpoint_install (debugger_t *debug, debug_breakpoint_t *bp)
{
    bp->pending_token =
        debug->backend->ops->break_insert (debug->backend, bp->file, bp->line, bp->disabled,
                                           bp->condition, debug_reply_breakpoint, NULL, NULL);
    return bp->pending_token != 0;
}

void
debug_breakpoint_remove (debugger_t *debug, guint index)
{
    debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, index);

    if (bp->gdb_number != NULL && debug_alive (debug))
        (void) debug->backend->ops->break_delete (debug->backend, bp->gdb_number);
    else if (bp->pending_token != 0)
        g_array_append_val (debug->dropped_tokens, bp->pending_token);
    g_ptr_array_remove_index (debug->breakpoints, index);
}

/* A breakpoint kept but not stopped on, or stopped on again */
void
debug_breakpoint_toggle_enabled (debugger_t *debug, guint index)
{
    debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, index);

    bp->disabled = !bp->disabled;
    if (bp->gdb_number != NULL && debug_alive (debug))
        (void) debug->backend->ops->break_enable (debug->backend, bp->gdb_number, !bp->disabled);
    debug_breakpoints_sync (debug);
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    debug_session_refresh (debug);
}

/* Two breakpoints on one line are one: the later goes */
static void
debug_breakpoints_dedup (debugger_t *debug)
{
    guint i, j;

    for (i = 0; i < debug->breakpoints->len; i++)
        for (j = i + 1; j < debug->breakpoints->len;)
        {
            const debug_breakpoint_t *a = g_ptr_array_index (debug->breakpoints, i);
            const debug_breakpoint_t *b = g_ptr_array_index (debug->breakpoints, j);

            if (a->line == b->line && debug_same_file (a->file, b->file))
                debug_breakpoint_remove (debug, j);
            else
                j++;
        }
}

static gint
debug_long_compare (gconstpointer a, gconstpointer b)
{
    const long x = *(const long *) a, y = *(const long *) b;

    return x < y ? -1 : x > y ? 1 : 0;
}

static gint
debug_breakpoint_line_compare (gconstpointer a, gconstpointer b)
{
    const debug_breakpoint_t *x = *(debug_breakpoint_t *const *) a;
    const debug_breakpoint_t *y = *(debug_breakpoint_t *const *) b;

    return x->line < y->line ? -1 : x->line > y->line ? 1 : 0;
}

/* The marks of the breakpoints move with their lines when the text is edited: the breakpoints of
   every open file take the lines of their marks */
static void
debug_breakpoints_sync (debugger_t *debug)
{
    GPtrArray *done = g_ptr_array_new ();
    gboolean moved = FALSE;
    guint i, j;

    if (debug->host->marker_lines == NULL)
    {
        g_ptr_array_free (done, TRUE);
        return;
    }
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        const debug_breakpoint_t *first = g_ptr_array_index (debug->breakpoints, i);
        GArray *lines = NULL;
        GPtrArray *mine;
        int mark;

        for (j = 0; j < done->len && !debug_same_file (g_ptr_array_index (done, j), first->file);
             j++)
            ;
        if (j < done->len)
            continue;
        g_ptr_array_add (done, first->file);

        for (mark = 0; mark < DEBUG_MARK_COUNT; mark++)
        {
            GArray *some;

            if (!debug_is_breakpoint_mark (mark))
                continue;
            some = debug->host->marker_lines (debug->host, first->file, debug->marks[mark]);
            if (some == NULL)
                continue;
            if (lines == NULL)
                lines = some;
            else
            {
                g_array_append_vals (lines, some->data, some->len);
                g_array_free (some, TRUE);
            }
        }
        if (lines == NULL)
            continue;  // no window has the file

        mine = g_ptr_array_new ();
        for (j = i; j < debug->breakpoints->len; j++)
        {
            debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, j);

            if (debug_same_file (bp->file, first->file))
                g_ptr_array_add (mine, bp);
        }
        /* a mark is never lost by editing: other counts mean the marks of the file are not
           there yet */
        if (mine->len == lines->len)
        {
            g_array_sort (lines, debug_long_compare);
            g_ptr_array_sort (mine, debug_breakpoint_line_compare);
            for (j = 0; j < mine->len; j++)
            {
                debug_breakpoint_t *bp = g_ptr_array_index (mine, j);
                const long line = g_array_index (lines, long, j);

                /* the program running was built from the text as it was: GDB keeps the
                   breakpoint on the old line, the next start puts it on the new one */
                if (bp->line != line)
                {
                    bp->line = line;
                    moved = TRUE;
                }
            }
        }
        g_ptr_array_free (mine, TRUE);
        g_array_free (lines, TRUE);
    }
    g_ptr_array_free (done, TRUE);
    if (moved)
        debug_breakpoints_dedup (debug);
}

/* The project of a file, as the project plugin sees it; NULL without that plugin */
static char *
debug_project_of (debugger_t *debug, void *edit)
{
    char *file = edit != NULL ? debug->host->get_current_file (debug->host, edit) : NULL;
    GVariantDict args;
    GVariant *reply;
    char *root = NULL;

    if (debug->host->service_call != NULL)
    {
        g_variant_dict_init (&args, NULL);
        if (file != NULL)
            g_variant_dict_insert (&args, "file", "s", file);
        reply = debug->host->service_call (debug->host, "project", "root",
                                           g_variant_dict_end (&args), NULL);
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "root", "s", &root);
            g_variant_unref (reply);
        }
    }
    // without the project plugin, by the same rules, when the file is in a project
    if (root == NULL && file != NULL)
    {
        root = project_find_root (file);
        if (!project_is_project (root))
            g_clear_pointer (&root, g_free);
    }
    g_free (file);
    return root;
}

/* Work on the project of that directory: its configurations, breakpoints and watches */
static mc_ep_result_t
debug_project_switch (debugger_t *debug, char *project)
{
    if (!g_file_test (project, G_FILE_TEST_IS_DIR))
    {
        debug_error (debug, _ ("The project directory does not exist."));
        g_free (project);
        return MC_EPR_FAILED;
    }
    if (g_strcmp0 (debug->project_dir, project) == 0)
    {
        g_free (project);
        return MC_EPR_OK;
    }
    if (debug->state == DEBUG_STARTING || debug->state == DEBUG_RUNNING
        || debug->state == DEBUG_STOPPED)
    {
        if (query_dialog (_ ("Debug"), _ ("Stop the session and switch project?"), D_NORMAL, 2,
                          _ ("&Switch"), _ ("&Cancel"))
            != 0)
        {
            g_free (project);
            return MC_EPR_FAILED;
        }
    }
    if (debug->backend != NULL)
        debug->backend->ops->stop (debug->backend);
    debug_requests_clear (debug);
    debug_pty_close (debug);
    debug_clear_current (debug);
    debug->state = DEBUG_OFF;
    // the build of the project before is no start of this one
    debug->start_after_build = FALSE;
    debug->breakpoints_installed = FALSE;
    g_ptr_array_set_size (debug->breakpoints, 0);
    g_ptr_array_set_size (debug->address_breakpoints, 0);
    debug_marks_show (debug, NULL);
    g_ptr_array_set_size (debug->launches, 0);
    g_ptr_array_set_size (debug->watches, 0);
    debug->active_launch = 0;
    g_free (debug->project_dir);
    debug->project_dir = project;
    debug->venv_told = FALSE;
    g_ptr_array_set_size (debug->frames, 0);
    debug_config_load (debug);
    debug_marks_show (debug, NULL);
    if (debug->console_window != 0)
        debug_output_show (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_open_project (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *file = edit != NULL ? debug->host->get_current_file (debug->host, edit) : NULL;
    char *suggested =
        debug->project_dir != NULL ? g_strdup (debug->project_dir) : debug_project_of (debug, edit);
    char *chosen;

    if (suggested == NULL)
        suggested = file != NULL ? g_path_get_dirname (file) : g_get_current_dir ();
    chosen = input_dialog (_ ("Open debug project"), _ ("Project directory:"), NULL, suggested,
                           INPUT_COMPLETE_FILENAMES);
    g_free (suggested);
    g_free (file);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    {
        char *project = g_canonicalize_filename (chosen, NULL);

        g_free (chosen);
        return debug_project_switch (debug, project);
    }
}

/* The debugger works on a project: the one the project plugin knows, else the one the user
   names */
static gboolean
debug_require_project (debugger_t *debug, void *edit)
{
    char *root;

    if (debug->project_dir != NULL)
        return TRUE;
    root =
        debug_project_of (debug, edit != NULL ? edit : debug->host->window_top_file (debug->host));
    if (root != NULL)
        return debug_project_switch (debug, root) == MC_EPR_OK;
    return debug_open_project (debug, edit) == MC_EPR_OK;
}

static mc_ep_result_t
debug_project_status (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    const debug_launch_t *launch = debug_active_launch (debug);
    char *message_text;

    (void) edit;
    message_text = g_strdup_printf (_ ("Project: %s\nConfiguration: %s\nExecutable: %s"),
                                    debug->project_dir != NULL ? debug->project_dir : _ ("<none>"),
                                    launch != NULL ? launch->name : _ ("<none>"),
                                    launch != NULL ? launch->executable : _ ("<none>"));
    debug->host->message (debug->host, D_NORMAL, _ ("Debug project"), message_text);
    g_free (message_text);
    return MC_EPR_OK;
}

static gboolean
debug_parse_environment (debugger_t *debug, const char *value, char ***entries)
{
    GError *error = NULL;
    int count, i;

    *entries = NULL;
    if (value == NULL || *value == '\0')
        return TRUE;
    if (!g_shell_parse_argv (value, &count, entries, &error))
    {
        debug_error (debug, error->message);
        g_clear_error (&error);
        return FALSE;
    }
    for (i = 0; i < count; i++)
    {
        const char *p = (*entries)[i];

        if (!(g_ascii_isalpha (*p) || *p == '_'))
            break;
        for (p++; g_ascii_isalnum (*p) || *p == '_'; p++)
            ;
        if (*p != '=' || strchr (p + 1, '\n') != NULL || strchr (p + 1, '\r') != NULL)
            break;
    }
    if (i == count)
        return TRUE;
    debug_error (debug,
                 _ ("Environment entries must be NAME=VALUE, separated by spaces."
                    " Quote values containing spaces."));
    g_strfreev (*entries);
    *entries = NULL;
    return FALSE;
}

static void
debug_launch_copy (debug_launch_t *to, const debug_launch_t *from)
{
    to->name = g_strdup (from->name);
    to->executable = g_strdup (from->executable);
    to->arguments = g_strdup (from->arguments);
    to->directory = g_strdup (from->directory);
    to->environment = g_strdup (from->environment);
    to->gdb_path = g_strdup (from->gdb_path);
    to->build = from->build;
    to->terminal = from->terminal;
    to->backend = g_strdup (from->backend);
    to->adapter = g_strdup (from->adapter);
    to->address = g_strdup (from->address);
    to->launch_extra = g_strdup (from->launch_extra);
}

static void
debug_launch_clear (debug_launch_t *launch)
{
    g_free (launch->name);
    g_free (launch->executable);
    g_free (launch->arguments);
    g_free (launch->directory);
    g_free (launch->environment);
    g_free (launch->gdb_path);
    g_free (launch->backend);
    g_free (launch->adapter);
    g_free (launch->address);
    g_free (launch->launch_extra);
    memset (launch, 0, sizeof (*launch));
}

/* Ask the build plugin something about the project, NULL without that plugin */
static GVariant *
debug_build_call (debugger_t *debug, const char *method, const char *key, const char *value)
{
    GVariantDict args;

    if (debug->host->service_call == NULL)
        return NULL;
    g_variant_dict_init (&args, NULL);
    g_variant_dict_insert (&args, key, "s", value);
    return debug->host->service_call (debug->host, "build", method, g_variant_dict_end (&args),
                                      NULL);
}

/* What the build plugin knows of a program: whether it has debug information, and whether the
   compiler optimized it; a word of advice when it is not fit for debugging */
static void
debug_check_program (debugger_t *debug, const char *program, const char *system,
                     const char *build_dir)
{
    GVariant *reply = debug_build_call (debug, "elf", "path", program);
    gboolean debug_info = TRUE, optimized = FALSE;
    const char *how;
    char *text_value;

    if (reply == NULL)
        return;
    (void) g_variant_lookup (reply, "debug_info", "b", &debug_info);
    (void) g_variant_lookup (reply, "optimized", "b", &optimized);
    g_variant_unref (reply);
    if (debug_info && !optimized)
        return;

    how = g_strcmp0 (system, "meson") == 0 ? "meson configure -Dbuildtype=debug %s"
        : g_strcmp0 (system, "cmake") == 0 ? "cmake -DCMAKE_BUILD_TYPE=Debug %s"
                                           : "CFLAGS='-g -O0'%s";
    {
        char *command = g_strdup_printf (
            how,
            g_strcmp0 (system, "meson") == 0 || g_strcmp0 (system, "cmake") == 0 ? build_dir : "");

        text_value = g_strdup_printf (
            debug_info
                ? _ ("%s\nwas built with optimization: the steps jump about and some variables\n"
                     "are gone.  For debugging it is built without, for example\n\n%s")
                : _ ("%s\nhas no debug information: the debugger cannot show its source.\n"
                     "It is built with it, for example\n\n%s"),
            program, command);
        g_free (command);
    }
    debug->host->message (debug->host, D_NORMAL, _ ("Debug"), text_value);
    g_free (text_value);
}

/* The debug adapter of a program by the name of its file: a program of the machine has none,
   GDB runs it */
typedef struct
{
    const char *suffix;
    const char *adapter;
    const char *address;  // the adapter is on a socket
    const char *extra;    // what its request launch needs
} debug_adapter_default_t;

static const debug_adapter_default_t debug_adapter_defaults[] = {
    { ".py", "python3 -m debugpy.adapter", NULL, NULL },
    // bash-dap runs the script in the terminal of the program by itself
    { ".sh", "bash-dap", NULL, NULL },
    { ".bash", "bash-dap", NULL, NULL },
    { ".go", "dlv dap --listen=127.0.0.1:0", "127.0.0.1:0", "{\"mode\": \"debug\"}" },
    { ".js", "js-debug-adapter 0", "127.0.0.1:0", "{\"type\": \"pwa-node\"}" },
    { ".mjs", "js-debug-adapter 0", "127.0.0.1:0", "{\"type\": \"pwa-node\"}" },
    { ".ts", "js-debug-adapter 0", "127.0.0.1:0", "{\"type\": \"pwa-node\"}" },
};

static const debug_adapter_default_t *
debug_adapter_for (const char *program)
{
    guint i;

    for (i = 0; program != NULL && i < G_N_ELEMENTS (debug_adapter_defaults); i++)
        if (g_str_has_suffix (program, debug_adapter_defaults[i].suffix))
            return &debug_adapter_defaults[i];
    return NULL;
}

/* How to get an adapter that is not there, NULL when it is not known */
static const char *
debug_adapter_hint (const char *adapter)
{
    static const struct
    {
        const char *word;
        const char *how;
    } hints[] = {
        { "debugpy", N_ ("debugpy is had with: pip install debugpy") },
        { "bash-dap",
          N_ ("bash-dap, the debugger of bash scripts, needs Python 3 alone: "
              "pipx install bash-dap, or pip install --user bash-dap") },
        { "bash-debug-adapter",
          N_ ("bash-debug-adapter is the adapter of the VS Code extension Bash Debug (the "
              "package bash-debug-adapter of Mason): node out/bashDebug.js of the extension, "
              "with Node.js; its bashdb is in its bashdb_dir") },
        { "bashDebug.js",
          N_ ("bashDebug.js is the adapter of the VS Code extension Bash Debug: it "
              "needs Node.js, and its bashdb is in its bashdb_dir") },
        { "dlv", N_ ("Delve is had with: go install github.com/go-delve/delve/cmd/dlv@latest") },
        { "lldb",
          N_ ("lldb-dap comes with LLDB (apt install lldb); before LLVM 18 it is "
              "lldb-vscode") },
        { "js-debug",
          N_ ("js-debug-adapter is the server of vscode-js-debug, with Node.js "
              "(the package js-debug-adapter of Mason, or npm)") },
        { "gdb", N_ ("GDB speaks the protocol from version 14: gdb -i dap") },
    };
    guint i;

    for (i = 0; adapter != NULL && i < G_N_ELEMENTS (hints); i++)
        if (strstr (adapter, hints[i].word) != NULL)
            return _ (hints[i].how);
    return NULL;
}

/* The Python of the virtual environment of a project, NULL when it has none */
static char *
debug_project_python (const char *project_dir)
{
    static const char *const envs[] = { ".venv", "venv", ".env", "env" };
    guint i;

    for (i = 0; project_dir != NULL && i < G_N_ELEMENTS (envs); i++)
    {
        char *python = g_build_filename (project_dir, envs[i], "bin", "python", (char *) NULL);

        if (g_file_test (python, G_FILE_TEST_IS_EXECUTABLE))
            return python;
        g_free (python);
    }
    return NULL;
}

/* Whether a Python has debugpy: the program runs under it, and the server of debugpy in it */
static gboolean
debug_python_has_debugpy (const char *python)
{
    char *argv[] = { (char *) python, (char *) "-c", (char *) "import debugpy", NULL };
    int status = -1;

    return g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                         NULL, NULL, NULL, NULL, &status, NULL)
        && status == 0;
}

/* A script of Python of a project with a virtual environment runs with its Python, with the
   packages of the project: the adapter is the debugpy of the environment, which runs the
   script with its own Python (the debugpy of the program and that of the adapter have to be of
   one version).  Without debugpy there the user is told how to get it. */
static void
debug_launch_python (debugger_t *debug, debug_launch_t *launch)
{
    char *python, *quoted;

    if (launch->executable == NULL || !g_str_has_suffix (launch->executable, ".py")
        || g_strcmp0 (launch->adapter, "python3 -m debugpy.adapter") != 0
        || (python = debug_project_python (debug->project_dir)) == NULL)
        return;
    if (!debug_python_has_debugpy (python))
    {
        if (!debug->venv_told)
        {
            char *text_value = g_strdup_printf (
                _ ("The project has a virtual environment, but its Python has no debugpy:\n"
                   "the script runs with the Python of the system, without the packages of\n"
                   "the project. With debugpy in it, it runs with them:\n\n"
                   "%s -m pip install debugpy"),
                python);

            debug->venv_told = TRUE;
            debug->host->message (debug->host, D_NORMAL, _ ("Debug"), text_value);
            g_free (text_value);
        }
        g_free (python);
        return;
    }
    // quoted only when the shell would split or change it
    quoted = strpbrk (python, " \t\n'\"\\$`*?[]{}()<>|&;~#") != NULL ? g_shell_quote (python)
                                                                     : g_strdup (python);
    g_free (launch->adapter);
    launch->adapter = g_strconcat (quoted, " -m debugpy.adapter", (char *) NULL);
    g_free (quoted);
    g_free (python);
}

/* A configuration has the adapter of its program, what the configuration has not set */
static void
debug_launch_adapter_defaults (debugger_t *debug, debug_launch_t *launch)
{
    const debug_adapter_default_t *d = debug_adapter_for (launch->executable);

    g_free (launch->backend);
    launch->backend = g_strdup ("dap");
    if ((launch->adapter == NULL || *launch->adapter == '\0')
        && (launch->address == NULL || *launch->address == '\0'))
    {
        g_free (launch->adapter);
        g_free (launch->address);
        // a program of the machine: GDB speaks the protocol too
        launch->adapter = g_strdup (d != NULL ? d->adapter : "gdb -i dap");
        launch->address = g_strdup (d != NULL ? d->address : NULL);
    }
    if ((launch->launch_extra == NULL || *launch->launch_extra == '\0') && d != NULL
        && d->extra != NULL)
    {
        g_free (launch->launch_extra);
        launch->launch_extra = g_strdup (d->extra);
    }
    debug_launch_python (debug, launch);
}

/* A new configuration as the project suggests it: the program the build has made, the root of
   the project to run it in, and a build before the start when the project can be built; else
   the file in front, when it is a script an adapter runs */
static void
debug_launch_guess (debugger_t *debug, debug_launch_t *launch, void *edit)
{
    GVariant *reply = debug_build_call (debug, "info", "root", debug->project_dir);
    const char **programs = NULL;
    const char *system = NULL, *command = NULL, *dir = NULL;
    char *program = NULL;

    launch->directory = g_strdup (debug->project_dir);
    launch->gdb_path = g_strdup ("gdb");
    launch->terminal = TRUE;
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "programs", "^a&s", &programs);
        (void) g_variant_lookup (reply, "system", "&s", &system);
        (void) g_variant_lookup (reply, "command", "&s", &command);
        (void) g_variant_lookup (reply, "dir", "&s", &dir);
        launch->build = command != NULL;
    }

    if (programs != NULL && programs[0] != NULL && programs[1] == NULL)
        program = g_strdup (programs[0]);
    else if (programs != NULL && programs[0] != NULL)
    {
        // several: the newest first, to choose from
        const guint count = g_strv_length ((char **) programs);
        Listbox *selector;
        const char *chosen;
        guint i;

        selector = listbox_window_new (MIN ((int) count, 16), MIN (COLS - 8, 76),
                                       _ ("The program to debug"), NULL);
        for (i = 0; i < count; i++)
        {
            const char *p = programs[i];
            const gsize len = strlen (debug->project_dir);
            const char *shown =
                strncmp (p, debug->project_dir, len) == 0 && p[len] == '/' ? p + len + 1 : p;

            LISTBOX_APPEND_TEXT (selector, 0, shown, (void *) p, FALSE);
        }
        chosen = listbox_run_with_data (selector, NULL);
        program = g_strdup (chosen != NULL ? chosen : programs[0]);
    }

    if (program == NULL)
    {
        void *file_window = edit != NULL ? edit : debug->host->window_top_file (debug->host);
        char *file =
            file_window != NULL ? debug->host->get_current_file (debug->host, file_window) : NULL;

        if (debug_adapter_for (file) != NULL)
        {
            launch->executable = file;
            launch->name = g_path_get_basename (file);
            launch->build = FALSE;
            debug_launch_adapter_defaults (debug, launch);
            file = NULL;
        }
        g_free (file);
    }
    if (program != NULL)
    {
        launch->executable = program;
        launch->name = g_path_get_basename (program);
        debug_check_program (debug, program, system, dir);
    }
    else if (launch->name == NULL)
        launch->name = g_strdup (_ ("Debug"));
    g_free (programs);
    if (reply != NULL)
        g_variant_unref (reply);
}

/* All of a configuration in one form; FALSE when it is cancelled */
static gboolean
debug_launch_form (debugger_t *debug, debug_launch_t *launch, const debug_launch_t *self,
                   gboolean *in_project)
{
    // the options of the index of the symbols, when the plugin ctags is there
    char *index_options = NULL;
    gboolean result = FALSE;

    if (debug->project_dir != NULL)
    {
        GVariantDict args;
        GVariant *reply;

        g_variant_dict_init (&args, NULL);
        g_variant_dict_insert (&args, "root", "s", debug->project_dir);
        reply = debug->host->service_call (debug->host, "ctags", "options",
                                           g_variant_dict_end (&args), NULL);
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "options", "s", &index_options);
            g_variant_unref (reply);
        }
    }

    while (TRUE)
    {
        char *name = NULL, *executable = NULL, *arguments = NULL, *directory = NULL;
        char *environment = NULL, *gdb_path = NULL, *ctags = NULL;
        char *adapter = NULL, *address = NULL, *extra = NULL, *problem_text = NULL;
        char **entries = NULL;
        gboolean build = launch->build, keep = *in_project, terminal = launch->terminal;
        const char *backend_items[] = { _ ("&GDB"), _ ("Debug &adapter") };
        int backend = debug_launch_is_dap (launch) ? 1 : 0;
        const char *problem = NULL;
        guint i;
        int ret;

        {
            quick_widget_t widgets[] = {
                QUICK_LABELED_INPUT (_ ("Name:"), input_label_above, launch->name, "debug-name",
                                     &name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("Program:"), input_label_above,
                                     launch->executable != NULL ? launch->executable : "",
                                     "debug-program", &executable, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES),
                QUICK_LABELED_INPUT (_ ("Arguments:"), input_label_above,
                                     launch->arguments != NULL ? launch->arguments : "",
                                     "debug-arguments", &arguments, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES),
                QUICK_LABELED_INPUT (_ ("Working directory:"), input_label_above,
                                     launch->directory != NULL ? launch->directory : "",
                                     "debug-directory", &directory, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_CD),
                QUICK_LABELED_INPUT (_ ("Environment (NAME=VALUE, quoted):"), input_label_above,
                                     launch->environment != NULL ? launch->environment : "",
                                     "debug-environment", &environment, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_RADIO (2, backend_items, &backend, NULL),
                QUICK_LABELED_INPUT (_ ("GDB:"), input_label_left,
                                     launch->gdb_path != NULL ? launch->gdb_path : "gdb",
                                     "debug-gdb", &gdb_path, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_COMMANDS),
                QUICK_LABELED_INPUT (_ ("Adapter:"), input_label_left,
                                     launch->adapter != NULL ? launch->adapter : "",
                                     "debug-adapter", &adapter, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_COMMANDS),
                QUICK_LABELED_INPUT (_ ("Address (host:port, 0: its own):"), input_label_left,
                                     launch->address != NULL ? launch->address : "",
                                     "debug-address", &address, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("Launch (JSON):"), input_label_left,
                                     launch->launch_extra != NULL ? launch->launch_extra : "",
                                     "debug-launch", &extra, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("Options of ctags, for the index of the symbols:"),
                                     input_label_above, index_options != NULL ? index_options : "",
                                     "debug-ctags", &ctags, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_SEPARATOR (TRUE),
                QUICK_CHECKBOX (_ ("&Build the project before the start"), &build, NULL),
                QUICK_CHECKBOX (_ ("Run in a &terminal window: its screen and keys"), &terminal,
                                NULL),
                QUICK_CHECKBOX (_ ("&Keep in the project, in .coole/debug.ini"), &keep, NULL),
                QUICK_BUTTONS_OK_CANCEL,
                QUICK_END,
            };
            WRect r = { -1, -1, 0, MIN (76, COLS - 4) };
            quick_dialog_t qdlg;

            // without the plugin ctags its line is not there
            if (index_options == NULL)
            {
                const size_t at = 10;

                memmove (&widgets[at], &widgets[at + 1],
                         sizeof (widgets) - (at + 1) * sizeof (widgets[0]));
            }
            qdlg = (quick_dialog_t) {
                .rect = r,
                .title = _ ("Debug configuration"),
                .help = "[Debugger]",
                .help_file = "debugger.md",
                .widgets = widgets,
                .callback = NULL,
                .mouse_callback = NULL,
            };

            ret = quick_dialog (&qdlg);
        }
        if (ret == B_CANCEL)
        {
            g_free (name);
            g_free (executable);
            g_free (arguments);
            g_free (directory);
            g_free (environment);
            g_free (gdb_path);
            g_free (ctags);
            g_free (adapter);
            g_free (address);
            g_free (extra);
            break;
        }
        if (ctags != NULL)
        {
            g_free (index_options);
            index_options = g_strdup (g_strstrip (ctags));
            g_free (ctags);
        }

        // what was typed stays for the next round
        debug_launch_clear (launch);
        launch->name = g_strstrip (name);
        launch->executable = *g_strstrip (executable) != '\0'
            ? g_canonicalize_filename (executable, debug->project_dir)
            : g_strdup ("");
        g_free (executable);
        launch->arguments = arguments;
        launch->directory = g_canonicalize_filename (
            *g_strstrip (directory) != '\0' ? directory : debug->project_dir, debug->project_dir);
        g_free (directory);
        launch->environment = environment;
        launch->gdb_path = *g_strstrip (gdb_path) != '\0' ? gdb_path : g_strdup ("gdb");
        if (*gdb_path == '\0')
            g_free (gdb_path);
        launch->build = build;
        launch->terminal = terminal;
        launch->backend = g_strdup (backend == 1 ? "dap" : "gdb-mi");
        launch->adapter = g_strdup (g_strstrip (adapter));
        launch->address = g_strdup (g_strstrip (address));
        launch->launch_extra = g_strdup (g_strstrip (extra));
        g_free (adapter);
        g_free (address);
        g_free (extra);
        // a script is no program of GDB: its adapter runs it; and an adapter needs a command
        if (debug_launch_is_dap (launch) || debug_adapter_for (launch->executable) != NULL)
            debug_launch_adapter_defaults (debug, launch);
        *in_project = keep;

        if (*launch->name == '\0')
            problem = _ ("Enter a configuration name.");
        for (i = 0; problem == NULL && i < debug->launches->len; i++)
        {
            const debug_launch_t *other = g_ptr_array_index (debug->launches, i);

            if (other != self && g_strcmp0 (other->name, launch->name) == 0)
                problem = _ ("A configuration with this name already exists.");
        }
        if (problem == NULL && *launch->executable == '\0')
            problem = _ ("Enter the program to debug.");
        if (problem == NULL && !g_file_test (launch->directory, G_FILE_TEST_IS_DIR))
            problem = _ ("The working directory does not exist.");
#ifdef ENABLE_DAP
        if (problem == NULL && *launch->launch_extra != '\0')
        {
            // checked here, where it can be put right, and not at the start
            JsonParser *parser = json_parser_new ();
            GError *error = NULL;

            if (!json_parser_load_from_data (parser, launch->launch_extra, -1, &error))
                problem = problem_text =
                    g_strdup_printf (_ ("The launch JSON is wrong: %s"), error->message);
            else if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
                problem = _ ("The launch JSON is an object: {\"name\": value, ...}.");
            g_clear_error (&error);
            g_object_unref (parser);
        }
#endif
        if (problem != NULL)
            debug_error (debug, problem);
        else if (!debug_parse_environment (debug, launch->environment, &entries))
            problem = "";
        g_strfreev (entries);
        g_free (problem_text);
        if (problem == NULL)
        {
            result = TRUE;
            break;
        }
    }

    if (result && index_options != NULL)
    {
        GVariantDict args;
        GVariant *reply;

        // the plugin ctags keeps them, and indexes again when they change
        g_variant_dict_init (&args, NULL);
        g_variant_dict_insert (&args, "root", "s", debug->project_dir);
        g_variant_dict_insert (&args, "options", "s", index_options);
        reply = debug->host->service_call (debug->host, "ctags", "set_options",
                                           g_variant_dict_end (&args), NULL);
        if (reply != NULL)
            g_variant_unref (reply);
    }
    g_free (index_options);
    return result;
}

/* A configuration made or changed: a new one is guessed from the project first */
static mc_ep_result_t
debug_configure_impl (debugger_t *debug, void *edit, gboolean create_new)
{
    debug_launch_t *existing, form = { 0 };
    gboolean in_project;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    existing = create_new ? NULL : debug_active_launch (debug);
    if (existing != NULL)
        debug_launch_copy (&form, existing);
    else
        debug_launch_guess (debug, &form, edit);
    in_project = debug->launches_in_project;

    if (!debug_launch_form (debug, &form, existing, &in_project))
    {
        debug_launch_clear (&form);
        return MC_EPR_FAILED;
    }
    if (existing == NULL)
    {
        existing = g_new0 (debug_launch_t, 1);
        g_ptr_array_add (debug->launches, existing);
        debug->active_launch = debug->launches->len - 1;
    }
    else
        debug_launch_clear (existing);
    *existing = form;
    debug->launches_in_project = in_project;
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_configure (void *data, void *edit)
{
    return debug_configure_impl ((debugger_t *) data, edit, FALSE);
}

static mc_ep_result_t
debug_new_configuration (void *data, void *edit)
{
    return debug_configure_impl ((debugger_t *) data, edit, TRUE);
}

static mc_ep_result_t
debug_select_configuration (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    Listbox *selector;
    debug_launch_t *chosen;
    guint i;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    if (debug->launches->len == 0)
        return debug_new_configuration (data, edit);
    selector = listbox_window_new (MIN ((int) debug->launches->len, 16), 70,
                                   _ ("Select debug configuration"), NULL);
    for (i = 0; i < debug->launches->len; i++)
    {
        debug_launch_t *launch = g_ptr_array_index (debug->launches, i);
        char *label = g_strdup_printf ("%s  %s", launch->name, launch->executable);

        LISTBOX_APPEND_TEXT (selector, 0, label, launch, FALSE);
        g_free (label);
    }
    chosen = listbox_run_with_data (selector, NULL);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    for (i = 0; i < debug->launches->len; i++)
        if (g_ptr_array_index (debug->launches, i) == chosen)
        {
            debug->active_launch = i;
            debug_config_save (debug);
            debug_session_refresh (debug);
            return MC_EPR_OK;
        }
    return MC_EPR_FAILED;
}

static mc_ep_result_t
debug_delete_configuration (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_launch_t *launch = debug_active_launch (debug);

    (void) edit;
    if (launch == NULL)
        return MC_EPR_FAILED;
    if (query_dialog (_ ("Debug"), _ ("Delete the selected debug configuration?"), D_NORMAL, 2,
                      _ ("&Delete"), _ ("&Cancel"))
        != 0)
        return MC_EPR_FAILED;
    g_ptr_array_remove_index (debug->launches, debug->active_launch);
    debug->active_launch = 0;
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* Whether a debug adapter runs the program of a configuration, not GDB/MI */
static gboolean
debug_launch_is_dap (const debug_launch_t *launch)
{
    return g_strcmp0 (launch->backend, "dap") == 0;
}

/* The command of the debug adapter of a configuration, NULL when it is not there to run, and
   the user told */
static char *
debug_adapter_check (debugger_t *debug, const debug_launch_t *launch)
{
    char **argv = NULL;
    char *found = NULL;
    GError *error = NULL;

#ifndef ENABLE_DAP
    (void) launch;
    (void) argv;
    (void) found;
    (void) error;
    debug_error (debug, _ ("This coole is built without the debug adapters: they need json-glib."));
    return NULL;
#else
    if (launch->adapter == NULL || *launch->adapter == '\0')
    {
        // an adapter that listens already needs no command
        if (launch->address != NULL && *launch->address != '\0')
            return g_strdup ("");
        debug_error (debug, _ ("Enter the command of the debug adapter."));
        return NULL;
    }
    if (!g_shell_parse_argv (launch->adapter, NULL, &argv, &error))
    {
        debug_error (debug, error->message);
        g_error_free (error);
        return NULL;
    }
    found = strchr (argv[0], '/') != NULL
        ? (g_file_test (argv[0], G_FILE_TEST_IS_EXECUTABLE) ? g_strdup (argv[0]) : NULL)
        : g_find_program_in_path (argv[0]);
    if (found == NULL)
    {
        const char *hint = debug_adapter_hint (launch->adapter);
        char *text_value = hint != NULL
            ? g_strdup_printf (_ ("The debug adapter %s was not found.\n%s"), argv[0], hint)
            : g_strdup_printf (_ ("The debug adapter %s was not found."), argv[0]);

        debug_error (debug, text_value);
        g_free (text_value);
        g_strfreev (argv);
        return NULL;
    }
    g_free (found);
    g_strfreev (argv);
    return g_strdup (launch->adapter);
#endif
}

static mc_ep_result_t
debug_start (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_launch_t *launch;
    GError *error = NULL;
    char **argv = NULL;
    char **environment_entries = NULL;
    int argc = 0;
    char *gdb_path, *absolute;
    const char *configured_gdb;
    debug_start_t spec = { 0 };
    debug_state_t previous;
    gboolean dap;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    if (debug->state == DEBUG_RUNNING || debug->state == DEBUG_STOPPED
        || debug->state == DEBUG_STARTING)
        return MC_EPR_FAILED;
    // the windows of debugging, those of before kept for the end of it: before the panel, the
    // build or the console come
    if (!debug->layout_pushed && debug->host->layout_push != NULL)
        debug->layout_pushed = debug->host->layout_push (debug->host, "Debug");
    if (debug_alive (debug))
        debug->backend->ops->stop (debug->backend);
    debug_pty_close (debug);
    debug_clear_current (debug);
    launch = debug_active_launch (debug);
    if (launch == NULL)
    {
        // no configuration yet: the project suggests one
        if (debug_configure_impl (debug, edit, TRUE) != MC_EPR_OK)
            return MC_EPR_FAILED;
        launch = debug_active_launch (debug);
    }
    dap = debug_launch_is_dap (launch);
    if (debug->start_after_build)
        return MC_EPR_OK;  // the build is going on: the start comes after it
    if (launch->build && !debug->built_for_start)
    {
        GVariant *reply = debug_build_call (debug, "run", "root", debug->project_dir);
        gboolean started = FALSE;

        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "started", "b", &started);
            g_variant_unref (reply);
        }
        if (started)
        {
            debug->start_after_build = TRUE;
            (void) debug_session_show (debug, NULL);
            debug_session_refresh (debug);
            return MC_EPR_OK;
        }
        if (reply != NULL)
            return MC_EPR_FAILED;  // the build plugin is there, but did not build
    }
    debug->built_for_start = FALSE;
    if (debug->host->save_modified_files != NULL
        && !debug->host->save_modified_files (debug->host, debug->project_dir))
        return MC_EPR_FAILED;
    if (!g_file_test (launch->directory, G_FILE_TEST_IS_DIR))
    {
        debug_error (debug, _ ("The working directory does not exist."));
        return MC_EPR_FAILED;
    }
    absolute = g_canonicalize_filename (launch->executable, debug->project_dir);
    // what an adapter runs may be a script, of Python or of the shell
    if (!g_file_test (absolute, dap ? G_FILE_TEST_EXISTS : G_FILE_TEST_IS_EXECUTABLE))
    {
        debug_error (debug,
                     dap ? _ ("The configured program does not exist.")
                         : _ ("The configured executable does not exist or is not "
                              "executable."));
        g_free (absolute);
        return MC_EPR_FAILED;
    }
    if (dap)
    {
        gdb_path = debug_adapter_check (debug, launch);
        if (gdb_path == NULL)
        {
            g_free (absolute);
            return MC_EPR_FAILED;
        }
    }
    else
    {
        configured_gdb =
            launch->gdb_path != NULL && *launch->gdb_path != '\0' ? launch->gdb_path : "gdb";
        gdb_path = strchr (configured_gdb, '/') != NULL
            ? g_canonicalize_filename (configured_gdb, debug->project_dir)
            : g_find_program_in_path (configured_gdb);
        if (gdb_path == NULL || !g_file_test (gdb_path, G_FILE_TEST_IS_EXECUTABLE))
        {
            debug_error (debug, _ ("The configured GDB executable was not found."));
            g_free (absolute);
            g_free (gdb_path);
            return MC_EPR_FAILED;
        }
    }
    if (launch->arguments != NULL && *launch->arguments != '\0'
        && !g_shell_parse_argv (launch->arguments, &argc, &argv, &error))
    {
        debug_error (debug, error->message);
        g_clear_error (&error);
        g_free (absolute);
        g_free (gdb_path);
        return MC_EPR_FAILED;
    }
    if (!debug_parse_environment (debug, launch->environment, &environment_entries))
    {
        g_strfreev (argv);
        g_free (absolute);
        g_free (gdb_path);
        return MC_EPR_FAILED;
    }
    // a backend of the other kind goes
    if (debug->backend != NULL && strcmp (debug->backend->ops->name, dap ? "dap" : "gdb-mi") != 0)
        g_clear_pointer (&debug->backend, debug->backend->ops->free);
    if (debug->backend == NULL)
#ifdef ENABLE_DAP
        debug->backend =
            dap ? debug_dap_new (&debug_events, debug) : debug_gdb_mi_new (&debug_events, debug);
#else
        debug->backend = debug_gdb_mi_new (&debug_events, debug);
#endif
#ifdef ENABLE_MCTERM
    // the terminal window of the program, else a terminal whose output goes to the console
    if (!(launch->terminal && debug_terminal_open (debug)) && !debug_pty_open (debug))
    {
        debug_error (debug, _ ("Could not create a terminal for the program."));
        g_strfreev (argv);
        g_strfreev (environment_entries);
        g_free (absolute);
        g_free (gdb_path);
        return MC_EPR_FAILED;
    }
#endif
    debug_requests_clear (debug);
    spec.program = absolute;
    spec.argv = argv;
    spec.directory = launch->directory;
    spec.environment = environment_entries;
    spec.tty = debug->pty_name;
    spec.debugger = gdb_path;
    spec.address = dap ? launch->address : NULL;
    spec.launch_extra = dap ? launch->launch_extra : NULL;
    // the state of a start, for what the debugger says at once
    previous = debug->state;
    debug->state = DEBUG_STARTING;
    debug->breakpoints_installed = FALSE;
    if (!debug->backend->ops->start (debug->backend, &spec, &error))
    {
        debug->state = previous;
        debug_error (debug, error != NULL ? error->message : _ ("Could not start GDB."));
        g_clear_error (&error);
        g_strfreev (argv);
        g_strfreev (environment_entries);
        g_free (absolute);
        g_free (gdb_path);
        debug_pty_close (debug);
        return MC_EPR_FAILED;
    }
    debug_watches_clear_values (debug);
    g_string_truncate (debug->console, 0);
    (void) debug_session_show (debug, NULL);
    // the program in a terminal of its own: that is what there is to see, the console when it says
    // something
    if (debug->program_terminal)
        debug_program_show (debug);
    else
        debug_output_show (debug);
    g_strfreev (argv);
    g_strfreev (environment_entries);
    g_free (absolute);
    g_free (gdb_path);
    return MC_EPR_OK;
}

/* A breakpoint on a line of a file, sent to GDB when it runs */
static void
debug_breakpoint_add (debugger_t *debug, const char *file, long line, const char *condition)
{
    char *real = realpath (file, NULL);
    debug_breakpoint_t *bp = g_new0 (debug_breakpoint_t, 1);

    bp->file = real != NULL ? g_strdup (real) : g_strdup (file);
    bp->line = line;
    bp->condition = condition != NULL && *condition != '\0' ? g_strdup (condition) : NULL;
    free (real);
    g_ptr_array_add (debug->breakpoints, bp);
    // the breakpoints of the start are sent already: this one is sent by itself
    if (debug_session_live (debug) && debug->breakpoints_installed)
        (void) debug_breakpoint_install (debug, bp);
}

/* --------------------------------------------------------------------------------------------- */
/* The functions of the project, from the plugin ctags */
/* --------------------------------------------------------------------------------------------- */

static WInput *fn_input = NULL;
static WListbox *fn_list = NULL;
static debugger_t *fn_debug = NULL;
static GPtrArray *fn_found = NULL;  // debug_function_t
static char *fn_last = NULL;

static void
debug_function_free (gpointer p)
{
    debug_function_t *f = (debug_function_t *) p;

    g_free (f->name);
    g_free (f->file);
    g_free (f);
}

/* The functions of the project whose names have the letters of @query, best first: NULL when
   the plugin ctags is not there */
GPtrArray *
debug_functions (const debugger_t *debug, const char *root, const char *file, const char *query)
{
    GVariantDict args;
    GVariant *reply, *symbols;
    GVariantIter iter;
    const char *name, *kind, *path;
    gint32 line;
    GPtrArray *found;

    g_variant_dict_init (&args, NULL);
    if (root != NULL)
        g_variant_dict_insert (&args, "root", "s", root);
    if (file != NULL)
        g_variant_dict_insert (&args, "file", "s", file);
    g_variant_dict_insert (&args, "query", "s", query);
    g_variant_dict_insert (&args, "max", "i", (gint32) (file != NULL ? 0 : 500));
    reply = debug->host->service_call (debug->host, "ctags", "symbols", g_variant_dict_end (&args),
                                       NULL);
    if (reply == NULL)
        return NULL;
    found = g_ptr_array_new_with_free_func (debug_function_free);
    symbols = g_variant_lookup_value (reply, "symbols", G_VARIANT_TYPE ("a(sssi)"));
    if (symbols != NULL)
    {
        g_variant_iter_init (&iter, symbols);
        while (g_variant_iter_next (&iter, "(&s&s&si)", &name, &kind, &path, &line))
            if (strcmp (kind, "func") == 0)
            {
                debug_function_t *f = g_new (debug_function_t, 1);

                f->name = g_strdup (name);
                f->file = g_strdup (path);
                f->line = line;
                g_ptr_array_add (found, f);
            }
        g_variant_unref (symbols);
    }
    g_variant_unref (reply);
    return found;
}

static void
debug_function_refilter (void)
{
    const char *text = input_get_ctext (fn_input);
    guint i;

    if (fn_last != NULL && strcmp (fn_last, text) == 0)
        return;
    g_free (fn_last);
    fn_last = g_strdup (text);
    listbox_remove_list (fn_list);
    if (fn_found != NULL)
        g_ptr_array_unref (fn_found);
    fn_found = *text != '\0' ? debug_functions (fn_debug, fn_debug->project_dir, NULL, text) : NULL;
    for (i = 0; fn_found != NULL && i < fn_found->len; i++)
    {
        const debug_function_t *f = g_ptr_array_index (fn_found, i);
        const char *shown = f->file;
        char *label;

        if (g_str_has_prefix (shown, fn_debug->project_dir)
            && shown[strlen (fn_debug->project_dir)] == '/')
            shown += strlen (fn_debug->project_dir) + 1;
        label = g_strdup_printf ("%-40s %s:%ld", f->name, shown, f->line);
        listbox_add_item (fn_list, LISTBOX_APPEND_AT_END, 0, label, (void *) f, FALSE);
        g_free (label);
    }
    listbox_select_first (fn_list);
    widget_draw (WIDGET (fn_list));
}

static cb_ret_t
debug_function_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
    {
        cb_ret_t ret = dlg_default_callback (w, sender, msg, parm, data);

        widget_select (WIDGET (fn_input));
        return ret;
    }
    case MSG_KEY:
        if (parm == KEY_UP || parm == KEY_DOWN || parm == KEY_PPAGE || parm == KEY_NPAGE)
        {
            send_message (WIDGET (fn_list), w, MSG_KEY, parm, data);
            return MSG_HANDLED;
        }
        return dlg_default_callback (w, sender, msg, parm, data);
    case MSG_POST_KEY:
        debug_function_refilter ();
        return MSG_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

static lcback_ret_t
debug_function_activate (WListbox *l)
{
    (void) l;
    return LISTBOX_DONE;
}

/* Choose a function of the project by the letters of its name; NULL when none is chosen, and
 *@name with what was typed when the plugin ctags is not there to list them */
static debug_function_t *
debug_function_pick (debugger_t *debug, const char *title, char **name)
{
    WDialog *dlg;
    debug_function_t *chosen = NULL;
    int dlg_h, dlg_w, list_h;
    GPtrArray *probe;

    *name = NULL;
    if (debug->project_dir == NULL)
        return NULL;
    // without ctags: a name to type
    probe = debug_functions (debug, debug->project_dir, NULL, "");
    if (probe == NULL)
    {
        *name = input_dialog (title, _ ("Function:"), "debug-function", "", INPUT_COMPLETE_NONE);
        return NULL;
    }
    g_ptr_array_unref (probe);

    dlg_w = MIN (COLS - 4, 90);
    list_h = MAX (5, MIN (LINES - 10, 16));
    dlg_h = list_h + 6;
    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, debug_function_callback, NULL, "[Debugger]", title);
    dlg->help_file = "debugger.md";
    fn_input = input_new (1, 1, input_colors, dlg_w - 2, "", "debug-function", INPUT_COMPLETE_NONE);
    group_add_widget (GROUP (dlg), fn_input);
    fn_list = listbox_new (3, 1, list_h, dlg_w - 2, FALSE, debug_function_activate);
    group_add_widget (GROUP (dlg), fn_list);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 - 10, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 + 1, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));
    fn_debug = debug;
    g_clear_pointer (&fn_last, g_free);
    debug_function_refilter ();

    if (dlg_run (dlg) == B_ENTER)
    {
        char *text = NULL;
        const debug_function_t *f = NULL;

        listbox_get_current (fn_list, &text, (void **) &f);
        if (f != NULL)
        {
            chosen = g_new (debug_function_t, 1);
            chosen->name = g_strdup (f->name);
            chosen->file = g_strdup (f->file);
            chosen->line = f->line;
        }
        else if (*input_get_ctext (fn_input) != '\0')
            *name = g_strdup (input_get_ctext (fn_input));
    }
    widget_destroy (WIDGET (dlg));
    fn_input = NULL;
    fn_list = NULL;
    fn_debug = NULL;
    g_clear_pointer (&fn_last, g_free);
    g_clear_pointer (&fn_found, g_ptr_array_unref);
    return chosen;
}

/* --------------------------------------------------------------------------------------------- */

/* Debug > Breakpoint on function: on the line of the function the index has */
static mc_ep_result_t
debug_act_function_breakpoint (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_function_t *f;
    char *name;
    guint i;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    f = debug_function_pick (debug, _ ("Breakpoint on function"), &name);
    if (f == NULL)
    {
        if (name != NULL)
            debug_error (debug,
                         _ ("The functions of the project come from the index of the "
                            "plugin ctags: put the breakpoint on a line of the function."));
        g_free (name);
        return MC_EPR_FAILED;
    }
    debug_breakpoints_sync (debug);
    if (debug_breakpoint_at (debug, f->file, f->line, &i) == NULL)
    {
        debug_breakpoint_add (debug, f->file, f->line, NULL);
        debug_config_save (debug);
    }
    (void) debug->host->show_location (debug->host, f->file, f->line);
    debug_marks_show (debug, NULL);
    debug_session_refresh (debug);
    debug_function_free (f);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* Debug > Show the stop line: the source at the line the program is stopped at, the cursor
   having gone elsewhere */
static mc_ep_result_t
debug_act_show_stop (void *data, void *edit)
{
    return debug_run_command ((debugger_t *) data, DEBUG_CMD_SHOW_STOP, edit) ? MC_EPR_OK
                                                                              : MC_EPR_FAILED;
}

/* Debug > Run to function: a breakpoint the debugger takes off when it stops there */
static mc_ep_result_t
debug_act_run_to_function (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_function_t *f;
    char *name;
    gboolean sent;

    (void) edit;
    if (debug->state != DEBUG_STOPPED)
    {
        debug_error (debug, _ ("The program runs to a function from where it is stopped."));
        return MC_EPR_FAILED;
    }
    f = debug_function_pick (debug, _ ("Run to function"), &name);
    if (f != NULL)
    {
        const debug_launch_t *launch = debug_active_launch (debug);

        g_free (name);
        // a debug adapter takes the name only, what a function breakpoint of the protocol is
        if (launch != NULL && debug_launch_is_dap (launch))
            name = g_strdup (f->name);
        else
        {
            // GDB the function by its file, as two of them may have the name (static ones)
            char *base = g_path_get_basename (f->file);

            name = g_strdup_printf ("%s:%s", base, f->name);
            g_free (base);
        }
        debug_function_free (f);
    }
    if (name == NULL || *name == '\0')
    {
        g_free (name);
        return MC_EPR_FAILED;
    }
    sent = debug->backend->ops->break_function (debug->backend, name, TRUE) != 0
        && debug->backend->ops->exec (debug->backend, DEBUG_EXEC_CONTINUE, NULL, NULL, NULL) != 0;
    g_free (name);
    return sent ? MC_EPR_OK : MC_EPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

/* The condition of @bp asked for, the one it has to be changed: an empty one takes it off */
void
debug_breakpoint_condition (debugger_t *debug, debug_breakpoint_t *bp)
{
    char *condition;

    if (bp->pending_token != 0)
    {
        debug_error (debug, _ ("Wait for the debugger to confirm this breakpoint."));
        return;
    }
    condition = input_dialog (
        _ ("Breakpoint condition"), _ ("Stop there only when this is true (empty: each time):"),
        "debug-condition", bp->condition != NULL ? bp->condition : "", INPUT_COMPLETE_NONE);
    if (condition == NULL)
        return;
    if (*g_strstrip (condition) == '\0')
        g_clear_pointer (&condition, g_free);
    g_free (bp->condition);
    bp->condition = condition;
    // the debugger has it already: changed there too
    if (debug_session_live (debug) && bp->gdb_number != NULL
        && debug->backend->ops->break_condition != NULL)
        (void) debug->backend->ops->break_condition (debug->backend, bp->gdb_number,
                                                     condition != NULL ? condition : "");
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    debug_session_refresh (debug);
}

/* Alt-F6: the condition of the breakpoint of the line of the cursor, a breakpoint put there
   with it when there is none */
static mc_ep_result_t
debug_condition_at_cursor (debugger_t *debug, void *edit)
{
    debug_breakpoint_t *bp;
    char *file, *condition;
    long line;

    if (edit == NULL || !debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    file = debug->host->get_current_file (debug->host, edit);
    line = debug->host->get_cursor_line (debug->host, edit);
    if (file == NULL || line <= 0)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    debug_breakpoints_sync (debug);
    bp = debug_breakpoint_at (debug, file, line, NULL);
    if (bp != NULL)
    {
        g_free (file);
        debug_breakpoint_condition (debug, bp);
        return MC_EPR_OK;
    }
    condition = input_dialog (_ ("Breakpoint condition"),
                              _ ("Stop there only when this is true (empty: each time):"),
                              "debug-condition", "", INPUT_COMPLETE_NONE);
    if (condition == NULL)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    debug_breakpoint_add (debug, file, line, g_strstrip (condition));
    g_free (condition);
    g_free (file);
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* Debug > Breakpoint condition */
static mc_ep_result_t
debug_act_condition (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    return debug_condition_at_cursor (
        debug, edit != NULL ? edit : debug->host->window_top_file (debug->host));
}

static mc_ep_result_t
debug_toggle_breakpoint (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_breakpoint_t *bp;
    char *file;
    long line;
    guint i = 0;

    if (edit == NULL)
        return MC_EPR_FAILED;
    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    file = debug->host->get_current_file (debug->host, edit);
    line = debug->host->get_cursor_line (debug->host, edit);
    if (file == NULL || line <= 0)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    debug_breakpoints_sync (debug);
    bp = debug_breakpoint_at (debug, file, line, &i);
    if (bp != NULL)
    {
        if (bp->pending_token != 0)
        {
            debug_error (debug, _ ("Wait for GDB to confirm this breakpoint."));
            g_free (file);
            return MC_EPR_FAILED;
        }
        debug_breakpoint_remove (debug, i);
        g_free (file);
    }
    else
    {
        debug_breakpoint_add (debug, file, line, NULL);
        g_free (file);
    }
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_control (void *data, void *edit, debug_exec_t what, debug_state_t required)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    if (debug->state != required || debug->backend == NULL)
        return MC_EPR_FAILED;
    debug->instruction_step =
        what == DEBUG_EXEC_STEP_INSTRUCTION || what == DEBUG_EXEC_NEXT_INSTRUCTION;
    // a step by an instruction moves in no line of the source: the instructions show it
    if (debug->instruction_step && debug->disasm_window == NULL)
        debug_disasm_show (debug, FALSE);
    return debug->backend->ops->exec (debug->backend, what, NULL, NULL, NULL) != 0 ? MC_EPR_OK
                                                                                   : MC_EPR_FAILED;
}

static mc_ep_result_t
debug_continue (void *data, void *edit)
{
    return debug_control (data, edit, DEBUG_EXEC_CONTINUE, DEBUG_STOPPED);
}
static mc_ep_result_t
debug_pause (void *data, void *edit)
{
    return debug_control (data, edit, DEBUG_EXEC_PAUSE, DEBUG_RUNNING);
}
static mc_ep_result_t
debug_next (void *data, void *edit)
{
    return debug_control (data, edit, DEBUG_EXEC_NEXT, DEBUG_STOPPED);
}
static mc_ep_result_t
debug_step (void *data, void *edit)
{
    return debug_control (data, edit, DEBUG_EXEC_STEP, DEBUG_STOPPED);
}
static mc_ep_result_t
debug_step_instruction (void *data, void *edit)
{
    return debug_control (data, edit, DEBUG_EXEC_STEP_INSTRUCTION, DEBUG_STOPPED);
}
static mc_ep_result_t
debug_next_instruction (void *data, void *edit)
{
    return debug_control (data, edit, DEBUG_EXEC_NEXT_INSTRUCTION, DEBUG_STOPPED);
}
static mc_ep_result_t
debug_finish (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    /* out of the outermost frame, main, there is nowhere to step to: the program goes on, to
       its end or to the next breakpoint, as GDB would refuse "finish" */
    if (debug->state == DEBUG_STOPPED && debug->frames->len == 1)
    {
        debug_output_console (debug, _ ("Step out of the outermost frame: the program goes on.\n"),
                              FALSE);
        return debug_control (data, edit, DEBUG_EXEC_CONTINUE, DEBUG_STOPPED);
    }
    return debug_control (data, edit, DEBUG_EXEC_FINISH, DEBUG_STOPPED);
}

static mc_ep_result_t
debug_run_to_cursor (debugger_t *debug, void *edit)
{
    char *file;
    long line;
    gboolean sent;

    if (edit == NULL || debug->state != DEBUG_STOPPED)
        return MC_EPR_FAILED;
    file = debug->host->get_current_file (debug->host, edit);
    line = debug->host->get_cursor_line (debug->host, edit);
    if (file == NULL || line <= 0)
    {
        g_free (file);
        return MC_EPR_FAILED;
    }
    sent = debug->backend->ops->run_to (debug->backend, file, line) != 0;
    g_free (file);
    return sent ? MC_EPR_OK : MC_EPR_FAILED;
}

/* The selection when it is on one line, else the word under the cursor */
static char *
debug_expression_at_cursor (debugger_t *debug, void *edit)
{
    WEdit *e = (WEdit *) edit;
    off_t start, end;

    if (e == NULL)
        return NULL;
    if (eval_marks (e, &start, &end) && end > start && end - start <= 256)
    {
        GString *text = g_string_sized_new ((gsize) (end - start));
        off_t i;

        for (i = start; i < end; i++)
        {
            const int c = edit_buffer_get_byte (&e->buffer, i);

            if (c == '\n')
                break;
            g_string_append_c (text, (char) c);
        }
        if (i == end)
        {
            g_strstrip (text->str);
            if (text->str[0] != '\0')
                return g_string_free (text, FALSE);
        }
        g_string_free (text, TRUE);
    }
    return debug->host->get_cursor_word (debug->host, edit);
}

/* The value of an expression, in a dialog that can add it to the watches; takes @expression */
mc_ep_result_t
debug_evaluate_text (debugger_t *debug, char *expression)
{
    gboolean sent;

    if (debug->state != DEBUG_STOPPED || debug->eval_pending)
    {
        g_free (expression);
        return MC_EPR_FAILED;
    }
    // looked into when the debugger can, its members a tree
    if (debug->backend->ops->inspect != NULL)
        sent = debug->backend->ops->inspect (debug->backend, expression, debug_reply_inspect,
                                             expression, g_free)
            != 0;
    else
        sent = debug->backend->ops->evaluate (debug->backend, expression, debug_reply_evaluate,
                                              expression, g_free)
            != 0;
    debug->eval_pending = sent;
    return sent ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
debug_evaluate (debugger_t *debug, void *edit)
{
    char *expression;

    if (debug->state != DEBUG_STOPPED || debug->eval_pending)
        return MC_EPR_FAILED;
    expression = debug_expression_at_cursor (debug, edit);
    if (expression == NULL)
    {
        expression =
            input_dialog (_ ("Evaluate"), _ ("Expression:"), NULL, "", INPUT_COMPLETE_NONE);
        if (expression == NULL)
            return MC_EPR_FAILED;
        g_strstrip (expression);
        if (*expression == '\0')
        {
            g_free (expression);
            return MC_EPR_FAILED;
        }
    }
    return debug_evaluate_text (debug, expression);
}

/* The service "debugger", for the plugin terminal:
   program_key (key) -> taken: a key typed in the tab Program, debug_program_key () */
static GVariant *
debug_service_call (void *data, const char *method, GVariant *args, GError **error)
{
    debugger_t *debug = (debugger_t *) data;
    GVariantDict reply;
    int key = 0;

    if (strcmp (method, "program_key") != 0)
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD, "debugger: no method %s",
                     method);
        return NULL;
    }
    if (args == NULL || !g_variant_lookup (args, "key", "i", &key))
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "debugger: no key");
        return NULL;
    }
    g_variant_dict_init (&reply, NULL);
    g_variant_dict_insert (&reply, "taken", "b", debug_program_key (debug, key));
    return g_variant_dict_end (&reply);
}

static mc_ep_result_t
debug_handle_key (void *data, int key, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    int cmd;

    cmd = debug_command_of_key (debug, key);
    /* a window that is no file: the panel of the debugger and back is the key that leaves it, all
       the others are the window's; the tab Program has its keys through the service */
    if (edit == NULL)
        return cmd == DEBUG_CMD_PANEL && debug_run_command (debug, cmd, NULL)
            ? MC_EPR_OK
            : MC_EPR_NOT_SUPPORTED;
    // Stop, Ctrl-F2 and Shift-F5, is the editor's Save as and Insert file while nothing is debugged
    if (cmd == DEBUG_CMD_STOP && !debug_session_live (debug))
        return MC_EPR_NOT_SUPPORTED;
    // debug mode: the commands of the debugger have their keys, the editor's F3 to F8 too
    if (!debug_stepping (debug) && debug->debug_mode && cmd != DEBUG_CMD_NONE
        && cmd != DEBUG_CMD_HELP && cmd != DEBUG_CMD_CLOSE && cmd != DEBUG_CMD_LEAVE
        && cmd != DEBUG_CMD_EVALUATE && cmd != DEBUG_CMD_STEP_INSTRUCTION
        && cmd != DEBUG_CMD_NEXT_INSTRUCTION)
    {
        // a step with nothing running is no Search of the editor either
        if (!debug_run_command (debug, cmd, edit))
            tty_beep ();
        return MC_EPR_OK;
    }
    /* out of step mode, a key of the debugger the editor has nothing on is the debugger's:
       Alt-F5, Ctrl-B, Ctrl-F8, Shift-F9; F5 stays Copy, Ctrl-F2 Save as unless a program is
       debugged */
    if (!debug_stepping (debug))
    {
        // the program debugged, Stop is the debugger's
        if (cmd == DEBUG_CMD_STOP)
            return debug_run_command (debug, cmd, edit) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
        if (cmd == DEBUG_CMD_NONE || cmd == DEBUG_CMD_HELP || cmd == DEBUG_CMD_CLOSE
            || cmd == DEBUG_CMD_LEAVE || cmd == DEBUG_CMD_EVALUATE
            || keybind_lookup_keymap_command (WIDGET (edit)->keymap, key) != CK_IgnoreKey)
            return MC_EPR_NOT_SUPPORTED;
        return debug_run_command (debug, cmd, edit) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
    }

    // Help and Quit stay the editor's
    if (cmd != DEBUG_CMD_HELP && cmd != DEBUG_CMD_CLOSE && debug_run_command (debug, cmd, edit))
        return MC_EPR_OK;

    if (debug_step_passes (keybind_lookup_keymap_command (WIDGET (edit)->keymap, key)))
        return MC_EPR_NOT_SUPPORTED;
    // a key that would change the text
    tty_beep ();
    return MC_EPR_OK;
}

/* A command of the debugger from the button bar of a file window */
static mc_ep_result_t
debug_handle_action (void *data, long command, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    const int cmd = debug_command (debug, command);

    if (cmd == DEBUG_CMD_HELP || !debug_run_command (debug, cmd, edit))
        return MC_EPR_NOT_SUPPORTED;
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_handle_event (void *data, void *edit, int event_id, void *payload)
{
    debugger_t *debug = (debugger_t *) data;

    (void) payload;
    // the values are of the text as it was
    if (event_id == MC_EP_EVENT_TEXT_CHANGED && edit != NULL
        && debug->host->clear_line_notes != NULL)
    {
        char *file = debug->host->get_current_file (debug->host, edit);

        if (file != NULL)
            debug->host->clear_line_notes (debug->host, file);
        g_free (file);
        return MC_EPR_NOT_SUPPORTED;
    }
    if (event_id != MC_EP_EVENT_FOCUS_IN || edit == NULL
        || !(debug_stepping (debug) || debug->debug_mode))
        return MC_EPR_NOT_SUPPORTED;
    debug_editor_buttonbar (debug, WIDGET (edit));
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_show_stack (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    Listbox *selector;
    debug_frame_t *frame;
    guint i;

    (void) edit;
    if (debug->state != DEBUG_STOPPED || debug->frames->len == 0)
        return MC_EPR_FAILED;
    selector = listbox_window_new (MIN ((int) debug->frames->len, 16), 70, _ ("Call stack"), NULL);
    for (i = 0; i < debug->frames->len; i++)
    {
        frame = g_ptr_array_index (debug->frames, i);
        LISTBOX_APPEND_TEXT (selector, 0, frame->label, frame, FALSE);
    }
    frame = listbox_run_with_data (selector, NULL);
    if (frame == NULL)
        return MC_EPR_FAILED;
    debug_select_frame (debug, frame);
    return MC_EPR_OK;
}

/* A frame of the call stack chosen: its source, its variables, and the mark of the line */
void
debug_select_frame (debugger_t *debug, const debug_frame_t *frame)
{
    if (debug_source_here (frame->file) && frame->line > 0 && debug->host->show_location != NULL)
        (void) debug->host->show_location (debug->host, frame->file, frame->line);
    (void) debug->backend->ops->select_frame (debug->backend, frame->level);
    (void) debug->backend->ops->variables (debug->backend, debug_reply_variables, NULL, NULL);
    debug_registers_refresh (debug);
    g_free (debug->disasm_address);
    debug->disasm_address = g_strdup (frame->address);
    debug_disasm_refresh (debug);
    debug_watches_refresh (debug);
    // the mark goes with the frame: the steps go on from there
    if (debug_source_here (frame->file) && frame->line > 0)
    {
        debug_clear_current (debug);
        debug->current_file = g_strdup (frame->file);
        debug->current_line = frame->line;
        debug_breakpoints_sync (debug);
        debug_marks_show (debug, frame->file);
    }
    debug_session_refresh (debug);
}

static mc_ep_result_t
debug_watch_add (debugger_t *debug, const char *expression)
{
    debug_watch_t *watch;
    guint i;

    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *existing = g_ptr_array_index (debug->watches, i);

        if (g_strcmp0 (existing->expression, expression) == 0)
        {
            debug_error (debug, _ ("This expression is already watched."));
            return MC_EPR_FAILED;
        }
    }
    watch = g_new0 (debug_watch_t, 1);
    watch->expression = g_strdup (expression);
    g_ptr_array_add (debug->watches, watch);
    debug_config_save (debug);
    if (debug->state == DEBUG_STOPPED)
        debug_watches_refresh (debug);
    else
        debug_watches_show (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* An expression asked for, the selection or the word under the cursor of the file there to be
   taken as it is or changed; NULL when none is given */
static char *
debug_ask_expression (debugger_t *debug, void *edit, const char *title)
{
    void *file = edit != NULL ? edit : debug->host->window_top_file (debug->host);
    char *guess = file != NULL ? debug_expression_at_cursor (debug, file) : NULL;
    char *expression;

    expression = input_dialog (title, _ ("Expression:"), "debug-expression",
                               guess != NULL ? guess : "", INPUT_COMPLETE_NONE);
    g_free (guess);
    if (expression != NULL && *g_strstrip (expression) == '\0')
        g_clear_pointer (&expression, g_free);
    return expression;
}

/* Debug > Run to cursor: the line of the cursor of the file in front */
static mc_ep_result_t
debug_act_run_to_cursor (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->state != DEBUG_STOPPED)
    {
        debug_error (debug, _ ("The program runs to the cursor from where it is stopped."));
        return MC_EPR_FAILED;
    }
    return debug_run_to_cursor (debug,
                                edit != NULL ? edit : debug->host->window_top_file (debug->host));
}

/* Debug > Evaluate expression: its value, in the dialog that can add it to the watches */
static mc_ep_result_t
debug_act_evaluate (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *expression;

    if (debug->state != DEBUG_STOPPED)
    {
        debug_error (debug, _ ("An expression has a value while the program is stopped."));
        return MC_EPR_FAILED;
    }
    expression = debug_ask_expression (debug, edit, _ ("Evaluate"));
    return expression != NULL ? debug_evaluate_text (debug, expression) : MC_EPR_FAILED;
}

mc_ep_result_t
debug_add_watch (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *expression;
    mc_ep_result_t result = MC_EPR_FAILED;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    expression = debug_ask_expression (debug, edit, _ ("Add watch"));
    if (expression == NULL)
        return MC_EPR_FAILED;
    g_strstrip (expression);
    if (*expression != '\0')
        result = debug_watch_add (debug, expression);
    g_free (expression);
    return result;
}

static mc_ep_result_t
debug_remove_watch (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    Listbox *selector;
    debug_watch_t *chosen;
    guint i;

    (void) edit;
    if (debug->watches->len == 0)
        return MC_EPR_FAILED;
    selector =
        listbox_window_new (MIN ((int) debug->watches->len, 16), 70, _ ("Remove watch"), NULL);
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        LISTBOX_APPEND_TEXT (selector, 0, watch->expression, watch, FALSE);
    }
    chosen = listbox_run_with_data (selector, NULL);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    for (i = 0; i < debug->watches->len; i++)
        if (g_ptr_array_index (debug->watches, i) == chosen)
        {
            g_ptr_array_remove_index (debug->watches, i);
            debug_config_save (debug);
            debug_watches_show (debug);
            debug_session_refresh (debug);
            return MC_EPR_OK;
        }
    return MC_EPR_FAILED;
}

static mc_ep_result_t
debug_show_watches (void *data, void *edit)
{
    // the watches are in the panel
    return debug_session_show (data, edit);
}

static mc_ep_result_t
debug_stop (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    // a start waiting for the build is called off too
    debug->start_after_build = FALSE;
    if (debug->backend != NULL)
        debug->backend->ops->stop (debug->backend);
    debug_requests_clear (debug);
    debug_pty_close (debug);
    debug_clear_current (debug);
    debug_watches_clear_values (debug);
    debug->state = DEBUG_OFF;
    debug->breakpoints_installed = FALSE;
    debug_marks_show (debug, NULL);
    debug_notes_clear (debug);
    g_ptr_array_set_size (debug->locals, 0);
    g_ptr_array_set_size (debug->frames, 0);
    debug_registers_clear (debug);
    debug_session_refresh (debug);
    // the windows as they were before, the output kept
    if (debug->layout_pushed)
    {
        debug->layout_pushed = FALSE;
        debug->host->layout_pop (debug->host);
    }
    return MC_EPR_OK;
}

/* coole --debug, the editor up: the panel, with the focus, where F5 starts */
/* The build before a start is done: the start goes on, or the errors are shown */
static void
debug_build_finished (const char *name, const char *signal, GVariant *args, void *user_data)
{
    debugger_t *debug = (debugger_t *) user_data;
    gboolean ok = FALSE;
    const char *root = NULL;

    (void) name;
    if (strcmp (signal, "finished") != 0 || !debug->start_after_build)
        return;
    (void) g_variant_lookup (args, "ok", "b", &ok);
    (void) g_variant_lookup (args, "root", "&s", &root);
    if (g_strcmp0 (root, debug->project_dir) != 0)
        return;
    debug->start_after_build = FALSE;
    debug_session_refresh (debug);
    if (!ok)
    {
        debug_error (debug,
                     _ ("The build failed: the lines with errors are marked, and\n"
                        "Alt-Shift-J goes from one to the next."));
        return;
    }
    debug->built_for_start = TRUE;
    (void) debug_start (debug, NULL);
    debug->built_for_start = FALSE;
}

static const mc_ep_window_kind_t debug_window_kinds[] = {
    {
        .name = "debugger.panel",
        .window = debug_panel_window,
        .label = N_ ("&Debugger panel"),
        .section = DEBUG_KEYMAP_SECTION,
        .command = "DebugPanel",
        .state = debug_panel_state,
        .show = debug_panel_show,
        .close = debug_panel_close,
    },
    {
        .name = "debugger.console",
        .window = debug_console_window,
        .label = N_ ("Debug c&onsole"),
        .state = debug_console_state,
        .show = debug_console_show,
        .close = debug_console_close,
    },
    {
        .name = "debugger.disassembly",
        .window = debug_disasm_window,
        .label = N_ ("Disassembl&y"),
        .state = debug_disasm_state,
        .show = debug_disasm_kind_show,
        .close = debug_disasm_kind_close,
    },
};

static void *
debug_open (mc_editor_host_t *host, void *editor_dialog)
{
    debugger_t *debug = g_new0 (debugger_t, 1);
    int i;

    (void) editor_dialog;
    debug->host = host;
    debug->launches = g_ptr_array_new_with_free_func (debug_launch_free);
    debug->breakpoints = g_ptr_array_new_with_free_func (debug_breakpoint_free);
    debug->watches = g_ptr_array_new_with_free_func (debug_watch_free);
    debug->console = g_string_new (NULL);
    debug->dropped_tokens = g_array_new (FALSE, FALSE, sizeof (unsigned int));
    debug->locals = g_ptr_array_new_with_free_func (debug_variable_free);
    debug->frames = g_ptr_array_new_with_free_func (debug_frame_free);
    debug->registers = g_ptr_array_new_with_free_func (debug_variable_free);
    debug->instructions = g_ptr_array_new_with_free_func (debug_instruction_free);
    debug->address_breakpoints = g_ptr_array_new_with_free_func (debug_address_breakpoint_free);
    debug->pty_master = -1;
    debug->pty_slave = -1;
    debug->run_timer = -1;
    host->commands_register (host, DEBUG_KEYMAP_SECTION, N_ ("&Debugger"), debug_commands);
    if (host->window_kind != NULL)
        for (i = 0; i < (int) G_N_ELEMENTS (debug_window_kinds); i++)
            host->window_kind (host, &debug_window_kinds[i], debug);
    debug->build_signal = host->service_connect (host, "build", debug_build_finished, debug);
    debug->service = host->service_register (host, DEBUG_SERVICE, debug_service_call, debug, NULL);
    for (i = 0; i < DEBUG_CMD_COUNT; i++)
        debug->commands[i] = host->command_id (host, debug_commands[i].name);
    for (i = 0; i < DEBUG_MARK_COUNT; i++)
    {
        debug->marks[i] =
            host->marker_kind != NULL ? host->marker_kind (host, &debug_mark_kinds[i]) : -1;
        debug->glyphs[i] = mc_skin_get ("widget-editor", debug_mark_kinds[i].glyph_key,
                                        mc_global.utf8_display ? debug_mark_kinds[i].glyph
                                                               : debug_mark_kinds[i].glyph_ascii);
    }
    if (host->startup_option (host, "debug") != NULL)
    {
        (void) debug_project_switch (debug, g_strdup (host->startup_option (host, "debug")));
        // the panel comes with the layout Debug the editor puts the windows in
        debug->debug_mode = TRUE;
    }
    return debug;
}

static void
debug_close (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->session_window != NULL)
        debug->session_window->debug = NULL;
    if (debug->disasm_window != NULL)
        debug->disasm_window->debug = NULL;
    if (debug->backend != NULL)
        debug->backend->ops->free (debug->backend);
    debug_clear_current (debug);
    debug_pty_close (debug);
    if (debug->run_timer >= 0)
    {
        delete_select_channel (debug->run_timer);
        close (debug->run_timer);
    }
    g_ptr_array_free (debug->launches, TRUE);
    g_ptr_array_free (debug->breakpoints, TRUE);
    g_ptr_array_free (debug->watches, TRUE);
    g_ptr_array_unref (debug->frames);
    g_free (debug->project_dir);
    g_free (debug->current_file);
    if (debug->build_signal != 0)
        debug->host->service_disconnect (debug->host, debug->build_signal);
    if (debug->service)
        debug->host->service_unregister (debug->host, DEBUG_SERVICE);
    for (int i = 0; i < DEBUG_MARK_COUNT; i++)
        g_free (debug->glyphs[i]);
    g_free (debug->current_func);
    g_array_free (debug->dropped_tokens, TRUE);
    g_ptr_array_unref (debug->locals);
    g_ptr_array_unref (debug->registers);
    if (debug->registers_before != NULL)
        g_hash_table_destroy (debug->registers_before);
    g_free (debug->current_address);
    g_ptr_array_unref (debug->instructions);
    g_ptr_array_free (debug->address_breakpoints, TRUE);
    g_free (debug->disasm_address);
    g_free (debug->source_file);
    g_strfreev (debug->source_lines);
    g_string_free (debug->console, TRUE);
    g_free (debug);
}

static gboolean
debug_ok_to_quit (void *data)
{
    debugger_t *debug = (debugger_t *) data;
    const gboolean live = debug_alive (debug)
        && (debug->state == DEBUG_RUNNING || debug->state == DEBUG_STOPPED
            || debug->state == DEBUG_STARTING);

    if (live)
        return query_dialog (_ ("Debug"), _ ("Stop the debug session and quit?"), D_NORMAL, 2,
                             _ ("&Stop"), _ ("&Cancel"))
            == 0;
    // debug mode, coole --debug: the project and its windows are not left by a slip of F10
    if (debug->debug_mode)
        return query_dialog (_ ("Debug"), _ ("Quit the editor?"), D_NORMAL, 2, _ ("&Yes"),
                             _ ("&No"))
            == 0;
    return TRUE;
}

static mc_ep_result_t
debug_file_open (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *file = debug->host->get_current_file (debug->host, edit);

    if (file == NULL)
        return MC_EPR_NOT_SUPPORTED;
    // the first file of a project: its breakpoints show at once
    if (debug->project_dir == NULL)
    {
        char *root = debug_project_of (debug, edit);

        if (root != NULL)
            (void) debug_project_switch (debug, root);
    }
    debug_breakpoints_sync (debug);
    debug_marks_show (debug, file);
    g_free (file);
    return MC_EPR_OK;
}

/* Debug mode on and off: the file windows take the keys of the debugger, F3 to F8 */
static mc_ep_result_t
debug_act_mode (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    debug->debug_mode = !debug->debug_mode;
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* Start, or go on when the program is stopped */
static mc_ep_result_t
debug_act_start (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->state == DEBUG_STOPPED)
        return debug_continue (data, edit);
    return debug_start (data, edit);
}

static const mc_ep_action_t debug_actions[] = {
    { "Open project", debug_open_project },
    { "Project status", debug_project_status },
    { "Configure", debug_configure },
    { "New configuration", debug_new_configuration },
    { "Select configuration", debug_select_configuration },
    { "Delete configuration", debug_delete_configuration },
    { "Start", debug_act_start },
    { "Toggle breakpoint", debug_toggle_breakpoint },
    { "Continue", debug_continue },
    { "Pause", debug_pause },
    { "Step over", debug_next },
    { "Step into", debug_step },
    { "Step out", debug_finish },
    { "Output", debug_show_output },
    { "Call stack", debug_show_stack },
    { "Add watch", debug_add_watch },
    { "Remove watch", debug_remove_watch },
    { "Watches", debug_show_watches },
    { "Send input", debug_send_input },
    { "Debug session", debug_session_show },
    { "Stop", debug_stop },
    { "GDB command", debug_act_gdb_command },
    { "Debug keys", debug_act_mode },
    { "Breakpoint on function", debug_act_function_breakpoint },
    { "Run to function", debug_act_run_to_function },
    { "Evaluate", debug_act_evaluate },
    { "Run to cursor", debug_act_run_to_cursor },
    { "Step into instruction", debug_step_instruction },
    { "Step over instruction", debug_next_instruction },
    { "Disassembly", debug_act_disassembly },
    { "Show stop", debug_act_show_stop },
    { "Breakpoint condition", debug_act_condition },
};

static const mc_ep_cmd_menu_entry_t debug_menu[] = {
    { MC_EP_MENU_FILE, N_ ("Open debug &project..."), DEBUG_ACT_OPEN_PROJECT, NULL },
    // what one does, first
    { DEBUG_MENU, N_ ("&Start or continue"), DEBUG_ACT_START, NULL },
    { DEBUG_MENU, N_ ("Toggle &breakpoint"), DEBUG_ACT_TOGGLE_BREAKPOINT, NULL },
    { DEBUG_MENU, N_ ("Breakpoint condition..."), DEBUG_ACT_CONDITION, NULL },
    { DEBUG_MENU, N_ ("Breakpoint on fun&ction..."), DEBUG_ACT_FUNCTION_BREAKPOINT, NULL },
    { DEBUG_MENU, N_ ("Panel of t&he debugger"), DEBUG_ACT_SESSION, NULL },
    { DEBUG_MENU, N_ ("Debug ke&ys in files"), DEBUG_ACT_MODE, NULL },
    { DEBUG_MENU, NULL, 0, NULL },
    { DEBUG_MENU, N_ ("Step o&ver"), DEBUG_ACT_NEXT, NULL },
    { DEBUG_MENU, N_ ("Step &into"), DEBUG_ACT_STEP, NULL },
    { DEBUG_MENU, N_ ("Step o&ut"), DEBUG_ACT_FINISH, NULL },
    { DEBUG_MENU, N_ ("Step into instruction"), DEBUG_ACT_STEP_INSTRUCTION, NULL },
    { DEBUG_MENU, N_ ("Step over instruction"), DEBUG_ACT_NEXT_INSTRUCTION, NULL },
    { DEBUG_MENU, N_ ("Run to cursor"), DEBUG_ACT_RUN_TO_CURSOR, NULL },
    { DEBUG_MENU, N_ ("Run t&o function..."), DEBUG_ACT_RUN_TO_FUNCTION, NULL },
    { DEBUG_MENU, N_ ("Show the stop line"), DEBUG_ACT_SHOW_STOP, NULL },
    { DEBUG_MENU, N_ ("&Pause"), DEBUG_ACT_PAUSE, NULL },
    { DEBUG_MENU, N_ ("S&top"), DEBUG_ACT_STOP, NULL },
    { DEBUG_MENU, NULL, 0, NULL },
    { DEBUG_MENU, N_ ("Co&nsole"), DEBUG_ACT_OUTPUT, NULL },
    { DEBUG_MENU, N_ ("Call stac&k..."), DEBUG_ACT_STACK, NULL },
    { DEBUG_MENU, N_ ("Disassembly"), DEBUG_ACT_DISASSEMBLY, NULL },
    { DEBUG_MENU, N_ ("Evaluate e&xpression..."), DEBUG_ACT_EVALUATE, NULL },
    { DEBUG_MENU, N_ ("&Add watch..."), DEBUG_ACT_ADD_WATCH, NULL },
    { DEBUG_MENU, N_ ("&Remove watch..."), DEBUG_ACT_REMOVE_WATCH, NULL },
    { DEBUG_MENU, N_ ("Send &line..."), DEBUG_ACT_INPUT, NULL },
    { DEBUG_MENU, N_ ("GDB co&mmand..."), DEBUG_ACT_GDB_COMMAND, NULL },
    { DEBUG_MENU, NULL, 0, NULL },
    // what one sets once
    { DEBUG_MENU, N_ ("Ne&w configuration..."), DEBUG_ACT_NEW_CONFIGURATION, NULL },
    { DEBUG_MENU, N_ ("Select con&figuration..."), DEBUG_ACT_SELECT_CONFIGURATION, NULL },
    { DEBUG_MENU, N_ ("Confi&gure selected..."), DEBUG_ACT_CONFIGURE, NULL },
    { DEBUG_MENU, N_ ("&Delete configuration..."), DEBUG_ACT_DELETE_CONFIGURATION, NULL },
    { DEBUG_MENU, N_ ("Open proj&ect..."), DEBUG_ACT_OPEN_PROJECT, NULL },
    { DEBUG_MENU, N_ ("Project status..."), DEBUG_ACT_PROJECT_STATUS, NULL },
    // the windows of the debugger, with those of the other plugins
};

/* The key of a menu entry: of its command, the one the editor has nothing on first, since that
   one works in a file window too */
static char *
debug_menu_shortcut (int action_index)
{
    static const struct
    {
        int action;
        int cmd;
    } keys[] = {
        { DEBUG_ACT_START, DEBUG_CMD_START_CONTINUE },
        { DEBUG_ACT_CONTINUE, DEBUG_CMD_START_CONTINUE },
        { DEBUG_ACT_TOGGLE_BREAKPOINT, DEBUG_CMD_TOGGLE_BREAKPOINT },
        { DEBUG_ACT_SESSION, DEBUG_CMD_PANEL },
        { DEBUG_ACT_EVALUATE, DEBUG_CMD_EVALUATE },
        { DEBUG_ACT_RUN_TO_CURSOR, DEBUG_CMD_RUN_TO_CURSOR },
        { DEBUG_ACT_NEXT, DEBUG_CMD_STEP_OVER },
        { DEBUG_ACT_STEP, DEBUG_CMD_STEP_INTO },
        { DEBUG_ACT_FINISH, DEBUG_CMD_STEP_OUT },
        { DEBUG_ACT_STEP_INSTRUCTION, DEBUG_CMD_STEP_INSTRUCTION },
        { DEBUG_ACT_NEXT_INSTRUCTION, DEBUG_CMD_NEXT_INSTRUCTION },
        { DEBUG_ACT_PAUSE, DEBUG_CMD_PAUSE },
        { DEBUG_ACT_STOP, DEBUG_CMD_STOP },
        { DEBUG_ACT_SHOW_STOP, DEBUG_CMD_SHOW_STOP },
        { DEBUG_ACT_CONDITION, DEBUG_CMD_CONDITION },
    };
    const global_keymap_t *map = keymap_section_map (DEBUG_KEYMAP_SECTION);
    const char *first = NULL;
    const char *found = NULL;
    long command;
    size_t i, k;

    for (k = 0; k < G_N_ELEMENTS (keys) && keys[k].action != action_index; k++)
        ;
    if (k == G_N_ELEMENTS (keys) || map == NULL)
        return NULL;
    command = keybind_lookup_action (debug_commands[keys[k].cmd].name);
    for (i = 0; map[i].key != 0; i++)
        if (map[i].command == command && map[i].caption[0] != '\0')
        {
            if (found == NULL
                && keybind_lookup_keymap_command (editor_map, map[i].key) == CK_IgnoreKey)
                found = map[i].caption;
            if (first == NULL)
                first = map[i].caption;
        }
    if (found == NULL)
        found = first;
    if (found == NULL)
        return NULL;
    // "Alt-G" of the keymap is Alt with Shift and g, F18 is Shift-F8: said so
    if (g_str_has_prefix (found, "Alt-") && g_ascii_isupper (found[4]) && found[5] == '\0')
        return g_strdup_printf ("Alt-Shift-%c", found[4]);
    if ((found[0] == 'F' || found[0] == 'f') && atoi (found + 1) > 10 && atoi (found + 1) <= 20)
        return g_strdup_printf ("Shift-F%d", atoi (found + 1) - 10);
    return g_strdup (found);
}

static const mc_editor_plugin_t debug_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "debugger",
    .display_name = "Debugger",
    .flags = MC_EPF_NONE,
    .open = debug_open,
    .close = debug_close,
    .on_file_open = debug_file_open,
    .ok_to_quit = debug_ok_to_quit,
    .handle_key = debug_handle_key,
    .handle_action = debug_handle_action,
    .handle_event = debug_handle_event,
    .actions = debug_actions,
    .action_count = G_N_ELEMENTS (debug_actions),
    .cmd_menu_entries = debug_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (debug_menu),
    .get_menu_shortcut = debug_menu_shortcut,
};

/* The entry of the module, which the editor calls when it loads it */
const mc_editor_plugin_t *
mc_editor_plugin_register (void)
{
    return &debug_plugin;
}
