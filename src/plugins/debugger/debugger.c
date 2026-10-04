#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>
#ifdef ENABLE_MCTERM
#include <sys/ioctl.h>
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
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"

#include "src/keymap.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"

#include "debugger.h"
#include "gdb-mi.h"

typedef enum
{
    DEBUG_OFF,
    DEBUG_STARTING,
    DEBUG_RUNNING,
    DEBUG_STOPPED,
    DEBUG_FINISHED
} debug_state_t;

typedef struct
{
    char *file;
    long line;
    char *gdb_number;
    unsigned int pending_token;
} debug_breakpoint_t;

typedef struct
{
    char *name;
    char *executable;
    char *arguments;
    char *directory;
    char *environment;
    char *gdb_path;
} debug_launch_t;

typedef struct
{
    char *expression;
    char *value;
    unsigned int pending_token;
} debug_watch_t;

typedef struct
{
    char *file;
    long line;
    long level;
    char *label;
} debug_frame_t;

typedef struct
{
    char *name;
    char *value;
} debug_local_t;

typedef struct debug_session_window_t debug_session_window_t;

/* the commands of the debugger, in the [debugger] section of the keymap */
enum
{
    DEBUG_CMD_NONE = -1,
    DEBUG_CMD_HELP,
    DEBUG_CMD_START_CONTINUE,
    DEBUG_CMD_PAUSE,
    DEBUG_CMD_STEP_INTO,
    DEBUG_CMD_STEP_OVER,
    DEBUG_CMD_STEP_OUT,
    DEBUG_CMD_STOP,
    DEBUG_CMD_TOGGLE_BREAKPOINT,
    DEBUG_CMD_RUN_TO_CURSOR,
    DEBUG_CMD_EVALUATE,
    DEBUG_CMD_LEAVE,
    DEBUG_CMD_CLOSE,
    DEBUG_CMD_COUNT
};

#define DEBUG_KEYMAP_SECTION "debugger"
#define DEBUG_MENU           N_ ("&Debug")

static const mc_ep_command_t debug_commands[DEBUG_CMD_COUNT + 1] = {
    { "Help", NULL, "f1" },
    { "DebugStartContinue", N_ ("Start or continue debugging"), "f5" },
    { "DebugPause", N_ ("Pause debugging"), "f6" },
    { "DebugStepInto", N_ ("Step into"), "f7" },
    { "DebugStepOver", N_ ("Step over"), "f8" },
    { "DebugStepOut", N_ ("Step out"), "f9" },
    { "DebugStop", N_ ("Stop debugging"), "f15" },
    { "DebugToggleBreakpoint", N_ ("Toggle breakpoint"), "f2" },
    { "DebugRunToCursor", N_ ("Run to cursor"), "f4" },
    { "DebugEvaluate", N_ ("Evaluate expression"), "enter" },
    { "DebugLeave", N_ ("Leave step mode"), "esc" },
    { "DebugClose", N_ ("Close debug session window"), "f10" },
    { NULL, NULL, NULL },
};

/* the marks of the debugger in the gutter */
enum
{
    DEBUG_MARK_BREAKPOINT,
    DEBUG_MARK_PENDING,
    DEBUG_MARK_DISABLED,
    DEBUG_MARK_EXEC,
    DEBUG_MARK_EXEC_BREAKPOINT,
    DEBUG_MARK_COUNT
};

static const mc_ep_marker_kind_t debug_mark_kinds[DEBUG_MARK_COUNT] = {
    { "debugger.breakpoint", "breakpoint-char", "\u25cf", "o", "breakpoint", "breakpointline", NULL,
      10 },
    // GDB has not taken it yet, or has refused it
    { "debugger.breakpoint-pending", "breakpoint-pending-char", "\u25cc", "?", "breakpointpending",
      "breakpointline", NULL, 12 },
    { "debugger.breakpoint-disabled", "breakpoint-disabled-char", "\u25cb", "-",
      "breakpointdisabled", NULL, NULL, 11 },
    { "debugger.exec", "exec-char", "\u25b6", ">", "execmark", "execline", "bookmarkfound", 20 },
    { "debugger.exec-breakpoint", "exec-breakpoint-char", "\u25c9", "@", "execmark", "execline",
      "bookmarkfound", 21 },
};

typedef struct
{
    mc_editor_host_t *host;
    gdb_mi_session_t *gdb;
    debug_state_t state;
    char *project_dir;
    GPtrArray *launches;
    guint active_launch;
    GPtrArray *breakpoints;
    GPtrArray *watches;
    GString *output;
    // what GDB says of itself, apart from the output of the program
    GString *console;
    gint64 console_window;
    gint64 output_window;
    GString *stack_text;
    gint64 stack_window;
    GPtrArray *frames;
    GString *variables_text;
    gint64 variables_window;
    GString *watches_text;
    gint64 watches_window;
    int pty_master;
    int pty_slave;
    char *pty_name;
    char *current_file;
    long current_line;
    unsigned int next_token;
    GQueue *startup_commands;
    // the breakpoints went to GDB at the start: a new one goes by itself
    gboolean breakpoints_installed;
    // the replies GDB owes: debug_request_t
    GPtrArray *requests;
    gboolean eval_pending;
    // the local variables of the frame: debug_local_t
    GPtrArray *locals;
    char *current_func;
    /* the user went back to editing with the program stopped; the next stop steps again */
    gboolean step_left;
    int marks[DEBUG_MARK_COUNT];
    long commands[DEBUG_CMD_COUNT];
    debug_session_window_t *session_window;
} debugger_t;

struct debug_session_window_t
{
    WEditWindow window;
    debugger_t *debug;
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
    DEBUG_ACT_STOP
};

static mc_ep_result_t debug_start (void *data, void *edit);
static mc_ep_result_t debug_continue (void *data, void *edit);
static mc_ep_result_t debug_pause (void *data, void *edit);
static mc_ep_result_t debug_step (void *data, void *edit);
static mc_ep_result_t debug_next (void *data, void *edit);
static mc_ep_result_t debug_finish (void *data, void *edit);
static mc_ep_result_t debug_stop (void *data, void *edit);
static mc_ep_result_t debug_toggle_breakpoint (void *data, void *edit);
static mc_ep_result_t debug_run_to_cursor (debugger_t *debug, void *edit);
static mc_ep_result_t debug_evaluate (debugger_t *debug, void *edit);
static mc_ep_result_t debug_watch_add (debugger_t *debug, const char *expression);
static void debug_breakpoints_sync (debugger_t *debug);
static void debug_breakpoints_dedup (debugger_t *debug);
static gboolean debug_session_live (const debugger_t *debug);
static void debug_reply_watch (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data);
static void debug_setup_next (debugger_t *debug);
static void debug_marks_show (debugger_t *debug, const char *file);
static void debug_session_refresh (debugger_t *debug);

