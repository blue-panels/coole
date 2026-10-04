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
 *    info       id -> cols, lines, top, total, visible, focused, window
 *    add_type   type, suffixes (strings), starts (strings)
 *
 *  Signal "closed", id: the user closed the window.
 *
 *  Preview: Ctrl-Alt-P, or Window > Preview, shows a window "Preview" at the right of the file,
 *  whatever the file is, and follows it: its text as it changes, its cursor, the file window that
 *  comes to the front.  The renderers name the types they know when the viewer asks with the
 *  signal "types": add_type, the type, the ends of the names of its files (".xml") and the
 *  starts of their texts ("<?xml"); it asks when the Preview is shown and when another file comes
 *  into it, and forgets the types named before.  A file is of the type of the longest end of its
 *  name named, else of how its text starts, else "text".  The viewer asks for the view of it
 *  with the signal "render" (id, type, path, text, width, revision): a renderer that knows
 *  the type answers with set_text for that id.  The signal "follow" (id, type, line of the file,
 *  from 1) asks where the cursor is in the view: the renderer answers with scroll_to.  A type
 *  nobody renders is shown as the text of the file, line for line.
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/editor-plugin.h"
#include "lib/plugin-prefs.h"  // mc_plugin_prefs_load_hotkey()
#include "lib/plugin-service.h"

#include "src/editor/editwindow.h"

#include "textwindow.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define VIEWER_SERVICE "viewer"

/* How much of the screen a window takes by default, in percent */
#define VIEWER_SIZE_RIGHT  50
#define VIEWER_SIZE_BOTTOM 25

#define PREVIEW_TITLE      "Preview"

// where a window stands, to stand there again when it is shown
#define VIEWER_DOCK_NONE   0
#define VIEWER_DOCK_RIGHT  1
#define VIEWER_DOCK_BOTTOM 2
/* The key of the preview: [Preview] key= in viewer.ini */
#define PREVIEW_CONFIG "viewer.ini"
#define PREVIEW_KEY    "ctrl-alt-p"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_editor_host_t *host;
    GHashTable *windows;  // id -> WEditWindow
    GHashTable *docks;    // WEditWindow -> its dock, VIEWER_DOCK_*
    gint64 last_id;

    // the preview
    int key;
    gint64 preview_id;  // its window, 0 while there is none
    char *path;         // what it shows: the file, its type, the revision and width rendered
    char *type;
    guint64 revision;
    int width;
    gint64 asking;      // the window a signal asks a renderer about now
    gboolean answered;  // and whether a renderer answered
    const void *edit;   // the file window it follows, only to see that another comes

    // the types the renderers name: by the end of the name, by the start of the text
    GPtrArray *suffixes;  // preview_kind_t
    GPtrArray *starts;    // preview_kind_t
} viewer_t;

/* A type a renderer named, and how a file of it is known */
typedef struct
{
    char *type;
    char *match;  // the end of the name, or the start of the text
} preview_kind_t;

/* What a window tells the viewer when it is destroyed */
typedef struct
{
    viewer_t *viewer;
    gint64 id;
} viewer_window_t;

/*** forward declarations (file scope functions) *************************************************/

const mc_editor_plugin_t *mc_editor_plugin_register (void);
static GVariant *preview_add_type (viewer_t *v, GVariant *args, GError **error);

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

/* A list of strings: "as" from a plugin, "av" of strings from a script. NULL without it.
   Caller frees */
