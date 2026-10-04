#include <config.h>

#include <sys/select.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "lib/tty/key.h"
#include "src/plugins/debugger/gdb-mi.h"

typedef struct
{
    int fd;
    select_fn callback;
    void *data;
} watched_t;

static watched_t watched[4];
static int watched_count;
static gboolean got_version;
static gboolean exited;

void
add_select_channel (int fd, select_fn callback, void *data)
{
    g_assert_cmpint (watched_count, <, G_N_ELEMENTS (watched));
    watched[watched_count++] = (watched_t) { fd, callback, data };
}

void
delete_select_channel (int fd)
{
    int i;

    for (i = 0; i < watched_count; i++)
        if (watched[i].fd == fd)
        {
            watched[i] = watched[--watched_count];
            return;
        }
}

static void
record (const char *line, void *data)
{
    (void) data;
    if (strstr (line, "GNU gdb") != NULL)
        got_version = TRUE;
    if (g_str_has_prefix (line, "=gdb-exited"))
        exited = TRUE;
}

static void
test_fields (void)
{
    char *quoted = gdb_mi_quote ("a b\\\"c\n");
    char *value =
        gdb_mi_field ("*stopped,frame={fullname=\"/tmp/a\\\"b.c\",line=\"42\"}", "fullname");
    char *line = gdb_mi_field ("*stopped,frame={fullname=\"/tmp/a.c\",line=\"42\"}", "line");

    g_assert_cmpstr (quoted, ==, "\"a b\\\\\\\"c\\n\"");
    g_assert_cmpstr (value, ==, "/tmp/a\"b.c");
    g_assert_cmpstr (line, ==, "42");
    g_free (quoted);
    g_free (value);
    g_free (line);
}

static void
test_gdb_transport (void)
{
    char *gdb = g_find_program_in_path ("gdb");
    gdb_mi_session_t *session;
    GError *error = NULL;
    gint64 deadline;

    if (gdb == NULL)
    {
        g_test_skip ("GDB is unavailable");
        return;
    }
    session = gdb_mi_session_new (record, NULL);
    g_assert_true (gdb_mi_session_start (session, gdb, &error));
    g_assert_no_error (error);
    g_assert_true (gdb_mi_session_send (session, "-gdb-version"));
    g_assert_true (gdb_mi_session_send (session, "-gdb-exit"));
    deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!exited && g_get_monotonic_time () < deadline)
    {
        fd_set set;
        struct timeval timeout = { 0, 100000 };
        int i, max_fd = -1;
        watched_t ready[4];
        int ready_count = 0;

        FD_ZERO (&set);
        for (i = 0; i < watched_count; i++)
        {
            FD_SET (watched[i].fd, &set);
            max_fd = MAX (max_fd, watched[i].fd);
        }
        if (max_fd < 0 || select (max_fd + 1, &set, NULL, NULL, &timeout) <= 0)
            continue;
        for (i = 0; i < watched_count; i++)
            if (FD_ISSET (watched[i].fd, &set))
                ready[ready_count++] = watched[i];
        for (i = 0; i < ready_count; i++)
            ready[i].callback (ready[i].fd, ready[i].data);
    }
    g_assert_true (got_version);
    g_assert_true (exited);
    gdb_mi_session_free (session);
    g_free (gdb);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/debugger/mi-fields", test_fields);
    g_test_add_func ("/debugger/gdb-transport", test_gdb_transport);
    return g_test_run ();
}
