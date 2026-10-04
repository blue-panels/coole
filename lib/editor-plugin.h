/** \file editor-plugin.h
 *  \brief Header: editor plugin API for mcedit extensions
 */

#ifndef MC__EDITOR_PLUGIN_H
#define MC__EDITOR_PLUGIN_H

#include "lib/global.h"
#include "lib/widget/rect.h"  // WRect
#include "lib/plugin-service.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MC_EDITOR_PLUGIN_API_VERSION 10
#define MC_EDITOR_PLUGIN_ENTRY       "mc_editor_plugin_register"
#define MC_EDITOR_PLUGIN_CMD_BASE    30000L /* Plugins-menu: base + plugin_index */
#define MC_EDITOR_PLUGIN_ACTION_BASE 31000L /* per-action menu commands           */
#define MC_EDITOR_PLUGIN_ACTIONS_MAX 256    /* max named actions per plugin        */
#ifndef MC_PLUGINS_DIR
#define MC_PLUGINS_DIR "/usr/lib/coole/plugins"
#endif

/* Well-known target menu names for mc_ep_cmd_menu_entry_t.menu_name.  Any other name is a menu of
 * the plugins' own, made in the menubar before Window: "&Debug" (N_() translatable, & marks the
 * letter that drops it with Alt). */
#define MC_EP_MENU_COMMAND  "Command"
#define MC_EP_MENU_FILE     "File"
#define MC_EP_MENU_NAVIGATE "Navigate"
/* the Plugins menu: the windows a plugin shows, "Project tree" for one */
#define MC_EP_MENU_PLUGINS "Plugins"

/*** enums ***************************************************************************************/

typedef enum
{
    MC_EPR_OK = 0,
    MC_EPR_FAILED = -1,
    MC_EPR_NOT_SUPPORTED = -2
} mc_ep_result_t;

typedef enum
{
    MC_EPF_NONE = 0,
    MC_EPF_HAS_MENU = 1 << 0
} mc_ep_flags_t;

typedef struct mc_ep_state_t
{
    gboolean available;
    gboolean enabled;
    const char *reason;
} mc_ep_state_t;

/*** enums ***************************************************************************************/

/* Event IDs for handle_event() */
#define MC_EP_EVENT_FILE_SAVED   1 /* file written to disk; payload = const char *path */
#define MC_EP_EVENT_FILE_RENAMED 2 /* file renamed via Save As; payload = const char *new_path */
#define MC_EP_EVENT_FOCUS_IN     3 /* editor window got focus */
#define MC_EP_EVENT_FOCUS_OUT    4 /* editor window lost focus */
/* The editor is idle after the text of @edit changed: told once for the changes that came
   together, and when another file window comes to the front.  payload = NULL */
#define MC_EP_EVENT_TEXT_CHANGED 5
/* The editor is idle with the cursor of @edit on another line, or another file window in
   front.  payload = NULL */
#define MC_EP_EVENT_CURSOR_MOVED 6

/*** structures declarations (and typedefs of structures)*****************************************/

/* A kind of mark in the gutter of the file windows, which a plugin registers (marker_kind()).
 * The mark sits in the column between the line number and the fold mark; the skin gives its
 * glyph in [widget-editor] and its colours in [editor]. */
typedef struct
{
    const char *name;                /* unique: "debugger.breakpoint" */
    const char *glyph_key;           /* [widget-editor] key of the glyph */
    const char *glyph;               /* glyph when the skin has none, UTF-8 terminal */
    const char *glyph_ascii;         /* the same on any other terminal */
    const char *color_key;           /* [editor] key of the glyph colour; unset: the gutter's */
    const char *line_color_key;      /* [editor] key to colour the whole line; NULL: none */
    const char *line_color_fallback; /* [editor] key when the skin has no line_color_key */
    int priority;                    /* of two marks on one line the higher one is shown */
    const char *color;               /* when the skin has no color_key: this foreground, "red",
                                        on the gutter's background; NULL: the gutter's color */
} mc_ep_marker_kind_t;

/* A command of a plugin with its default keys ("f5; ctrl-r"), for commands_register() */
typedef struct
{
    const char *name;        /* in the keymap files: "DebugStepOver" */
    const char *description; /* N_() translatable, for Options > Key bindings */
    const char *keys;        /* NULL: none */
} mc_ep_command_t;