static void
debug_breakpoint_free (gpointer data)
{
    debug_breakpoint_t *bp = (debug_breakpoint_t *) data;

    g_free (bp->file);
    g_free (bp->gdb_number);
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

static debug_launch_t *
debug_active_launch (const debugger_t *debug)
{
    return debug->active_launch < debug->launches->len
        ? g_ptr_array_index (debug->launches, debug->active_launch)
        : NULL;
}

static void
debug_frame_free (gpointer data)
{
    debug_frame_t *frame = (debug_frame_t *) data;

    g_free (frame->file);
    g_free (frame->label);
    g_free (frame);
}

static const char *
debug_state_name (debug_state_t state)
{
    switch (state)
    {
    case DEBUG_STARTING:
        return _ ("Starting");
    case DEBUG_RUNNING:
        return _ ("Running");
    case DEBUG_STOPPED:
        return _ ("Stopped");
    case DEBUG_FINISHED:
        return _ ("Finished");
    case DEBUG_OFF:
    default:
        return _ ("Not started");
    }
}

/* The command of the debugger of that number, DEBUG_CMD_NONE for any other */
static int
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
static int
debug_command_of_key (const debugger_t *debug, int key)
{
    return debug_command (debug,
                          debug->host->command_lookup (debug->host, DEBUG_KEYMAP_SECTION, key));
}

static const char *
debug_session_button_label (int cmd, debug_state_t state)
{
    switch (cmd)
    {
    case DEBUG_CMD_HELP:
        return _ ("Help");
    case DEBUG_CMD_START_CONTINUE:
        return state == DEBUG_STOPPED                       ? _ ("Continue")
            : state == DEBUG_OFF || state == DEBUG_FINISHED ? _ ("Start")
                                                            : NULL;
    case DEBUG_CMD_PAUSE:
        return state == DEBUG_RUNNING ? _ ("Pause") : NULL;
    case DEBUG_CMD_STEP_INTO:
        return state == DEBUG_STOPPED ? _ ("Into") : NULL;
    case DEBUG_CMD_STEP_OVER:
        return state == DEBUG_STOPPED ? _ ("Over") : NULL;
    case DEBUG_CMD_STEP_OUT:
        return state == DEBUG_STOPPED ? _ ("Out") : NULL;
    case DEBUG_CMD_STOP:
        return state == DEBUG_STARTING || state == DEBUG_RUNNING || state == DEBUG_STOPPED
            ? _ ("Stop")
            : NULL;
    case DEBUG_CMD_TOGGLE_BREAKPOINT:
        return _ ("Break");
    case DEBUG_CMD_RUN_TO_CURSOR:
        return state == DEBUG_STOPPED ? _ ("ToCurs") : NULL;
    case DEBUG_CMD_CLOSE:
        return _ ("Close");
    default:
        return NULL;
    }
}

static void
debug_session_buttonbar (debug_session_window_t *session)
{
    WButtonBar *bb = buttonbar_find (DIALOG (WIDGET (session)->owner));
    const debugger_t *debug = session->debug;
    Widget *w = WIDGET (session);
    int i;

    if (bb == NULL || debug == NULL)
        return;
    for (i = 1; i <= 10; i++)
    {
        const int cmd = debug_command_of_key (debug, KEY_F (i));
        const char *label = debug_session_button_label (cmd, debug->state);

        if (label == NULL)
            buttonbar_clear_label (bb, i, NULL);
        else
            buttonbar_set_label_command (bb, i, label, debug->commands[cmd],
                                         cmd == DEBUG_CMD_HELP ? NULL : w);
    }
    widget_draw (WIDGET (bb));
}

/* Step mode: while the program is stopped, or running after a step, a file window takes the
   debugger keys; what moves around the text is the editor's, what would change it is held back.
   Esc goes back to editing until the next stop. */
static gboolean
debug_stepping (const debugger_t *debug)
{
    return !debug->step_left && (debug->state == DEBUG_STOPPED || debug->state == DEBUG_RUNNING);
}

/* The editor commands step mode leaves to the editor: they move, look, select or switch
   windows, and none of them changes the text. */
static gboolean
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

/* The button bar of a file window: the editor's own, with the debugger's buttons over it in step
   mode */
static void
debug_editor_buttonbar (debugger_t *debug, Widget *edit)
{
    WButtonBar *bb = buttonbar_find (DIALOG (edit->owner));
    int i;

    if (bb == NULL)
        return;
    edit_set_buttonbar (EDIT (edit), bb);
    if (debug_stepping (debug))
        for (i = 1; i <= 10; i++)
        {
            const int cmd = debug_command_of_key (debug, KEY_F (i));
            const gboolean editors =
                cmd == DEBUG_CMD_NONE || cmd == DEBUG_CMD_HELP || cmd == DEBUG_CMD_CLOSE;

            // the bar sends the command to the editor, which gives it to handle_action()
            if (!editors && debug_session_button_label (cmd, debug->state) != NULL)
                buttonbar_set_label_command (bb, i, debug_session_button_label (cmd, debug->state),
                                             debug->commands[cmd], NULL);
            else if (!editors
                     || !debug_step_passes (
                         keybind_lookup_keymap_command (edit->keymap, KEY_F (i))))
                buttonbar_clear_label (bb, i, NULL);
        }
    widget_draw (WIDGET (bb));
}

static void
debug_session_draw (debug_session_window_t *session)
{
    WEditWindow *win = &session->window;
    Widget *w = WIDGET (session);
    debugger_t *debug = session->debug;
    const int color = edit_window_frame_color (win, widget_get_state (w, WST_FOCUSED));
    const debug_launch_t *launch = debug != NULL ? debug_active_launch (debug) : NULL;
    GString *body = g_string_new (NULL);
    char **lines;
    gsize line_count;
    int row;
    guint i;

    edit_window_draw_frame (win, color, widget_get_state (w, WST_FOCUSED));
    tty_setcolor (color);
    widget_gotoyx (w, 0, 2);
    tty_print_string (str_term_trim (_ ("[Debug session]"), MAX (0, w->rect.cols - 10)));
    edit_window_draw_icons (win, color);

    if (debug == NULL)
    {
        widget_gotoyx (w, 1, 1);
        tty_print_string (_ ("Debug session ended."));
        g_string_free (body, TRUE);
        return;
    }

    g_string_append_printf (body, _ ("Project: %s\n"),
                            debug->project_dir != NULL ? debug->project_dir : _ ("<none>"));
    g_string_append_printf (body, _ ("Configuration: %s\n"),
                            launch != NULL ? launch->name : _ ("<none>"));
    g_string_append_printf (body, _ ("State: %s\n"), debug_state_name (debug->state));
    if (debug->current_file != NULL)
        g_string_append_printf (body, _ ("At: %s:%ld\n"), debug->current_file, debug->current_line);
    else if (debug->state == DEBUG_STOPPED && debug->current_func != NULL)
        g_string_append_printf (body, _ ("In: %s, with no source\n"), debug->current_func);
    else
        g_string_append_c (body, '\n');
    if (debug_stepping (debug))
        g_string_append (body,
                         _ ("Step mode: the source window takes the debugger keys; "
                            "Esc goes back to editing.\n"));
    if (debug->watches->len > 0)
    {
        g_string_append (body, _ ("Watches:\n"));
        for (i = 0; i < debug->watches->len; i++)
        {
            const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

            g_string_append_printf (body, "  %s = %s\n", watch->expression,
                                    watch->value != NULL ? watch->value : _ ("<not evaluated>"));
        }
    }
    lines = g_strsplit (body->str, "\n", -1);
    line_count = g_strv_length (lines);
    for (row = 1; row < w->rect.lines - 1; row++)
    {
        tty_setcolor (EDITOR_NORMAL_COLOR);
        tty_draw_hline (w->rect.y + row, w->rect.x + 1, ' ', MAX (0, w->rect.cols - 2));
        if ((gsize) (row - 1) < line_count)
        {
            widget_gotoyx (w, row, 1);
            tty_print_string (str_term_trim (lines[row - 1], MAX (0, w->rect.cols - 2)));
        }
    }
    g_strfreev (lines);
    g_string_free (body, TRUE);
}

static gboolean
debug_session_close_window (WEditWindow *win)
{
    WDialog *dialog = DIALOG (WIDGET (win)->owner);

    edit_window_give_room_back (win);
    edit_window_destroy (win);
    widget_draw (WIDGET (dialog));
    return TRUE;
}

/* Run a command of the debugger; one that works on a line takes that of @edit, or of the topmost
   file window when @edit is NULL */
static gboolean
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
    default:
        return FALSE;
    }
}

