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

    if (session->pid != 0)
        return FALSE;
    if (!g_spawn_async_with_pipes (
            NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
            &session->pid, &session->input, &session->output, &session->errors, error))
        return FALSE;

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

char *
gdb_mi_field (const char *record, const char *name)
{
    char *needle;
    const char *p;
    GString *out;

    if (*name == '\0' && record[0] != '\0' && record[1] == '"')
        p = record + 2;
    else
    {
        needle = g_strconcat (name, "=\"", NULL);
        p = strstr (record, needle);
        while (p != NULL && p > record && p[-1] != ',' && p[-1] != '{')
            p = strstr (p + 1, needle);
        g_free (needle);
        if (p != NULL)
            p += strlen (name) + 2;
    }
    if (p == NULL)
        return NULL;
    out = g_string_new (NULL);
    while (*p != '\0' && *p != '"')
    {
        if (*p == '\\' && p[1] != '\0')
        {
            p++;
            if (*p == 'n')
                g_string_append_c (out, '\n');
            else if (*p == 'r')
                g_string_append_c (out, '\r');
            else if (*p == 't')
                g_string_append_c (out, '\t');
            else
                g_string_append_c (out, *p);
        }
        else
            g_string_append_c (out, *p);
        p++;
    }
    return g_string_free (out, FALSE);
}
