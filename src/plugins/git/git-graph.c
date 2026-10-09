/*
   The git plugin: the tab Graph, the branches drawn as git-graph does.

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

/** \file git-graph.c
 *  \brief Source: the tab Graph of the git plugin
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/tty/tty.h"

#include "src/editor/edit-impl.h"

#include "git-private.h"

/*** global variables ****************************************************************************/

const char *const git_graph_refs_key[GIT_GRAPH_REFS_COUNT] = {
    "current",
    "local",
    "remote",
    "all",
};

const char *const git_graph_refs_label[GIT_GRAPH_REFS_COUNT] = {
    N_ ("Current"),
    N_ ("Local"),
    N_ ("Local+remote"),
    N_ ("All"),
};

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* A character of the graph: [git-graph] of the skin under @key, one character, ASCII on a
   terminal of 8 bits; else @fallback */
static mc_tty_char_t
git_graph_glyph (const char *key, mc_tty_char_t fallback)
{
    char *text = mc_skin_get ("git-graph", key, "");
    mc_tty_char_t c = fallback;

    if (text != NULL && *text != '\0' && g_utf8_validate (text, -1, NULL)
        && *g_utf8_next_char (text) == '\0')
    {
        const gunichar u = g_utf8_get_char (text);

        if (mc_global.utf8_display || u < 0x80)
            c = (mc_tty_char_t) u;
    }
    g_free (text);
    return c;
}

/* --------------------------------------------------------------------------------------------- */

