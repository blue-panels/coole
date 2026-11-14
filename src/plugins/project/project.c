/*
   The project plugin: the project a file is part of, its files at hand, and their tree.

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

/** \file project.c
 *  \brief Source: the project plugin
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/plugin-service.h"

#include "src/keymap.h"  // keymap_menu_shortcut ()
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/edittree.h"
#include "src/editor/editwindow.h"

#include "project-core.h"
#include "project.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define PROJECT_SERVICE        "project"
#define PROJECT_KEYMAP_SECTION "project"
#define PROJECT_RECENT_MAX     30
// a larger file, or one with a NUL, shows its size at the right of the tree, not its lines
#define PROJECT_LINES_MAX_SIZE (1024 * 1024)
// what the marks of the tree stand for: by the names of the files, the kinds of the symbols
#define PROJECT_MARKS_FILE "project-marks.ini"
// the lines that Find in project lists at most; the letters it starts at
#define PROJECT_FIND_MAX 5000
#define PROJECT_FIND_MIN 2
// the bytes of a line it shows at most, and of a line it looks for the column of the text in
#define PROJECT_FIND_LABEL_MAX  300
#define PROJECT_FIND_COLUMN_MAX 4096
// the rows of its list at most, and of its view of the file around the line of the cursor
#define PROJECT_FIND_LIST_MAX    13
#define PROJECT_FIND_VIEW_BEFORE 3
#define PROJECT_FIND_VIEW_AFTER  5
#define PROJECT_FIND_VIEW        (PROJECT_FIND_VIEW_BEFORE + 1 + PROJECT_FIND_VIEW_AFTER)
#define PROJECT_LIST_MAX         500

/*** file scope type declarations ****************************************************************/

/* the commands of the plugin, in the [project] section of the keymap */
enum
{
    PROJECT_CMD_NONE = -1,
    PROJECT_CMD_OPEN_FILE,
    PROJECT_CMD_RECENT,
    PROJECT_CMD_TREE,
    PROJECT_CMD_ALTERNATE,
    PROJECT_CMD_FIND,
    PROJECT_CMD_COUNT
};

/* what a file or a symbol of the tree is: its mark and its color in [project] of the skin */
typedef enum
{
    PROJECT_ICON_NONE = 0,
    PROJECT_ICON_FILE,
    PROJECT_ICON_SOURCE,
    PROJECT_ICON_HEADER,
    PROJECT_ICON_BUILD,
    PROJECT_ICON_DOC,
    PROJECT_ICON_CONFIG,
    PROJECT_ICON_SCRIPT,
    PROJECT_ICON_BINARY,
    PROJECT_ICON_FUNCTION,
    PROJECT_ICON_PROTOTYPE,
    PROJECT_ICON_TYPE,
    PROJECT_ICON_ENUM,
    PROJECT_ICON_MACRO,
    PROJECT_ICON_VARIABLE,
    PROJECT_ICON_COUNT
} project_icon_t;

/* the actions of the menus */
enum
{
    PROJECT_ACT_OPEN_FILE,
    PROJECT_ACT_RECENT,
    PROJECT_ACT_TREE,
    PROJECT_ACT_ALTERNATE,
    PROJECT_ACT_FIND,
    PROJECT_ACT_OPEN_PROJECT,
    PROJECT_ACT_CLOSE_PROJECT
};

typedef struct
{
    mc_editor_host_t *host;
    char *root;            // NULL till a file is opened
    gboolean root_chosen;  // by the user: the files opened do not change it
    gboolean closed;       // by the user: no project till one is opened
    GPtrArray *files;      // relative to the root, NULL till listed
    gint64 files_time;     // when they were listed
    GPtrArray *recent;     // absolute names, the latest first
    long commands[PROJECT_CMD_COUNT];
    edit_tree_t *tree;      // the window of the tree, NULL when there is none
    mctree_model_t *model;  // and what it shows
    GHashTable *paths;      // the paths of the directories open, NULL for none
    gboolean service;
    // what the list chose, opened once its dialog is gone
    char *open_file;
    long open_line;
    // the outline of the file in front: its symbols under it in the tree, from the plugin ctags
    char *outline_file;
    GPtrArray *outline;   // project_symbol_t, in the order of the lines
    int outline_current;  // the one the cursor is in, -1 for none
    gboolean outline_closed;
    // ctags took long over the outline of the file: made again when it is saved, not at each edit
    gboolean outline_slow;
    // what the tree shows at the right of a file, by its absolute name: project_size_t
    GHashTable *sizes;
    // the marks of the tree and their colors, from the skin while the tree is shown
    char *icons[PROJECT_ICON_COUNT];
    // what a file or a symbol is, from project-marks.ini: "n:name", "e:ext", "s:stem", "k:kind"
    GHashTable *marks;
    int icon_colors[PROJECT_ICON_COUNT];
} project_t;

/* the lines or the size of a file at the right of the tree, kept till the file changes */
typedef struct
{
    gint64 mtime;
    long mtime_ns;  // a change saved within the same second, the size kept, is seen too
    goffset size;
    char *text;
    project_icon_t icon;
} project_size_t;

/* a symbol of the outline */
typedef struct
{
    char *label;
    long line;
    project_icon_t icon;
} project_symbol_t;

/* a node of the tree: its data */
typedef struct
{
    char *name;
    char *rel;  // relative to the root
    gboolean dir;
    int symbol;  // a symbol of the outline: its index; -1 for a file or a directory
} project_node_t;

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

static void project_tree_close (project_t *project);

static void project_tree_rebuild (project_t *project, gboolean keep);
static void project_tree_reveal (project_t *project, const char *file);

/*** file scope variables ************************************************************************/

