/*
   GDB/MI: a session of GDB with its machine interface, and the records it
   writes.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/tty/key.h"

#include "gdb-mi.h"

struct gdb_mi_session_t
{
    GPid pid;
    int input;
    int output;
    int errors;
    GString *pending;
    gdb_mi_record_fn callback;
    void *data;
};

static void
gdb_mi_close_channel (gdb_mi_session_t *session, int *fd)
{
    if (*fd >= 0)
    {
        delete_select_channel (*fd);
        close (*fd);
        *fd = -1;
    }
    if (session->output < 0 && session->errors < 0 && session->pid != 0)
    {
        (void) waitpid (session->pid, NULL, 0);
        g_spawn_close_pid (session->pid);
        session->pid = 0;
        if (session->input >= 0)
        {
            close (session->input);
            session->input = -1;
        }
        session->callback ("=gdb-exited", session->data);
    }
}

static int
gdb_mi_read_output (int fd, void *data)
{
    gdb_mi_session_t *session = (gdb_mi_session_t *) data;
    char buf[4096];
    ssize_t n;

    n = read (fd, buf, sizeof (buf));
    if (n > 0)
    {
        char *newline;

        g_string_append_len (session->pending, buf, n);
        while ((newline = memchr (session->pending->str, '\n', session->pending->len)) != NULL)
        {
            char *record =
                g_strndup (session->pending->str, (gsize) (newline - session->pending->str));

            g_string_erase (session->pending, 0, (gssize) (newline - session->pending->str + 1));
            if (*record != '\0')
                session->callback (record, session->data);
            g_free (record);
        }
        if (session->pending->len > 1024 * 1024)
            g_string_truncate (session->pending, 0);
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
        gdb_mi_close_channel (session, &session->output);
    return 0;
}

static int
gdb_mi_read_error (int fd, void *data)
{
    gdb_mi_session_t *session = (gdb_mi_session_t *) data;
    char buf[1024];
    ssize_t n = read (fd, buf, sizeof (buf) - 1);

    if (n > 0)
    {
        buf[n] = '\0';
        session->callback (buf, session->data);
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
        gdb_mi_close_channel (session, &session->errors);
    return 0;
}

gdb_mi_session_t *
gdb_mi_session_new (gdb_mi_record_fn callback, void *data)
{
    gdb_mi_session_t *session = g_new0 (gdb_mi_session_t, 1);

    session->input = session->output = session->errors = -1;
    session->pending = g_string_new (NULL);
    session->callback = callback;
    session->data = data;
    return session;
}

gboolean
gdb_mi_session_start (gdb_mi_session_t *session, const char *gdb_path, GError **error)
{
    char *argv[] = { (char *) (gdb_path != NULL ? gdb_path : "gdb"), (char *) "--nx",
                     (char *) "--quiet", (char *) "--interpreter=mi3", NULL };
    char **envp;
    gboolean spawned;

    if (session->pid != 0)
        return FALSE;
    /* GDB starts the program with $SHELL, and the arguments are written the way sh reads them:
       the user's shell, fish for one, would read them otherwise.  The program gets the user's
       shell back with -gdb-set environment. */
    envp = g_environ_setenv (g_get_environ (), "SHELL", "/bin/sh", TRUE);
    spawned = g_spawn_async_with_pipes (
        NULL, argv, envp, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
        &session->pid, &session->input, &session->output, &session->errors, error);
    g_strfreev (envp);
    if (!spawned)
        return FALSE;
    // what the GDB before left of a line is not the start of this one's
    g_string_truncate (session->pending, 0);

    (void) fcntl (session->output, F_SETFL, fcntl (session->output, F_GETFL) | O_NONBLOCK);
    (void) fcntl (session->errors, F_SETFL, fcntl (session->errors, F_GETFL) | O_NONBLOCK);
    add_select_channel (session->output, gdb_mi_read_output, session);
    add_select_channel (session->errors, gdb_mi_read_error, session);
    return TRUE;
}

gboolean
gdb_mi_session_send (gdb_mi_session_t *session, const char *command)
{
    char *line;
    size_t len, sent = 0;
    struct sigaction ignore = { 0 }, previous;
    gboolean ok = TRUE;

    if (session->input < 0 || command == NULL || strchr (command, '\n') != NULL)
        return FALSE;
    /* A crashed GDB can close its input between the user's command and write(). */
    ignore.sa_handler = SIG_IGN;
    sigemptyset (&ignore.sa_mask);
    if (sigaction (SIGPIPE, &ignore, &previous) < 0)
        return FALSE;
    line = g_strconcat (command, "\n", NULL);
    len = strlen (line);
    while (sent < len)
    {
        ssize_t n = write (session->input, line + sent, len - sent);

        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            ok = FALSE;
            break;
        }
        sent += (size_t) n;
    }
    g_free (line);
    (void) sigaction (SIGPIPE, &previous, NULL);
    return ok;
}

