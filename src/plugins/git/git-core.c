/*
   The git of a repository: what it runs, what its answers say.

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

/** \file git-core.c
 *  \brief Source: the git of a repository: what it runs, what its answers say
 */

#include <config.h>

#include <errno.h>
#include <stdarg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "git-core.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The environment of git: ours, no prompt for a password nor an editor, the paths taken as they
   are and not as patterns, plus @extra.  What a git that started the editor (as the editor of git
   commit, or from a hook) gave it is left out: its repository, its index, its author and dates */
static char **
git_environment (const char *const *extra)
{
    static const char *const outer[] = { "GIT_DIR",
                                         "GIT_WORK_TREE",
                                         "GIT_INDEX_FILE",
                                         "GIT_COMMON_DIR",
                                         "GIT_OBJECT_DIRECTORY",
                                         "GIT_ALTERNATE_OBJECT_DIRECTORIES",
                                         "GIT_PREFIX",
                                         NULL };
    static const char *const identity[] = { "GIT_AUTHOR_NAME",
                                            "GIT_AUTHOR_EMAIL",
                                            "GIT_AUTHOR_DATE",
                                            "GIT_COMMITTER_NAME",
                                            "GIT_COMMITTER_EMAIL",
                                            "GIT_COMMITTER_DATE",
                                            NULL };
    char **env = g_get_environ ();
    const char *const *v;

    for (v = outer; *v != NULL; v++)
        env = g_environ_unsetenv (env, *v);
    // git exports GIT_EXEC_PATH to what it runs: the author of its commit is not ours
    if (g_environ_getenv (env, "GIT_EXEC_PATH") != NULL)
        for (v = identity; *v != NULL; v++)
            env = g_environ_unsetenv (env, *v);
    env = g_environ_setenv (env, "GIT_LITERAL_PATHSPECS", "1", TRUE);
    env = g_environ_setenv (env, "GIT_TERMINAL_PROMPT", "0", TRUE);
    env = g_environ_setenv (env, "GIT_EDITOR", "true", TRUE);
    env = g_environ_setenv (env, "GIT_SEQUENCE_EDITOR", "true", TRUE);
    env = g_environ_setenv (env, "GIT_PAGER", "cat", TRUE);
    for (; extra != NULL && *extra != NULL; extra++)
    {
        const char *eq = strchr (*extra, '=');

        if (eq != NULL)
        {
            char *name = g_strndup (*extra, (gsize) (eq - *extra));

            env = g_environ_setenv (env, name, eq + 1, TRUE);
            g_free (name);
        }
    }
    return env;
}

/* --------------------------------------------------------------------------------------------- */

