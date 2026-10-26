/*
   The query plugin: F6, a query written by AI from the request of the user in the file.

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

/** \file query-ai.c
 *  \brief Source: a query written by AI, F6
 *
 *  The request is a paragraph of the file of queries, or the selection.  The backend reads what
 *  AI is told of the database, the names of its tables and columns, never a row; the program of
 *  AI, claude or codex, gets it with the request on stdin and prints the query, which takes the
 *  place under the request, the request made a comment, one change Undo takes back.  The query
 *  is not run: F5 runs it.
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib-unix.h>
#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/widget.h"
#include "lib/tty/key.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

#include "query-core.h"
#include "query-int.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// what the program of AI printed on stderr shown at most, its end
#define QUERY_AI_ERR_MAX 4000

// the program of AI in the configuration of coole, when MC_QUERY_AI does not name one
#define QUERY_AI_CONFIG_GROUP "Query"
#define QUERY_AI_CONFIG_KEY   "ai"

/*** file scope type declarations ****************************************************************/

struct query_proc_t
{
    GPid pid;
    int fds[2];  // its stdout and stderr, -1 when ended
    GString *out, *err;
    gboolean cut;  // stdout was more than QUERY_OUTPUT_MAX
    query_proc_done_fn done;
    void *data;
};

struct query_ai_t
{
    char **argv;
    const char *from;  // where it is named: MC_QUERY_AI, the configuration, none
    guint program;     // the program in argv, after "env NAME=value"
    char *name;        // its name, for Output: "claude" of "env HOME=/x /usr/bin/claude -p"
    query_proc_t *proc;
    gboolean waiting;  // for the context of the backend
    gboolean stopped;
    gint64 started;

