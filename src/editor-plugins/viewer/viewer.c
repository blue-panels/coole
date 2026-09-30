/*
   The viewer plugin of the editor: windows that show the text they are given.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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
 *  \brief Source: the viewer plugin of the editor
 *
 *  A module the editor loads from its directory of plugins. It offers the service "viewer":
 *  whoever has text to show, a script or another plugin, opens a window with it, gives it new
 *  text, scrolls it, hides it and closes it. The viewer knows nothing of where the text comes
 *  from.
 *
 *  Methods, with their arguments and answers, a{sv}:
 *
 *    open       title, text, place ("right", "bottom", "full"), size (percent), focus -> id
 *    set_text   id, text
 *    set_title  id, title
 *    scroll_to  id, line
 *    show       id, focus
 *    hide       id
 *    close      id
 *    info       id -> cols, lines, top, total, visible
 *
 *  Signal "closed", id: the user closed the window.
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/editor-plugin.h"
#include "lib/plugin-service.h"

#include "src/editor/editwindow.h"

#include "textwindow.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define VIEWER_SERVICE "viewer"

/* How much of the screen a window takes by default, in percent */
#define VIEWER_SIZE_RIGHT  50
#define VIEWER_SIZE_BOTTOM 25

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_editor_host_t *host;
    GHashTable *windows;  // id -> WEditWindow
    gint64 last_id;
} viewer_t;

/* What a window tells the viewer when it is destroyed */
typedef struct
{
    viewer_t *viewer;
    gint64 id;
} viewer_window_t;

/*** forward declarations (file scope functions) *************************************************/

const mc_editor_plugin_t *mc_editor_plugin_register (void);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static GVariant *
viewer_answer_empty (void)
{
    return g_variant_new_array (G_VARIANT_TYPE ("{sv}"), NULL, 0);
}

/* --------------------------------------------------------------------------------------------- */

/* A string argument, text or bytes: text a script gives need not be UTF-8. Caller frees */
static char *
viewer_arg_text (GVariant *args, const char *key, gsize *len)
{
    GVariant *v;
    char *text = NULL;

    v = g_variant_lookup_value (args, key, NULL);
    if (v == NULL)
        return NULL;

    if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
    {
        text = g_variant_dup_string (v, len);
    }
    else if (g_variant_is_of_type (v, G_VARIANT_TYPE_BYTESTRING))
    {
        gsize n;
        const char *bytes;

        bytes = g_variant_get_fixed_array (v, &n, 1);
        // a bytestring may carry its terminating zero
        if (n != 0 && bytes[n - 1] == '\0')
            n--;
        text = g_strndup (bytes, n);
        *len = n;
    }

    g_variant_unref (v);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
viewer_arg_int (GVariant *args, const char *key, gint64 *value)
{
    GVariant *v;
    gboolean ok = TRUE;

    v = g_variant_lookup_value (args, key, NULL);
    if (v == NULL)
        return FALSE;

    if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT64))
        *value = g_variant_get_int64 (v);
    else if (g_variant_is_of_type (v, G_VARIANT_TYPE_INT32))
        *value = g_variant_get_int32 (v);
    else if (g_variant_is_of_type (v, G_VARIANT_TYPE_DOUBLE))
    {
        const double d = g_variant_get_double (v);

        *value = (gint64) d;
    }
    else
        ok = FALSE;

    g_variant_unref (v);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
viewer_arg_bool (GVariant *args, const char *key, gboolean def)
{
    gboolean value;

    if (!g_variant_lookup (args, key, "b", &value))
        return def;
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/* The window the id argument names */
static WEditWindow *
viewer_window (viewer_t *v, GVariant *args, gint64 *id, GError **error)
{
    WEditWindow *win;
    gint64 n = 0;

    if (!viewer_arg_int (args, "id", &n))
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "no window id");
        return NULL;
    }

    win = (WEditWindow *) g_hash_table_lookup (v->windows, &n);
    if (win == NULL)
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "no window %" G_GINT64_FORMAT,
                     n);
        return NULL;
    }

    if (id != NULL)
        *id = n;
    return win;
}

/* --------------------------------------------------------------------------------------------- */

/* A window is destroyed, by the user or with the editor: it is forgotten, and those who listen
   are told */
static void
viewer_window_destroyed (void *data)
{
    viewer_window_t *vw = (viewer_window_t *) data;
    GVariantDict dict;

    g_hash_table_remove (vw->viewer->windows, &vw->id);

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", vw->id);
    vw->viewer->host->service_emit (vw->viewer->host, VIEWER_SERVICE, "closed",
                                    g_variant_dict_end (&dict));
    g_free (vw);
}

/* --------------------------------------------------------------------------------------------- */

/* Show @win, and give the focus back to @prev unless @focus */
static void
viewer_show (viewer_t *v, WEditWindow *win, gboolean focus, void *prev)
{
    v->host->window_show (v->host, win);
    v->host->window_make_room (v->host, win);
    if (!focus && prev != NULL && prev != win)
        v->host->window_show (v->host, prev);
}

/* --------------------------------------------------------------------------------------------- */

