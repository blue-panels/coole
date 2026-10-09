/*
   The layouts of the windows of the editor.

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
 *  \brief Source: the layouts of the windows of the editor
 *
 *  A layout is which windows of the plugins are open and where: those of the column at the
 *  right with their shares of its height, the tabs of the row at the bottom and the one seen,
 *  the sizes of the two.  There are three of the editor's, Edit, Project and Debug, and those
 *  the user keeps, in layouts.ini of the settings, with the windows of each project as they
 *  were the last time.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"  // mc_config_get_path()
#include "lib/widget.h"

#include "src/plugins/project/project-core.h"  // project_find_root()

#include "edit-impl.h"
#include "editwidget.h"
#include "editwindow.h"
#include "editdock.h"
#include "editlayout.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define LAYOUTS_FILE  "layouts.ini"
#define LAYOUT_GROUP  "Layout "
#define PROJECT_GROUP "Project "
#define OPTIONS_GROUP "Options"

#define LAYOUT_DEBUG  "Debug"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    GPtrArray *right;   // the names of the windows of the column at the right, from the top
    GArray *shares;     // int: their shares of its height
    GPtrArray *bottom;  // the names of the tabs of the bottom
    GPtrArray *other;   // the windows open in no dock
    char *tab;          // the tab seen
    int right_size;     // percent of the screen; 0: as it is
    int bottom_size;
} layout_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static const char *const builtin_names[] = { "Edit", "Project", LAYOUT_DEBUG };

// the windows before layout_push(), and the name of the layout the windows were last put by
static layout_t *pushed = NULL;
static char *current_name = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static layout_t *
layout_new (void)
{
    layout_t *l = g_new0 (layout_t, 1);

    l->right = g_ptr_array_new_with_free_func (g_free);
    l->shares = g_array_new (FALSE, FALSE, sizeof (int));
    l->bottom = g_ptr_array_new_with_free_func (g_free);
    l->other = g_ptr_array_new_with_free_func (g_free);
    return l;
}

/* --------------------------------------------------------------------------------------------- */

static void
layout_free (layout_t *l)
{
    if (l == NULL)
        return;
    g_ptr_array_free (l->right, TRUE);
    g_array_free (l->shares, TRUE);
    g_ptr_array_free (l->bottom, TRUE);
    g_ptr_array_free (l->other, TRUE);
    g_free (l->tab);
    g_free (l);
}

/* --------------------------------------------------------------------------------------------- */

