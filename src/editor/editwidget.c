/*
   Editor initialisation and callback handler.

   Copyright (C) 1996-2026
   Free Software Foundation, Inc.

   Written by:
   Paul Sheer, 1996, 1997
   Andrew Borodin <aborodin@vmail.ru> 2012-2024
   Ilia Maslakov <il.smind@gmail.com> 2010-2012, 2026

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
 *  \brief Source: editor initialisation and callback handler
 *  \author Paul Sheer
 *  \date 1996, 1997
 */

#include <config.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/global.h"

#include "lib/tty/tty.h"    // LINES, COLS
#include "lib/tty/key.h"    // is_idle(), bracketed_pasting_in_progress
#include "lib/tty/color.h"  // tty_setcolor()
#include "lib/skin.h"
#include "lib/fileloc.h"  // EDIT_HOME_DIR
#include "lib/strutil.h"  // str_term_trim()
#include "lib/util.h"     // mc_build_filename()
#include "lib/plugin-prefs.h"
#include "lib/widget.h"
#include "lib/widget/table.h"
#include "lib/mcconfig.h"
#include "lib/event.h"  // mc_event_raise()
#include "lib/charsets.h"
#include "lib/editor-plugin.h"
#include "lib/extension-runtime.h"
#include "lib/runtime-events.h"

#include "src/keymap.h"  // keybind_lookup_keymap_command()
#include "src/runtime-host.h"
#include "src/setup.h"  // save_setup_cmd()
#include "src/events_init.h"
#include "src/appearance.h"      // appearance_box()
#include "src/keybind_dialog.h"  // keybind_dialog()
#include "src/key_learn.h"       // key_learn()
#include "src/key_sniffer.h"     // key_sniffer()
#include "src/manage_plugins.h"  // manage_plugins_dialog()

#include "edit-impl.h"
#include "editwidget.h"
#include "editmacros.h"  // edit_execute_macro()

/*** file scope macro definitions ****************************************************************/

#define WINDOW_MIN_LINES (2 + 2)
#define WINDOW_MIN_COLS  (2 + LINE_STATE_WIDTH + 2)

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const mc_editor_plugin_t *plugin;
    void *plugin_data;
} editor_plugin_instance_t;

typedef struct
{
    mc_editor_host_t *host;
    GPtrArray *instances; /* editor_plugin_instance_t* */
} editor_plugin_ctx_t;

/*** forward declarations (file scope functions) *************************************************/

static cb_ret_t edit_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data);
static void edit_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event);
static char *edit_class_get_title (const WEditWindow *win);
static gboolean edit_class_is_modified (const WEditWindow *win);
static gboolean edit_class_close (WEditWindow *win);
static gboolean edit_class_ok_to_quit (WEditWindow *win);
static void edit_quit (WDialog *h);

/*** global variables ****************************************************************************/

char *edit_fold_open_char = NULL;
char *edit_fold_close_char = NULL;

/* A file window of the editor screen */
const edit_window_class_t edit_class = {
    .callback = edit_callback,
    .mouse_callback = edit_mouse_callback,
    .get_title = edit_class_get_title,
    .is_modified = edit_class_is_modified,
    .close = edit_class_close,
    .ok_to_quit = edit_class_ok_to_quit,
    .min_lines = WINDOW_MIN_LINES,
    .min_cols = WINDOW_MIN_COLS,
};

/*** file scope variables ************************************************************************/

static unsigned int edit_dlg_init_refcounter = 0;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
edit_publish_runtime_open (WEdit *edit)
{
    mc_runtime_event_snapshot_t *snapshot;

    runtime_host_set_current_editor (edit);
    events_publish_runtime_startup ();
    if (!events_runtime_is_started () || !mc_runtime_events_is_initialized ())
        return;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_OPEN);
    snapshot->data.editor_open.editor =
        mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, edit);
    snapshot->data.editor_open.path =
        edit->filename != NULL ? g_strdup (edit->filename) : g_strdup ("");
    snapshot->data.editor_open.readonly = FALSE;
    snapshot->data.editor_open.line = (guint) MAX (edit->buffer.curs_line, 0) + 1;
    snapshot->data.editor_open.column = (guint) MAX (edit->curs_col, 0) + 1;

    (void) mc_runtime_event_publish (snapshot, NULL);
    mc_runtime_event_snapshot_free (snapshot);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_publish_runtime_key (WEdit *edit, int keycode)
{
    mc_runtime_event_snapshot_t *snapshot;
    gboolean consumed;
    unsigned int plain_key;

    runtime_host_set_current_editor (edit);
    if (!events_runtime_is_started () || !mc_runtime_events_is_initialized ())
        return FALSE;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor =
        mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, edit);
    snapshot->data.editor_key.key.name = tty_keycode_to_keyname (keycode);
    if (snapshot->data.editor_key.key.name == NULL)
    {
        mc_runtime_event_snapshot_free (snapshot);
        return FALSE;
    }

    snapshot->data.editor_key.key.code = keycode;
    snapshot->data.editor_key.key.shift = (keycode & KEY_M_SHIFT) != 0;
    snapshot->data.editor_key.key.ctrl = (keycode & KEY_M_CTRL) != 0;
    snapshot->data.editor_key.key.alt = (keycode & KEY_M_ALT) != 0;
    plain_key = (unsigned int) keycode & ~KEY_M_MASK;
    if ((keycode & (KEY_M_CTRL | KEY_M_ALT)) == 0 && plain_key >= ' ' && plain_key <= '~')
    {
        char text[2] = { (char) plain_key, '\0' };

        snapshot->data.editor_key.key.text = g_strdup (text);
    }

    (void) mc_runtime_event_publish (snapshot, NULL);
    consumed = snapshot->consumed;
    mc_runtime_event_snapshot_free (snapshot);

    return consumed;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: request redraw/refresh.
 */