static cb_ret_t
debug_session_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    debug_session_window_t *session = (debug_session_window_t *) w;

    switch (msg)
    {
    case MSG_DRAW:
        debug_session_draw (session);
        return MSG_HANDLED;
    case MSG_FOCUS:
        debug_session_buttonbar (session);
        debug_session_draw (session);
        return MSG_HANDLED;
    case MSG_UNFOCUS:
        debug_session_draw (session);
        return MSG_HANDLED;
    case MSG_KEY:
        if (session->debug == NULL)
            return MSG_NOT_HANDLED;
        if (debug_run_command (session->debug, debug_command_of_key (session->debug, parm), NULL))
            return MSG_HANDLED;
        // the F keys of the editor mean nothing here
        if (debug_command_of_key (session->debug, parm) == DEBUG_CMD_NONE && parm >= KEY_F (1)
            && parm <= KEY_F (10))
            return MSG_HANDLED;
        return MSG_NOT_HANDLED;
    case MSG_ACTION:
        if (session->debug != NULL
            && debug_run_command (session->debug, debug_command (session->debug, parm), NULL))
            return MSG_HANDLED;
        return MSG_NOT_HANDLED;
    case MSG_CURSOR:
        widget_gotoyx (w, 1, 1);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (session->debug != NULL)
            session->debug->session_window = NULL;
        return group_default_callback (w, sender, msg, parm, data);
    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

static char *
debug_session_title (const WEditWindow *win)
{
    (void) win;
    return g_strdup (_ ("Debug session"));
}

static const edit_window_class_t debug_session_class = {
    .callback = debug_session_callback,
    .get_title = debug_session_title,
    .close = debug_session_close_window,
    .min_lines = 6,
    .min_cols = 32,
};

static void
debug_session_refresh (debugger_t *debug)
{
    Widget *current = WIDGET (debug->host->window_current (debug->host));

    if (debug->session_window != NULL)
        widget_draw (WIDGET (debug->session_window));
    if (current == NULL)
        return;
    if (current == WIDGET (debug->session_window))
        debug_session_buttonbar (debug->session_window);
    else if (edit_widget_is_editor (current))
        debug_editor_buttonbar (debug, current);
}

static mc_ep_result_t
debug_session_show (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_session_window_t *session;
    WRect area, rect;

    (void) edit;
    if (debug->session_window != NULL)
    {
        debug->host->window_show (debug->host, debug->session_window);
        return MC_EPR_OK;
    }
    debug->host->window_area (debug->host, &area);
    rect = area;
    rect.lines = MIN (area.lines, MAX (8, area.lines * 30 / 100));
    rect.y = area.y + area.lines - rect.lines;
    session = g_new0 (debug_session_window_t, 1);
    edit_window_init (&session->window, &rect, &debug_session_class);
    session->window.fullscreen = 0;
    session->debug = debug;
    debug->session_window = session;
    debug->host->window_add (debug->host, session);
    debug->host->window_make_room (debug->host, session);
    debug_session_refresh (debug);
    return MC_EPR_OK;
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

static void
debug_config_save (debugger_t *debug)
{
    GKeyFile *keyfile;
    char *path, *directory, *contents;
    gsize length;
    char **locations, **expressions;
    guint i;
    GError *error = NULL;

    path = debug_config_path (debug);
    if (path == NULL)
        return;
    debug_breakpoints_sync (debug);
    keyfile = g_key_file_new ();
    g_key_file_set_string (keyfile, "Debug", "project", debug->project_dir);
    g_key_file_set_integer (keyfile, "Debug", "launch_count", (gint) debug->launches->len);
    g_key_file_set_integer (keyfile, "Debug", "active_launch", (gint) debug->active_launch);
    for (i = 0; i < debug->launches->len; i++)
    {
        const debug_launch_t *launch = g_ptr_array_index (debug->launches, i);
        char *group = g_strdup_printf ("Launch %u", i);

        g_key_file_set_string (keyfile, group, "name", launch->name);
        g_key_file_set_string (keyfile, group, "executable", launch->executable);
        g_key_file_set_string (keyfile, group, "arguments",
                               launch->arguments != NULL ? launch->arguments : "");
        g_key_file_set_string (keyfile, group, "directory",
                               launch->directory != NULL ? launch->directory : "");
        g_key_file_set_string (keyfile, group, "environment",
                               launch->environment != NULL ? launch->environment : "");
        g_key_file_set_string (keyfile, group, "gdb_path",
                               launch->gdb_path != NULL ? launch->gdb_path : "gdb");
        g_free (group);
    }
    locations = g_new0 (char *, debug->breakpoints->len + 1);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        locations[i] = g_strdup_printf ("%s:%ld", bp->file, bp->line);
    }
    g_key_file_set_string_list (keyfile, "Debug", "breakpoints", (const gchar *const *) locations,
                                debug->breakpoints->len);
    expressions = g_new0 (char *, debug->watches->len + 1);
    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        expressions[i] = g_strdup (watch->expression);
    }
    g_key_file_set_string_list (keyfile, "Debug", "watches", (const gchar *const *) expressions,
                                debug->watches->len);
    contents = g_key_file_to_data (keyfile, &length, NULL);
    directory = g_path_get_dirname (path);
    if (g_mkdir_with_parents (directory, 0700) != 0
        || !g_file_set_contents (path, contents, length, &error) || g_chmod (path, 0600) != 0)
        debug->host->message (debug->host, D_ERROR, _ ("Debug"),
                              error != NULL ? error->message
                                            : _ ("Could not save the debug project settings."));
    g_clear_error (&error);
    g_strfreev (locations);
    g_strfreev (expressions);
    g_free (directory);
    g_free (contents);
    g_key_file_free (keyfile);
    g_free (path);
}