static char **
viewer_arg_strings (GVariant *args, const char *key)
{
    GVariant *v;
    GPtrArray *list;
    GVariantIter iter;
    GVariant *item;

    v = g_variant_lookup_value (args, key, NULL);
    if (v == NULL)
        return NULL;

    list = g_ptr_array_new ();
    if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING))
        g_ptr_array_add (list, g_variant_dup_string (v, NULL));
    else if (g_variant_is_of_type (v, G_VARIANT_TYPE_STRING_ARRAY)
             || g_variant_is_of_type (v, G_VARIANT_TYPE ("av")))
    {
        g_variant_iter_init (&iter, v);
        while ((item = g_variant_iter_next_value (&iter)) != NULL)
        {
            GVariant *s = item;

            if (g_variant_is_of_type (item, G_VARIANT_TYPE_VARIANT))
                s = g_variant_get_variant (item);
            if (g_variant_is_of_type (s, G_VARIANT_TYPE_STRING))
                g_ptr_array_add (list, g_variant_dup_string (s, NULL));
            if (s != item)
                g_variant_unref (s);
            g_variant_unref (item);
        }
    }
    g_variant_unref (v);

    g_ptr_array_add (list, NULL);
    return (char **) g_ptr_array_free (list, FALSE);
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

    g_hash_table_remove (vw->viewer->docks, g_hash_table_lookup (vw->viewer->windows, &vw->id));
    g_hash_table_remove (vw->viewer->windows, &vw->id);
    if (vw->id == vw->viewer->preview_id)
    {
        vw->viewer->preview_id = 0;
        g_clear_pointer (&vw->viewer->path, g_free);
    }

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", vw->id);
    vw->viewer->host->service_emit (vw->viewer->host, VIEWER_SERVICE, "closed",
                                    g_variant_dict_end (&dict));
    g_free (vw);
}

/* --------------------------------------------------------------------------------------------- */

/* Show @win, and give the focus back to @prev unless @focus */
/* A window, added already, where it stands: in a dock, or with room made for it */
static void
viewer_place (viewer_t *v, WEditWindow *win, int dock)
{
    const WRect *r = &CONST_WIDGET (win)->rect;

    if (dock == VIEWER_DOCK_RIGHT && v->host->window_dock_right != NULL)
        v->host->window_dock_right (v->host, win, r->cols);
    else if (dock == VIEWER_DOCK_BOTTOM && v->host->window_dock_bottom != NULL)
        v->host->window_dock_bottom (v->host, win, r->lines);
    else if (win->fullscreen == 0)
        v->host->window_make_room (v->host, win);
}

/* --------------------------------------------------------------------------------------------- */

static void
viewer_show (viewer_t *v, WEditWindow *win, gboolean focus, void *prev)
{
    // a window on the screen has its room already; a hidden one goes back to its dock
    const gboolean hidden = !widget_get_state (CONST_WIDGET (win), WST_VISIBLE);
    const gpointer stands = g_hash_table_lookup (v->docks, win);
    const int dock = GPOINTER_TO_INT (stands);

    if (hidden)
        viewer_place (v, win, dock);
    v->host->window_show (v->host, win);
    if (!focus && prev != NULL && prev != win)
        v->host->window_show (v->host, prev);
}

/* --------------------------------------------------------------------------------------------- */

/* A window at @r, or fullscreen, put on the screen; the fullscreen window makes room for it, and
   the window with the focus keeps it unless @focus */
static WEditWindow *
viewer_window_new (viewer_t *v, const WRect *r, int dock, gboolean full, const char *title,
                   const char *text, gsize len, gboolean focus, gint64 *id)
{
    WEditWindow *win;
    viewer_window_t *vw;
    gint64 *key;
    void *prev;

    win = edit_text_window_new (r, title != NULL ? title : "");
    if (full)
        win->fullscreen = 1;
    edit_text_window_set_text (win, text, text != NULL ? len : 0);

    vw = g_new (viewer_window_t, 1);
    vw->viewer = v;
    vw->id = ++v->last_id;
    key = g_new (gint64, 1);
    *key = vw->id;
    g_hash_table_insert (v->windows, key, win);
    edit_text_window_on_destroy (win, viewer_window_destroyed, vw);

    prev = v->host->window_current (v->host);
    v->host->window_add (v->host, win);
    g_hash_table_insert (v->docks, win, GINT_TO_POINTER (dock));
    viewer_place (v, win, dock);
    if (!focus && prev != NULL)
        v->host->window_show (v->host, prev);

    *id = vw->id;
    return win;
}