gboolean
gdb_mi_session_alive (const gdb_mi_session_t *session)
{
    return session != NULL && session->pid != 0;
}

void
gdb_mi_session_stop (gdb_mi_session_t *session)
{
    if (session == NULL)
        return;
    if (session->pid != 0)
    {
        int attempt;

        /* GDB owns the inferior.  Ask it to kill the target before exiting. */
        (void) gdb_mi_session_send (session, "-exec-interrupt");
        (void) gdb_mi_session_send (session, "-exec-abort");
        (void) gdb_mi_session_send (session, "-gdb-exit");
        for (attempt = 0; attempt < 50; attempt++)
        {
            if (waitpid (session->pid, NULL, WNOHANG) == session->pid)
                break;
            g_usleep (10000);
        }
        if (attempt == 50)
        {
            (void) kill (session->pid, SIGTERM);
            if (waitpid (session->pid, NULL, WNOHANG) == 0)
            {
                (void) kill (session->pid, SIGKILL);
                (void) waitpid (session->pid, NULL, 0);
            }
        }
        g_spawn_close_pid (session->pid);
        session->pid = 0;
    }
    if (session->output >= 0)
        gdb_mi_close_channel (session, &session->output);
    if (session->errors >= 0)
        gdb_mi_close_channel (session, &session->errors);
    if (session->input >= 0)
    {
        close (session->input);
        session->input = -1;
    }
    g_string_truncate (session->pending, 0);
}

void
gdb_mi_session_free (gdb_mi_session_t *session)
{
    if (session == NULL)
        return;
    gdb_mi_session_stop (session);
    g_string_free (session->pending, TRUE);
    g_free (session);
}