/* What the editor provides to a plugin */
typedef struct mc_editor_host_t
{
    /* v2 */
    void (*redraw) (struct mc_editor_host_t *host);
    void (*message) (struct mc_editor_host_t *host, int flags, const char *title, const char *text);
    void *host_data; /* opaque, owned by editor core */

    /* v3: generic editor service callbacks.
     * Strings returned by get_* are allocated by core, freed by plugin with g_free().
     * Strings passed to jump_to / insert_text are owned by the plugin. */

    /* Word under cursor.  NULL if not on a word. */
    char *(*get_cursor_word) (struct mc_editor_host_t *host, void *edit);
    /* Absolute path of currently edited file.  NULL if no file is open. */
    char *(*get_current_file) (struct mc_editor_host_t *host, void *edit);
    /* Current cursor line, 1-based.  0 if unavailable. */
    long (*get_cursor_line) (struct mc_editor_host_t *host, void *edit);
    /* Open file at line, recording current position in the navigation stack. */
    gboolean (*jump_to) (struct mc_editor_host_t *host, void *edit, const char *file, long line);
    /* Insert text at cursor, first removing remove_before bytes backwards. */
    void (*insert_text) (struct mc_editor_host_t *host, void *edit, const char *text,
                         gsize remove_before);

    /* v6: windows of the editor screen.
     * A window is a WEditWindow (src/editor/editwindow.h) of a class of the plugin, made with
     * edit_window_init().  Once added the editor owns it: the window is destroyed when the user
     * closes it and when the editor ends, and the class learns of that by MSG_DESTROY; the plugin
     * never frees an added window.  The windows are destroyed before close() of the plugin. */

    /* The part of the screen the windows take. */
    void (*window_area) (struct mc_editor_host_t *host, WRect *r);
    /* Put a window on the screen, on top of the others and selected. */
    void (*window_add) (struct mc_editor_host_t *host, void *window);
    /* Show a window as it was when hidden, on top of the others and selected. */
    void (*window_show) (struct mc_editor_host_t *host, void *window);
    /* Hide a window as it is; the next window is selected. */
    void (*window_hide) (struct mc_editor_host_t *host, void *window);
    /* Make room for a window that does not fill the screen: the topmost fullscreen window stops
       being fullscreen and takes the area above it. */
    void (*window_make_room) (struct mc_editor_host_t *host, void *window);
    /* Make that window fullscreen again, unless the user has moved or resized it since. */
    void (*window_give_room_back) (struct mc_editor_host_t *host, void *window);
    /* The window with the focus, NULL when none has it. */
    void *(*window_current) (struct mc_editor_host_t *host);
    /* The topmost file window seen on the screen, NULL when there is none. */
    void *(*window_top_file) (struct mc_editor_host_t *host);

    /* v6: the text of a file window.  get_text() gives all of it, @len bytes; caller frees.
     * get_revision() grows with every change of the text. */
    char *(*get_text) (struct mc_editor_host_t *host, void *edit, gsize *len);
    guint64 (*get_revision) (struct mc_editor_host_t *host, void *edit);

    /* v6: services, which plugins and scripts offer one another (lib/plugin-service.h).
     * A plugin offers a service from open() and takes it back in close(). */
    gboolean (*service_register) (struct mc_editor_host_t *host, const char *name,
                                  mc_service_call_fn call, void *data, GError **error);
    void (*service_unregister) (struct mc_editor_host_t *host, const char *name);
    GVariant *(*service_call) (struct mc_editor_host_t *host, const char *name, const char *method,
                               GVariant *args, GError **error);
    guint (*service_connect) (struct mc_editor_host_t *host, const char *name,
                              mc_service_signal_fn fn, void *user_data);
    void (*service_disconnect) (struct mc_editor_host_t *host, guint id);
    void (*service_emit) (struct mc_editor_host_t *host, const char *name, const char *signal,
                          GVariant *args);

    /* v9: marks in the gutter, addressed by absolute file path and 1-based line.  A mark moves
     * with its line when lines are inserted or deleted above it.
     * marker_kind() registers a kind once and gives its id; the same name gives the same id.
     * set_marker() puts or takes a mark in every window of the file.
     * clear_markers() takes all the marks of a kind, of one file or (file NULL) of all.
     * marker_lines() gives the lines (long, sorted) of the marks of a kind in the first window
     * of the file, NULL when no window has it; caller frees with g_array_free(). */
    int (*marker_kind) (struct mc_editor_host_t *host, const mc_ep_marker_kind_t *kind);
    void (*set_marker) (struct mc_editor_host_t *host, const char *file, long line, int kind,
                        gboolean enabled);
    void (*clear_markers) (struct mc_editor_host_t *host, const char *file, int kind);
    GArray *(*marker_lines) (struct mc_editor_host_t *host, const char *file, int kind);

    /* v9: commands with keys the user can change.  commands_register() gives the plugin a section
     * of the keymap (its name, then the title it has in Options > Key bindings) with the
     * commands, the last one with a NULL name; a command the program has already, "Help", is
     * that one.  command_id() gives the number of a command by its name, the same as long as the
     * program runs; command_lookup() the command a key is bound to in the section, CK_IgnoreKey
     * when none.  A command of the plugin that comes to the editor (from the button bar, for
     * one) goes to handle_action(). */
    void (*commands_register) (struct mc_editor_host_t *host, const char *section,
                               const char *title, const mc_ep_command_t *commands);
    long (*command_id) (struct mc_editor_host_t *host, const char *name);
    long (*command_lookup) (struct mc_editor_host_t *host, const char *section, int key);

    /* v9: show the window of a file, or open it in a new one; its cursor stays where it was */
    gboolean (*open_file) (struct mc_editor_host_t *host, const char *file);

    /* v9: a note after the text of a line (1-based) of every window of a file, NULL takes it
     * off; clear_line_notes() takes all of them off a file, or (file NULL) off all files */
    void (*set_line_note) (struct mc_editor_host_t *host, const char *file, long line,
                           const char *text);
    void (*clear_line_notes) (struct mc_editor_host_t *host, const char *file);

    /* v9: an option the program was started with, NULL when it was not: "debug", the directory
     * of a project to debug (coole --debug) */
    const char *(*startup_option) (struct mc_editor_host_t *host, const char *name);
    /* Call @fn with @data once the editor is idle: what opens windows or files when a plugin
     * starts goes so, not from open() or from an event */
    void (*call_later) (struct mc_editor_host_t *host, void (*fn) (void *data), void *data);
    /* Close a window as the user would, asking first when it has to; TRUE when it is gone.  Not
     * from an event of that window: from call_later() */
    gboolean (*window_close) (struct mc_editor_host_t *host, void *window);
    /* v10: a window added in the column at the right of the screen: all its height, the others
       making room, or the lower part of the lowest window of the column there is; @cols wide
       when it makes the column.  Its room goes back as with window_give_room_back(), a window
       of the column under it taking its place */
    void (*window_dock_right) (struct mc_editor_host_t *host, void *window, int cols);
    /* Show a debugger location without adding each step to the navigation stack. */
    gboolean (*show_location) (struct mc_editor_host_t *host, const char *file, long line);
    /* v8: offer to save modified source files within a project root. */
    gboolean (*save_modified_files) (struct mc_editor_host_t *host, const char *project_root);
} mc_editor_host_t;