/* --------------------------------------------------------------------------------------------- */

static GVariant *
viewer_open (viewer_t *v, GVariant *args, GError **error)
{
    WEditWindow *win;
    GVariantDict dict;
    WRect a, r;
    char *title, *text, *place;
    gsize len = 0;
    gint64 size = 0;
    gboolean focus;
    gboolean full = FALSE;
    int dock = VIEWER_DOCK_NONE;
    gint64 id;

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
        dock = VIEWER_DOCK_RIGHT;
    }
    else if (strcmp (place, "bottom") == 0)
    {
        if (!viewer_arg_int (args, "size", &size))
            size = VIEWER_SIZE_BOTTOM;
        r.lines = (int) CLAMP (a.lines * size / 100, 3, a.lines);
        r.y = a.y + a.lines - r.lines;
        dock = VIEWER_DOCK_BOTTOM;
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
    text = viewer_arg_text (args, "text", &len);
    win = viewer_window_new (v, &r, dock, full, title, text, len, focus, &id);
    g_free (title);
    g_free (text);
    (void) win;

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", id);
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
    if (strcmp (method, "add_type") == 0)
        return preview_add_type (v, args, error);

    win = viewer_window (v, args, &id, error);
    if (win == NULL)
        return NULL;

    // a renderer answers a signal of the preview
    if (id == v->asking && (strcmp (method, "set_text") == 0 || strcmp (method, "scroll_to") == 0))
        v->answered = TRUE;

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
        // a tab of the bottom not seen is there all the same
        g_variant_dict_insert (
            &dict, "visible", "b",
            widget_get_state (CONST_WIDGET (win), WST_VISIBLE)
                || (v->host->window_docked != NULL && v->host->window_docked (v->host, win)));
        g_variant_dict_insert (&dict, "focused", "b",
                               v->host->window_current (v->host) == (void *) win);
        // the window itself, for the layouts of the editor in the same program
        g_variant_dict_insert (&dict, "window", "t", (guint64) (gsize) win);
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
/*** the preview *******************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
preview_kind_free (gpointer data)
{
    preview_kind_t *k = (preview_kind_t *) data;

    g_free (k->type);
    g_free (k->match);
    g_free (k);
}

/* --------------------------------------------------------------------------------------------- */

/* The renderers are asked to name their types again; the ones they named before are forgotten,
   so that a renderer that is gone leaves none behind */
static void
preview_ask_types (viewer_t *v)
{
    g_ptr_array_set_size (v->suffixes, 0);
    g_ptr_array_set_size (v->starts, 0);
    v->host->service_emit (v->host, VIEWER_SERVICE, "types", viewer_answer_empty ());
}

/* --------------------------------------------------------------------------------------------- */

/* The type of a file, what the signal "render" names for the renderers: by the longest end of
   its name a renderer named, else by how its text starts, else "text". Of two that fit as well,
   the one named later */
static const char *
preview_type (const viewer_t *v, const char *path, const char *text, gsize len)
{
    const char *type = "text";
    size_t best = 0;
    guint i;

    if (path != NULL)
    {
        const size_t lp = strlen (path);

        for (i = 0; i < v->suffixes->len; i++)
        {
            const preview_kind_t *k = (const preview_kind_t *) g_ptr_array_index (v->suffixes, i);
            const size_t ls = strlen (k->match);

            if (ls != 0 && lp > ls && ls >= best
                && g_ascii_strcasecmp (path + lp - ls, k->match) == 0)
            {
                type = k->type;
                best = ls;
            }
        }
        if (best != 0)
            return type;
    }

    // the start of the text, past a byte order mark and white space
    if (len >= 3 && memcmp (text, "\xEF\xBB\xBF", 3) == 0)
    {
        text += 3;
        len -= 3;
    }
    while (len != 0 && g_ascii_isspace (*text))
    {
        text++;
        len--;
    }
    for (i = 0; i < v->starts->len; i++)
    {
        const preview_kind_t *k = (const preview_kind_t *) g_ptr_array_index (v->starts, i);
        const size_t ls = strlen (k->match);

        if (ls != 0 && ls <= len && memcmp (text, k->match, ls) == 0)
            type = k->type;
    }

    return type;
}

/* --------------------------------------------------------------------------------------------- */

/* add_type: a renderer names a type, the ends of the names and the starts of the texts of its
   files */
static GVariant *
preview_add_type (viewer_t *v, GVariant *args, GError **error)
{
    gsize len = 0;
    char *type;
    char **suffixes, **starts;
    char **p;

    type = viewer_arg_text (args, "type", &len);
    if (type == NULL || *type == '\0')
    {
        g_free (type);
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "no type");
        return NULL;
    }

    suffixes = viewer_arg_strings (args, "suffixes");
    starts = viewer_arg_strings (args, "starts");

    for (p = suffixes; p != NULL && *p != NULL; p++)
    {
        preview_kind_t *k = g_new (preview_kind_t, 1);

        k->type = g_strdup (type);
        k->match = g_strdup (*p);
        g_ptr_array_add (v->suffixes, k);
    }
    for (p = starts; p != NULL && *p != NULL; p++)
    {
        preview_kind_t *k = g_new (preview_kind_t, 1);

        k->type = g_strdup (type);
        k->match = g_strdup (*p);
        g_ptr_array_add (v->starts, k);
    }

    g_strfreev (starts);
    g_strfreev (suffixes);
    g_free (type);
    return viewer_answer_empty ();
}

