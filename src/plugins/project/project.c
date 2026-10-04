/*
   The project plugin: the project a file is part of, its files at hand, and their tree.

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

/** \file project.c
 *  \brief Source: the project plugin
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/plugin-service.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"

#include "project-core.h"
#include "project.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define PROJECT_SERVICE        "project"
#define PROJECT_KEYMAP_SECTION "project"
#define PROJECT_RECENT_MAX     30
#define PROJECT_LIST_MAX       500

/*** file scope type declarations ****************************************************************/

/* the commands of the plugin, in the [project] section of the keymap */
enum
{
    PROJECT_CMD_NONE = -1,
    PROJECT_CMD_OPEN_FILE,
    PROJECT_CMD_RECENT,
    PROJECT_CMD_TREE,
    PROJECT_CMD_ALTERNATE,
    PROJECT_CMD_COUNT
};

/* the actions of the menus */
enum
{
    PROJECT_ACT_OPEN_FILE,
    PROJECT_ACT_RECENT,
    PROJECT_ACT_TREE,
    PROJECT_ACT_ALTERNATE,
    PROJECT_ACT_OPEN_PROJECT
};

typedef struct project_tree_t project_tree_t;

typedef struct
{
    mc_editor_host_t *host;
    char *root;            // NULL till a file is opened
    gboolean root_chosen;  // by the user: the files opened do not change it
    GPtrArray *files;      // relative to the root, NULL till listed
    GPtrArray *recent;     // absolute names, the latest first
    long commands[PROJECT_CMD_COUNT];
    project_tree_t *tree;
    gboolean service;
    // the marks of a closed and an open directory in the tree, from the skin
    char *glyph_closed;
    char *glyph_open;
    // what the list chose, opened once its dialog is gone
    char *open_file;
    long open_line;
} project_t;

/* a row of the tree */
typedef struct
{
    char *name;
    char *rel;  // relative to the root
    int depth;
    gboolean dir;
} project_row_t;

struct project_tree_t
{
    WEditWindow window;
    project_t *project;
    GHashTable *expanded;  // rel of the open directories
    GPtrArray *rows;       // project_row_t shown
    int selected;
    int top;
};

/* where an item of the list goes: a file, at a line or where it was */
typedef struct
{
    char *path;  // relative to the root, or absolute
    long line;   // 0: where the cursor was
} project_target_t;

/* an item of the list of the files to open */
typedef struct
{
    const char *rel;
    int score;
} project_match_t;

/*** forward declarations (file scope functions) *************************************************/

static void project_tree_rebuild (project_tree_t *tree);
static void project_tree_reveal (project_tree_t *tree, const char *file);

/*** file scope variables ************************************************************************/

static const mc_ep_command_t project_commands[PROJECT_CMD_COUNT + 1] = {
    { "ProjectOpenFile", N_ ("Open a file of the project"), "alt-shift-p" },
    { "ProjectRecentFiles", N_ ("Recent files of the project"), "ctrl-e" },
    { "ProjectTree", N_ ("Show or hide the project tree"), "alt-shift-t" },
    { "ProjectAlternate", N_ ("Switch between source and header"), "alt-shift-a" },
    { NULL, NULL, NULL },
};

/* state of the list of the files to open while its dialog runs */
static WInput *pick_input = NULL;
static WListbox *pick_list = NULL;
static const GPtrArray *pick_files = NULL;
static const GPtrArray *pick_recent = NULL;
static const char *pick_root = NULL;
static char *pick_last = NULL;
static mc_editor_host_t *pick_host = NULL;
static GPtrArray *pick_targets = NULL;  // project_target_t, what the items of the list go to

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
project_row_free (gpointer p)
{
    project_row_t *row = (project_row_t *) p;

    g_free (row->name);
    g_free (row->rel);
    g_free (row);
}

/* --------------------------------------------------------------------------------------------- */

