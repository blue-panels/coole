/*
   The git plugin: the window Git, its windows, its lists, the diff, the keys and the mouse.

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

/** \file git-window.c
 *  \brief Source: the window Git of the git plugin
 */

#include <config.h>

#include <string.h>
#include <time.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editdock.h"  // edit_dock_free_area()
#include "src/editor/editwindow.h"

#include "git-private.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* the tabs in the title of the window of the list, from its column 2, a space between them */
#define GIT_TAB_STATUS_TITLE "[ 1 Status ]"
#define GIT_TAB_LOG_TITLE    "[ 2 Log ]"
#define GIT_TAB_GRAPH_TITLE  "[ 3 Graph ]"
#define GIT_DIFF_MAX         20000

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* The window */
/* --------------------------------------------------------------------------------------------- */

static int
git_list_len (const git_window_t *win, int list)
{
    const git_t *git = win->git;

    switch (list)
    {
    case GIT_LIST_UNSTAGED:
        return (int) git->unstaged->len;
    case GIT_LIST_STAGED:
        return (int) git->staged->len;
    case GIT_LIST_COMMITS:
        return git->commits != NULL ? (int) git->commits->len : 0;
    case GIT_LIST_GRAPH:
        // its rows: those of the commits, and those between that join lines
        return git->graph_rows != NULL ? (int) git->graph_rows->len : 0;
    case GIT_LIST_FILES:
        // the message of the commit first, then its files
        return git->files != NULL ? (int) git->files->len + 1 : 0;
    default:
        return 0;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The change the cursor of a list of changes is on, NULL when none */
static git_change_t *
git_list_change (const git_window_t *win, int list)
{
    const git_cursor_t *c = &win->cursor[list];
    const GPtrArray *a = list == GIT_LIST_UNSTAGED ? win->git->unstaged
        : list == GIT_LIST_STAGED                  ? win->git->staged
        : list == GIT_LIST_FILES                   ? win->git->files
                                                   : NULL;
    const int index = list == GIT_LIST_FILES ? c->selected - 1 : c->selected;

    if (a == NULL || index < 0 || index >= (int) a->len)
        return NULL;
    return g_ptr_array_index (a, index);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_change_label (GString *s, const git_change_t *c)
{
    g_string_append_c (s, c->code);
    g_string_append_c (s, ' ');
    if (c->orig != NULL)
    {
        g_string_append (s, c->orig);
        g_string_append (s, " -> ");
    }
    g_string_append (s, c->path);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_commit_label (GString *s, const git_commit_t *c)
{
    char date[16] = "";
    const time_t t = (time_t) c->time;
    struct tm tm;

    if (localtime_r (&t, &tm) != NULL)
        (void) strftime (date, sizeof (date), "%Y-%m-%d", &tm);
    g_string_append_printf (s, "%.7s %s ", c->sha, date);
    g_string_append (s, str_fit_to_term (c->author, 12, J_LEFT));
    g_string_append_c (s, ' ');
    if (*c->refs != '\0')
        g_string_append_printf (s, "(%s) ", c->refs);
    g_string_append (s, c->subject);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_list_label (const git_window_t *win, int list, int index, GString *s)
{
    const git_t *git = win->git;

    switch (list)
    {
    case GIT_LIST_UNSTAGED:
        git_change_label (s, g_ptr_array_index (git->unstaged, index));
        break;
    case GIT_LIST_STAGED:
        git_change_label (s, g_ptr_array_index (git->staged, index));
        break;
    case GIT_LIST_COMMITS:
        git_commit_label (s, g_ptr_array_index (git->commits, index));
        break;
    case GIT_LIST_GRAPH:
    {
        const int commit = git_graph_commit_of_row (git, index);

        if (commit >= 0)
            git_commit_label (s, g_ptr_array_index (git->graph_commits, commit));
    }
    break;
    case GIT_LIST_FILES:
        if (index == 0)
            g_string_append_printf (s, _ ("Commit %.7s: %s"), git->commit->sha,
                                    git->commit->subject);
        else
            git_change_label (s, g_ptr_array_index (git->files, index - 1));
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The lists the window shows now, and the one the keys go to */
static int
git_list_of_tab (const git_window_t *win)
{
    if (win->tab != GIT_TAB_STATUS)
        return win->git->files != NULL  ? GIT_LIST_FILES
            : win->tab == GIT_TAB_GRAPH ? GIT_LIST_GRAPH
                                        : GIT_LIST_COMMITS;
    return win->list == GIT_LIST_STAGED ? GIT_LIST_STAGED : GIT_LIST_UNSTAGED;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_cursor_clamp (git_window_t *win, int list)
{
    git_cursor_t *c = &win->cursor[list];
    const int len = git_list_len (win, list);

    c->selected = CLAMP (c->selected, 0, MAX (len - 1, 0));
    c->top = CLAMP (c->top, 0, c->selected);
}

/* --------------------------------------------------------------------------------------------- */
/* The rows inside a window of the group */
static int
git_pane_rows (const git_window_t *win, int role)
{
    const git_pane_t *p = win->pane[role];

    return p != NULL ? MAX (1, CONST_WIDGET (p)->rect.lines - 2) : 1;
}

/* --------------------------------------------------------------------------------------------- */

/* The rows of the diff */
static int
git_body_lines (const git_window_t *win)
{
    return git_pane_rows (win, GIT_PANE_DIFF);
}

/* --------------------------------------------------------------------------------------------- */
/* The window the keys go to: that of the list the cursor is in, or of the diff */
static git_pane_t *
git_focus_pane (const git_window_t *win)
{
    if (win->in_diff)
        return win->pane[GIT_PANE_DIFF];
    if (win->tab == GIT_TAB_STATUS && win->list == GIT_LIST_STAGED)
        return win->pane[GIT_PANE_STAGED];
    return win->pane[GIT_PANE_LIST];
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a window of the group is on the screen: not closed by the user, the staged changes not
   given up to the files of a reword */
static gboolean
git_pane_shown (const git_window_t *win, int role)
{
    return win->pane[role] != NULL && !win->hidden[role]
        && widget_get_state (CONST_WIDGET (win->pane[role]), WST_VISIBLE);
}

/* --------------------------------------------------------------------------------------------- */

/* The window the keys go to, one on the screen: that of the cursor, else the first one there,
   the cursor going to it */
static git_pane_t *
git_focus_pane_shown (git_window_t *win)
{
    git_pane_t *p = git_focus_pane (win);
    int role;

    if (p != NULL && git_pane_shown (win, p->role))
        return p;
    for (role = 0; role < GIT_PANE_COUNT; role++)
        if (git_pane_shown (win, role))
        {
            win->in_diff = role == GIT_PANE_DIFF;
            if (role == GIT_PANE_STAGED)
            {
                win->tab = GIT_TAB_STATUS;
                win->list = GIT_LIST_STAGED;
            }
            else if (role == GIT_PANE_LIST && win->tab == GIT_TAB_STATUS)
                win->list = GIT_LIST_UNSTAGED;
            return win->pane[role];
        }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/* The rows a list has in its window: the one at the top has a title in its first row */
static int
git_list_rows (const git_window_t *win, int list)
{
    if (list == GIT_LIST_STAGED)
        return git_pane_rows (win, GIT_PANE_STAGED);
    return MAX (1, git_pane_rows (win, GIT_PANE_LIST) - 1);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_colors (git_t *git)
{
    if (git->colors)
        return;
    git->color_add = mc_skin_color_on ("editor", "_default_", "green");
    git->color_del = mc_skin_color_on ("editor", "_default_", "red");
    git->color_hunk = mc_skin_color_on ("editor", "_default_", "cyan");
    git->color_sha = mc_skin_color_on ("editor", "_default_", "yellow");
    git->ahead = mc_skin_get ("git-graph", "ahead", "+");
    git->behind = mc_skin_get ("git-graph", "behind", "-");
    git->color_head = mc_skin_color_on ("editor", "_default_", "brightcyan");
    git_graph_colors (git);
    git_graph_glyphs (git);
    git->colors = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static int
git_diff_line_color (const git_t *git, const char *line)
{
    if (g_str_has_prefix (line, "+++") || g_str_has_prefix (line, "---")
        || g_str_has_prefix (line, "diff ") || g_str_has_prefix (line, "index ")
        || g_str_has_prefix (line, "new file") || g_str_has_prefix (line, "deleted file")
        || g_str_has_prefix (line, "rename ") || g_str_has_prefix (line, "similarity "))
        return EDITOR_BOLD_COLOR;
    if (g_str_has_prefix (line, "commit "))
        return git->color_sha;
    switch (line[0])
    {
    case '+':
        return git->color_add;
    case '-':
        return git->color_del;
    case '@':
        return git->color_hunk;
    default:
        return EDITOR_NORMAL_COLOR;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* @rows rows of a list at @y, @x of the window, @cols wide */
static void
git_draw_list (git_window_t *win, Widget *w, int list, int y, int x, int cols, int rows,
               gboolean active)
{
    const git_cursor_t *c = &win->cursor[list];
    const int len = git_list_len (win, list);
    GString *s = g_string_new (NULL);
    int row;

    for (row = 0; row < rows; row++)
    {
        const int index = c->top + row;
        int color = EDITOR_NORMAL_COLOR;

        g_string_truncate (s, 0);
        if (index < len)
        {
            if (list == GIT_LIST_GRAPH)
            {
                git_draw_graph_row (
                    win, w, y + row, x, cols, index,
                    index == c->selected ? (active ? EDITOR_MARKED_COLOR : EDITOR_BOLD_COLOR) : 0);
                continue;
            }
            git_list_label (win, list, index, s);
            if (index == c->selected && active)
                color = EDITOR_MARKED_COLOR;
            else if (index == c->selected)
                color = EDITOR_BOLD_COLOR;
        }
        tty_setcolor (color);
        widget_gotoyx (w, y + row, x);
        // a commit is cut at its end, a name loses its middle
        tty_print_string (str_fit_to_term (
            s->str, cols,
            list == GIT_LIST_COMMITS || list == GIT_LIST_GRAPH ? J_LEFT : J_LEFT_FIT));
    }
    g_string_free (s, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_draw_title (Widget *w, int y, int x, int cols, const char *text, gboolean active)
{
    tty_setcolor (active ? EDITOR_BOLD_COLOR : EDITOR_NORMAL_COLOR);
    widget_gotoyx (w, y, x);
    tty_print_string (str_fit_to_term (text, cols, J_LEFT_FIT));
}

/* --------------------------------------------------------------------------------------------- */

static void
git_draw_diff (git_window_t *win, Widget *w, int y, int x, int cols, int rows)
{
    const git_t *git = win->git;
    int row;

    for (row = 0; row < rows; row++)
    {
        const int index = win->diff_top + row;
        const char *line = win->diff != NULL && index < (int) win->diff->len
            ? g_ptr_array_index (win->diff, index)
            : "";

        tty_setcolor (git_diff_line_color (git, line));
        widget_gotoyx (w, y + row, x);
        tty_print_string (
            str_fit_to_term (str_term_substring (line, win->diff_left, cols), cols, J_LEFT_FIT));
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The branch and how far it is from its upstream: "master +2 -1", the characters of the skin */
static char *
git_branch_label (const git_t *git)
{
    GString *s = g_string_new (NULL);

    if (git->root == NULL)
        g_string_append (s, _ ("no git work tree"));
    else if (git->branch.head != NULL)
        g_string_append (s, git->branch.head);
    else if (git->branch.sha != NULL)
        g_string_append_printf (s, _ ("HEAD detached at %.7s"), git->branch.sha);
    if (git->branch.upstream != NULL && (git->branch.ahead != 0 || git->branch.behind != 0))
        g_string_append_printf (s, " %s%d %s%d", git->ahead, git->branch.ahead, git->behind,
                                git->branch.behind);
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/* The title of a window of the group at the top, in the color of the frame or @color */
static void
git_pane_title (Widget *w, int *x, const char *text, int color)
{
    const int room = w->rect.cols - 8 - *x;

    if (room <= 0)
        return;
    tty_setcolor (color);
    widget_gotoyx (w, 0, *x);
    tty_print_string (str_fit_to_term (text, MIN (room, str_term_width1 (text)), J_LEFT_FIT));
    *x += MIN (room, str_term_width1 (text));
}

/* --------------------------------------------------------------------------------------------- */

static void
git_pane_draw (git_pane_t *pane)
{
    WEditWindow *ew = &pane->window;
    Widget *w = WIDGET (pane);
    git_window_t *win = pane->group;
    const gboolean focused = widget_get_state (w, WST_FOCUSED);
    const int frame = edit_window_frame_color (ew, focused);
    const int cols = MAX (1, w->rect.cols - 2);
    const int rows = MAX (1, w->rect.lines - 2);
    int row, x = 2;

    edit_window_draw_frame (ew, frame, focused);
    for (row = 1; row <= rows; row++)
    {
        tty_setcolor (EDITOR_NORMAL_COLOR);
        tty_draw_hline (w->rect.y + row, w->rect.x + 1, ' ', cols);
    }
    if (win == NULL)
    {
        edit_window_draw_icons (ew, frame);
        return;
    }
    git_colors (win->git);

    switch (pane->role)
    {
    case GIT_PANE_LIST:
    {
        git_t *git = win->git;
        const int list = win->tab == GIT_TAB_STATUS ? GIT_LIST_UNSTAGED : git_list_of_tab (win);
        char *branch, *caption;

        // the tabs in the title, the branch at its right
        git_pane_title (w, &x, GIT_TAB_STATUS_TITLE,
                        win->tab == GIT_TAB_STATUS ? EDITOR_MARKED_COLOR : frame);
        git_pane_title (w, &x, " ", frame);
        git_pane_title (w, &x, GIT_TAB_LOG_TITLE,
                        win->tab == GIT_TAB_LOG ? EDITOR_MARKED_COLOR : frame);
        git_pane_title (w, &x, " ", frame);
        git_pane_title (w, &x, GIT_TAB_GRAPH_TITLE,
                        win->tab == GIT_TAB_GRAPH ? EDITOR_MARKED_COLOR : frame);
        {
            char *label = git_branch_label (git);

            branch = g_strdup_printf (" %s ", label);
            g_free (label);
        }
        {
            const int bw = str_term_width1 (branch);
            int bx = w->rect.cols - 8 - bw;

            if (bx > x)
                git_pane_title (w, &bx, branch, EDITOR_BOLD_COLOR);
        }
        g_free (branch);

        if (win->tab == GIT_TAB_STATUS)
            caption = g_strdup_printf (_ ("Changes not staged (%u)"), git->unstaged->len);
        else if (git->files != NULL)
            caption = g_strdup_printf (_ ("Files of %.7s (%u)"), git->commit->sha, git->files->len);
        else
        {
            const gboolean graph = win->tab == GIT_TAB_GRAPH;
            const GPtrArray *a = graph ? git->graph_commits : git->commits;

            // "+": there are more than those read
            caption =
                g_strdup_printf ((graph ? git->graph_more : git->log_more) ? _ ("Commits (%u+)")
                                                                           : _ ("Commits (%u)"),
                                 a != NULL ? a->len : 0);
        }
        win->refs_cols = 0;
        // the refs the graph reads, at the right: a click or v chooses others
        if (win->tab == GIT_TAB_GRAPH && git->files == NULL)
        {
            char *refs = g_strdup_printf ("[ %s ]", _ (git_graph_refs_label[git->graph_refs]));
            const int rw = str_term_width1 (refs);

            if (rw + str_term_width1 (caption) + 1 <= cols)
            {
                win->refs_x = 1 + cols - rw;
                win->refs_cols = rw;
                git_draw_title (w, 1, 1, cols - rw, caption, focused);
                git_draw_title (w, 1, win->refs_x, rw, refs, focused);
            }
            g_free (refs);
        }
        if (win->refs_cols == 0)
            git_draw_title (w, 1, 1, cols, caption, focused);
        g_free (caption);
        git_draw_list (win, w, list, 2, 1, cols, rows - 1, focused);
    }
    break;
    case GIT_PANE_STAGED:
    {
        char *title = g_strdup_printf (_ ("[Staged changes (%u)]"), win->git->staged->len);

        git_pane_title (w, &x, title, frame);
        g_free (title);
        git_draw_list (win, w, GIT_LIST_STAGED, 1, 1, cols, rows,
                       focused && win->tab == GIT_TAB_STATUS);
    }
    break;
    default:
    {
        char *title =
            g_strdup_printf ("[%s]", win->diff_title != NULL ? win->diff_title : _ ("Diff"));

        git_pane_title (w, &x, title, frame);
        g_free (title);
        git_draw_diff (win, w, 1, 1, cols, rows);
    }
    break;
    }

    // the scrollbars on the frame: where the list or the diff is, of how much
    if (ew->fullscreen == 0)
    {
        if (pane->role == GIT_PANE_DIFF)
        {
            edit_window_set_scroll (ew, TRUE, win->diff != NULL ? (long) win->diff->len : 0, rows,
                                    win->diff_top);
            edit_window_set_scroll (ew, FALSE, win->diff_width, cols, win->diff_left);
        }
        else
        {
            const int list = pane->role == GIT_PANE_STAGED ? GIT_LIST_STAGED
                : win->tab == GIT_TAB_STATUS               ? GIT_LIST_UNSTAGED
                                                           : git_list_of_tab (win);

            edit_window_set_scroll (ew, TRUE, git_list_len (win, list), git_list_rows (win, list),
                                    win->cursor[list].top);
        }
        edit_window_draw_bars (ew, frame);
    }
    edit_window_draw_icons (ew, frame);
}

/* --------------------------------------------------------------------------------------------- */

/* The keys go to the window of the list the cursor is in, or of the diff, when one of the group
   has them */
static void
git_group_sync_focus (git_window_t *win)
{
    git_pane_t *target = git_focus_pane_shown (win);
    gboolean ours = FALSE;
    int i;

    for (i = 0; i < GIT_PANE_COUNT; i++)
        if (win->pane[i] != NULL && widget_get_state (WIDGET (win->pane[i]), WST_FOCUSED))
            ours = TRUE;
    if (!ours || target == NULL || widget_get_state (WIDGET (target), WST_FOCUSED))
        return;
    win->moving_focus = TRUE;
    widget_select (WIDGET (target));
    win->moving_focus = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_diff_set (git_window_t *win, const char *text, gsize len)
{
    const char *p = text;
    const char *end = text + len;
    GString *line = g_string_new (NULL);

    if (win->diff != NULL)
        g_ptr_array_set_size (win->diff, 0);
    else
        win->diff = g_ptr_array_new_with_free_func (g_free);
    win->diff_width = 0;
    while (p < end && win->diff->len < GIT_DIFF_MAX)
    {
        const char *nl = memchr (p, '\n', (size_t) (end - p));
        const char *stop = nl != NULL ? nl : end;
        int col = 0;

        g_string_truncate (line, 0);
        for (; p < stop; p++)
        {
            if (*p == '\t')
            {
                do
                    g_string_append_c (line, ' ');
                while (++col % TAB_SIZE != 0);
                continue;
            }
            // a carriage return or another control character would move the cursor
            g_string_append_c (line, (unsigned char) *p < ' ' ? '.' : *p);
            col++;
        }
        if (line->len != 0 && line->str[line->len - 1] == '.' && stop[-1] == '\r')
            g_string_truncate (line, line->len - 1);
        g_ptr_array_add (win->diff, g_strdup (line->str));
        win->diff_width = MAX (win->diff_width, str_term_width1 (line->str));
        p = nl != NULL ? nl + 1 : end;
    }
    if (p < end)
        g_ptr_array_add (win->diff, g_strdup (_ ("... (the rest is not shown)")));
    g_string_free (line, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_window_buttonbar (git_window_t *win)
{
    git_pane_t *pane = git_focus_pane_shown (win);
    Widget *w = WIDGET (pane);
    WButtonBar *bb;
    int i;

    if (pane == NULL || w->owner == NULL)
        return;
    bb = buttonbar_find (DIALOG (w->owner));
    if (bb == NULL)
        return;
    for (i = 1; i <= 10; i++)
        buttonbar_clear_label (bb, i, NULL);
    buttonbar_set_label_command (bb, 1, _ ("Help"), KEY_F (1), w);
    if (win->tab == GIT_TAB_STATUS)
    {
        buttonbar_set_label_command (bb, 2, _ ("Commit"), KEY_F (2), w);
        buttonbar_set_label_command (bb, 3, _ ("Amend"), KEY_F (3), w);
        buttonbar_set_label_command (bb, 4, _ ("Edit"), KEY_F (4), w);
        buttonbar_set_label_command (bb, 8, _ ("Discard"), KEY_F (8), w);
    }
    else
        buttonbar_set_label_command (bb, 4, _ ("Reword"), KEY_F (4), w);
    buttonbar_set_label_command (bb, 5, _ ("Reload"), KEY_F (5), w);
    buttonbar_set_label_command (bb, 6, _ ("Push"), KEY_F (6), w);
    buttonbar_set_label_command (bb, 7, _ ("Pull"), KEY_F (7), w);
    buttonbar_set_label_command (bb, 10, _ ("Close"), KEY_F (10), w);
    widget_draw (WIDGET (bb));
}

/* --------------------------------------------------------------------------------------------- */

static void
git_window_tab (git_window_t *win, int tab)
{
    if (tab != win->tab && win->git->files != NULL)
        git_close_commit (win->git);
    win->tab = tab;
    win->in_diff = FALSE;
    if (tab == GIT_TAB_LOG)
        git_read_log (win->git);
    else if (tab == GIT_TAB_GRAPH)
        git_read_graph (win->git);
    git_cursor_clamp (win, git_list_of_tab (win));
    if (git_list_of_tab (win) == GIT_LIST_GRAPH)
        git_graph_cursor_skip (win, 1);
    git_group_arrange (win);
    git_diff_update (win);
    git_window_buttonbar (win);
    git_group_draw (win);
}

/* --------------------------------------------------------------------------------------------- */
/* The three windows closed together */
static void
git_group_close (git_window_t *win)
{
    git_pane_t *panes[GIT_PANE_COUNT];
    WDialog *dialog = NULL;
    int i;

    // the message at the top left goes with them, that of a commit kept for the next one; the
    // user keeping one that would be lost keeps the window Git too
    if (win->git != NULL && win->git->msg_window != NULL && !git_message_set_aside (win->git))
        return;
    if (win->git != NULL && win->git->standalone)
        git_quit (win->git);
    // the last one destroyed frees the group
    memcpy (panes, win->pane, sizeof (panes));
    for (i = 0; i < GIT_PANE_COUNT; i++)
        if (panes[i] != NULL)
        {
            dialog = DIALOG (WIDGET (panes[i])->owner);
            edit_window_destroy (EDIT_WINDOW (panes[i]));
        }
    if (dialog != NULL)
        widget_draw (WIDGET (dialog));
}

/* --------------------------------------------------------------------------------------------- */
/* What the keys do */
/* --------------------------------------------------------------------------------------------- */

/* After a stage, an unstage or a discard: the list the cursor was in emptied, the cursor goes to
   the other one */
static void
git_window_reload_after_change (git_window_t *win)
{
    git_window_reload (win);
    if (win->tab != GIT_TAB_STATUS || git_list_len (win, win->list) != 0)
        return;
    win->list = win->list == GIT_LIST_STAGED ? GIT_LIST_UNSTAGED : GIT_LIST_STAGED;
    if (git_list_len (win, win->list) == 0)
        win->list = GIT_LIST_UNSTAGED;
    git_diff_update (win);
    git_group_draw (win);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_stage (git_window_t *win, gboolean all)
{
    git_t *git = win->git;
    const git_change_t *c = git_list_change (win, win->list);

    if (win->list == GIT_LIST_UNSTAGED)
    {
        const char *one[] = { "add", "-A", "--", c != NULL ? c->path : NULL, NULL };
        const char *every[] = { "add", "-A", NULL };

        if (all || c != NULL)
            (void) git_do_args (git, _ ("Stage"), all ? every : one);
    }
    else if (git->branch.sha == NULL)
    {
        // nothing to go back to before the first commit: out of the index
        const char *one[] = {
            "rm", "--cached", "-r", "-q", "--", c != NULL ? c->path : NULL, NULL
        };
        const char *every[] = { "rm", "--cached", "-r", "-q", ".", NULL };

        if (all || c != NULL)
            (void) git_do_args (git, _ ("Unstage"), all ? every : one);
    }
    else
    {
        const char *one[] = {
            "reset", "-q", "HEAD", "--", c != NULL ? c->path : NULL, c != NULL ? c->orig : NULL,
            NULL
        };
        const char *every[] = { "reset", "-q", NULL };

        if (all || c != NULL)
            (void) git_do_args (git, _ ("Unstage"), all ? every : one);
    }
    git_window_reload_after_change (win);
}

/* --------------------------------------------------------------------------------------------- */

/* The change of a file in the work tree thrown away, after a question */
static void
git_discard (git_window_t *win)
{
    git_t *git = win->git;
    const git_change_t *c = git_list_change (win, GIT_LIST_UNSTAGED);
    char *question, *path;
    char code;
    gboolean ok;

    // the change of the cursor on the tab Status alone: the Log shows no change to throw away
    if (win->tab != GIT_TAB_STATUS || win->list != GIT_LIST_UNSTAGED || c == NULL)
        return;
    if (c->code == 'U')
    {
        git->host->message (git->host, D_ERROR, _ ("Discard"),
                            _ ("The file has a conflict: resolve it or stage it first."));
        return;
    }
    // the change copied: a pull done in the terminal while the question is up reads the status
    code = c->code;
    path = g_strdup (c->path);
    // a file added with git add -N has nothing in git to go back to: it is deleted, as one git
    // does not track
    question =
        g_strdup_printf (code == '?' || code == 'A' ? _ ("Delete %s?\nGit does not track it.")
                                                    : _ ("Discard the changes of %s?"),
                         path);
    ok = query_dialog (_ ("Discard"), question, D_ERROR, 2, _ ("&Yes"), _ ("&No")) == 0;
    g_free (question);
    if (ok && code == '?')
    {
        (void) git_do (git, _ ("Discard"), "clean", "-f", "-d", "-q", "--", path, (char *) NULL);
    }
    else if (ok && code == 'A')
    {
        (void) git_do (git, _ ("Discard"), "rm", "-f", "-q", "--", path, (char *) NULL);
    }
    else if (ok)
    {
        (void) git_do (git, _ ("Discard"), "checkout", "-q", "--", path, (char *) NULL);
    }
    g_free (path);
    if (!ok)
        return;
    git_window_reload_after_change (win);
}

/* --------------------------------------------------------------------------------------------- */

/* The file of the change the cursor is on, in a window of the editor */
static void
git_edit_file (git_window_t *win)
{
    git_t *git = win->git;
    const git_change_t *c = git_list_change (win, git_list_of_tab (win));
    char *file;

    if (c == NULL || git->root == NULL)
        return;
    file = g_build_filename (git->root, c->path, (char *) NULL);
    if (g_file_test (file, G_FILE_TEST_IS_REGULAR) && git->host->open_file (git->host, file))
    {
        void *edit = git->host->window_current (git->host);

        // the whole screen over the window Git, which comes back when it is closed: not in the
        // place of the message, which is where a file opens otherwise
        if (edit != NULL && edit_widget_is_editor (CONST_WIDGET (edit)) && edit != git->msg_window)
        {
            if (EDIT_WINDOW (edit)->fullscreen == 0)
                edit_window_toggle_fullscreen (EDIT_WINDOW (edit));
            git->opened = edit;
            git->opened_from_message = FALSE;
            widget_draw (WIDGET (CONST_WIDGET (edit)->owner));
        }
    }
    else if (!g_file_test (file, G_FILE_TEST_IS_REGULAR))
        git->host->message (git->host, D_NORMAL, _ ("Git"), _ ("The file is not there."));
    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */

/* The next commits of the log or of the graph read, the cursor on the commit it was on; FALSE
   when there are none */
static gboolean
git_list_more (git_window_t *win, int list)
{
    git_t *git = win->git;

    if (list == GIT_LIST_COMMITS)
        return git_read_log_more (git);
    if (list != GIT_LIST_GRAPH || !git->graph_more)
        return FALSE;
    // the graph of all of them again: where a branch goes depends on the whole history
    git_graph_reread (win, git->graph_limit * 2);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
git_window_key (git_window_t *win, int key)
{
    git_t *git = win->git;
    const int list = git_list_of_tab (win);
    git_cursor_t *c = &win->cursor[list];
    int last = git_list_len (win, list) - 1;
    const int body = git_body_lines (win);

    switch (key)
    {
    case KEY_F (1):
        git_help ();
        return TRUE;
    case KEY_F (10):
        git_group_close (win);
        return TRUE;
    case '1':
        git_window_tab (win, GIT_TAB_STATUS);
        return TRUE;
    case '2':
        git_window_tab (win, GIT_TAB_LOG);
        return TRUE;
    case '3':
        git_window_tab (win, GIT_TAB_GRAPH);
        return TRUE;
    case 'v':
        if (win->tab != GIT_TAB_GRAPH)
            return FALSE;
        git_graph_choose_refs (win);
        return TRUE;
    case KEY_F (5):
    case XCTRL ('r'):
        git_window_reread (win);
        return TRUE;
    case KEY_F (6):
        git_remote (win, "push");
        return TRUE;
    case KEY_F (7):
        git_remote (win, "pull");
        return TRUE;
    case KEY_F (15):
        git_remote (win, "fetch");
        return TRUE;
    case '\t':
    {
        // the next window on the screen: the changes, the staged ones, the diff
        const git_pane_t *now = git_focus_pane_shown (win);
        int role = now != NULL ? now->role : GIT_PANE_DIFF;
        int n;

        for (n = 0; n < GIT_PANE_COUNT; n++)
        {
            role = (role + 1) % GIT_PANE_COUNT;
            if (git_pane_shown (win, role))
                break;
        }
        win->in_diff = role == GIT_PANE_DIFF;
        if (role == GIT_PANE_STAGED)
            win->list = GIT_LIST_STAGED;
        else if (role == GIT_PANE_LIST && win->tab == GIT_TAB_STATUS)
            win->list = GIT_LIST_UNSTAGED;
        git_diff_update (win);
        git_group_draw (win);
    }
        return TRUE;
    case ESC_CHAR:
        if (win->in_diff)
            win->in_diff = FALSE;
        else if (list == GIT_LIST_FILES)
            git_close_commit (git);
        else
        {
            git_group_close (win);
            return TRUE;
        }
        git_diff_update (win);
        git_group_draw (win);
        return TRUE;
    default:
        break;
    }

    if (win->in_diff)
    {
        const int total = win->diff != NULL ? (int) win->diff->len : 0;
        const int bottom = MAX (0, total - body);

        switch (key)
        {
        case KEY_UP:
            win->diff_top = MAX (0, win->diff_top - 1);
            break;
        case KEY_DOWN:
            win->diff_top = MIN (bottom, win->diff_top + 1);
            break;
        case KEY_PPAGE:
            win->diff_top = MAX (0, win->diff_top - body);
            break;
        case KEY_NPAGE:
        case ' ':
            win->diff_top = MIN (bottom, win->diff_top + body);
            break;
        case KEY_HOME:
            win->diff_top = 0;
            win->diff_left = 0;
            break;
        case KEY_END:
            win->diff_top = bottom;
            break;
        case KEY_RIGHT:
            win->diff_left = MIN (win->diff_left + 8, MAX (0, win->diff_width - 1));
            break;
        case KEY_LEFT:
            if (win->diff_left == 0)
                win->in_diff = FALSE;
            win->diff_left = MAX (0, win->diff_left - 8);
            break;
        case '\n':
        case KEY_ENTER:
            win->in_diff = FALSE;
            break;
        default:
            return FALSE;
        }
        git_group_draw (win);
        return TRUE;
    }

    // at the last commit read, the next ones: on the way down to it, and End at it again
    if ((list == GIT_LIST_COMMITS || list == GIT_LIST_GRAPH)
        && ((key == KEY_DOWN && c->selected >= last)
            || (key == KEY_NPAGE && c->selected + git_list_rows (win, list) > last)
            || (key == KEY_END && c->selected >= last))
        && git_list_more (win, list))
        last = git_list_len (win, list) - 1;

    switch (key)
    {
    case KEY_UP:
        if (c->selected == 0 && list == GIT_LIST_STAGED)
            win->list = GIT_LIST_UNSTAGED;
        c->selected = MAX (0, c->selected - 1);
        break;
    case KEY_DOWN:
        if (c->selected >= last && list == GIT_LIST_UNSTAGED && git->staged->len != 0)
            win->list = GIT_LIST_STAGED;
        c->selected = MIN (MAX (last, 0), c->selected + 1);
        break;
    case KEY_PPAGE:
        c->selected = MAX (0, c->selected - git_list_rows (win, list));
        break;
    case KEY_NPAGE:
        c->selected = MIN (MAX (last, 0), c->selected + git_list_rows (win, list));
        break;
    case KEY_HOME:
        c->selected = 0;
        break;
    case KEY_END:
        c->selected = MAX (last, 0);
        break;
    case KEY_RIGHT:
        win->in_diff = TRUE;
        break;
    case KEY_LEFT:
    case KEY_BACKSPACE:
        if (list != GIT_LIST_FILES)
            return FALSE;
        git_close_commit (git);
        break;
    case '\n':
    case KEY_ENTER:
    case ' ':
        if (list == GIT_LIST_UNSTAGED || list == GIT_LIST_STAGED)
        {
            git_stage (win, FALSE);
            return TRUE;
        }
        if ((list == GIT_LIST_COMMITS || list == GIT_LIST_GRAPH) && git_list_commit (win) != NULL)
        {
            git_open_commit (git, git_list_commit (win));
            win->cursor[GIT_LIST_FILES].selected = 0;
            win->cursor[GIT_LIST_FILES].top = 0;
        }
        else if (list == GIT_LIST_FILES && key != ' ')
            win->in_diff = TRUE;
        break;
    case 'a':
        if (list != GIT_LIST_UNSTAGED && list != GIT_LIST_STAGED)
            return FALSE;
        git_stage (win, TRUE);
        return TRUE;
    case 'd':
    case KEY_DC:
    case KEY_F (8):
        git_discard (win);
        return TRUE;
    case 'c':
    case KEY_F (2):
        if (win->tab != GIT_TAB_STATUS)
            return FALSE;
        // to the message of the commit at the top left; its F2 commits
        git_message_start (git, GIT_MSG_COMMIT, NULL);
        return TRUE;
    case 'A':
    case KEY_F (3):
        if (win->tab != GIT_TAB_STATUS)
            return FALSE;
        git_commit (git, TRUE);
        return TRUE;
    case 'r':
    case 'e':
    case KEY_F (4):
        if (win->tab == GIT_TAB_STATUS || (list == GIT_LIST_FILES && key == 'e'))
            git_edit_file (win);
        else if (key != 'e')
        {
            git_commit_t *commit = list == GIT_LIST_FILES ? git->commit : git_list_commit (win);

            if (commit != NULL)
            {
                if (win->tab == GIT_TAB_GRAPH
                    && !git_succeeds (git->root, "merge-base", "--is-ancestor", commit->sha, "HEAD",
                                      (char *) NULL))
                {
                    git->host->message (git->host, D_NORMAL, _ ("Reword"),
                                        _ ("This commit is not on the current branch."));
                    return TRUE;
                }
                git_message_start (git, GIT_MSG_REWORD, commit);
            }
        }
        return TRUE;
    default:
        return FALSE;
    }
    if (list == GIT_LIST_GRAPH)
        git_graph_cursor_skip (win, key == KEY_UP || key == KEY_PPAGE ? -1 : 1);
    git_cursor_show (win, git_list_of_tab (win));
    git_diff_update (win);
    git_group_draw (win);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/* A window of the group has the focus: the list or the diff it shows has the keys */
static void
git_pane_focused (git_pane_t *pane)
{
    git_window_t *win = pane->group;
    // from another window of the editor, not from one of the group a moment ago: the files saved
    // there show
    const gboolean from_outside = g_get_monotonic_time () - win->unfocused > G_USEC_PER_SEC / 5;

    if (!win->moving_focus)
        switch (pane->role)
        {
        case GIT_PANE_LIST:
            win->in_diff = FALSE;
            if (win->tab == GIT_TAB_STATUS)
                win->list = GIT_LIST_UNSTAGED;
            break;
        case GIT_PANE_STAGED:
            win->in_diff = FALSE;
            win->list = GIT_LIST_STAGED;
            if (win->tab != GIT_TAB_STATUS)
            {
                win->tab = GIT_TAB_STATUS;
                g_clear_pointer (&win->diff_of, g_free);
            }
            break;
        default:
            win->in_diff = TRUE;
            break;
        }
    if (from_outside && !win->moving_focus)
        git_window_reload (win);
    else
    {
        git_diff_update (win);
        git_group_draw (win);
    }
    git_window_buttonbar (win);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
git_pane_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    git_pane_t *pane = (git_pane_t *) w;
    git_window_t *win = pane->group;

    switch (msg)
    {
    case MSG_DRAW:
        git_pane_draw (pane);
        return MSG_HANDLED;
    case MSG_FOCUS:
        if (win != NULL && win->git != NULL)
            git_pane_focused (pane);
        else
            git_pane_draw (pane);
        return MSG_HANDLED;
    case MSG_UNFOCUS:
        if (win != NULL)
            win->unfocused = g_get_monotonic_time ();
        git_pane_draw (pane);
        return MSG_HANDLED;
    case MSG_KEY:
        return win != NULL && win->git != NULL && git_window_key (win, parm) ? MSG_HANDLED
                                                                             : MSG_NOT_HANDLED;
    case MSG_ACTION:
        // a button of the bar, the key it stands for
        if (win != NULL && win->git != NULL && sender != NULL
            && sender == WIDGET (buttonbar_find (DIALOG (w->owner))))
            return git_window_key (win, parm) ? MSG_HANDLED : MSG_NOT_HANDLED;
        if (parm == CK_Close)
        {
            (void) git_pane_close (&pane->window);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;
    case MSG_CURSOR:
        if (win != NULL && pane->role != GIT_PANE_DIFF)
        {
            const int list = pane->role == GIT_PANE_STAGED ? GIT_LIST_STAGED
                : win->tab == GIT_TAB_STATUS               ? GIT_LIST_UNSTAGED
                                                           : git_list_of_tab (win);
            const git_cursor_t *c = &win->cursor[list];

            widget_gotoyx (w, (pane->role == GIT_PANE_LIST ? 2 : 1) + c->selected - c->top, 1);
        }
        else
            widget_gotoyx (w, 1, 1);
        return MSG_HANDLED;
    case MSG_DESTROY:
        if (win != NULL)
        {
            int i;
            gboolean left = FALSE;

            win->pane[pane->role] = NULL;
            for (i = 0; i < GIT_PANE_COUNT; i++)
                left = left || win->pane[i] != NULL;
            // the last window of the group takes the group with it
            if (!left)
            {
                if (win->git != NULL)
                    win->git->win = NULL;
                if (win->diff != NULL)
                    g_ptr_array_free (win->diff, TRUE);
                g_free (win->diff_of);
                g_free (win->diff_title);
                g_free (win);
            }
        }
        return group_default_callback (w, sender, msg, parm, data);
    default:
        return group_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
git_pane_mouse (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    git_pane_t *pane = (git_pane_t *) w;
    git_window_t *win = pane->group;
    int list, index = -1;

    if (win == NULL || win->git == NULL)
        return;
    list = pane->role == GIT_PANE_STAGED ? GIT_LIST_STAGED
        : win->tab == GIT_TAB_STATUS     ? GIT_LIST_UNSTAGED
                                         : git_list_of_tab (win);
    // the row of a list under the mouse
    if (pane->role == GIT_PANE_LIST && event->y >= 2)
        index = win->cursor[list].top + event->y - 2;
    else if (pane->role == GIT_PANE_STAGED && event->y >= 1)
        index = win->cursor[list].top + event->y - 1;

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        widget_select (w);
        if (pane->role == GIT_PANE_LIST && event->y == 1 && win->refs_cols != 0
            && event->x >= win->refs_x && event->x < win->refs_x + win->refs_cols)
        {
            git_graph_choose_refs (win);
            break;
        }
        if (index >= 0 && index < git_list_len (win, list))
            win->cursor[list].selected = index;
        if (list == GIT_LIST_GRAPH)
            git_graph_cursor_skip (win, 1);
        git_diff_update (win);
        git_group_draw (win);
        break;
    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE && index >= 0 && index == win->cursor[list].selected)
            (void) git_window_key (win, '\n');
        break;
    case MSG_MOUSE_SCROLL_UP:
    case MSG_MOUSE_SCROLL_DOWN:
    {
        const int step = msg == MSG_MOUSE_SCROLL_UP ? -3 : 3;

        if (pane->role == GIT_PANE_DIFF)
        {
            const int total = win->diff != NULL ? (int) win->diff->len : 0;

            win->diff_top = CLAMP (win->diff_top + step, 0, MAX (0, total - git_body_lines (win)));
        }
        else
        {
            git_cursor_t *c = &win->cursor[list];

            c->top = CLAMP (c->top + step, 0,
                            MAX (0, git_list_len (win, list) - git_list_rows (win, list)));
        }
        git_group_draw (win);
    }
    break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
git_pane_title_of (const WEditWindow *ew)
{
    const git_pane_t *pane = (const git_pane_t *) ew;
    const char *root =
        pane->group != NULL && pane->group->git != NULL && pane->group->git->root != NULL
        ? pane->group->git->root
        : "";

    switch (pane->role)
    {
    case GIT_PANE_LIST:
        return g_strdup_printf (_ ("Git %s"), root);
    case GIT_PANE_STAGED:
        return g_strdup_printf (_ ("Git staged %s"), root);
    default:
        return g_strdup_printf (_ ("Git diff %s"), root);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A click on the title: the tabs of the window of the list */
static gboolean
git_pane_title_click (WEditWindow *ew, int x)
{
    git_pane_t *pane = (git_pane_t *) ew;
    git_window_t *win = pane->group;
    const int status_end = 2 + str_term_width1 (GIT_TAB_STATUS_TITLE);
    const int log_start = status_end + 1;
    const int log_end = log_start + str_term_width1 (GIT_TAB_LOG_TITLE);
    const int graph_start = log_end + 1;
    const int graph_end = graph_start + str_term_width1 (GIT_TAB_GRAPH_TITLE);

    if (win == NULL || win->git == NULL || pane->role != GIT_PANE_LIST)
        return FALSE;
    if (x >= 2 && x < status_end)
        git_window_tab (win, GIT_TAB_STATUS);
    else if (x >= log_start && x < log_end)
        git_window_tab (win, GIT_TAB_LOG);
    else if (x >= graph_start && x < graph_end)
        git_window_tab (win, GIT_TAB_GRAPH);
    else
        return FALSE;
    // the list has the keys, of the tab chosen
    win->in_diff = FALSE;
    git_group_draw (win);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A scrollbar moved: the list or the diff goes there */
static void
git_pane_scrolled (WEditWindow *ew, gboolean vertical, long pos)
{
    git_pane_t *pane = (git_pane_t *) ew;
    git_window_t *win = pane->group;

    if (win == NULL)
        return;
    if (pane->role == GIT_PANE_DIFF)
    {
        const int total = win->diff != NULL ? (int) win->diff->len : 0;

        if (vertical)
            win->diff_top = (int) CLAMP (pos, 0, MAX (0, total - git_body_lines (win)));
        else
            win->diff_left = (int) CLAMP (pos, 0, MAX (0, win->diff_width - 1));
    }
    else
    {
        const int list = pane->role == GIT_PANE_STAGED ? GIT_LIST_STAGED
            : win->tab == GIT_TAB_STATUS               ? GIT_LIST_UNSTAGED
                                                       : git_list_of_tab (win);

        win->cursor[list].top =
            (int) CLAMP (pos, 0, MAX (0, git_list_len (win, list) - git_list_rows (win, list)));
    }
    git_group_draw (win);
}

/* --------------------------------------------------------------------------------------------- */

/* the lists: a scrollbar down the right side */
static const edit_window_class_t git_pane_class = {
    .callback = git_pane_callback,
    .mouse_callback = git_pane_mouse,
    .get_title = git_pane_title_of,
    .close = git_pane_close,
    .min_lines = 3,
    .min_cols = 10,
    .vbar = TRUE,
    .scrolled = git_pane_scrolled,
    .title_click = git_pane_title_click,
};

/* the diff: one along the bottom too */
static const edit_window_class_t git_diff_pane_class = {
    .callback = git_pane_callback,
    .mouse_callback = git_pane_mouse,
    .get_title = git_pane_title_of,
    .close = git_pane_close,
    .min_lines = 3,
    .min_cols = 10,
    .vbar = TRUE,
    .hbar_x = 1,
    .scrolled = git_pane_scrolled,
};

/* --------------------------------------------------------------------------------------------- */

/* A window, a file window as well, at @r: not fullscreen, not filling the room of the docks */
static void
git_place (WEditWindow *ew, const WRect *r)
{
    Widget *w = WIDGET (ew);

    ew->fullscreen = 0;
    ew->dock_fill = 0;
    ew->room_fullscreen = 0;
    w->pos_flags = WPOS_KEEP_DEFAULT;
    if (!rects_are_equal (&w->rect, r))
    {
        WRect to = *r;

        widget_set_size_rect (w, &to);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
git_pane_new (git_window_t *win, int role, const WRect *r)
{
    git_pane_t *pane = g_new0 (git_pane_t, 1);

    edit_window_init (&pane->window, r,
                      role == GIT_PANE_DIFF ? &git_diff_pane_class : &git_pane_class);
    pane->window.fullscreen = 0;
    pane->group = win;
    pane->role = role;
    win->pane[role] = pane;
    win->git->host->window_add (win->git->host, pane);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Run git in the work tree with @args; on failure say what git said, under @title */
gboolean
git_do_args (git_t *git, const char *title, const char *const *args)
{
    git_output_t o;
    gboolean ok;

    ok = git_run (git->root, args, NULL, &o) && o.status == 0;
    if (!ok)
    {
        char *why = git_output_error (&o);

        git->host->message (git->host, D_ERROR, title, why);
        g_free (why);
    }
    git_output_clear (&o);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

git_commit_t *
git_list_commit (const git_window_t *win)
{
    const int list = win->tab == GIT_TAB_GRAPH ? GIT_LIST_GRAPH : GIT_LIST_COMMITS;
    const GPtrArray *a = list == GIT_LIST_GRAPH ? win->git->graph_commits : win->git->commits;
    int index = win->cursor[list].selected;

    if (list == GIT_LIST_GRAPH)
        index = git_graph_commit_of_row (win->git, index);

    if (a == NULL || index < 0 || index >= (int) a->len)
        return NULL;
    return g_ptr_array_index (a, index);
}

/* --------------------------------------------------------------------------------------------- */

void
git_cursor_show (git_window_t *win, int list)
{
    git_cursor_t *c = &win->cursor[list];
    const int rows = git_list_rows (win, list);

    if (c->selected < c->top)
        c->top = c->selected;
    else if (c->selected >= c->top + rows)
        c->top = c->selected - rows + 1;
}

/* --------------------------------------------------------------------------------------------- */

void
git_group_draw (git_window_t *win)
{
    int i;

    git_group_sync_focus (win);
    for (i = 0; i < GIT_PANE_COUNT; i++)
        if (win->pane[i] != NULL)
            widget_draw (WIDGET (win->pane[i]));
}

/* --------------------------------------------------------------------------------------------- */

/* The diff of what the cursor is on, made when that has changed */
void
git_diff_update (git_window_t *win)
{
    git_t *git = win->git;
    const int list = git_list_of_tab (win);
    const git_change_t *c = NULL;
    const git_commit_t *commit = NULL;
    GPtrArray *args = g_ptr_array_new ();
    char *of = NULL, *range = NULL;
    char *text;
    gsize len = 0;

    if (list == GIT_LIST_COMMITS || list == GIT_LIST_GRAPH)
    {
        commit = git_list_commit (win);
        if (commit != NULL)
        {
            of = g_strconcat ("c:", commit->sha, (char *) NULL);
            g_ptr_array_add (args, (gpointer) "show");
            g_ptr_array_add (args, (gpointer) "--stat");
            g_ptr_array_add (args, (gpointer) "--summary");
            g_ptr_array_add (args, (gpointer) "--format=fuller");
            g_ptr_array_add (args, commit->sha);
        }
    }
    else if (list == GIT_LIST_FILES && win->cursor[list].selected == 0)
    {
        commit = git->commit;
        of = g_strconcat ("c:", commit->sha, (char *) NULL);
        g_ptr_array_add (args, (gpointer) "show");
        g_ptr_array_add (args, (gpointer) "--stat");
        g_ptr_array_add (args, (gpointer) "--summary");
        g_ptr_array_add (args, (gpointer) "--format=fuller");
        g_ptr_array_add (args, commit->sha);
    }
    else
    {
        c = git_list_change (win, list);
        if (c != NULL)
            of = g_strdup_printf ("%d:%c:%s:%s", list, c->code, c->path,
                                  c->orig != NULL ? c->orig : "");
        if (c == NULL)
            g_ptr_array_set_size (args, 0);
        else if (list == GIT_LIST_FILES)
        {
            commit = git->commit;
            if (commit->parents > 0)
            {
                range = g_strconcat (commit->sha, "^", (char *) NULL);
                g_ptr_array_add (args, (gpointer) "diff");
                g_ptr_array_add (args, (gpointer) "-M");
                g_ptr_array_add (args, range);
                g_ptr_array_add (args, commit->sha);
            }
            else
            {
                g_ptr_array_add (args, (gpointer) "show");
                g_ptr_array_add (args, (gpointer) "--format=");
                g_ptr_array_add (args, commit->sha);
            }
        }
        else if (c->code == '?')
        {
            // a file git does not know: all of it is new
            g_ptr_array_add (args, (gpointer) "diff");
            g_ptr_array_add (args, (gpointer) "--no-index");
            g_ptr_array_add (args, (gpointer) "--");
            g_ptr_array_add (args, (gpointer) "/dev/null");
        }
        else
        {
            g_ptr_array_add (args, (gpointer) "diff");
            g_ptr_array_add (args, (gpointer) "-M");
            if (list == GIT_LIST_STAGED)
                g_ptr_array_add (args, (gpointer) "--cached");
        }
        if (c != NULL && c->code != '?')
            g_ptr_array_add (args, (gpointer) "--");
        if (c != NULL && c->orig != NULL)
            g_ptr_array_add (args, c->orig);
        if (c != NULL)
            g_ptr_array_add (args, c->path);
    }
    g_ptr_array_add (args, NULL);

    if (of == NULL || g_strcmp0 (of, win->diff_of) != 0)
    {
        if (of != NULL)
            text = git_capture_args (git->root, (const char *const *) args->pdata, &len);
        else
            text = g_strdup ("");
        if (of != NULL && len == 0 && c != NULL)
        {
            g_free (text);
            text = g_strdup (c->code == '?' && g_str_has_suffix (c->path, "/")
                                 ? _ ("A directory git does not track.")
                                 : _ ("No change to show."));
            len = strlen (text);
        }
        git_diff_set (win, text, len);
        g_free (text);
        g_free (win->diff_title);
        win->diff_title =
            commit != NULL && (list == GIT_LIST_COMMITS || list == GIT_LIST_GRAPH || c == NULL)
            ? g_strdup_printf (_ ("Commit %.7s"), commit->sha)
            : c != NULL ? g_strdup (c->path)
                        : NULL;
        g_free (win->diff_of);
        win->diff_of = of;
        of = NULL;
        win->diff_top = 0;
        win->diff_left = 0;
    }
    g_free (of);
    g_free (range);
    g_ptr_array_free (args, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* Everything read again: the status, and the log when the tab shows it */
void
git_window_reload (git_window_t *win)
{
    git_t *git = win->git;
    int i;

    git_read_status (git);
    if (win->tab == GIT_TAB_LOG)
        git_read_log (git);
    else if (win->tab == GIT_TAB_GRAPH)
        git_read_graph (git);
    for (i = 0; i < GIT_LIST_COUNT; i++)
        git_cursor_clamp (win, i);
    // the same name may have another diff now
    g_clear_pointer (&win->diff_of, g_free);
    git_diff_update (win);
    git_group_draw (win);
}

/* --------------------------------------------------------------------------------------------- */
/* A window of the group closed by the user: it goes, the others take its room; the last one gone
   closes the window Git */
gboolean
git_pane_close (WEditWindow *ew)
{
    git_pane_t *pane = (git_pane_t *) ew;
    git_window_t *win = pane->group;
    int role;

    if (win == NULL)
    {
        edit_window_destroy (ew);
        return TRUE;
    }
    win->hidden[pane->role] = TRUE;
    for (role = 0; role < GIT_PANE_COUNT; role++)
        if (!win->hidden[role])
            break;
    if (role == GIT_PANE_COUNT)
    {
        git_group_close (win);
        return TRUE;
    }
    win->moving_focus = TRUE;
    widget_hide (WIDGET (pane));
    win->moving_focus = FALSE;
    git_group_arrange (win);
    // the keys to another window of the group, not to whatever window the editor goes to
    {
        git_pane_t *target = git_focus_pane_shown (win);

        if (target != NULL)
        {
            win->moving_focus = TRUE;
            win->git->host->window_show (win->git->host, target);
            win->moving_focus = FALSE;
        }
    }
    git_window_buttonbar (win);
    git_group_draw (win);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Everything read again, the log too */
void
git_window_reread (git_window_t *win)
{
    git_forget_log (win->git);
    git_window_reload (win);
    if (win->tab != GIT_TAB_STATUS)
        git_window_tab (win, win->tab);
}

/* --------------------------------------------------------------------------------------------- */
/* The windows of the group where they go: at the left the message being written, a third of the
   height, then the changes and the staged ones (the files of the commit alone for a reword);
   the diff at the right, all the height.  Those closed by the user leave their room to the
   others */
void
git_group_arrange (git_window_t *win)
{
    git_t *git = win->git;
    WRect area;
    const WRect *a = &area;
    const gboolean reword = git->msg_window != NULL && git->msg_mode == GIT_MSG_REWORD;
    const gboolean list = win->pane[GIT_PANE_LIST] != NULL && !win->hidden[GIT_PANE_LIST];
    const gboolean staged =
        win->pane[GIT_PANE_STAGED] != NULL && !win->hidden[GIT_PANE_STAGED] && !reword;
    const gboolean diff = win->pane[GIT_PANE_DIFF] != NULL && !win->hidden[GIT_PANE_DIFF];
    const gboolean column = git->msg_window != NULL || list || staged;
    int left, y, rest;
    WRect r;
    int i;

    // the screen but the docks, which a terminal at the bottom may have come to since
    edit_dock_free_area (DIALOG (git->host->host_data), &area);
    y = a->y;
    rest = a->lines;
    // the column at the left all the width without the diff; the diff all of it without the column
    left = !diff ? a->cols
        : column ? CLAMP (a->cols * (win->tab == GIT_TAB_GRAPH ? 1 : 2)
                              / (win->tab == GIT_TAB_GRAPH ? 2 : 5),
                          MIN (44, MAX (10, a->cols - 20)), MAX (10, a->cols - 20))
                 : 0;
    r.x = a->x;
    r.cols = left;
    if (git->msg_window != NULL)
    {
        r.y = y;
        r.lines = list || staged ? MAX (5, a->lines / 3) : rest;
        git_place (EDIT_WINDOW (git->msg_window), &r);
        y += r.lines;
        rest -= r.lines;
    }
    if (list)
    {
        r.y = y;
        r.lines = staged ? MAX (3, rest / 2) : rest;
        git_place (EDIT_WINDOW (win->pane[GIT_PANE_LIST]), &r);
        y += r.lines;
        rest -= r.lines;
    }
    if (staged)
    {
        r.y = y;
        r.lines = MAX (3, rest);
        git_place (EDIT_WINDOW (win->pane[GIT_PANE_STAGED]), &r);
    }
    if (diff)
    {
        r = *a;
        r.x = a->x + left;
        r.cols = a->cols - left;
        git_place (EDIT_WINDOW (win->pane[GIT_PANE_DIFF]), &r);
    }

    // on the screen those that have their room, the others off it
    win->moving_focus = TRUE;
    for (i = 0; i < GIT_PANE_COUNT; i++)
        if (win->pane[i] != NULL)
        {
            Widget *pw = WIDGET (win->pane[i]);
            const gboolean show = i == GIT_PANE_LIST ? list : i == GIT_PANE_STAGED ? staged : diff;

            if (show && !widget_get_state (pw, WST_VISIBLE))
                widget_show (pw);
            else if (!show && widget_get_state (pw, WST_VISIBLE))
                widget_hide (pw);
        }
    win->moving_focus = FALSE;
    if (win->pane[GIT_PANE_DIFF] != NULL && WIDGET (win->pane[GIT_PANE_DIFF])->owner != NULL)
        widget_draw (WIDGET (WIDGET (win->pane[GIT_PANE_DIFF])->owner));
}

/* --------------------------------------------------------------------------------------------- */
/* The windows of the group to the front, the keys to the one of the cursor */
void
git_group_show (git_window_t *win)
{
    git_t *git = win->git;
    git_pane_t *target;
    int i;

    git_group_arrange (win);
    target = git_focus_pane_shown (win);
    // each one shown takes the focus on its way: the cursor stays where it was
    win->moving_focus = TRUE;
    for (i = 0; i < GIT_PANE_COUNT; i++)
        if (git_pane_shown (win, i) && win->pane[i] != target)
            git->host->window_show (git->host, win->pane[i]);
    if (target != NULL)
        git->host->window_show (git->host, target);
    win->moving_focus = FALSE;
    git_window_reload (win);
}

/* --------------------------------------------------------------------------------------------- */

/* The window on the work tree of @edit, in the tab @tab */
void
git_window_open (git_t *git, void *edit, int tab)
{
    git_window_t *win;
    char *root = git_root_of (git, edit);
    WRect area, r;

    if (root == NULL)
    {
        git->host->message (git->host, D_NORMAL, _ ("Git"),
                            _ ("The file is not in a git work tree."));
        return;
    }
    if (g_strcmp0 (root, git->root) != 0)
    {
        // another work tree: the message of the last one in the window Git goes with it, its
        // commit kept for the next time; one kept by the user keeps the work tree
        if (git->msg_window != NULL && g_strcmp0 (git->msg_root, root) != 0
            && !git_message_set_aside (git))
        {
            g_free (root);
            return;
        }
        git_forget_log (git);
        g_free (git->root);
        git->root = root;
        root = NULL;
        if (git->win != NULL)
        {
            memset (git->win->cursor, 0, sizeof (git->win->cursor));
            git->win->in_diff = FALSE;
        }
    }
    g_free (root);

    if (git->win != NULL)
    {
        git_group_show (git->win);
        git_window_tab (git->win, tab);
        git_message_draft (git);
        return;
    }

    // where the windows of the files are: the screen but the docks
    edit_dock_free_area (DIALOG (git->host->host_data), &area);
    win = g_new0 (git_window_t, 1);
    win->git = git;
    win->tab = tab;
    win->list = GIT_LIST_UNSTAGED;
    git->win = win;
    git_read_status (git);
    if (tab == GIT_TAB_LOG)
        git_read_log (git);
    else if (tab == GIT_TAB_GRAPH)
        git_read_graph (git);

    // frame to frame, so that the sticky windows keep them together
    r = area;
    // each one added takes the focus on its way: it changes no tab
    win->moving_focus = TRUE;
    git_pane_new (win, GIT_PANE_DIFF, &r);
    git_pane_new (win, GIT_PANE_STAGED, &r);
    git_pane_new (win, GIT_PANE_LIST, &r);
    win->moving_focus = FALSE;
    git_group_arrange (win);
    git_diff_update (win);
    git_group_draw (win);
    git_message_draft (git);
}

/* --------------------------------------------------------------------------------------------- */

/* The window of the message closed before a commit: no commit; the message is kept for the
   next one only when git refused it */
void
git_group_show_later (void *data)
{
    git_t *git = (git_t *) data;

    if (git->win != NULL)
        git_group_show (git->win);
    // a file the menu of the message opened: back to the message
    if (git->opened_from_message && git->msg_window != NULL)
        git->host->window_show (git->host, git->msg_window);
    git->opened_from_message = FALSE;
}

/* --------------------------------------------------------------------------------------------- */
