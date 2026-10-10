/*
   The debugger plugin: what its files share, the state of the debugger and its types.
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

#ifndef MC__DEBUGGER_INT_H
#define MC__DEBUGGER_INT_H

#include "lib/global.h"
#include "src/editor/editwindow.h"
#include "lib/editor-plugin.h"
#include "debug-backend.h"

/*** typedefs(not structures) and defined constants **********************************************/

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
    long gdb_line;  // the line GDB put it on, of the text the program was built from
    unsigned int pending_token;
    gboolean disabled;    // kept, but GDB does not stop on it
    gboolean unverified;  // the debugger has it, on no code yet
    char *condition;      // the program stops there only when it is true; NULL for always
} debug_breakpoint_t;

typedef struct
{
    char *name;
    char *executable;
    char *arguments;
    char *directory;
    char *environment;
    char *gdb_path;
    gboolean build;  // the project is built before the start
    // the program runs in a terminal window of its own, the screen and the keys its own, and
    // not in the console
    gboolean terminal;
    // "dap": a debug adapter runs it, @adapter its command; on a socket at @address
    // ("host:port") when there is one; @launch_extra, JSON, goes into its request "launch"
    char *backend;
    char *adapter;
    char *address;
    char *launch_extra;
} debug_launch_t;

typedef struct
{
    char *expression;
    char *value;
    unsigned int pending_token;
} debug_watch_t;

typedef struct debug_session_window_t debug_session_window_t;
typedef struct debug_disasm_window_t debug_disasm_window_t;

/* A breakpoint on an instruction, from the window of the instructions; not kept */
typedef struct
{
    char *address;
    char *gdb_number;
    unsigned int pending_token;
} debug_address_breakpoint_t;

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
    DEBUG_CMD_PANEL,
    DEBUG_CMD_INDEX,
    DEBUG_CMD_STEP_INSTRUCTION,
    DEBUG_CMD_NEXT_INSTRUCTION,
    DEBUG_CMD_SHOW_STOP,
    DEBUG_CMD_CONDITION,
    DEBUG_CMD_COUNT
};

#define DEBUG_KEYMAP_SECTION "debugger"
#define DEBUG_SERVICE        "debugger"
#define DEBUG_MENU           N_ ("&Debug")

/* the marks of the debugger in the gutter */
enum
{
    DEBUG_MARK_BREAKPOINT,
    DEBUG_MARK_PENDING,
    DEBUG_MARK_DISABLED,
    DEBUG_MARK_CONDITION,
    DEBUG_MARK_EXEC,
    DEBUG_MARK_EXEC_BREAKPOINT,
    DEBUG_MARK_COUNT
};
typedef struct
{
    mc_editor_host_t *host;
    debug_backend_t *backend;
    debug_state_t state;
    char *project_dir;
    GPtrArray *launches;
    guint active_launch;
    GPtrArray *breakpoints;
    GPtrArray *watches;
    // what GDB says of itself, apart from the output of the program
    GString *console;
    gint64 console_window;
    GPtrArray *frames;
    int pty_master;
    int pty_slave;
    char *pty_name;
    // the program runs in the terminal of the plugin terminal, the tab Program
    gboolean program_terminal;
    gboolean service;  // the service "debugger" is offered
    /* the time the program was run on at, by a continue or a step, and the time it ran till it
       stopped, in microseconds: the work of the debugger is in it, and of the user typing what
       the program reads */
    gint64 run_started;
    gint64 run_time;
    // a program that runs on longer than a step: its tab takes the focus, a timerfd tells when
    int run_timer;
    char *current_file;
    long current_line;
    // the breakpoints went to GDB at the start: a new one goes by itself
    gboolean breakpoints_installed;
    // steps out of code with no source in a row: not for ever
    int steps_out;
    gboolean eval_pending;
    // the local variables of the frame: debug_variable_t
    GPtrArray *locals;
    // the registers, when the panel shows them, and their values at the stop before
    GPtrArray *registers;
    GHashTable *registers_before;
    gboolean registers_shown;
    // the last step went by an instruction: code with no source is not stepped out of
    gboolean instruction_step;
    char *current_func;
    char *current_address;
    /* the user went back to editing with the program stopped; the next stop steps again */
    gboolean step_left;
    int marks[DEBUG_MARK_COUNT];
    // the glyphs of the marks, from the skin: the panel shows them as the gutter does
    char *glyphs[DEBUG_MARK_COUNT];
    long commands[DEBUG_CMD_COUNT];
    // the configurations are kept in the project, .coole/debug.ini, for all who work on it
    gboolean launches_in_project;
    // the tokens of breakpoints removed before GDB said their numbers: deleted when it does
    GArray *dropped_tokens;
    // a start waits for the build; the build is done for this start
    gboolean start_after_build;
    gboolean built_for_start;
    guint build_signal;
    // debug mode: the file windows take the keys of the debugger, F3 to F8, whether the program
    // runs or not (coole --debug, or Debug > Debug keys in files)
    gboolean debug_mode;
    gboolean layout_pushed;  // the editor keeps the windows of before debugging
    debug_session_window_t *session_window;
    // the virtual environment of the project with no debugpy has been told of
    gboolean venv_told;
    // the window of the instructions, those it shows, and the address of the frame among them
    debug_disasm_window_t *disasm_window;
    GPtrArray *instructions;
    char *disasm_address;
    GPtrArray *address_breakpoints;
    // the lines of the source file the window shows lines of
    char *source_file;
    char **source_lines;
} debugger_t;