/* The name of a file of the project, relative to the root; NULL for one outside */
static const char *
project_relative (const project_t *project, const char *file)
{
    const gsize len = project->root != NULL ? strlen (project->root) : 0;

    if (file == NULL || len == 0 || strncmp (file, project->root, len) != 0)
        return NULL;
    if (project->root[len - 1] == '/')
        return file + len;
    return file[len] == '/' ? file + len + 1 : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static char *
project_state_path (const project_t *project)
{
    char *hash, *name, *path;

    hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, project->root, -1);
    name = g_strconcat (hash, ".ini", (char *) NULL);
    path = g_build_filename (g_get_user_config_dir (), "coole", "projects", name, (char *) NULL);
    g_free (name);
    g_free (hash);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

static void
project_recent_save (const project_t *project)
{
    GKeyFile *keyfile;
    char *path, *dir, *contents;
    gsize len;

    if (project->root == NULL)
        return;
    path = project_state_path (project);
    keyfile = g_key_file_new ();
    g_key_file_set_string (keyfile, "Project", "root", project->root);
    g_key_file_set_string_list (keyfile, "Project", "recent",
                                (const gchar *const *) project->recent->pdata,
                                project->recent->len);
    contents = g_key_file_to_data (keyfile, &len, NULL);
    dir = g_path_get_dirname (path);
    if (g_mkdir_with_parents (dir, 0700) == 0)
        (void) g_file_set_contents (path, contents, (gssize) len, NULL);
    g_free (dir);
    g_free (contents);
    g_key_file_free (keyfile);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_recent_load (project_t *project)
{
    GKeyFile *keyfile = g_key_file_new ();
    char *path = project_state_path (project);
    char **names;
    gsize count, i;

    g_ptr_array_set_size (project->recent, 0);
    if (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
    {
        names = g_key_file_get_string_list (keyfile, "Project", "recent", &count, NULL);
        for (i = 0; names != NULL && i < count && i < PROJECT_RECENT_MAX; i++)
            if (g_file_test (names[i], G_FILE_TEST_IS_REGULAR))
                g_ptr_array_add (project->recent, g_strdup (names[i]));
        g_strfreev (names);
    }
    g_key_file_free (keyfile);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_files_drop (project_t *project)
{
    if (project->files != NULL)
        g_ptr_array_free (project->files, TRUE);
    project->files = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The files of the project, listed again: one may have come since */
static const GPtrArray *
project_files (project_t *project)
{
    project_files_drop (project);
    project->files = project_list_files (project->root);
    return project->files;
}

/* --------------------------------------------------------------------------------------------- */

static void
project_set_root (project_t *project, const char *root, gboolean chosen)
{
    if (root == NULL || g_strcmp0 (project->root, root) == 0)
    {
        project->root_chosen = project->root_chosen || chosen;
        return;
    }
    g_free (project->root);
    project->root = g_strdup (root);
    project->root_chosen = chosen;
    project_files_drop (project);
    project_recent_load (project);
    if (project->tree != NULL)
    {
        g_hash_table_remove_all (project->tree->expanded);
        project->tree->selected = project->tree->top = 0;
        (void) project_files (project);
        project_tree_rebuild (project->tree);
    }
    if (project->service)
        project->host->service_emit (project->host, PROJECT_SERVICE, "changed",
                                     g_variant_new_parsed ("{'root': <%s>}", project->root));
}

/* --------------------------------------------------------------------------------------------- */

/* A file window came to the front: it is the latest of the project, and it is the project the
   first time */
static void
project_file_seen (project_t *project, void *edit)
{
    char *file = project->host->get_current_file (project->host, edit);
    guint i;

    if (file == NULL)
        return;
    if (project->root == NULL
        || (!project->root_chosen && project_relative (project, file) == NULL
            && project->recent->len == 0))
    {
        char *root = project_find_root (file);

        project_set_root (project, root, FALSE);
        g_free (root);
    }
    if (project_relative (project, file) != NULL)
    {
        for (i = 0; i < project->recent->len; i++)
            if (strcmp (g_ptr_array_index (project->recent, i), file) == 0)
            {
                g_free (g_ptr_array_steal_index (project->recent, i));
                break;
            }
        g_ptr_array_insert (project->recent, 0, g_strdup (file));
        if (project->recent->len > PROJECT_RECENT_MAX)
            g_ptr_array_set_size (project->recent, PROJECT_RECENT_MAX);
        project_recent_save (project);
    }
    if (project->tree != NULL)
        project_tree_reveal (project->tree, file);
    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */
/* The list of the files to open */
/* --------------------------------------------------------------------------------------------- */

static gint
project_match_compare (gconstpointer a, gconstpointer b)
{
    const project_match_t *x = (const project_match_t *) a;
    const project_match_t *y = (const project_match_t *) b;

    if (x->score != y->score)
        return y->score - x->score;
    return strcmp (x->rel, y->rel);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_target_free (gpointer p)
{
    project_target_t *t = (project_target_t *) p;

    g_free (t->path);
    g_free (t);
}

/* --------------------------------------------------------------------------------------------- */

/* An item of the list; @path NULL: a line to read, which opens nothing */
static void
project_pick_add (const char *label, const char *path, long line)
{
    project_target_t *t = NULL;

    if (path != NULL)
    {
        t = g_new (project_target_t, 1);
        t->path = g_strdup (path);
        t->line = line;
        g_ptr_array_add (pick_targets, t);
    }
    listbox_add_item (pick_list, LISTBOX_APPEND_AT_END, 0, label, t, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The name of a file of the project that fits @query best; NULL when none does */
static const char *
project_pick_best_file (const char *query)
{
    const char *best = NULL;
    int best_score = 0;
    guint i;

    for (i = 0; pick_files != NULL && i < pick_files->len; i++)
    {
        const char *rel = g_ptr_array_index (pick_files, i);
        const int score = project_match_score (rel, query);

        if (score > best_score
            || (score == best_score && best != NULL && strlen (rel) < strlen (best)))
        {
            best = rel;
            best_score = score;
        }
    }
    return best;
}

/* --------------------------------------------------------------------------------------------- */

/* "@name": the symbols of the project; "file@name": those of a file, from the plugin ctags */
static void
project_pick_symbols (const char *text, const char *at)
{
    char *file_query = g_strndup (text, (gsize) (at - text));
    const char *query = at + 1;
    const char *rel = NULL;
    char *file = NULL;
    GVariantDict args;
    GVariant *reply, *symbols;
    gboolean indexing = FALSE;
    const char *name, *kind, *path;
    gint32 line;
    GVariantIter iter;
    int count = 0;

    if (*file_query != '\0')
    {
        rel = project_pick_best_file (file_query);
        if (rel == NULL)
        {
            project_pick_add (_ ("No file of the project matches."), NULL, 0);
            g_free (file_query);
            return;
        }
        file = g_build_filename (pick_root, rel, (char *) NULL);
    }
    else if (*query == '\0')
    {
        project_pick_add (_ ("A name after @: the symbols of the project; file@: of a file."), NULL,
                          0);
        g_free (file_query);
        return;
    }

    g_variant_dict_init (&args, NULL);
    g_variant_dict_insert (&args, "root", "s", pick_root);
    if (file != NULL)
        g_variant_dict_insert (&args, "file", "s", file);
    g_variant_dict_insert (&args, "query", "s", query);
    g_variant_dict_insert (&args, "max", "i", (gint32) PROJECT_LIST_MAX);
    reply = pick_host->service_call != NULL
        ? pick_host->service_call (pick_host, "ctags", "symbols", g_variant_dict_end (&args), NULL)
        : (g_variant_dict_clear (&args), NULL);
    if (reply == NULL)
        project_pick_add (_ ("The symbols come from the plugin ctags, which is off."), NULL, 0);
    else
    {
        (void) g_variant_lookup (reply, "indexing", "b", &indexing);
        symbols = g_variant_lookup_value (reply, "symbols", G_VARIANT_TYPE ("a(sssi)"));
        if (symbols != NULL)
        {
            g_variant_iter_init (&iter, symbols);
            while (g_variant_iter_next (&iter, "(&s&s&si)", &name, &kind, &path, &line))
            {
                const char *shown = path;
                char *label;

                if (g_str_has_prefix (path, pick_root) && path[strlen (pick_root)] == '/')
                    shown = path + strlen (pick_root) + 1;
                label = file != NULL
                    ? g_strdup_printf ("%-40s %-8s %d", name, kind, (int) line)
                    : g_strdup_printf ("%-40s %-8s %s:%d", name, kind, shown, (int) line);
                project_pick_add (label, path, line);
                g_free (label);
                count++;
            }
            g_variant_unref (symbols);
        }
        if (count == 0)
            project_pick_add (indexing ? _ ("The project is being indexed...")
                                       : _ ("No symbol matches."),
                              NULL, 0);
        g_variant_unref (reply);
    }
    g_free (file);
    g_free (file_query);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_pick_refilter (void)
{
    const char *text = input_get_ctext (pick_input);
    char *file_query = NULL;
    long line = 0;
    GArray *matches;
    guint i;

    if (pick_last != NULL && strcmp (pick_last, text) == 0)
        return;
    g_free (pick_last);
    pick_last = g_strdup (text);
    listbox_remove_list (pick_list);
    g_ptr_array_set_size (pick_targets, 0);

    if (strchr (text, '@') != NULL)
    {
        project_pick_symbols (text, strchr (text, '@'));
        listbox_select_first (pick_list);
        widget_draw (WIDGET (pick_list));
        return;
    }

    // "name:42": the file at that line
    {
        const char *colon = strrchr (text, ':');

        if (colon != NULL && colon != text && colon[strspn (colon + 1, "0123456789") + 1] == '\0')
        {
            line = atol (colon + 1);
            file_query = g_strndup (text, (gsize) (colon - text));
            text = file_query;
        }
    }

    // nothing typed: the recent files first, then all
    if (*text == '\0' && pick_recent != NULL)
        for (i = 0; i < pick_recent->len; i++)
        {
            const char *file = g_ptr_array_index (pick_recent, i);
            const char *rel = strncmp (file, pick_root, strlen (pick_root)) == 0
                ? file + strlen (pick_root) + (pick_root[strlen (pick_root) - 1] == '/' ? 0 : 1)
                : file;

            project_pick_add (rel, file, 0);
        }

    matches = g_array_new (FALSE, FALSE, sizeof (project_match_t));
    for (i = 0; pick_files != NULL && i < pick_files->len; i++)
    {
        project_match_t m;

        m.rel = g_ptr_array_index (pick_files, i);
        m.score = project_match_score (m.rel, text);
        if (m.score > 0)
            g_array_append_val (matches, m);
    }
    if (*text != '\0')
        g_array_sort (matches, project_match_compare);
    for (i = 0; i < matches->len && i < PROJECT_LIST_MAX; i++)
    {
        const project_match_t *m = &g_array_index (matches, project_match_t, i);

        if (*text == '\0' && pick_recent != NULL && pick_recent->len > 0)
            break;  // the recent ones say it: the whole list comes with the first letter
        if (line > 0)
        {
            char *label = g_strdup_printf ("%s:%ld", m->rel, line);

            project_pick_add (label, m->rel, line);
            g_free (label);
        }
        else
            project_pick_add (m->rel, m->rel, 0);
    }
    g_array_free (matches, TRUE);
    g_free (file_query);
    listbox_select_first (pick_list);
    widget_draw (WIDGET (pick_list));
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
project_pick_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
    {
        cb_ret_t ret = dlg_default_callback (w, sender, msg, parm, data);

        widget_select (WIDGET (pick_input));
        return ret;
    }
    case MSG_KEY:
        // the keys that go through the list, the focus staying in the line
        if (parm == KEY_UP || parm == KEY_DOWN || parm == KEY_PPAGE || parm == KEY_NPAGE)
        {
            send_message (WIDGET (pick_list), w, MSG_KEY, parm, data);
            return MSG_HANDLED;
        }
        return dlg_default_callback (w, sender, msg, parm, data);
    case MSG_POST_KEY:
        project_pick_refilter ();
        return MSG_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static lcback_ret_t
project_pick_activate (WListbox *l)
{
    (void) l;
    return LISTBOX_DONE;
}

/* --------------------------------------------------------------------------------------------- */

/* Choose a file of the project, and maybe a line of it (@line, 0 when none); NULL when none is.
   @recent_only lists the recent ones alone. */
static char *
project_pick (project_t *project, const char *title, gboolean recent_only, long *line)
{
    WDialog *dlg;
    char *chosen = NULL;
    int dlg_h, dlg_w, list_h;

    dlg_w = MIN (COLS - 4, 100);
    list_h = MAX (5, MIN (LINES - 10, 20));
    dlg_h = list_h + 6;
    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, project_pick_callback, NULL, "[Project]", title);
    dlg->help_file = "project.md";

    pick_input = input_new (1, 1, input_colors, dlg_w - 2, "", NULL, INPUT_COMPLETE_NONE);
    group_add_widget (GROUP (dlg), pick_input);
    pick_list = listbox_new (3, 1, list_h, dlg_w - 2, FALSE, project_pick_activate);
    group_add_widget (GROUP (dlg), pick_list);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 - 10, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 + 1, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    pick_files = recent_only ? NULL : project_files (project);
    pick_recent = project->recent;
    pick_root = project->root;
    pick_host = project->host;
    pick_targets = g_ptr_array_new_with_free_func (project_target_free);
    g_clear_pointer (&pick_last, g_free);
    project_pick_refilter ();

    *line = 0;
    if (dlg_run (dlg) == B_ENTER)
    {
        char *text = NULL;
        const project_target_t *t = NULL;

        listbox_get_current (pick_list, &text, (void **) &t);
        if (t != NULL)
        {
            chosen = g_path_is_absolute (t->path)
                ? g_strdup (t->path)
                : g_build_filename (project->root, t->path, (char *) NULL);
            *line = t->line;
        }
    }

    pick_input = NULL;
    pick_list = NULL;
    pick_files = NULL;
    pick_recent = NULL;
    pick_root = NULL;
    pick_host = NULL;
    g_clear_pointer (&pick_last, g_free);
    widget_destroy (WIDGET (dlg));
    g_clear_pointer (&pick_targets, g_ptr_array_unref);
    return chosen;
}

/* --------------------------------------------------------------------------------------------- */
/* The tree of the project */
/* --------------------------------------------------------------------------------------------- */

static gint
project_row_compare (gconstpointer a, gconstpointer b)
{
    const project_row_t *x = *(project_row_t *const *) a;
    const project_row_t *y = *(project_row_t *const *) b;

    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return g_utf8_collate (x->name, y->name);
}

/* --------------------------------------------------------------------------------------------- */

/* The rows of a directory of the tree, and of the open ones in it, from the sorted names */
static void
project_tree_add_dir (project_tree_t *tree, const GPtrArray *files, const char *dir, int depth)
{
    const gsize dir_len = strlen (dir);
    GPtrArray *children = g_ptr_array_new ();
    GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    guint i;

    for (i = 0; files != NULL && i < files->len; i++)
    {
        const char *rel = g_ptr_array_index (files, i);
        const char *rest, *slash;
        project_row_t *row;

        if (dir_len != 0 && (strncmp (rel, dir, dir_len) != 0 || rel[dir_len] != '/'))
            continue;
        rest = dir_len != 0 ? rel + dir_len + 1 : rel;
        slash = strchr (rest, '/');
        row = g_new0 (project_row_t, 1);
        row->depth = depth;
        if (slash == NULL)
        {
            row->name = g_strdup (rest);
            row->rel = g_strdup (rel);
        }
        else
        {
            row->name = g_strndup (rest, (gsize) (slash - rest));
            row->rel = g_strndup (rel, (gsize) (slash - rel));
            row->dir = TRUE;
            if (g_hash_table_contains (seen, row->rel))
            {
                project_row_free (row);
                continue;
            }
            g_hash_table_add (seen, g_strdup (row->rel));
        }
        g_ptr_array_add (children, row);
    }
    g_ptr_array_sort (children, project_row_compare);

    for (i = 0; i < children->len; i++)
    {
        project_row_t *row = g_ptr_array_index (children, i);

        g_ptr_array_add (tree->rows, row);
        if (row->dir && g_hash_table_contains (tree->expanded, row->rel))
            project_tree_add_dir (tree, files, row->rel, depth + 1);
    }
    g_ptr_array_free (children, FALSE);
    g_hash_table_destroy (seen);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_rebuild (project_tree_t *tree)
{
    project_t *project = tree->project;

    g_ptr_array_set_size (tree->rows, 0);
    if (project->files == NULL)
        (void) project_files (project);
    project_tree_add_dir (tree, project->files, "", 0);
    tree->selected = CLAMP (tree->selected, 0, MAX ((int) tree->rows->len - 1, 0));
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

static int
project_tree_body_lines (const project_tree_t *tree)
{
    return MAX (1, WIDGET (tree)->rect.lines - 2);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_scroll_to_selected (project_tree_t *tree)
{
    const int lines = project_tree_body_lines (tree);

    if (tree->selected < tree->top)
        tree->top = tree->selected;
    else if (tree->selected >= tree->top + lines)
        tree->top = tree->selected - lines + 1;
}

/* --------------------------------------------------------------------------------------------- */

/* Open the directories down to a file of the project and put the selection on it */
static void
project_tree_reveal (project_tree_t *tree, const char *file)
{
    const char *rel = project_relative (tree->project, file);
    const char *slash;
    guint i;

    if (rel == NULL)
        return;
    for (slash = strchr (rel, '/'); slash != NULL; slash = strchr (slash + 1, '/'))
        g_hash_table_add (tree->expanded, g_strndup (rel, (gsize) (slash - rel)));
    project_tree_rebuild (tree);
    for (i = 0; i < tree->rows->len; i++)
        if (strcmp (((project_row_t *) g_ptr_array_index (tree->rows, i))->rel, rel) == 0)
        {
            tree->selected = (int) i;
            break;
        }
    project_tree_scroll_to_selected (tree);
    widget_draw (WIDGET (tree));
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_draw (project_tree_t *tree)
{
    WEditWindow *win = &tree->window;
    Widget *w = WIDGET (tree);
    const gboolean focused = widget_get_state (w, WST_FOCUSED);
    const int color = edit_window_frame_color (win, focused);
    const int lines = project_tree_body_lines (tree);
    const int width = MAX (0, w->rect.cols - 2);
    char *current = NULL;
    const char *current_rel;
    void *top_file;
    char *title;
    int row;

    edit_window_draw_frame (win, color, focused);
    tty_setcolor (color);
    title = g_strdup_printf (
        "[%s]", tree->project->root != NULL ? x_basename (tree->project->root) : _ ("Project"));
    widget_gotoyx (w, 0, 2);
    tty_print_string (str_term_trim (title, MAX (0, w->rect.cols - 10)));
    g_free (title);
    edit_window_draw_icons (win, color);

    top_file = tree->project->host->window_top_file (tree->project->host);
    if (top_file != NULL)
        current = tree->project->host->get_current_file (tree->project->host, top_file);
    current_rel = project_relative (tree->project, current);

    for (row = 0; row < lines; row++)
    {
        const int index = tree->top + row;
        GString *line = g_string_new (NULL);
        int c = EDITOR_NORMAL_COLOR;

        if (index < (int) tree->rows->len)
        {
            const project_row_t *r = g_ptr_array_index (tree->rows, index);
            int i;

            for (i = 0; i < r->depth; i++)
                g_string_append (line, "  ");
            if (r->dir)
            {
                g_string_append (line,
                                 g_hash_table_contains (tree->expanded, r->rel)
                                     ? tree->project->glyph_open
                                     : tree->project->glyph_closed);
                g_string_append_c (line, ' ');
            }
            else
                g_string_append (line, "  ");
            g_string_append (line, r->name);
            if (r->dir)
                g_string_append_c (line, '/');
            if (index == tree->selected && focused)
                c = EDITOR_MARKED_COLOR;
            else if (current_rel != NULL && strcmp (current_rel, r->rel) == 0)
                c = EDITOR_BOLD_COLOR;
        }
        tty_setcolor (EDITOR_NORMAL_COLOR);
        tty_draw_hline (w->rect.y + row + 1, w->rect.x + 1, ' ', width);
        tty_setcolor (c);
        widget_gotoyx (w, row + 1, 1);
        tty_print_string (str_fit_to_term (line->str, width, J_LEFT));
        g_string_free (line, TRUE);
    }
    g_free (current);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_activate (project_tree_t *tree)
{
    project_row_t *row;

    if (tree->selected < 0 || tree->selected >= (int) tree->rows->len)
        return;
    row = g_ptr_array_index (tree->rows, tree->selected);
    if (row->dir)
    {
        if (!g_hash_table_remove (tree->expanded, row->rel))
            g_hash_table_add (tree->expanded, g_strdup (row->rel));
        project_tree_rebuild (tree);
    }
    else
    {
        char *file = g_build_filename (tree->project->root, row->rel, (char *) NULL);

        (void) tree->project->host->open_file (tree->project->host, file);
        g_free (file);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
project_tree_key (project_tree_t *tree, int key)
{
    const int lines = project_tree_body_lines (tree);
    const int last = (int) tree->rows->len - 1;
    project_row_t *row = tree->selected >= 0 && tree->selected <= last
        ? g_ptr_array_index (tree->rows, tree->selected)
        : NULL;

    switch (key)
    {
    case KEY_UP:
        tree->selected = MAX (0, tree->selected - 1);
        break;
    case KEY_DOWN:
        tree->selected = MIN (last, tree->selected + 1);
        break;
    case KEY_PPAGE:
        tree->selected = MAX (0, tree->selected - lines);
        break;
    case KEY_NPAGE:
        tree->selected = MIN (last, tree->selected + lines);
        break;
    case KEY_HOME:
        tree->selected = 0;
        break;
    case KEY_END:
        tree->selected = MAX (0, last);
        break;
    case KEY_RIGHT:
        if (row != NULL && row->dir && !g_hash_table_contains (tree->expanded, row->rel))
            project_tree_activate (tree);
        break;
    case KEY_LEFT:
        if (row != NULL && row->dir && g_hash_table_contains (tree->expanded, row->rel))
            project_tree_activate (tree);
        else if (row != NULL && row->depth > 0)
        {
            // up to the directory it is in
            int i;

            for (i = tree->selected - 1; i >= 0; i--)
                if (((project_row_t *) g_ptr_array_index (tree->rows, i))->depth < row->depth)
                {
                    tree->selected = i;
                    break;
                }
        }
        break;
    case '\n':
    case KEY_ENTER:
        project_tree_activate (tree);
        break;
    default:
        // a letter goes to the next name that starts with it
        if (key > ' ' && key < 128 && last >= 0)
        {
            int i;

            for (i = 1; i <= last + 1; i++)
            {
                const int at = (tree->selected + i) % (last + 1);
                const project_row_t *r = g_ptr_array_index (tree->rows, at);

                if (g_ascii_tolower (r->name[0]) == g_ascii_tolower (key))
                {
                    tree->selected = at;
                    break;
                }
            }
            break;
        }
        return FALSE;
    }
    project_tree_scroll_to_selected (tree);
    widget_draw (WIDGET (tree));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_buttonbar (project_tree_t *tree)
{
    WButtonBar *bb = buttonbar_find (DIALOG (WIDGET (tree)->owner));
    int i;

    if (bb == NULL)
        return;
    for (i = 2; i <= 10; i++)
        buttonbar_clear_label (bb, i, NULL);
    buttonbar_set_label_command (bb, 1, _ ("Help"), CK_Help, NULL);
    buttonbar_set_label_command (bb, 10, _ ("Close"), CK_Close, WIDGET (tree));
    widget_draw (WIDGET (bb));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
project_tree_close (WEditWindow *win)
{
    WDialog *dialog = DIALOG (WIDGET (win)->owner);

    edit_window_give_room_back (win);
    edit_window_destroy (win);
    widget_draw (WIDGET (dialog));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
project_tree_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    project_tree_t *tree = (project_tree_t *) w;

    switch (msg)
    {
    case MSG_DRAW:
        project_tree_draw (tree);
        return MSG_HANDLED;
    case MSG_FOCUS:
        project_tree_buttonbar (tree);
        project_tree_draw (tree);
        return MSG_HANDLED;
    case MSG_UNFOCUS:
        project_tree_draw (tree);
        return MSG_HANDLED;
    case MSG_KEY:
        if (parm == KEY_F (10) || parm == ESC_CHAR)
        {
            (void) project_tree_close (&tree->window);
            return MSG_HANDLED;
        }
        return project_tree_key (tree, parm) ? MSG_HANDLED : MSG_NOT_HANDLED;
    case MSG_ACTION:
        if (parm == CK_Close)
        {
            (void) project_tree_close (&tree->window);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;
    case MSG_CURSOR:
        widget_gotoyx (w, 1 + tree->selected - tree->top, 1);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (tree->project != NULL)
            tree->project->tree = NULL;
        g_hash_table_destroy (tree->expanded);
        g_ptr_array_free (tree->rows, TRUE);
        return group_default_callback (w, sender, msg, parm, data);
    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    project_tree_t *tree = (project_tree_t *) w;
    const int index = tree->top + event->y - 1;

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        widget_select (w);
        if (index >= 0 && index < (int) tree->rows->len)
            tree->selected = index;
        widget_draw (w);
        break;
    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE && index == tree->selected)
            project_tree_activate (tree);
        else if (index == tree->selected && index >= 0 && index < (int) tree->rows->len
                 && ((project_row_t *) g_ptr_array_index (tree->rows, index))->dir)
            project_tree_activate (tree);
        break;
    case MSG_MOUSE_SCROLL_UP:
        tree->top = MAX (0, tree->top - 3);
        widget_draw (w);
        break;
    case MSG_MOUSE_SCROLL_DOWN:
        tree->top =
            MAX (0, MIN ((int) tree->rows->len - project_tree_body_lines (tree), tree->top + 3));
        widget_draw (w);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
project_tree_title (const WEditWindow *win)
{
    const project_tree_t *tree = (const project_tree_t *) win;

    return g_strdup_printf (
        _ ("Project %s"),
        tree->project != NULL && tree->project->root != NULL ? tree->project->root : "");
}

/* --------------------------------------------------------------------------------------------- */

static const edit_window_class_t project_tree_class = {
    .callback = project_tree_callback,
    .mouse_callback = project_tree_mouse,
    .get_title = project_tree_title,
    .close = project_tree_close,
    .min_lines = 5,
    .min_cols = 16,
};

/* --------------------------------------------------------------------------------------------- */

/* Alt-1: the tree comes, takes the focus, and goes */
static void
project_tree_toggle (project_t *project)
{
    project_tree_t *tree;
    WRect area, rect;
    void *top_file;

    if (project->tree != NULL)
    {
        if (project->host->window_current (project->host) == project->tree)
            (void) project_tree_close (&project->tree->window);
        else
            project->host->window_show (project->host, project->tree);
        return;
    }
    if (project->root == NULL)
    {
        project->host->message (project->host, D_NORMAL, _ ("Project"),
                                _ ("Open a file of the project first."));
        return;
    }

    top_file = project->host->window_top_file (project->host);
    project->host->window_area (project->host, &area);
    rect = area;
    tree = g_new0 (project_tree_t, 1);
    edit_window_init (&tree->window, &rect, &project_tree_class);
    tree->window.fullscreen = 0;
    tree->project = project;
    tree->expanded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    tree->rows = g_ptr_array_new_with_free_func (project_row_free);
    project->tree = tree;
    project->host->window_add (project->host, tree);
    // the column at the right, the panel of the debugger under it
    project->host->window_dock_right (project->host, tree, CLAMP (area.cols * 30 / 100, 24, 50));
    (void) project_files (project);
    project_tree_rebuild (tree);
    if (top_file != NULL)
    {
        char *file = project->host->get_current_file (project->host, top_file);

        project_tree_reveal (tree, file);
        g_free (file);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* Commands */
/* --------------------------------------------------------------------------------------------- */

/* The file the list chose: from the idle of the editor, the list gone, so that the window it
   opens is drawn */
static void
project_open_later (void *data)
{
    project_t *project = (project_t *) data;
    char *file = project->open_file;

    project->open_file = NULL;
    if (file != NULL && project->open_line > 0)
        (void) project->host->show_location (project->host, file, project->open_line);
    else if (file != NULL)
        (void) project->host->open_file (project->host, file);
    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_open_chosen (project_t *project, const char *title, gboolean recent_only)
{
    char *file;
    long line;

    if (project->root == NULL)
    {
        project->host->message (project->host, D_NORMAL, _ ("Project"),
                                _ ("Open a file of the project first."));
        return;
    }
    file = project_pick (project, title, recent_only, &line);
    if (file == NULL)
        return;
    g_free (project->open_file);
    project->open_file = file;
    project->open_line = line;
    if (project->host->call_later != NULL)
        project->host->call_later (project->host, project_open_later, project);
    else
        project_open_later (project);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_alternate (project_t *project, void *edit)
{
    char *file, *other;

    if (edit == NULL)
        edit = project->host->window_top_file (project->host);
    file = edit != NULL ? project->host->get_current_file (project->host, edit) : NULL;
    if (file == NULL)
        return;
    if (project->files == NULL && project->root != NULL)
        (void) project_files (project);
    other = project_alternate_file (file, project->root, project->files);
    if (other != NULL)
        (void) project->host->open_file (project->host, other);
    else
        project->host->message (project->host, D_NORMAL, _ ("Project"),
                                _ ("No header or source pairs with this file."));
    g_free (other);
    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
project_run_command (project_t *project, int cmd, void *edit)
{
    switch (cmd)
    {
    case PROJECT_CMD_OPEN_FILE:
        project_open_chosen (project, _ ("Open file of the project"), FALSE);
        return TRUE;
    case PROJECT_CMD_RECENT:
        project_open_chosen (project, _ ("Recent files of the project"), TRUE);
        return TRUE;
    case PROJECT_CMD_TREE:
        project_tree_toggle (project);
        return TRUE;
    case PROJECT_CMD_ALTERNATE:
        project_alternate (project, edit);
        return TRUE;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
project_command (const project_t *project, long command)
{
    int i;

    for (i = 0; command != CK_IgnoreKey && i < PROJECT_CMD_COUNT; i++)
        if (project->commands[i] == command)
            return i;
    return PROJECT_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
project_handle_key (void *data, int key, void *edit)
{
    project_t *project = (project_t *) data;
    const int cmd = project_command (
        project, project->host->command_lookup (project->host, PROJECT_KEYMAP_SECTION, key));

    return project_run_command (project, cmd, edit) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
project_handle_action (void *data, long command, void *edit)
{
    project_t *project = (project_t *) data;

    return project_run_command (project, project_command (project, command), edit)
        ? MC_EPR_OK
        : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

/* coole --debug, the editor up: the latest file of the project instead of an empty one, and the
   tree */
static void
project_startup (void *data)
{
    project_t *project = (project_t *) data;
    void *edit = project->host->window_top_file (project->host);
    char *file = edit != NULL ? project->host->get_current_file (project->host, edit) : NULL;

    if (edit != NULL && file == NULL && !((WEdit *) edit)->modified && project->recent->len > 0
        && project->host->open_file (project->host, g_ptr_array_index (project->recent, 0)))
        (void) project->host->window_close (project->host, edit);
    g_free (file);
    if (project->tree == NULL)
        project_tree_toggle (project);
}

static mc_ep_result_t
project_handle_event (void *data, void *edit, int event_id, void *payload)
{
    project_t *project = (project_t *) data;

    (void) payload;
    if (event_id == MC_EP_EVENT_FOCUS_IN && edit != NULL)
        project_file_seen (project, edit);
    else if (event_id == MC_EP_EVENT_FILE_SAVED || event_id == MC_EP_EVENT_FILE_RENAMED)
    {
        // a file new to the project
        project_files_drop (project);
        if (project->tree != NULL)
            project_tree_rebuild (project->tree);
    }
    return MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
project_file_open (void *data, void *edit)
{
    project_file_seen ((project_t *) data, edit);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* Menu actions */
/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
project_act_open_file (void *data, void *edit)
{
    return project_run_command (data, PROJECT_CMD_OPEN_FILE, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
project_act_recent (void *data, void *edit)
{
    return project_run_command (data, PROJECT_CMD_RECENT, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
project_act_tree (void *data, void *edit)
{
    return project_run_command (data, PROJECT_CMD_TREE, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
project_act_alternate (void *data, void *edit)
{
    return project_run_command (data, PROJECT_CMD_ALTERNATE, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
project_act_open_project (void *data, void *edit)
{
    project_t *project = (project_t *) data;
    char *file = edit != NULL ? project->host->get_current_file (project->host, edit) : NULL;
    char *suggested = project->root != NULL ? g_strdup (project->root)
        : file != NULL                      ? project_find_root (file)
                                            : g_get_current_dir ();
    char *chosen, *root;

    g_free (file);
    chosen = input_dialog (_ ("Open project"), _ ("Project directory:"), "project-root", suggested,
                           INPUT_COMPLETE_FILENAMES);
    g_free (suggested);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    root = g_canonicalize_filename (chosen, NULL);
    g_free (chosen);
    if (!g_file_test (root, G_FILE_TEST_IS_DIR))
    {
        project->host->message (project->host, D_ERROR, _ ("Project"),
                                _ ("The project directory does not exist."));
        g_free (root);
        return MC_EPR_FAILED;
    }
    project_set_root (project, root, TRUE);
    g_free (root);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* The service: what the other plugins and the scripts ask */
/* --------------------------------------------------------------------------------------------- */

static GVariant *
project_call (void *data, const char *method, GVariant *args, GError **error)
{
    project_t *project = (project_t *) data;
    GVariantDict reply;

    g_variant_dict_init (&reply, NULL);
    if (strcmp (method, "root") == 0)
    {
        // of a file, else of the project there is
        const char *file = NULL;
        char *root;

        if (args != NULL)
            (void) g_variant_lookup (args, "file", "&s", &file);
        root = file != NULL ? project_find_root (file) : g_strdup (project->root);
        if (root != NULL)
            g_variant_dict_insert (&reply, "root", "s", root);
        g_free (root);
    }
    else if (strcmp (method, "files") == 0)
    {
        const GPtrArray *files = project->root != NULL ? project_files (project) : NULL;

        g_variant_dict_insert_value (
            &reply, "files",
            g_variant_new_strv (files != NULL ? (const gchar *const *) files->pdata : NULL,
                                files != NULL ? (gssize) files->len : 0));
    }
    else if (strcmp (method, "recent") == 0)
        g_variant_dict_insert_value (
            &reply, "files",
            g_variant_new_strv ((const gchar *const *) project->recent->pdata,
                                (gssize) project->recent->len));
    else
    {
        g_variant_dict_clear (&reply);
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD, "project: no method %s",
                     method);
        return NULL;
    }
    return g_variant_dict_end (&reply);
}

/* --------------------------------------------------------------------------------------------- */

static void *
project_open (mc_editor_host_t *host, void *editor_dialog)
{
    project_t *project = g_new0 (project_t, 1);
    int i;

    (void) editor_dialog;
    project->host = host;
    project->recent = g_ptr_array_new_with_free_func (g_free);
    project->glyph_closed =
        mc_skin_get ("widget-editor", "tree-closed-char", mc_global.utf8_display ? ">" : "+");
    project->glyph_open =
        mc_skin_get ("widget-editor", "tree-open-char", mc_global.utf8_display ? "\u25bc" : "-");
    host->commands_register (host, PROJECT_KEYMAP_SECTION, N_ ("&Project"), project_commands);
    for (i = 0; i < PROJECT_CMD_COUNT; i++)
        project->commands[i] = host->command_id (host, project_commands[i].name);
    project->service = host->service_register (host, PROJECT_SERVICE, project_call, project, NULL);
    if (host->startup_option (host, "debug") != NULL)
    {
        project_set_root (project, host->startup_option (host, "debug"), TRUE);
        host->call_later (host, project_startup, project);
    }
    return project;
}

/* --------------------------------------------------------------------------------------------- */

static void
project_close (void *data)
{
    project_t *project = (project_t *) data;

    if (project->tree != NULL)
        project->tree->project = NULL;
    if (project->service)
        project->host->service_unregister (project->host, PROJECT_SERVICE);
    project_files_drop (project);
    g_ptr_array_free (project->recent, TRUE);
    g_free (project->root);
    g_free (project->glyph_closed);
    g_free (project->glyph_open);
    g_free (project->open_file);
    g_free (project);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_ep_action_t project_actions[] = {
    { "Open file", project_act_open_file },
    { "Recent files", project_act_recent },
    { "Tree", project_act_tree },
    { "Header or source", project_act_alternate },
    { "Open project", project_act_open_project },
};

static const mc_ep_cmd_menu_entry_t project_menu[] = {
    { MC_EP_MENU_FILE, N_ ("Open file of pro&ject..."), PROJECT_ACT_OPEN_FILE, NULL },
    { MC_EP_MENU_FILE, N_ ("Recent files of project..."), PROJECT_ACT_RECENT, NULL },
    { MC_EP_MENU_FILE, N_ ("Open project..."), PROJECT_ACT_OPEN_PROJECT, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Header or source"), PROJECT_ACT_ALTERNATE, NULL },
    { MC_EP_MENU_PLUGINS, N_ ("Project &tree"), PROJECT_ACT_TREE, "Alt-Shift-T" },
};

static const mc_editor_plugin_t project_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "project",
    .display_name = "Project",
    .flags = MC_EPF_NONE,
    .open = project_open,
    .close = project_close,
    .handle_key = project_handle_key,
    .handle_action = project_handle_action,
    .handle_event = project_handle_event,
    .on_file_open = project_file_open,
    .actions = project_actions,
    .action_count = G_N_ELEMENTS (project_actions),
    .cmd_menu_entries = project_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (project_menu),
};

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
project_get_plugin (void)
{
    return &project_plugin;
}

/* --------------------------------------------------------------------------------------------- */
