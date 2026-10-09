/*
   The git plugin: push, pull and fetch in the shell of the terminal.

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

/** \file git-remote.c
 *  \brief Source: push, pull and fetch of the git plugin
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/plugin-service.h"

#include "src/editor/edit-impl.h"
#include "src/execute.h"  // shell_execute()

#include "git-private.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The remotes of the repository, never NULL */
static char **
git_remote_names (git_t *git)
{
    char *text = git_capture (git->root, NULL, "remote", (char *) NULL);
    char **remotes;

    remotes = g_strsplit (g_strstrip (text), "\n", -1);
    g_free (text);
    if (remotes[0] != NULL && remotes[0][0] == '\0')
    {
        g_strfreev (remotes);
        remotes = g_new0 (char *, 1);
    }
    return remotes;
}

/* --------------------------------------------------------------------------------------------- */

/* The index of @name among @remotes, -1 when it is not one */
static int
git_remote_index (char **remotes, const char *name)
{
    int n;

    for (n = 0; name != NULL && remotes[n] != NULL; n++)
        if (strcmp (remotes[n], name) == 0)
            return n;
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

/* The remote git pushes the branch to: its pushRemote, else remote.pushDefault, else that of its
   upstream, else origin, else the first one */
static int
git_push_remote (git_t *git, char **remotes, const char *upstream_remote)
{
    char *key = g_strdup_printf ("branch.%s.pushRemote", git->branch.head);
    char *push = git_config_value (git->root, key);
    char *push_default = git_config_value (git->root, "remote.pushDefault");
    int n;

    n = git_remote_index (remotes, push);
    if (n < 0)
        n = git_remote_index (remotes, push_default);
    if (n < 0)
        n = git_remote_index (remotes, upstream_remote);
    if (n < 0)
        n = git_remote_index (remotes, "origin");
    g_free (push_default);
    g_free (push);
    g_free (key);
    return MAX (n, 0);
}

/* --------------------------------------------------------------------------------------------- */

/* The names of @remotes for a radio, the '&' of a name not taken for a hotkey */
static char **
git_radio_labels (char **remotes)
{
    char **labels = g_new0 (char *, g_strv_length (remotes) + 1);
    int n;

    for (n = 0; remotes[n] != NULL; n++)
    {
        char **parts = g_strsplit (remotes[n], "&", -1);

        labels[n] = g_strjoinv ("&&", parts);
        g_strfreev (parts);
    }
    return labels;
}

/* --------------------------------------------------------------------------------------------- */

/* The options of a push asked for: the remote, --force-with-lease, -u, --follow-tags.  Appended to
   @command, the remote and the branch always named; FALSE when the push is given up */
static gboolean
git_push_dialog (git_t *git, GString *command)
{
    char **remotes = git_remote_names (git);
    const int count = (int) g_strv_length (remotes);
    const git_branch_t *branch = &git->branch;
    char *key, *upstream_remote, *merge;
    char **labels = NULL;
    quick_widget_t widgets[16];
    char *about, *remote_label = NULL;
    int remote, i = 0;
    gboolean force, upstream, tags = FALSE;
    gboolean apart, ok;

    if (count == 0)
    {
        git->host->message (git->host, D_ERROR, _ ("Push"),
                            _ ("The repository has no remote to push to."));
        g_strfreev (remotes);
        return FALSE;
    }
    key = g_strdup_printf ("branch.%s.remote", branch->head);
    upstream_remote = branch->upstream != NULL ? git_config_value (git->root, key) : NULL;
    g_free (key);
    key = g_strdup_printf ("branch.%s.merge", branch->head);
    merge = branch->upstream != NULL ? git_config_value (git->root, key) : NULL;
    g_free (key);
    remote = git_push_remote (git, remotes, upstream_remote);

    apart = branch->upstream != NULL && branch->ahead > 0 && branch->behind > 0;
    if (branch->upstream == NULL)
        about = g_strdup_printf (_ ("Branch %s, no upstream"), branch->head);
    else if (apart)
        about = g_strdup_printf (_ ("Branch %s -> %s, %d ahead, %d behind: gone apart"),
                                 branch->head, branch->upstream, branch->ahead, branch->behind);
    else
        about = g_strdup_printf (_ ("Branch %s -> %s, %d ahead, %d behind"), branch->head,
                                 branch->upstream, branch->ahead, branch->behind);
    // gone apart after an amend or a reword made here: no push without it; gone apart after
    // the commits of others were fetched, a push with it would throw them away
    force = apart && g_strcmp0 (git->rewrote, branch->head) == 0;
    upstream = branch->upstream == NULL;

    widgets[i++] = (quick_widget_t) QUICK_LABEL (about, NULL);
    widgets[i++] = (quick_widget_t) QUICK_SEPARATOR (FALSE);
    if (count == 1)
    {
        remote_label = g_strdup_printf (_ ("Remote: %s"), remotes[0]);
        widgets[i++] = (quick_widget_t) QUICK_LABEL (remote_label, NULL);
    }
    else
    {
        labels = git_radio_labels (remotes);
        widgets[i++] = (quick_widget_t) QUICK_LABEL (_ ("Remote:"), NULL);
        widgets[i++] = (quick_widget_t) QUICK_RADIO (count, (const char **) labels, &remote, NULL);
    }
    widgets[i++] = (quick_widget_t) QUICK_SEPARATOR (FALSE);
    widgets[i++] = (quick_widget_t) QUICK_CHECKBOX (_ ("Force with &lease (--force-with-lease)"),
                                                    &force, NULL);
    widgets[i++] = (quick_widget_t) QUICK_CHECKBOX (_ ("Set &upstream (-u)"), &upstream, NULL);
    widgets[i++] =
        (quick_widget_t) QUICK_CHECKBOX (_ ("Include &tags (--follow-tags)"), &tags, NULL);
    widgets[i++] = (quick_widget_t) QUICK_START_BUTTONS (TRUE, TRUE);
    widgets[i++] = (quick_widget_t) QUICK_BUTTON (_ ("&Push"), B_ENTER, NULL, NULL);
    widgets[i++] = (quick_widget_t) QUICK_BUTTON (_ ("&Cancel"), B_CANCEL, NULL, NULL);
    widgets[i++] = (quick_widget_t) QUICK_END;

    {
        quick_dialog_t qdlg = {
            .rect = { -1, -1, 0, 0 },
            .title = _ ("Push"),
            .help = "[Git]",
            .help_file = "git.md",
            .widgets = widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        ok = quick_dialog (&qdlg) == B_ENTER;
    }

    if (ok)
    {
        const char *name = remotes[remote];
        char *quoted;

        if (force)
            g_string_append (command, " --force-with-lease");
        if (upstream)
            g_string_append (command, " -u");
        if (tags)
            g_string_append (command, " --follow-tags");
        quoted = g_shell_quote (name);
        g_string_append_printf (command, " %s HEAD", quoted);
        g_free (quoted);
        // to the branch of the upstream by its own name, which may not be that of the branch
        if (merge != NULL && g_str_has_prefix (merge, "refs/heads/")
            && g_strcmp0 (name, upstream_remote) == 0
            && strcmp (merge + strlen ("refs/heads/"), branch->head) != 0)
        {
            quoted = g_shell_quote (merge + strlen ("refs/heads/"));
            g_string_append_printf (command, ":%s", quoted);
            g_free (quoted);
        }
        if (force)
            g_clear_pointer (&git->rewrote, g_free);
    }
    g_free (about);
    g_free (remote_label);
    g_free (merge);
    g_free (upstream_remote);
    g_strfreev (labels);
    g_strfreev (remotes);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/* The command typed into the shell of the plugin terminal, which gets the keys; FALSE when there
   is no such plugin, TRUE when it ran or could not and said so */
static gboolean
git_remote_in_terminal (git_t *git, const char *command)
{
    GVariantDict args;
    GVariant *reply;
    GError *error = NULL;
    gboolean signal = FALSE;

    if (git->host->service_call == NULL)
        return FALSE;
    if (git->remote_running)
    {
        git->host->message (git->host, D_ERROR, _ ("Git"),
                            _ ("A push, a pull or a fetch runs in the terminal."));
        return TRUE;
    }
    g_variant_dict_init (&args, NULL);
    g_variant_dict_insert (&args, "command", "s", command);
    g_variant_dict_insert (&args, "cwd", "s", git->root);
    reply =
        git->host->service_call (git->host, "terminal", "run", g_variant_dict_end (&args), &error);
    if (reply == NULL)
    {
        const gboolean none = g_error_matches (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_NOT_FOUND);

        if (!none)
            git->host->message (git->host, D_ERROR, _ ("Git"), error->message);
        g_clear_error (&error);
        return !none;
    }
    (void) g_variant_lookup (reply, "signal", "b", &signal);
    g_variant_unref (reply);
    // a shell that does not tell when it is done: the status is read when the window Git is back
    git->remote_running = signal;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* A value of git config, NULL when there is none */
char *
git_config_value (const char *root, const char *name)
{
    char *value = git_capture (root, NULL, "config", "--get", name, (char *) NULL);

    g_strchomp (value);
    if (*value == '\0')
        g_clear_pointer (&value, g_free);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/* The command in the terminal over: everything read again, and the keys back to the window Git
   when git did it; when it said no, the keys stay with what it said.  Not from call_later (): the
   shell told it from its output, which wakes no idle call; the terminal is docked apart from the
   windows Git, and nothing draws over them after */
void
git_terminal_finished (const char *name, const char *signal, GVariant *args, void *user_data)
{
    git_t *git = (git_t *) user_data;
    gint32 status = -1;

    (void) name;
    if (strcmp (signal, "finished") != 0 || !git->remote_running)
        return;
    git->remote_running = FALSE;
    (void) g_variant_lookup (args, "status", "i", &status);
    // -1: the window of the terminal closed or its shell ended, at the end of the editor maybe,
    // the windows Git gone with it
    if (git->win == NULL || status < 0)
        return;
    if (status == 0)
        git_group_show (git->win);
    git_window_reread (git->win);
}

/* --------------------------------------------------------------------------------------------- */

/* git push, pull or fetch on the terminal, where git may ask for a password, then the status
   read again */
void
git_remote (git_window_t *win, const char *what)
{
    git_t *git = win->git;
    GString *command;
    char *quoted;
    int pause = pause_after_run;

    // asked before the options of a push are
    if (git->remote_running)
    {
        git->host->message (git->host, D_ERROR, _ ("Git"),
                            _ ("A push, a pull or a fetch runs in the terminal."));
        return;
    }
    git_read_status (git);
    command = g_string_new ("git -C ");
    quoted = g_shell_quote (git->root);
    g_string_append (command, quoted);
    g_free (quoted);
    g_string_append_printf (command, " %s", what);

    if (strcmp (what, "push") == 0)
    {
        if (git->branch.head == NULL)
        {
            git->host->message (git->host, D_ERROR, _ ("Push"), _ ("HEAD is detached."));
            g_string_free (command, TRUE);
            return;
        }
        if (!git_push_dialog (git, command))
        {
            g_string_free (command, TRUE);
            return;
        }
    }

    // in the shell of the terminal, seen and typed into there; else on the terminal of the editor
    if (git_remote_in_terminal (git, command->str))
    {
        g_string_free (command, TRUE);
        return;
    }
    // what git said stays on the screen till a key
    pause_after_run = pause_always;
    shell_execute (command->str, 0);
    pause_after_run = pause;
    g_string_free (command, TRUE);
    git_window_reread (win);
}

/* --------------------------------------------------------------------------------------------- */
