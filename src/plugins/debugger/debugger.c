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

static mc_ep_result_t debug_start (void *data, void *edit);
static mc_ep_result_t debug_continue (void *data, void *edit);
static mc_ep_result_t debug_pause (void *data, void *edit);
static mc_ep_result_t debug_next (void *data, void *edit);
static mc_ep_result_t debug_step (void *data, void *edit);
static mc_ep_result_t debug_step_instruction (void *data, void *edit);
static mc_ep_result_t debug_next_instruction (void *data, void *edit);
static mc_ep_result_t debug_finish (void *data, void *edit);
static mc_ep_result_t debug_run_to_cursor (debugger_t *debug, void *edit);
static mc_ep_result_t debug_stop (void *data, void *edit);

/*** end of forward declarations */

/*** file scope functions *********************************************************************/

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

void
debug_error (debugger_t *debug, const char *message_text)
{
    debug->host->message (debug->host, D_ERROR, _ ("Debug"), message_text);
}

/* GDB is gone or starts again: it owes nothing */
void
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

gboolean
debug_session_live (const debugger_t *debug)
{
    return debug->state == DEBUG_STARTING || debug->state == DEBUG_RUNNING
        || debug->state == DEBUG_STOPPED;
}

void
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
gboolean
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

/* Ask the build plugin something about the project, NULL without that plugin */
GVariant *
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

/* --------------------------------------------------------------------------------------------- */
/* The functions of the project, from the plugin ctags */
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

/* Debug > Show the stop line: the source at the line the program is stopped at, the cursor
   having gone elsewhere */
static mc_ep_result_t
debug_act_show_stop (void *data, void *edit)
{
    return debug_run_command ((debugger_t *) data, DEBUG_CMD_SHOW_STOP, edit) ? MC_EPR_OK
                                                                              : MC_EPR_FAILED;
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
