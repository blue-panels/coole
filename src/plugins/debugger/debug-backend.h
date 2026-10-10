/** \file debug-backend.h
 *  \brief Header: what the debugger plugin asks of a debugger, GDB/MI or a debug adapter
 *
 *  The plugin keeps the breakpoints, the watches, the panel and the marks; a backend runs the
 *  program under its debugger and says what happens.  Every request answers through its own
 *  callback, and what the debugger says by itself comes as an event.
 */

#ifndef MC__DEBUG_BACKEND_H
#define MC__DEBUG_BACKEND_H

#include <glib.h>

typedef struct debug_backend_t debug_backend_t;

/* What runs the program: argv and environment are NULL-terminated, NULL when empty */
typedef struct
{
    const char *program;
    char **argv;
    const char *directory;
    char **environment;
    const char *tty;       // the terminal of the program, NULL for none
    const char *debugger;  // the path of GDB, or the command of the adapter
    // a debug adapter: on a socket at "host:port" rather than its stdin and stdout, port 0 for
    // one it says on its output; JSON members added to the request that launches the program
    const char *address;
    const char *launch_extra;
} debug_start_t;

/* A frame of the call stack */
typedef struct
{
    long level;
    char *func;
    char *file;  // the source as the debugger names it, NULL when it has none
    long line;
    char *from;     // the library of a frame with no source
    char *address;  // of the instruction, 0x...
    char *label;    // "#0 func  file:line", made by the plugin
} debug_frame_t;

/* A variable of a frame, or a register */
typedef struct
{
    char *name;
    char *value;
} debug_variable_t;

/* An instruction of the machine */
typedef struct
{
    char *address;  // 0x...
    char *func;     // NULL in code with no symbol
    long offset;    // from the start of the function
    char *text;
    char *file;  // the source line it is of, NULL when that is not known
    long line;
} debug_instruction_t;

/* The answer to a request; what a kind of request does not give is NULL */
typedef struct
{
    guint request;
    gboolean ok;
    const char *msg;          // why it failed
    const char *value;        // of an expression
    const char *id;           // of a breakpoint the debugger has taken
    gboolean pending;         // taken, but on no code yet: a library not loaded, say
    long line;                // where it has put it
    GPtrArray *frames;        // debug_frame_t
    GPtrArray *variables;     // debug_variable_t
    GPtrArray *registers;     // debug_variable_t, the value in hex
    GPtrArray *instructions;  // debug_instruction_t, by their addresses
} debug_reply_t;

typedef void (*debug_reply_cb) (void *ui, const debug_reply_t *reply, void *data);

typedef enum
{
    DEBUG_EXEC_RUN,
    DEBUG_EXEC_CONTINUE,
    DEBUG_EXEC_PAUSE,
    DEBUG_EXEC_NEXT,
    DEBUG_EXEC_STEP,
    DEBUG_EXEC_FINISH,
    // by one instruction of the machine, into a call or over it
    DEBUG_EXEC_STEP_INSTRUCTION,
    DEBUG_EXEC_NEXT_INSTRUCTION
} debug_exec_t;

typedef enum
{
    DEBUG_STOP_OTHER,
    DEBUG_STOP_BREAKPOINT,
    DEBUG_STOP_STEP,    // a step has ended
    DEBUG_STOP_FINISH,  // a step out has ended
    DEBUG_STOP_SIGNAL,
    DEBUG_STOP_EXITED
} debug_stop_reason_t;

/* Where the program has stopped; what is not known is NULL */
typedef struct
{
    debug_stop_reason_t reason;
    gboolean has_frame;
    const char *func;
    const char *file;
    long line;
    const char *address;
    const char *exit_code;
    const char *signal_name;
    const char *signal_meaning;
} debug_stop_t;

typedef enum
{
    DEBUG_OUTPUT_CONSOLE,  // what the debugger says
    DEBUG_OUTPUT_PROGRAM,  // what the program writes, when it has no terminal of its own
    DEBUG_OUTPUT_ERROR     // what the debugger writes to stderr: a line of its own
} debug_output_t;