char *
gdb_mi_quote (const char *value)
{
    GString *out = g_string_new ("\"");
    const unsigned char *p = (const unsigned char *) (value != NULL ? value : "");

    for (; *p != '\0'; p++)
    {
        if (*p == '\\' || *p == '"')
            g_string_append_c (out, '\\');
        if (*p == '\n')
            g_string_append (out, "\\n");
        else if (*p == '\r')
            g_string_append (out, "\\r");
        else if (*p == '\t')
            g_string_append (out, "\\t");
        else
            g_string_append_c (out, (char) *p);
    }
    g_string_append_c (out, '"');
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
gdb_mi_value_free (gpointer data)
{
    gdb_mi_value_t *value = (gdb_mi_value_t *) data;

    if (value == NULL)
        return;
    g_free (value->name);
    g_free (value->string);
    if (value->items != NULL)
        g_ptr_array_free (value->items, TRUE);
    g_free (value);
}

/* --------------------------------------------------------------------------------------------- */

static gdb_mi_value_t *
gdb_mi_value_new (gdb_mi_value_kind_t kind)
{
    gdb_mi_value_t *value = g_new0 (gdb_mi_value_t, 1);

    value->kind = kind;
    if (kind != GDB_MI_STRING)
        value->items = g_ptr_array_new_with_free_func (gdb_mi_value_free);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/* A C string of GDB, from its opening quote; NULL when it is not closed */
static char *
gdb_mi_parse_cstring (const char **p)
{
    GString *out = g_string_new (NULL);
    const char *s = *p + 1;

    for (; *s != '\0' && *s != '"'; s++)
    {
        if (*s != '\\' || s[1] == '\0')
        {
            g_string_append_c (out, *s);
            continue;
        }
        s++;
        switch (*s)
        {
        case 'n':
            g_string_append_c (out, '\n');
            break;
        case 'r':
            g_string_append_c (out, '\r');
            break;
        case 't':
            g_string_append_c (out, '\t');
            break;
        case 'e':
            g_string_append_c (out, '\033');
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        {
            // an octal byte: a character of a name in UTF-8 comes so
            int byte = 0, n;

            for (n = 0; n < 3 && *s >= '0' && *s <= '7'; n++, s++)
                byte = byte * 8 + (*s - '0');
            s--;
            g_string_append_c (out, (char) byte);
            break;
        }
        default:
            g_string_append_c (out, *s);
            break;
        }
    }
    if (*s != '"')
    {
        g_string_free (out, TRUE);
        return NULL;
    }
    *p = s + 1;
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gdb_mi_value_t *gdb_mi_parse_value (const char **p);

/* name=value; NULL on an error */
static gdb_mi_value_t *
gdb_mi_parse_result (const char **p)
{
    const char *start = *p;
    gdb_mi_value_t *value;
    char *name;

    while (**p != '\0' && **p != '=' && **p != ',' && **p != '}' && **p != ']')
        (*p)++;
    if (**p != '=' || *p == start)
        return NULL;
    name = g_strndup (start, (gsize) (*p - start));
    (*p)++;
    value = gdb_mi_parse_value (p);
    if (value == NULL)
        g_free (name);
    else
        value->name = name;
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/* The items of a tuple or a list up to @close; a list has values or results */
static gboolean
gdb_mi_parse_items (const char **p, gdb_mi_value_t *container, char close)
{
    (*p)++;
    if (**p == close)
    {
        (*p)++;
        return TRUE;
    }
    while (TRUE)
    {
        gdb_mi_value_t *item;

        if (container->kind == GDB_MI_LIST && (**p == '"' || **p == '{' || **p == '['))
            item = gdb_mi_parse_value (p);
        else
            item = gdb_mi_parse_result (p);
        if (item == NULL)
            return FALSE;
        g_ptr_array_add (container->items, item);
        if (**p == ',')
            (*p)++;
        else if (**p == close)
        {
            (*p)++;
            return TRUE;
        }
        else
            return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static gdb_mi_value_t *
gdb_mi_parse_value (const char **p)
{
    gdb_mi_value_t *value;

    switch (**p)
    {
    case '"':
        value = gdb_mi_value_new (GDB_MI_STRING);
        value->string = gdb_mi_parse_cstring (p);
        if (value->string != NULL)
            return value;
        break;
    case '{':
        value = gdb_mi_value_new (GDB_MI_TUPLE);
        if (gdb_mi_parse_items (p, value, '}'))
            return value;
        break;
    case '[':
        value = gdb_mi_value_new (GDB_MI_LIST);
        if (gdb_mi_parse_items (p, value, ']'))
            return value;
        break;
    default:
        return NULL;
    }
    gdb_mi_value_free (value);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gdb_mi_record_t *
gdb_mi_parse (const char *line)
{
    gdb_mi_record_t *record = g_new0 (gdb_mi_record_t, 1);
    char *copy = g_strdup (line != NULL ? line : "");
    const char *p = copy;
    char *end;
    gsize len = strlen (copy);

    while (len > 0 && (copy[len - 1] == '\r' || copy[len - 1] == '\n'))
        copy[--len] = '\0';
    record->results = gdb_mi_value_new (GDB_MI_TUPLE);

    if (strncmp (copy, "(gdb)", 5) == 0)
    {
        record->kind = GDB_MI_RECORD_PROMPT;
        goto done;
    }

    record->token = strtoul (p, &end, 10);
    record->has_token = end != p;
    p = end;

    switch (*p)
    {
    case '~':
    case '@':
    case '&':
        if (record->has_token || p[1] != '"')
            break;
        record->kind = *p == '~' ? GDB_MI_RECORD_CONSOLE
            : *p == '@'          ? GDB_MI_RECORD_TARGET
                                 : GDB_MI_RECORD_LOG;
        p++;
        record->text = gdb_mi_parse_cstring (&p);
        if (record->text != NULL)
            goto done;
        break;
    case '^':
    case '*':
    case '+':
    case '=':
    {
        const char kind = *p;
        const char *start = ++p;

        while (*p != '\0' && *p != ',')
            p++;
        if (p == start)
            break;
        record->klass = g_strndup (start, (gsize) (p - start));
        while (*p == ',')
        {
            gdb_mi_value_t *item;

            p++;
            item = gdb_mi_parse_result (&p);
            if (item == NULL)
                break;
            g_ptr_array_add (record->results->items, item);
        }
        record->kind = kind == '^' ? GDB_MI_RECORD_RESULT
            : kind == '*'          ? GDB_MI_RECORD_EXEC
            : kind == '+'          ? GDB_MI_RECORD_STATUS
                                   : GDB_MI_RECORD_NOTIFY;
        // what could be read of a record cut short is kept
        goto done;
    }
    default:
        break;
    }

    // no record: the text as it is
    g_clear_pointer (&record->klass, g_free);
    g_clear_pointer (&record->text, g_free);
    g_ptr_array_set_size (record->results->items, 0);
    record->kind = GDB_MI_RECORD_OTHER;
    record->has_token = FALSE;
    record->token = 0;
    record->text = g_strdup (copy);

done:
    g_free (copy);
    return record;
}

/* --------------------------------------------------------------------------------------------- */

void
gdb_mi_record_free (gdb_mi_record_t *record)
{
    if (record == NULL)
        return;
    g_free (record->klass);
    g_free (record->text);
    gdb_mi_value_free (record->results);
    g_free (record);
}

/* --------------------------------------------------------------------------------------------- */

const gdb_mi_value_t *
gdb_mi_get (const gdb_mi_value_t *tuple, const char *name)
{
    guint i;

    if (tuple == NULL || tuple->items == NULL || name == NULL)
        return NULL;
    for (i = 0; i < tuple->items->len; i++)
    {
        const gdb_mi_value_t *item = g_ptr_array_index (tuple->items, i);

        if (g_strcmp0 (item->name, name) == 0)
            return item;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
gdb_mi_get_string (const gdb_mi_value_t *tuple, const char *name)
{
    const gdb_mi_value_t *value = gdb_mi_get (tuple, name);

    return value != NULL && value->kind == GDB_MI_STRING ? value->string : NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
gdb_mi_record_string (const gdb_mi_record_t *record, const char *name)
{
    return gdb_mi_get_string (record->results, name);
}
