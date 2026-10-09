/*
   The git plugin: the message of a commit, an amend or a reword, and the commit made
   with it.

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

/** \file git-message.c
 *  \brief Source: the message of a commit of the git plugin
 */

#include <config.h>

#include <string.h>
#include <unistd.h>

#include "lib/global.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

#include "git-private.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* The message of a commit */
/* --------------------------------------------------------------------------------------------- */

/* The lines of @text after "# ", for the part of the message git leaves out */
static void
git_append_commented (GString *s, const char *text)
{
    char **lines = g_strsplit (text, "\n", -1);
    char **l;

    for (l = lines; *l != NULL; l++)
    {
        char *p;

        if (**l == '\0' && l[1] == NULL)
            break;
        g_string_append (s, **l != '\0' ? "# " : "#");
        // the tabs git indents with, as wide as anywhere
        for (p = *l; *p != '\0'; p++)
            if (*p == '\t')
                g_string_append (s, "    ");
            else
                g_string_append_c (s, *p);
        g_string_append_c (s, '\n');
    }
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

/* The branch HEAD is on and its commit, as "refs/heads/name sha" */
static char *
git_head_now (const char *root)
{
    char *ref = g_strstrip (git_capture (root, NULL, "symbolic-ref", "-q", "HEAD", (char *) NULL));
    char *sha =
        g_strstrip (git_capture (root, NULL, "rev-parse", "-q", "--verify", "HEAD", (char *) NULL));
    char *head = g_strdup_printf ("%s %s", ref, sha);

    g_free (ref);
    g_free (sha);
    return head;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
git_message_forget (git_t *git)
{
    // the diff shown for the message goes with it
    if (git->msg_diff_window != 0 && git->host->service_call != NULL)
    {
        GVariantDict dict;
        GVariant *reply;

        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "id", "x", git->msg_diff_window);
        reply = git->host->service_call (git->host, "viewer", "close", g_variant_dict_end (&dict),
                                         NULL);
        if (reply != NULL)
            g_variant_unref (reply);
    }
    git->msg_diff_window = 0;
    if (git->msg_window != NULL)
    {
        git->msg_window = NULL;
        if (git->win != NULL)
            git_group_arrange (git->win);
    }
    git->msg_mode = GIT_MSG_NONE;
    g_clear_pointer (&git->msg_file, g_free);
    g_clear_pointer (&git->msg_root, g_free);
    g_clear_pointer (&git->msg_sha, g_free);
    g_clear_pointer (&git->msg_head, g_free);
    git->msg_failed = FALSE;
    git->msg_edit = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The window of the message just opened goes into the window Git, at the top left; for a
   reword, the files of the commit under it */
static void
git_message_attach (git_t *git, const git_commit_t *commit)
{
    git_window_t *win = git->win;
    void *current = git->host->window_current (git->host);
    char *file = current != NULL && edit_widget_is_editor (CONST_WIDGET (current))
        ? git->host->get_current_file (git->host, current)
        : NULL;
    guint i;

    if (file == NULL || strcmp (file, git->msg_file) != 0)
    {
        g_free (file);
        return;
    }
    g_free (file);
    git->msg_window = current;
    if (git->msg_mode == GIT_MSG_REWORD)
    {
        win->tab = GIT_TAB_LOG;
        git_read_log (git);
        // its files: a commit of the log there, an older one on its own
        {
            gboolean own = FALSE;
            git_commit_t *c = git_find_commit (git, commit->sha, &own);

            for (i = 0; git->commits != NULL && i < git->commits->len; i++)
                if (g_ptr_array_index (git->commits, i) == c)
                {
                    win->cursor[GIT_LIST_COMMITS].selected = (int) i;
                    git_cursor_show (win, GIT_LIST_COMMITS);
                }
            if (c != NULL)
            {
                git_open_commit (git, c);
                if (own)
                    git->own_commit = c;
                win->cursor[GIT_LIST_FILES].selected = 0;
                win->cursor[GIT_LIST_FILES].top = 0;
            }
        }
    }
    else if (git->msg_mode == GIT_MSG_AMEND)
        win->tab = GIT_TAB_STATUS;
    win->in_diff = FALSE;
    git_group_arrange (win);
    git_diff_update (win);
    git_group_draw (win);
}

/* TRUE when a merge left files with conflicts: git add -A would take them as resolved, the
   markers in them */
gboolean
git_conflicts (const char *root, const char *title)
{
    char *files = g_strstrip (
        git_capture (root, NULL, "diff", "--name-only", "--diff-filter=U", (char *) NULL));
    const gboolean any = *files != '\0';

    g_free (files);
    if (any)
        message (D_ERROR, title, "%s",
                 _ ("Files have conflicts: resolve them, or stage them one by one, first."));
    return any;
}

/* --------------------------------------------------------------------------------------------- */

/* The message in the window Git goes, for another one: that of a commit is kept in its file for
   the next commit; that of an amend or a reword changed goes after a question.  FALSE when the
   user keeps it */
gboolean
git_message_set_aside (git_t *git)
{
    WEdit *edit = (WEdit *) git->msg_window;
    char *text = git->host->get_text (git->host, edit, NULL);
    char *message = git_message_strip (text);

    if (git->msg_mode == GIT_MSG_COMMIT)
    {
        if (*message != '\0')
            (void) g_file_set_contents (git->msg_file, text, -1, NULL);
        else
            (void) unlink (git->msg_file);
    }
    else
    {
        if (edit->modified
            && query_dialog (_ ("Git"), _ ("Give up the message being written?"), D_NORMAL, 2,
                             _ ("&Yes"), _ ("&No"))
                != 0)
        {
            g_free (message);
            g_free (text);
            return FALSE;
        }
        (void) unlink (git->msg_file);
    }
    g_free (message);
    g_free (text);
    // closed as it is: what it holds is where it has to be
    edit->modified = 0;
    git_message_forget (git);
    (void) git->host->window_close (git->host, edit);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The window Git on its status has the message of the next commit at the top left, the keys
   staying with the list */
void
git_message_draft (git_t *git)
{
    if (git->win == NULL || git->once || git->msg_mode != GIT_MSG_NONE)
        return;
    git_message_start (git, GIT_MSG_COMMIT, NULL);
    if (git->win != NULL)
        git_group_show (git->win);
}

void
git_message_draft_later (void *data)
{
    git_message_draft ((git_t *) data);
}

/* --------------------------------------------------------------------------------------------- */

/* Write the message of a commit in a window of the editor: @mode, and the commit it is of when
   it rewords one */
void
git_message_start (git_t *git, git_msg_mode_t mode, const git_commit_t *commit)
{
    char *gitdir, *file, *message = NULL;
    GString *s;

    if (git->msg_mode != GIT_MSG_NONE)
    {
        const gboolean same = git->msg_mode == mode && g_strcmp0 (git->msg_root, git->root) == 0
            && (mode != GIT_MSG_REWORD
                || (commit != NULL && g_strcmp0 (git->msg_sha, commit->sha) == 0));

        // the same again, or one out of the window Git: the one being written comes to the front
        if (same || git->msg_window == NULL)
        {
            if (git->host->show_location (git->host, git->msg_file, 1))
            {
                // not what was asked for: one message at a time out of the window Git
                if (!same)
                    git->host->message (
                        git->host, D_NORMAL, _ ("Git"),
                        _ ("Another message is being written: save it or close its window first."));
                return;
            }
            git_message_forget (git);
        }
        // in the window Git, the message of a commit makes room for an amend or a reword
        else if (!git_message_set_aside (git))
            return;
    }
    if (git->root == NULL)
        return;
    gitdir = git_dir (git->root);
    if (gitdir == NULL)
        return;
    file = g_build_filename (gitdir,
                             mode == GIT_MSG_REWORD      ? GIT_REWORD_FILE
                                 : mode == GIT_MSG_AMEND ? GIT_AMEND_FILE
                                                         : GIT_MESSAGE_FILE,
                             (char *) NULL);
    g_free (gitdir);

    if (mode == GIT_MSG_COMMIT)
    {
        // what a commit git refused left: written again
        char *left = NULL;

        if (g_file_get_contents (file, &left, NULL, NULL))
            message = git_message_strip (left);
        g_free (left);
    }
    else
    {
        const char *sha = mode == GIT_MSG_REWORD ? commit->sha : "HEAD";

        message = git_capture (git->root, NULL, "log", "-1", "--format=%B", sha, (char *) NULL);
        g_strchomp (message);
    }

    s = g_string_new (message != NULL ? message : "");
    if (s->len == 0 || s->str[s->len - 1] != '\n')
        g_string_append_c (s, '\n');
    g_string_append (s, "\n");
    g_string_append (s,
                     _ ("# Save the message (F2) to commit, close the window to give up.\n"
                        "# The lines starting with '#' are left out; an empty message gives "
                        "up.\n"));
    // the message there is has such lines: they would be lost unseen
    if (message != NULL && (*message == '#' || strstr (message, "\n#") != NULL))
        g_string_append (s,
                         _ ("# WARNING: lines of this message start with '#' and will be left "
                            "out:\n# start them otherwise to keep them.\n"));
    if (mode == GIT_MSG_REWORD)
    {
        char *show = git_capture (git->root, NULL, "show", "--stat",
                                  "--format=commit %H%nAuthor: %an <%ae>%nDate:   %ad", commit->sha,
                                  (char *) NULL);

        g_string_append_printf (
            s,
            _ ("#\n# The message of commit %.7s. The commits after it are made again with\n"
               "# their messages; the index and the files are not touched.\n#\n"),
            commit->sha);
        git_append_commented (s, show);
        g_free (show);
    }
    else
    {
        char *status = git_capture (git->root, NULL, "-c", "advice.statusHints=false", "status",
                                    (char *) NULL);

        g_string_append (s,
                         mode == GIT_MSG_AMEND
                             ? _ ("#\n# Amending the last commit, with the staged changes.\n#\n")
                             : "#\n");
        git_append_commented (s, status);
        g_free (status);
    }

    if (!g_file_set_contents (file, s->str, (gssize) s->len, NULL))
        git->host->message (git->host, D_ERROR, _ ("Git"), _ ("Cannot write the message file."));
    else
    {
        git_message_forget (git);
        git->msg_mode = mode;
        git->msg_file = file;
        file = NULL;
        git->msg_root = g_strdup (git->root);
        git->msg_sha = mode == GIT_MSG_REWORD ? g_strdup (commit->sha) : NULL;
        git->msg_head = mode != GIT_MSG_COMMIT ? git_head_now (git->root) : NULL;
        if (git->host->show_location (git->host, git->msg_file, 1) && git->win != NULL)
            git_message_attach (git, commit);
    }
    g_string_free (s, TRUE);
    g_free (message);
    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */

/* Commit what is staged, asking to stage everything when nothing is */
void
git_commit (git_t *git, gboolean amend)
{
    if (git->root == NULL)
    {
        git->host->message (git->host, D_NORMAL, _ ("Git"), _ ("No git work tree here."));
        return;
    }
    git_read_status (git);
    if (amend && git->branch.sha == NULL)
    {
        git->host->message (git->host, D_NORMAL, _ ("Amend"), _ ("There is no commit yet."));
        return;
    }
    if (!amend && git->staged->len == 0)
    {
        if (git->unstaged->len == 0)
        {
            git->host->message (git->host, D_NORMAL, _ ("Commit"), _ ("Nothing to commit."));
            return;
        }
        if (git_conflicts (git->root, _ ("Commit"))
            || query_dialog (_ ("Commit"), _ ("Nothing is staged. Stage all the changes?"),
                             D_NORMAL, 2, _ ("&Yes"), _ ("&No"))
                != 0
            || !git_do (git, _ ("Stage"), "add", "-A", (char *) NULL))
            return;
        if (git->win != NULL)
            git_window_reload (git->win);
    }
    git_message_start (git, amend ? GIT_MSG_AMEND : GIT_MSG_COMMIT, NULL);
}

/* --------------------------------------------------------------------------------------------- */

void
git_close_later (void *data)
{
    git_t *git = (git_t *) data;
    void *edit = git->msg_edit;

    git->msg_edit = NULL;
    if (git->once)
    {
        git_quit (git);
        return;
    }
    if (edit != NULL)
        (void) git->host->window_close (git->host, edit);
    // back where the commit was asked for, the message of the next one ready
    if (git->win != NULL)
    {
        git_message_draft (git);
        git_group_show (git->win);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The message file is saved: the commit is made with it */
void
git_message_saved (git_t *git, void *edit)
{
    char *text = NULL, *message;
    gboolean ok = FALSE;

    if (!g_file_get_contents (git->msg_file, &text, NULL, NULL))
        return;
    message = git_message_strip (text);
    g_free (text);
    if (*message == '\0')
    {
        git->host->message (git->host, D_NORMAL, _ ("Commit"),
                            _ ("The message is empty: nothing is committed."));
        g_free (message);
        return;
    }

    // the commit amended, or the branch reworded, not one that came since in the terminal
    if (git->msg_head != NULL)
    {
        char *head = git_head_now (git->msg_root);
        const gboolean moved = strcmp (head, git->msg_head) != 0;

        g_free (head);
        if (moved)
        {
            git->host->message (git->host, D_ERROR,
                                git->msg_mode == GIT_MSG_AMEND ? _ ("Amend") : _ ("Reword"),
                                _ ("HEAD has moved since the message was started: close its "
                                   "window and start again."));
            git->msg_failed = TRUE;
            g_free (message);
            return;
        }
    }

    if (git->msg_mode == GIT_MSG_REWORD)
    {
        GError *error = NULL;

        ok = git_reword (git->msg_root, git->msg_sha, message, NULL, &error);
        if (!ok)
        {
            git->host->message (git->host, D_ERROR, _ ("Reword"), error->message);
            g_error_free (error);
        }
    }
    else
    {
        const char *commit_args[] = { "commit", "--cleanup=strip", "-F", git->msg_file, NULL };

        // the message was written before anything was staged
        if (git->msg_mode == GIT_MSG_COMMIT)
        {
            g_free (git->root);
            git->root = g_strdup (git->msg_root);
            git_read_status (git);
            if (git->staged->len == 0
                && (git->unstaged->len == 0 || git_conflicts (git->root, _ ("Commit"))
                    || query_dialog (_ ("Commit"),
                                     _ ("Nothing is staged. Stage all the changes and commit?"),
                                     D_NORMAL, 2, _ ("&Yes"), _ ("&No"))
                        != 0
                    || !git_do (git, _ ("Stage"), "add", "-A", (char *) NULL)))
            {
                if (git->unstaged->len == 0)
                    git->host->message (git->host, D_NORMAL, _ ("Commit"),
                                        _ ("Nothing to commit."));
                // not committed: the message is kept, as when git says no
                git->msg_failed = TRUE;
                g_free (message);
                return;
            }
        }
        const char *amend_args[] = { "commit", "--amend",     "--cleanup=strip",
                                     "-F",     git->msg_file, NULL };
        git_output_t o;

        ok = git_run (git->msg_root, git->msg_mode == GIT_MSG_AMEND ? amend_args : commit_args,
                      NULL, &o)
            && o.status == 0;
        if (!ok)
        {
            char *why = git_output_error (&o);

            git->host->message (git->host, D_ERROR, _ ("Commit"), why);
            g_free (why);
            git->msg_failed = TRUE;
        }
        git_output_clear (&o);
    }
    g_free (message);
    if (!ok)
        return;

    // done: the file and its window go, and the window Git shows the commit
    if (git->msg_head != NULL && g_str_has_prefix (git->msg_head, "refs/heads/"))
    {
        const char *name = git->msg_head + strlen ("refs/heads/");

        g_free (git->rewrote);
        git->rewrote = g_strndup (name, strcspn (name, " "));
    }
    (void) unlink (git->msg_file);
    git_message_forget (git);
    git->msg_edit = edit;
    git->host->call_later (git->host, git_close_later, git);
    if (git->win != NULL)
        git_window_reload (git->win);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
git_is_message_file (const git_t *git, void *edit)
{
    char *file;
    gboolean is;

    if (git->msg_mode == GIT_MSG_NONE || edit == NULL)
        return FALSE;
    file = git->host->get_current_file (git->host, edit);
    is = file != NULL && strcmp (file, git->msg_file) == 0;
    g_free (file);
    return is;
}

/* --------------------------------------------------------------------------------------------- */