static void
debug_config_load (debugger_t *debug)
{
    GKeyFile *keyfile;
    char *path;
    char **locations, **expressions;
    gsize count, i;
    gint launch_count;

    path = debug_config_path (debug);
    if (path == NULL)
        return;
    keyfile = g_key_file_new ();
    if (!g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
        goto out;
    launch_count = g_key_file_has_key (keyfile, "Debug", "launch_count", NULL)
        ? g_key_file_get_integer (keyfile, "Debug", "launch_count", NULL)
        : -1;
    if (launch_count >= 0 && launch_count <= 1000)
    {
        for (i = 0; i < (gsize) launch_count; i++)
        {
            debug_launch_t *launch = g_new0 (debug_launch_t, 1);
            char *group = g_strdup_printf ("Launch %u", (guint) i);

            launch->name = g_key_file_get_string (keyfile, group, "name", NULL);
            launch->executable = g_key_file_get_string (keyfile, group, "executable", NULL);
            launch->arguments = g_key_file_get_string (keyfile, group, "arguments", NULL);
            launch->directory = g_key_file_get_string (keyfile, group, "directory", NULL);
            launch->environment = g_key_file_get_string (keyfile, group, "environment", NULL);
            launch->gdb_path = g_key_file_get_string (keyfile, group, "gdb_path", NULL);
            if (launch->directory == NULL || *launch->directory == '\0')
            {
                g_free (launch->directory);
                launch->directory = g_strdup (debug->project_dir);
            }
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
    else
    {
        debug_launch_t *legacy = g_new0 (debug_launch_t, 1);

        legacy->name = g_strdup (_ ("Default"));
        legacy->executable = g_key_file_get_string (keyfile, "Debug", "executable", NULL);
        legacy->arguments = g_key_file_get_string (keyfile, "Debug", "arguments", NULL);
        legacy->directory = g_key_file_get_string (keyfile, "Debug", "directory", NULL);
        if (legacy->executable != NULL)
            g_ptr_array_add (debug->launches, legacy);
        else
            debug_launch_free (legacy);
    }
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

        if (separator == NULL)
            continue;
        line = g_ascii_strtoll (separator + 1, &end, 10);
        if (end == separator + 1 || *end != '\0' || line <= 0 || line > G_MAXLONG)
            continue;
        {
            debug_breakpoint_t *bp = g_new0 (debug_breakpoint_t, 1);

            bp->file = g_strndup (locations[i], separator - locations[i]);
            bp->line = (long) line;
            g_ptr_array_add (debug->breakpoints, bp);
        }
    }
    g_strfreev (locations);
out:
    g_key_file_free (keyfile);
    g_free (path);
}

static void
debug_error (debugger_t *debug, const char *message_text)
{
    debug->host->message (debug->host, D_ERROR, _ ("Debug"), message_text);
}

/* What to do with the reply of GDB to a command */
typedef void (*debug_reply_fn) (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data);

typedef struct
{
    unsigned int token;
    debug_reply_fn reply;
    gpointer data;
    GDestroyNotify free_data;
} debug_request_t;

static void
debug_request_free (gpointer p)
{
    debug_request_t *request = (debug_request_t *) p;

    if (request->free_data != NULL)
        request->free_data (request->data);
    g_free (request);
}

static void
debug_local_free (gpointer p)
{
    debug_local_t *local = (debug_local_t *) p;

    g_free (local->name);
    g_free (local->value);
    g_free (local);
}

/* Send a command to GDB, its reply to go to @reply (NULL: only an error is told).  Gives the
   token of the command, 0 when it could not be sent; @data is freed either way. */
static unsigned int
debug_request (debugger_t *debug, debug_reply_fn reply, gpointer data, GDestroyNotify free_data,
               const char *command)
{
    debug_request_t *request;
    char *line;
    gboolean sent;

    request = g_new0 (debug_request_t, 1);
    request->token = ++debug->next_token;
    request->reply = reply;
    request->data = data;
    request->free_data = free_data;
    line = g_strdup_printf ("%u%s", request->token, command);
    sent = debug->gdb != NULL && gdb_mi_session_send (debug->gdb, line);
    g_free (line);
    if (!sent)
    {
        debug_request_free (request);
        debug_error (debug, _ ("Could not send a command to GDB."));
        return 0;
    }
    g_ptr_array_add (debug->requests, request);
    return request->token;
}

/* GDB is gone or starts again: it owes nothing */
static void
debug_requests_clear (debugger_t *debug)
{
    g_ptr_array_set_size (debug->requests, 0);
    debug->eval_pending = FALSE;
}

/* The reply to a request: taken off the list, then handled.  FALSE when nothing asked for it. */
static gboolean
debug_request_reply (debugger_t *debug, const gdb_mi_record_t *reply)
{
    guint i;

    if (!reply->has_token)
        return FALSE;
    for (i = 0; i < debug->requests->len; i++)
    {
        debug_request_t *request = g_ptr_array_index (debug->requests, i);

        if (request->token == reply->token)
        {
            g_ptr_array_steal_index (debug->requests, i);
            if (request->reply != NULL)
                request->reply (debug, reply, request->data);
            else if (g_strcmp0 (reply->klass, "error") == 0)
                debug_error (debug,
                             gdb_mi_record_string (reply, "msg") != NULL
                                 ? gdb_mi_record_string (reply, "msg")
                                 : _ ("GDB refused a command."));
            debug_request_free (request);
            return TRUE;
        }
    }
    return FALSE;
}

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
        g_variant_dict_insert (&dict, "place", "s", "right");
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

static void
debug_output_show (debugger_t *debug)
{
    debug_text_show (debug, _ ("Debug output"), debug->output->str, &debug->output_window);
}

static void
debug_watches_show (debugger_t *debug)
{
    guint i;

    g_string_truncate (debug->watches_text, 0);
    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        g_string_append_printf (debug->watches_text, "%s = %s\n", watch->expression,
                                watch->value != NULL ? watch->value : _ ("<not evaluated>"));
    }
    debug_text_show (debug, _ ("Watches"), debug->watches_text->str, &debug->watches_window);
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
        char *quoted = gdb_mi_quote (watch->expression);
        char *command = g_strconcat ("-data-evaluate-expression ", quoted, NULL);

        g_free (watch->value);
        watch->value = g_strdup (_ ("<evaluating>"));
        watch->pending_token = debug_request (debug, debug_reply_watch, NULL, NULL, command);
        if (watch->pending_token == 0)
        {
            g_free (watch->value);
            watch->value = g_strdup (_ ("<GDB unavailable>"));
        }
        g_free (command);
        g_free (quoted);
    }
    if (debug->watches->len > 0 || debug->watches_window != 0)
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
    if (debug->watches_window != 0)
        debug_watches_show (debug);
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

static void
debug_output_append (debugger_t *debug, const char *text_value)
{
    char *valid = g_utf8_make_valid (text_value, -1);

    g_string_append (debug->output, valid);
    g_free (valid);
    if (debug->output->len > 100000)
        g_string_erase (debug->output, 0, debug->output->len - 100000);
    debug_output_show (debug);
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

static gboolean
debug_session_live (const debugger_t *debug)
{
    return debug->state == DEBUG_STARTING || debug->state == DEBUG_RUNNING
        || debug->state == DEBUG_STOPPED;
}

static int
debug_breakpoint_mark (const debugger_t *debug, const debug_breakpoint_t *bp)
{
    if (debug_session_live (debug) && bp->gdb_number == NULL)
        return DEBUG_MARK_PENDING;
    return DEBUG_MARK_BREAKPOINT;
}

static gboolean
debug_is_breakpoint_mark (int mark)
{
    return mark == DEBUG_MARK_BREAKPOINT || mark == DEBUG_MARK_PENDING
        || mark == DEBUG_MARK_DISABLED;
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
static void
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

/* What GDB says of itself: kept for the console, @line ends a line of its own */
static void
debug_output_console (debugger_t *debug, const char *text_value, gboolean line)
{
    char *valid = g_utf8_make_valid (text_value, -1);

    g_string_append (debug->console, valid);
    g_free (valid);
    if (line && debug->console->len > 0 && debug->console->str[debug->console->len - 1] != '\n')
        g_string_append_c (debug->console, '\n');
    if (debug->console->len > 100000)
        g_string_erase (debug->console, 0, debug->console->len - 100000);
    if (debug->console_window != 0)
        debug_text_show (debug, _ ("GDB console"), debug->console->str, &debug->console_window);
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

/* The call stack, from the reply to -stack-list-frames */
static void
debug_reply_stack (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data)
{
    const gdb_mi_value_t *stack = gdb_mi_get (reply->results, "stack");
    guint i;

    (void) data;
    g_string_truncate (debug->stack_text, 0);
    g_ptr_array_set_size (debug->frames, 0);
    for (i = 0; stack != NULL && stack->items != NULL && i < stack->items->len; i++)
    {
        const gdb_mi_value_t *frame = g_ptr_array_index (stack->items, i);
        const char *level = gdb_mi_get_string (frame, "level");
        const char *func = gdb_mi_get_string (frame, "func");
        const char *file = gdb_mi_get_string (frame, "fullname");
        const char *line = gdb_mi_get_string (frame, "line");
        const char *from = gdb_mi_get_string (frame, "from");
        debug_frame_t *entry;

        entry = g_new0 (debug_frame_t, 1);
        entry->level = level != NULL ? atol (level) : -1;
        entry->line = line != NULL ? atol (line) : 0;
        entry->file = g_strdup (file);
        if (file != NULL)
            entry->label =
                g_strdup_printf ("#%s %s  %s:%s", level != NULL ? level : "?",
                                 func != NULL ? func : "?", file, line != NULL ? line : "?");
        else
            entry->label = g_strdup_printf ("#%s %s  %s", level != NULL ? level : "?",
                                            func != NULL ? func : "?",
                                            from != NULL ? from : _ ("<no source>"));
        g_string_append_printf (debug->stack_text, "%s\n", entry->label);
        g_ptr_array_add (debug->frames, entry);
    }
    debug_text_show (debug, _ ("Call stack"), debug->stack_text->str, &debug->stack_window);
}

/* The local variables of the frame, from the reply to -stack-list-variables */
static void
debug_reply_variables (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data)
{
    const gdb_mi_value_t *variables = gdb_mi_get (reply->results, "variables");
    guint i;

    (void) data;
    g_string_truncate (debug->variables_text, 0);
    g_ptr_array_set_size (debug->locals, 0);
    for (i = 0; variables != NULL && variables->items != NULL && i < variables->items->len; i++)
    {
        const gdb_mi_value_t *variable = g_ptr_array_index (variables->items, i);
        const char *name = gdb_mi_get_string (variable, "name");
        const char *value = gdb_mi_get_string (variable, "value");
        const char *type = gdb_mi_get_string (variable, "type");
        debug_local_t *local;

        if (name == NULL)
            continue;
        local = g_new0 (debug_local_t, 1);
        local->name = g_strdup (name);
        // --simple-values gives no value of a struct or an array: its type stands for it
        local->value = value != NULL ? g_strdup (value)
            : type != NULL           ? g_strdup_printf ("{%s}", type)
                                     : g_strdup (_ ("<unavailable>"));
        g_string_append_printf (debug->variables_text, "%s = %s\n", local->name, local->value);
        g_ptr_array_add (debug->locals, local);
    }
    debug_text_show (debug, _ ("Local variables"), debug->variables_text->str,
                     &debug->variables_window);
}

/* The value of a watch */
static void
debug_reply_watch (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data)
{
    const gboolean failed = g_strcmp0 (reply->klass, "error") == 0;
    guint i;

    (void) data;
    for (i = 0; i < debug->watches->len; i++)
    {
        debug_watch_t *watch = g_ptr_array_index (debug->watches, i);
        const char *value;

        if (watch->pending_token != reply->token)
            continue;
        watch->pending_token = 0;
        value = gdb_mi_record_string (reply, failed ? "msg" : "value");
        g_free (watch->value);
        watch->value = g_strdup (value != NULL ? value : _ ("<unavailable>"));
        debug_watches_show (debug);
        debug_session_refresh (debug);
        return;
    }
}

static void
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

static gboolean
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

/* The value of an expression asked for with Enter */
static void
debug_reply_evaluate (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data)
{
    const char *expression = (const char *) data;
    const gboolean failed = g_strcmp0 (reply->klass, "error") == 0;
    const char *value = gdb_mi_record_string (reply, failed ? "msg" : "value");
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

/* GDB has taken a breakpoint, or has refused it; the reply to -break-insert */
static void
debug_reply_breakpoint (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data)
{
    const gdb_mi_value_t *bkpt = gdb_mi_get (reply->results, "bkpt");
    guint i;

    (void) data;
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);
        const char *line;

        if (bp->pending_token != reply->token)
            continue;
        bp->pending_token = 0;
        if (g_strcmp0 (reply->klass, "done") != 0 || bkpt == NULL)
        {
            // the mark stays the one of a breakpoint GDB has not taken
            const char *msg = gdb_mi_record_string (reply, "msg");

            if (msg != NULL)
                debug_output_console (debug, msg, TRUE);
            debug_marks_show (debug, NULL);
            return;
        }
        g_free (bp->gdb_number);
        bp->gdb_number = g_strdup (gdb_mi_get_string (bkpt, "number"));
        // GDB stops on the next line with code: the breakpoint goes there
        line = gdb_mi_get_string (bkpt, "line");
        if (line != NULL && atol (line) > 0 && atol (line) != bp->line)
        {
            debug_breakpoints_sync (debug);
            bp->line = atol (line);
            debug_breakpoints_dedup (debug);
            debug_config_save (debug);
        }
        debug_marks_show (debug, NULL);
        return;
    }
}

/* A command of the start is done: the next one goes */
static void
debug_reply_startup (debugger_t *debug, const gdb_mi_record_t *reply, gpointer data)
{
    const char *msg;

    (void) data;
    if (g_strcmp0 (reply->klass, "done") == 0)
    {
        debug_setup_next (debug);
        return;
    }
    msg = gdb_mi_record_string (reply, "msg");
    g_queue_clear_full (debug->startup_commands, g_free);
    debug->state = DEBUG_FINISHED;
    debug_error (debug, msg != NULL ? msg : _ ("GDB rejected a startup command."));
    debug_session_refresh (debug);
}

/* The program has ended, or GDB has */
static void
debug_finished (debugger_t *debug)
{
    if (debug->state != DEBUG_OFF)
        debug->state = DEBUG_FINISHED;
    g_queue_clear_full (debug->startup_commands, g_free);
    debug->breakpoints_installed = FALSE;
    debug_clear_current (debug);
    debug_marks_show (debug, NULL);
    debug_watches_clear_values (debug);
    debug_session_refresh (debug);
}

/* The program has stopped: at a breakpoint, after a step, on a signal, or for good */
static void
debug_stopped (debugger_t *debug, const gdb_mi_record_t *record)
{
    const char *reason = gdb_mi_record_string (record, "reason");
    const gdb_mi_value_t *frame = gdb_mi_get (record->results, "frame");
    const char *file = gdb_mi_get_string (frame, "fullname");
    const char *line = gdb_mi_get_string (frame, "line");
    gboolean keep_debug_focus;

    if (reason != NULL && g_str_has_prefix (reason, "exited"))
    {
        const char *exit_code = gdb_mi_record_string (record, "exit-code");
        char *status = exit_code != NULL
            ? g_strdup_printf (_ ("\nProgram exited with code %s.\n"), exit_code)
            : g_strdup (_ ("\nProgram exited.\n"));

        debug_output_append (debug, status);
        g_free (status);
        debug_finished (debug);
        return;
    }

    if (g_strcmp0 (reason, "signal-received") == 0)
    {
        char *text_value = g_strdup_printf (_ ("\nProgram received signal %s, %s.\n"),
                                            gdb_mi_record_string (record, "signal-name") != NULL
                                                ? gdb_mi_record_string (record, "signal-name")
                                                : "?",
                                            gdb_mi_record_string (record, "signal-meaning") != NULL
                                                ? gdb_mi_record_string (record, "signal-meaning")
                                                : "");

        debug_output_append (debug, text_value);
        g_free (text_value);
    }
    else if (g_strcmp0 (reason, "function-finished") == 0
             && gdb_mi_record_string (record, "return-value") != NULL)
    {
        char *text_value = g_strdup_printf (_ ("Value returned is %s\n"),
                                            gdb_mi_record_string (record, "return-value"));

        debug_output_console (debug, text_value, FALSE);
        g_free (text_value);
    }

    keep_debug_focus = debug->session_window != NULL
        && debug->host->window_current (debug->host) == debug->session_window;
    debug->state = DEBUG_STOPPED;
    debug->step_left = FALSE;
    debug_clear_current (debug);
    g_free (debug->current_func);
    debug->current_func = g_strdup (gdb_mi_get_string (frame, "func"));
    if (file != NULL && line != NULL && debug->host->show_location != NULL)
        (void) debug->host->show_location (debug->host, file, atol (line));
    if (keep_debug_focus && debug->session_window != NULL)
        debug->host->window_show (debug->host, debug->session_window);
    if (file != NULL && line != NULL && atol (line) > 0)
    {
        debug->current_file = g_strdup (file);
        debug->current_line = atol (line);
    }
    if (file != NULL)
    {
        debug_breakpoints_sync (debug);
        debug_marks_show (debug, file);
    }
    (void) debug_request (debug, NULL, NULL, NULL, "-stack-select-frame 0");
    (void) debug_request (debug, debug_reply_stack, NULL, NULL, "-stack-list-frames");
    (void) debug_request (debug, debug_reply_variables, NULL, NULL,
                          "-stack-list-variables --simple-values");
    debug_watches_refresh (debug);
}

/* GDB has moved a breakpoint: one waiting for a library is now in it */
static void
debug_breakpoint_modified (debugger_t *debug, const gdb_mi_record_t *record)
{
    const gdb_mi_value_t *bkpt = gdb_mi_get (record->results, "bkpt");
    const char *number = gdb_mi_get_string (bkpt, "number");
    const char *line = gdb_mi_get_string (bkpt, "line");
    guint i;

    if (number == NULL || line == NULL || atol (line) <= 0)
        return;
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        if (g_strcmp0 (bp->gdb_number, number) == 0 && bp->line != atol (line))
        {
            debug_breakpoints_sync (debug);
            bp->line = atol (line);
            debug_breakpoints_dedup (debug);
            debug_marks_show (debug, NULL);
            return;
        }
    }
}

static void
debug_record (const char *line, void *data)
{
    debugger_t *debug = (debugger_t *) data;
    gdb_mi_record_t *record = gdb_mi_parse (line);

    switch (record->kind)
    {
    case GDB_MI_RECORD_RESULT:
        if (!debug_request_reply (debug, record) && g_strcmp0 (record->klass, "error") == 0)
        {
            const char *msg = gdb_mi_record_string (record, "msg");

            debug_error (debug, msg != NULL ? msg : line);
            if (debug->state == DEBUG_STARTING)
                debug_finished (debug);
        }
        break;
    case GDB_MI_RECORD_EXEC:
        if (g_strcmp0 (record->klass, "running") == 0)
        {
            debug->state = DEBUG_RUNNING;
            debug_watches_clear_values (debug);
            debug_session_refresh (debug);
        }
        else if (g_strcmp0 (record->klass, "stopped") == 0)
        {
            debug_stopped (debug, record);
            debug_session_refresh (debug);
            widget_draw (WIDGET (debug->host->host_data));
        }
        break;
    case GDB_MI_RECORD_NOTIFY:
        if (g_strcmp0 (record->klass, "thread-group-exited") == 0
            || g_strcmp0 (record->klass, "gdb-exited") == 0)
        {
            if (g_strcmp0 (record->klass, "gdb-exited") == 0)
                debug_requests_clear (debug);
            debug_finished (debug);
        }
        else if (g_strcmp0 (record->klass, "breakpoint-modified") == 0)
            debug_breakpoint_modified (debug, record);
        break;
    case GDB_MI_RECORD_TARGET:
        debug_output_append (debug, record->text);
        break;
    case GDB_MI_RECORD_CONSOLE:
    case GDB_MI_RECORD_LOG:
        debug_output_console (debug, record->text, FALSE);
        break;
    case GDB_MI_RECORD_OTHER:
        // what GDB writes to stderr
        debug_output_console (debug, record->text, TRUE);
        break;
    default:
        break;
    }
    gdb_mi_record_free (record);
    tty_refresh ();
}

/* Send a command whose reply matters only when it is an error */
static gboolean
debug_send (debugger_t *debug, const char *command)
{
    return debug_request (debug, NULL, NULL, NULL, command) != 0;
}

static void
debug_queue_quoted (debugger_t *debug, const char *command, const char *argument)
{
    char *quoted = gdb_mi_quote (argument);
    char *line = g_strconcat (command, " ", quoted, NULL);

    g_queue_push_tail (debug->startup_commands, line);
    g_free (quoted);
}

static gboolean
debug_breakpoint_install (debugger_t *debug, debug_breakpoint_t *bp)
{
    char *location = g_strdup_printf ("%s:%ld", bp->file, bp->line);
    char *quoted = gdb_mi_quote (location);
    char *command;
    gboolean sent;

    // -f: a breakpoint in a library not loaded yet waits for it
    command = g_strconcat ("-break-insert -f ", quoted, NULL);
    bp->pending_token = debug_request (debug, debug_reply_breakpoint, NULL, NULL, command);
    sent = bp->pending_token != 0;
    g_free (command);
    g_free (quoted);
    g_free (location);
    return sent;
}

static void
debug_breakpoint_remove (debugger_t *debug, guint index)
{
    debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, index);

    if (bp->gdb_number != NULL && gdb_mi_session_alive (debug->gdb))
    {
        char *command = g_strconcat ("-break-delete ", bp->gdb_number, NULL);

        (void) debug_send (debug, command);
        g_free (command);
    }
    g_ptr_array_remove_index (debug->breakpoints, index);
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

static void
debug_setup_next (debugger_t *debug)
{
    char *command = g_queue_pop_head (debug->startup_commands);

    if (command != NULL)
    {
        if (debug_request (debug, debug_reply_startup, NULL, NULL, command) == 0)
        {
            g_queue_clear_full (debug->startup_commands, g_free);
            debug->state = DEBUG_FINISHED;
        }
        g_free (command);
        return;
    }

    debug_breakpoints_sync (debug);
    for (guint i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        g_clear_pointer (&bp->gdb_number, g_free);
        bp->pending_token = 0;
        (void) debug_breakpoint_install (debug, bp);
    }
    debug->breakpoints_installed = TRUE;
    debug_marks_show (debug, NULL);
    if (!debug_send (debug, "-exec-run"))
        debug->state = DEBUG_FINISHED;
}

/* The project of a file, as the project plugin sees it; NULL without that plugin */
static char *
debug_project_of (debugger_t *debug, void *edit)
{
    char *file = edit != NULL ? debug->host->get_current_file (debug->host, edit) : NULL;
    GVariantDict args;
    GVariant *reply;
    char *root = NULL;

    if (debug->host->service_call == NULL)
    {
        g_free (file);
        return NULL;
    }
    g_variant_dict_init (&args, NULL);
    if (file != NULL)
        g_variant_dict_insert (&args, "file", "s", file);
    reply = debug->host->service_call (debug->host, "project", "root", g_variant_dict_end (&args),
                                       NULL);
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "root", "s", &root);
        g_variant_unref (reply);
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
    gdb_mi_session_stop (debug->gdb);
    g_queue_clear_full (debug->startup_commands, g_free);
    debug_requests_clear (debug);
    debug_pty_close (debug);
    debug_clear_current (debug);
    debug->state = DEBUG_OFF;
    debug->breakpoints_installed = FALSE;
    g_ptr_array_set_size (debug->breakpoints, 0);
    debug_marks_show (debug, NULL);
    g_ptr_array_set_size (debug->launches, 0);
    g_ptr_array_set_size (debug->watches, 0);
    debug->active_launch = 0;
    g_free (debug->project_dir);
    debug->project_dir = project;
    g_ptr_array_set_size (debug->frames, 0);
    g_string_truncate (debug->output, 0);
    g_string_truncate (debug->stack_text, 0);
    g_string_truncate (debug->variables_text, 0);
    debug_config_load (debug);
    debug_marks_show (debug, NULL);
    if (debug->output_window != 0)
        debug_output_show (debug);
    if (debug->stack_window != 0)
        debug_text_show (debug, _ ("Call stack"), "", &debug->stack_window);
    if (debug->variables_window != 0)
        debug_text_show (debug, _ ("Local variables"), "", &debug->variables_window);
    if (debug->watches_window != 0)
        debug_watches_show (debug);
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

static mc_ep_result_t
debug_configure_impl (debugger_t *debug, void *edit, gboolean create_new)
{
    debug_launch_t *launch;
    char *name, *executable, *arguments, *directory, *environment, *gdb_path;
    char *absolute_executable, *absolute_dir;
    char **environment_entries = NULL;
    guint i;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    launch = create_new ? NULL : debug_active_launch (debug);
    name = input_dialog (_ ("Debug configuration"), _ ("Configuration name:"), NULL,
                         launch != NULL ? launch->name : _ ("Debug"), INPUT_COMPLETE_NONE);
    if (name == NULL)
        return MC_EPR_FAILED;
    if (*name == '\0')
    {
        debug_error (debug, _ ("Enter a configuration name."));
        g_free (name);
        return MC_EPR_FAILED;
    }
    for (i = 0; i < debug->launches->len; i++)
    {
        debug_launch_t *other = g_ptr_array_index (debug->launches, i);

        if (other != launch && g_strcmp0 (other->name, name) == 0)
        {
            debug_error (debug, _ ("A configuration with this name already exists."));
            g_free (name);
            return MC_EPR_FAILED;
        }
    }
    executable = input_dialog (_ ("Debug configuration"), _ ("Executable file:"), NULL,
                               launch != NULL ? launch->executable : "", INPUT_COMPLETE_FILENAMES);
    if (executable == NULL)
        goto cancelled_name;
    if (*executable == '\0')
    {
        debug_error (debug, _ ("Enter an executable file."));
        g_free (executable);
        goto cancelled_name;
    }
    arguments = input_dialog (_ ("Debug configuration"), _ ("Program arguments:"), NULL,
                              launch != NULL && launch->arguments != NULL ? launch->arguments : "",
                              INPUT_COMPLETE_NONE);
    if (arguments == NULL)
    {
        g_free (executable);
        goto cancelled_name;
    }
    directory = input_dialog (_ ("Debug configuration"), _ ("Working directory:"), NULL,
                              launch != NULL && launch->directory != NULL ? launch->directory
                                                                          : debug->project_dir,
                              INPUT_COMPLETE_FILENAMES);
    if (directory == NULL)
    {
        g_free (arguments);
        g_free (executable);
        goto cancelled_name;
    }
    environment = input_dialog (
        _ ("Debug configuration"), _ ("Environment (quoted NAME=VALUE entries):"), NULL,
        launch != NULL && launch->environment != NULL ? launch->environment : "",
        INPUT_COMPLETE_NONE);
    if (environment == NULL)
    {
        g_free (directory);
        g_free (arguments);
        g_free (executable);
        goto cancelled_name;
    }
    if (!debug_parse_environment (debug, environment, &environment_entries))
    {
        g_free (environment);
        g_free (directory);
        g_free (arguments);
        g_free (executable);
        goto cancelled_name;
    }
    g_strfreev (environment_entries);
    gdb_path = input_dialog (_ ("Debug configuration"), _ ("GDB executable:"), NULL,
                             launch != NULL && launch->gdb_path != NULL ? launch->gdb_path : "gdb",
                             INPUT_COMPLETE_FILENAMES);
    if (gdb_path == NULL)
    {
        g_free (environment);
        g_free (directory);
        g_free (arguments);
        g_free (executable);
        goto cancelled_name;
    }
    if (*gdb_path == '\0')
    {
        g_free (gdb_path);
        gdb_path = g_strdup ("gdb");
    }
    absolute_executable = g_canonicalize_filename (executable, debug->project_dir);
    absolute_dir = g_canonicalize_filename (*directory != '\0' ? directory : debug->project_dir,
                                            debug->project_dir);
    g_free (executable);
    g_free (directory);
    if (!g_file_test (absolute_dir, G_FILE_TEST_IS_DIR))
    {
        debug_error (debug, _ ("The working directory does not exist."));
        g_free (absolute_executable);
        g_free (absolute_dir);
        g_free (arguments);
        g_free (environment);
        g_free (gdb_path);
        goto cancelled_name;
    }
    if (launch == NULL)
    {
        launch = g_new0 (debug_launch_t, 1);
        g_ptr_array_add (debug->launches, launch);
        debug->active_launch = debug->launches->len - 1;
    }
    else
    {
        g_free (launch->name);
        g_free (launch->executable);
        g_free (launch->arguments);
        g_free (launch->directory);
        g_free (launch->environment);
        g_free (launch->gdb_path);
    }
    launch->name = name;
    launch->executable = absolute_executable;
    launch->arguments = arguments;
    launch->directory = absolute_dir;
    launch->environment = environment;
    launch->gdb_path = gdb_path;
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;

cancelled_name:
    g_free (name);
    return MC_EPR_FAILED;
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

static mc_ep_result_t
debug_start (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_launch_t *launch;
    GError *error = NULL;
    char **argv = NULL;
    char **environment_entries = NULL;
    int argc = 0, i;
    char *gdb_path, *absolute;
    const char *configured_gdb;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    if (debug->state == DEBUG_RUNNING || debug->state == DEBUG_STOPPED
        || debug->state == DEBUG_STARTING)
        return MC_EPR_FAILED;
    if (debug->gdb != NULL && gdb_mi_session_alive (debug->gdb))
        gdb_mi_session_stop (debug->gdb);
    debug_pty_close (debug);
    debug_clear_current (debug);
    launch = debug_active_launch (debug);
    if (launch == NULL)
    {
        if (debug_configure (data, edit) != MC_EPR_OK)
            return MC_EPR_FAILED;
        launch = debug_active_launch (debug);
    }
    if (debug->host->save_modified_files != NULL
        && !debug->host->save_modified_files (debug->host, debug->project_dir))
        return MC_EPR_FAILED;
    if (!g_file_test (launch->directory, G_FILE_TEST_IS_DIR))
    {
        debug_error (debug, _ ("The working directory does not exist."));
        return MC_EPR_FAILED;
    }
    absolute = g_canonicalize_filename (launch->executable, debug->project_dir);
    if (!g_file_test (absolute, G_FILE_TEST_IS_EXECUTABLE))
    {
        debug_error (debug, _ ("The configured executable does not exist or is not executable."));
        g_free (absolute);
        return MC_EPR_FAILED;
    }
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
    if (debug->gdb == NULL)
        debug->gdb = gdb_mi_session_new (debug_record, debug);
#ifdef ENABLE_MCTERM
    if (!debug_pty_open (debug))
    {
        debug_error (debug, _ ("Could not create a terminal for the program."));
        g_strfreev (argv);
        g_strfreev (environment_entries);
        g_free (absolute);
        g_free (gdb_path);
        return MC_EPR_FAILED;
    }
#endif
    if (!gdb_mi_session_start (debug->gdb, gdb_path, &error))
    {
        debug_error (debug, error != NULL ? error->message : _ ("Could not start GDB."));
        g_clear_error (&error);
        g_strfreev (argv);
        g_strfreev (environment_entries);
        g_free (absolute);
        g_free (gdb_path);
        debug_pty_close (debug);
        return MC_EPR_FAILED;
    }
    debug->state = DEBUG_STARTING;
    debug->breakpoints_installed = FALSE;
    g_queue_clear_full (debug->startup_commands, g_free);
    debug_requests_clear (debug);
    debug_watches_clear_values (debug);
    g_string_truncate (debug->output, 0);
    g_string_truncate (debug->console, 0);
    g_string_truncate (debug->stack_text, 0);
    g_string_truncate (debug->variables_text, 0);
    g_queue_push_tail (debug->startup_commands, g_strdup ("-gdb-set mi-async on"));
    g_queue_push_tail (debug->startup_commands, g_strdup ("-gdb-set startup-with-shell off"));
    debug_queue_quoted (debug, "-file-exec-and-symbols", absolute);
    debug_queue_quoted (debug, "-environment-cd", launch->directory);
    for (i = 0; environment_entries != NULL && environment_entries[i] != NULL; i++)
    {
        char *command = g_strconcat ("-gdb-set environment ", environment_entries[i], NULL);

        g_queue_push_tail (debug->startup_commands, command);
    }
    if (debug->pty_name != NULL)
        debug_queue_quoted (debug, "-inferior-tty-set", debug->pty_name);
    if (argc > 0)
    {
        GString *args = g_string_new ("-exec-arguments");

        for (i = 0; i < argc; i++)
        {
            char *quoted = gdb_mi_quote (argv[i]);

            g_string_append_c (args, ' ');
            g_string_append (args, quoted);
            g_free (quoted);
        }
        g_queue_push_tail (debug->startup_commands, g_string_free (args, FALSE));
    }
    (void) debug_session_show (debug, NULL);
    debug_setup_next (debug);
    g_strfreev (argv);
    g_strfreev (environment_entries);
    g_free (absolute);
    g_free (gdb_path);
    return MC_EPR_OK;
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
        char *real = realpath (file, NULL);

        bp = g_new0 (debug_breakpoint_t, 1);
        bp->file = real != NULL ? g_strdup (real) : g_strdup (file);
        bp->line = line;
        free (real);
        g_free (file);
        g_ptr_array_add (debug->breakpoints, bp);
        // the breakpoints of the start are sent already: this one is sent by itself
        if (debug_session_live (debug) && debug->breakpoints_installed)
            (void) debug_breakpoint_install (debug, bp);
    }
    debug_marks_show (debug, NULL);
    debug_config_save (debug);
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_control (void *data, void *edit, const char *command, debug_state_t required)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    if (debug->state != required)
        return MC_EPR_FAILED;
    return debug_send (debug, command) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
debug_continue (void *data, void *edit)
{
    return debug_control (data, edit, "-exec-continue", DEBUG_STOPPED);
}
static mc_ep_result_t
debug_pause (void *data, void *edit)
{
    return debug_control (data, edit, "-exec-interrupt", DEBUG_RUNNING);
}
static mc_ep_result_t
debug_next (void *data, void *edit)
{
    return debug_control (data, edit, "-exec-next", DEBUG_STOPPED);
}
static mc_ep_result_t
debug_step (void *data, void *edit)
{
    return debug_control (data, edit, "-exec-step", DEBUG_STOPPED);
}
static mc_ep_result_t
debug_finish (void *data, void *edit)
{
    return debug_control (data, edit, "-exec-finish", DEBUG_STOPPED);
}

static mc_ep_result_t
debug_run_to_cursor (debugger_t *debug, void *edit)
{
    char *file, *location, *quoted, *command;
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
    location = g_strdup_printf ("%s:%ld", file, line);
    quoted = gdb_mi_quote (location);
    command = g_strconcat ("-exec-until ", quoted, NULL);
    sent = debug_send (debug, command);
    g_free (command);
    g_free (quoted);
    g_free (location);
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

static mc_ep_result_t
debug_evaluate (debugger_t *debug, void *edit)
{
    char *expression, *quoted, *command;
    gboolean sent;

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
    quoted = gdb_mi_quote (expression);
    command = g_strconcat ("-data-evaluate-expression ", quoted, NULL);
    sent = debug_request (debug, debug_reply_evaluate, expression, g_free, command) != 0;
    g_free (command);
    g_free (quoted);
    debug->eval_pending = sent;
    return sent ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
debug_handle_key (void *data, int key, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    int cmd;

    if (edit == NULL || !debug_stepping (debug))
        return MC_EPR_NOT_SUPPORTED;

    cmd = debug_command_of_key (debug, key);
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
    if (event_id != MC_EP_EVENT_FOCUS_IN || edit == NULL || !debug_stepping (debug))
        return MC_EPR_NOT_SUPPORTED;
    debug_editor_buttonbar (debug, WIDGET (edit));
    return MC_EPR_OK;
}

static mc_ep_result_t
debug_show_output (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    debug_output_show (debug);
    if (debug->output_window != 0)
        debug_text_raise (debug, debug->output_window);
    else if (debug->output->len != 0)
        debug->host->message (debug->host, D_NORMAL, _ ("Debug output"), debug->output->str);
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
    if (frame->file != NULL && frame->line > 0 && debug->host->show_location != NULL)
        (void) debug->host->show_location (debug->host, frame->file, frame->line);
    {
        char *command = g_strdup_printf ("-stack-select-frame %ld", frame->level);

        (void) debug_send (debug, command);
        (void) debug_request (debug, debug_reply_variables, NULL, NULL,
                              "-stack-list-variables --simple-values");
        debug_watches_refresh (debug);
        g_free (command);
    }
    // the mark goes with the frame: the steps go on from there
    if (frame->file != NULL && frame->line > 0)
    {
        debug_clear_current (debug);
        debug->current_file = g_strdup (frame->file);
        debug->current_line = frame->line;
        debug_breakpoints_sync (debug);
        debug_marks_show (debug, frame->file);
    }
    return MC_EPR_OK;
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

static mc_ep_result_t
debug_add_watch (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *expression;
    mc_ep_result_t result = MC_EPR_FAILED;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    expression = input_dialog (_ ("Add watch"), _ ("Expression:"), NULL, "", INPUT_COMPLETE_NONE);
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
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    debug_watches_show (debug);
    debug_text_raise (debug, debug->watches_window);
    return MC_EPR_OK;
}

static mc_ep_result_t
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

static mc_ep_result_t
debug_stop (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;

    (void) edit;
    gdb_mi_session_stop (debug->gdb);
    g_queue_clear_full (debug->startup_commands, g_free);
    debug_requests_clear (debug);
    debug_pty_close (debug);
    debug_clear_current (debug);
    debug_watches_clear_values (debug);
    debug->state = DEBUG_OFF;
    debug->breakpoints_installed = FALSE;
    debug_marks_show (debug, NULL);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

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
    debug->startup_commands = g_queue_new ();
    debug->output = g_string_new (NULL);
    debug->console = g_string_new (NULL);
    debug->requests = g_ptr_array_new_with_free_func (debug_request_free);
    debug->locals = g_ptr_array_new_with_free_func (debug_local_free);
    debug->stack_text = g_string_new (NULL);
    debug->frames = g_ptr_array_new_with_free_func (debug_frame_free);
    debug->variables_text = g_string_new (NULL);
    debug->watches_text = g_string_new (NULL);
    debug->pty_master = -1;
    debug->pty_slave = -1;
    host->commands_register (host, DEBUG_KEYMAP_SECTION, N_ ("&Debugger"), debug_commands);
    for (i = 0; i < DEBUG_CMD_COUNT; i++)
        debug->commands[i] = host->command_id (host, debug_commands[i].name);
    for (i = 0; i < DEBUG_MARK_COUNT; i++)
        debug->marks[i] =
            host->marker_kind != NULL ? host->marker_kind (host, &debug_mark_kinds[i]) : -1;
    return debug;
}

static void
debug_close (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->session_window != NULL)
        debug->session_window->debug = NULL;
    gdb_mi_session_free (debug->gdb);
    debug_clear_current (debug);
    debug_pty_close (debug);
    g_ptr_array_free (debug->launches, TRUE);
    g_ptr_array_free (debug->breakpoints, TRUE);
    g_ptr_array_free (debug->watches, TRUE);
    g_queue_free_full (debug->startup_commands, g_free);
    g_string_free (debug->output, TRUE);
    g_string_free (debug->stack_text, TRUE);
    g_ptr_array_free (debug->frames, TRUE);
    g_string_free (debug->variables_text, TRUE);
    g_string_free (debug->watches_text, TRUE);
    g_free (debug->project_dir);
    g_free (debug->current_file);
    g_free (debug->current_func);
    g_ptr_array_free (debug->requests, TRUE);
    g_ptr_array_free (debug->locals, TRUE);
    g_string_free (debug->console, TRUE);
    g_free (debug);
}

static gboolean
debug_ok_to_quit (void *data)
{
    debugger_t *debug = (debugger_t *) data;

    if (debug->gdb == NULL || !gdb_mi_session_alive (debug->gdb))
        return TRUE;
    if (debug->state != DEBUG_RUNNING && debug->state != DEBUG_STOPPED
        && debug->state != DEBUG_STARTING)
        return TRUE;
    return query_dialog (_ ("Debug"), _ ("Stop the debug session and quit?"), D_NORMAL, 2,
                         _ ("&Stop"), _ ("&Cancel"))
        == 0;
}

static mc_ep_result_t
debug_file_open (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *file = debug->host->get_current_file (debug->host, edit);

    if (file == NULL)
        return MC_EPR_NOT_SUPPORTED;
    debug_breakpoints_sync (debug);
    debug_marks_show (debug, file);
    g_free (file);
    return MC_EPR_OK;
}

static const mc_ep_action_t debug_actions[] = {
    { "Open project", debug_open_project },
    { "Project status", debug_project_status },
    { "Configure", debug_configure },
    { "New configuration", debug_new_configuration },
    { "Select configuration", debug_select_configuration },
    { "Delete configuration", debug_delete_configuration },
    { "Start", debug_start },
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
};

static const mc_ep_cmd_menu_entry_t debug_menu[] = {
    { MC_EP_MENU_FILE, N_ ("Open debug &project..."), DEBUG_ACT_OPEN_PROJECT, NULL },
    { DEBUG_MENU, N_ ("Open proj&ect..."), DEBUG_ACT_OPEN_PROJECT, NULL },
    { DEBUG_MENU, N_ ("Project status..."), DEBUG_ACT_PROJECT_STATUS, NULL },
    { DEBUG_MENU, N_ ("Ne&w configuration..."), DEBUG_ACT_NEW_CONFIGURATION, NULL },
    { DEBUG_MENU, N_ ("Select con&figuration..."), DEBUG_ACT_SELECT_CONFIGURATION, NULL },
    { DEBUG_MENU, N_ ("Confi&gure selected..."), DEBUG_ACT_CONFIGURE, NULL },
    { DEBUG_MENU, N_ ("&Delete configuration..."), DEBUG_ACT_DELETE_CONFIGURATION, NULL },
    { DEBUG_MENU, NULL, 0, NULL },
    { DEBUG_MENU, N_ ("&Start"), DEBUG_ACT_START, NULL },
    { DEBUG_MENU, N_ ("Toggle &breakpoint"), DEBUG_ACT_TOGGLE_BREAKPOINT, NULL },
    { DEBUG_MENU, NULL, 0, NULL },
    { DEBUG_MENU, N_ ("&Continue"), DEBUG_ACT_CONTINUE, NULL },
    { DEBUG_MENU, N_ ("&Pause"), DEBUG_ACT_PAUSE, NULL },
    { DEBUG_MENU, N_ ("Step o&ver"), DEBUG_ACT_NEXT, NULL },
    { DEBUG_MENU, N_ ("Step &into"), DEBUG_ACT_STEP, NULL },
    { DEBUG_MENU, N_ ("Step o&ut"), DEBUG_ACT_FINISH, NULL },
    { DEBUG_MENU, N_ ("&Output"), DEBUG_ACT_OUTPUT, NULL },
    { DEBUG_MENU, N_ ("Call stac&k..."), DEBUG_ACT_STACK, NULL },
    { DEBUG_MENU, N_ ("&Add watch..."), DEBUG_ACT_ADD_WATCH, NULL },
    { DEBUG_MENU, N_ ("&Remove watch..."), DEBUG_ACT_REMOVE_WATCH, NULL },
    { DEBUG_MENU, N_ ("Show watc&hes"), DEBUG_ACT_WATCHES, NULL },
    { DEBUG_MENU, N_ ("Send li&ne..."), DEBUG_ACT_INPUT, NULL },
    { DEBUG_MENU, N_ ("Debug session..."), DEBUG_ACT_SESSION, NULL },
    { DEBUG_MENU, N_ ("S&top"), DEBUG_ACT_STOP, NULL },
};

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
};

const mc_editor_plugin_t *
debugger_get_plugin (void)
{
    return &debug_plugin;
}