static void
editor_host_redraw_impl (mc_editor_host_t *host)
{
    if (host != NULL && host->host_data != NULL)
        widget_draw (WIDGET (host->host_data));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: show message box.
 */

static void
editor_host_message_impl (mc_editor_host_t *host, int flags, const char *title, const char *text)
{
    (void) host;
    message (flags, title, "%s", text != NULL ? text : "");
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
word_has_identifier_char (const GString *w)
{
    gsize i;

    for (i = 0; i < w->len; i++)
        if (!is_break_char (w->str[i]))
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: get word under cursor.
 */

static char *
editor_host_get_cursor_word_impl (mc_editor_host_t *host, void *edit)
{
    WEdit *e = (WEdit *) edit;
    GString *word;
    off_t start = 0;
    gsize cut = 0;

    (void) host;

    if (e == NULL)
        return NULL;

    word = edit_buffer_get_word_from_pos (&e->buffer, e->buffer.curs1, &start, &cut);

    /* Cursor is on a break character (e.g. right after "foo(("): step back to
     * the end of the adjacent identifier. */
    if (word != NULL && !word_has_identifier_char (word) && start > 0)
    {
        g_string_free (word, TRUE);
        word = edit_buffer_get_word_from_pos (&e->buffer, start - 1, &start, &cut);
    }

    /* Cursor is past end of an identifier (e.g. at '\n'): retry one byte left. */
    if ((word == NULL || word->len == 0) && e->buffer.curs1 > 0)
    {
        if (word != NULL)
            g_string_free (word, TRUE);
        word = edit_buffer_get_word_from_pos (&e->buffer, e->buffer.curs1 - 1, &start, &cut);
    }

    if (word == NULL || word->len == 0 || !word_has_identifier_char (word))
    {
        if (word != NULL)
            g_string_free (word, TRUE);
        return NULL;
    }

    return g_string_free (word, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: get path of currently edited file.
 */

static char *
editor_host_get_current_file_impl (mc_editor_host_t *host, void *edit)
{
    WEdit *e = (WEdit *) edit;

    (void) host;

    if (e == NULL || e->filename == NULL)
        return NULL;

    return g_strdup (e->filename);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: get current cursor line (1-based).
 */

static long
editor_host_get_cursor_line_impl (mc_editor_host_t *host, void *edit)
{
    WEdit *e = (WEdit *) edit;

    (void) host;

    if (e == NULL)
        return 0;

    return e->start_line + e->curs_row + 1;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: jump to file:line, saving current position in the navigation stack.
 */

static gboolean
editor_host_jump_to_impl (mc_editor_host_t *host, void *edit, const char *file, long line)
{
    WEdit *e = (WEdit *) edit;
    WDialog *dlg;
    gboolean ret;

    (void) host;

    if (e == NULL || file == NULL)
        return FALSE;

    dlg = DIALOG (WIDGET (e)->owner);

    /* If the current file is modified, open target in a new window without
     * touching the navigation stack -- we are not navigating away. */
    if (e->modified != 0)
    {
        char *path = mc_path_absolute (file);
        edit_arg_t arg;

        edit_arg_init (&arg, path, line);
        ret = edit_load_file_from_filename (dlg, &arg);
        g_free (path);
        return ret;
    }

    if (edit_stack_iterator + 1 >= MAX_HISTORY_MOVETO)
        return FALSE;

    /* Save current position */
    if (e->filename != NULL)
    {
        edit_update_curs_col (e);
        edit_arg_assign (&edit_history_moveto[edit_stack_iterator], g_strdup (e->filename),
                         e->start_line + e->curs_row + 1);
        edit_history_moveto[edit_stack_iterator].column = e->curs_col;
        edit_history_moveto[edit_stack_iterator].start_line = e->start_line;
    }

    /* Push target and jump */
    edit_stack_iterator++;
    edit_arg_assign (&edit_history_moveto[edit_stack_iterator], mc_path_absolute (file), line);
    return edit_reload_line (e, &edit_history_moveto[edit_stack_iterator]);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: insert text at cursor, removing remove_before bytes.
 */

static void
editor_host_insert_text_impl (mc_editor_host_t *host, void *edit, const char *text,
                              gsize remove_before)
{
    WEdit *e = (WEdit *) edit;

    (void) host;

    if (e == NULL || text == NULL)
        return;

    for (gsize i = 0; i < remove_before; i++)
        edit_backspace (e, TRUE);

    for (const char *p = text; *p != '\0'; p++)
        edit_insert (e, (unsigned char) *p);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: the part of the screen the windows take.
 */

static void
editor_host_window_area_impl (mc_editor_host_t *host, WRect *r)
{
    edit_window_area (DIALOG (host->host_data), r);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: put a window of a plugin on the screen.
 */

static void
editor_host_window_add_impl (mc_editor_host_t *host, void *window)
{
    WDialog *h = DIALOG (host->host_data);

    edit_window_add (h, EDIT_WINDOW (window));
    widget_select (WIDGET (window));
    widget_draw (WIDGET (h));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: show a hidden window of a plugin.
 */

static void
editor_host_window_show_impl (mc_editor_host_t *host, void *window)
{
    (void) host;

    edit_window_show (EDIT_WINDOW (window));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: hide a window of a plugin.
 */

static void
editor_host_window_hide_impl (mc_editor_host_t *host, void *window)
{
    (void) host;

    edit_window_hide (EDIT_WINDOW (window));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: the fullscreen window makes room for a window of a plugin.
 */

static void
editor_host_window_make_room_impl (mc_editor_host_t *host, void *window)
{
    (void) host;

    edit_window_make_room (EDIT_WINDOW (window));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Host callback: the room made for a window of a plugin is given back.
 */

static void
editor_host_window_give_room_back_impl (mc_editor_host_t *host, void *window)
{
    (void) host;

    edit_window_give_room_back (EDIT_WINDOW (window));
}

/* --------------------------------------------------------------------------------------------- */
/* Host callbacks: the services, passed on to lib/plugin-service.c */

static gboolean
editor_host_service_register_impl (mc_editor_host_t *host, const char *name,
                                   mc_service_call_fn call, void *data, GError **error)
{
    (void) host;

    return mc_service_register (name, call, data, error);
}

static void
editor_host_service_unregister_impl (mc_editor_host_t *host, const char *name)
{
    (void) host;

    mc_service_unregister (name);
}

static GVariant *
editor_host_service_call_impl (mc_editor_host_t *host, const char *name, const char *method,
                               GVariant *args, GError **error)
{
    (void) host;

    return mc_service_call (name, method, args, error);
}

static guint
editor_host_service_connect_impl (mc_editor_host_t *host, const char *name, mc_service_signal_fn fn,
                                  void *user_data)
{
    (void) host;

    return mc_service_connect (name, fn, user_data);
}

static void
editor_host_service_disconnect_impl (mc_editor_host_t *host, guint id)
{
    (void) host;

    mc_service_disconnect (id);
}

static void
editor_host_service_emit_impl (mc_editor_host_t *host, const char *name, const char *signal,
                               GVariant *args)
{
    (void) host;

    mc_service_emit (name, signal, args);
}

/* --------------------------------------------------------------------------------------------- */

static void
editor_plugin_instance_free (gpointer data)
{
    editor_plugin_instance_t *inst = (editor_plugin_instance_t *) data;

    if (inst != NULL)
    {
        if (inst->plugin != NULL && inst->plugin->close != NULL && inst->plugin_data != NULL)
            inst->plugin->close (inst->plugin_data);
        g_free (inst);
    }
}

/* --------------------------------------------------------------------------------------------- */

static editor_plugin_ctx_t *
editor_plugin_ctx_create (WDialog *edit_dlg)
{
    const GSList *plugins;
    editor_plugin_ctx_t *ctx;

    if (edit_dlg == NULL)
        return NULL;

    plugins = mc_editor_plugin_list ();
    if (plugins == NULL)
        return NULL;

    ctx = g_new0 (editor_plugin_ctx_t, 1);
    ctx->host = g_new0 (mc_editor_host_t, 1);
    ctx->host->redraw = editor_host_redraw_impl;
    ctx->host->message = editor_host_message_impl;
    ctx->host->host_data = edit_dlg;
    ctx->host->get_cursor_word = editor_host_get_cursor_word_impl;
    ctx->host->get_current_file = editor_host_get_current_file_impl;
    ctx->host->get_cursor_line = editor_host_get_cursor_line_impl;
    ctx->host->jump_to = editor_host_jump_to_impl;
    ctx->host->insert_text = editor_host_insert_text_impl;
    ctx->host->window_area = editor_host_window_area_impl;
    ctx->host->window_add = editor_host_window_add_impl;
    ctx->host->window_show = editor_host_window_show_impl;
    ctx->host->window_hide = editor_host_window_hide_impl;
    ctx->host->window_make_room = editor_host_window_make_room_impl;
    ctx->host->window_give_room_back = editor_host_window_give_room_back_impl;
    ctx->host->service_register = editor_host_service_register_impl;
    ctx->host->service_unregister = editor_host_service_unregister_impl;
    ctx->host->service_call = editor_host_service_call_impl;
    ctx->host->service_connect = editor_host_service_connect_impl;
    ctx->host->service_disconnect = editor_host_service_disconnect_impl;
    ctx->host->service_emit = editor_host_service_emit_impl;
    ctx->instances = g_ptr_array_new_with_free_func (editor_plugin_instance_free);

    for (; plugins != NULL; plugins = g_slist_next (plugins))
    {
        const mc_editor_plugin_t *plugin = (const mc_editor_plugin_t *) plugins->data;
        editor_plugin_instance_t *inst;

        /* Honour user disable from Manage Plugins for this editor session. */
        if (plugin->name != NULL
            && mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_EDITOR, plugin->name))
            continue;

        inst = g_new0 (editor_plugin_instance_t, 1);
        inst->plugin = plugin;
        inst->plugin_data = plugin->open (ctx->host, edit_dlg);
        if (inst->plugin_data == NULL)
        {
            g_free (inst);
            continue;
        }

        g_ptr_array_add (ctx->instances, inst);
    }

    if (ctx->instances->len == 0)
    {
        g_ptr_array_free (ctx->instances, TRUE);
        g_free (ctx->host);
        g_free (ctx);
        return NULL;
    }

    return ctx;
}

/* --------------------------------------------------------------------------------------------- */

static void
editor_plugin_ctx_destroy (WDialog *edit_dlg)
{
    editor_plugin_ctx_t *ctx;

    if (edit_dlg == NULL)
        return;

    ctx = (editor_plugin_ctx_t *) edit_dlg->data.p;
    if (ctx == NULL)
        return;

    g_ptr_array_free (ctx->instances, TRUE);
    g_free (ctx->host);
    g_free (ctx);
    edit_dlg->data.p = NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_plugin_handle_action (WDialog *edit_dlg, long command, WEdit *edit)
{
    editor_plugin_ctx_t *ctx;
    gsize plugin_idx = 0;
    const GSList *plugins = NULL;
    const mc_editor_plugin_t *plugin = NULL;
    guint i;

    if (edit_dlg == NULL)
        return FALSE;

    ctx = (editor_plugin_ctx_t *) edit_dlg->data.p;
    if (ctx == NULL)
        return FALSE;

    if (edit == NULL && GROUP (edit_dlg)->current != NULL
        && edit_widget_is_editor (CONST_WIDGET (GROUP (edit_dlg)->current->data)))
        edit = EDIT (GROUP (edit_dlg)->current->data);

    /* Per-action command from a named menu (Navigate, Command, etc.) */
    if (command >= MC_EDITOR_PLUGIN_ACTION_BASE)
    {
        long encoded = command - MC_EDITOR_PLUGIN_ACTION_BASE;
        gsize p_idx = (gsize) (encoded / MC_EDITOR_PLUGIN_ACTIONS_MAX);
        int a_idx = (int) (encoded % MC_EDITOR_PLUGIN_ACTIONS_MAX);
        const mc_editor_plugin_t *aplugin = NULL;

        plugins = mc_editor_plugin_list ();
        for (; plugins != NULL && p_idx > 0; plugins = g_slist_next (plugins), p_idx--)
            ;
        aplugin = (plugins != NULL) ? (const mc_editor_plugin_t *) plugins->data : NULL;

        if (aplugin == NULL || aplugin->actions == NULL || a_idx >= aplugin->action_count)
            return FALSE;

        for (i = 0; i < ctx->instances->len; i++)
        {
            editor_plugin_instance_t *inst =
                (editor_plugin_instance_t *) g_ptr_array_index (ctx->instances, i);
            if (inst->plugin == aplugin)
                return (aplugin->actions[a_idx].callback (inst->plugin_data, edit) == MC_EPR_OK);
        }
        return FALSE;
    }

    if (command >= MC_EDITOR_PLUGIN_CMD_BASE)
    {
        plugin_idx = (gsize) (command - MC_EDITOR_PLUGIN_CMD_BASE);
        plugins = mc_editor_plugin_list ();
        for (; plugins != NULL && plugin_idx > 0; plugins = g_slist_next (plugins), plugin_idx--)
            ;
        plugin = (plugins != NULL) ? (const mc_editor_plugin_t *) plugins->data : NULL;
        if (plugin == NULL)
            return FALSE;
    }

    for (i = 0; i < ctx->instances->len; i++)
    {
        editor_plugin_instance_t *inst =
            (editor_plugin_instance_t *) g_ptr_array_index (ctx->instances, i);

        if (plugin != NULL)
        {
            if (inst->plugin != plugin)
                continue;

            if (plugin->query_state != NULL)
            {
                mc_ep_state_t state = { TRUE, TRUE, NULL };
                if (plugin->query_state (inst->plugin_data, edit, &state) == MC_EPR_OK
                    && (!state.available || !state.enabled))
                {
                    if (state.reason != NULL && *state.reason != '\0')
                        message (D_NORMAL, _ ("Plugin"), "%s", state.reason);
                    return FALSE;
                }
            }

            if (plugin->activate != NULL)
                return (plugin->activate (inst->plugin_data, edit) == MC_EPR_OK);
            if (plugin->handle_action != NULL)
                return (plugin->handle_action (inst->plugin_data, command, edit) == MC_EPR_OK);
            return FALSE;
        }
        else if (inst->plugin != NULL && inst->plugin->handle_action != NULL
                 && inst->plugin->handle_action (inst->plugin_data, command, edit) == MC_EPR_OK)
        {
            return TRUE;
        }
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_plugin_configure (WDialog *edit_dlg, long command, WEdit *edit)
{
    editor_plugin_ctx_t *ctx;
    gsize plugin_idx = 0;
    const GSList *plugins = NULL;
    const mc_editor_plugin_t *plugin = NULL;
    guint i;

    if (edit_dlg == NULL)
        return FALSE;

    ctx = (editor_plugin_ctx_t *) edit_dlg->data.p;
    if (ctx == NULL)
        return FALSE;

    if (command < MC_EDITOR_PLUGIN_CMD_BASE)
        return FALSE;

    plugin_idx = (gsize) (command - MC_EDITOR_PLUGIN_CMD_BASE);
    plugins = mc_editor_plugin_list ();
    for (; plugins != NULL && plugin_idx > 0; plugins = g_slist_next (plugins), plugin_idx--)
        ;
    plugin = (plugins != NULL) ? (const mc_editor_plugin_t *) plugins->data : NULL;
    if (plugin == NULL || plugin->configure == NULL)
        return FALSE;

    for (i = 0; i < ctx->instances->len; i++)
    {
        editor_plugin_instance_t *inst =
            (editor_plugin_instance_t *) g_ptr_array_index (ctx->instances, i);

        if (inst->plugin == plugin)
            return (plugin->configure (inst->plugin_data, edit) == MC_EPR_OK);
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
gboolean
edit_plugin_handle_key (WDialog *edit_dlg, int key, WEdit *edit)
{
    editor_plugin_ctx_t *ctx;
    guint i;

    if (edit_dlg == NULL)
        return FALSE;

    ctx = (editor_plugin_ctx_t *) edit_dlg->data.p;
    if (ctx == NULL)
        return FALSE;

    if (edit == NULL && GROUP (edit_dlg)->current != NULL
        && edit_widget_is_editor (CONST_WIDGET (GROUP (edit_dlg)->current->data)))
        edit = EDIT (GROUP (edit_dlg)->current->data);

    for (i = 0; i < ctx->instances->len; i++)
    {
        editor_plugin_instance_t *inst =
            (editor_plugin_instance_t *) g_ptr_array_index (ctx->instances, i);
        const mc_editor_plugin_t *plugin = inst->plugin;

        if (plugin == NULL || plugin->handle_key == NULL)
            continue;

        /* No state gate here: only the plugin knows whether the key is one of
           its own, and a disabled plugin still has to explain itself when the
           user presses that key. */
        if (plugin->handle_key (inst->plugin_data, key, edit) == MC_EPR_OK)
            return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Init the 'edit' subsystem
 */

static void
edit_dlg_init (void)
{
    edit_dlg_init_refcounter++;

    if (edit_dlg_init_refcounter == 1)
    {
        edit_window_state_char = mc_skin_get ("widget-editor", "window-state-char", "*");
        edit_window_close_char = mc_skin_get ("widget-editor", "window-close-char", "X");
        edit_fold_open_char = mc_skin_get ("widget-editor", "fold-open-char", "v");
        edit_fold_close_char = mc_skin_get ("widget-editor", "fold-close-char", ">");
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Deinit the 'edit' subsystem
 */

static void
edit_dlg_deinit (void)
{
    if (edit_dlg_init_refcounter == 1)
    {
        g_free (edit_window_state_char);
        g_free (edit_window_close_char);
        g_free (edit_fold_open_char);
        g_free (edit_fold_close_char);
    }

    if (edit_dlg_init_refcounter != 0)
        edit_dlg_init_refcounter--;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Show info about editor
 */

static void
edit_about (void)
{
    char *version = g_strdup_printf ("MCEdit %s", mc_global.mc_version);
    char *package_copyright = mc_get_package_copyright ();

    char *description =
        g_strdup_printf (_ ("A user friendly text editor\nwritten for the %s."), PACKAGE_NAME);

    {
        quick_widget_t quick_widgets[] = {
            QUICK_LABEL (version, NULL),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABEL (description, NULL),
            QUICK_SEPARATOR (FALSE),
            QUICK_LABEL (package_copyright, NULL),
            QUICK_START_BUTTONS (TRUE, TRUE),
            QUICK_BUTTON (_ ("&OK"), B_ENTER, NULL, NULL),
            QUICK_END,
        };

        WRect r = { -1, -1, 0, 40 };

        quick_dialog_t qdlg = {
            .rect = r,
            .title = _ ("About"),
            .help = "[Internal File Editor]",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        quick_widgets[0].pos_flags = WPOS_KEEP_TOP | WPOS_CENTER_HORZ;
        quick_widgets[2].pos_flags = WPOS_KEEP_TOP | WPOS_CENTER_HORZ;
        quick_widgets[4].pos_flags = WPOS_KEEP_TOP | WPOS_CENTER_HORZ;

        (void) quick_dialog (&qdlg);
    }

    g_free (version);
    g_free (package_copyright);
    g_free (description);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Show info about loaded editor plugins.
 */

typedef struct
{
    char *kind;
    char *name;
    char *id;
    char *api;
    char *flags;
    char *capabilities;
    long configure_command;
} edit_plugin_info_row_t;

static void
edit_plugin_info_row_destroy (edit_plugin_info_row_t *row)
{
    g_free (row->kind);
    g_free (row->name);
    g_free (row->id);
    g_free (row->api);
    g_free (row->flags);
    g_free (row->capabilities);
    g_free (row);
}

static void
edit_plugin_info_append_capability (GString *text, const char *name)
{
    if (text->len != 0)
        g_string_append (text, ", ");
    g_string_append (text, name);
}

static void
edit_runtime_info_collect (const char *runtime_name, const char *display_name, guint abi_version,
                           guint64 capability_flags, guint64 required_host_capabilities,
                           gpointer user_data)
{
    GPtrArray *rows = (GPtrArray *) user_data;
    edit_plugin_info_row_t *row = g_new0 (edit_plugin_info_row_t, 1);
    GString *capabilities = g_string_new (NULL);
    static const struct
    {
        guint64 flag;
        const char *name;
    } host_capabilities[] = {
        { MC_RUNTIME_HOST_CAP_EVENTS, "events" }, { MC_RUNTIME_HOST_CAP_CONTEXT_DATA, "context" },
        { MC_RUNTIME_HOST_CAP_UI, "ui" },         { MC_RUNTIME_HOST_CAP_LOG, "log" },
        { MC_RUNTIME_HOST_CAP_EDITOR, "editor" }, { MC_RUNTIME_HOST_CAP_PROCESS, "process" },
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS (host_capabilities); i++)
        if ((required_host_capabilities & host_capabilities[i].flag) != 0)
            edit_plugin_info_append_capability (capabilities, host_capabilities[i].name);

    row->kind = g_strdup ("runtime");
    row->name = g_strdup (display_name);
    row->id = g_strdup (runtime_name);
    row->api = g_strdup_printf ("%u", abi_version);
    row->flags = g_strdup_printf ("0x%" PRIx64, capability_flags);
    row->capabilities = g_string_free (capabilities, FALSE);
    row->configure_command = CK_IgnoreKey;
    g_ptr_array_add (rows, row);
}

static int
edit_plugin_info_get_nrows (const void *data)
{
    const GPtrArray *rows = (const GPtrArray *) data;

    return rows != NULL ? (int) rows->len : 0;
}

static const char *
edit_plugin_info_get_text (const void *data, int row_index, int column)
{
    const GPtrArray *rows = (const GPtrArray *) data;
    const edit_plugin_info_row_t *row;

    if (rows == NULL || row_index < 0 || row_index >= (int) rows->len)
        return "";
    row = (const edit_plugin_info_row_t *) g_ptr_array_index (rows, (guint) row_index);

    switch (column)
    {
    case 1:
        return row->kind;
    case 2:
        return row->name;
    case 3:
        return row->id;
    case 4:
        return row->api;
    case 5:
        return row->flags;
    case 6:
        return row->capabilities;
    default:
        return "";
    }
}

static gboolean
edit_plugin_info_get_checked (const void *data, int row, int column)
{
    (void) data;
    (void) row;
    (void) column;
    return TRUE;
}

static int
edit_plugin_info_header_get_nrows (const void *data)
{
    (void) data;
    return 1;
}

static const char *
edit_plugin_info_header_get_text (const void *data, int row, int column)
{
    static const char *const headings[] = { "On",  "Kind",  "Name",        "ID",
                                            "API", "Flags", "Capabilities" };

    (void) data;
    return row == 0 && column >= 0 && column < (int) G_N_ELEMENTS (headings) ? headings[column]
                                                                             : "";
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_plugins_info (WDialog *h)
{
    const GSList *plugins;
    GPtrArray *rows;
    WDialog *dlg;
    WTable *header;
    WTable *table;
    WEdit *edit = NULL;
    int table_height, dialog_height, dialog_width, table_width, capabilities_width;
    int selected, action;
    table_column_def_t columns[7];
    table_column_def_t header_columns[7];
    table_datasource_t header_datasource = { 0 };
    table_datasource_t datasource = { 0 };

    plugins = mc_editor_plugin_list ();
    rows = g_ptr_array_new_with_free_func ((GDestroyNotify) edit_plugin_info_row_destroy);
    for (; plugins != NULL; plugins = g_slist_next (plugins))
    {
        const mc_editor_plugin_t *p = (const mc_editor_plugin_t *) plugins->data;
        edit_plugin_info_row_t *row = g_new0 (edit_plugin_info_row_t, 1);
        GString *capabilities = g_string_new (NULL);

        if (p->activate != NULL)
            edit_plugin_info_append_capability (capabilities, "activate");
        if (p->configure != NULL)
            edit_plugin_info_append_capability (capabilities, "configure");
        if (p->query_state != NULL)
            edit_plugin_info_append_capability (capabilities, "query");
        if (p->handle_action != NULL)
            edit_plugin_info_append_capability (capabilities, "action");
        if (p->handle_key != NULL)
            edit_plugin_info_append_capability (capabilities, "key");
        if (p->handle_event != NULL)
            edit_plugin_info_append_capability (capabilities, "event");
        if (p->open != NULL)
            edit_plugin_info_append_capability (capabilities, "open");
        if (p->close != NULL)
            edit_plugin_info_append_capability (capabilities, "close");

        row->kind = g_strdup ("editor");
        row->name = g_strdup (p->display_name != NULL ? p->display_name : "-");
        row->id = g_strdup (p->name != NULL ? p->name : "-");
        row->api = g_strdup_printf ("%d", p->api_version);
        row->flags = g_strdup_printf ("0x%x", (unsigned int) p->flags);
        row->capabilities = g_string_free (capabilities, FALSE);
        row->configure_command = MC_EDITOR_PLUGIN_CMD_BASE + (long) rows->len;
        g_ptr_array_add (rows, row);
    }

    mc_runtime_plugins_enumerate_runtimes (edit_runtime_info_collect, rows);
    if (rows->len == 0)
    {
        message (D_NORMAL, _ ("Plugin info"), "%s", _ ("No plugins are loaded."));
        g_ptr_array_free (rows, TRUE);
        return;
    }

    dialog_width = MIN (COLS - 4, 110);
    table_width = dialog_width - 2;
    capabilities_width = MAX (10, table_width - 63);
    columns[0] = (table_column_def_t) { 4, J_CENTER, TABLE_COL_CHECK };
    columns[1] = (table_column_def_t) { 8, J_LEFT, TABLE_COL_TEXT };
    columns[2] = (table_column_def_t) { 18, J_LEFT, TABLE_COL_TEXT };
    columns[3] = (table_column_def_t) { 12, J_LEFT, TABLE_COL_TEXT };
    columns[4] = (table_column_def_t) { 5, J_CENTER, TABLE_COL_TEXT };
    columns[5] = (table_column_def_t) { 8, J_CENTER, TABLE_COL_TEXT };
    columns[6] = (table_column_def_t) { capabilities_width, J_LEFT_FIT, TABLE_COL_TEXT };
    memcpy (header_columns, columns, sizeof (columns));
    header_columns[0].type = TABLE_COL_TEXT;
    table_height = MAX (4, MIN ((int) rows->len, 16));
    dialog_height = table_height + 4;
    dlg = dlg_create (TRUE, (LINES - dialog_height) / 2, (COLS - dialog_width) / 2, dialog_height,
                      dialog_width, WPOS_KEEP_DEFAULT, TRUE, dialog_colors, NULL, NULL,
                      "[Plugin info]", _ ("Plugin info"));
    dlg->help_file = MCEDIT_HELP_FILE;
    header = table_new (1, 1, 1, table_width, 7, header_columns);
    header->scrollbar = FALSE;
    header->scrollbar_on_frame = FALSE;
    widget_set_options (WIDGET (header), WOP_SELECTABLE, FALSE);
    header_datasource.get_nrows = edit_plugin_info_header_get_nrows;
    header_datasource.get_text = edit_plugin_info_header_get_text;
    table_set_datasource (header, header_datasource);
    header->current = -1;
    group_add_widget (GROUP (dlg), header);
    group_add_widget (GROUP (dlg), hline_new (2, -1, -1));
    table = table_new (3, 1, table_height, table_width, 7, columns);
    table->scrollbar = TRUE;
    table->scrollbar_on_frame = FALSE;
    datasource.get_nrows = edit_plugin_info_get_nrows;
    datasource.get_text = edit_plugin_info_get_text;
    datasource.get_checked = edit_plugin_info_get_checked;
    datasource.data = rows;
    table_set_datasource (table, datasource);
    group_add_widget (GROUP (dlg), table);
    widget_select (WIDGET (table));

    action = dlg_run (dlg);
    selected = table_get_current (table);
    widget_destroy (WIDGET (dlg));

    if (action == B_ENTER && selected >= 0 && selected < (int) rows->len)
    {
        const edit_plugin_info_row_t *row =
            (const edit_plugin_info_row_t *) g_ptr_array_index (rows, (guint) selected);

        if (row->configure_command == CK_IgnoreKey)
            message (D_NORMAL, _ ("Plugin info"), "%s",
                     _ ("Selected runtime has no configuration callback."));
        else
        {
            if (h != NULL)
                edit = edit_find_editor (h);
            if (!edit_plugin_configure (h, row->configure_command, edit))
                message (D_NORMAL, _ ("Plugin info"), "%s",
                         _ ("Selected plugin has no configuration callback."));
        }
    }

    g_ptr_array_free (rows, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Show a help window
 */

static void
edit_help (const WDialog *h)
{
    ev_help_t event_data = { h->help_file, h->help_ctx, NULL };

    mc_event_raise (MCEVENT_GROUP_CORE, "help", &event_data);
}

/* --------------------------------------------------------------------------------------------- */

static char *
edit_get_shortcut (long command)
{
    const char *ext_map;
    const char *shortcut = NULL;

    shortcut = keybind_lookup_keymap_shortcut (editor_map, command);
    if (shortcut != NULL)
        return g_strdup (shortcut);

    ext_map = keybind_lookup_keymap_shortcut (editor_map, CK_ExtendedKeyMap);
    if (ext_map != NULL)
        shortcut = keybind_lookup_keymap_shortcut (editor_x_map, command);
    if (shortcut != NULL)
        return g_strdup_printf ("%s %s", ext_map, shortcut);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static char *
edit_get_title (const WDialog *h, const ssize_t width)
{
    const WEdit *edit;
    const char *modified;
    const char *file_label;
    char *filename;

    edit = edit_find_editor (h);
    modified = edit->modified != 0 ? "(*) " : "    ";

    const ssize_t width1 = width - strlen (modified);

    if (edit->filename == NULL)
        filename = g_strdup (_ ("[NoName]"));
    else
        filename = g_strdup (edit->filename);

    file_label = str_term_trim (filename, width1 - str_term_width1 (_ ("Edit: ")));
    g_free (filename);

    return g_strconcat (_ ("Edit: "), modified, file_label, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @edit is the one file window of the screen */
static gboolean
edit_is_last_editor (const WDialog *h, const WEdit *edit)
{
    GList *l;

    for (l = CONST_GROUP (h)->widgets; l != NULL; l = g_list_next (l))
        if (l->data != edit && edit_widget_is_editor (CONST_WIDGET (l->data)))
            return FALSE;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
edit_dialog_command_execute (WDialog *h, long command)
{
    WGroup *g = GROUP (h);
    cb_ret_t ret = MSG_HANDLED;

    if (edit_plugin_handle_action (h, command, NULL))
        return MSG_HANDLED;

    switch (command)
    {
    case CK_EditNew:
        edit_load_file_from_filename (h, NULL);
        break;
    case CK_EditFile:
        edit_load_cmd (h);
        break;
    case CK_History:
        edit_load_file_from_history (h);
        break;
    case CK_EditSyntaxFile:
        edit_load_syntax_file (h);
        break;
    case CK_EditUserMenu:
        edit_load_menu_file (h);
        break;
    case CK_Close:
        if (edit_window_is_window (CONST_WIDGET (g->current->data)))
        {
            WEditWindow *win = EDIT_WINDOW (g->current->data);

            // closing the last file is quitting the editor, the other windows asked as well
            if (edit_widget_is_editor (CONST_WIDGET (win)) && edit_is_last_editor (h, EDIT (win)))
                edit_quit (h);
            else
                (void) win->klass->close (win);
        }
        break;
    case CK_Help:
        edit_help (h);
        break;
    case CK_Menu:
        edit_menu_cmd (h);
        break;
    case CK_Quit:
    case CK_Cancel:
        // don't close editor due to SIGINT, but stop move/resize window
        {
            Widget *w = WIDGET (g->current->data);

            if (edit_window_is_window (w) && EDIT_WINDOW (w)->drag_state != EDIT_WINDOW_DRAG_NONE)
                edit_window_restore_size (EDIT_WINDOW (w));
            else if (command == CK_Quit)
                dlg_close (h);
        }
        break;
    case CK_About:
        edit_about ();
        break;
    case CK_SyntaxOnOff:
        edit_syntax_onoff_cmd (h);
        break;
    case CK_ShowTabTws:
        edit_show_tabs_tws_cmd (h);
        break;
    case CK_ShowMargin:
        edit_show_margin_cmd (h);
        break;
    case CK_ShowControlChars:
        edit_show_control_chars_cmd (h);
        break;
    case CK_ShowNumbers:
        edit_show_numbers_cmd (h);
        break;
    case CK_Refresh:
        edit_refresh_cmd ();
        break;
    case CK_Shell:
        // the terminal plugin shows its window; without it there is nothing to show
        break;
    case CK_LearnKeys:
        key_learn ();
        break;
    case CK_KeyBindings:
        keybind_dialog ();
        break;
    case CK_KeySniffer:
        key_sniffer ();
        break;
    case CK_ManagePlugins:
        manage_plugins_dialog ();
        break;
    case CK_OptionsAppearance:
        appearance_box ();
        break;
    case CK_WindowMove:
    case CK_WindowResize:
        if (edit_window_is_window (CONST_WIDGET (g->current->data)))
            edit_window_handle_move_resize (EDIT_WINDOW (g->current->data), command);
        break;
    case CK_WindowList:
        edit_window_list (h);
        break;
    case CK_WindowNext:
        group_select_next_widget (g);
        break;
    case CK_WindowPrev:
        group_select_prev_widget (g);
        break;
    case CK_Options:
        edit_options_dialog ();
        break;
    case CK_EditPluginsInfo:
        edit_plugins_info (h);
        break;
#ifdef ENABLE_LUA_PLUGIN
    case CK_EditLuaScripts:
        (void) manage_lua_editor_scripts_dialog ();
        break;
#endif
    case CK_OptionsSaveMode:
        edit_save_mode_cmd ();
        break;
    case CK_SaveSetup:
        save_setup_cmd ();
        break;
    default:
        ret = MSG_NOT_HANDLED;
        break;
    }

    return ret;
}

/* --------------------------------------------------------------------------------------------- */
/*
 * Translate the keycode into either 'command' or 'char_for_insertion'.
 * 'command' is one of the editor commands from lib/keybind.h.
 */

static gboolean
edit_translate_key (WEdit *edit, long x_key, int *cmd, int *ch)
{
    Widget *w = WIDGET (edit);
    long command = CK_InsertChar;
    int char_for_insertion = -1;

    // an ordinary insertable character
    if (!w->ext_mode && x_key < 256)
    {
        int c;

        if (edit->charpoint >= MB_LEN_MAX)
        {
            edit->charpoint = 0;
            edit->charbuf[edit->charpoint] = '\0';
        }
        if (edit->charpoint < MB_LEN_MAX)
        {
            edit->charbuf[edit->charpoint++] = x_key;
            edit->charbuf[edit->charpoint] = '\0';
        }

        // input from 8-bit locale
        if (!mc_global.utf8_display)
        {
            // source is in 8-bit codeset
            c = convert_from_input_c (x_key);

            if (is_printable (c))
            {
                if (!edit->utf8)
                    char_for_insertion = c;
                else
                    char_for_insertion = convert_from_8bit_to_utf_c2 ((char) x_key);
                goto fin;
            }
        }
        else
        {
            // UTF-8 locale
            int res;

            res = str_is_valid_char (edit->charbuf, edit->charpoint);
            if (res < 0 && res != -2)
            {
                edit->charpoint = 0;  // broken multibyte char, skip
                goto fin;
            }

            if (edit->utf8)
            {
                // source is in UTF-8 codeset
                if (res < 0)
                {
                    char_for_insertion = x_key;
                    goto fin;
                }

                edit->charbuf[edit->charpoint] = '\0';
                edit->charpoint = 0;
                if (g_unichar_isprint (g_utf8_get_char (edit->charbuf)))
                {
                    char_for_insertion = x_key;
                    goto fin;
                }
            }
            else
            {
                // 8-bit source
                if (res < 0)
                {
                    // not finished multibyte input (we're in the middle of multibyte utf-8 char)
                    goto fin;
                }

                if (g_unichar_isprint (g_utf8_get_char (edit->charbuf)))
                {
                    c = convert_from_utf_to_current (edit->charbuf);
                    edit->charbuf[0] = '\0';
                    edit->charpoint = 0;
                    char_for_insertion = c;
                    goto fin;
                }

                // non-printable utf-8 input, skip it
                edit->charbuf[0] = '\0';
                edit->charpoint = 0;
            }
        }
    }

    // Commands specific to the key emulation
    command = widget_lookup_key (w, x_key);
    if (command == CK_IgnoreKey)
        command = CK_InsertChar;

fin:
    *cmd = (int) command;  // FIXME
    *ch = char_for_insertion;

    return !(command == CK_InsertChar && char_for_insertion == -1);
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_quit (WDialog *h)
{
    GList *l;
    GSList *m = NULL;
    GSList *me;

    // don't stop the dialog before final decision
    widget_set_state (WIDGET (h), WST_ACTIVE, TRUE);

    // check window state and get modified files
    for (l = GROUP (h)->widgets; l != NULL; l = g_list_next (l))
        if (edit_window_is_window (CONST_WIDGET (l->data)))
        {
            WEditWindow *win = EDIT_WINDOW (l->data);

            if (win->drag_state != EDIT_WINDOW_DRAG_NONE)
            {
                edit_window_restore_size (win);
                g_slist_free (m);
                return;
            }

            /* create separate list because widget_select()
               changes the window position in Z order */
            if (win->klass->ok_to_quit != NULL && win->klass->is_modified != NULL
                && win->klass->is_modified (win))
                m = g_slist_prepend (m, l->data);
        }

    for (me = m; me != NULL; me = g_slist_next (me))
    {
        WEditWindow *win = EDIT_WINDOW (me->data);

        // the window is shown to the user who is asked about it, a hidden one too
        edit_window_show (win);

        if (!win->klass->ok_to_quit (win))
            break;
    }

    // if all files were checked, quit editor
    if (me == NULL)
        dlg_close (h);

    g_slist_free (m);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
edit_set_buttonbar (WEdit *edit, WButtonBar *bb)
{
    Widget *w = WIDGET (edit);

    buttonbar_set_label (bb, 1, Q_ ("ButtonBar|Help"), w->keymap, NULL);
    buttonbar_set_label (bb, 2, Q_ ("ButtonBar|Save"), w->keymap, w);
    buttonbar_set_label (bb, 3, Q_ ("ButtonBar|Mark"), w->keymap, w);
    buttonbar_set_label (bb, 4, Q_ ("ButtonBar|Replac"), w->keymap, w);
    buttonbar_set_label (bb, 5, Q_ ("ButtonBar|Copy"), w->keymap, w);
    buttonbar_set_label (bb, 6, Q_ ("ButtonBar|Move"), w->keymap, w);
    buttonbar_set_label (bb, 7, Q_ ("ButtonBar|Search"), w->keymap, w);
    buttonbar_set_label (bb, 8, Q_ ("ButtonBar|Delete"), w->keymap, w);
    buttonbar_set_label (bb, 9, Q_ ("ButtonBar|PullDn"), w->keymap, NULL);
    buttonbar_set_label (bb, 10, Q_ ("ButtonBar|Quit"), w->keymap, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_total_update (WEdit *edit)
{
    edit_find_bracket (edit);
    edit->force |= REDRAW_COMPLETELY;
    edit_update_curs_row (edit);
    edit_update_screen (edit);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Map a screen row to the buffer line shown there, stepping over hidden (folded) lines.
 */
static long
edit_line_at_row (WEdit *edit, int row)
{
    long line = edit->start_line;
    edit_fold_t *f;
    int r;

    /* the top line itself may sit inside a hidden run (a headless one at the file start) */
    f = edit_fold_find (edit, line);
    if (f != NULL && line > f->line_start)
        line = f->line_start + f->line_count + 1;

    for (r = 0; r < row && line < edit->buffer.lines; r++)
    {
        f = edit_fold_find (edit, line);
        if (f != NULL && line == f->line_start)
            line = f->line_start + f->line_count + 1;
        else
            line++;
    }

    return MIN (line, edit->buffer.lines);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_update_cursor (WEdit *edit, const mouse_event_t *event)
{
    int x, y;
    gboolean done;

    x = event->x - (EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1);
    y = event->y - (EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1);

    if (edit->mark2 != -1 && event->msg == MSG_MOUSE_UP)
        return TRUE;  // don't do anything

    if (event->msg == MSG_MOUSE_DOWN || event->msg == MSG_MOUSE_UP)
        edit_push_key_press (edit);

    if (!edit_options.cursor_beyond_eol)
        edit->prev_col = x - edit->start_col - edit_options.line_state_width;
    else
    {
        long line_len;

        line_len = edit_move_forward3 (edit, edit_buffer_get_current_bol (&edit->buffer), 0,
                                       edit_buffer_get_current_eol (&edit->buffer));

        if (x > line_len - 1)
        {
            edit->over_col = x - line_len - edit->start_col - edit_options.line_state_width;
            edit->prev_col = line_len;
        }
        else
        {
            edit->over_col = 0;
            edit->prev_col = x - edit_options.line_state_width - edit->start_col;
        }
    }

    if (edit->folds != NULL)
    {
        /* screen rows and buffer lines differ by the hidden ones: resolve the row first */
        long target = edit_line_at_row (edit, y);

        if (target > edit->buffer.curs_line)
            edit_move_down (edit, target - edit->buffer.curs_line, FALSE);
        else if (target < edit->buffer.curs_line)
            edit_move_up (edit, edit->buffer.curs_line - target, FALSE);
        else
            edit_move_to_prev_col (edit, edit_buffer_get_current_bol (&edit->buffer));
    }
    else if (y > edit->curs_row)
        edit_move_down (edit, y - edit->curs_row, FALSE);
    else if (y < edit->curs_row)
        edit_move_up (edit, edit->curs_row - y, FALSE);
    else
        edit_move_to_prev_col (edit, edit_buffer_get_current_bol (&edit->buffer));

    /* On a fold start line, handle clicks relative to fold indicator */
    if (edit->folds != NULL && !edit->filter_active)
    {
        edit_fold_t *fold;

        fold = edit_fold_find (edit, edit->buffer.curs_line);
        if (fold != NULL && edit->buffer.curs_line == fold->line_start)
        {
            off_t bol, eol, bracket_off;

            bol = edit_buffer_get_current_bol (&edit->buffer);
            eol = edit_buffer_get_current_eol (&edit->buffer);

            /* Find the opening bracket by scanning backward from EOL */
            for (bracket_off = eol - 1; bracket_off >= bol; bracket_off--)
            {
                int ch;

                ch = edit_buffer_get_byte (&edit->buffer, bracket_off);
                if (ch == '{' || ch == '[' || ch == '(')
                    break;
            }

            if (bracket_off >= bol)
            {
                long bracket_col, click_col;

                bracket_col = (long) edit_move_forward3 (edit, bol, 0, bracket_off);
                click_col = x - edit->start_col - edit_options.line_state_width;

                if (click_col >= bracket_col)
                {
                    long line_visual_len, fold_visual_end;

                    /* Calculate visual end of fold indicator */
                    line_visual_len = (long) edit_move_forward3 (edit, bol, 0, eol);
                    fold_visual_end = line_visual_len + edit_fold_indicator_width (edit, fold);

                    /* Snap cursor to the bracket */
                    edit_cursor_move (edit, bracket_off - edit->buffer.curs1);
                    edit->curs_col = bracket_col;
                    edit->prev_col = bracket_col;

                    if (edit_options.cursor_beyond_eol && click_col >= fold_visual_end)
                    {
                        /* Click beyond fold indicator - cursor past fold end */
                        edit->over_col = click_col - bracket_col;
                    }
                    else
                    {
                        /* Click on fold indicator - stay at bracket */
                        edit->over_col = 0;
                    }
                }
            }
        }
    }

    if (event->msg == MSG_MOUSE_CLICK)
    {
        edit_mark_cmd (edit, TRUE);  // reset
        edit->highlight = 0;
    }

    done = (event->msg != MSG_MOUSE_DRAG);
    if (done)
        edit_mark_cmd (edit, FALSE);

    return done;
}

/* --------------------------------------------------------------------------------------------- */
/** Callback for the edit dialog */

static cb_ret_t
edit_dialog_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WGroup *g = GROUP (w);
    WDialog *h = DIALOG (w);

    switch (msg)
    {
    case MSG_INIT:
        edit_dlg_init ();
        return MSG_HANDLED;

    case MSG_RESIZE:
        dlg_default_callback (w, NULL, MSG_RESIZE, 0, NULL);
        menubar_arrange (menubar_find (h));
        return MSG_HANDLED;

    case MSG_ACTION:
    {
        // Handle shortcuts, menu, and buttonbar.

        cb_ret_t result;

        result = edit_dialog_command_execute (h, parm);

        /* We forward any commands coming from the menu, and which haven't been
           handled by the dialog, to the focused WEdit window. */
        if (result == MSG_NOT_HANDLED && sender == WIDGET (menubar_find (h)))
            result = send_message (g->current->data, NULL, MSG_ACTION, parm, NULL);

        return result;
    }

    case MSG_KEY:
    {
        Widget *we = WIDGET (g->current->data);
        cb_ret_t ret = MSG_NOT_HANDLED;

        if (edit_widget_is_editor (we))
        {
            gboolean ext_mode;
            long command;

            if (edit_publish_runtime_key (EDIT (we), parm))
                return MSG_HANDLED;

            // keep and then extmod flag
            ext_mode = we->ext_mode;
            command = widget_lookup_key (we, parm);
            we->ext_mode = ext_mode;

            if (command == CK_IgnoreKey)
                we->ext_mode = FALSE;
            else
            {
                ret = edit_dialog_command_execute (h, command);
                /* if command was not handled, keep the extended mode
                   for the further key processing */
                if (ret == MSG_HANDLED)
                    we->ext_mode = FALSE;
            }
        }

        /*
         * Due to the "end of bracket" escape the editor sees input with is_idle() == false
         * (expects more characters) and hence doesn't yet refresh the screen, but then
         * no further characters arrive (there's only an "end of bracket" which is swallowed
         * by tty_get_event()), so you end up with a screen that's not refreshed after pasting.
         * So let's trigger an IDLE signal.
         */
        if (!is_idle ())
            widget_idle (w, TRUE);
        return ret;
    }

        // hardcoded menu hotkeys (see edit_drop_hotkey_menu)
    case MSG_UNHANDLED_KEY:
        return edit_drop_hotkey_menu (h, parm) ? MSG_HANDLED : MSG_NOT_HANDLED;

    case MSG_VALIDATE:
        edit_quit (h);
        return MSG_HANDLED;

    case MSG_DESTROY:
        editor_plugin_ctx_destroy (h);
        edit_dlg_deinit ();
        return MSG_HANDLED;

    case MSG_IDLE:
        widget_idle (w, FALSE);
        return send_message (g->current->data, NULL, MSG_IDLE, 0, NULL);

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Handle mouse events of editor screen.
 *
 * @param w Widget object (the editor)
 * @param msg mouse event message
 * @param event mouse event data
 */
static void
edit_dialog_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    gboolean unhandled = TRUE;

    if (msg == MSG_MOUSE_DOWN && event->y == 0)
    {
        WGroup *g = GROUP (w);
        WDialog *h = DIALOG (w);
        WMenuBar *b;

        b = menubar_find (h);

        if (!widget_get_state (WIDGET (b), WST_FOCUSED))
        {
            // menubar

            GList *l;
            GList *top = NULL;
            int x;

            // Try find top fullscreen window
            for (l = g->widgets; l != NULL; l = g_list_next (l))
                if (edit_window_is_window (CONST_WIDGET (l->data))
                    && widget_get_state (CONST_WIDGET (l->data), WST_VISIBLE)
                    && EDIT_WINDOW (l->data)->fullscreen != 0)
                    top = l;

            // Handle fullscreen/close buttons in the top line
            x = w->rect.cols - 6;

            if (top != NULL && event->x >= x)
            {
                WEditWindow *win = EDIT_WINDOW (top->data);

                if (top != g->current)
                {
                    // Window is not active. Activate it
                    widget_select (WIDGET (win));
                }

                // Handle buttons
                if (event->x - x <= 2)
                    edit_window_toggle_fullscreen (win);
                else
                    send_message (h, NULL, MSG_ACTION, CK_Close, NULL);

                unhandled = FALSE;
            }

            if (unhandled)
                menubar_activate (b, drop_menus, -1);
        }
    }

    // Continue handling of unhandled event in window or menu
    event->result.abort = unhandled;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
edit_dialog_bg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
        w->rect = WIDGET (w->owner)->rect;
        rect_grow (&w->rect, -1, 0);
        w->pos_flags |= WPOS_KEEP_ALL;
        return MSG_HANDLED;

    default:
        return background_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
edit_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WEdit *e = EDIT (w);

    switch (msg)
    {
    case MSG_FOCUS:
        edit_set_buttonbar (e, buttonbar_find (DIALOG (w->owner)));
        return MSG_HANDLED;

    case MSG_DRAW:
        e->force |= REDRAW_COMPLETELY;
        edit_update_screen (e);
        return MSG_HANDLED;

    case MSG_KEY:
    {
        int cmd, ch;
        cb_ret_t ret = MSG_NOT_HANDLED;

        // The user may override the access-keys for the menu bar.
        if (macro_index == -1 && !bracketed_pasting_in_progress && edit_execute_macro (e, parm))
        {
            edit_update_screen (e);
            ret = MSG_HANDLED;
        }
        else if (edit_plugin_handle_key (DIALOG (w->owner), parm, e))
        {
            edit_update_screen (e);
            ret = MSG_HANDLED;
        }
        else if (edit_translate_key (e, parm, &cmd, &ch))
        {
            edit_execute_key_command (e, cmd, ch);
            edit_update_screen (e);
            ret = MSG_HANDLED;
        }

        return ret;
    }

    case MSG_ACTION:
        // command from menubar or buttonbar
        edit_execute_key_command (e, parm, -1);
        edit_update_screen (e);
        return MSG_HANDLED;

    case MSG_CURSOR:
    {
        int y, x;

        y = (EDIT_WINDOW (e)->fullscreen != 0 ? 0 : 1) + EDIT_TEXT_VERTICAL_OFFSET + e->curs_row;
        x = (EDIT_WINDOW (e)->fullscreen != 0 ? 0 : 1) + EDIT_TEXT_HORIZONTAL_OFFSET
            + edit_options.line_state_width + e->curs_col + e->start_col + e->over_col;

        widget_gotoyx (w, y, x);
        return MSG_HANDLED;
    }

    case MSG_IDLE:
        edit_update_screen (e);
        return MSG_HANDLED;

    case MSG_DESTROY:
        edit_clean (e);
        return MSG_HANDLED;

    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Handle mouse events of editor window that its frame does not take
 *
 * @param w Widget object (the editor window)
 * @param msg mouse event message
 * @param event mouse event data
 */
static void
edit_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    WEdit *edit = EDIT (w);

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        edit_update_curs_row (edit);
        edit_update_curs_col (edit);

        if (event->count == GPM_DOUBLE)
            edit->word_highlight = TRUE;
        else if (event->count == GPM_TRIPLE)
            edit->line_highlight = TRUE;

        // click in the line-state gutter area - toggle fold
        if (edit_options.line_state)
        {
            int gutter_x;

            gutter_x = event->x - (EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1);
            if (gutter_x >= 0 && gutter_x < edit_options.line_state_width)
            {
                int click_y;
                long line;
                edit_fold_t *fold;

                // map screen row to file line and move the cursor there
                click_y = event->y - (EDIT_WINDOW (edit)->fullscreen != 0 ? 0 : 1);
                line = edit_line_at_row (edit, click_y);
                edit_cursor_move (edit,
                                  edit_buffer_get_forward_offset (&edit->buffer,
                                                                  edit->start_display,
                                                                  line - edit->start_line, 0)
                                      - edit->buffer.curs1);

                fold = edit_fold_find (edit, line);

                if (fold != NULL && line == fold->line_start)
                {
                    // existing fold - unfold
                    edit_fold_remove (edit, fold->line_start);
                }
                else
                {
                    // try to create fold: find { on this line, match }
                    off_t bol, eol, pos;

                    bol = edit_buffer_get_current_bol (&edit->buffer);
                    eol = edit_buffer_get_current_eol (&edit->buffer);

                    for (pos = bol; pos < eol; pos++)
                    {
                        if (strchr ("{[(", edit_buffer_get_byte (&edit->buffer, pos)) != NULL)
                        {
                            off_t match;

                            edit_cursor_move (edit, pos - edit->buffer.curs1);
                            match = edit_get_bracket (edit, 0, 0);
                            if (match >= 0)
                            {
                                long line2;

                                line2 = edit_buffer_count_lines (&edit->buffer, 0, match);
                                if (line2 > line)
                                    edit_fold_make (edit, line, line2 - line);
                            }
                            break;
                        }
                    }
                }

                edit->force |= REDRAW_PAGE;
                edit_total_update (edit);
                break;
            }
        }

        edit_update_cursor (edit, event);
        edit_total_update (edit);
        break;

    case MSG_MOUSE_UP:
        edit_update_cursor (edit, event);
        edit_total_update (edit);
        edit->word_highlight = FALSE;
        edit->line_highlight = FALSE;
        break;

    case MSG_MOUSE_DRAG:
        edit_update_cursor (edit, event);
        edit_total_update (edit);
        /* edit_update_screen() coalesces redraws while more input is pending.  A stream of
         * mouse-motion reports therefore used to leave the selection invisible until the
         * button was released.  Render and flush the current drag position immediately. */
        if (!is_idle ())
            edit_render_keypress (edit);
        tty_refresh ();
        break;

    case MSG_MOUSE_SCROLL_UP:
        edit_move_up (edit, 2, TRUE);
        edit_total_update (edit);
        break;

    case MSG_MOUSE_SCROLL_DOWN:
        edit_move_down (edit, 2, TRUE);
        edit_total_update (edit);
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
edit_class_get_title (const WEditWindow *win)
{
    const WEdit *e = CONST_EDIT (win);

    if (e->filename == NULL)
        return g_strdup_printf (" [%s]", _ ("NoName"));
    return g_strdup (e->filename);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_class_is_modified (const WEditWindow *win)
{
    return CONST_EDIT (win)->modified != 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_class_close (WEditWindow *win)
{
    return edit_close_cmd (EDIT (win));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_class_ok_to_quit (WEditWindow *win)
{
    return edit_ok_to_quit (EDIT (win));
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/**
 * Edit one file.
 *
 * @param arg the file and the line number
 * @return TRUE if no errors was occurred, FALSE otherwise
 */

gboolean
edit_file (const edit_arg_t *arg)
{
    GList *files;
    gboolean ok;

    files = g_list_prepend (NULL, (edit_arg_t *) arg);
    ok = edit_files (files);
    g_list_free (files);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Edit a file in a screen of its own, on top of the current one.
 *
 * @param file_name file name
 * @param start_line line number
 */

void
edit_file_at_line (const char *file_name, long start_line)
{
    char *path = mc_path_absolute (file_name);
    const edit_arg_t arg = { path, start_line, 0, -1 };

    (void) edit_file (&arg);
    g_free (path);
    dialog_switch_process_pending ();
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_files (const GList *files)
{
    WDialog *edit_dlg;
    WGroup *g;
    WMenuBar *menubar;
    Widget *w, *wd;
    const GList *file;
    gboolean ok = FALSE;

    // Create a new dialog and add it widgets to it
    edit_dlg = dlg_create (FALSE, 0, 0, 1, 1, WPOS_FULLSCREEN, FALSE, NULL, edit_dialog_callback,
                           edit_dialog_mouse_callback, "[Internal File Editor]", NULL);
    edit_dlg->help_file = MCEDIT_HELP_FILE;
    wd = WIDGET (edit_dlg);
    widget_want_tab (wd, TRUE);
    wd->keymap = editor_map;
    wd->ext_keymap = editor_x_map;

    edit_dlg->get_shortcut = edit_get_shortcut;
    edit_dlg->get_title = edit_get_title;

    edit_register_builtin_plugins ();
    mc_editor_plugins_load ();

    g = GROUP (edit_dlg);

    edit_dlg->bg = WIDGET (background_new (1, 0, wd->rect.lines - 2, wd->rect.cols,
                                           EDITOR_BACKGROUND_COLOR, ' ', edit_dialog_bg_callback));
    group_add_widget (g, edit_dlg->bg);

    menubar = menubar_new (NULL);
    w = WIDGET (menubar);
    group_add_widget_autopos (g, w, w->pos_flags, NULL);
    edit_init_menu (menubar);

    w = WIDGET (buttonbar_new ());
    group_add_widget_autopos (g, w, w->pos_flags, NULL);

    edit_dlg->data.p = editor_plugin_ctx_create (edit_dlg);

    for (file = files; file != NULL; file = g_list_next (file))
    {
        gboolean f_ok;

        f_ok = edit_load_file_from_filename (edit_dlg, (const edit_arg_t *) file->data);
        // at least one file has been opened succefully
        ok = ok || f_ok;
    }

    if (ok)
    {
        events_publish_runtime_startup ();
        dlg_run (edit_dlg);
    }

    if (!ok || widget_get_state (wd, WST_CLOSED))
        widget_destroy (wd);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

WEdit *
edit_find_editor (const WDialog *h)
{
    const WGroup *g = CONST_GROUP (h);
    GList *l;

    if (edit_widget_is_editor (CONST_WIDGET (g->current->data)))
        return EDIT (g->current->data);

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_widget_is_editor (CONST_WIDGET (l->data)))
            return EDIT (l->data);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Check if widget is an WEdit class.
 *
 * @param w probably editor object
 * @return TRUE if widget is an WEdit class, FALSE otherwise
 */

gboolean
edit_widget_is_editor (const Widget *w)
{
    return (edit_window_is_window (w) && CONST_EDIT_WINDOW (w)->klass == &edit_class);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_update_screen (WEdit *e)
{
    edit_scroll_screen_over_cursor (e);
    edit_update_curs_col (e);
    edit_status (e, widget_get_state (WIDGET (e), WST_FOCUSED));

    // pop all events for this window for internal handling
    if (!is_idle ())
        e->force |= REDRAW_PAGE;
    else
    {
        if ((e->force & REDRAW_COMPLETELY) != 0)
            e->force |= REDRAW_PAGE;
        edit_render_keypress (e);
    }

    widget_draw (WIDGET (buttonbar_find (DIALOG (WIDGET (e)->owner))));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Create new editor window and insert it into editor screen.
 *
 * @param h     editor dialog (screen)
 * @param y     y coordinate
 * @param x     x coordinate
 * @param lines window height
 * @param cols  window width
 * @param f     file object
 * @param fline line number in file
 * @return TRUE if new window was successfully created and inserted into editor screen,
 *         FALSE otherwise
 */

gboolean
edit_add_window (WDialog *h, const WRect *r, const edit_arg_t *arg)
{
    WEdit *edit;

    edit = edit_init (NULL, r, arg);
    if (edit == NULL)
        return FALSE;

    edit_window_add (h, EDIT_WINDOW (edit));
    edit_set_buttonbar (edit, buttonbar_find (h));
    edit_publish_runtime_open (edit);
    widget_draw (WIDGET (h));

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
