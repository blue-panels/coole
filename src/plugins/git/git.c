/*
   The git plugin: the changes of the work tree, the log, and the messages of the commits.

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

/** \file git.c
 *  \brief Source: the git plugin
 *
 *  A window "Git" with three tabs: Status, the changes of the work tree
 *  and those of the index, which a key moves from one to the other, Log, the commits and
 *  Graph, the history of all refs; Log and Graph show
 *  the files each one changes.  The diff of what the cursor is on is beside the list.  The
 *  message of a commit, a new one or one of the log, is written in a window of the editor:
 *  saved, it is the message.
 */

#include <config.h>

#include <string.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/event.h"
#include "lib/mcconfig.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"
#include "src/keymap.h"  // editor_map

#include "git-private.h"
#include "git.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define GIT_KEYMAP_SECTION "git"
#define GIT_HELP_FILE      "git.md"
#define GIT_HELP_NODE      "[Git]"

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static const mc_ep_command_t git_commands[GIT_CMD_COUNT + 1] = {
    { "GitStatus", N_ ("Changes of the work tree (git status)"), "alt-shift-c" },
    { "GitLog", N_ ("Commits of the branch (git log)"), "alt-shift-l" },
    { "GitCommit", N_ ("Commit the changes in the index"), NULL },
    { NULL, NULL, NULL },
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The commits of HEAD from the @skip-th, @limit of them */
static GPtrArray *
git_log_part (git_t *git, guint skip, int limit)
{
    char *n = g_strdup_printf ("%d", limit);
    char *from = g_strdup_printf ("--skip=%u", skip);
    gsize len = 0;
    char *text;
    GPtrArray *part;

    // a branch with no commit has no log, and git says so
    text = git->branch.sha != NULL ? git_capture (git->root, &len, "log", from, "-n", n,
                                                  GIT_LOG_FORMAT, "HEAD", "--", (char *) NULL)
                                   : g_strdup ("");
    part = git_parse_log (text, len);
    g_free (text);
    g_free (from);
    g_free (n);
    return part;
}

/* --------------------------------------------------------------------------------------------- */
/* The plugin */
/* --------------------------------------------------------------------------------------------- */

static void
git_open_later (void *data)
{
    git_t *git = (git_t *) data;
    void *edit = git->open_edit;

    git->open_edit = NULL;
    git_window_open (git, edit, git->open_tab);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
git_run_command (git_t *git, int cmd, void *edit)
{
    switch (cmd)
    {
    case GIT_CMD_STATUS:
    case GIT_CMD_LOG:
        git->open_tab = cmd == GIT_CMD_LOG ? GIT_TAB_LOG : GIT_TAB_STATUS;
        git->open_edit = edit;
        git->host->call_later (git->host, git_open_later, git);
        return TRUE;
    case GIT_CMD_COMMIT:
    {
        char *root = git_root_of (git, edit);

        if (root != NULL && g_strcmp0 (root, git->root) != 0)
        {
            git_forget_log (git);
            g_free (git->root);
            git->root = root;
        }
        else
            g_free (root);
        git_commit (git, FALSE);
    }
        return TRUE;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
git_command (const git_t *git, long command)
{
    int i;

    for (i = 0; command != CK_IgnoreKey && i < GIT_CMD_COUNT; i++)
        if (git->commands[i] == command)
            return i;
    return GIT_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */

/* The file F4 opened closed as the user would, asked about its changes; its closing brings the
   window Git back */
static void
git_close_opened_later (void *data)
{
    git_t *git = (git_t *) data;

    if (git->opened != NULL)
        (void) git->host->window_close (git->host, git->opened);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
git_handle_key (void *data, int key, void *edit)
{
    git_t *git = (git_t *) data;

    // Quit (Esc, F10) in the file F4 opened from the window Git closes that file alone
    if (edit != NULL && edit == git->opened
        && keybind_lookup_keymap_command (editor_map, key) == CK_Quit)
    {
        git->host->call_later (git->host, git_close_opened_later, git);
        return MC_EPR_OK;
    }

    // the key of the menu of the user, before the scripts that take it (base64-decode, F11)
    if (edit != NULL && keybind_lookup_keymap_command (editor_map, key) == CK_UserMenu
        && git_is_any_message_file (git, edit))
    {
        git_message_menu (git, (WEdit *) edit);
        return MC_EPR_OK;
    }
    const int cmd =
        git_command (git, git->host->command_lookup (git->host, GIT_KEYMAP_SECTION, key));

    return git_run_command (git, cmd, edit) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
git_handle_action (void *data, long command, void *edit)
{
    git_t *git = (git_t *) data;

    // F11 in a message of a commit: the menu of the message, not the menu of the user
    if (command == CK_UserMenu && git_is_any_message_file (git, edit))
    {
        git_message_menu (git, (WEdit *) edit);
        return MC_EPR_OK;
    }

    return git_run_command (git, git_command (git, command), edit) ? MC_EPR_OK
                                                                   : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
git_handle_event (void *data, void *edit, int event_id, void *payload)
{
    git_t *git = (git_t *) data;

    (void) payload;
    if (event_id == MC_EP_EVENT_FILE_SAVED && git_is_message_file (git, edit))
        git_message_saved (git, edit);
    return MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
git_file_close (void *data, void *edit)
{
    git_t *git = (git_t *) data;

    // the file F4 opened is closed: back to the window Git
    if (edit != NULL && edit == git->opened)
    {
        git->opened = NULL;
        if (git->win != NULL)
            git->host->call_later (git->host, git_group_show_later, git);
    }
    if (edit != NULL && edit == git->msg_window && !git_is_message_file (git, edit))
        git->msg_window = NULL;
    // the window of a message committed, closed by the editor before git_close_later () came
    if (edit != NULL && edit == git->msg_edit)
        git->msg_edit = NULL;

    if (git_is_message_file (git, edit))
    {
        const gboolean other = git->msg_mode != GIT_MSG_COMMIT;

        if (!git->msg_failed)
            (void) unlink (git->msg_file);
        git_message_forget (git);
        if (git->once)
            git_quit (git);
        // an amend or a reword given up: the message of the next commit comes back
        else if (other && git->win != NULL)
            git->host->call_later (git->host, git_message_draft_later, git);
    }
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* Menu actions */
/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
git_act_status (void *data, void *edit)
{
    return git_run_command (data, GIT_CMD_STATUS, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
git_act_log (void *data, void *edit)
{
    return git_run_command (data, GIT_CMD_LOG, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
git_act_commit (void *data, void *edit)
{
    return git_run_command (data, GIT_CMD_COMMIT, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

/* The windows Git in the menu Window, each an entry: the message, the list, the staged changes,
   the diff.  An entry opens the window Git when it is not, and gives its window the keys */

/* which window of the window Git an entry is for: a pane, or the message */
#define GIT_KIND_MESSAGE GIT_PANE_COUNT

static void *
git_kind_window (git_t *git, int kind)
{
    if (kind == GIT_KIND_MESSAGE)
        return git->win != NULL ? git->msg_window : NULL;
    // one closed by the user is not on the screen
    return git->win != NULL && !git->win->hidden[kind] ? git->win->pane[kind] : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_window_state_t
git_kind_state (git_t *git, int kind)
{
    void *window = git_kind_window (git, kind);

    if (window == NULL)
        return MC_EP_WINDOW_CLOSED;
    // coole -G ends with the window Git: its entries bring it to the front, and never close it,
    // which the entry of a window in front does
    if (git->standalone || git->once)
        return MC_EP_WINDOW_OPEN;
    return git->host->window_current (git->host) == window ? MC_EP_WINDOW_FOCUSED
                                                           : MC_EP_WINDOW_OPEN;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_kind_show (git_t *git, int kind)
{
    git_window_t *win;

    if (git->win == NULL)
        git_window_open (git, NULL, GIT_TAB_STATUS);
    win = git->win;
    if (win == NULL)
        return;
    // one closed by the user comes back
    if (kind != GIT_KIND_MESSAGE)
        win->hidden[kind] = FALSE;
    switch (kind)
    {
    case GIT_KIND_MESSAGE:
        // the message of the next commit, when none is written
        if (git->msg_window == NULL)
            git_message_start (git, GIT_MSG_COMMIT, NULL);
        else
            git->host->window_show (git->host, git->msg_window);
        return;
    case GIT_PANE_STAGED:
        win->tab = GIT_TAB_STATUS;
        win->list = GIT_LIST_STAGED;
        win->in_diff = FALSE;
        break;
    case GIT_PANE_DIFF:
        win->in_diff = TRUE;
        break;
    default:
        win->in_diff = FALSE;
        if (win->tab == GIT_TAB_STATUS)
            win->list = GIT_LIST_UNSTAGED;
        break;
    }
    git_group_show (win);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_kind_close (git_t *git, int kind)
{
    if (kind == GIT_KIND_MESSAGE)
    {
        // the message goes, that of a commit kept for the next time
        if (git->msg_window != NULL)
            (void) git_message_set_aside (git);
    }
    // that window alone, the others taking its room
    else if (git->win != NULL && git->win->pane[kind] != NULL)
        (void) git_pane_close (EDIT_WINDOW (git->win->pane[kind]));
}

/* --------------------------------------------------------------------------------------------- */

#define GIT_KIND_FUNCTIONS(suffix, kind)                                                           \
    static mc_ep_window_state_t git_kind_state_##suffix (void *data)                               \
    {                                                                                              \
        return git_kind_state ((git_t *) data, kind);                                              \
    }                                                                                              \
    static void git_kind_show_##suffix (void *data) { git_kind_show ((git_t *) data, kind); }      \
    static void git_kind_close_##suffix (void *data) { git_kind_close ((git_t *) data, kind); }    \
    static void *git_kind_window_##suffix (void *data)                                             \
    {                                                                                              \
        return git_kind_window ((git_t *) data, kind);                                             \
    }

GIT_KIND_FUNCTIONS (message, GIT_KIND_MESSAGE)
GIT_KIND_FUNCTIONS (list, GIT_PANE_LIST)
GIT_KIND_FUNCTIONS (staged, GIT_PANE_STAGED)
GIT_KIND_FUNCTIONS (diff, GIT_PANE_DIFF)

/* only the list has a name, for the layouts: the window Git comes back whole with it */
static const mc_ep_window_kind_t git_window_kinds[] = {
    {
        .label = N_ ("Git: commit message"),
        .state = git_kind_state_message,
        .show = git_kind_show_message,
        .close = git_kind_close_message,
        .window = git_kind_window_message,
    },
    {
        .name = "git.window",
        .label = N_ ("&Git: status and log"),
        .section = GIT_KEYMAP_SECTION,
        .command = "GitStatus",
        .state = git_kind_state_list,
        .show = git_kind_show_list,
        .close = git_kind_close_list,
        .window = git_kind_window_list,
    },
    {
        .label = N_ ("Git: staged changes"),
        .state = git_kind_state_staged,
        .show = git_kind_show_staged,
        .close = git_kind_close_staged,
        .window = git_kind_window_staged,
    },
    {
        .label = N_ ("Git: diff"),
        .state = git_kind_state_diff,
        .show = git_kind_show_diff,
        .close = git_kind_close_diff,
        .window = git_kind_window_diff,
    },
};

/* --------------------------------------------------------------------------------------------- */

/* The empty window the editor starts with when it is given no file: under -G it would come to
   the front, over the window Git, whenever a window is closed */
static void
git_close_empty_files (git_t *git)
{
    GList *l = GROUP (git->host->host_data)->widgets;
    GPtrArray *empty = g_ptr_array_new ();
    guint i;

    for (; l != NULL; l = g_list_next (l))
        if (edit_widget_is_editor (CONST_WIDGET (l->data)) && EDIT (l->data)->filename == NULL
            && !EDIT (l->data)->modified && l->data != git->msg_window)
            g_ptr_array_add (empty, l->data);
    // the focus goes through the windows Git on its way: it changes no tab
    if (git->win != NULL)
        git->win->moving_focus = TRUE;
    for (i = 0; i < empty->len; i++)
        (void) git->host->window_close (git->host, g_ptr_array_index (empty, i));
    if (git->win != NULL)
        git->win->moving_focus = FALSE;
    g_ptr_array_free (empty, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* coole --git, the editor up: what the options ask for */
static void
git_startup_open (git_t *git)
{
    mc_editor_host_t *host = git->host;
    const char *open = host->startup_option (host, "git-open");
    const char *name = host->startup_option (host, "git-commit");
    git_commit_t *commit;
    gboolean own;

    git->home = git_toplevel (host->startup_option (host, "git"));
    if (git->home == NULL)
    {
        host->message (host, D_ERROR, _ ("Git"), _ ("The directory is not in a git work tree."));
        git_quit (git);
        return;
    }
    g_free (git->root);
    git->root = g_strdup (git->home);
    git_read_status (git);

    if (strcmp (open, "commit") == 0 || strcmp (open, "amend") == 0)
    {
        git->once = TRUE;
        git->standalone = TRUE;
        git_window_open (git, NULL, GIT_TAB_STATUS);
        git_commit (git, strcmp (open, "amend") == 0);
        if (git->msg_mode == GIT_MSG_NONE)
            git_quit (git);
        return;
    }
    if (strcmp (open, "reword") == 0)
    {
        git->once = TRUE;
        git->standalone = TRUE;
        git_window_open (git, NULL, GIT_TAB_LOG);
        commit = git_find_commit (git, name, &own);
        if (commit == NULL)
            host->message (host, D_ERROR, _ ("Reword"), _ ("No such commit."));
        else
            git_message_start (git, GIT_MSG_REWORD, commit);
        if (own)
            git_commit_free (commit);
        if (git->msg_mode == GIT_MSG_NONE)
            git_quit (git);
        return;
    }

    git->standalone = TRUE;
    git_window_open (git, NULL, strcmp (open, "status") == 0 ? GIT_TAB_STATUS : GIT_TAB_LOG);
    if (git->win == NULL || strcmp (open, "show") != 0)
        return;
    commit = git_find_commit (git, name, &own);
    if (commit == NULL)
    {
        host->message (host, D_ERROR, _ ("Git"), _ ("No such commit."));
        return;
    }
    {
        git_window_t *win = git->win;
        guint i;

        for (i = 0; git->commits != NULL && i < git->commits->len; i++)
            if (g_ptr_array_index (git->commits, i) == commit)
                win->cursor[GIT_LIST_COMMITS].selected = (int) i;
        git_cursor_show (win, GIT_LIST_COMMITS);
        git_open_commit (git, commit);
        // older than the log reads: the window Git keeps it while its files are shown
        if (own)
            git->own_commit = commit;
        win->cursor[GIT_LIST_FILES].selected = 0;
        win->cursor[GIT_LIST_FILES].top = 0;
        git_diff_update (win);
        git_group_draw (win);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
git_startup (void *data)
{
    git_t *git = (git_t *) data;

    git_startup_open (git);
    // the editor goes on for git: the empty window it started with goes
    if ((git->standalone || git->once) && (git->win != NULL || git->msg_mode != GIT_MSG_NONE))
    {
        git_close_empty_files (git);
        // the window that gave the keys away has them back
        if (git->win != NULL && !git->once)
            git_group_show (git->win);
        else if (git->msg_mode != GIT_MSG_NONE)
            (void) git->host->show_location (git->host, git->msg_file, 1);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void *
git_open (mc_editor_host_t *host, void *editor_dialog)
{
    git_t *git = g_new0 (git_t, 1);
    int i;

    (void) editor_dialog;
    git->host = host;
    {
        char *refs =
            mc_config_get_string (mc_global.main_config, GIT_CONFIG_GROUP, GIT_CONFIG_GRAPH,
                                  git_graph_refs_key[GIT_GRAPH_REFS_REMOTE]);

        git->graph_refs = GIT_GRAPH_REFS_REMOTE;
        for (i = 0; i < GIT_GRAPH_REFS_COUNT; i++)
            if (strcmp (refs, git_graph_refs_key[i]) == 0)
                git->graph_refs = (git_graph_refs_t) i;
        g_free (refs);
    }
    git->staged = g_ptr_array_new_with_free_func (git_change_free);
    git->unstaged = g_ptr_array_new_with_free_func (git_change_free);
    host->commands_register (host, GIT_KEYMAP_SECTION, N_ ("&Git"), git_commands);
    if (host->window_kind != NULL)
        for (i = 0; i < (int) G_N_ELEMENTS (git_window_kinds); i++)
            host->window_kind (host, &git_window_kinds[i], git);
    for (i = 0; i < GIT_CMD_COUNT; i++)
        git->commands[i] = host->command_id (host, git_commands[i].name);
    if (host->service_connect != NULL)
        git->terminal_signal = host->service_connect (host, "terminal", git_terminal_finished, git);
    if (host->startup_option (host, "git") != NULL)
        host->call_later (host, git_startup, git);
    return git;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_close (void *data)
{
    git_t *git = (git_t *) data;

    if (git->terminal_signal != 0)
        git->host->service_disconnect (git->host, git->terminal_signal);
    if (git->win != NULL)
        git->win->git = NULL;
    // the windows are gone with the editor already
    git->msg_diff_window = 0;
    // a message left unsaved in its window when the editor ended: no commit; that of the next
    // commit is kept for the next time, as when the window Git is closed
    if (git->msg_mode != GIT_MSG_NONE && git->msg_mode != GIT_MSG_COMMIT && !git->msg_failed)
        (void) unlink (git->msg_file);
    git_message_forget (git);
    git_forget_log (git);
    git_branch_clear (&git->branch);
    g_ptr_array_free (git->staged, TRUE);
    g_ptr_array_free (git->unstaged, TRUE);
    g_free (git->root);
    g_free (git->home);
    g_free (git->over_path);
    g_free (git->rewrote);
    g_free (git->ahead);
    g_free (git->behind);
    g_free (git->color_other);
    g_free (git);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_ep_action_t git_actions[] = {
    { "Status", git_act_status },
    { "Log", git_act_log },
    { "Commit", git_act_commit },
};

static const mc_ep_cmd_menu_entry_t git_menu[] = {
    { MC_EP_MENU_COMMAND, NULL, 0, NULL },
    { MC_EP_MENU_COMMAND, N_ ("Git status"), GIT_ACT_STATUS, NULL },
    { MC_EP_MENU_COMMAND, N_ ("Git log"), GIT_ACT_LOG, NULL },
    { MC_EP_MENU_COMMAND, N_ ("Git commit..."), GIT_ACT_COMMIT, NULL },
};

static const mc_editor_plugin_t git_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "git",
    .display_name = "Git",
    .flags = MC_EPF_NONE,
    .open = git_open,
    .close = git_close,
    .handle_key = git_handle_key,
    .handle_action = git_handle_action,
    .handle_event = git_handle_event,
    .on_file_close = git_file_close,
    .actions = git_actions,
    .action_count = G_N_ELEMENTS (git_actions),
    .cmd_menu_entries = git_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (git_menu),
};

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
git_get_plugin (void)
{
    return &git_plugin;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The same with the arguments after @title, up to a NULL */
gboolean
git_do (git_t *git, const char *title, ...)
{
    va_list ap;
    const char **args;
    gboolean ok;

    va_start (ap, title);
    args = git_args_va (ap);
    va_end (ap);
    ok = git_do_args (git, title, args);
    g_free (args);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/* The work tree of the file in front, else of the project, else of the current directory */
char *
git_root_of (git_t *git, void *edit)
{
    char *file, *root = NULL;

    if (git->home != NULL)
        return g_strdup (git->home);
    if (edit == NULL)
        edit = git->host->window_top_file (git->host);
    file = edit != NULL ? git->host->get_current_file (git->host, edit) : NULL;
    if (file != NULL)
        root = git_toplevel (file);
    g_free (file);
    if (root == NULL && git->host->service_call != NULL)
    {
        GVariant *reply = git->host->service_call (git->host, "project", "root", NULL, NULL);
        char *project = NULL;

        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "root", "s", &project);
            g_variant_unref (reply);
        }
        if (project != NULL)
            root = git_toplevel (project);
        g_free (project);
    }
    if (root == NULL)
    {
        char *cwd = g_get_current_dir ();

        root = git_toplevel (cwd);
        g_free (cwd);
    }
    return root;
}

/* --------------------------------------------------------------------------------------------- */

void
git_close_commit (git_t *git)
{
    g_clear_pointer (&git->files, g_ptr_array_unref);
    git->commit = NULL;
    if (git->own_commit != NULL)
    {
        git_commit_free (git->own_commit);
        git->own_commit = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
git_forget_log (git_t *git)
{
    git_close_commit (git);
    g_clear_pointer (&git->commits, g_ptr_array_unref);
    g_clear_pointer (&git->graph_rows, g_ptr_array_unref);
    g_clear_pointer (&git->graph_commits, g_ptr_array_unref);
    git->graph_cols = 0;
    g_clear_pointer (&git->log_head, g_free);
    g_clear_pointer (&git->log_branch, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* The status of the work tree again; the log too when HEAD has moved */
void
git_read_status (git_t *git)
{
    const char *args[] = { "status", "--porcelain=v2", "-z", "--branch", NULL };
    git_output_t o;

    g_ptr_array_set_size (git->staged, 0);
    g_ptr_array_set_size (git->unstaged, 0);
    git_branch_clear (&git->branch);
    if (git->root == NULL)
        return;
    if (git_run (git->root, args, NULL, &o) && o.status == 0)
        git_parse_status (o.out->str, o.out->len, git->staged, git->unstaged, &git->branch);
    git_output_clear (&o);
    if ((git->commits != NULL || git->graph_commits != NULL)
        && (g_strcmp0 (git->log_head, git->branch.sha) != 0
            || g_strcmp0 (git->log_branch, git->branch.head) != 0))
        git_forget_log (git);
}

/* --------------------------------------------------------------------------------------------- */

void
git_read_log (git_t *git)
{
    if (git->commits != NULL || git->root == NULL)
        return;
    git->commits = git_log_part (git, 0, GIT_LOG_STEP);
    git->log_more = git->commits->len == GIT_LOG_STEP;
    g_free (git->log_head);
    g_free (git->log_branch);
    git->log_head = g_strdup (git->branch.sha);
    git->log_branch = g_strdup (git->branch.head);
}

/* --------------------------------------------------------------------------------------------- */

/* The next commits of the log after those read; FALSE when there are none */
gboolean
git_read_log_more (git_t *git)
{
    GPtrArray *part;
    guint i;

    if (git->commits == NULL || !git->log_more)
        return FALSE;
    part = git_log_part (git, git->commits->len, GIT_LOG_STEP);
    git->log_more = part->len == GIT_LOG_STEP;
    // taken over by the log, not freed with the part
    g_ptr_array_set_free_func (part, NULL);
    for (i = 0; i < part->len; i++)
        g_ptr_array_add (git->commits, g_ptr_array_index (part, i));
    g_ptr_array_unref (part);
    return i != 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The files commit @c changes, against its first parent */
void
git_open_commit (git_t *git, git_commit_t *c)
{
    char *range = g_strconcat (c->sha, "^", (char *) NULL);
    const char *diff_args[] = { "diff", "--name-status", "-z", "-M", range, c->sha, NULL };
    const char *root_args[] = { "diff-tree", "--root",         "-r", "--name-status",
                                "-z",        "--no-commit-id", "-M", c->sha,
                                NULL };
    gsize len;
    char *text = git_capture_args (git->root, c->parents > 0 ? diff_args : root_args, &len);

    git_close_commit (git);
    git->files = git_parse_name_status (text, len);
    git->commit = c;
    g_free (text);
    g_free (range);
}

/* --------------------------------------------------------------------------------------------- */

void
git_help (void)
{
    ev_help_t event_data = { GIT_HELP_FILE, GIT_HELP_NODE, NULL };

    mc_event_raise (MCEVENT_GROUP_CORE, "help", &event_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_quit_now (void *data)
{
    git_t *git = (git_t *) data;

    (void) send_message (WIDGET (git->host->host_data), NULL, MSG_ACTION, CK_Quit, NULL);
}

/* coole --git: the editor ends, back to whoever started it */
void
git_quit (git_t *git)
{
    git->standalone = FALSE;
    git->once = FALSE;
    git->host->call_later (git->host, git_quit_now, git);
}

/* --------------------------------------------------------------------------------------------- */

/* The commit @name stands for, from the log when it is there; NULL when there is none such */
git_commit_t *
git_find_commit (git_t *git, const char *name, gboolean *own)
{
    char *spec = g_strconcat (name, "^{commit}", (char *) NULL);
    char *sha = git_capture (git->root, NULL, "rev-parse", "--verify", "-q", "--end-of-options",
                             spec, (char *) NULL);
    git_commit_t *found = NULL;
    guint i;

    g_strchomp (sha);
    *own = FALSE;
    git_read_log (git);
    for (i = 0; *sha != '\0' && git->commits != NULL && i < git->commits->len; i++)
        if (strcmp (((git_commit_t *) g_ptr_array_index (git->commits, i))->sha, sha) == 0)
            found = g_ptr_array_index (git->commits, i);
    if (found == NULL && *sha != '\0')
    {
        // not among the latest of the branch: alone, as the log would have it
        gsize len;
        char *text =
            git_capture (git->root, &len, "log", "-1", GIT_LOG_FORMAT, sha, "--", (char *) NULL);
        GPtrArray *log = git_parse_log (text, len);

        if (log->len != 0)
            found = g_ptr_array_steal_index (log, 0);
        else
        {
            found = g_new0 (git_commit_t, 1);
            found->sha = g_strdup (sha);
            found->author = g_strdup ("");
            found->refs = g_strdup ("");
            found->subject = g_strdup ("");
        }
        g_ptr_array_unref (log);
        g_free (text);
        *own = TRUE;
    }
    g_free (sha);
    g_free (spec);
    return found;
}

/* --------------------------------------------------------------------------------------------- */
