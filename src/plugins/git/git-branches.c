/*
   The git plugin: the window of the branches.

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

/** \file git-branches.c
 *  \brief Source: the window of the branches of the git plugin
 */

#include <config.h>

#include <string.h>
#include <time.h>

#include "lib/global.h"
#include "lib/strutil.h"

#include "src/editor/edit-impl.h"

#include "git-private.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* The branches (Shift-F7, b) */
/* --------------------------------------------------------------------------------------------- */

static struct
{
    git_window_t *win;
    GPtrArray *refs;  // git_ref_t
    GPtrArray *own;   // what their own commits are kept in
    WInput *input;
    WCheck *local, *remote, *tags, *extended;
    WListbox *list;
    char *last;         // the filter the list was made for, NULL to make it again
    gboolean own_read;  // the own commits of the refs read, for the extended filter
} git_br;

/* --------------------------------------------------------------------------------------------- */

static void
git_branches_read (git_t *git)
{
    const char *args[] = { "for-each-ref", git_refs_format (TRUE),
                           "refs/heads",   "refs/remotes",
                           "refs/tags",    NULL };
    git_output_t o;

    g_clear_pointer (&git_br.refs, g_ptr_array_unref);
    g_clear_pointer (&git_br.own, g_ptr_array_unref);
    git_br.own_read = FALSE;
    // the commits apart from HEAD need git 2.41: without them on an older one
    if (git_run (git->root, args, NULL, &o) && o.status == 0)
        git_br.refs = git_parse_refs (o.out->str, o.out->len);
    else
    {
        char *text;
        gsize len;

        args[1] = git_refs_format (FALSE);
        text = git_capture_args (git->root, args, &len);
        git_br.refs = git_parse_refs (text, len);
        g_free (text);
    }
    git_output_clear (&o);
}

/* --------------------------------------------------------------------------------------------- */

/* The commits of each ref that main has not, with what they say, read when the extended filter
   first needs them: in a repository of many branches far from main they are many */
