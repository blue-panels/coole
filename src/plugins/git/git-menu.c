/*
   The git plugin: the menu of a message (F11), its trailers, its checks, the AI and the
   commands of the user.

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

/** \file git-menu.c
 *  \brief Source: the menu of a message of the git plugin
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/util.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"

#include "git-private.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define GIT_AI_FILE      "ai.ini"
#define GIT_MENU_FILE    "git-menu.ini"
#define GIT_AI_GROUP     "commit-message"
#define GIT_AI_INPUT_MAX (256 * 1024)

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* The menu of a message file: F11 */
/* --------------------------------------------------------------------------------------------- */

/* What the message file of @edit is of: its work tree, and the commit whose message it is
   (NULL for a new commit) */
typedef struct
{
    char *root;
    char *sha;
    gboolean amend;
} git_msg_target_t;

/* --------------------------------------------------------------------------------------------- */

static gboolean
git_msg_target (git_t *git, void *edit, git_msg_target_t *t)
{
    memset (t, 0, sizeof (*t));
    if (git_is_message_file (git, edit))
    {
        t->root = g_strdup (git->msg_root);
        t->sha = git->msg_mode == GIT_MSG_REWORD ? g_strdup (git->msg_sha)
            : git->msg_mode == GIT_MSG_AMEND     ? g_strdup ("HEAD")
                                                 : NULL;
        t->amend = git->msg_mode == GIT_MSG_AMEND;
    }
    else
    {
        // COMMIT_EDITMSG of a git commit that has the editor for its editor: in .git
        char *file = git->host->get_current_file (git->host, edit);
        char *dir = g_path_get_dirname (file);

        if (strcmp (x_basename (dir), ".git") == 0)
            t->root = g_path_get_dirname (dir);
        else
        {
            char *cwd = g_get_current_dir ();

            t->root = git_toplevel (cwd);
            g_free (cwd);
        }
        g_free (dir);
        g_free (file);
    }
    return t->root != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_msg_target_clear (git_msg_target_t *t)
{
    g_free (t->root);
    g_free (t->sha);
}

/* --------------------------------------------------------------------------------------------- */

/* The text of the window of @edit made @text, one step of undo */
static void
git_edit_set_text (WEdit *edit, const char *text)
{
    const char *p;

    edit_push_key_press (edit);
    edit_cursor_move (edit, -edit->buffer.curs1);
    while (edit->buffer.size > 0)
        (void) edit_delete (edit, TRUE);
    for (p = text; *p != '\0'; p++)
        edit_insert (edit, (unsigned char) *p);
    edit_cursor_move (edit, -edit->buffer.curs1);
    edit->force |= REDRAW_PAGE;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_edit_add_trailer (git_t *git, WEdit *edit, const char *trailer)
{
    char *text = git->host->get_text (git->host, edit, NULL);
    char *changed = git_message_add_trailer (text, trailer);

    if (strcmp (text, changed) != 0)
        git_edit_set_text (edit, changed);
    g_free (changed);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

static char *
git_signoff (const char *root)
{
    char *name = git_config_value (root, "user.name");
    char *mail = git_config_value (root, "user.email");
    char *trailer = NULL;

    if (name != NULL && mail != NULL)
        trailer = g_strdup_printf ("Signed-off-by: %s <%s>", name, mail);
    g_free (name);
    g_free (mail);
    return trailer;
}

/* --------------------------------------------------------------------------------------------- */

/* The full sha of the commit @name stands for, NULL when it is none; @name is no option for git
   whatever it starts with */
static char *
git_resolve_commit (const char *root, const char *name)
{
    char *spec = g_strconcat (name, "^{commit}", (char *) NULL);
    char *sha = git_capture (root, NULL, "rev-parse", "--verify", "-q", "--end-of-options", spec,
                             (char *) NULL);

    g_free (spec);
    if (*g_strchomp (sha) == '\0')
        g_clear_pointer (&sha, g_free);
    return sha;
}

/* --------------------------------------------------------------------------------------------- */

/* "Name <mail>" is the user, @me the mail of git config */
static gboolean
git_is_me (const char *author, const char *me)
{
    char *mail;
    gboolean is;

    if (me == NULL || *me == '\0')
        return FALSE;
    mail = g_strconcat ("<", me, ">", (char *) NULL);
    is = g_str_has_suffix (author, mail);
    g_free (mail);
    return is;
}

/* --------------------------------------------------------------------------------------------- */

/* One of the authors of the history, chosen; NULL when none is */
static char *
git_choose_author (const char *root)
{
    char *text = git_capture (root, NULL, "log", "-n", "3000", "--format=%an <%ae>", (char *) NULL);
    char **lines = g_strsplit (text, "\n", -1);
    GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
    char *me = git_config_value (root, "user.email");
    char *chosen = NULL;
    Listbox *l;
    char **line;
    int n = 0, width = 30;

    for (line = lines; *line != NULL; line++)
        if (**line != '\0' && !g_hash_table_contains (seen, *line) && !git_is_me (*line, me))
        {
            g_hash_table_add (seen, *line);
            width = MAX (width, str_term_width1 (*line) + 4);
            n++;
        }
    if (n == 0)
    {
        message (D_NORMAL, _ ("Co-authored-by"), "%s", _ ("Nobody else is in the history."));
        goto out;
    }
    l = listbox_window_new (MIN (n, LINES - 6), MIN (width, COLS - 4), _ ("Co-authored-by"), NULL);
    g_hash_table_remove_all (seen);
    for (line = lines; *line != NULL; line++)
        if (**line != '\0' && !g_hash_table_contains (seen, *line) && !git_is_me (*line, me))
        {
            g_hash_table_add (seen, *line);
            LISTBOX_APPEND_TEXT (l, 0, *line, *line, FALSE);
        }
    {
        const char *author = listbox_run_with_data (l, NULL);

        if (author != NULL)
            chosen = g_strconcat ("Co-authored-by: ", author, (char *) NULL);
    }

out:
    g_free (me);
    g_hash_table_destroy (seen);
    g_strfreev (lines);
    g_free (text);
    return chosen;
}

/* --------------------------------------------------------------------------------------------- */

/* "Fixes: 1e1573e28a1b ("subject")" of a commit asked for */
static char *
git_fixes (const char *root)
{
    char *name = input_dialog (_ ("Fixes"), _ ("The commit it fixes:"), "git-fixes", "",
                               INPUT_COMPLETE_NONE);
    char *trailer = NULL;

    if (name != NULL && *g_strstrip (name) != '\0')
    {
        char *sha = git_resolve_commit (root, name);

        if (sha != NULL)
        {
            char *line = git_capture (root, NULL, "log", "-1", "--abbrev=12",
                                      "--format=%h (\"%s\")", sha, "--", (char *) NULL);

            if (*g_strchomp (line) != '\0')
                trailer = g_strconcat ("Fixes: ", line, (char *) NULL);
            g_free (line);
        }
        if (trailer == NULL)
            message (D_ERROR, _ ("Fixes"), _ ("No commit %s here."), name);
        g_free (sha);
    }
    g_free (name);
    return trailer;
}

/* --------------------------------------------------------------------------------------------- */

/* The changes the message is of, as a diff: of the commit reworded, of the staged changes and the
   last commit for an amend, of the staged changes for a commit */
static char *
git_msg_diff (const git_msg_target_t *t, gboolean color)
{
    const char *colors = color ? "--color=always" : "--no-color";
    char *text;

    if (t->sha != NULL && !t->amend)
    {
        text = git_capture (t->root, NULL, "show", colors, "-M", "--stat", "-p", "--format=fuller",
                            t->sha, (char *) NULL);
    }
    else
    {
        const char *amend_args[] = {
            "diff", colors, "-M", "--stat", "-p", "--cached", "HEAD^", NULL
        };
        const char *args[] = { "diff", colors, "-M", "--stat", "-p", "--cached", NULL };

        text = git_capture_args (t->root, t->amend ? amend_args : args, NULL);
        // the first commit amended has no parent
        if (t->amend && *text == '\0')
        {
            char *staged = git_capture_args (t->root, args, NULL);
            char *head;

            g_free (text);
            head = git_capture (t->root, NULL, "show", colors, "--stat", "-p", "--format=", "HEAD",
                                (char *) NULL);
            // what the commit has, then what the amend adds to it
            text = g_strconcat (head, staged, (char *) NULL);
            g_free (head);
            g_free (staged);
        }
    }
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* Text in the window of the viewer at the right, the one there is when it is open */
static void
git_msg_show_text (git_t *git, const char *title_of, const char *text_of)
{
    char *text = g_strdup (text_of);
    char *title = g_strdup (title_of);
    GVariantDict dict;
    GVariant *reply = NULL;

    if (git->host->service_call == NULL)
        goto out;
    if (git->msg_diff_window != 0)
    {
        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "id", "x", git->msg_diff_window);
        g_variant_dict_insert (&dict, "text", "s", text);
        reply = git->host->service_call (git->host, "viewer", "set_text",
                                         g_variant_dict_end (&dict), NULL);
        if (reply != NULL)
        {
            g_variant_unref (reply);
            g_variant_dict_init (&dict, NULL);
            g_variant_dict_insert (&dict, "id", "x", git->msg_diff_window);
            g_variant_dict_insert (&dict, "title", "s", title);
            reply = git->host->service_call (git->host, "viewer", "set_title",
                                             g_variant_dict_end (&dict), NULL);
            if (reply != NULL)
                g_variant_unref (reply);
            g_variant_dict_init (&dict, NULL);
            g_variant_dict_insert (&dict, "id", "x", git->msg_diff_window);
            g_variant_dict_insert (&dict, "focus", "b", TRUE);
            reply = git->host->service_call (git->host, "viewer", "show",
                                             g_variant_dict_end (&dict), NULL);
        }
        else
            git->msg_diff_window = 0;
    }
    if (git->msg_diff_window == 0)
    {
        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "title", "s", title);
        g_variant_dict_insert (&dict, "text", "s", text);
        g_variant_dict_insert (&dict, "place", "s", "right");
        g_variant_dict_insert (&dict, "size", "i", 50);
        g_variant_dict_insert (&dict, "focus", "b", TRUE);
        reply =
            git->host->service_call (git->host, "viewer", "open", g_variant_dict_end (&dict), NULL);
        if (reply != NULL)
            (void) g_variant_lookup (reply, "id", "x", &git->msg_diff_window);
        else
            message (D_ERROR, _ ("Git"), "%s", _ ("The viewer plugin is not there to show it."));
    }
    if (reply != NULL)
        g_variant_unref (reply);

out:
    g_free (title);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

/* The diff of the commit reworded, or of what is committed */
static void
git_msg_show_diff (git_t *git, const git_msg_target_t *t)
{
    char *text = git_msg_diff (t, TRUE);
    char *title = t->sha != NULL && !t->amend ? g_strdup_printf (_ ("Commit %.7s"), t->sha)
                                              : g_strdup (_ ("What is committed"));

    git_msg_show_text (git, title, *text != '\0' ? text : _ ("Nothing is staged."));
    g_free (title);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

/* A file of coole: in the directory of its configuration, else in that of its data; NULL when
   there is none */
static char *
git_coole_file (const char *name)
{
    char *path = g_build_filename (mc_global.sysconfig_dir, name, (char *) NULL);

    if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
    {
        g_free (path);
        path = g_build_filename (mc_global.share_data_dir, name, (char *) NULL);
    }
    if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
        g_clear_pointer (&path, g_free);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* What a file of coole has, NULL when there is none */
static char *
git_coole_text (const char *name)
{
    char *path = git_coole_file (name);
    char *text = NULL;

    if (path != NULL && !g_file_get_contents (path, &text, NULL, NULL))
        text = NULL;
    g_free (path);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* A key of [commit-message] in ai.ini of the user; the prompts and the input of the project, in
   its .coole/ai.ini, first, and those of coole last.  A repository gives no command: it would
   run whatever whoever made it wrote */
static char *
git_ai_value (const char *root, const char *key)
{
    const gboolean asked = g_str_has_prefix (key, "prompt") || strcmp (key, "input") == 0;
    char *paths[3];
    char *value = NULL;
    int i;

    paths[0] = asked ? g_build_filename (root, ".coole", GIT_AI_FILE, (char *) NULL) : NULL;
    paths[1] = g_build_filename (g_get_user_config_dir (), "coole", GIT_AI_FILE, (char *) NULL);
    paths[2] = asked ? git_coole_file (GIT_AI_FILE) : NULL;
    for (i = 0; i < 3 && value == NULL; i++)
    {
        GKeyFile *kf = g_key_file_new ();

        if (paths[i] != NULL && g_key_file_load_from_file (kf, paths[i], G_KEY_FILE_NONE, NULL))
            value = g_key_file_get_string (kf, GIT_AI_GROUP, key, NULL);
        g_key_file_free (kf);
        if (value != NULL && *g_strstrip (value) == '\0')
            g_clear_pointer (&value, g_free);
    }
    for (i = 0; i < 3; i++)
        g_free (paths[i]);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/* What a generator printed, without the fences of markdown it may put around it */
static char *
git_ai_unfence (const char *out)
{
    char **lines = g_strsplit (out, "\n", -1);
    GString *s = g_string_new (NULL);
    char **l;

    for (l = lines; *l != NULL; l++)
        if (!g_str_has_prefix (*l, "```"))
        {
            g_string_append (s, *l);
            g_string_append_c (s, '\n');
        }
    g_strfreev (lines);
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* @text in lines of @width columns at most, broken between words, a word longer cut; @max_lines
   of them, the last ending with an ellipsis when there is more */
static char *
git_wrap_words (const char *text, int width, int max_lines)
{
    GString *out = g_string_new (NULL);
    char **words = g_strsplit (text, " ", -1);
    GString *line = g_string_new (NULL);
    int lines = 0;
    char **w;

    for (w = words; *w != NULL && lines < max_lines; w++)
    {
        if (**w == '\0')
            continue;
        if (line->len != 0 && str_term_width1 (line->str) + 1 + str_term_width1 (*w) > width)
        {
            g_string_append_printf (out, "%s\n", line->str);
            g_string_truncate (line, 0);
            lines++;
            if (lines == max_lines)
                break;
        }
        if (line->len != 0)
            g_string_append_c (line, ' ');
        g_string_append (line, str_fit_to_term (*w, MIN (width, str_term_width1 (*w)), J_LEFT));
    }
    if (lines < max_lines && line->len != 0)
        g_string_append (out, line->str);
    else if (*w != NULL)
    {
        // more than the lines take: the last one says so
        if (out->len != 0 && out->str[out->len - 1] == '\n')
            g_string_truncate (out, out->len - 1);
        g_string_append (out, mc_global.utf8_display ? " \u2026" : " ...");
    }
    g_string_free (line, TRUE);
    g_strfreev (words);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The message written by the command of ai.ini, from the diff: in place of the message, the
   trailers kept */
static void
git_msg_generate (git_t *git, WEdit *edit, const git_msg_target_t *t)
{
    // what it is the message of, for the prompt and for the command
    const char *mode = t->amend ? "amend" : t->sha != NULL ? "reword" : "commit";
    char *command = git_ai_value (t->root, "command");
    char *prompt_key = g_strconcat ("prompt-", mode, (char *) NULL);
    char *prompt = git_ai_value (t->root, prompt_key);
    char *input_kind = git_ai_value (t->root, "input");
    char *env[3];
    GString *input;
    git_output_t o;
    WDialog *wait;
    gboolean ran;

    if (command == NULL)
    {
        char *user =
            g_build_filename (g_get_user_config_dir (), "coole", GIT_AI_FILE, (char *) NULL);

        message (D_ERROR, _ ("Generate"),
                 _ ("No command to write messages with: give one in [%s] of\n%s"), GIT_AI_GROUP,
                 user);
        g_free (user);
        goto out;
    }
    // no prompt for the mode: the prompt of all
    if (prompt == NULL)
        prompt = git_ai_value (t->root, "prompt");
    input = g_string_new (prompt);
    if (input_kind == NULL || strcmp (input_kind, "none") != 0)
    {
        char *diff = git_msg_diff (t, FALSE);

        // nothing staged for a commit: what F2 would offer to commit, staged first
        if (*diff == '\0' && strcmp (mode, "commit") == 0)
        {
            const char *add_args[] = { "add", "-A", NULL };
            char *changes =
                git_capture (t->root, NULL, "status", "--porcelain", "-z", (char *) NULL);
            const gboolean any = *changes != '\0';
            gboolean staged = any && !git_conflicts (t->root, _ ("Generate"))
                && query_dialog (_ ("Generate"), _ ("Nothing is staged. Stage all the changes?"),
                                 D_NORMAL, 2, _ ("&Yes"), _ ("&No"))
                    == 0;

            g_free (changes);
            if (staged)
            {
                git_output_t added;

                staged = git_run (t->root, add_args, NULL, &added) && added.status == 0;
                if (!staged)
                {
                    char *why = git_output_error (&added);

                    message (D_ERROR, _ ("Stage"), "%s", why);
                    g_free (why);
                }
                git_output_clear (&added);
            }
            if (staged)
            {
                if (git->win != NULL && g_strcmp0 (git->root, t->root) == 0)
                    git_window_reload (git->win);
                g_free (diff);
                diff = git_msg_diff (t, FALSE);
            }
            else if (any)
            {
                // the changes left as they are, or git said no
                g_free (diff);
                g_string_free (input, TRUE);
                goto out;
            }
        }
        // a model given no diff writes about that
        if (*diff == '\0')
        {
            message (D_NORMAL, _ ("Generate"), _ ("Nothing to describe: the diff is empty."));
            g_free (diff);
            g_string_free (input, TRUE);
            goto out;
        }
        g_string_append (input, "\n\n");
        g_string_append_len (input, diff, MIN ((gssize) strlen (diff), GIT_AI_INPUT_MAX));
        g_free (diff);
    }

    {
        char *shown =
            git_wrap_words (command, MIN (60, MAX (20, COLS - 10)), MIN (8, MAX (1, LINES - 8)));

        wait =
            create_message (D_NORMAL, _ ("Generate"), _ ("Generating the message with\n%s"), shown);
        g_free (shown);
    }
    mc_refresh ();
    // the command knows what it writes: COOLE_GIT_COMMIT^ is the history before the commit
    env[0] = g_strconcat ("COOLE_GIT_MODE=", mode, (char *) NULL);
    env[1] = g_strconcat ("COOLE_GIT_COMMIT=", t->sha != NULL ? t->sha : "", (char *) NULL);
    env[2] = NULL;
    ran = git_run_shell (t->root, command, input->str, (const char *const *) env, &o);
    g_free (env[0]);
    g_free (env[1]);
    dlg_run_done (wait);
    widget_destroy (WIDGET (wait));
    g_string_free (input, TRUE);

    if (!ran || o.status != 0 || *g_strstrip (o.out->str) == '\0')
    {
        char *why =
            ran && o.status == 0 ? g_strdup (_ ("It printed nothing.")) : git_output_error (&o);

        char *shown =
            git_wrap_words (command, MIN (60, MAX (20, COLS - 10)), MIN (8, MAX (1, LINES - 8)));

        message (D_ERROR, _ ("Generate"), "%s\n\n%s", shown, why);
        g_free (shown);
        g_free (why);
    }
    else
    {
        char *text = git->host->get_text (git->host, edit, NULL);
        char *written = git_ai_unfence (o.out->str);
        char *changed = git_message_replace (text, written, TRUE);

        git_edit_set_text (edit, changed);
        g_free (changed);
        g_free (written);
        g_free (text);
    }
    git_output_clear (&o);

out:
    g_free (input_kind);
    g_free (prompt);
    g_free (prompt_key);
    g_free (command);
}

/* --------------------------------------------------------------------------------------------- */

/* The line of a key in an ini file, from 1; 1 when it is not there */
static long
git_ini_line_of (const char *path, const char *key)
{
    char *text = NULL;
    long line = 1, n = 1;
    char **lines, **l;

    if (!g_file_get_contents (path, &text, NULL, NULL))
        return 1;
    lines = g_strsplit (text, "\n", -1);
    for (l = lines; *l != NULL; l++, n++)
    {
        const char *p = *l;

        while (*p == ' ' || *p == '\t')
            p++;
        if (g_str_has_prefix (p, key) && (p[strlen (key)] == ' ' || p[strlen (key)] == '='))
        {
            line = n;
            break;
        }
    }
    g_strfreev (lines);
    g_free (text);
    return line;
}

/* --------------------------------------------------------------------------------------------- */

/* From the idle of the editor, the window of the message having drawn itself after its key: a
   file on the whole screen over the window Git, as F4 opens one, Esc coming back */
static void
git_open_over_later (void *data)
{
    git_t *git = (git_t *) data;
    char *path = git->over_path;

    git->over_path = NULL;
    if (path != NULL && git->host->show_location (git->host, path, MAX (1, git->over_line)))
    {
        void *edit = git->host->window_current (git->host);

        if (edit != NULL && edit_widget_is_editor (CONST_WIDGET (edit)) && edit != git->msg_window)
        {
            if (EDIT_WINDOW (edit)->fullscreen == 0)
                edit_window_toggle_fullscreen (EDIT_WINDOW (edit));
            git->opened = edit;
            git->opened_from_message = TRUE;
            widget_draw (WIDGET (CONST_WIDGET (edit)->owner));
        }
    }
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_open_over (git_t *git, const char *path, long line)
{
    g_free (git->over_path);
    git->over_path = g_strdup (path);
    git->over_line = line;
    git->host->call_later (git->host, git_open_over_later, git);
}

/* --------------------------------------------------------------------------------------------- */

static char *
git_menu_path (void)
{
    return g_build_filename (g_get_user_config_dir (), "coole", GIT_MENU_FILE, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* The menu of the message: that of the user, else that of coole */
static GPtrArray *
git_menu_load (void)
{
    char *path = git_menu_path ();
    char *text = NULL;
    GPtrArray *menu = NULL;
    GError *error = NULL;

    if (g_file_get_contents (path, &text, NULL, NULL))
    {
        menu = git_menu_parse (text, &error);
        if (menu == NULL)
        {
            message (D_ERROR, _ ("Commit message"), "%s:\n%s", path, error->message);
            g_error_free (error);
        }
    }
    g_free (text);
    text = menu == NULL ? git_coole_text (GIT_MENU_FILE) : NULL;
    if (text != NULL)
        menu = git_menu_parse (text, NULL);
    if (menu == NULL)
        menu = g_ptr_array_new ();
    g_free (text);
    g_free (path);
    return menu;
}

/* --------------------------------------------------------------------------------------------- */

/* The menu of the user to edit, made from that of coole when there is none */
static void
git_menu_edit (git_t *git)
{
    char *path = git_menu_path ();

    if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        char *dir = g_path_get_dirname (path);
        char *text = git_coole_text (GIT_MENU_FILE);

        (void) g_mkdir_with_parents (dir, 0700);
        (void) g_file_set_contents (path, text != NULL ? text : "", -1, NULL);
        g_free (text);
        g_free (dir);
    }
    git_open_over (git, path, 1);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

/* The prompt of the AI in a window of the editor, on the whole screen: in .coole/ai.ini of the
   project when it has one there, else in ai.ini of the user, made from that of coole when there
   is none */
static void
git_msg_edit_prompt (git_t *git, const git_msg_target_t *t)
{
    char *project = g_build_filename (t->root, ".coole", GIT_AI_FILE, (char *) NULL);
    char *user = g_build_filename (g_get_user_config_dir (), "coole", GIT_AI_FILE, (char *) NULL);
    // the prompt of the mode of the message when there is one, else that of all
    char *key = g_strconcat ("prompt-",
                             t->amend             ? "amend"
                                 : t->sha != NULL ? "reword"
                                                  : "commit",
                             (char *) NULL);
    const char *path = user;
    GKeyFile *kf = g_key_file_new ();

    if (g_key_file_load_from_file (kf, project, G_KEY_FILE_NONE, NULL)
        && (g_key_file_has_key (kf, GIT_AI_GROUP, key, NULL)
            || g_key_file_has_key (kf, GIT_AI_GROUP, "prompt", NULL)))
        path = project;
    g_key_file_free (kf);
    if (path == user && !g_file_test (user, G_FILE_TEST_EXISTS))
    {
        char *dir = g_path_get_dirname (user);
        char *text = git_coole_text (GIT_AI_FILE);

        (void) g_mkdir_with_parents (dir, 0700);
        (void) g_file_set_contents (user, text != NULL ? text : "", -1, NULL);
        g_free (text);
        g_free (dir);
    }
    git_open_over (git, path,
                   git_ini_line_of (path, key) > 1 ? git_ini_line_of (path, key)
                                                   : git_ini_line_of (path, "prompt"));
    g_free (key);
    g_free (user);
    g_free (project);
}

/* --------------------------------------------------------------------------------------------- */
/* The label of an item that gives none: that of its action */
static char *
git_menu_label (const git_menu_item_t *item, const git_msg_target_t *t, const char *signoff)
{
    const char *a = item->action;

    if (item->label != NULL && *item->label != '\0')
        return g_strdup (item->label);
    if (a == NULL)
        return g_strdup (item->command);
    if (strcmp (a, "signoff") == 0)
        return g_strdup (signoff != NULL ? signoff : _ ("Signed-off-by (no user.name)"));
    if (strcmp (a, "coauthor") == 0)
        return g_strdup (_ ("Co-authored-by: an author of the history..."));
    if (strcmp (a, "fixes") == 0)
        return g_strdup (_ ("Fixes: the commit it fixes..."));
    if (strcmp (a, "restore") == 0)
        return g_strdup (_ ("Restore the message the commit has"));
    if (strcmp (a, "diff") == 0)
        return g_strdup (t->sha != NULL && !t->amend ? _ ("Diff of the commit")
                                                     : _ ("Diff of what is committed"));
    if (strcmp (a, "check") == 0)
        return g_strdup (_ ("Check the shape of the message"));
    if (strcmp (a, "generate") == 0)
        return g_strdup (_ ("Generate the message with AI"));
    if (strcmp (a, "prompt") == 0)
        return g_strdup (_ ("Edit the prompt of the AI..."));
    if (strcmp (a, "menu") == 0)
        return g_strdup (_ ("Edit this menu..."));
    return g_strdup (a);
}

/* --------------------------------------------------------------------------------------------- */

/* A command of the menu: the message on its stdin; what it prints goes where @item says */
static void
git_menu_run (git_t *git, WEdit *edit, const git_menu_item_t *item, const git_msg_target_t *t)
{
    const char *mode = t->amend ? "amend" : t->sha != NULL ? "reword" : "commit";
    const char *output = item->output != NULL ? item->output : "insert";
    char *text = git->host->get_text (git->host, edit, NULL);
    char *env[3];
    git_output_t o;
    gboolean ran;

    env[0] = g_strconcat ("COOLE_GIT_MODE=", mode, (char *) NULL);
    env[1] = g_strconcat ("COOLE_GIT_COMMIT=", t->sha != NULL ? t->sha : "", (char *) NULL);
    env[2] = NULL;
    ran = git_run_shell (t->root, item->command, text, (const char *const *) env, &o);
    g_free (env[0]);
    g_free (env[1]);

    if (!ran || o.status != 0)
    {
        char *why = git_output_error (&o);
        char *shown = git_wrap_words (item->command, MIN (60, MAX (20, COLS - 10)),
                                      MIN (8, MAX (1, LINES - 8)));

        message (D_ERROR, _ ("Commit message"), "%s\n\n%s", shown, why);
        g_free (shown);
        g_free (why);
    }
    else if (strcmp (output, "trailer") == 0)
    {
        char **lines = g_strsplit (o.out->str, "\n", -1);
        char **l;

        for (l = lines; *l != NULL; l++)
            if (*g_strstrip (*l) != '\0')
                git_edit_add_trailer (git, edit, *l);
        g_strfreev (lines);
    }
    else if (strcmp (output, "replace") == 0)
    {
        char *changed = git_message_replace (text, o.out->str, TRUE);

        git_edit_set_text (edit, changed);
        g_free (changed);
    }
    else if (strcmp (output, "show") == 0)
        git_msg_show_text (git, item->label != NULL ? item->label : item->command, o.out->str);
    else if (strcmp (output, "insert") == 0 && o.out->len != 0)
    {
        // the last newline is the end of the output, not a line to insert
        if (o.out->str[o.out->len - 1] == '\n')
            g_string_truncate (o.out, o.out->len - 1);
        git->host->insert_text (git->host, edit, o.out->str, 0);
    }
    git_output_clear (&o);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
git_is_any_message_file (const git_t *git, void *edit)
{
    char *file = edit != NULL ? git->host->get_current_file (git->host, edit) : NULL;
    const char *base = file != NULL ? x_basename (file) : NULL;
    gboolean is;

    is = base != NULL
        && (strcmp (base, GIT_MESSAGE_FILE) == 0 || strcmp (base, GIT_AMEND_FILE) == 0
            || strcmp (base, GIT_REWORD_FILE) == 0 || strcmp (base, "COMMIT_EDITMSG") == 0);
    g_free (file);
    return is;
}

/* --------------------------------------------------------------------------------------------- */

void
git_message_menu (git_t *git, WEdit *edit)
{
    git_msg_target_t t;
    GPtrArray *menu, *shown, *labels;
    char *signoff, *trailer = NULL;
    const char *mode;
    const git_menu_item_t *item = NULL;
    Listbox *l;
    int choice, width = 40;
    guint i;

    if (!git_msg_target (git, edit, &t))
    {
        message (D_ERROR, _ ("Git"), "%s", _ ("The work tree of the message is not found."));
        git_msg_target_clear (&t);
        return;
    }
    mode = t.amend ? "amend" : t.sha != NULL ? "reword" : "commit";
    signoff = git_signoff (t.root);
    menu = git_menu_load ();

    // the items of this message, their key before them the way the menu of the user has it
    shown = g_ptr_array_new ();
    labels = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < menu->len; i++)
    {
        const git_menu_item_t *it = g_ptr_array_index (menu, i);
        char *label;

        if (!git_menu_item_in_mode (it, mode)
            || (it->action != NULL && strcmp (it->action, "restore") == 0 && t.sha == NULL))
            continue;
        label = git_menu_label (it, &t, signoff);
        g_ptr_array_add (labels, g_strdup_printf ("%c   %s", it->key, label));
        g_free (label);
        width = MAX (width, str_term_width1 (g_ptr_array_index (labels, labels->len - 1)) + 4);
        g_ptr_array_add (shown, (gpointer) it);
    }
    l = listbox_window_new (MAX (1, MIN ((int) shown->len, LINES - 6)), MIN (width, COLS - 6),
                            _ ("Commit message"), "[Git]");
    for (i = 0; i < shown->len; i++)
        LISTBOX_APPEND_TEXT (l, ((const git_menu_item_t *) g_ptr_array_index (shown, i))->key,
                             g_ptr_array_index (labels, i), NULL, FALSE);
    choice = listbox_run (l);
    g_ptr_array_free (labels, TRUE);
    if (choice >= 0 && choice < (int) shown->len)
        item = g_ptr_array_index (shown, choice);
    g_ptr_array_free (shown, TRUE);

    if (item == NULL)
        ;
    else if (item->command != NULL)
        git_menu_run (git, edit, item, &t);
    else if (strcmp (item->action, "signoff") == 0)
    {
        if (signoff == NULL)
            message (D_ERROR, _ ("Signed-off-by"), "%s",
                     _ ("git config has no user.name and user.email."));
        else
            trailer = g_strdup (signoff);
    }
    else if (strcmp (item->action, "coauthor") == 0)
        trailer = git_choose_author (t.root);
    else if (strcmp (item->action, "fixes") == 0)
        trailer = git_fixes (t.root);
    else if (strcmp (item->action, "restore") == 0)
    {
        char *message_now =
            git_capture (t.root, NULL, "log", "-1", "--format=%B", t.sha, (char *) NULL);
        char *text = git->host->get_text (git->host, edit, NULL);
        char *changed = git_message_replace (text, message_now, FALSE);

        git_edit_set_text (edit, changed);
        g_free (changed);
        g_free (text);
        g_free (message_now);
    }
    else if (strcmp (item->action, "diff") == 0)
        git_msg_show_diff (git, &t);
    else if (strcmp (item->action, "check") == 0)
    {
        char *text = git->host->get_text (git->host, edit, NULL);
        char *problems = git_message_check (text);

        g_free (text);
        message (*problems != '\0' ? D_ERROR : D_NORMAL, _ ("Check"), "%s",
                 *problems != '\0' ? g_strchomp (problems) : _ ("The message is fine."));
        g_free (problems);
    }
    else if (strcmp (item->action, "generate") == 0)
        git_msg_generate (git, edit, &t);
    else if (strcmp (item->action, "prompt") == 0)
        git_msg_edit_prompt (git, &t);
    else if (strcmp (item->action, "menu") == 0)
        git_menu_edit (git);
    else
        message (D_ERROR, _ ("Commit message"), _ ("No action %s in the menu."), item->action);

    if (trailer != NULL)
        git_edit_add_trailer (git, edit, trailer);
    g_free (trailer);
    g_free (signoff);
    g_ptr_array_free (menu, TRUE);
    git_msg_target_clear (&t);
    edit_update_screen (edit);
}

/* --------------------------------------------------------------------------------------------- */