static mc_tty_char_t
git_graph_char (const git_t *git, guint8 mask)
{
    if ((mask & GIT_GRAPH_NODE) != 0)
        return git->glyph[(mask & GIT_GRAPH_MERGE) != 0 ? GIT_GLYPH_MERGE : GIT_GLYPH_COMMIT];
    switch (mask)
    {
    case 0:
        return ' ';
    case GIT_GRAPH_UP:
    case GIT_GRAPH_DOWN:
    case GIT_GRAPH_UP | GIT_GRAPH_DOWN:
        return git->glyph[GIT_GLYPH_VERT];
    case GIT_GRAPH_LEFT:
    case GIT_GRAPH_RIGHT:
    case GIT_GRAPH_LEFT | GIT_GRAPH_RIGHT:
        return git->glyph[GIT_GLYPH_HORIZ];
    case GIT_GRAPH_DOWN | GIT_GRAPH_RIGHT:
        return git->glyph[GIT_GLYPH_LEFTTOP];
    case GIT_GRAPH_DOWN | GIT_GRAPH_LEFT:
        return git->glyph[GIT_GLYPH_RIGHTTOP];
    case GIT_GRAPH_UP | GIT_GRAPH_RIGHT:
        return git->glyph[GIT_GLYPH_LEFTBOTTOM];
    case GIT_GRAPH_UP | GIT_GRAPH_LEFT:
        return git->glyph[GIT_GLYPH_RIGHTBOTTOM];
    case GIT_GRAPH_UP | GIT_GRAPH_DOWN | GIT_GRAPH_RIGHT:
        return git->glyph[GIT_GLYPH_LEFTMIDDLE];
    case GIT_GRAPH_UP | GIT_GRAPH_DOWN | GIT_GRAPH_LEFT:
        return git->glyph[GIT_GLYPH_RIGHTMIDDLE];
    case GIT_GRAPH_DOWN | GIT_GRAPH_LEFT | GIT_GRAPH_RIGHT:
        return git->glyph[GIT_GLYPH_TOPMIDDLE];
    case GIT_GRAPH_UP | GIT_GRAPH_LEFT | GIT_GRAPH_RIGHT:
        return git->glyph[GIT_GLYPH_BOTTOMMIDDLE];
    default:
        return git->glyph[GIT_GLYPH_CROSS];
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The color of the branches of color @n of the graph */
static int
git_graph_lane_color (const git_t *git, int n)
{
    return n < GIT_GRAPH_COLOR_OTHER ? git->color_kind[n]
                                     : git->color_other[(n - GIT_GRAPH_COLOR_OTHER) % git->n_other];
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
git_read_graph (git_t *git)
{
    char *n;
    gsize len;
    char *data;

    if (git->graph_commits != NULL || git->root == NULL)
        return;
    if (git->graph_limit == 0)
        git->graph_limit = GIT_LOG_STEP;
    n = g_strdup_printf ("%d", git->graph_limit);
    {
        // HEAD in each, a detached one having no branch at it
        const char *args[10] = { "log", "--topo-order", "-n", n, GIT_LOG_FORMAT };
        int i = 5;

        switch (git->graph_refs)
        {
        case GIT_GRAPH_REFS_ALL:
            args[i++] = "--all";
            break;
        case GIT_GRAPH_REFS_REMOTE:
            args[i++] = "--remotes";
            MC_FALLTHROUGH;
        case GIT_GRAPH_REFS_LOCAL:
            args[i++] = "--branches";
            args[i++] = "--tags";
            MC_FALLTHROUGH;
        default:
            args[i++] = "HEAD";
            break;
        }
        args[i++] = "--";
        args[i] = NULL;
        data = git_capture_args (git->root, args, &len);
    }
    g_free (n);
    git->graph_commits = git_parse_log (data, len);
    git->graph_more = (int) git->graph_commits->len == git->graph_limit;
    {
        char *text = g_strstrip (git_capture (git->root, NULL, "remote", (char *) NULL));
        char **remotes = g_strsplit (text, "\n", -1);

        git->graph_rows = git_graph_build (git->graph_commits, (const char *const *) remotes);
        g_strfreev (remotes);
        g_free (text);
    }
    git->graph_cols = git->graph_rows->len != 0
        ? ((const git_graph_row_t *) g_ptr_array_index (git->graph_rows, 0))->cols
        : 0;
    if (git->log_head == NULL)
    {
        git->log_head = g_strdup (git->branch.sha);
        g_free (git->log_branch);
        git->log_branch = g_strdup (git->branch.head);
    }
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

/* The commit of row @row of the graph: that of a row that joins lines is the one under it; -1
   for none */
int
git_graph_commit_of_row (const git_t *git, int row)
{
    const GPtrArray *rows = git->graph_rows;

    for (; rows != NULL && row >= 0 && row < (int) rows->len; row++)
    {
        const git_graph_row_t *r = g_ptr_array_index (rows, row);

        if (r->commit >= 0)
            return r->commit;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

/* The cursor of the graph off a row that joins lines, @dir that way, else the other */
void
git_graph_cursor_skip (git_window_t *win, int dir)
{
    const GPtrArray *rows = win->git->graph_rows;
    git_cursor_t *c = &win->cursor[GIT_LIST_GRAPH];
    int i;

    if (rows == NULL)
        return;
    for (i = c->selected; i >= 0 && i < (int) rows->len; i += dir)
        if (((const git_graph_row_t *) g_ptr_array_index (rows, i))->commit >= 0)
        {
            c->selected = i;
            return;
        }
    for (i = c->selected; i >= 0 && i < (int) rows->len; i -= dir)
        if (((const git_graph_row_t *) g_ptr_array_index (rows, i))->commit >= 0)
        {
            c->selected = i;
            return;
        }
}

/* --------------------------------------------------------------------------------------------- */

/* A color of the graph: that of [git-graph] of the skin under @key, else @fallback */
static int
git_graph_color (const char *key, const char *fallback)
{
    char *name = mc_skin_get ("git-graph", key, fallback);
    const int color = mc_skin_color_on ("editor", "_default_", name);

    g_free (name);
    return color;
}

/* The colors of the branches: main, develop, release and hotfix as git-graph has them, and the
   others in turn, sixteen of them on a terminal of 256 colors, eight on one of 16; "others" in
   [git-graph] of the skin, separated by ';', instead */
void
git_graph_colors (git_t *git)
{
    const gboolean many = tty_use_256colors (NULL);
    const char *others_256 = "color170;color44;color214;color141;color48;color203;color39;"
                             "color227;color207;color87;color172;color99;color156;color211;"
                             "color75;color180";
    const char *others_16 = "magenta;cyan;brightmagenta;brightcyan;brightblue;brightgreen;"
                            "brightred;yellow";
    char *list = mc_skin_get ("git-graph", "others", many ? others_256 : others_16);
    char **names = g_strsplit_set (list, ";,", -1);
    int n, i;

    git->color_kind[GIT_GRAPH_COLOR_MAIN] = git_graph_color ("main", many ? "color33" : "blue");
    git->color_kind[GIT_GRAPH_COLOR_DEVELOP] =
        git_graph_color ("develop", many ? "color220" : "brightyellow");
    git->color_kind[GIT_GRAPH_COLOR_RELEASE] =
        git_graph_color ("release", many ? "color40" : "green");
    git->color_kind[GIT_GRAPH_COLOR_HOTFIX] = git_graph_color ("hotfix", many ? "color196" : "red");
    g_free (git->color_other);
    git->color_other = g_new (int, g_strv_length (names) + 1);
    for (n = 0, i = 0; names[i] != NULL; i++)
        if (*g_strstrip (names[i]) != '\0')
            git->color_other[n++] = mc_skin_color_on ("editor", "_default_", names[i]);
    if (n == 0)
        git->color_other[n++] = EDITOR_NORMAL_COLOR;
    git->n_other = n;
    g_strfreev (names);
    g_free (list);
}

/* --------------------------------------------------------------------------------------------- */

/* The characters of the graph: the lines those of the frames of the skin unless [git-graph] has
   its own, rounded corners for one */
void
git_graph_glyphs (git_t *git)
{
    git->glyph[GIT_GLYPH_VERT] = git_graph_glyph ("vert", mc_tty_frm[MC_TTY_FRM_VERT]);
    git->glyph[GIT_GLYPH_HORIZ] = git_graph_glyph ("horiz", mc_tty_frm[MC_TTY_FRM_HORIZ]);
    git->glyph[GIT_GLYPH_LEFTTOP] = git_graph_glyph ("lefttop", mc_tty_frm[MC_TTY_FRM_LEFTTOP]);
    git->glyph[GIT_GLYPH_RIGHTTOP] = git_graph_glyph ("righttop", mc_tty_frm[MC_TTY_FRM_RIGHTTOP]);
    git->glyph[GIT_GLYPH_LEFTBOTTOM] =
        git_graph_glyph ("leftbottom", mc_tty_frm[MC_TTY_FRM_LEFTBOTTOM]);
    git->glyph[GIT_GLYPH_RIGHTBOTTOM] =
        git_graph_glyph ("rightbottom", mc_tty_frm[MC_TTY_FRM_RIGHTBOTTOM]);
    git->glyph[GIT_GLYPH_LEFTMIDDLE] =
        git_graph_glyph ("leftmiddle", mc_tty_frm[MC_TTY_FRM_LEFTMIDDLE]);
    git->glyph[GIT_GLYPH_RIGHTMIDDLE] =
        git_graph_glyph ("rightmiddle", mc_tty_frm[MC_TTY_FRM_RIGHTMIDDLE]);
    git->glyph[GIT_GLYPH_TOPMIDDLE] =
        git_graph_glyph ("topmiddle", mc_tty_frm[MC_TTY_FRM_TOPMIDDLE]);
    git->glyph[GIT_GLYPH_BOTTOMMIDDLE] =
        git_graph_glyph ("bottommiddle", mc_tty_frm[MC_TTY_FRM_BOTTOMMIDDLE]);
    git->glyph[GIT_GLYPH_CROSS] = git_graph_glyph ("cross", mc_tty_frm[MC_TTY_FRM_CROSS]);
    git->glyph[GIT_GLYPH_COMMIT] = git_graph_glyph ("commit", '*');
    git->glyph[GIT_GLYPH_MERGE] = git_graph_glyph ("merge", 'o');
    git->glyph[GIT_GLYPH_ARROW_LEFT] = git_graph_glyph ("arrow-left", '<');
    git->glyph[GIT_GLYPH_ARROW_RIGHT] = git_graph_glyph ("arrow-right", '>');
    git->glyph[GIT_GLYPH_MORE] = git_graph_glyph ("more", '>');
    // a commit beside the line of its branch rather than on it: not unless the skin says
    git->glyph[GIT_GLYPH_RAIL] = git_graph_glyph ("rail", 0);
}

/* --------------------------------------------------------------------------------------------- */

/* Print one colored part of a graph label without crossing the list's right edge. */
static void
git_graph_text (const char *part, int color, int *room)
{
    int width;

    if (*room <= 0 || part == NULL)
        return;
    tty_setcolor (color);
    width = MIN (*room, str_term_width1 (part));
    tty_print_string (str_term_substring (part, 0, width));
    *room -= width;
}

void
git_draw_graph_row (git_window_t *win, Widget *w, int y, int x, int cols, int index, int highlight)
{
    const git_t *git = win->git;
    const git_graph_row_t *row = g_ptr_array_index (git->graph_rows, index);
    const git_commit_t *c =
        row->commit >= 0 ? g_ptr_array_index (git->graph_commits, row->commit) : NULL;
    const int branch = row->node >= 0 ? git_graph_lane_color (git, row->color[row->node]) : 0;
    int slots, room, i, rail = -1;
    char **refs;

    // two terminal columns a branch, what does not fit given up for the commit
    slots = MIN (MAX (1, row->cols), MAX (1, (cols - 18) / 2));
    room = cols - slots * 2;
    // a commit beside its line, after the character of the skin
    if (c != NULL && git->glyph[GIT_GLYPH_RAIL] != 0
        && (row->line[row->node] & GIT_GRAPH_MERGE) == 0 && row->node < slots)
        rail = row->node;
    widget_gotoyx (w, y, x);
    for (i = 0; i < slots; i++)
    {
        tty_setcolor (highlight != 0 ? highlight : git_graph_lane_color (git, row->color[i]));
        tty_print_char (i == rail ? git->glyph[GIT_GLYPH_RAIL]
                                  : git_graph_char (git, row->line[i]));
        if (i == rail)
            tty_print_char (git->glyph[GIT_GLYPH_COMMIT]);
        else if (i < slots - 1)
        {
            const guint8 link = row->link[i];

            tty_setcolor (highlight != 0 ? highlight
                                         : git_graph_lane_color (git, row->link_color[i]));
            tty_print_char (link == GIT_GRAPH_LINK_LINE          ? git->glyph[GIT_GLYPH_HORIZ]
                                : link == GIT_GRAPH_LINK_TO_LEFT ? git->glyph[GIT_GLYPH_ARROW_LEFT]
                                : link == GIT_GRAPH_LINK_TO_RIGHT
                                ? git->glyph[GIT_GLYPH_ARROW_RIGHT]
                                : ' ');
        }
    }
    tty_setcolor (highlight != 0 ? highlight : EDITOR_NORMAL_COLOR);
    if (rail != slots - 1)
        tty_print_char (row->cols > slots ? git->glyph[GIT_GLYPH_MORE] : ' ');
    // a row that joins lines: nothing after them
    if (c == NULL)
    {
        tty_setcolor (EDITOR_NORMAL_COLOR);
        while (room-- > 0)
            tty_print_char (' ');
        return;
    }
    if (highlight != 0)
    {
        GString *label = g_string_new (NULL);

        g_string_append_printf (label, "%.7s ", c->sha);
        if (*c->refs != '\0')
            g_string_append_printf (label, "(%s) ", c->refs);
        g_string_append (label, c->subject);
        tty_print_string (str_fit_to_term (label->str, room, J_LEFT));
        g_string_free (label, TRUE);
        return;
    }
    {
        char sha[8];

        g_strlcpy (sha, c->sha, sizeof (sha));
        git_graph_text (sha, git->color_sha, &room);
    }
    git_graph_text (" ", EDITOR_NORMAL_COLOR, &room);
    // the refs in the color of the branch of the commit, the tags as the sha
    if (*c->refs != '\0')
    {
        git_graph_text ("(", EDITOR_NORMAL_COLOR, &room);
        refs = g_strsplit (c->refs, ", ", -1);
        for (i = 0; refs[i] != NULL; i++)
        {
            const char *ref = refs[i];

            if (i != 0)
                git_graph_text (", ", EDITOR_NORMAL_COLOR, &room);
            if (g_str_has_prefix (ref, "HEAD -> "))
            {
                git_graph_text ("HEAD -> ", git->color_head, &room);
                ref += strlen ("HEAD -> ");
            }
            git_graph_text (ref,
                            g_str_has_prefix (ref, "tag: ") ? git->color_sha
                                : strcmp (ref, "HEAD") == 0 ? git->color_head
                                                            : branch,
                            &room);
        }
        g_strfreev (refs);
        git_graph_text (") ", EDITOR_NORMAL_COLOR, &room);
    }
    git_graph_text (c->subject, EDITOR_NORMAL_COLOR, &room);
    tty_setcolor (EDITOR_NORMAL_COLOR);
    while (room-- > 0)
        tty_print_char (' ');
}

/* --------------------------------------------------------------------------------------------- */

/* The graph read again with @limit commits, the cursor on the commit it was on, else on the
   first */
void
git_graph_reread (git_window_t *win, int limit)
{
    git_t *git = win->git;
    git_cursor_t *c = &win->cursor[GIT_LIST_GRAPH];
    const git_commit_t *at = git_list_commit (win);
    char *sha = at != NULL ? g_strdup (at->sha) : NULL;
    guint i;

    git_close_commit (git);
    g_clear_pointer (&git->graph_rows, g_ptr_array_unref);
    g_clear_pointer (&git->graph_commits, g_ptr_array_unref);
    git->graph_limit = limit;
    git_read_graph (git);
    c->selected = 0;
    c->top = 0;
    for (i = 0; sha != NULL && i < git->graph_rows->len; i++)
    {
        const git_graph_row_t *row = g_ptr_array_index (git->graph_rows, i);
        const git_commit_t *commit =
            row->commit >= 0 ? g_ptr_array_index (git->graph_commits, row->commit) : NULL;

        if (commit != NULL && strcmp (commit->sha, sha) == 0)
        {
            c->selected = (int) i;
            break;
        }
    }
    g_free (sha);
    git_graph_cursor_skip (win, 1);
    git_cursor_show (win, GIT_LIST_GRAPH);
}

/* --------------------------------------------------------------------------------------------- */

/* The refs the graph reads the commits of, chosen from a list and kept in the config */
void
git_graph_choose_refs (git_window_t *win)
{
    git_t *git = win->git;
    Listbox *l;
    int i, width = 0, choice;

    for (i = 0; i < GIT_GRAPH_REFS_COUNT; i++)
        width = MAX (width, str_term_width1 (_ (git_graph_refs_label[i])));
    l = listbox_window_new (GIT_GRAPH_REFS_COUNT, width + 4, _ ("Graph"), "[Git]");
    for (i = 0; i < GIT_GRAPH_REFS_COUNT; i++)
        LISTBOX_APPEND_TEXT (l, 0, _ (git_graph_refs_label[i]), NULL, FALSE);
    listbox_set_current (l->list, (int) git->graph_refs);
    choice = listbox_run (l);
    if (choice < 0 || choice >= GIT_GRAPH_REFS_COUNT)
        return;
    if (choice != (int) git->graph_refs)
    {
        git->graph_refs = (git_graph_refs_t) choice;
        mc_config_set_string (mc_global.main_config, GIT_CONFIG_GROUP, GIT_CONFIG_GRAPH,
                              git_graph_refs_key[choice]);
        git_graph_reread (win, GIT_LOG_STEP);
        git_diff_update (win);
    }
    git_group_draw (win);
}

/* --------------------------------------------------------------------------------------------- */