static const mc_ep_command_t project_commands[PROJECT_CMD_COUNT + 1] = {
    { "ProjectOpenFile", N_ ("Open a file of the project"), "alt-shift-p" },
    { "ProjectRecentFiles", N_ ("Recent files of the project"), "ctrl-e" },
    { "ProjectTree", N_ ("Show or hide the project tree"), "alt-shift-t" },
    { "ProjectAlternate", N_ ("Switch between source and header"), "alt-shift-a" },
    { "ProjectFind", N_ ("Find a text in the files of the project"), "alt-f7" },
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

/* the groups of project-marks.ini and the keys of [project] of the skin, by project_icon_t:
 * "source-char = x", "source = green" */
static const char *const project_icon_names[PROJECT_ICON_COUNT] = {
    NULL,     "file",     "source",    "header", "build", "doc",   "config",   "script",
    "binary", "function", "prototype", "type",   "enum",  "macro", "variable",
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
project_symbol_free (gpointer p)
{
    project_symbol_t *sym = (project_symbol_t *) p;

    g_free (sym->label);
    g_free (sym);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_node_free (gpointer p)
{
    project_node_t *data = (project_node_t *) p;

    g_free (data->name);
    g_free (data->rel);
    g_free (data);
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

/* The files of the project: listed again when they were some seconds ago, one may have come
   since; listing them takes long in a big tree */
static const GPtrArray *
project_files (project_t *project)
{
    const gint64 now = g_get_monotonic_time ();

    if (project->files != NULL && now - project->files_time < 10 * G_USEC_PER_SEC)
        return project->files;
    project_files_drop (project);
    project->files = project_list_files (project->root);
    project->files_time = now;
    return project->files;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the list of the files has @file, of the project */
static gboolean
project_files_have (const project_t *project, const char *file)
{
    const char *rel = project_relative ((project_t *) project, file);
    guint i;

    for (i = 0; rel != NULL && project->files != NULL && i < project->files->len; i++)
        if (strcmp (g_ptr_array_index (project->files, i), rel) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
project_set_root (project_t *project, const char *root, gboolean chosen)
{
    if (root == NULL || g_strcmp0 (project->root, root) == 0)
    {
        // the project of a file opened as a project now: told, as one opened
        if (root != NULL && chosen && !project->root_chosen && project->service)
            project->host->service_emit (
                project->host, PROJECT_SERVICE, "changed",
                g_variant_new_parsed ("{'root': <%s>, 'chosen': <%b>}", project->root, TRUE));
        project->root_chosen = project->root_chosen || chosen;
        return;
    }
    g_free (project->root);
    project->root = g_strdup (root);
    project->root_chosen = chosen;
    project->closed = FALSE;
    project_files_drop (project);
    project_recent_load (project);
    if (project->tree != NULL)
    {
        (void) project_files (project);
        project_tree_rebuild (project, FALSE);
    }
    if (project->service)
        project->host->service_emit (
            project->host, PROJECT_SERVICE, "changed",
            g_variant_new_parsed ("{'root': <%s>, 'chosen': <%b>}", project->root, chosen));
}

/* --------------------------------------------------------------------------------------------- */

/* No project, till one is opened: the files opened do not make one.  The others are told, the
   root empty */
static void
project_unset (project_t *project)
{
    if (project->root == NULL)
        return;
    project_tree_close (project);
    g_free (project->root);
    project->root = NULL;
    project->root_chosen = FALSE;
    project->closed = TRUE;
    project_files_drop (project);
    g_ptr_array_set_size (project->recent, 0);
    if (project->service)
        project->host->service_emit (project->host, PROJECT_SERVICE, "changed",
                                     g_variant_new_parsed ("{'root': <''>, 'chosen': <true>}"));
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
    if (!project->closed
        && (project->root == NULL
            || (!project->root_chosen && project_relative (project, file) == NULL
                && project->recent->len == 0)))
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
    project_tree_reveal (project, file);
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

/* A file by its name under @root when it is there, else as it is */
static const char *
project_relative_to (const char *root, const char *file)
{
    const size_t len = strlen (root);

    if (strncmp (file, root, len) == 0 && file[len] == '/')
        return file + len + 1;
    return file;
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

    // ":42": the file in front, the latest one, at that line
    if (text[0] == ':' && text[1] != '\0' && text[strspn (text + 1, "0123456789") + 1] == '\0')
    {
        if (pick_recent != NULL && pick_recent->len != 0)
        {
            const char *file = g_ptr_array_index (pick_recent, 0);
            const char *rel = project_relative_to (pick_root, file);
            char *label = g_strdup_printf ("%s%s", rel, text);

            project_pick_add (label, file, atol (text + 1));
            g_free (label);
        }
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
project_entry_compare (gconstpointer a, gconstpointer b)
{
    const project_node_t *x = *(project_node_t *const *) a;
    const project_node_t *y = *(project_node_t *const *) b;

    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return g_utf8_collate (x->name, y->name);
}

/* --------------------------------------------------------------------------------------------- */

static void project_outline_update (project_t *project, void *edit, gboolean edited);

/* Whether @rel is the file of the outline, which has symbols */
static gboolean
project_is_outline (const project_t *project, const char *rel)
{
    const char *orel;

    if (project->outline == NULL || project->outline->len == 0)
        return FALSE;
    orel = project_relative ((project_t *) project, project->outline_file);
    return orel != NULL && strcmp (orel, rel) == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* A node of the tree under @parent; @data taken */
static mctree_node_t *
project_node_add (mctree_model_t *model, mctree_node_t *parent, project_node_t *data)
{
    mctree_node_t *node =
        mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, data->name, NULL);

    mctree_node_set_data (node, data, project_node_free);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

/* The symbols of the file of the outline under its node, open unless the user has folded them */
static void
project_tree_add_outline (project_t *project, mctree_model_t *model, mctree_node_t *file)
{
    const project_node_t *f = (const project_node_t *) file->data;
    guint i;

    if (!project_is_outline (project, f->rel))
        return;
    for (i = 0; i < project->outline->len; i++)
    {
        const project_symbol_t *sym = g_ptr_array_index (project->outline, i);
        project_node_t *data = g_new0 (project_node_t, 1);

        data->name = g_strdup (sym->label);
        data->rel = g_strdup (f->rel);
        data->symbol = (int) i;
        (void) project_node_add (model, file, data);
    }
    file->expanded = !project->outline_closed;
}

/* --------------------------------------------------------------------------------------------- */

/* A directory opened, the root for one: what is in it, from the names of the files of the
   project, the directories first, those in it read when they are opened */
static void
project_tree_children (mctree_model_t *model, mctree_node_t *node, void *user_data)
{
    project_t *project = (project_t *) user_data;
    const char *dir = node->data != NULL ? ((const project_node_t *) node->data)->rel : "";
    const gsize dir_len = strlen (dir);
    const GPtrArray *files = project->files;
    GPtrArray *children = g_ptr_array_new ();
    GHashTable *seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    guint i;

    mctree_node_set_lazy (node, FALSE);
    for (i = 0; files != NULL && i < files->len; i++)
    {
        const char *rel = g_ptr_array_index (files, i);
        const char *rest, *slash;
        project_node_t *data;

        if (dir_len != 0 && (strncmp (rel, dir, dir_len) != 0 || rel[dir_len] != '/'))
            continue;
        rest = dir_len != 0 ? rel + dir_len + 1 : rel;
        slash = strchr (rest, '/');
        data = g_new0 (project_node_t, 1);
        data->symbol = -1;
        if (slash == NULL)
        {
            data->name = g_strdup (rest);
            data->rel = g_strdup (rel);
        }
        else
        {
            data->name = g_strndup (rest, (gsize) (slash - rest));
            data->rel = g_strndup (rel, (gsize) (slash - rel));
            data->dir = TRUE;
            if (g_hash_table_contains (seen, data->rel))
            {
                project_node_free (data);
                continue;
            }
            g_hash_table_add (seen, g_strdup (data->rel));
        }
        g_ptr_array_add (children, data);
    }
    g_ptr_array_sort (children, project_entry_compare);

    for (i = 0; i < children->len; i++)
    {
        project_node_t *data = g_ptr_array_index (children, i);
        mctree_node_t *child = project_node_add (model, node, data);

        if (data->dir)
        {
            mctree_node_set_lazy (child, TRUE);
            // open before: read and open now, the first time it is seen, under a parent closed too
            if (project->paths != NULL)
            {
                char *path = mctree_node_path (child);

                if (g_hash_table_contains (project->paths, path))
                    mctree_node_expand (model, child);
                g_free (path);
            }
        }
        else
            project_tree_add_outline (project, model, child);
    }
    g_ptr_array_free (children, TRUE);
    g_hash_table_destroy (seen);
}

/* --------------------------------------------------------------------------------------------- */

/* The title of the frame: where the project is, its whole root, the home as ~ */
static char *
project_tree_title (const project_t *project)
{
    const char *home = g_get_home_dir ();
    const size_t home_len = home != NULL ? strlen (home) : 0;
    const char *root = project->root;

    if (root == NULL)
        return g_strdup (_ ("Project"));
    if (home_len > 1 && strncmp (root, home, home_len) == 0
        && (root[home_len] == '/' || root[home_len] == '\0'))
        return g_strdup_printf ("~%s", root + home_len);
    return g_strdup (root);
}

/* --------------------------------------------------------------------------------------------- */

/* The tree made again from the files and the outline: the directories open, and the node of the
   cursor, stay so when @keep */
static void
project_tree_rebuild (project_t *project, gboolean keep)
{
    edit_tree_t *tree = project->tree;
    mctree_model_t *was = project->model;
    const mctree_node_t *current = keep && tree != NULL ? edit_tree_current (tree) : NULL;
    char *cursor = current != NULL ? mctree_node_path (current) : NULL;
    // what was open, under a directory never opened too
    GHashTable *paths = keep && was != NULL ? mctree_model_kept_paths (was, project->paths) : NULL;
    char *title;

    if (tree == NULL)
    {
        g_free (cursor);
        if (paths != NULL)
            g_hash_table_destroy (paths);
        return;
    }
    // the outline folded by the user stays so, that of another file comes open
    if (was != NULL && project->outline_file != NULL)
    {
        const char *rel = project_relative (project, project->outline_file);
        char *path = rel != NULL ? g_strdelimit (g_strdup (rel), "/", MCTREE_PATH_SEP[0]) : NULL;
        const mctree_node_t *file = path != NULL ? mctree_model_find_path (was, path) : NULL;

        if (file != NULL && file->children != NULL && file->children->len > 0)
            project->outline_closed = !file->expanded;
        g_free (path);
    }
    if (project->files == NULL)
        (void) project_files (project);
    project->model = mctree_model_new (G_MAXSIZE);
    (void) mctree_model_add_node (project->model, NULL, MCTREE_NODE_ROOT, "", NULL);
    project->model->root->expanded = TRUE;
    project->model->children = project_tree_children;
    project->model->children_data = project;
    if (project->paths != NULL)
        g_hash_table_destroy (project->paths);
    project->paths = paths;
    // the directories open again as they are read
    project_tree_children (project->model, project->model->root, project);
    edit_tree_set_model (tree, project->model);
    if (cursor != NULL)
        (void) mctree_view_goto_path (tree->view, cursor);
    title = project_tree_title (project);
    edit_tree_set_title (tree, title);
    g_free (title);
    mctree_model_free (was);
    g_free (cursor);
}

/* --------------------------------------------------------------------------------------------- */

/* Open the directories down to a file of the project and put the cursor on it */
static void
project_tree_reveal (project_t *project, const char *file)
{
    const char *rel = project_relative (project, file);
    mctree_node_t *node;
    const char *at;
    char *path;

    if (project->tree == NULL || project->model == NULL || rel == NULL)
        return;
    node = project->model->root;
    for (at = rel; node != NULL;)
    {
        const char *slash = strchr (at, '/');
        const gsize len = slash != NULL ? (gsize) (slash - at) : strlen (at);
        mctree_node_t *next = NULL;
        guint i;

        for (i = 0; node->children != NULL && i < node->children->len && next == NULL; i++)
        {
            mctree_node_t *child = g_ptr_array_index (node->children, i);

            if (strlen (child->key) == len && strncmp (child->key, at, len) == 0)
                next = child;
        }
        node = next;
        if (node == NULL || slash == NULL)
            break;
        mctree_node_expand (project->model, node);
        at = slash + 1;
    }
    path = g_strdelimit (g_strdup (rel), "/", MCTREE_PATH_SEP[0]);
    mctree_view_rebuild (project->tree->view);
    {
        // the cursor on a symbol of the file stays there: the click on it shows the file
        const mctree_node_t *current = edit_tree_current (project->tree);
        char *at_path = current != NULL ? mctree_node_path (current) : NULL;
        const gsize path_len = strlen (path);

        if (at_path == NULL || strncmp (at_path, path, path_len) != 0
            || at_path[path_len] != MCTREE_PATH_SEP[0])
            (void) mctree_view_goto_path (project->tree->view, path);
        g_free (at_path);
    }
    g_free (path);
    widget_draw (WIDGET (project->tree));
}

/* --------------------------------------------------------------------------------------------- */

/* What makes a file or a symbol one of the groups of project-marks.ini: those of coole, then
   those of the user over them */
static GHashTable *
project_marks (project_t *project)
{
    static const char *const keys[] = { "names", "extensions", "stems", "kinds" };
    const char *dirs[] = { g_getenv ("COOLE_MISC_DIR"), mc_global.sysconfig_dir,
                           mc_global.share_data_dir };
    mc_config_t *cfg;
    char *path;
    int i;
    guint d;

    if (project->marks != NULL)
        return project->marks;
    project->marks = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    cfg = mc_config_init (NULL, TRUE);
    for (d = 0; d < G_N_ELEMENTS (dirs); d++)
        if (dirs[d] != NULL)
        {
            path = g_build_filename (dirs[d], PROJECT_MARKS_FILE, (char *) NULL);
            if (mc_config_read_file (cfg, path, TRUE, FALSE))
                d = G_N_ELEMENTS (dirs);
            g_free (path);
        }
    path = g_build_filename (mc_config_get_path (), PROJECT_MARKS_FILE, (char *) NULL);
    (void) mc_config_read_file (cfg, path, TRUE, TRUE);
    g_free (path);
    for (i = 1; i < PROJECT_ICON_COUNT; i++)
    {
        guint k;

        for (k = 0; k < G_N_ELEMENTS (keys); k++)
        {
            gsize n, j;
            gchar **list = mc_config_get_string_list (cfg, project_icon_names[i], keys[k], &n);

            for (j = 0; j < n; j++)
            {
                char *word = g_strstrip (list[j]);

                // an extension in any case: CALC.C is calc.c
                if (k == 1)
                    word = g_ascii_strdown (word, -1);
                else
                    word = g_strdup (word);
                if (*word != '\0')
                    g_hash_table_insert (project->marks,
                                         g_strdup_printf ("%c:%s", keys[k][0], word),
                                         GINT_TO_POINTER (i));
                g_free (word);
            }
            g_strfreev (list);
        }
    }
    mc_config_deinit (cfg);
    return project->marks;
}

/* --------------------------------------------------------------------------------------------- */

/* The group of project-marks.ini that has @word under @key ('n', 'e', 's' or 'k') */
static project_icon_t
project_mark (project_t *project, char key, const char *word)
{
    char *k = g_strdup_printf ("%c:%s", key, word);
    const gpointer icon = g_hash_table_lookup (project_marks (project), k);

    g_free (k);
    return (project_icon_t) GPOINTER_TO_INT (icon);
}

/* --------------------------------------------------------------------------------------------- */

/* What a file is by its name: its whole name, its extension, its name before the extension
   (README.ru); @script: it starts with "#!" */
static project_icon_t
project_file_icon (project_t *project, const char *rel, gboolean script)
{
    const char *base = strrchr (rel, '/');
    const char *dot;
    project_icon_t icon;

    base = base != NULL ? base + 1 : rel;
    dot = strrchr (base, '.');
    icon = project_mark (project, 'n', base);
    if (icon == PROJECT_ICON_NONE && dot != NULL && dot != base)
    {
        char *ext = g_ascii_strdown (dot + 1, -1);
        char *stem = g_strndup (base, (gsize) (dot - base));

        icon = project_mark (project, 'e', ext);
        if (icon == PROJECT_ICON_NONE)
            icon = project_mark (project, 's', stem);
        g_free (stem);
        g_free (ext);
    }
    if (icon == PROJECT_ICON_NONE)
        icon = script ? PROJECT_ICON_SCRIPT : PROJECT_ICON_FILE;
    return icon;
}

/* --------------------------------------------------------------------------------------------- */

/* What a symbol is by the kind ctags gives it; a variable when project-marks.ini does not say */
static project_icon_t
project_symbol_icon (project_t *project, const char *kind)
{
    const project_icon_t icon = project_mark (project, 'k', kind);

    return icon != PROJECT_ICON_NONE ? icon : PROJECT_ICON_VARIABLE;
}

/* --------------------------------------------------------------------------------------------- */

/* The marks of the tree and their colors from the skin; none by default, as UTF-8 may not be
   there */
static void
project_icons_load (project_t *project)
{
    int i;

    for (i = 1; i < PROJECT_ICON_COUNT; i++)
    {
        char *key = g_strconcat (project_icon_names[i], "-char", (char *) NULL);
        char *fg = mc_skin_get ("project", project_icon_names[i], "");

        g_free (project->icons[i]);
        project->icons[i] = mc_skin_get ("project", key, "");
        project->icon_colors[i] =
            fg != NULL && *fg != '\0' ? mc_skin_color_on ("editor", "_default_", fg) : -1;
        g_free (fg);
        g_free (key);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
project_icons_free (project_t *project)
{
    int i;

    for (i = 0; i < PROJECT_ICON_COUNT; i++)
        g_clear_pointer (&project->icons[i], g_free);
    g_clear_pointer (&project->marks, g_hash_table_destroy);
}

/* --------------------------------------------------------------------------------------------- */

static void
project_size_free (gpointer p)
{
    project_size_t *size = (project_size_t *) p;

    g_free (size->text);
    g_free (size);
}

/* --------------------------------------------------------------------------------------------- */

/* The size of a file the way the tree shows it: 512B, 12K, 3M */
static char *
project_size_text (goffset size)
{
    if (size < 1024)
        return g_strdup_printf ("%dB", (int) size);
    if (size < 1024 * 1024)
        return g_strdup_printf ("%" G_GINT64_FORMAT "K", (gint64) ((size + 1023) / 1024));
    return g_strdup_printf ("%" G_GINT64_FORMAT "M",
                            (gint64) ((size + 1024 * 1024 - 1) / (1024 * 1024)));
}

/* --------------------------------------------------------------------------------------------- */

/* The nanoseconds of the time a file was changed, where the system keeps them */
static long
project_mtime_ns (const GStatBuf *st)
{
#if defined(HAVE_STRUCT_STAT_ST_MTIM)
    return (long) st->st_mtim.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
    return (long) st->st_mtimespec.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
    return (long) st->st_mtimensec;
#else
    (void) st;
    return 0;
#endif
}

/* --------------------------------------------------------------------------------------------- */

/* What the tree shows of a file: its lines at the right, or its size when it is large or not
   text, and what it is; read again only when its time or its size changes.  NULL when it cannot
   be read */
static const project_size_t *
project_file_size (project_t *project, const char *rel)
{
    char *file = g_build_filename (project->root, rel, (char *) NULL);
    GStatBuf st;
    project_size_t *size;
    char *contents = NULL;
    gsize len = 0;
    gboolean read = FALSE;

    if (g_stat (file, &st) != 0 || !S_ISREG (st.st_mode))
    {
        g_free (file);
        return NULL;
    }
    if (project->sizes == NULL)
        project->sizes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, project_size_free);
    size = g_hash_table_lookup (project->sizes, file);
    if (size != NULL && size->mtime == (gint64) st.st_mtime
        && size->mtime_ns == project_mtime_ns (&st) && size->size == (goffset) st.st_size)
    {
        g_free (file);
        return size;
    }
    if (size == NULL)
    {
        size = g_new0 (project_size_t, 1);
        g_hash_table_insert (project->sizes, g_strdup (file), size);
    }
    size->mtime = (gint64) st.st_mtime;
    size->mtime_ns = project_mtime_ns (&st);
    size->size = (goffset) st.st_size;
    g_free (size->text);
    size->text = NULL;
    if (st.st_size <= PROJECT_LINES_MAX_SIZE)
        read = g_file_get_contents (file, &contents, &len, NULL);
    if (read && memchr (contents, '\0', len) == NULL)
    {
        gsize i, lines = 0;

        for (i = 0; i < len; i++)
            if (contents[i] == '\n')
                lines++;
        // the last line without a newline is a line too
        if (len > 0 && contents[len - 1] != '\n')
            lines++;
        size->text = g_strdup_printf ("%" G_GSIZE_FORMAT, lines);
        size->icon =
            project_file_icon (project, rel, len > 1 && contents[0] == '#' && contents[1] == '!');
    }
    else
    {
        size->text = project_size_text ((goffset) st.st_size);
        // a large file, or one that cannot be read, is still what its name says; a small one
        // with a NUL is binary
        size->icon = read ? PROJECT_ICON_BINARY : project_file_icon (project, rel, FALSE);
    }
    g_free (contents);
    g_free (file);
    return size;
}

/* --------------------------------------------------------------------------------------------- */

/* What project_file_size () last found of a file, without looking at it again */
static const project_size_t *
project_file_size_known (const project_t *project, const char *rel)
{
    char *file;
    const project_size_t *size;

    if (project->sizes == NULL)
        return NULL;
    file = g_build_filename (project->root, rel, (char *) NULL);
    size = g_hash_table_lookup (project->sizes, file);
    g_free (file);
    return size;
}

/* --------------------------------------------------------------------------------------------- */

/* The row of a node: a directory with a slash after its name; a file with its lines at the
   right, or its size */
static void
project_tree_row (edit_tree_t *tree, const mctree_node_t *node, GString *name, GString *info)
{
    project_t *project = (project_t *) tree->data;
    const project_node_t *data = (const project_node_t *) node->data;

    g_string_append (name, node->key);
    if (data != NULL && data->dir)
        g_string_append_c (name, '/');
    else if (project != NULL && project->root != NULL && data != NULL && data->symbol < 0
             && data->rel != NULL)
    {
        const project_size_t *size = project_file_size (project, data->rel);

        if (size != NULL)
            g_string_append (info, size->text);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The mark of a file or of a symbol, from [project] of the skin; none for a directory */
static void
project_tree_icon (edit_tree_t *tree, const mctree_node_t *node, GString *icon, int *color)
{
    project_t *project = (project_t *) tree->data;
    const project_node_t *data = (const project_node_t *) node->data;
    project_icon_t kind = PROJECT_ICON_NONE;

    if (project == NULL || project->root == NULL || data == NULL || data->dir)
        return;
    if (data->symbol >= 0)
    {
        if (project->outline != NULL && (guint) data->symbol < project->outline->len)
            kind = ((const project_symbol_t *) g_ptr_array_index (project->outline, data->symbol))
                       ->icon;
    }
    else if (data->rel != NULL)
    {
        // the row of the node, drawn just before, looked at the file: no stat again
        const project_size_t *size = project_file_size_known (project, data->rel);

        kind = size != NULL ? size->icon : project_file_icon (project, data->rel, FALSE);
    }
    if (kind == PROJECT_ICON_NONE || project->icons[kind] == NULL
        || project->icons[kind][0] == '\0')
        return;
    if (strcmp (project->icons[kind], "%e") == 0)
    {
        // the first letter of the extension: c for calc.c, h for calc.h, p for calc.py
        const char *base = data->symbol < 0 && data->rel != NULL ? strrchr (data->rel, '/') : NULL;
        const char *dot;

        if (data->symbol >= 0 || data->rel == NULL)
            return;
        base = base != NULL ? base + 1 : data->rel;
        dot = strrchr (base, '.');
        if (dot == NULL || dot == base || !g_ascii_isalnum (dot[1]))
            return;
        g_string_append_c (icon, g_ascii_tolower (dot[1]));
    }
    else
        g_string_append (icon, project->icons[kind]);
    *color = project->icon_colors[kind];
}

/* --------------------------------------------------------------------------------------------- */

/* Enter or a double click: a file opened, a symbol at its line; a directory opens or closes */
static gboolean
project_tree_activate (edit_tree_t *tree, mctree_node_t *node)
{
    project_t *project = (project_t *) tree->data;
    const project_node_t *data = (const project_node_t *) node->data;

    if (project == NULL || data == NULL || data->dir)
        return FALSE;
    if (data->symbol >= 0)
    {
        // the line of the symbol, in the window of the file
        const project_symbol_t *sym = g_ptr_array_index (project->outline, data->symbol);

        (void) project->host->show_location (project->host, project->outline_file, sym->line);
    }
    else
    {
        char *file = g_build_filename (project->root, data->rel, (char *) NULL);

        (void) project->host->open_file (project->host, file);
        g_free (file);
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The file in front stands out */
static gboolean
project_tree_bold (edit_tree_t *tree, const mctree_node_t *node)
{
    const project_t *project = (const project_t *) tree->data;
    const project_node_t *data = (const project_node_t *) node->data;
    void *top;
    char *current;
    const char *rel;
    gboolean bold;

    if (project == NULL || data == NULL || data->dir)
        return FALSE;
    if (data->symbol >= 0)
        return FALSE;
    top = project->host->window_top_file (project->host);
    current = top != NULL ? project->host->get_current_file (project->host, top) : NULL;
    rel = project_relative ((project_t *) project, current);
    bold = rel != NULL && strcmp (rel, data->rel) == 0;
    g_free (current);
    return bold;
}

/* --------------------------------------------------------------------------------------------- */

/* The symbol the cursor of the file is in, by the color of its name: treecurrent in [editor] of
   the skin, yellow on the background of the editor without it; a bar is the file's and the
   cursor's */
static void
project_tree_colors (edit_tree_t *tree, const mctree_node_t *node, int *name, int *info)
{
    const project_t *project = (const project_t *) tree->data;
    const project_node_t *data = (const project_node_t *) node->data;

    (void) info;
    if (project == NULL || data == NULL || data->dir || data->symbol < 0
        || data->symbol != project->outline_current)
        return;
    *name = mc_skin_color_is_set ("editor", "treecurrent")
        ? mc_skin_color_get ("editor", "treecurrent")
        : mc_skin_color_on ("editor", "_default_", "yellow");
}

/* --------------------------------------------------------------------------------------------- */

static void
project_tree_closed (edit_tree_t *tree)
{
    project_t *project = (project_t *) tree->data;

    if (project == NULL)
        return;
    project->tree = NULL;
    g_clear_pointer (&project->model, mctree_model_free);
    g_clear_pointer (&project->paths, g_hash_table_destroy);
}

/* --------------------------------------------------------------------------------------------- */

static const edit_tree_ops_t project_tree_ops = {
    .row = project_tree_row,
    .icon = project_tree_icon,
    .activate = project_tree_activate,
    .bold = project_tree_bold,
    .colors = project_tree_colors,
    .closed = project_tree_closed,
};

/* --------------------------------------------------------------------------------------------- */

/* The tree closed, as F10 does */
static void
project_tree_close (project_t *project)
{
    if (project->tree != NULL)
        (void) send_message (WIDGET (project->tree), NULL, MSG_ACTION, CK_Close, NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* Alt-1: the tree comes, takes the focus, and goes */
static void
project_tree_toggle (project_t *project)
{
    WRect area;
    void *top_file;
    char *title;

    if (project->tree != NULL)
    {
        if (project->host->window_current (project->host) == project->tree)
            project_tree_close (project);
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
    title = project_tree_title (project);
    project_icons_load (project);
    project->tree = edit_tree_new (title, &project_tree_ops, project);
    g_free (title);
    project->host->window_add (project->host, project->tree);
    // the column at the right, the panel of the debugger under it
    project->host->window_dock_right (project->host, project->tree,
                                      CLAMP (area.cols * 30 / 100, 24, 50));
    (void) project_files (project);
    project_tree_rebuild (project, FALSE);
    if (top_file != NULL)
    {
        char *file = project->host->get_current_file (project->host, top_file);

        project_tree_reveal (project, file);
        project_outline_update (project, top_file, FALSE);
        g_free (file);
    }
}

/* --------------------------------------------------------------------------------------------- */
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

/* A line found by Find in project */
typedef struct
{
    char *rel;
    long line;
    long column;  // in bytes, of the text found
} project_found_t;

static void
project_found_free (gpointer p)
{
    project_found_t *f = (project_found_t *) p;

    g_free (f->rel);
    g_free (f);
}

/* --------------------------------------------------------------------------------------------- */

/* Where @text is in @line, in bytes, 0 when it is not; @any_case: in any case, of any script */
static long
project_find_column (const char *line, const char *text, gboolean any_case)
{
    const char *at;
    char *folded;
    const char *p;
    long column = 0;

    if (!any_case || !g_utf8_validate (line, -1, NULL) || !g_utf8_validate (text, -1, NULL))
    {
        at = any_case ? NULL : strstr (line, text);
        if (at == NULL && any_case)
        {
            char *l = g_ascii_strdown (line, -1), *t = g_ascii_strdown (text, -1);

            at = strstr (l, t);
            at = at != NULL ? line + (at - l) : NULL;
            g_free (l);
            g_free (t);
        }
        return at != NULL ? (long) (at - line) : 0;
    }
    // the folded text is compared with the folded line from each of its letters on
    folded = g_utf8_casefold (text, -1);
    for (p = line; *p != '\0' && p - line < PROJECT_FIND_COLUMN_MAX; p = g_utf8_next_char (p))
    {
        const gsize n = strlen (p);
        char *here = g_utf8_casefold (p, (gssize) MIN (n, strlen (text) * 4 + 4));
        const gboolean hit = g_str_has_prefix (here, folded);

        g_free (here);
        if (hit)
        {
            column = (long) (p - line);
            break;
        }
    }
    g_free (folded);
    return column;
}

/* --------------------------------------------------------------------------------------------- */

/* the dialog of Find in project while it runs: the text, the lines found, a view of the one the
   cursor is on */
static project_t *find_project = NULL;
static WInput *find_input = NULL;
static WListbox *find_list = NULL;
static WListbox *find_view = NULL;
static char *find_last = NULL;        // the text the list is of
static GPtrArray *find_found = NULL;  // project_found_t, what the items of the list go to
static const project_found_t *find_shown = NULL;  // the line find_view shows
static WDialog *find_dlg = NULL;
// grep, running while the dialog does: its output read as it comes, not waited for
static GPid find_pid = 0;
static int find_fd = -1;
static GString *find_pending = NULL;  // what grep wrote after the last whole line
static char *find_files = NULL;       // the files of the project by NUL, for xargs -0
static gboolean find_failed = FALSE;  // grep could not be run
static gboolean find_any_case = FALSE;

/* --------------------------------------------------------------------------------------------- */

/* The lines of the file around the one of the cursor of the list, that one current */
static void
project_find_view (void)
{
    const project_found_t *f = NULL;
    char *file;
    FILE *fp;

    listbox_get_current (find_list, NULL, (void **) &f);
    if (f == find_shown)
        return;
    find_shown = f;
    listbox_remove_list (find_view);
    file = f != NULL ? g_build_filename (find_project->root, f->rel, (char *) NULL) : NULL;
    // read up to the last line shown, not the whole file
    fp = file != NULL ? fopen (file, "r") : NULL;
    if (fp != NULL)
    {
        const long first = MAX (1, f->line - PROJECT_FIND_VIEW_BEFORE);
        const long last = f->line + PROJECT_FIND_VIEW_AFTER;
        GString *text = g_string_new (NULL);
        long n = 1;
        int c = 0;

        while (n <= last && c != EOF)
        {
            g_string_truncate (text, 0);
            while ((c = fgetc (fp)) != EOF && c != '\n')
                if (n >= first && text->len < PROJECT_FIND_LABEL_MAX)
                    g_string_append_c (text, c == '\t' ? ' ' : (char) c);
            if (n >= first && (c != EOF || text->len > 0))
                listbox_add_item (find_view, LISTBOX_APPEND_AT_END, 0, text->str, NULL, FALSE);
            n++;
        }
        g_string_free (text, TRUE);
        fclose (fp);
        listbox_set_current (find_view, (int) (f->line - first));
    }
    g_free (file);
    widget_draw (WIDGET (find_view));
}

/* --------------------------------------------------------------------------------------------- */

static void
project_find_child_setup (gpointer data)
{
    (void) data;
    (void) setsid ();
}

/* --------------------------------------------------------------------------------------------- */

/* Whether @text is looked for in any case: it has no capital letter, of any script, as grep -i
   folds them all */
static gboolean
project_find_any_case (const char *text)
{
    const char *p;

    if (!g_utf8_validate (text, -1, NULL))
    {
        for (p = text; *p != '\0'; p++)
            if (g_ascii_isupper (*p))
                return FALSE;
        return TRUE;
    }
    for (p = text; *p != '\0'; p = g_utf8_next_char (p))
        if (g_unichar_isupper (g_utf8_get_char (p)))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The file of the names of the files of the project, separated by NUL, for xargs -0: no shell
   quoting, no limit of the command line; written once for the dialog.  NULL when it cannot be */
static const char *
project_find_files (void)
{
    const GPtrArray *files;
    FILE *list;
    int fd;
    guint i;

    if (find_files != NULL)
        return find_files;
    fd = g_file_open_tmp ("coole-find-XXXXXX", &find_files, NULL);
    if (fd < 0)
        return NULL;
    list = fdopen (fd, "w");
    if (list == NULL)
    {
        close (fd);
        unlink (find_files);
        g_clear_pointer (&find_files, g_free);
        return NULL;
    }
    files = project_files (find_project);
    for (i = 0; files != NULL && i < files->len; i++)
    {
        const char *rel = (const char *) g_ptr_array_index (files, i);

        // what coole keeps of the project, its tags among them, is no text of it
        if (strncmp (rel, ".coole/", 7) == 0)
            continue;
        fputs (rel, list);
        fputc ('\0', list);
    }
    fclose (list);
    return find_files;
}

/* --------------------------------------------------------------------------------------------- */

/* grep stopped, it and the xargs that runs it, its output no longer read */
static void
project_find_stop (void)
{
    if (find_fd >= 0)
    {
        delete_select_channel (find_fd);
        close (find_fd);
        find_fd = -1;
    }
    if (find_pid != 0)
    {
        (void) kill (-find_pid, SIGTERM);
        (void) waitpid (find_pid, NULL, 0);
        g_spawn_close_pid (find_pid);
        find_pid = 0;
    }
    if (find_pending != NULL)
        g_string_truncate (find_pending, 0);
}

/* --------------------------------------------------------------------------------------------- */

/* The title of the dialog: the lines found so far, and whether grep still looks */
static void
project_find_title (void)
{
    const guint n = find_found->len;
    char *title;

    if (find_failed)
        title = g_strdup (_ ("Find in project: cannot run grep"));
    else if (find_last == NULL || str_length (find_last) < PROJECT_FIND_MIN)
        title = g_strdup (_ ("Find in project"));
    else if (n >= PROJECT_FIND_MAX)
        title = g_strdup_printf (_ ("Find in project: the first %u lines"), n);
    else if (find_fd >= 0)
        title = g_strdup_printf (
            ngettext ("Find in project: %u line...", "Find in project: %u lines...", n), n);
    else
        title = g_strdup_printf (
            ngettext ("Find in project: %u line", "Find in project: %u lines", n), n);
    frame_set_title (FRAME (find_dlg->bg), title);
    g_free (title);
}

/* --------------------------------------------------------------------------------------------- */

/* A line of grep, "name NUL number:text", added to the list */
static void
project_find_add (char *line, gsize len)
{
    char *name_end, *colon, *rest;
    long number;
    project_found_t *f;

    name_end = memchr (line, '\0', len);
    if (name_end == NULL)
        return;
    colon = strchr (name_end + 1, ':');
    if (colon == NULL)
        return;
    *colon = '\0';
    number = strtol (name_end + 1, NULL, 10);
    if (number <= 0)
        return;
    rest = colon + 1;
    f = g_new (project_found_t, 1);
    f->rel = g_strdup (line);
    f->line = number;
    f->column = project_find_column (rest, find_last, find_any_case);
    g_ptr_array_add (find_found, f);
    // the line as it shows: no indent, tabs as spaces, a long one cut
    g_strdelimit (rest, "\t", ' ');
    rest = g_strchug (rest);
    if (strlen (rest) > PROJECT_FIND_LABEL_MAX)
    {
        char *cut = rest + PROJECT_FIND_LABEL_MAX;

        if (g_utf8_validate (rest, -1, NULL))
            cut = g_utf8_find_prev_char (rest, cut + 1);
        *cut = '\0';
    }
    listbox_add_item_take (find_list, LISTBOX_APPEND_AT_END, 0,
                           g_strdup_printf ("%s:%ld: %s", line, number, rest), f, FALSE);
    // the first line found: the cursor on it, the file around it shown
    if (find_found->len == 1)
    {
        listbox_select_first (find_list);
        project_find_view ();
    }
}

/* --------------------------------------------------------------------------------------------- */

/* grep has written: its whole lines go to the list; at its end, or at PROJECT_FIND_MAX lines, it
   is done */
static int
project_find_ready (int fd, void *data)
{
    char buf[8192];
    const ssize_t n = read (fd, buf, sizeof (buf));
    const char *nl;

    (void) data;
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    if (n > 0)
        g_string_append_len (find_pending, buf, n);
    while (find_found->len < PROJECT_FIND_MAX
           && (nl = memchr (find_pending->str, '\n', find_pending->len)) != NULL)
    {
        const gsize len = (gsize) (nl - find_pending->str);
        // copied whole: a NUL is in it, after the name
        char *line = g_malloc (len + 1);

        memcpy (line, find_pending->str, len);
        line[len] = '\0';
        project_find_add (line, len);
        g_free (line);
        g_string_erase (find_pending, 0, (gssize) len + 1);
    }
    if (n <= 0 && find_pending->len > 0 && find_found->len < PROJECT_FIND_MAX)
    {
        char *line = g_malloc (find_pending->len + 1);

        memcpy (line, find_pending->str, find_pending->len + 1);
        project_find_add (line, find_pending->len);
        g_free (line);
    }
    if (n <= 0 || find_found->len >= PROJECT_FIND_MAX)
        project_find_stop ();
    project_find_title ();
    widget_draw (WIDGET (find_dlg));
    widget_update_cursor (WIDGET (find_dlg));
    tty_refresh ();
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The list made again for the text of the line, when it is another one: grep started for it, from
   PROJECT_FIND_MIN letters on, the one before stopped */
static void
project_find_refill (void)
{
    const char *text = input_get_ctext (find_input);
    const char *files;
    char *script;
    char *argv[7];
    int out = -1;

    if (find_last != NULL && strcmp (find_last, text) == 0)
        return;
    project_find_stop ();
    g_free (find_last);
    find_last = g_strdup (text);
    listbox_remove_list (find_list);
    g_ptr_array_set_size (find_found, 0);
    // the view of the list before is gone with it
    listbox_remove_list (find_view);
    find_shown = NULL;
    find_failed = FALSE;
    if (str_length (text) >= PROJECT_FIND_MIN)
    {
        files = project_find_files ();
        find_any_case = project_find_any_case (text);
        /* -F: the text as it is, no pattern; -I: binary files skipped; -H: the name with one file
           too; -Z: a NUL after the name, which may have a colon in it; the text and the list as
           arguments of sh, no quoting */
        script = g_strdup_printf ("exec xargs -0 grep -n -H -Z -I -F %s-e \"$1\" -- < \"$2\"",
                                  find_any_case ? "-i " : "");
        argv[0] = (char *) "/bin/sh";
        argv[1] = (char *) "-c";
        argv[2] = script;
        argv[3] = (char *) "sh";
        argv[4] = (char *) text;
        argv[5] = (char *) files;
        argv[6] = NULL;
        // a session of its own: xargs and its greps stopped as a whole
        if (files == NULL
            || !g_spawn_async_with_pipes (find_project->root, argv, NULL,
                                          G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL,
                                          project_find_child_setup, NULL, &find_pid, NULL, &out,
                                          NULL, NULL))
        {
            find_failed = TRUE;
            find_pid = 0;
        }
        else
        {
            find_fd = out;
            (void) fcntl (out, F_SETFL, fcntl (out, F_GETFL) | O_NONBLOCK);
            add_select_channel (out, project_find_ready, NULL);
        }
        g_free (script);
    }
    project_find_title ();
    widget_draw (WIDGET (find_dlg));
}

/* --------------------------------------------------------------------------------------------- */

/* Enter typed before grep found a line for the text: it is waited for, the first line or its end */
static void
project_find_wait (void)
{
    if (find_fd < 0)
        return;
    (void) fcntl (find_fd, F_SETFL, fcntl (find_fd, F_GETFL) & ~O_NONBLOCK);
    while (find_fd >= 0 && find_found->len == 0)
        (void) project_find_ready (find_fd, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
project_find_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
    {
        cb_ret_t ret = dlg_default_callback (w, sender, msg, parm, data);

        widget_select (WIDGET (find_input));
        return ret;
    }
    case MSG_KEY:
        // the keys that go through the list, the focus staying in the line
        if (parm == KEY_UP || parm == KEY_DOWN || parm == KEY_PPAGE || parm == KEY_NPAGE)
        {
            send_message (WIDGET (find_list), w, MSG_KEY, parm, data);
            return MSG_HANDLED;
        }
        return dlg_default_callback (w, sender, msg, parm, data);
    case MSG_NOTIFY:
        if (sender != WIDGET (find_list))
            return MSG_NOT_HANDLED;
        // Enter in the list, the focus there after a click: the line chosen
        if (parm == CK_Enter)
        {
            DIALOG (w)->ret_value = B_ENTER;
            dlg_close (DIALOG (w));
            return MSG_HANDLED;
        }
        // its cursor moved
        if (parm == 0)
        {
            project_find_view ();
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;
    case MSG_POST_KEY:
        // grep runs once the typing stops, not at each letter
        if (strcmp (find_last != NULL ? find_last : "", input_get_ctext (find_input)) != 0)
            widget_idle (w, TRUE);
        return MSG_HANDLED;
    case MSG_IDLE:
        widget_idle (w, FALSE);
        project_find_refill ();
        return MSG_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static lcback_ret_t
project_find_activate (WListbox *l)
{
    (void) l;
    return LISTBOX_DONE;
}

/* --------------------------------------------------------------------------------------------- */

/* Find in project: the lines of the files of the project that have the text of the line, the word
   of the cursor first, listed as it is typed, the file around the one of the cursor under them;
   Enter goes to the one chosen */
static void
project_find (project_t *project, void *edit)
{
    char *word;
    WDialog *dlg;
    int dlg_h, dlg_w, list_h;
    const project_found_t *chosen = NULL;

    if (project->root == NULL)
    {
        project->host->message (project->host, D_NORMAL, _ ("Project"),
                                _ ("Open a file of the project first."));
        return;
    }
    if (edit == NULL)
        edit = project->host->window_top_file (project->host);
    word = edit != NULL && project->host->get_cursor_word != NULL
        ? project->host->get_cursor_word (project->host, edit)
        : NULL;

    // the line, the list, a line, the view, a line, the buttons, the frame
    dlg_w = COLS - 4;
    list_h = MAX (4, MIN (PROJECT_FIND_LIST_MAX, LINES - 2 - PROJECT_FIND_VIEW - 7));
    dlg_h = list_h + PROJECT_FIND_VIEW + 7;
    dlg = dlg_create (TRUE, MAX (0, (LINES - dlg_h) / 2), (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, project_find_callback, NULL,
                      "[Project]", _ ("Find in project"));
    dlg->help_file = "project.md";

    find_input = input_new (1, 1, input_colors, dlg_w - 2, word != NULL ? word : "", "project-find",
                            INPUT_COMPLETE_NONE);
    // the word offered is added to, not typed over
    if (word != NULL && *word != '\0')
        find_input->first = FALSE;
    g_free (word);
    group_add_widget (GROUP (dlg), find_input);
    find_list = listbox_new (3, 1, list_h, dlg_w - 2, FALSE, project_find_activate);
    group_add_widget (GROUP (dlg), find_list);
    group_add_widget (GROUP (dlg), hline_new (list_h + 3, -1, -1));
    find_view = listbox_new (list_h + 4, 1, PROJECT_FIND_VIEW, dlg_w - 2, FALSE, NULL);
    widget_set_options (WIDGET (find_view), WOP_SELECTABLE, FALSE);
    group_add_widget (GROUP (dlg), find_view);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 - 10, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, dlg_w / 2 + 1, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    find_project = project;
    find_dlg = dlg;
    find_found = g_ptr_array_new_with_free_func (project_found_free);
    find_pending = g_string_new (NULL);
    g_clear_pointer (&find_last, g_free);
    find_shown = NULL;
    project_find_refill ();

    if (dlg_run (dlg) == B_ENTER)
    {
        // Enter typed with the text, before the list was made for it
        project_find_refill ();
        project_find_wait ();
        listbox_get_current (find_list, NULL, (void **) &chosen);
    }
    project_find_stop ();
    if (chosen != NULL)
    {
        char *file = g_build_filename (project->root, chosen->rel, (char *) NULL);

        (void) project->host->show_location_at (project->host, file, chosen->line, chosen->column,
                                                0);
        g_free (file);
    }

    widget_destroy (WIDGET (dlg));
    find_input = NULL;
    find_list = NULL;
    find_view = NULL;
    find_shown = NULL;
    find_project = NULL;
    find_dlg = NULL;
    if (find_files != NULL)
        unlink (find_files);
    g_clear_pointer (&find_files, g_free);
    g_clear_pointer (&find_last, g_free);
    g_clear_pointer (&find_found, g_ptr_array_unref);
    if (find_pending != NULL)
        g_string_free (find_pending, TRUE);
    find_pending = NULL;
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
    case PROJECT_CMD_FIND:
        project_find (project, edit);
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

/* The symbol the cursor of @edit is in: the last one that starts above it */
static void
project_outline_cursor (project_t *project, void *edit)
{
    int current = -1;
    long line;
    guint i;

    if (project->outline != NULL && edit != NULL)
    {
        line = project->host->get_cursor_line (project->host, edit);
        for (i = 0; i < project->outline->len; i++)
            if (((project_symbol_t *) g_ptr_array_index (project->outline, i))->line <= line)
                current = (int) i;
    }
    if (current != project->outline_current)
    {
        project->outline_current = current;
        if (project->tree != NULL)
            widget_draw (WIDGET (project->tree));
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A symbol of the outline as the tree shows it: divide (), struct node, VERSION */
static char *
project_symbol_label (const char *name, const char *kind)
{
    if (strcmp (kind, "func") == 0 || strcmp (kind, "proto") == 0)
        return g_strconcat (name, " ()", (char *) NULL);
    if (strcmp (kind, "struct") == 0 || strcmp (kind, "union") == 0 || strcmp (kind, "enum") == 0
        || strcmp (kind, "class") == 0)
        return g_strconcat (kind, " ", name, (char *) NULL);
    return g_strdup (name);
}

/* --------------------------------------------------------------------------------------------- */

/* The outline of the file of @edit, from its text as it is now: what the plugin ctags finds in
   it but the members of the structures and the values of the enums */
static void
project_outline_update (project_t *project, void *edit, gboolean edited)
{
    char *file = edit != NULL ? project->host->get_current_file (project->host, edit) : NULL;
    GPtrArray *outline = NULL;
    char *text = NULL;
    gsize len = 0;
    gint64 started;

    // ctags slow over this file: an edit waits for the save
    if (project->tree == NULL
        || (edited && project->outline_slow && g_strcmp0 (file, project->outline_file) == 0))
    {
        g_free (file);
        return;
    }
    started = g_get_monotonic_time ();
    if (file != NULL && project_relative (project, file) != NULL && g_utf8_validate (file, -1, NULL)
        && project->host->service_call != NULL && project->host->get_text != NULL)
        text = project->host->get_text (project->host, edit, &len);
    // a file of some megabytes is no source to outline
    if (text != NULL && len <= 1024 * 1024)
    {
        GVariantDict args;
        GVariant *reply, *symbols;

        g_variant_dict_init (&args, NULL);
        g_variant_dict_insert (&args, "file", "s", file);
        g_variant_dict_insert_value (&args, "text",
                                     g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, text, len, 1));
        reply = project->host->service_call (project->host, "ctags", "outline",
                                             g_variant_dict_end (&args), NULL);
        symbols = reply != NULL
            ? g_variant_lookup_value (reply, "symbols", G_VARIANT_TYPE ("a(sssi)"))
            : NULL;
        if (symbols != NULL)
        {
            GVariantIter iter;
            const char *name, *kind, *path;
            gint32 line;

            outline = g_ptr_array_new_with_free_func (project_symbol_free);
            g_variant_iter_init (&iter, symbols);
            while (g_variant_iter_next (&iter, "(&s&s&si)", &name, &kind, &path, &line))
                if (strcmp (kind, "field") != 0 && strcmp (kind, "enum val") != 0)
                {
                    project_symbol_t *sym = g_new (project_symbol_t, 1);

                    sym->label = project_symbol_label (name, kind);
                    sym->icon = project_symbol_icon (project, kind);
                    sym->line = line;
                    g_ptr_array_add (outline, sym);
                }
            g_variant_unref (symbols);
        }
        if (reply != NULL)
            g_variant_unref (reply);
    }
    g_free (text);

    // another file comes with its outline open
    if (g_strcmp0 (file, project->outline_file) != 0)
        project->outline_closed = FALSE;
    g_free (project->outline_file);
    project->outline_file = file;
    if (project->outline != NULL)
        g_ptr_array_unref (project->outline);
    project->outline = outline;
    project->outline_current = -2;  // drawn again
    // more than a twentieth of a second: too long for every rest of the typing
    project->outline_slow = g_get_monotonic_time () - started > G_USEC_PER_SEC / 20;
    project_tree_rebuild (project, TRUE);
    project_outline_cursor (project, edit);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
project_handle_event (void *data, void *edit, int event_id, void *payload)
{
    project_t *project = (project_t *) data;

    if (event_id == MC_EP_EVENT_FOCUS_IN && edit != NULL)
        project_file_seen (project, edit);
    else if (event_id == MC_EP_EVENT_TEXT_CHANGED && edit != NULL)
        project_outline_update (project, edit, TRUE);
    else if (event_id == MC_EP_EVENT_CURSOR_MOVED && edit != NULL)
        project_outline_cursor (project, edit);
    else if (event_id == MC_EP_EVENT_FILE_SAVED || event_id == MC_EP_EVENT_FILE_RENAMED)
    {
        // a file new to the project: listed again
        if (payload != NULL && !project_files_have (project, (const char *) payload))
        {
            project_files_drop (project);
            project_tree_rebuild (project, TRUE);
        }
        // the outline made too slow for the edits is made with the save
        if (edit != NULL && project->outline_slow)
            project_outline_update (project, edit, FALSE);
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

static mc_ep_result_t
project_act_find (void *data, void *edit)
{
    return project_run_command (data, PROJECT_CMD_FIND, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

/* The others may keep the project as it is: the debugger, while a program is debugged, asks
   the user before the project changes to @root (NULL: closed).  Asked before, so that a Cancel
   leaves all the plugins on the one project; TRUE without the debugger */
static gboolean
project_may_change (project_t *project, const char *root)
{
    GVariantDict args;
    GVariant *reply;
    gboolean ok = TRUE;

    if (project->host->service_call == NULL)
        return TRUE;
    g_variant_dict_init (&args, NULL);
    if (root != NULL)
        g_variant_dict_insert (&args, "root", "s", root);
    reply = project->host->service_call (project->host, "debugger", "leave_project",
                                         g_variant_dict_end (&args), NULL);
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "ok", "b", &ok);
        g_variant_unref (reply);
    }
    return ok;
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
    if (!project_may_change (project, root))
    {
        g_free (root);
        return MC_EPR_FAILED;
    }
    project_set_root (project, root, TRUE);
    g_free (root);
    return MC_EPR_OK;
}

static mc_ep_result_t
project_act_close_project (void *data, void *edit)
{
    project_t *project = (project_t *) data;

    (void) edit;
    if (project->root == NULL)
    {
        project->host->message (project->host, D_NORMAL, _ ("Project"), _ ("No project is open."));
        return MC_EPR_FAILED;
    }
    if (!project_may_change (project, NULL))
        return MC_EPR_FAILED;
    project_unset (project);
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
    else if (strcmp (method, "open") == 0)
    {
        // the project of a directory, as Open project chooses it
        const char *root = NULL;

        if (args == NULL || !g_variant_lookup (args, "root", "&s", &root)
            || !g_file_test (root, G_FILE_TEST_IS_DIR))
        {
            g_variant_dict_clear (&reply);
            g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_ARGS, "project: no directory");
            return NULL;
        }
        {
            // "/src/app/" or a relative name: the root as Open project has it
            char *canonical = g_canonicalize_filename (root, NULL);
            const gboolean ok = project_may_change (project, canonical);

            if (ok)
                project_set_root (project, canonical, TRUE);
            g_free (canonical);
            g_variant_dict_insert (&reply, "opened", "b", ok);
        }
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

/* The tree in the menu Window */
static mc_ep_window_state_t
project_tree_state (void *data)
{
    project_t *project = (project_t *) data;

    if (project->tree == NULL)
        return MC_EP_WINDOW_CLOSED;
    return project->host->window_current (project->host) == project->tree ? MC_EP_WINDOW_FOCUSED
                                                                          : MC_EP_WINDOW_OPEN;
}

static void
project_tree_show (void *data)
{
    project_t *project = (project_t *) data;

    if (project->tree != NULL)
        project->host->window_show (project->host, project->tree);
    else
        project_tree_toggle (project);
}

static void
project_tree_hide (void *data)
{
    project_t *project = (project_t *) data;

    project_tree_close (project);
}

static void *
project_tree_window (void *data)
{
    return ((project_t *) data)->tree;
}

static const mc_ep_window_kind_t project_tree_kind = {
    .name = "project.tree",
    .window = project_tree_window,
    .label = N_ ("Project tr&ee"),
    .section = PROJECT_KEYMAP_SECTION,
    .command = "ProjectTree",
    .state = project_tree_state,
    .show = project_tree_show,
    .close = project_tree_hide,
};

/* --------------------------------------------------------------------------------------------- */

static void *
project_open (mc_editor_host_t *host, void *editor_dialog)
{
    project_t *project = g_new0 (project_t, 1);
    int i;

    (void) editor_dialog;
    project->host = host;
    project->recent = g_ptr_array_new_with_free_func (g_free);
    host->commands_register (host, PROJECT_KEYMAP_SECTION, N_ ("&Project"), project_commands);
    // Alt held, the bar says what Alt-F7 does
    if (host->command_label != NULL)
        host->command_label (host, project_commands[PROJECT_CMD_FIND].name, N_ ("ButtonBar|Find"));
    if (host->window_kind != NULL)
        host->window_kind (host, &project_tree_kind, project);
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

    if (project->host->command_label != NULL)
        project->host->command_label (project->host, project_commands[PROJECT_CMD_FIND].name, NULL);
    if (project->tree != NULL)
        project->tree->data = NULL;
    g_clear_pointer (&project->model, mctree_model_free);
    g_clear_pointer (&project->paths, g_hash_table_destroy);
    g_clear_pointer (&project->sizes, g_hash_table_destroy);
    project_icons_free (project);
    if (project->service)
        project->host->service_unregister (project->host, PROJECT_SERVICE);
    project_files_drop (project);
    g_ptr_array_free (project->recent, TRUE);
    g_free (project->root);
    g_free (project->open_file);
    g_free (project->outline_file);
    if (project->outline != NULL)
        g_ptr_array_unref (project->outline);
    g_free (project);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_ep_action_t project_actions[] = {
    { "Open file", project_act_open_file },
    { "Recent files", project_act_recent },
    { "Tree", project_act_tree },
    { "Header or source", project_act_alternate },
    { "Find in project", project_act_find },
    { "Open project", project_act_open_project },
    { "Close project", project_act_close_project },
};

static const mc_ep_cmd_menu_entry_t project_menu[] = {
    { MC_EP_MENU_PROJECT, N_ ("&Open project..."), PROJECT_ACT_OPEN_PROJECT, NULL },
    { MC_EP_MENU_PROJECT, N_ ("Close pro&ject"), PROJECT_ACT_CLOSE_PROJECT, NULL },
    { MC_EP_MENU_PROJECT, N_ ("Open &file of project..."), PROJECT_ACT_OPEN_FILE, NULL },
    { MC_EP_MENU_PROJECT, N_ ("&Recent files of project..."), PROJECT_ACT_RECENT, NULL },
    { MC_EP_MENU_PROJECT, N_ ("Project &tree"), PROJECT_ACT_TREE, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Header or source"), PROJECT_ACT_ALTERNATE, NULL },
    { MC_EP_MENU_PROJECT, N_ ("Fin&d in project..."), PROJECT_ACT_FIND, NULL },
};

/* The key of a menu entry, that of the command of its action */
static char *
project_menu_shortcut (int action_index)
{
    int cmd;

    switch (action_index)
    {
    case PROJECT_ACT_OPEN_FILE:
        cmd = PROJECT_CMD_OPEN_FILE;
        break;
    case PROJECT_ACT_RECENT:
        cmd = PROJECT_CMD_RECENT;
        break;
    case PROJECT_ACT_TREE:
        cmd = PROJECT_CMD_TREE;
        break;
    case PROJECT_ACT_ALTERNATE:
        cmd = PROJECT_CMD_ALTERNATE;
        break;
    case PROJECT_ACT_FIND:
        cmd = PROJECT_CMD_FIND;
        break;
    default:
        return NULL;
    }
    return keymap_menu_shortcut (PROJECT_KEYMAP_SECTION, project_commands[cmd].name);
}

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
    .get_menu_shortcut = project_menu_shortcut,
};

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
project_get_plugin (void)
{
    return &project_plugin;
}

/* --------------------------------------------------------------------------------------------- */