static void
git_br_read_own (const git_t *git)
{
    GPtrArray *log_args;
    guint i;
    gboolean any_main = FALSE;

    git_br.own_read = TRUE;
    log_args = g_ptr_array_new ();
    g_ptr_array_add (log_args, (gpointer) "log");
    g_ptr_array_add (log_args, (gpointer) GIT_OWN_FORMAT);
    g_ptr_array_add (log_args, (gpointer) "--branches");
    g_ptr_array_add (log_args, (gpointer) "--remotes");
    g_ptr_array_add (log_args, (gpointer) "--tags");
    g_ptr_array_add (log_args, (gpointer) "--not");
    for (i = 0; i < git_br.refs->len; i++)
    {
        const git_ref_t *ref = g_ptr_array_index (git_br.refs, i);

        if (ref->main)
        {
            g_ptr_array_add (log_args, ref->ref);
            any_main = TRUE;
        }
    }
    g_ptr_array_add (log_args, (gpointer) "--");
    g_ptr_array_add (log_args, NULL);
    if (any_main)
    {
        gsize len;
        char *text = git_capture_args (git->root, (const char *const *) log_args->pdata, &len);

        git_br.own = git_refs_own (git_br.refs, text, len);
        g_free (text);
    }
    g_ptr_array_free (log_args, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* "Ilia M." of "Ilia Maslakov" */
static char *
git_short_author (const char *author)
{
    const char *space = strchr (author, ' ');

    if (space == NULL || space[1] == '\0')
        return g_strdup (author);
    return g_strdup_printf ("%.*s %.*s.", (int) (space - author), author,
                            (int) (g_utf8_next_char (space + 1) - (space + 1)), space + 1);
}

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    const git_ref_t *ref;
    int score;
    const char *subject;
} git_br_row_t;

static gint
git_br_row_cmp (gconstpointer a, gconstpointer b)
{
    const git_br_row_t *x = a;
    const git_br_row_t *y = b;

    if (x->score != y->score)
        return y->score - x->score;
    // the fresh ones first, of the same time the local one first
    if (x->ref->time != y->ref->time)
        return x->ref->time < y->ref->time ? 1 : -1;
    if (x->ref->kind != y->ref->kind)
        return (int) x->ref->kind - (int) y->ref->kind;
    return strcmp (x->ref->name, y->ref->name);
}

/* --------------------------------------------------------------------------------------------- */

/* The list made again for the filter and the kinds checked */
static void
git_br_refilter (gboolean force)
{
    const git_t *git = git_br.win->git;
    const char *text = input_get_ctext (git_br.input);
    const gboolean extended = git_br.extended->state;
    char *key = g_strdup_printf ("%s\n%d%d%d%d", text, git_br.local->state, git_br.remote->state,
                                 git_br.tags->state, extended);
    char **words;
    GArray *rows;
    const gint64 now = (gint64) time (NULL);
    // the room the list gives a line
    const int list_w = WIDGET (git_br.list)->rect.cols - 2;
    int name_w = 8;
    guint i;

    if (!force && git_br.last != NULL && strcmp (git_br.last, key) == 0)
    {
        g_free (key);
        return;
    }
    g_free (git_br.last);
    git_br.last = key;
    words = git_filter_words (text);
    if (extended && words[0] != NULL && !git_br.own_read)
        git_br_read_own (git);
    rows = g_array_new (FALSE, FALSE, sizeof (git_br_row_t));
    for (i = 0; i < git_br.refs->len; i++)
    {
        const git_ref_t *ref = g_ptr_array_index (git_br.refs, i);
        git_br_row_t row = { ref, 0, NULL };

        if ((ref->kind == GIT_REF_LOCAL && !git_br.local->state)
            || (ref->kind == GIT_REF_REMOTE && !git_br.remote->state)
            || (ref->kind == GIT_REF_TAG && !git_br.tags->state)
            || !git_ref_match (ref, words, extended, &row.score, &row.subject))
            continue;
        // the current one first when nothing is typed
        if (words[0] == NULL && ref->current)
            row.score = 1;
        g_array_append_val (rows, row);
        name_w = MAX (name_w, str_term_width1 (ref->name));
    }
    g_array_sort (rows, git_br_row_cmp);
    g_strfreev (words);

    name_w = MIN (name_w, 32);
    listbox_remove_list (git_br.list);
    for (i = 0; i < rows->len; i++)
    {
        const git_br_row_t *row = &g_array_index (rows, git_br_row_t, i);
        const git_ref_t *ref = row->ref;
        GString *line = g_string_new (ref->current ? "* " : "  ");
        GString *ab = g_string_new (NULL);
        char *age = git_age (now, ref->time);
        char *author = git_short_author (ref->author);

        g_string_append (line, str_fit_to_term (ref->name, name_w, J_LEFT_FIT));
        if (ref->ahead > 0)
            g_string_append_printf (ab, "%s%d", git->ahead, ref->ahead);
        if (ref->behind > 0)
            g_string_append_printf (ab, "%s%s%d", ab->len != 0 ? " " : "", git->behind,
                                    ref->behind);
        g_string_append_printf (line, "  %s", str_fit_to_term (ab->str, 11, J_LEFT));
        g_string_append_printf (line, " %s", str_fit_to_term (age, 14, J_LEFT));
        g_string_append_printf (line, " %s", str_fit_to_term (author, 12, J_LEFT_FIT));
        // cut the end here: the list would cut a long line in the middle
        g_string_append_c (line, ' ');
        g_string_append (line,
                         str_fit_to_term (row->subject != NULL ? row->subject : ref->subject,
                                          MAX (list_w - str_term_width1 (line->str), 0), J_LEFT));
        listbox_add_item_take (git_br.list, LISTBOX_APPEND_AT_END, 0, g_string_free (line, FALSE),
                               (void *) ref, FALSE);
        g_string_free (ab, TRUE);
        g_free (age);
        g_free (author);
    }
    g_array_free (rows, TRUE);
    listbox_select_first (git_br.list);
    widget_draw (WIDGET (git_br.list));
}

/* --------------------------------------------------------------------------------------------- */

static const git_ref_t *
git_br_current (void)
{
    char *text = NULL;
    void *ref = NULL;

    listbox_get_current (git_br.list, &text, &ref);
    return ref;
}

/* --------------------------------------------------------------------------------------------- */

/* A ref of the name of @ref without its remote, NULL when there is no such local branch */
static const git_ref_t *
git_br_local_of (const char *local)
{
    guint i;

    for (i = 0; i < git_br.refs->len; i++)
    {
        const git_ref_t *r = g_ptr_array_index (git_br.refs, i);

        if (r->kind == GIT_REF_LOCAL && strcmp (r->name, local) == 0)
            return r;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Switch to @ref: a branch of a remote through the local one, made tracking it when there is
   none, a tag detached */
static gboolean
git_br_switch (git_t *git, const git_ref_t *ref)
{
    const char *slash = strchr (ref->name, '/');

    if (ref->kind == GIT_REF_TAG)
    {
        return git_do (git, _ ("Switch"), "switch", "--detach", ref->name, "--", (char *) NULL);
    }
    if (ref->kind == GIT_REF_REMOTE && slash != NULL && git_br_local_of (slash + 1) == NULL)
    {
        return git_do (git, _ ("Switch"), "switch", "-c", slash + 1, "--track", ref->name, "--",
                       (char *) NULL);
    }
    {
        return git_do (git, _ ("Switch"), "switch",
                       ref->kind == GIT_REF_REMOTE ? slash + 1 : ref->name, "--", (char *) NULL);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A new branch from the one under the cursor */
static void
git_br_new (git_t *git, const git_ref_t *ref)
{
    char *text = g_strdup_printf (_ ("A new branch from %s:"), ref->name);
    char *name = input_dialog (_ ("New branch"), text, "git-new-branch", "", INPUT_COMPLETE_NONE);

    g_free (text);
    if (name != NULL && *g_strstrip (name) != '\0')
    {
        // a name git takes for a branch, never an option such as -D
        if (!git_succeeds (git->root, "check-ref-format", "--branch", name, (char *) NULL))
            message (D_ERROR, _ ("New branch"), _ ("%s is not a name of a branch."), name);
        else if (git_do (git, _ ("New branch"), "branch", name, ref->ref, (char *) NULL))
        {
            git_branches_read (git);
            git_br_refilter (TRUE);
        }
    }
    g_free (name);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the local branch @ref is not merged as git branch -d sees it: into its upstream, into
   HEAD when it has none */
static gboolean
git_br_unmerged (const git_t *git, const git_ref_t *ref)
{
    char *upstream = g_strconcat (ref->ref, "@{upstream}", (char *) NULL);
    int status;

    // 0 merged, 1 not, else no upstream
    status =
        git_exit_code (git->root, "merge-base", "--is-ancestor", ref->ref, upstream, (char *) NULL);
    if (status != 0 && status != 1)
        status = git_exit_code (git->root, "merge-base", "--is-ancestor", ref->ref, "HEAD",
                                (char *) NULL);
    g_free (upstream);
    return status == 1;
}

/* --------------------------------------------------------------------------------------------- */

/* The local branch under the cursor deleted after a question, again when it is not merged */
static void
git_br_delete (git_t *git, const git_ref_t *ref)
{
    char *question;
    gboolean ok;

    if (ref->kind != GIT_REF_LOCAL)
    {
        message (D_ERROR, _ ("Delete"), "%s",
                 _ ("Only a local branch is deleted here; one of a remote goes with a push."));
        return;
    }
    if (ref->current)
    {
        message (D_ERROR, _ ("Delete"), "%s", _ ("The current branch cannot be deleted."));
        return;
    }
    question = g_strdup_printf (_ ("Delete the branch %s?"), ref->name);
    ok = query_dialog (_ ("Delete"), question, D_ERROR, 2, _ ("&Yes"), _ ("&No")) == 0;
    g_free (question);
    if (!ok)
        return;
    {
        const char *args[] = { "branch", "-d", "--", ref->name, NULL };
        git_output_t o;

        ok = git_run (git->root, args, NULL, &o) && o.status == 0;
        // asked again only when it is not merged; anything else, a worktree on it, is shown
        if (!ok && !git_br_unmerged (git, ref))
        {
            char *why = git_output_error (&o);

            git->host->message (git->host, D_ERROR, _ ("Delete"), why);
            g_free (why);
            git_output_clear (&o);
            return;
        }
        git_output_clear (&o);
    }
    if (!ok)
    {
        question =
            g_strdup_printf (_ ("The branch %s is not merged. Delete it anyway?"), ref->name);
        ok = query_dialog (_ ("Delete"), question, D_ERROR, 2, _ ("&Yes"), _ ("&No")) == 0;
        g_free (question);
        if (ok)
        {
            ok = git_do (git, _ ("Delete"), "branch", "-D", "--", ref->name, (char *) NULL);
        }
    }
    if (ok)
    {
        git_branches_read (git);
        git_br_refilter (TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* What the branch under the cursor has that the current one has not and the other way, and the
   files it changes since they went apart */
static void
git_br_compare (git_t *git, const git_ref_t *ref)
{
    char *theirs = g_strconcat ("HEAD..", ref->sha, (char *) NULL);
    char *ours = g_strconcat (ref->sha, "..HEAD", (char *) NULL);
    char *apart = g_strconcat ("HEAD...", ref->sha, (char *) NULL);
    char *in = git_capture (git->root, NULL, "log", "--format=%h %s", theirs, "--", (char *) NULL);
    char *out = git_capture (git->root, NULL, "log", "--format=%h %s", ours, "--", (char *) NULL);
    char *files =
        git_capture (git->root, NULL, "diff", "--name-status", apart, "--", (char *) NULL);
    GPtrArray *lines = g_ptr_array_new_with_free_func (g_free);
    char **l, **split;
    char *title;
    Listbox *box;
    int width = 40;
    guint i;

    split = g_strsplit (g_strstrip (in), "\n", -1);
    for (l = split; *l != NULL; l++)
        if (**l != '\0')
            g_ptr_array_add (lines, g_strdup_printf ("%s %s", git->ahead, *l));
    g_strfreev (split);
    split = g_strsplit (g_strstrip (out), "\n", -1);
    for (l = split; *l != NULL; l++)
        if (**l != '\0')
            g_ptr_array_add (lines, g_strdup_printf ("%s %s", git->behind, *l));
    g_strfreev (split);
    split = g_strsplit (g_strstrip (files), "\n", -1);
    if (split[0] != NULL && *split[0] != '\0')
        g_ptr_array_add (lines, g_strdup (""));
    for (l = split; *l != NULL; l++)
        if (**l != '\0')
        {
            g_strdelimit (*l, "\t", ' ');
            g_ptr_array_add (lines, g_strdup (*l));
        }
    g_strfreev (split);
    if (lines->len == 0)
        g_ptr_array_add (lines, g_strdup (_ ("The same commits as the current branch")));
    for (i = 0; i < lines->len; i++)
        width = MAX (width, str_term_width1 (g_ptr_array_index (lines, i)) + 4);
    title = g_strdup_printf (_ ("%s and the current branch"), ref->name);
    box = listbox_window_new (MAX (1, MIN ((int) lines->len, LINES - 6)), MIN (width, COLS - 6),
                              title, "[Git]");
    for (i = 0; i < lines->len; i++)
        LISTBOX_APPEND_TEXT (box, 0, g_ptr_array_index (lines, i), NULL, FALSE);
    (void) listbox_run (box);
    g_free (title);
    g_ptr_array_free (lines, TRUE);
    g_free (in);
    g_free (out);
    g_free (files);
    g_free (theirs);
    g_free (ours);
    g_free (apart);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
git_br_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    git_t *git = git_br.win->git;

    switch (msg)
    {
    case MSG_INIT:
    {
        cb_ret_t ret = dlg_default_callback (w, sender, msg, parm, data);

        widget_select (WIDGET (git_br.input));
        return ret;
    }
    case MSG_KEY:
    {
        const git_ref_t *ref = git_br_current ();

        // the keys that go through the list, the focus staying in the filter
        if (parm == KEY_UP || parm == KEY_DOWN || parm == KEY_PPAGE || parm == KEY_NPAGE)
        {
            send_message (WIDGET (git_br.list), w, MSG_KEY, parm, data);
            return MSG_HANDLED;
        }
        if (parm == '\n' || parm == KEY_ENTER)
        {
            if (ref != NULL)
            {
                DIALOG (w)->ret_value = B_ENTER;
                dlg_close (DIALOG (w));
            }
            return MSG_HANDLED;
        }
        if (ref != NULL && parm == KEY_F (7))
            git_br_new (git, ref);
        else if (ref != NULL && parm == KEY_F (8))
            git_br_delete (git, ref);
        else if (ref != NULL && parm == KEY_F (3))
            git_br_compare (git, ref);
        else
            return dlg_default_callback (w, sender, msg, parm, data);
        return MSG_HANDLED;
    }
    case MSG_NOTIFY:
    case MSG_HOTKEY_HANDLED:
        // a box checked by its key or a click: what is typed goes on to the filter
        widget_select (WIDGET (git_br.input));
        git_br_refilter (FALSE);
        return MSG_HANDLED;
    case MSG_POST_KEY:
        git_br_refilter (FALSE);
        return MSG_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The list of the branches: Enter switches to the one under the cursor */
void
git_branches (git_window_t *win)
{
    git_t *git = win->git;
    WDialog *dlg;
    WGroup *g;
    const char *hint = _ ("Enter switch   F7 new   F8 delete   F3 compare   Esc");
    const char *l_local = _ ("&Local"), *l_remote = _ ("&Remote"), *l_tags = _ ("&Tags");
    const char *l_extended = _ ("&Extended");
    int dlg_h, dlg_w, list_h, checks_w, x;
    const git_ref_t *chosen = NULL;

    if (git->root == NULL)
        return;
    git_colors (git);
    git_br.win = win;
    git_branches_read (git);

    dlg_w = MIN (COLS - 4, 120);
    list_h = MAX (5, LINES - 12);
    dlg_h = list_h + 6;
    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, git_br_callback, NULL, "[Git]", _ ("Branches"));
    dlg->help_file = "git.md";
    g = GROUP (dlg);
    checks_w = str_term_width1 (l_local) + str_term_width1 (l_remote) + str_term_width1 (l_tags)
        + str_term_width1 (l_extended) + 4 * 5 + 2;
    group_add_widget (g, label_new (1, 2, _ ("Filter:")));
    x = 3 + str_term_width1 (_ ("Filter:"));
    git_br.input = input_new (1, x, input_colors, MAX (10, dlg_w - x - checks_w - 3), "",
                              "git-branches", INPUT_COMPLETE_NONE);
    group_add_widget (g, git_br.input);
    x = dlg_w - checks_w - 1;
    git_br.local = check_new (1, x, TRUE, l_local);
    group_add_widget (g, git_br.local);
    x += str_term_width1 (l_local) + 5;
    git_br.remote = check_new (1, x, TRUE, l_remote);
    group_add_widget (g, git_br.remote);
    x += str_term_width1 (l_remote) + 5;
    git_br.tags = check_new (1, x, FALSE, l_tags);
    group_add_widget (g, git_br.tags);
    x += str_term_width1 (l_tags) + 5;
    git_br.extended = check_new (1, x, FALSE, l_extended);
    group_add_widget (g, git_br.extended);
    group_add_widget (g, hline_new (2, -1, -1));
    git_br.list = listbox_new (3, 1, list_h, dlg_w - 2, FALSE, NULL);
    group_add_widget (g, git_br.list);
    group_add_widget (g, hline_new (dlg_h - 3, -1, -1));
    group_add_widget (g, label_new (dlg_h - 2, 2, hint));
    g_clear_pointer (&git_br.last, g_free);
    git_br_refilter (TRUE);

    if (dlg_run (dlg) == B_ENTER)
        chosen = git_br_current ();
    if (chosen != NULL && !chosen->current && git_br_switch (git, chosen))
        git_window_reread (win);
    widget_destroy (WIDGET (dlg));
    g_clear_pointer (&git_br.refs, g_ptr_array_unref);
    g_clear_pointer (&git_br.own, g_ptr_array_unref);
    g_clear_pointer (&git_br.last, g_free);
    git_br.win = NULL;
}

/* --------------------------------------------------------------------------------------------- */