/* A named action a plugin exposes for menu or keyboard use.
 * Invoked directly (not via activate/handle_action). */
typedef struct
{
    const char *label; /* action description */
    mc_ep_result_t (*callback) (void *plugin_data, void *edit);
} mc_ep_action_t;

/* An entry injected into a named editor top-level menu.
 * label = NULL means a separator (action_index and shortcut are ignored). */
typedef struct
{
    const char *menu_name; /* MC_EP_MENU_NAVIGATE, MC_EP_MENU_COMMAND, etc. */
    const char *label;     /* translatable menu item text with & accelerator; NULL = separator */
    int action_index;      /* index into mc_editor_plugin_t.actions[] */
    const char *shortcut;  /* shortcut text shown right-aligned in the menu; NULL = none */
} mc_ep_cmd_menu_entry_t;

/* What a plugin provides (callback table) */
typedef struct mc_editor_plugin_t
{
    int api_version;          /* MC_EDITOR_PLUGIN_API_VERSION */
    const char *name;         /* plugin id: "ctags", "spell" */
    const char *display_name; /* UI label */
    mc_ep_flags_t flags;

    /* Required */
    void *(*open) (mc_editor_host_t *host, void *editor_dialog /* opaque */);
    void (*close) (void *plugin_data);

    /* Optional */
    mc_ep_result_t (*activate) (void *plugin_data, void *edit /* opaque */);
    mc_ep_result_t (*configure) (void *plugin_data, void *edit /* opaque */);
    mc_ep_result_t (*handle_action) (void *plugin_data, long command, void *edit /* opaque */);
    mc_ep_result_t (*query_state) (void *plugin_data, void *edit /* opaque */,
                                   mc_ep_state_t *state);
    mc_ep_result_t (*handle_key) (void *plugin_data, int key, void *edit /* opaque */);
    mc_ep_result_t (*handle_event) (void *plugin_data, void *edit /* opaque */, int event_id,
                                    void *payload);
    mc_ep_result_t (*on_file_open) (void *plugin_data, void *edit /* opaque */);
    mc_ep_result_t (*on_file_close) (void *plugin_data, void *edit /* opaque */);

    /* v4: named actions and top-level menu entries */
    const mc_ep_action_t *actions; /* NULL = none */
    int action_count;
    const mc_ep_cmd_menu_entry_t *cmd_menu_entries; /* NULL = none */
    int cmd_menu_entry_count;

    /* v4: resolve current shortcut text for action_index at menu-build time.
     * Returns a newly-allocated string (caller frees with g_free) or NULL.
     * NULL falls back to cmd_menu_entries[].shortcut.  May be NULL. */
    char *(*get_menu_shortcut) (int action_index);
    /* v7: ask before closing the editor while a plugin owns a running process. */
    gboolean (*ok_to_quit) (void *plugin_data);
} mc_editor_plugin_t;

typedef const mc_editor_plugin_t *(*mc_editor_plugin_register_fn) (void);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Registry */
gboolean mc_editor_plugin_add (const mc_editor_plugin_t *plugin);
const GSList *mc_editor_plugin_list (void);
const mc_editor_plugin_t *mc_editor_plugin_find_by_name (const char *name);

/* Loader */
void mc_editor_plugins_load (void);
void mc_editor_plugins_shutdown (void);

/*** inline functions ****************************************************************************/

#endif /* MC__EDITOR_PLUGIN_H */