struct debug_disasm_window_t
{
    WEditWindow window;
    debugger_t *debug;
    int cursor;
    int top;
    gboolean follow;  // the next drawing brings the program counter in sight
};

struct debug_session_window_t
{
    WEditWindow window;
    debugger_t *debug;
    int cursor;  // the row of the panel the cursor is on
    int top;     // the first row in sight
};

/* a function of the project, from the index of the plugin ctags */
typedef struct
{
    char *name;
    char *file;
    long line;
} debug_function_t;

/*** declarations of public functions ************************************************************/

/*** functions shared by the files of the plugin */

/* debugger.c */
void debug_functions_free (gpointer p);
debug_launch_t *debug_active_launch (const debugger_t *debug);
int debug_command (const debugger_t *debug, long command);
int debug_command_of_key (const debugger_t *debug, int key);
gboolean debug_stepping (const debugger_t *debug);
gboolean debug_step_passes (long command);
gboolean debug_run_command (debugger_t *debug, int cmd, void *edit);
void debug_config_save (debugger_t *debug);
void debug_error (debugger_t *debug, const char *message_text);
gboolean debug_alive (const debugger_t *debug);
gboolean debug_session_live (const debugger_t *debug);
int debug_breakpoint_mark (const debugger_t *debug, const debug_breakpoint_t *bp);
void debug_marks_show (debugger_t *debug, const char *file);
void debug_clear_current (debugger_t *debug);
gboolean debug_source_is_asm (const char *file);
gboolean debug_source_here (const char *file);
void debug_breakpoint_remove (debugger_t *debug, guint index);
void debug_breakpoint_toggle_enabled (debugger_t *debug, guint index);
void debug_breakpoints_sync (debugger_t *debug);
gboolean debug_require_project (debugger_t *debug, void *edit);
GPtrArray *debug_functions (const debugger_t *debug, const char *root, const char *file,
                            const char *query);
void debug_breakpoint_condition (debugger_t *debug, debug_breakpoint_t *bp);

/* debug-panel.c */
void debug_window_buttonbar (const debugger_t *debug, Widget *w);
void debug_editor_buttonbar (debugger_t *debug, Widget *edit);
gboolean debug_session_close_window (WEditWindow *win);
void debug_session_refresh (debugger_t *debug);
mc_ep_result_t debug_session_show (void *data, void *edit);
mc_ep_window_state_t debug_panel_state (void *data);
void debug_panel_show (void *data);
void debug_panel_close (void *data);
void *debug_panel_window (void *data);

/* debug-values.c */
void debug_watch_free (gpointer data);
void debug_watches_refresh (debugger_t *debug);
void debug_watches_clear_values (debugger_t *debug);
void debug_notes_clear (debugger_t *debug);
void debug_reply_stack (void *ui, const debug_reply_t *reply, void *data);
void debug_reply_variables (void *ui, const debug_reply_t *reply, void *data);
void debug_registers_refresh (debugger_t *debug);
void debug_registers_toggle (debugger_t *debug);
void debug_registers_clear (debugger_t *debug);
mc_ep_result_t debug_evaluate_text (debugger_t *debug, char *expression);
mc_ep_result_t debug_evaluate (debugger_t *debug, void *edit);
mc_ep_result_t debug_show_stack (void *data, void *edit);
void debug_select_frame (debugger_t *debug, const debug_frame_t *frame);
mc_ep_result_t debug_act_evaluate (void *data, void *edit);
mc_ep_result_t debug_add_watch (void *data, void *edit);
mc_ep_result_t debug_remove_watch (void *data, void *edit);
mc_ep_result_t debug_show_watches (void *data, void *edit);

/* debug-disasm.c */
void debug_disasm_refresh (debugger_t *debug);
void debug_disasm_show (debugger_t *debug, gboolean focus);
void debug_address_breakpoint_free (gpointer p);
void debug_address_breakpoint_install (debugger_t *debug, debug_address_breakpoint_t *bp);
void debug_address_breakpoint_toggle (debugger_t *debug, const char *address);
mc_ep_window_state_t debug_disasm_state (void *data);
void debug_disasm_kind_show (void *data);
void debug_disasm_kind_close (void *data);
void *debug_disasm_window (void *data);
mc_ep_result_t debug_act_disassembly (void *data, void *edit);

/* debug-program.c */
void debug_output_show (debugger_t *debug);
void debug_output_append (debugger_t *debug, const char *text_value);
void debug_output_console (debugger_t *debug, const char *text_value, gboolean line);
void debug_pty_close (debugger_t *debug);
void debug_program_show (debugger_t *debug);
void debug_run_timer (debugger_t *debug, gboolean arm);
gboolean debug_program_current (debugger_t *debug);
gboolean debug_pty_open (debugger_t *debug);
gboolean debug_terminal_open (debugger_t *debug);
void debug_gdb_command (debugger_t *debug);
gboolean debug_program_key (debugger_t *debug, int key);
mc_ep_result_t debug_show_output (void *data, void *edit);
mc_ep_result_t debug_send_input (void *data, void *edit);
mc_ep_window_state_t debug_console_state (void *data);
void debug_console_show (void *data);
void debug_console_close (void *data);
void *debug_console_window (void *data);
mc_ep_result_t debug_act_gdb_command (void *data, void *edit);

/*** end of shared functions */

#endif /* MC__DEBUGGER_INT_H */