static GVariant *
viewer_open (viewer_t *v, GVariant *args, GError **error)
{
    WEditWindow *win;
    viewer_window_t *vw;
    GVariantDict dict;
    WRect a, r;
    char *title, *text, *place;
    gsize len = 0;
    gint64 size = 0;
    gboolean focus;
    gboolean full = FALSE;
    gint64 *key;
    void *prev;

    place = NULL;
    (void) g_variant_lookup (args, "place", "s", &place);
    focus = viewer_arg_bool (args, "focus", TRUE);

    v->host->window_area (v->host, &a);
    r = a;

    if (place == NULL || strcmp (place, "right") == 0)
    {
        if (!viewer_arg_int (args, "size", &size))
            size = VIEWER_SIZE_RIGHT;
        r.cols = (int) CLAMP (a.cols * size / 100, 10, a.cols);
        r.x = a.x + a.cols - r.cols;
    }
    else if (strcmp (place, "bottom") == 0)
    {
        if (!viewer_arg_int (args, "size", &size))
            size = VIEWER_SIZE_BOTTOM;
        r.lines = (int) CLAMP (a.lines * size / 100, 3, a.lines);
        r.y = a.y + a.lines - r.lines;
    }
    else if (strcmp (place, "full") == 0)
        full = TRUE;
    else
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "no place %s", place);
        g_free (place);
        return NULL;
    }
    g_free (place);

    title = viewer_arg_text (args, "title", &len);
    win = edit_text_window_new (&r, title != NULL ? title : "");
    g_free (title);
    if (full)
        win->fullscreen = 1;

    text = viewer_arg_text (args, "text", &len);
    edit_text_window_set_text (win, text, text != NULL ? len : 0);
    g_free (text);

    vw = g_new (viewer_window_t, 1);
    vw->viewer = v;
    vw->id = ++v->last_id;
    key = g_new (gint64, 1);
    *key = vw->id;
    g_hash_table_insert (v->windows, key, win);
    edit_text_window_on_destroy (win, viewer_window_destroyed, vw);

    prev = v->host->window_current (v->host);
    v->host->window_add (v->host, win);
    if (!full)
        v->host->window_make_room (v->host, win);
    if (!focus && prev != NULL)
        v->host->window_show (v->host, prev);

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", vw->id);
    return g_variant_dict_end (&dict);
}

/* --------------------------------------------------------------------------------------------- */

static GVariant *
viewer_call (void *data, const char *method, GVariant *args, GError **error)
{
    viewer_t *v = (viewer_t *) data;
    WEditWindow *win;
    gint64 id;

    if (strcmp (method, "open") == 0)
        return viewer_open (v, args, error);

    win = viewer_window (v, args, &id, error);
    if (win == NULL)
        return NULL;

    if (strcmp (method, "set_text") == 0)
    {
        gsize len = 0;
        char *text;

        text = viewer_arg_text (args, "text", &len);
        edit_text_window_set_text (win, text, text != NULL ? len : 0);
        g_free (text);
    }
    else if (strcmp (method, "set_title") == 0)
    {
        gsize len = 0;
        char *title;

        title = viewer_arg_text (args, "title", &len);
        edit_text_window_set_title (win, title != NULL ? title : "");
        g_free (title);
    }
    else if (strcmp (method, "scroll_to") == 0)
    {
        gint64 line = 0;

        if (!viewer_arg_int (args, "line", &line))
        {
            g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "no line");
            return NULL;
        }
        edit_text_window_scroll_to (win, (long) line);
    }
    else if (strcmp (method, "show") == 0)
    {
        void *prev = v->host->window_current (v->host);

        viewer_show (v, win, viewer_arg_bool (args, "focus", TRUE), prev);
    }
    else if (strcmp (method, "hide") == 0)
    {
        // the window it made room for takes the screen again
        v->host->window_give_room_back (v->host, win);
        v->host->window_hide (v->host, win);
    }
    else if (strcmp (method, "close") == 0)
    {
        v->host->window_give_room_back (v->host, win);
        edit_window_destroy (win);
    }
    else if (strcmp (method, "info") == 0)
    {
        GVariantDict dict;

        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "cols", "x", (gint64) edit_text_window_text_cols (win));
        g_variant_dict_insert (&dict, "lines", "x", (gint64) edit_text_window_text_lines (win));
        g_variant_dict_insert (&dict, "top", "x", (gint64) edit_text_window_top (win));
        g_variant_dict_insert (&dict, "total", "x", (gint64) edit_text_window_lines (win));
        g_variant_dict_insert (&dict, "visible", "b",
                               widget_get_state (CONST_WIDGET (win), WST_VISIBLE));
        return g_variant_dict_end (&dict);
    }
    else
    {
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD, "the viewer has no %s",
                     method);
        return NULL;
    }

    return viewer_answer_empty ();
}

/* --------------------------------------------------------------------------------------------- */

static void *
viewer_plugin_open (mc_editor_host_t *host, void *editor_dialog)
{
    viewer_t *v;
    GError *error = NULL;

    (void) editor_dialog;

    if (host->window_add == NULL || host->service_register == NULL)
        return NULL;

    v = g_new0 (viewer_t, 1);
    v->host = host;
    v->windows = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);

    if (!host->service_register (host, VIEWER_SERVICE, viewer_call, v, &error))
    {
        fprintf (stderr, "viewer: %s\n", error->message);
        g_error_free (error);
        g_hash_table_destroy (v->windows);
        g_free (v);
        return NULL;
    }

    return v;
}

/* --------------------------------------------------------------------------------------------- */

static void
viewer_plugin_close (void *plugin_data)
{
    viewer_t *v = (viewer_t *) plugin_data;

    // the editor has destroyed the windows before
    v->host->service_unregister (v->host, VIEWER_SERVICE);
    g_hash_table_destroy (v->windows);
    g_free (v);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t viewer_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "viewer",
    .display_name = "Viewer",
    .flags = MC_EPF_NONE,
    .open = viewer_plugin_open,
    .close = viewer_plugin_close,
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The entry of the module, which the editor calls when it loads it */
const mc_editor_plugin_t *
mc_editor_plugin_register (void)
{
    return &viewer_plugin;
}

/* --------------------------------------------------------------------------------------------- */