/* --------------------------------------------------------------------------------------------- */

static WEditWindow *
preview_window (viewer_t *v)
{
    if (v->preview_id == 0)
        return NULL;
    return (WEditWindow *) g_hash_table_lookup (v->windows, &v->preview_id);
}

/* --------------------------------------------------------------------------------------------- */

/* Text as a service takes it: a string when it is UTF-8, else the bytes it is */
static GVariant *
preview_text_variant (const char *text, gsize len)
{
    if (g_utf8_validate (text, (gssize) len, NULL) && memchr (text, '\0', len) == NULL)
        return g_variant_new_string (text);
    return g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, text, len, 1);
}

/* --------------------------------------------------------------------------------------------- */

/* Render the file of @edit into the preview, unless it shows this revision at this width */
static void
preview_render (viewer_t *v, void *edit)
{
    WEditWindow *win = preview_window (v);
    GVariantDict dict;
    char *path, *text;
    gsize len = 0;
    guint64 revision;
    int width;

    if (win == NULL || edit == NULL)
        return;

    // another file window in front: the room goes to it, the one before takes the screen again
    if (edit != v->edit)
    {
        v->edit = edit;
        v->host->window_give_room_back (v->host, win);
        v->host->window_make_room (v->host, win);
    }

    path = v->host->get_current_file (v->host, edit);
    revision = v->host->get_revision (v->host, edit);
    width = edit_text_window_text_cols (win);
    if (g_strcmp0 (path, v->path) == 0 && revision == v->revision && width == v->width)
    {
        g_free (path);
        return;
    }

    text = v->host->get_text (v->host, edit, &len);
    if (text == NULL)
    {
        g_free (path);
        return;
    }

    // another file: the renderers name their types again, one may have come or gone
    if (g_strcmp0 (path, v->path) != 0)
        preview_ask_types (v);

    g_free (v->path);
    v->path = path;
    g_free (v->type);
    v->type = g_strdup (preview_type (v, path, text, len));
    v->revision = revision;
    v->width = width;

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", v->preview_id);
    g_variant_dict_insert (&dict, "type", "s", v->type);
    g_variant_dict_insert (&dict, "path", "s", path != NULL ? path : "");
    g_variant_dict_insert_value (&dict, "text", preview_text_variant (text, len));
    g_variant_dict_insert (&dict, "width", "x", (gint64) width);
    g_variant_dict_insert (&dict, "revision", "x", (gint64) revision);

    v->asking = v->preview_id;
    v->answered = FALSE;
    v->host->service_emit (v->host, VIEWER_SERVICE, "render", g_variant_dict_end (&dict));
    v->asking = 0;

    // nobody renders this type: the text of the file, as it is
    if (!v->answered)
        edit_text_window_set_text (win, text, len);

    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

/* Scroll the preview to where the cursor of @edit is */
static void
preview_follow (viewer_t *v, void *edit)
{
    WEditWindow *win = preview_window (v);
    GVariantDict dict;
    long line;

    if (win == NULL || edit == NULL || v->type == NULL)
        return;

    line = v->host->get_cursor_line (v->host, edit);

    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "id", "x", v->preview_id);
    g_variant_dict_insert (&dict, "type", "s", v->type);
    g_variant_dict_insert (&dict, "line", "x", (gint64) line);

    v->asking = v->preview_id;
    v->answered = FALSE;
    v->host->service_emit (v->host, VIEWER_SERVICE, "follow", g_variant_dict_end (&dict));
    v->asking = 0;

    // the text of the file is shown line for line: the line of the cursor a third of the way down
    if (!v->answered)
        edit_text_window_scroll_to (win, MAX (0, line - 1 - edit_text_window_text_lines (win) / 3));
}