/* What a backend tells the plugin, @ui being the plugin's */
typedef struct
{
    void (*output) (void *ui, const char *text, debug_output_t kind);
    // the program is loaded: the breakpoints go, then DEBUG_EXEC_RUN
    void (*ready) (void *ui);
    // the debugger has refused to load the program; @msg NULL when it has said why
    void (*start_failed) (void *ui, const char *msg);
    void (*running) (void *ui);
    void (*stopped) (void *ui, const debug_stop_t *stop);
    // the program has ended, @gone: the debugger too, and it owes nothing any more
    void (*exited) (void *ui, gboolean gone);
    // the debugger has moved a breakpoint, a pending one into a library loaded for one
    void (*breakpoint_moved) (void *ui, const char *id, long line);
    // a pending breakpoint is on code now, on @line when it is known (else 0)
    void (*breakpoint_verified) (void *ui, const char *id, long line);
    // @unasked: an error no request is waiting for
    void (*error) (void *ui, const char *msg, gboolean unasked);
    // all that came in is told: time to draw
    void (*flush) (void *ui);
} debug_backend_events_t;

/* A request gives its number, 0 when it could not be sent (the backend says why); @cb NULL
   tells only an error, and @free_data frees @data either way */
typedef struct
{
    const char *name;
    gboolean (*start) (debug_backend_t *b, const debug_start_t *spec, GError **error);
    void (*stop) (debug_backend_t *b);
    gboolean (*alive) (const debug_backend_t *b);
    // the replies owed are forgotten
    void (*cancel) (debug_backend_t *b);
    guint (*exec) (debug_backend_t *b, debug_exec_t what, debug_reply_cb cb, void *data,
                   GDestroyNotify free_data);
    guint (*break_insert) (debug_backend_t *b, const char *file, long line, gboolean disabled,
                           debug_reply_cb cb, void *data, GDestroyNotify free_data);
    // a breakpoint on a function, @temporary: taken off when the program stops there
    guint (*break_function) (debug_backend_t *b, const char *func, gboolean temporary);
    // a breakpoint on an instruction
    guint (*break_address) (debug_backend_t *b, const char *address, debug_reply_cb cb, void *data,
                            GDestroyNotify free_data);
    guint (*break_delete) (debug_backend_t *b, const char *id);
    guint (*break_enable) (debug_backend_t *b, const char *id, gboolean enable);
    guint (*run_to) (debug_backend_t *b, const char *file, long line);
    guint (*select_frame) (debug_backend_t *b, long level);
    guint (*stack) (debug_backend_t *b, debug_reply_cb cb, void *data, GDestroyNotify free_data);
    guint (*variables) (debug_backend_t *b, debug_reply_cb cb, void *data,
                        GDestroyNotify free_data);
    guint (*evaluate) (debug_backend_t *b, const char *expression, debug_reply_cb cb, void *data,
                       GDestroyNotify free_data);
    // the instructions of the function around @address, or near it when it has no symbol
    guint (*disassemble) (debug_backend_t *b, const char *address, debug_reply_cb cb, void *data,
                          GDestroyNotify free_data);
    // whether the frame has registers to show; NULL: it has
    gboolean (*has_registers) (const debug_backend_t *b);
    // the registers of the frame: the general ones, the program counter and the flags
    guint (*registers) (debug_backend_t *b, debug_reply_cb cb, void *data,
                        GDestroyNotify free_data);
    // a command of the debugger itself, typed by the user
    guint (*console) (debug_backend_t *b, const char *line, debug_reply_cb cb, void *data,
                      GDestroyNotify free_data);
    void (*free) (debug_backend_t *b);
} debug_backend_ops_t;

struct debug_backend_t
{
    const debug_backend_ops_t *ops;
    const debug_backend_events_t *events;
    void *ui;
};

void debug_frame_free (gpointer data);
void debug_variable_free (gpointer data);
void debug_instruction_free (gpointer data);

/* GDB with its machine interface */
debug_backend_t *debug_gdb_mi_new (const debug_backend_events_t *events, void *ui);
#ifdef ENABLE_DAP
/* A debug adapter of the Debug Adapter Protocol */
debug_backend_t *debug_dap_new (const debug_backend_events_t *events, void *ui);
#endif

#endif