static void
layout_add_right (layout_t *l, const char *name, int share)
{
    g_ptr_array_add (l->right, g_strdup (name));
    g_array_append_val (l->shares, share);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
names_have (const GPtrArray *names, const char *name)
{
    guint i;

    for (i = 0; i < names->len; i++)
        if (strcmp (g_ptr_array_index (names, i), name) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
layout_has (const layout_t *l, const char *name)
{
    return names_have (l->right, name) || names_have (l->bottom, name)
        || names_have (l->other, name);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_ep_window_kind_t *
layout_kind (WDialog *h, const char *name, void **data)
{
    const mc_ep_window_kind_t *k;
    guint i;

    for (i = 0; (k = edit_window_kind_at (h, i, data)) != NULL; i++)
        if (k->name != NULL && strcmp (k->name, name) == 0)
            return k;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static WEditWindow *
layout_kind_window (const mc_ep_window_kind_t *k, void *data)
{
    if (k->window == NULL || k->state (data) == MC_EP_WINDOW_CLOSED)
        return NULL;
    return (WEditWindow *) k->window (data);
}

/* --------------------------------------------------------------------------------------------- */

/* The name of the kind of a window of a plugin, NULL for another window */
static const char *
layout_window_name (WDialog *h, const WEditWindow *win)
{
    const mc_ep_window_kind_t *k;
    void *data;
    guint i;

    if (win == NULL)
        return NULL;
    for (i = 0; (k = edit_window_kind_at (h, i, &data)) != NULL; i++)
        if (k->name != NULL && k->window != NULL && k->window (data) == win)
            return k->name;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The windows as they are */
static layout_t *
layout_capture (WDialog *h)
{
    layout_t *l = layout_new ();
    const mc_ep_window_kind_t *k;
    GPtrArray *wins;
    void *data;
    guint i;

    edit_dock_get_sizes (&l->right_size, &l->bottom_size);
    wins = edit_dock_windows (EDIT_DOCK_RIGHT);
    for (i = 0; i < wins->len; i++)
    {
        const char *name = layout_window_name (h, g_ptr_array_index (wins, i));

        if (name != NULL)
            layout_add_right (l, name, edit_dock_share (g_ptr_array_index (wins, i)));
    }
    g_ptr_array_free (wins, TRUE);
    wins = edit_dock_windows (EDIT_DOCK_BOTTOM);
    for (i = 0; i < wins->len; i++)
    {
        const char *name = layout_window_name (h, g_ptr_array_index (wins, i));

        if (name != NULL)
            g_ptr_array_add (l->bottom, g_strdup (name));
    }
    g_ptr_array_free (wins, TRUE);
    l->tab = g_strdup (layout_window_name (h, edit_dock_tab ()));

    for (i = 0; (k = edit_window_kind_at (h, i, &data)) != NULL; i++)
        if (k->name != NULL && k->state (data) != MC_EP_WINDOW_CLOSED && !layout_has (l, k->name))
            g_ptr_array_add (l->other, g_strdup (k->name));
    return l;
}

/* --------------------------------------------------------------------------------------------- */

/* The windows of a dock of a layout: opened, put in the dock, in its order */
static void
layout_apply_dock (WDialog *h, const layout_t *l, edit_dock_side_t side)
{
    const GPtrArray *names = side == EDIT_DOCK_RIGHT ? l->right : l->bottom;
    GPtrArray *order = g_ptr_array_new ();
    guint i;

    for (i = 0; i < names->len; i++)
    {
        const mc_ep_window_kind_t *k;
        WEditWindow *win;
        void *data;

        k = layout_kind (h, g_ptr_array_index (names, i), &data);
        if (k == NULL)
            continue;
        if (k->state (data) == MC_EP_WINDOW_CLOSED)
            k->show (data);
        win = layout_kind_window (k, data);
        if (win == NULL)
            continue;
        if (edit_dock_side (win) != side)
            edit_dock_add (win, side, 0);
        if (side == EDIT_DOCK_RIGHT)
            edit_dock_set_share (win, g_array_index (l->shares, int, i));
        g_ptr_array_add (order, win);
    }
    edit_dock_order (side, order);
    g_ptr_array_free (order, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* The windows as a layout has them, the focus staying where it is */
static void
layout_apply (WDialog *h, const layout_t *l)
{
    GList *current = GROUP (h)->current;
    Widget *focus = current != NULL ? WIDGET (current->data) : NULL;
    const mc_ep_window_kind_t *k;
    void *data;
    guint i;

    // the windows it has not go
    for (i = 0; (k = edit_window_kind_at (h, i, &data)) != NULL; i++)
        if (k->name != NULL && k->close != NULL && k->state (data) != MC_EP_WINDOW_CLOSED
            && !layout_has (l, k->name))
            k->close (data);

    edit_dock_set_sizes (l->right_size, l->bottom_size);
    layout_apply_dock (h, l, EDIT_DOCK_RIGHT);
    layout_apply_dock (h, l, EDIT_DOCK_BOTTOM);
    for (i = 0; i < l->other->len; i++)
    {
        k = layout_kind (h, g_ptr_array_index (l->other, i), &data);
        if (k != NULL && k->state (data) == MC_EP_WINDOW_CLOSED)
            k->show (data);
    }
    if (l->tab != NULL && (k = layout_kind (h, l->tab, &data)) != NULL)
    {
        WEditWindow *win = layout_kind_window (k, data);

        if (win != NULL && edit_dock_side (win) == EDIT_DOCK_BOTTOM)
            edit_dock_tab_select (win);
    }
    edit_dock_arrange (h);

    // the focus where it was, if that window is still seen
    if (focus != NULL && g_list_find (GROUP (h)->widgets, focus) != NULL
        && widget_get_state (focus, WST_VISIBLE))
        widget_select (focus);
    else
    {
        WEdit *top = edit_find_editor (h);

        if (top != NULL)
            widget_select (WIDGET (top));
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A group of the file of a layout or of a project: its name escaped, a group of a key file being
   no place for '[', ']' and the control characters */
static char *
layout_group (const char *prefix, const char *name)
{
    char *escaped = g_uri_escape_string (name, "/ ._-~+,:@!$&'()*=", TRUE);
    char *group = g_strconcat (prefix, escaped, (char *) NULL);

    g_free (escaped);
    return group;
}

/* --------------------------------------------------------------------------------------------- */

static char *
layouts_path (void)
{
    return g_build_filename (mc_config_get_path (), LAYOUTS_FILE, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static GKeyFile *
layouts_load (void)
{
    GKeyFile *kf = g_key_file_new ();
    char *path = layouts_path ();

    (void) g_key_file_load_from_file (kf, path, G_KEY_FILE_KEEP_COMMENTS, NULL);
    g_free (path);
    return kf;
}

/* --------------------------------------------------------------------------------------------- */

static void
layouts_store (GKeyFile *kf)
{
    char *path = layouts_path ();
    gsize len;
    char *contents = g_key_file_to_data (kf, &len, NULL);

    (void) g_file_set_contents (path, contents, (gssize) len, NULL);
    g_free (contents);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static char *
names_join (const GPtrArray *names)
{
    GString *s = g_string_new (NULL);
    guint i;

    for (i = 0; i < names->len; i++)
    {
        if (i != 0)
            g_string_append_c (s, ';');
        g_string_append (s, g_ptr_array_index (names, i));
    }
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
names_split (GPtrArray *names, const char *text)
{
    char **parts;
    guint i;

    if (text == NULL)
        return;
    parts = g_strsplit (text, ";", -1);
    for (i = 0; parts[i] != NULL; i++)
        if (*g_strstrip (parts[i]) != '\0')
            g_ptr_array_add (names, g_strdup (parts[i]));
    g_strfreev (parts);
}

/* --------------------------------------------------------------------------------------------- */

/* right = project.tree:40;debugger.panel:60, bottom = debugger.console;build.output, tab, other,
   right_size and bottom_size in percent */
static void
layout_to_group (GKeyFile *kf, const char *group, const layout_t *l)
{
    GString *right = g_string_new (NULL);
    char *text;
    guint i;

    for (i = 0; i < l->right->len; i++)
        g_string_append_printf (right, "%s%s:%d", i != 0 ? ";" : "",
                                (const char *) g_ptr_array_index (l->right, i),
                                g_array_index (l->shares, int, i));
    g_key_file_set_string (kf, group, "right", right->str);
    g_string_free (right, TRUE);
    text = names_join (l->bottom);
    g_key_file_set_string (kf, group, "bottom", text);
    g_free (text);
    text = names_join (l->other);
    g_key_file_set_string (kf, group, "other", text);
    g_free (text);
    g_key_file_set_string (kf, group, "tab", l->tab != NULL ? l->tab : "");
    g_key_file_set_integer (kf, group, "right_size", l->right_size);
    g_key_file_set_integer (kf, group, "bottom_size", l->bottom_size);
}

/* --------------------------------------------------------------------------------------------- */

static layout_t *
layout_from_group (GKeyFile *kf, const char *group)
{
    layout_t *l;
    char *text;

    if (!g_key_file_has_group (kf, group))
        return NULL;
    l = layout_new ();
    text = g_key_file_get_string (kf, group, "right", NULL);
    if (text != NULL)
    {
        char **parts = g_strsplit (text, ";", -1);
        guint i;

        for (i = 0; parts[i] != NULL; i++)
        {
            char *colon = strchr (parts[i], ':');
            int share = 100;

            if (colon != NULL)
            {
                *colon = '\0';
                share = MAX (1, atoi (colon + 1));
            }
            if (*g_strstrip (parts[i]) != '\0')
                layout_add_right (l, parts[i], share);
        }
        g_strfreev (parts);
        g_free (text);
    }
    text = g_key_file_get_string (kf, group, "bottom", NULL);
    names_split (l->bottom, text);
    g_free (text);
    text = g_key_file_get_string (kf, group, "other", NULL);
    names_split (l->other, text);
    g_free (text);
    l->tab = g_key_file_get_string (kf, group, "tab", NULL);
    if (l->tab != NULL && *l->tab == '\0')
        g_clear_pointer (&l->tab, g_free);
    l->right_size = g_key_file_get_integer (kf, group, "right_size", NULL);
    l->bottom_size = g_key_file_get_integer (kf, group, "bottom_size", NULL);
    return l;
}

/* --------------------------------------------------------------------------------------------- */

/* The layouts of the editor: Edit, the files alone; Project, the tree at the right; Debug, the
   tree and the panel of the debugger under it */
static layout_t *
layout_builtin (const char *name)
{
    layout_t *l;

    if (strcmp (name, "Edit") == 0)
        return layout_new ();
    if (strcmp (name, "Project") == 0)
    {
        l = layout_new ();
        layout_add_right (l, "project.tree", 100);
        return l;
    }
    if (strcmp (name, LAYOUT_DEBUG) == 0)
    {
        l = layout_new ();
        layout_add_right (l, "project.tree", 40);
        layout_add_right (l, "debugger.panel", 60);
        return l;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* A layout by its name: the one the user keeps, else the editor's */
static layout_t *
layout_load (const char *name)
{
    GKeyFile *kf = layouts_load ();
    char *group = layout_group (LAYOUT_GROUP, name);
    layout_t *l = layout_from_group (kf, group);

    g_free (group);
    g_key_file_free (kf);
    return l != NULL ? l : layout_builtin (name);
}

/* --------------------------------------------------------------------------------------------- */

/* The names of the layouts: the editor's, then those of the user */
static GPtrArray *
layout_names (void)
{
    GPtrArray *names = g_ptr_array_new_with_free_func (g_free);
    GKeyFile *kf = layouts_load ();
    char **groups = g_key_file_get_groups (kf, NULL);
    guint i;

    for (i = 0; i < G_N_ELEMENTS (builtin_names); i++)
        g_ptr_array_add (names, g_strdup (builtin_names[i]));
    for (i = 0; groups[i] != NULL; i++)
        if (g_str_has_prefix (groups[i], LAYOUT_GROUP))
        {
            char *name = g_uri_unescape_string (groups[i] + strlen (LAYOUT_GROUP), NULL);

            if (name != NULL && !names_have (names, name))
                g_ptr_array_add (names, name);
            else
                g_free (name);
        }
    g_strfreev (groups);
    g_key_file_free (kf);
    return names;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
layout_debug_switch (void)
{
    GKeyFile *kf = layouts_load ();
    GError *error = NULL;
    gboolean on = g_key_file_get_boolean (kf, OPTIONS_GROUP, "debug_layout", &error);

    if (error != NULL)
    {
        on = TRUE;
        g_error_free (error);
    }
    g_key_file_free (kf);
    return on;
}

/* --------------------------------------------------------------------------------------------- */

static void
layout_use (WDialog *h, const char *name)
{
    layout_t *l = layout_load (name);

    if (l == NULL)
        return;
    layout_apply (h, l);
    layout_free (l);
    g_free (current_name);
    current_name = g_strdup (name);
}

/* --------------------------------------------------------------------------------------------- */

/* The project of the file in front, when it is in one */
static char *
layout_project_root (WDialog *h)
{
    WEdit *top = edit_find_editor (h);
    char *root;

    if (top == NULL || top->filename == NULL)
        return NULL;
    root = project_find_root (top->filename);
    if (root != NULL && !project_is_project (root))
        g_clear_pointer (&root, g_free);
    return root;
}

/* --------------------------------------------------------------------------------------------- */

/* coole --git and its --git-*: the editor is there for git alone; the layout of the project is
   neither shown nor overwritten */
static gboolean
layout_set_aside (void)
{
    return edit_startup_option ("git") != NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The file the editor starts with has the focus, whatever window came after it */
static void
layout_focus_file (WDialog *h)
{
    WEdit *top = edit_find_editor (h);

    if (top != NULL)
        widget_select (WIDGET (top));
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
edit_layout_startup (void *dialog)
{
    WDialog *h = DIALOG (dialog);
    GKeyFile *kf;
    char *root, *group;
    layout_t *l;

    void *data;

    // no window of a plugin: nothing to put anywhere
    if (edit_window_kind_at (h, 0, &data) == NULL || layout_set_aside ())
        return;
    if (edit_startup_option ("debug") != NULL)
    {
        layout_use (h, LAYOUT_DEBUG);
        layout_focus_file (h);
        return;
    }
    root = layout_project_root (h);
    if (root == NULL)
        return;
    kf = layouts_load ();
    group = layout_group (PROJECT_GROUP, root);
    l = layout_from_group (kf, group);
    if (l != NULL)
    {
        layout_apply (h, l);
        layout_free (l);
        g_free (current_name);
        current_name = g_key_file_get_string (kf, group, "layout", NULL);
        layout_focus_file (h);
    }
    g_free (group);
    g_key_file_free (kf);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_layout_quit (WDialog *h)
{
    char *root = layout_project_root (h);
    void *data;
    GKeyFile *kf;
    char *group;
    layout_t *l;

    if (root == NULL)
        return;
    if (layout_set_aside ())
    {
        g_free (root);
        return;
    }
    // no window of a plugin, the plugins off: the layout kept is not overwritten by an empty one
    if (edit_window_kind_at (h, 0, &data) == NULL)
    {
        g_free (root);
        return;
    }
    // while debugging, the windows as they were before
    l = pushed != NULL ? pushed : layout_capture (h);
    kf = layouts_load ();
    group = layout_group (PROJECT_GROUP, root);
    layout_to_group (kf, group, l);
    g_key_file_set_string (kf, group, "layout", current_name != NULL ? current_name : "");
    layouts_store (kf);
    g_key_file_free (kf);
    g_free (group);
    if (l != pushed)
        layout_free (l);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_layout_push (WDialog *h, const char *name)
{
    if (pushed != NULL || (strcmp (name, LAYOUT_DEBUG) == 0 && !layout_debug_switch ()))
        return FALSE;
    pushed = layout_capture (h);
    layout_use (h, name);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_layout_pop (WDialog *h)
{
    layout_t *now;
    guint i;

    if (pushed == NULL)
        return;
    // the tabs of the bottom stay: what the program wrote is there to be read
    now = layout_capture (h);
    for (i = 0; i < now->bottom->len; i++)
        if (!layout_has (pushed, g_ptr_array_index (now->bottom, i)))
            g_ptr_array_add (pushed->bottom, g_strdup (g_ptr_array_index (now->bottom, i)));
    if (now->tab != NULL)
    {
        g_free (pushed->tab);
        pushed->tab = g_strdup (now->tab);
    }
    layout_free (now);
    layout_apply (h, pushed);
    layout_free (pushed);
    pushed = NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_layout_dialog (WDialog *h)
{
    GPtrArray *names = layout_names ();
    const int list_h = (int) MIN (names->len, 10) + 1;
    const int dlg_w = 50;
    const int dlg_h = list_h + 8;
    WDialog *dlg;
    WListbox *list;
    WCheck *debug;
    const char *chosen = NULL;
    int ret;
    guint i;

    dlg = dlg_create (TRUE, 0, 0, dlg_h, dlg_w, WPOS_CENTER, TRUE, dialog_colors, NULL, NULL,
                      "[Internal File Editor]", _ ("Layout of the windows"));
    list = listbox_new (2, 2, list_h, dlg_w - 4, FALSE, NULL);
    for (i = 0; i < names->len; i++)
    {
        const char *name = g_ptr_array_index (names, i);
        char *label =
            g_strdup_printf ("%c %s", g_strcmp0 (name, current_name) == 0 ? '*' : ' ', name);

        listbox_add_item (list, LISTBOX_APPEND_AT_END, 0, label, (void *) name, FALSE);
        g_free (label);
        if (g_strcmp0 (name, current_name) == 0)
            listbox_set_current (list, (int) i);
    }
    group_add_widget (GROUP (dlg), list);
    debug = check_new (list_h + 3, 2, layout_debug_switch (), _ ("&Debug layout while debugging"));
    group_add_widget (GROUP (dlg), debug);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, 3, B_ENTER, DEFPUSH_BUTTON, _ ("&Use"), NULL));
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, 12, B_USER, NORMAL_BUTTON, _ ("&Save as..."), NULL));
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, 27, B_USER + 1, NORMAL_BUTTON, _ ("De&lete"), NULL));
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, 38, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));
    widget_select (WIDGET (list));

    ret = dlg_run (dlg);
    if (ret != B_CANCEL)
    {
        char *text = NULL;
        GKeyFile *kf = layouts_load ();

        listbox_get_current (list, &text, (void **) &chosen);
        g_key_file_set_boolean (kf, OPTIONS_GROUP, "debug_layout", debug->state);
        if (ret == B_USER + 1 && chosen != NULL)
        {
            // the user's one goes: the editor's of that name comes back
            char *group = layout_group (LAYOUT_GROUP, chosen);

            (void) g_key_file_remove_group (kf, group, NULL);
            g_free (group);
        }
        layouts_store (kf);
        g_key_file_free (kf);
    }
    widget_destroy (WIDGET (dlg));

    if (ret == B_ENTER && chosen != NULL)
        layout_use (h, chosen);
    else if (ret == B_USER)
    {
        char *name = input_dialog (_ ("Save layout"), _ ("Name of the layout:"), "layout-name",
                                   current_name != NULL ? current_name : "", INPUT_COMPLETE_NONE);

        if (name != NULL && *g_strstrip (name) != '\0')
        {
            GKeyFile *kf = layouts_load ();
            char *group = layout_group (LAYOUT_GROUP, name);
            layout_t *l = layout_capture (h);

            layout_to_group (kf, group, l);
            layouts_store (kf);
            layout_free (l);
            g_free (group);
            g_key_file_free (kf);
            g_free (current_name);
            current_name = g_strdup (name);
        }
        g_free (name);
    }
    g_ptr_array_free (names, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