/* --------------------------------------------------------------------------------------------- */

/* Ctrl-Alt-P: the preview shown for the file of @edit, or hidden */
static mc_ep_result_t
preview_toggle (viewer_t *v, void *edit)
{
    WEditWindow *win = preview_window (v);

    if (win != NULL && widget_get_state (CONST_WIDGET (win), WST_VISIBLE))
    {
        v->host->window_give_room_back (v->host, win);
        v->host->window_hide (v->host, win);
        v->edit = NULL;
        return MC_EPR_OK;
    }

    // the focus on a window that is no file: the file seen on top
    if (edit == NULL)
        edit = v->host->window_top_file (v->host);
    if (edit == NULL)
        return MC_EPR_FAILED;

    if (win == NULL)
    {
        WRect a, r;

        v->host->window_area (v->host, &a);
        r = a;
        r.cols = (int) CLAMP (a.cols * VIEWER_SIZE_RIGHT / 100, 10, a.cols);
        r.x = a.x + a.cols - r.cols;
        (void) viewer_window_new (v, &r, VIEWER_DOCK_RIGHT, FALSE, PREVIEW_TITLE, NULL, 0, FALSE,
                                  &v->preview_id);
    }
    else
        viewer_show (v, win, FALSE, v->host->window_current (v->host));

    // what it shows may be old: all of it again; the room is made for this file
    g_clear_pointer (&v->path, g_free);
    v->edit = edit;
    preview_render (v, edit);
    preview_follow (v, edit);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
preview_shown (viewer_t *v)
{
    const WEditWindow *win = preview_window (v);

    return win != NULL && widget_get_state (CONST_WIDGET (win), WST_VISIBLE);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
viewer_plugin_activate (void *plugin_data, void *edit)
{
    return preview_toggle ((viewer_t *) plugin_data, edit);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
viewer_plugin_handle_key (void *plugin_data, int key, void *edit)
{
    viewer_t *v = (viewer_t *) plugin_data;

    if (v->key == 0 || key != v->key)
        return MC_EPR_NOT_SUPPORTED;

    return preview_toggle (v, edit);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
viewer_plugin_handle_event (void *plugin_data, void *edit, int event_id, void *payload)
{
    viewer_t *v = (viewer_t *) plugin_data;

    (void) payload;

    if (!preview_shown (v))
        return MC_EPR_NOT_SUPPORTED;

    switch (event_id)
    {
    case MC_EP_EVENT_TEXT_CHANGED:
        preview_render (v, edit);
        return MC_EPR_OK;

    case MC_EP_EVENT_CURSOR_MOVED:
        // the window may have been resized: the text is laid out again
        preview_render (v, edit);
        preview_follow (v, edit);
        return MC_EPR_OK;

    default:
        return MC_EPR_NOT_SUPPORTED;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The Preview in the menu Window */
static mc_ep_window_state_t
preview_kind_state (void *data)
{
    viewer_t *v = (viewer_t *) data;

    if (!preview_shown (v))
        return MC_EP_WINDOW_CLOSED;
    return v->host->window_current (v->host) == (void *) preview_window (v) ? MC_EP_WINDOW_FOCUSED
                                                                            : MC_EP_WINDOW_OPEN;
}

static void
preview_kind_show (void *data)
{
    viewer_t *v = (viewer_t *) data;

    if (!preview_shown (v))
        (void) preview_toggle (v, NULL);
    else
        v->host->window_show (v->host, preview_window (v));
}

static void
preview_kind_close (void *data)
{
    viewer_t *v = (viewer_t *) data;

    if (preview_shown (v))
        (void) preview_toggle (v, NULL);
}

static void *
preview_kind_window (void *data)
{
    viewer_t *v = (viewer_t *) data;

    return preview_shown (v) ? preview_window (v) : NULL;
}

static const mc_ep_window_kind_t preview_window_kind = {
    .name = "viewer.preview",
    .window = preview_kind_window,
    .label = N_ ("Pre&view"),
    .state = preview_kind_state,
    .show = preview_kind_show,
    .close = preview_kind_close,
};

/* --------------------------------------------------------------------------------------------- */

static void *
viewer_plugin_open (mc_editor_host_t *host, void *editor_dialog)
{
    viewer_t *v;
    GError *error = NULL;
    char *key_label = NULL;

    (void) editor_dialog;

    if (host->window_add == NULL || host->service_register == NULL || host->get_text == NULL)
        return NULL;

    v = g_new0 (viewer_t, 1);
    v->host = host;
    v->key =
        mc_plugin_prefs_load_hotkey (PREVIEW_CONFIG, "Preview", "key", PREVIEW_KEY, 0, &key_label);
    v->windows = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    v->docks = g_hash_table_new (g_direct_hash, g_direct_equal);
    v->suffixes = g_ptr_array_new_with_free_func (preview_kind_free);
    v->starts = g_ptr_array_new_with_free_func (preview_kind_free);

    if (!host->service_register (host, VIEWER_SERVICE, viewer_call, v, &error))
    {
        fprintf (stderr, "viewer: %s\n", error->message);
        g_error_free (error);
        g_ptr_array_free (v->starts, TRUE);
        g_ptr_array_free (v->suffixes, TRUE);
        g_hash_table_destroy (v->windows);
        g_hash_table_destroy (v->docks);
        g_free (v);
        g_free (key_label);
        return NULL;
    }
    if (host->window_kind != NULL)
    {
        mc_ep_window_kind_t kind = preview_window_kind;

        kind.shortcut = key_label;
        host->window_kind (host, &kind, v);
    }
    g_free (key_label);

    return v;
}

/* --------------------------------------------------------------------------------------------- */

static void
viewer_plugin_close (void *plugin_data)
{
    viewer_t *v = (viewer_t *) plugin_data;

    // the editor has destroyed the windows before
    v->host->service_unregister (v->host, VIEWER_SERVICE);
    g_ptr_array_free (v->starts, TRUE);
    g_ptr_array_free (v->suffixes, TRUE);
    g_hash_table_destroy (v->windows);
    g_hash_table_destroy (v->docks);
    g_free (v->type);
    g_free (v->path);
    g_free (v);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t viewer_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "viewer",
    .display_name = PREVIEW_TITLE,
    .flags = MC_EPF_HAS_MENU,
    .open = viewer_plugin_open,
    .close = viewer_plugin_close,
    .activate = viewer_plugin_activate,
    .handle_key = viewer_plugin_handle_key,
    .handle_event = viewer_plugin_handle_event,
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