    // the request: its window, the file of it, the text as it was asked
    void *edit;
    char *file;
    guint64 revision;
    char *place;  // the text from the start of the request to the end of the answer of before
    gsize start, asked, end;
    char *question;
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

// what the program of AI is not given of the environment of coole: the secrets of the database
static const char *const query_ai_secrets[] = {
    "PGPASSWORD",
    "MC_MONGO_URI",
    "COOLE_PG_URI",
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
query_proc_setup (gpointer data)
{
    (void) data;
    // a session of its own: stopped as a whole, with what it starts
    (void) setsid ();
}

/* --------------------------------------------------------------------------------------------- */

static void
query_proc_close_fds (query_proc_t *p)
{
    int i;

    for (i = 0; i < 2; i++)
        if (p->fds[i] >= 0)
        {
            delete_select_channel (p->fds[i]);
            close (p->fds[i]);
            p->fds[i] = -1;
        }
}

/* --------------------------------------------------------------------------------------------- */

static void
query_proc_free (query_proc_t *p)
{
    g_string_free (p->out, TRUE);
    g_string_free (p->err, TRUE);
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

static int
query_proc_ready (int fd, void *data)
{
    query_proc_t *p = (query_proc_t *) data;
    const int which = fd == p->fds[0] ? 0 : 1;
    GString *s = which == 0 ? p->out : p->err;
    char buf[8192];
    const ssize_t n = read (fd, buf, sizeof (buf));

    if (n > 0)
    {
        if (which == 0 && s->len + (gsize) n > QUERY_OUTPUT_MAX)
            p->cut = TRUE;
        if (s->len < QUERY_OUTPUT_MAX)
            g_string_append_len (s, buf, MIN (n, (ssize_t) (QUERY_OUTPUT_MAX - s->len)));
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
    {
        delete_select_channel (fd);
        close (fd);
        p->fds[which] = -1;
        if (p->fds[0] < 0 && p->fds[1] < 0)
        {
            int status = 0, i;
            char *out, *err;

            // its output has ended: it ends too, or is made to
            for (i = 0; i < 200 && waitpid (p->pid, &status, WNOHANG) == 0; i++)
                g_usleep (10000);
            if (i == 200)
            {
                (void) kill (-p->pid, SIGKILL);
                (void) waitpid (p->pid, &status, 0);
            }
            g_spawn_close_pid (p->pid);
            out = g_utf8_make_valid (p->out->str, (gssize) p->out->len);
            err = g_utf8_make_valid (p->err->str, (gssize) p->err->len);
            p->done (out, err, status, p->cut, p->data);
            query_proc_free (p);
        }
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The environment of coole without its secrets */
static char **
query_ai_environ (void)
{
    char **envp = g_get_environ ();
    guint i;

    for (i = 0; i < G_N_ELEMENTS (query_ai_secrets); i++)
        envp = g_environ_unsetenv (envp, query_ai_secrets[i]);
    return envp;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_ai_forget (query_ai_t *ai)
{
    ai->edit = NULL;
    g_clear_pointer (&ai->file, g_free);
    g_clear_pointer (&ai->place, g_free);
    g_clear_pointer (&ai->question, g_free);
}

/* --------------------------------------------------------------------------------------------- */

static query_ai_t *
query_ai_get (query_t *q)
{
    if (q->ai == NULL)
    {
        query_ai_t *ai = g_new0 (query_ai_t, 1);
        const char *env = g_getenv ("MC_QUERY_AI");
        char *value = NULL;
        guint i = 0;

        // the environment first: the panel, or a run, chooses another than the configuration
        if (env != NULL && *env != '\0')
        {
            value = g_strdup (env);
            ai->from = "MC_QUERY_AI";
        }
        else if (mc_global.main_config != NULL)
            value = mc_config_get_string (mc_global.main_config, QUERY_AI_CONFIG_GROUP,
                                          QUERY_AI_CONFIG_KEY, "");
        if (ai->from == NULL && value != NULL && *value != '\0')
            ai->from = "[" QUERY_AI_CONFIG_GROUP "] " QUERY_AI_CONFIG_KEY;
        ai->argv = query_ai_argv (value, NULL);
        g_free (value);
        if (ai->argv != NULL && strcmp (ai->argv[0], "env") == 0)
            for (i = 1; ai->argv[i] != NULL && strchr (ai->argv[i], '=') != NULL; i++)
                ;
        ai->program = i;
        ai->name =
            ai->argv != NULL && ai->argv[i] != NULL ? g_path_get_basename (ai->argv[i]) : NULL;
        q->ai = ai;
    }
    return q->ai;
}

/* --------------------------------------------------------------------------------------------- */

/* The window of the request, where it is in its text now: NULL when the window is gone, or the
   text of the request is no more, or is there twice */
static void *
query_ai_place (query_t *q, query_ai_t *ai, gsize *start)
{
    void *edit = ai->file != NULL                        ? query_file_window (q, ai->file)
        : q->host->window_top_file (q->host) == ai->edit ? ai->edit
                                                         : NULL;
    char *text, *found;
    gsize len = 0;

    if (edit == NULL)
        return NULL;
    if (q->host->get_revision (q->host, edit) == ai->revision)
    {
        *start = ai->start;
        return edit;
    }
    // changed while AI was writing: the request is found again
    text = q->host->get_text (q->host, edit, &len);
    found = text != NULL ? strstr (text, ai->place) : NULL;
    if (found != NULL && strstr (found + 1, ai->place) == NULL)
        *start = (gsize) (found - text);
    else
        edit = NULL;
    g_free (text);
    return edit;
}

/* --------------------------------------------------------------------------------------------- */

/* The query @sql put under the request, in the place of the answer of before, one change */
static gboolean
query_ai_put (query_t *q, query_ai_t *ai, const char *sql)
{
    gsize start = 0, n;
    void *edit = query_ai_place (q, ai, &start);
    char *lines, *text;
    const char *p;
    WEdit *e;

    if (edit == NULL)
        return FALSE;
    e = EDIT (edit);
    if (!edit_runtime_begin (e))
        return FALSE;
    lines = query_ai_commented (ai->place, ai->asked - ai->start, q->backend->comment);
    text = g_strconcat (lines, "\n", sql, (char *) NULL);
    // as the edits of a language server: a change at a place, the cursor at the end of the query
    edit_cursor_move (e, (off_t) start - e->buffer.curs1);
    for (n = ai->start; n < ai->end; n++)
        (void) edit_delete (e, TRUE);
    for (p = text; *p != '\0'; p++)
        edit_insert (e, (unsigned char) *p);
    edit_runtime_end (e);
    e->force |= REDRAW_PAGE;
    edit_scroll_screen_over_cursor (e);
    q->host->window_show (q->host, edit);
    g_free (text);
    g_free (lines);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The program of AI has ended: its query put in the file, what else it said in Output */
static void
query_ai_done (char *out, char *err, int status, gboolean cut, void *data)
{
    query_t *q = (query_t *) data;
    query_ai_t *ai = q->ai;
    const double seconds = (double) (g_get_monotonic_time () - ai->started) / G_USEC_PER_SEC;
    const int code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    GString *text = g_string_new (ANSI_BOLD);
    char *notes = NULL, *sql = code == 0 ? query_ai_answer (out, &notes) : NULL;

    (void) cut;
    ai->proc = NULL;
    g_string_append_printf (text, "AI   %.2f s   %s   ", seconds, ai->name);
    if (ai->stopped)
        g_string_append (text, _ ("stopped"));
    else if (code != 0)
        g_string_append_printf (text, ANSI_RED "%s %d", _ ("exit code"), code);
    g_string_append (text, ANSI_RESET "\n\n");

    if (ai->stopped)
        ;
    else if (code != 0 || sql == NULL)
    {
        const gsize len = strlen (err);

        if (code == 0)
            g_string_append_printf (text, _ ("%s wrote no query:\n%s\n"), ai->name, out);
        // the end of its errors, the start of what it says being the request itself at times
        g_string_append (text, len > QUERY_AI_ERR_MAX ? err + len - QUERY_AI_ERR_MAX : err);
    }
    else if (query_ai_put (q, ai, sql))
        g_string_append (text,
                         _ ("The query is put in the file under the request, not run: F5 "
                            "runs it, Undo takes it back.\n"));
    else
        g_string_append_printf (text,
                                _ ("The request is no more where it was in the file: the query "
                                   "is here.\n\n%s\n"),
                                sql);
    if (notes != NULL)
        g_string_append_printf (text, "\n%s\n", notes);
    query_tab_set (q, QUERY_TAB_OUTPUT, text->str, TRUE);
    query_ai_forget (ai);
    g_string_free (text, TRUE);
    g_free (sql);
    g_free (notes);
    g_free (out);
    g_free (err);
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

query_proc_t *
query_proc_start (const char *const *argv, char **envp, const char *dir, const char *input,
                  query_proc_done_fn done, void *data, GError **error)
{
    query_proc_t *p;
    char *path = NULL;
    int in, out[2], err[2], i;
    const gsize len = strlen (input);

    // the input in a file of its own, gone once open: a pipe would take 64 KB of it at most
    in = g_file_open_tmp ("coole-query-XXXXXX", &path, error);
    if (in < 0)
        return NULL;
    (void) g_unlink (path);
    g_free (path);
    if (write (in, input, len) != (ssize_t) len || lseek (in, 0, SEEK_SET) != 0)
    {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s",
                     g_strerror (errno));
        close (in);
        return NULL;
    }
    if (!g_unix_open_pipe (out, FD_CLOEXEC, error))
    {
        close (in);
        return NULL;
    }
    if (!g_unix_open_pipe (err, FD_CLOEXEC, error))
    {
        close (in);
        close (out[0]);
        close (out[1]);
        return NULL;
    }

    p = g_new0 (query_proc_t, 1);
    if (!g_spawn_async_with_fds (dir, (char **) argv, envp,
                                 G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH, query_proc_setup,
                                 NULL, &p->pid, in, out[1], err[1], error))
    {
        close (in);
        for (i = 0; i < 2; i++)
        {
            close (out[i]);
            close (err[i]);
        }
        g_free (p);
        return NULL;
    }
    close (in);
    close (out[1]);
    close (err[1]);
    p->fds[0] = out[0];
    p->fds[1] = err[0];
    p->out = g_string_new (NULL);
    p->err = g_string_new (NULL);
    p->done = done;
    p->data = data;
    for (i = 0; i < 2; i++)
    {
        (void) fcntl (p->fds[i], F_SETFL, fcntl (p->fds[i], F_GETFL) | O_NONBLOCK);
        add_select_channel (p->fds[i], query_proc_ready, p);
    }
    return p;
}

/* --------------------------------------------------------------------------------------------- */

void
query_proc_stop (query_proc_t *p)
{
    (void) kill (-p->pid, SIGTERM);
}

/* --------------------------------------------------------------------------------------------- */

void
query_proc_kill (query_proc_t *p)
{
    query_proc_close_fds (p);
    (void) kill (-p->pid, SIGKILL);
    (void) waitpid (p->pid, NULL, 0);
    g_spawn_close_pid (p->pid);
    query_proc_free (p);
}

/* --------------------------------------------------------------------------------------------- */

void
query_ai_ask (query_t *q, void *edit)
{
    query_ai_t *ai = query_ai_get (q);
    WEdit *e;
    char *text, *use = NULL;
    gsize len = 0;
    off_t start, end;
    long first, last;
    gboolean selected;

    if (q->backend->ai_context == NULL)
    {
        q->host->message (q->host, D_NORMAL, _ ("Query"),
                          _ ("AI writes no queries of this database."));
        return;
    }
    if (ai->waiting || ai->proc != NULL)
    {
        if (query_dialog (_ ("Query"), _ ("AI is writing a query. Stop it?"), D_NORMAL, 2,
                          _ ("&Stop"), _ ("&Cancel"))
            == 0)
        {
            ai->stopped = TRUE;
            if (ai->proc != NULL)
                query_proc_stop (ai->proc);
            else
            {
                ai->waiting = FALSE;
                query_ai_forget (ai);
                query_tab_set (q, QUERY_TAB_OUTPUT, _ ("AI is stopped.\n"), TRUE);
            }
        }
        return;
    }
    if (ai->name == NULL)
    {
        char *message =
            g_strdup_printf (_ ("%s is no command: claude, codex or a command line."), ai->from);

        q->host->message (q->host, D_ERROR, _ ("Query"), message);
        g_free (message);
        return;
    }
    if (edit == NULL)
        edit = q->host->window_top_file (q->host);
    if (edit == NULL)
        return;
    e = EDIT (edit);
    text = q->host->get_text (q->host, edit, &len);
    if (text == NULL)
        return;
    selected = eval_marks (e, &start, &end) && end > start && (gsize) end <= len;
    first = selected ? query_line_of (text, (gsize) start) : e->buffer.curs_line;
    last = selected ? query_line_of (text, (gsize) end - 1) : first;
    query_ai_forget (ai);
    ai->question = query_ai_part (text, first, last, !selected, q->backend->comment, &ai->start,
                                  &ai->asked, &ai->end);
    if (ai->question == NULL)
    {
        q->host->message (q->host, D_NORMAL, _ ("Query"),
                          selected ? _ ("The selection is empty.")
                                   : _ ("The request to AI is the paragraph of the cursor, or the "
                                        "selection:\nthe line of the cursor is empty."));
        g_free (text);
        return;
    }
    ai->place = g_strndup (text + ai->start, ai->end - ai->start);
    ai->edit = edit;
    ai->file = q->host->get_current_file (q->host, edit);
    ai->revision = q->host->get_revision (q->host, edit);
    ai->waiting = TRUE;
    ai->stopped = FALSE;
    ai->started = g_get_monotonic_time ();
    if (q->backend->context != NULL)
        use = q->backend->context (text, first);
    g_free (text);
    query_tab_set (q, QUERY_TAB_OUTPUT, _ ("AI is told of the database...\n"), TRUE);
    query_redraw (q);
    q->backend->ai_context (q->backend_data, use);
    g_free (use);
}

/* --------------------------------------------------------------------------------------------- */

void
query_ai_context (query_t *q, const char *context, const char *error)
{
    query_ai_t *ai = q->ai;
    char *prompt, **envp, *message;
    GError *gerror = NULL;

    // stopped while it was read
    if (ai == NULL || !ai->waiting)
        return;
    ai->waiting = FALSE;
    if (context == NULL)
    {
        message = g_strdup_printf (_ ("What AI is told of the database cannot be read: %s\n"),
                                   error != NULL ? error : "");
        query_tab_set (q, QUERY_TAB_OUTPUT, message, TRUE);
        g_free (message);
        query_ai_forget (ai);
        query_redraw (q);
        return;
    }
    prompt = query_ai_prompt (q->backend->language, context, ai->question);
    envp = query_ai_environ ();
    // in no project: nothing of one is read
    ai->proc = query_proc_start ((const char *const *) ai->argv, envp, g_get_tmp_dir (), prompt,
                                 query_ai_done, q, &gerror);
    g_strfreev (envp);
    g_free (prompt);
    if (ai->proc == NULL)
    {
        message = g_strdup_printf ("%s: %s\n", ai->name, gerror->message);
        g_clear_error (&gerror);
        query_tab_set (q, QUERY_TAB_OUTPUT, message, TRUE);
        g_free (message);
        query_ai_forget (ai);
    }
    else
    {
        message = g_strdup_printf (_ ("%s is writing the query of the request... F6 stops it.\n"),
                                   ai->name);
        query_tab_set (q, QUERY_TAB_OUTPUT, message, TRUE);
        g_free (message);
    }
    query_redraw (q);
}

/* --------------------------------------------------------------------------------------------- */

void
query_ai_startup (query_t *q, GString *text)
{
    query_ai_t *ai;
    char *found;

    if (q->backend->ai_context == NULL)
        return;
    ai = query_ai_get (q);
    if (ai->name == NULL)
    {
        g_string_append_printf (text, ANSI_RED);
        g_string_append_printf (text, _ ("%s is no command: claude, codex or a command line."),
                                ai->from);
        g_string_append (text, ANSI_RESET "\n");
        return;
    }
    found = g_find_program_in_path (ai->argv[ai->program]);
    if (found != NULL)
        g_string_append_printf (text,
                                _ ("F6 writes the query of the request of the paragraph by "
                                   "AI, %s: %s\n"),
                                ai->from != NULL ? ai->from : _ ("by default"), found);
    else
        g_string_append_printf (text,
                                _ ("%s is not found: F6, the queries written by AI, needs it, or "
                                   "another in [Query] ai of the configuration\n"),
                                ai->name);
    g_free (found);
}

/* --------------------------------------------------------------------------------------------- */

void
query_ai_free (query_t *q)
{
    query_ai_t *ai = q->ai;

    if (ai == NULL)
        return;
    if (ai->proc != NULL)
        query_proc_kill (ai->proc);
    query_ai_forget (ai);
    g_strfreev (ai->argv);
    g_free (ai->name);
    g_free (ai);
    q->ai = NULL;
}

/* --------------------------------------------------------------------------------------------- */