/* Read what is there to read of @fd into @to; FALSE at its end */
static gboolean
git_read_some (int fd, GString *to)
{
    char buf[8192];
    ssize_t n;

    do
        n = read (fd, buf, sizeof (buf));
    while (n < 0 && errno == EINTR);
    if (n <= 0)
        return FALSE;
    g_string_append_len (to, buf, n);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
git_ok (const char *root, const char *const *args, const char *const *env, char **out)
{
    git_output_t o;
    gboolean ok;

    ok = git_run (root, args, env, &o) && o.status == 0;
    if (ok && out != NULL)
    {
        *out = g_string_free (o.out, FALSE);
        o.out = NULL;
    }
    git_output_clear (&o);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static char *
git_line (const char *root, const char *const *args)
{
    char *out = NULL;

    if (!git_ok (root, args, NULL, &out))
        return NULL;
    g_strchomp (out);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

static void
git_set_error (GError **error, const char *what, const git_output_t *o)
{
    char *why = git_output_error (o);

    g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s: %s", what, why);
    g_free (why);
}

/* --------------------------------------------------------------------------------------------- */

/* The fields of a NUL ended entry of git status v2, the last one taking the rest: the path */
static char **
git_fields (const char *entry, int count)
{
    return g_strsplit (entry, " ", count);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_change_add (GPtrArray *to, char code, const char *path, const char *orig)
{
    git_change_t *c;

    if (to == NULL || code == '.' || code == ' ')
        return;
    c = g_new (git_change_t, 1);
    c->code = code;
    c->path = g_strdup (path);
    c->orig = g_strdup (orig);
    g_ptr_array_add (to, c);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_parse_branch_header (const char *line, git_branch_t *branch)
{
    if (branch == NULL)
        return;
    if (g_str_has_prefix (line, "# branch.oid "))
    {
        const char *oid = line + strlen ("# branch.oid ");

        g_free (branch->sha);
        branch->sha = strcmp (oid, "(initial)") != 0 ? g_strdup (oid) : NULL;
    }
    else if (g_str_has_prefix (line, "# branch.head "))
    {
        const char *head = line + strlen ("# branch.head ");

        g_free (branch->head);
        branch->head = strcmp (head, "(detached)") != 0 ? g_strdup (head) : NULL;
    }
    else if (g_str_has_prefix (line, "# branch.upstream "))
    {
        g_free (branch->upstream);
        branch->upstream = g_strdup (line + strlen ("# branch.upstream "));
    }
    else if (g_str_has_prefix (line, "# branch.ab "))
    {
        int ahead = 0, behind = 0;

        if (sscanf (line + strlen ("# branch.ab "), "+%d -%d", &ahead, &behind) == 2)
        {
            branch->ahead = ahead;
            branch->behind = behind;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The author of commit @sha as git commit-tree takes it: @env[0..2] */
static gboolean
git_author_env (const char *root, const char *sha, char **env, GError **error)
{
    const char *args[] = { "log", "-1", "--date=raw", "--format=%an%x00%ae%x00%ad", sha, NULL };
    git_output_t o;
    const char *name, *mail, *date;

    if (!git_run (root, args, NULL, &o) || o.status != 0)
    {
        git_set_error (error, sha, &o);
        git_output_clear (&o);
        return FALSE;
    }
    name = o.out->str;
    mail = name + strlen (name) + 1;
    date = mail < o.out->str + o.out->len ? mail + strlen (mail) + 1 : mail;
    if (date >= o.out->str + o.out->len)
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s: no author", sha);
        git_output_clear (&o);
        return FALSE;
    }
    env[0] = g_strconcat ("GIT_AUTHOR_NAME=", name, (char *) NULL);
    env[1] = g_strconcat ("GIT_AUTHOR_EMAIL=", mail, (char *) NULL);
    // "@" for a raw date: without it git takes one before 1973 for no date at all
    env[2] = g_strconcat ("GIT_AUTHOR_DATE=@", date, (char *) NULL);
    g_strchomp (env[2]);
    git_output_clear (&o);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The message of commit @sha as it is in the commit, byte for byte */
static char *
git_raw_message (const char *root, const char *sha, GError **error)
{
    const char *args[] = { "cat-file", "commit", sha, NULL };
    git_output_t o;
    const char *body;
    char *message;

    if (!git_run (root, args, NULL, &o) || o.status != 0)
    {
        git_set_error (error, sha, &o);
        git_output_clear (&o);
        return NULL;
    }
    body = strstr (o.out->str, "\n\n");
    message = g_strdup (body != NULL ? body + 2 : "");
    git_output_clear (&o);
    return message;
}

/* --------------------------------------------------------------------------------------------- */

/* A commit of the tree of @sha on @parents (strings), with its author and @message; its sha */
static char *
git_commit_again (const char *root, const char *gitdir, const char *sha, GPtrArray *parents,
                  const char *message, GError **error)
{
    char *env[4] = { NULL, NULL, NULL, NULL };
    char *tree = g_strconcat (sha, "^{tree}", (char *) NULL);
    char *file = g_build_filename (gitdir, "COOLE_REWORD_MSG", (char *) NULL);
    GPtrArray *args = g_ptr_array_new ();
    git_output_t o;
    char *made = NULL;
    guint i;

    if (!git_author_env (root, sha, env, error))
        goto out;
    if (!g_file_set_contents (file, message, -1, error))
        goto out;
    g_ptr_array_add (args, (gpointer) "commit-tree");
    g_ptr_array_add (args, tree);
    for (i = 0; i < parents->len; i++)
    {
        g_ptr_array_add (args, (gpointer) "-p");
        g_ptr_array_add (args, g_ptr_array_index (parents, i));
    }
    g_ptr_array_add (args, (gpointer) "-F");
    g_ptr_array_add (args, file);
    g_ptr_array_add (args, NULL);
    if (git_run (root, (const char *const *) args->pdata, (const char *const *) env, &o)
        && o.status == 0)
    {
        made = g_strdup (g_strchomp (o.out->str));
        if (*made == '\0')
            g_clear_pointer (&made, g_free);
    }
    if (made == NULL)
        git_set_error (error, "commit-tree", &o);
    git_output_clear (&o);
    (void) unlink (file);

out:
    g_ptr_array_free (args, TRUE);
    g_free (env[0]);
    g_free (env[1]);
    g_free (env[2]);
    g_free (file);
    g_free (tree);
    return made;
}

/* Run @argv in @cwd with @envp, what it prints in @output; FALSE when it could not start */
static gboolean
git_spawn (const char *cwd, char **argv, char **envp, git_output_t *output)
{
    GPid pid = 0;
    int out_fd = -1, err_fd = -1;
    gboolean started;
    int wstatus = 0;

    output->out = g_string_new (NULL);
    output->err = g_string_new (NULL);
    output->status = -1;

    started =
        g_spawn_async_with_pipes (cwd, argv, envp, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                  NULL, NULL, &pid, NULL, &out_fd, &err_fd, NULL);
    if (!started)
        return FALSE;

    // both pipes read as they come: a git that fills one of them would wait for us
    while (out_fd >= 0 || err_fd >= 0)
    {
        struct pollfd fds[2];
        int n = 0;

        if (out_fd >= 0)
        {
            fds[n].fd = out_fd;
            fds[n].events = POLLIN;
            n++;
        }
        if (err_fd >= 0)
        {
            fds[n].fd = err_fd;
            fds[n].events = POLLIN;
            n++;
        }
        if (poll (fds, (nfds_t) n, -1) < 0)
        {
            if (errno == EINTR)
                continue;
            break;
        }
        while (n-- > 0)
            if (fds[n].revents != 0)
            {
                const gboolean is_out = fds[n].fd == out_fd;

                if (!git_read_some (fds[n].fd, is_out ? output->out : output->err))
                {
                    close (fds[n].fd);
                    if (is_out)
                        out_fd = -1;
                    else
                        err_fd = -1;
                }
            }
    }
    if (out_fd >= 0)
        close (out_fd);
    if (err_fd >= 0)
        close (err_fd);

    while (waitpid (pid, &wstatus, 0) < 0 && errno == EINTR)
        ;
    g_spawn_close_pid (pid);
    if (WIFEXITED (wstatus))
        output->status = WEXITSTATUS (wstatus);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

static gboolean
git_blank (const char *line)
{
    for (; *line != '\0'; line++)
        if (!g_ascii_isspace (*line))
            return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The line of git commit -v above the diff: git leaves out everything from it on */
static gboolean
git_scissors (const char *line)
{
    return line[0] == '#'
        && strcmp (line + 1, " ------------------------ >8 ------------------------") == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The length of @line in characters; in bytes when it is not UTF-8 */
static long
git_line_length (const char *line)
{
    return g_utf8_validate (line, -1, NULL) ? g_utf8_strlen (line, -1) : (long) strlen (line);
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of a message file, and the first of the lines git leaves out at its end: those that
   start with # after the message, empty ones among them, and the diff under the scissors */
static char **
git_message_lines (const char *text, int *comments)
{
    char **lines = g_strsplit (text != NULL ? text : "", "\n", -1);
    int n = (int) g_strv_length (lines);
    int i;

    // the empty string after the last newline is no line
    if (n > 0 && *lines[n - 1] == '\0')
    {
        g_free (lines[n - 1]);
        lines[n - 1] = NULL;
        n--;
    }
    for (i = 0; i < n && !git_scissors (lines[i]); i++)
        ;
    n = i;
    *comments = n;
    for (i = n - 1; i >= 0 && (*lines[i] == '#' || git_blank (lines[i])); i--)
        ;
    for (i++; i < n; i++)
        if (*lines[i] == '#')
        {
            *comments = i;
            break;
        }
    return lines;
}

/* --------------------------------------------------------------------------------------------- */

/* "Token: value" */
static gboolean
git_is_trailer (const char *line)
{
    const char *p = line;

    while (g_ascii_isalnum (*p) || *p == '-')
        p++;
    return p != line && p[0] == ':' && p[1] == ' ' && p[2] != '\0';
}

/* --------------------------------------------------------------------------------------------- */

/* The message of @lines up to @end without the empty lines at its end, in @msg (strings it does
   not own); the first line of the block of trailers it ends with, msg->len when none */
static guint
git_message_body (char **lines, int end, GPtrArray *msg)
{
    guint start;
    int i;

    for (i = 0; i < end; i++)
        g_ptr_array_add (msg, lines[i]);
    while (msg->len != 0 && git_blank (g_ptr_array_index (msg, msg->len - 1)))
        g_ptr_array_set_size (msg, msg->len - 1);
    // the last paragraph, when every line of it is a trailer and it is not the subject
    for (start = msg->len; start > 0 && !git_blank (g_ptr_array_index (msg, start - 1)); start--)
        ;
    if (start == 0)
        return msg->len;
    for (i = (int) start; i < (int) msg->len; i++)
        if (!git_is_trailer (g_ptr_array_index (msg, i)))
            return msg->len;
    return start;
}

/* --------------------------------------------------------------------------------------------- */

/* @msg, then the lines git leaves out after an empty line */
static char *
git_message_join (GPtrArray *msg, char **lines, int comments)
{
    GString *s = g_string_new (NULL);
    guint i;

    for (i = 0; i < msg->len; i++)
    {
        g_string_append (s, g_ptr_array_index (msg, i));
        g_string_append_c (s, '\n');
    }
    if (lines[comments] != NULL)
    {
        int j;

        g_string_append_c (s, '\n');
        for (j = comments; lines[j] != NULL; j++)
        {
            g_string_append (s, lines[j]);
            g_string_append_c (s, '\n');
        }
    }
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
git_lines_have (GPtrArray *msg, guint from, const char *line)
{
    guint i;

    for (i = from; i < msg->len; i++)
        if (strcmp (g_ptr_array_index (msg, i), line) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* @trailer added to @msg, which ends with trailers from @start on */
static void
git_trailer_append (GPtrArray *msg, guint *start, const char *trailer)
{
    if (git_lines_have (msg, *start, trailer))
        return;
    if (*start == msg->len)
    {
        // no block of trailers yet: an empty subject when there is no message at all
        if (msg->len == 0)
            g_ptr_array_add (msg, (gpointer) "");
        g_ptr_array_add (msg, (gpointer) "");
        *start = msg->len;
    }
    g_ptr_array_add (msg, (gpointer) trailer);
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
git_run (const char *root, const char *const *args, const char *const *env, git_output_t *output)
{
    GPtrArray *argv = g_ptr_array_new ();
    char **envp = git_environment (env);
    gboolean started;

    g_ptr_array_add (argv, (gpointer) "git");
    if (root != NULL)
    {
        g_ptr_array_add (argv, (gpointer) "-C");
        g_ptr_array_add (argv, (gpointer) root);
    }
    // names as they are, no colors whatever the configuration says
    g_ptr_array_add (argv, (gpointer) "-c");
    g_ptr_array_add (argv, (gpointer) "core.quotepath=off");
    g_ptr_array_add (argv, (gpointer) "-c");
    g_ptr_array_add (argv, (gpointer) "color.ui=false");
    g_ptr_array_add (argv, (gpointer) "-c");
    g_ptr_array_add (argv, (gpointer) "core.commentChar=#");
    for (; args != NULL && *args != NULL; args++)
        g_ptr_array_add (argv, (gpointer) *args);
    g_ptr_array_add (argv, NULL);

    started = git_spawn (NULL, (char **) argv->pdata, envp, output);
    g_ptr_array_free (argv, TRUE);
    g_strfreev (envp);
    return started;
}

/* --------------------------------------------------------------------------------------------- */

const char **
git_args_va (va_list ap)
{
    GPtrArray *a = g_ptr_array_new ();
    const char *s;

    while ((s = va_arg (ap, const char *)) != NULL)
        g_ptr_array_add (a, (gpointer) s);
    g_ptr_array_add (a, NULL);
    return (const char **) g_ptr_array_free (a, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

char *
git_capture_args (const char *root, const char *const *args, gsize *len)
{
    git_output_t o;
    char *text;

    if (!git_run (root, args, NULL, &o))
    {
        git_output_clear (&o);
        if (len != NULL)
            *len = 0;
        return g_strdup ("");
    }
    if (len != NULL)
        *len = o.out->len;
    text = g_string_free (o.out, FALSE);
    o.out = NULL;
    git_output_clear (&o);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

char *
git_capture (const char *root, gsize *len, ...)
{
    va_list ap;
    const char **args;
    char *text;

    va_start (ap, len);
    args = git_args_va (ap);
    va_end (ap);
    text = git_capture_args (root, args, len);
    g_free (args);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
git_succeeds (const char *root, ...)
{
    va_list ap;
    const char **args;
    git_output_t o;
    gboolean ok;

    va_start (ap, root);
    args = git_args_va (ap);
    va_end (ap);
    ok = git_run (root, args, NULL, &o) && o.status == 0;
    git_output_clear (&o);
    g_free (args);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

int
git_exit_code (const char *root, ...)
{
    va_list ap;
    const char **args;
    git_output_t o;
    int status;

    va_start (ap, root);
    args = git_args_va (ap);
    va_end (ap);
    status = git_run (root, args, NULL, &o) ? o.status : -1;
    git_output_clear (&o);
    g_free (args);
    return status;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
git_run_shell (const char *dir, const char *command, const char *input, const char *const *env,
               git_output_t *output)
{
    char *file = NULL;
    char **envp;
    gboolean started = FALSE;
    int fd;

    output->out = NULL;
    output->err = NULL;
    output->status = -1;
    envp = git_environment (env);
    // written through the descriptor of the file made for us alone: a diff is nobody else's
    fd = g_file_open_tmp ("coole-git-XXXXXX", &file, NULL);
    if (fd >= 0)
    {
        const char *data = input != NULL ? input : "";
        size_t left = strlen (data);
        gboolean written = TRUE;

        while (left > 0 && written)
        {
            const ssize_t n = write (fd, data, left);

            if (n > 0)
            {
                data += n;
                left -= (size_t) n;
            }
            else
                written = n < 0 && errno == EINTR;
        }
        close (fd);
        if (written)
        {
            // the input on stdin, the command as the user wrote it
            char *argv[] = {
                (char *) "/bin/sh", (char *) "-c", (char *) "exec <\"$0\" && eval \"$1\"", file,
                (char *) command,   NULL
            };

            started = git_spawn (dir, argv, envp, output);
        }
        (void) unlink (file);
    }
    if (!started && output->out == NULL)
    {
        output->out = g_string_new (NULL);
        output->err = g_string_new (NULL);
        output->status = -1;
    }
    g_free (file);
    g_strfreev (envp);
    return started;
}

/* --------------------------------------------------------------------------------------------- */

void
git_output_clear (git_output_t *output)
{
    if (output->out != NULL)
        g_string_free (output->out, TRUE);
    if (output->err != NULL)
        g_string_free (output->err, TRUE);
    output->out = NULL;
    output->err = NULL;
}

/* --------------------------------------------------------------------------------------------- */

char *
git_output_error (const git_output_t *output)
{
    const GString *text = NULL;

    if (output->err != NULL && output->err->len != 0)
        text = output->err;
    else if (output->out != NULL && output->out->len != 0)
        text = output->out;
    if (text != NULL)
        return g_strchomp (g_strdup (text->str));
    if (output->status < 0)
        return g_strdup (_ ("git could not be run"));
    return g_strdup_printf (_ ("git ended with code %d"), output->status);
}

/* --------------------------------------------------------------------------------------------- */

char *
git_toplevel (const char *path)
{
    const char *args[] = { "rev-parse", "--show-toplevel", NULL };
    char *dir;
    char *top;

    if (path == NULL)
        return NULL;
    dir = g_file_test (path, G_FILE_TEST_IS_DIR) ? g_strdup (path) : g_path_get_dirname (path);
    top = g_file_test (dir, G_FILE_TEST_IS_DIR) ? git_line (dir, args) : NULL;
    g_free (dir);
    if (top != NULL && *top == '\0')
        g_clear_pointer (&top, g_free);
    return top;
}

/* --------------------------------------------------------------------------------------------- */

char *
git_dir (const char *root)
{
    const char *args[] = { "rev-parse", "--absolute-git-dir", NULL };

    return git_line (root, args);
}

/* --------------------------------------------------------------------------------------------- */

void
git_parse_status (const char *text, gsize len, GPtrArray *staged, GPtrArray *unstaged,
                  git_branch_t *branch)
{
    const char *p = text;
    const char *end = text + len;

    while (p < end)
    {
        const char *entry = p;
        const char *orig = NULL;
        char **f = NULL;
        const char *path = NULL;
        char x = '.', y = '.';

        p += strlen (p) + 1;
        switch (entry[0])
        {
        case '#':
            git_parse_branch_header (entry, branch);
            break;
        case '1':
            // 1 XY sub mH mI mW hH hI path
            f = git_fields (entry, 9);
            if (g_strv_length (f) == 9)
            {
                x = f[1][0];
                y = f[1][1];
                path = f[8];
            }
            break;
        case '2':
            // 2 XY sub mH mI mW hH hI Xscore path, then the original path
            f = git_fields (entry, 10);
            if (g_strv_length (f) == 10)
            {
                x = f[1][0];
                y = f[1][1];
                path = f[9];
            }
            if (p < end)
            {
                orig = p;
                p += strlen (p) + 1;
            }
            break;
        case 'u':
            // u XY sub m1 m2 m3 mW h1 h2 h3 path: a conflict, in the work tree to resolve
            f = git_fields (entry, 11);
            if (g_strv_length (f) == 11)
            {
                y = 'U';
                path = f[10];
            }
            break;
        case '?':
            y = '?';
            path = entry + 2;
            break;
        default:
            break;
        }
        if (path != NULL)
        {
            git_change_add (staged, x, path, x == 'R' || x == 'C' ? orig : NULL);
            git_change_add (unstaged, y, path, y == 'R' || y == 'C' ? orig : NULL);
        }
        g_strfreev (f);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
git_branch_clear (git_branch_t *branch)
{
    g_free (branch->head);
    g_free (branch->sha);
    g_free (branch->upstream);
    memset (branch, 0, sizeof (*branch));
}

/* --------------------------------------------------------------------------------------------- */

void
git_change_free (gpointer change)
{
    git_change_t *c = (git_change_t *) change;

    g_free (c->path);
    g_free (c->orig);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
git_parse_log (const char *text, gsize len)
{
    GPtrArray *log = g_ptr_array_new_with_free_func (git_commit_free);
    char *copy = g_strndup (text, len);
    char **records = g_strsplit (copy, "\x1e", -1);
    char **r;

    for (r = records; *r != NULL; r++)
    {
        char **f = g_strsplit (g_strchug (*r), "\x1f", 6);

        if (g_strv_length (f) == 6)
        {
            git_commit_t *c = g_new (git_commit_t, 1);
            char **parents = g_strsplit (f[4], " ", -1);
            char **q;

            c->sha = g_strdup (f[0]);
            c->author = g_strdup (f[1]);
            c->time = g_ascii_strtoll (f[2], NULL, 10);
            c->refs = g_strdup (f[3]);
            c->subject = g_strdup (f[5]);
            c->parents = 0;
            for (q = parents; *q != NULL; q++)
                if (**q != '\0')
                    c->parents++;
            c->parent_shas = g_new0 (char *, c->parents + 1);
            c->parents = 0;
            for (q = parents; *q != NULL; q++)
                if (**q != '\0')
                    c->parent_shas[c->parents++] = g_strdup (*q);
            g_strfreev (parents);
            g_ptr_array_add (log, c);
        }
        g_strfreev (f);
    }
    g_strfreev (records);
    g_free (copy);
    return log;
}

/* --------------------------------------------------------------------------------------------- */

void
git_commit_free (gpointer commit)
{
    git_commit_t *c = (git_commit_t *) commit;

    g_free (c->sha);
    g_free (c->author);
    g_free (c->refs);
    g_free (c->subject);
    g_strfreev (c->parent_shas);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
git_parse_name_status (const char *text, gsize len)
{
    GPtrArray *files = g_ptr_array_new_with_free_func (git_change_free);
    const char *p = text;
    const char *end = text + len;

    while (p < end)
    {
        const char *status = p;
        const char *first, *second = NULL;

        p += strlen (p) + 1;
        if (p >= end)
            break;
        first = p;
        p += strlen (p) + 1;
        // a rename or a copy names the file before and after
        if ((status[0] == 'R' || status[0] == 'C') && p < end)
        {
            second = p;
            p += strlen (p) + 1;
        }
        if (second != NULL)
            git_change_add (files, status[0], second, first);
        else
            git_change_add (files, status[0], first, NULL);
    }
    return files;
}

/* --------------------------------------------------------------------------------------------- */

char *
git_message_strip (const char *text)
{
    GString *out = g_string_new (NULL);
    char **lines = g_strsplit (text != NULL ? text : "", "\n", -1);
    gboolean blank = FALSE;
    char **l;

    for (l = lines; *l != NULL && !git_scissors (*l); l++)
    {
        if (**l == '#')
            continue;
        g_strchomp (*l);
        if (**l == '\0')
        {
            blank = out->len != 0;
            continue;
        }
        if (blank)
            g_string_append_c (out, '\n');
        blank = FALSE;
        g_string_append (out, *l);
        g_string_append_c (out, '\n');
    }
    g_strfreev (lines);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

char *
git_message_add_trailer (const char *text, const char *trailer)
{
    int comments;
    char **lines = git_message_lines (text, &comments);
    GPtrArray *msg = g_ptr_array_new ();
    guint start = git_message_body (lines, comments, msg);
    char *out;

    git_trailer_append (msg, &start, trailer);
    out = git_message_join (msg, lines, comments);
    g_ptr_array_free (msg, TRUE);
    g_strfreev (lines);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

char *
git_message_replace (const char *text, const char *message, gboolean keep_trailers)
{
    int comments, new_comments;
    char **lines = git_message_lines (text, &comments);
    char **new_lines = git_message_lines (message, &new_comments);
    GPtrArray *old = g_ptr_array_new ();
    GPtrArray *msg = g_ptr_array_new ();
    const guint old_start = git_message_body (lines, comments, old);
    guint start = git_message_body (new_lines, new_comments, msg);
    guint i;
    char *out;

    for (i = old_start; keep_trailers && i < old->len; i++)
        git_trailer_append (msg, &start, g_ptr_array_index (old, i));
    out = git_message_join (msg, lines, comments);
    g_ptr_array_free (msg, TRUE);
    g_ptr_array_free (old, TRUE);
    g_strfreev (new_lines);
    g_strfreev (lines);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

char *
git_message_check (const char *text)
{
    char *message = git_message_strip (text);
    char **lines = g_strsplit (text != NULL ? text : "", "\n", -1);
    GString *s = g_string_new (NULL);
    int i, subject = -1, after = 0;

    if (*message == '\0')
        g_string_append (s, _ ("The message is empty.\n"));
    // the lines numbered as in the file, those git leaves out skipped
    for (i = 0; *message != '\0' && lines[i] != NULL && !git_scissors (lines[i]); i++)
    {
        const char *line = lines[i];
        long len;

        if (*line == '#' || (subject < 0 && git_blank (line)))
            continue;
        len = git_line_length (line);
        if (subject < 0)
        {
            subject = i;
            if (len > 72)
                g_string_append_printf (s, _ ("The subject is %ld characters long, over 72.\n"),
                                        len);
            else if (len > 50)
                g_string_append_printf (s, _ ("The subject is %ld characters long, over 50.\n"),
                                        len);
            if (g_str_has_suffix (g_strchomp (lines[i]), "."))
                g_string_append (s, _ ("The subject ends with a period.\n"));
            continue;
        }
        if (after++ == 0 && !git_blank (line))
            g_string_append (s, _ ("The second line is not empty: the subject runs on.\n"));
        if (len > 72 && !git_is_trailer (line))
            g_string_append_printf (s, _ ("Line %d is %ld characters long, over 72.\n"), i + 1,
                                    len);
    }
    g_strfreev (lines);
    g_free (message);
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
git_menu_item_free (gpointer p)
{
    git_menu_item_t *item = (git_menu_item_t *) p;

    g_free (item->label);
    g_free (item->action);
    g_free (item->command);
    g_free (item->output);
    g_strfreev (item->modes);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
git_menu_parse (const char *text, GError **error)
{
    GKeyFile *kf = g_key_file_new ();
    GPtrArray *menu = NULL;
    char **groups, **g;

    if (!g_key_file_load_from_data (kf, text, (gsize) -1, G_KEY_FILE_NONE, error))
    {
        g_key_file_free (kf);
        return NULL;
    }
    menu = g_ptr_array_new_with_free_func (git_menu_item_free);
    groups = g_key_file_get_groups (kf, NULL);
    for (g = groups; *g != NULL; g++)
    {
        git_menu_item_t *item;
        char *action = g_key_file_get_string (kf, *g, "action", NULL);
        // a command as it is written, its backslashes those of sh and not escapes of GLib
        char *command = g_key_file_get_value (kf, *g, "command", NULL);

        if ((action == NULL || *action == '\0') && (command == NULL || *command == '\0'))
        {
            g_free (action);
            g_free (command);
            continue;
        }
        item = g_new0 (git_menu_item_t, 1);
        item->key = **g;
        item->label = g_key_file_get_string (kf, *g, "label", NULL);
        item->action = action != NULL && *action != '\0' ? g_strstrip (action) : NULL;
        if (item->action == NULL)
            g_free (action);
        item->command = item->action == NULL ? command : NULL;
        if (item->command == NULL)
            g_free (command);
        item->output = g_key_file_get_string (kf, *g, "output", NULL);
        if (item->output != NULL)
            g_strstrip (item->output);
        item->modes = g_key_file_get_string_list (kf, *g, "modes", NULL, NULL);
        g_ptr_array_add (menu, item);
    }
    g_strfreev (groups);
    g_key_file_free (kf);
    return menu;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
git_menu_item_in_mode (const git_menu_item_t *item, const char *mode)
{
    char **m;

    if (item->modes == NULL || item->modes[0] == NULL)
        return TRUE;
    for (m = item->modes; *m != NULL; m++)
        if (strcmp (g_strstrip (*m), mode) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
git_reword (const char *root, const char *sha, const char *message, char **new_head, GError **error)
{
    const char *head_args[] = { "rev-parse", "--verify", "HEAD", NULL };
    const char *self_args[] = { "rev-list", "--parents", "-n", "1", sha, NULL };
    char *range = g_strconcat (sha, "..HEAD", (char *) NULL);
    const char *after_args[] = { "rev-list", "--reverse", "--parents", range, NULL };
    char *head = NULL, *gitdir = NULL, *self = NULL, *after = NULL;
    char **lines = NULL;
    GPtrArray *parents = g_ptr_array_new_with_free_func (g_free);
    char *made = NULL;
    gboolean ok = FALSE;
    char **l;

    head = git_line (root, head_args);
    gitdir = git_dir (root);
    self = git_line (root, self_args);
    if (head == NULL || gitdir == NULL || self == NULL)
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, _ ("No commit %s here"), sha);
        goto out;
    }
    {
        const char *anc_args[] = { "merge-base", "--is-ancestor", sha, "HEAD", NULL };

        if (!git_ok (root, anc_args, NULL, NULL))
        {
            g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                         _ ("The commit %.7s is not on the branch"), sha);
            goto out;
        }
    }
    if (!git_ok (root, after_args, NULL, &after))
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED, _ ("No history after %.7s"), sha);
        goto out;
    }
    lines = g_strsplit (g_strchomp (after), "\n", -1);
    for (l = lines; *l != NULL; l++)
        if (**l != '\0' && strchr (*l, ' ') != strrchr (*l, ' '))
        {
            g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                         _ ("A merge comes after %.7s: its message cannot be changed here"), sha);
            goto out;
        }

    // the commit itself, on its own parents
    {
        char **p = g_strsplit (self, " ", -1);
        char **q;

        for (q = p + 1; *q != NULL; q++)
            if (**q != '\0')
                g_ptr_array_add (parents, g_strdup (*q));
        g_strfreev (p);
    }
    made = git_commit_again (root, gitdir, sha, parents, message, error);

    // the ones after it, each on the one made before
    for (l = lines; made != NULL && *l != NULL; l++)
    {
        char **p;
        char *raw;

        if (**l == '\0')
            continue;
        p = g_strsplit (*l, " ", 2);
        raw = git_raw_message (root, p[0], error);
        g_ptr_array_set_size (parents, 0);
        g_ptr_array_add (parents, made);
        made = raw != NULL ? git_commit_again (root, gitdir, p[0], parents, raw, error) : NULL;
        g_free (raw);
        g_strfreev (p);
    }

    // the branch moves when nobody has moved it meanwhile
    if (made != NULL)
    {
        char *reason = g_strconcat ("reword (coole): ", sha, (char *) NULL);
        const char *ref_args[] = { "update-ref", "-m", reason, "HEAD", made, head, NULL };
        git_output_t o;

        ok = git_run (root, ref_args, NULL, &o) && o.status == 0;
        if (!ok)
            git_set_error (error, "update-ref", &o);
        git_output_clear (&o);
        g_free (reason);
    }

out:
    if (ok && new_head != NULL)
        *new_head = g_strdup (made);
    g_free (made);
    g_ptr_array_free (parents, TRUE);
    g_strfreev (lines);
    g_free (after);
    g_free (self);
    g_free (gitdir);
    g_free (head);
    g_free (range);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */
