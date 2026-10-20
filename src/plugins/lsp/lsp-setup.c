/*
   The setup of a project for its language server: the checks, run as clangd sees the project,
   and the dialog of what they found.

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

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/widget.h"

#include "src/plugins/build/build-core.h"      // build_detect ()
#include "src/plugins/project/project-core.h"  // project_list_files ()

#include "lsp-setup-core.h"
#include "lsp-setup.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define LSP_HELP_FILE "lsp.md"

/* the checks at once: as many as the processors, that many at most */
#define LSP_SETUP_JOBS 16
/* the width of the text of the dialog of what was found */
#define LSP_SETUP_TEXT_COLS 70
/* the headers checked at most: evenly taken from all of them */
#define LSP_SETUP_MAX_HEADERS 400
/* the headers the include files first are taken from, and the rounds that take them */
#define LSP_SETUP_CANDIDATES 12
#define LSP_SETUP_ROUNDS     4
/* the headers with errors the candidates are tried on at most, and those kept after a round */
#define LSP_SETUP_SAMPLE 16
#define LSP_SETUP_KEPT   4

/* the buttons of the dialog of what was found */
#define LSP_SETUP_SHOW (B_USER + 1)

/*** file scope type declarations ****************************************************************/

typedef struct lsp_setup_t lsp_setup_t;

/* A clangd --check of a header */
typedef struct
{
    lsp_setup_t *setup;
    guint index;  // of the header
    GPid pid;
    int fd;  // its output, stderr
    GString *output;
} lsp_job_t;

struct lsp_setup_t
{
    char *root;
    char *program;
    char *build_dir;  // where compile_commands.json is, NULL when nowhere
    GPtrArray *files;
    char **tests;
    char **candidates;
    GPtrArray *shadowed;
    GPtrArray *headers;  // of the project, the tests aside

    // the checks of a configuration
    char **config;  // the includes checked, NULL for none: no configuration at all
    char *cdb;      // the compilation database of the project, as text; NULL for none
    char *tmp;      // a directory with a copy of it, the includes checked in its commands
    GArray *which;  // guint: the headers checked
    int *errors;    // of each header, in the last check
    guint next;     // in @which
    guint running;
    guint done;
    int partial;  // the errors of the headers checked so far
    int bound;    // a check is given up as its errors reach that; -1: never
    GPtrArray *jobs;
    void (*then) (lsp_setup_t *setup);

    // what is found
    int headers_bad;    // headers with errors alone
    int errors_before;  // their errors
    GArray *failing;    // guint: those headers
    GPtrArray *chosen;  // the includes taken, char *
    int errors_after;   // with them
    GArray *sample;     // guint: the headers with errors the includes are tried on
    int current;        // the errors of the sample with the includes taken
    int *tried;         // of the candidates in the first round: their errors, -1 not tried
    int headers_after;  // the headers with errors still
    int round;
    int best;  // of the round: the candidate, -1 for none
    int best_errors;
    int candidate;      // checked in the round
    gboolean *useless;  // of the candidates: no help in a round

    gboolean cancelled;
    gboolean finished;
    WDialog *dialog;
    WLabel *label;
    char *what;  // what is checked, for the dialog
};

/*** forward declarations (file scope functions) *************************************************/

static void lsp_setup_round (lsp_setup_t *setup);
static void lsp_setup_after_done (lsp_setup_t *setup);
static void lsp_setup_candidate_done (lsp_setup_t *setup);
static void lsp_setup_check (lsp_setup_t *setup, GArray *which, char **includes,
                             void (*then) (lsp_setup_t *setup));

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The dialog says what is checked, @what, NULL for the same as before, and how far */
static void
lsp_setup_progress (lsp_setup_t *setup, const char *what)
{
    if (what != NULL)
    {
        g_free (setup->what);
        setup->what = g_strdup (what);
    }
    if (setup->label == NULL)
        return;
    label_set_textv (setup->label, "%s\n%u / %u", setup->what != NULL ? setup->what : "",
                     setup->done, setup->which->len);
    widget_draw (WIDGET (setup->dialog));
}

/* --------------------------------------------------------------------------------------------- */

/* The checks over: the dialog closes */
static void
lsp_setup_finish (lsp_setup_t *setup)
{
    setup->finished = TRUE;
    if (setup->dialog != NULL)
    {
        setup->dialog->ret_value = B_ENTER;
        dlg_close (setup->dialog);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_setup_job_end (lsp_job_t *job)
{
    if (job->fd >= 0)
    {
        delete_select_channel (job->fd);
        close (job->fd);
        job->fd = -1;
    }
    if (job->pid != 0)
    {
        if (job->setup->cancelled)
            (void) kill (job->pid, SIGKILL);
        (void) waitpid (job->pid, NULL, 0);
        g_spawn_close_pid (job->pid);
        job->pid = 0;
    }
    g_string_free (job->output, TRUE);
    g_ptr_array_remove (job->setup->jobs, job);
    g_free (job);
}

/* --------------------------------------------------------------------------------------------- */

static void lsp_setup_spawn (lsp_setup_t *setup);

static int
lsp_setup_read (int fd, void *data)
{
    lsp_job_t *job = (lsp_job_t *) data;
    lsp_setup_t *setup = job->setup;
    char buf[8192];
    const ssize_t n = read (fd, buf, sizeof (buf));

    if (n > 0)
    {
        g_string_append_len (job->output, buf, n);
        return 0;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    // the check has ended
    setup->errors[job->index] = lsp_setup_count_errors (job->output->str);
    setup->partial += setup->errors[job->index];
    setup->running--;
    setup->done++;
    lsp_setup_job_end (job);
    if (setup->cancelled)
        return 1;
    lsp_setup_progress (setup, NULL);
    if (setup->done < setup->which->len && setup->bound >= 0 && setup->partial >= setup->bound)
    {
        /* the errors only grow as the headers are checked: these includes are no better than
           the best ones, the rest of their check is of no use */
        setup->cancelled = TRUE;
        while (setup->jobs->len > 0)
            lsp_setup_job_end (g_ptr_array_index (setup->jobs, 0));
        setup->cancelled = FALSE;
        setup->running = 0;
        setup->done = setup->which->len;
    }
    if (setup->done == setup->which->len)
        setup->then (setup);
    else
        lsp_setup_spawn (setup);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/* The next checks, as many as may run */
static void
lsp_setup_spawn (lsp_setup_t *setup)
{
    const guint jobs = CLAMP (g_get_num_processors (), 1, LSP_SETUP_JOBS);

    while (setup->running < jobs && setup->next < setup->which->len)
    {
        const guint index = g_array_index (setup->which, guint, setup->next++);
        char *header = g_build_filename (setup->root, g_ptr_array_index (setup->headers, index),
                                         (char *) NULL);
        char *check = g_strconcat ("--check=", header, (char *) NULL);
        /* the copy of the database with the includes checked, or the database itself; no
           configuration ever, .clangd of the project would change what is checked */
        char *dir = g_strconcat ("--compile-commands-dir=",
                                 setup->config != NULL          ? setup->tmp
                                     : setup->build_dir != NULL ? setup->build_dir
                                                                : setup->root,
                                 (char *) NULL);
        char *argv[] = { setup->program,
                         check,
                         (char *) "--check-locations=false",
                         dir,
                         (char *) "--enable-config=false",
                         NULL };
        lsp_job_t *job = g_new0 (lsp_job_t, 1);
        GError *error = NULL;

        job->setup = setup;
        job->index = index;
        job->fd = -1;
        job->output = g_string_new (NULL);
        if (g_spawn_async_with_pipes (setup->root, argv, NULL,
                                      G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD
                                          | G_SPAWN_STDOUT_TO_DEV_NULL,
                                      NULL, NULL, &job->pid, NULL, NULL, &job->fd, &error))
        {
            (void) fcntl (job->fd, F_SETFL, fcntl (job->fd, F_GETFL) | O_NONBLOCK);
            g_ptr_array_add (setup->jobs, job);
            add_select_channel (job->fd, lsp_setup_read, job);
            setup->running++;
        }
        else
        {
            // no clangd: nothing is known of the header
            g_clear_error (&error);
            g_string_free (job->output, TRUE);
            g_free (job);
            setup->errors[index] = 0;
            setup->done++;
        }
        g_free (check);
        g_free (dir);
        g_free (header);
    }
    if (setup->running == 0 && setup->done == setup->which->len && !setup->cancelled)
        setup->then (setup);
}

/* --------------------------------------------------------------------------------------------- */

/* The headers @which checked with @includes first (NULL: no configuration), then @then */
static void
lsp_setup_check (lsp_setup_t *setup, GArray *which, char **includes,
                 void (*then) (lsp_setup_t *setup))
{
    guint i;

    if (setup->which != which)
    {
        g_array_set_size (setup->which, 0);
        for (i = 0; i < which->len; i++)
            g_array_append_val (setup->which, g_array_index (which, guint, i));
    }
    setup->config = includes;
    if (includes != NULL)
    {
        char *cdb = lsp_setup_cdb_with (setup->cdb, setup->root, includes);
        char *file = g_build_filename (setup->tmp, "compile_commands.json", (char *) NULL);

        (void) g_file_set_contents (file, cdb != NULL ? cdb : "[]", -1, NULL);
        g_free (file);
        g_free (cdb);
    }
    setup->next = 0;
    setup->done = 0;
    setup->running = 0;
    setup->partial = 0;
    // a candidate is checked as long as it may do better than the best one of the round
    setup->bound = then == lsp_setup_candidate_done
        ? (setup->best >= 0 ? setup->best_errors : setup->current)
        : -1;
    setup->then = then;
    lsp_setup_spawn (setup);
}

/* --------------------------------------------------------------------------------------------- */

static int
lsp_setup_sum (const lsp_setup_t *setup, int *bad)
{
    int sum = 0;
    guint i;

    if (bad != NULL)
        *bad = 0;
    for (i = 0; i < setup->which->len; i++)
    {
        const int n = setup->errors[g_array_index (setup->which, guint, i)];

        sum += n;
        if (n > 0 && bad != NULL)
            (*bad)++;
    }
    return sum;
}

/* --------------------------------------------------------------------------------------------- */

/* The includes taken so far, and one more, NULL ended */
static char **
lsp_setup_with (const lsp_setup_t *setup, const char *more)
{
    GPtrArray *out = g_ptr_array_new ();
    guint i;

    for (i = 0; i < setup->chosen->len; i++)
        g_ptr_array_add (out, g_ptr_array_index (setup->chosen, i));
    if (more != NULL)
        g_ptr_array_add (out, (gpointer) more);
    g_ptr_array_add (out, NULL);
    return (char **) g_ptr_array_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static char **lsp_setup_current = NULL;

/* A candidate checked: the best of the round kept, the next one checked */
static void
lsp_setup_candidate_done (lsp_setup_t *setup)
{
    // of the headers checked: those not checked, the check given up, count as no better
    const int sum = setup->partial;

    g_free (lsp_setup_current);
    lsp_setup_current = NULL;
    if (setup->round == 0)
        setup->tried[setup->candidate] = sum;
    if (sum >= setup->current)
        setup->useless[setup->candidate] = TRUE;
    else if (setup->best < 0 || sum < setup->best_errors)
    {
        setup->best = setup->candidate;
        setup->best_errors = sum;
    }
    setup->candidate++;
    lsp_setup_round (setup);
}

/* --------------------------------------------------------------------------------------------- */

/* A round: each candidate not taken nor useless checked with those taken; the best taken */
static void
lsp_setup_round (lsp_setup_t *setup)
{
    while (setup->candidates[setup->candidate] != NULL)
    {
        const int c = setup->candidate;
        gboolean taken = FALSE;
        guint i;

        for (i = 0; i < setup->chosen->len && !taken; i++)
            taken = strcmp (g_ptr_array_index (setup->chosen, i), setup->candidates[c]) == 0;
        if (taken || setup->useless[c])
        {
            setup->candidate++;
            continue;
        }
        {
            char *what =
                g_strdup_printf (_ ("Checking the headers with %s first"), setup->candidates[c]);

            lsp_setup_current = lsp_setup_with (setup, setup->candidates[c]);
            lsp_setup_check (setup, setup->sample, lsp_setup_current, lsp_setup_candidate_done);
            lsp_setup_progress (setup, what);
            g_free (what);
        }
        return;
    }
    /* the round is over: its best taken, when it takes a tenth of the errors away at least; or
       nothing helps any more */
    if (setup->best >= 0 && setup->best_errors > setup->current - MAX (setup->current / 10, 1))
        setup->best = -1;
    if (setup->best >= 0)
    {
        g_ptr_array_add (setup->chosen, g_strdup (setup->candidates[setup->best]));
        setup->current = setup->best_errors;
    }
    // after the first round, the few that helped most are tried again, not all
    if (setup->round == 0)
    {
        int c, kept = 0, limit;

        for (limit = 0; kept < LSP_SETUP_KEPT && limit <= setup->errors_before; limit++)
            for (c = 0; setup->candidates[c] != NULL; c++)
                if (setup->tried[c] == limit)
                    kept++;
        for (c = 0; setup->candidates[c] != NULL; c++)
            if (setup->tried[c] < 0 || setup->tried[c] >= limit)
                setup->useless[c] = TRUE;
    }
    if (setup->best < 0 || setup->current == 0 || ++setup->round >= LSP_SETUP_ROUNDS)
    {
        // the headers that keep errors, with all that is taken
        if (setup->chosen->len > 0)
        {
            lsp_setup_current = lsp_setup_with (setup, NULL);
            lsp_setup_check (setup, setup->failing, lsp_setup_current, lsp_setup_after_done);
            lsp_setup_progress (setup, _ ("Checking the headers with what is found"));
        }
        else
            lsp_setup_finish (setup);
        return;
    }
    setup->best = -1;
    setup->candidate = 0;
    lsp_setup_round (setup);
}

/* --------------------------------------------------------------------------------------------- */

/* All the headers checked alone: those with errors are those the includes are tried on */
static void
lsp_setup_alone_done (lsp_setup_t *setup)
{
    guint i;

    g_array_set_size (setup->failing, 0);
    for (i = 0; i < setup->which->len; i++)
    {
        const guint index = g_array_index (setup->which, guint, i);

        if (setup->errors[index] > 0)
            g_array_append_val (setup->failing, index);
    }
    setup->errors_before = lsp_setup_sum (setup, &setup->headers_bad);
    setup->errors_after = setup->errors_before;
    setup->headers_after = setup->headers_bad;
    // the sample: the headers with errors, taken evenly
    g_array_set_size (setup->sample, 0);
    {
        const guint step = setup->failing->len > LSP_SETUP_SAMPLE
            ? (setup->failing->len + LSP_SETUP_SAMPLE - 1) / LSP_SETUP_SAMPLE
            : 1;

        for (i = 0; i < setup->failing->len; i += step)
        {
            const guint index = g_array_index (setup->failing, guint, i);

            g_array_append_val (setup->sample, index);
            setup->current += setup->errors[index];
        }
    }
    if (setup->headers_bad == 0 || setup->candidates[0] == NULL || setup->cdb == NULL)
    {
        lsp_setup_finish (setup);
        return;
    }
    setup->round = 0;
    setup->best = -1;
    setup->candidate = 0;
    lsp_setup_round (setup);
}

/* --------------------------------------------------------------------------------------------- */

/* What the headers keep with all the includes taken, for the dialog */
static void
lsp_setup_after_done (lsp_setup_t *setup)
{
    setup->errors_after = lsp_setup_sum (setup, &setup->headers_after);
    lsp_setup_finish (setup);
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_setup_free (lsp_setup_t *setup)
{
    setup->cancelled = TRUE;
    while (setup->jobs->len > 0)
        lsp_setup_job_end (g_ptr_array_index (setup->jobs, 0));
    g_ptr_array_free (setup->jobs, TRUE);
    if (setup->tmp != NULL)
    {
        char *file = g_build_filename (setup->tmp, "compile_commands.json", (char *) NULL);

        (void) g_remove (file);
        (void) g_rmdir (setup->tmp);
        g_free (file);
    }
    g_free (setup->tmp);
    g_free (setup->cdb);
    g_free (setup->what);
    g_free (setup->root);
    g_free (setup->program);
    g_free (setup->build_dir);
    if (setup->files != NULL)
        g_ptr_array_free (setup->files, TRUE);
    g_strfreev (setup->tests);
    g_strfreev (setup->candidates);
    if (setup->shadowed != NULL)
        g_ptr_array_free (setup->shadowed, TRUE);
    g_ptr_array_free (setup->headers, TRUE);
    g_array_free (setup->which, TRUE);
    g_array_free (setup->failing, TRUE);
    g_array_free (setup->sample, TRUE);
    g_free (setup->tried);
    g_ptr_array_free (setup->chosen, TRUE);
    g_free (setup->errors);
    g_free (setup->useless);
    g_free (lsp_setup_current);
    lsp_setup_current = NULL;
    g_free (setup);
}

/* --------------------------------------------------------------------------------------------- */

/* The directory of compile_commands.json: the build directory of meson or cmake, the root */
static char *
lsp_setup_build_dir (const char *root)
{
    build_info_t *build = build_detect (root);
    char *dirs[2] = { build != NULL ? build->build_dir : NULL, (char *) root };
    char *found = NULL;
    int i;

    for (i = 0; i < 2 && found == NULL; i++)
        if (dirs[i] != NULL)
        {
            char *file = g_build_filename (dirs[i], "compile_commands.json", (char *) NULL);

            if (g_file_test (file, G_FILE_TEST_IS_REGULAR))
                found = g_strdup (dirs[i]);
            g_free (file);
        }
    build_info_free (build);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

/* The checks, in a dialog with their progress; FALSE when the user cancels */
static gboolean
lsp_setup_checks (lsp_setup_t *setup)
{
    GArray *all = g_array_new (FALSE, FALSE, sizeof (guint));
    WDialog *dialog;
    WGroup *g;
    guint i;
    int answer;

    for (i = 0; i < setup->headers->len; i++)
        g_array_append_val (all, i);
    dialog = dlg_create (TRUE, 0, 0, 7, 60, WPOS_CENTER, FALSE, dialog_colors, NULL, NULL, NULL,
                         _ ("Project setup"));
    g = GROUP (dialog);
    setup->label = label_new (2, 3, "");
    group_add_widget (g, setup->label);
    group_add_widget (g, button_new (4, 25, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));
    setup->dialog = dialog;
    // the headers alone, as clangd reads them with no configuration
    lsp_setup_check (setup, all, NULL, lsp_setup_alone_done);
    lsp_setup_progress (setup, _ ("Checking the headers alone, as clangd reads them"));
    answer = setup->finished ? B_ENTER : dlg_run (dialog);
    setup->label = NULL;
    setup->dialog = NULL;
    widget_destroy (WIDGET (dialog));
    g_array_free (all, TRUE);
    if (answer != B_ENTER)
    {
        setup->cancelled = TRUE;
        while (setup->jobs->len > 0)
            lsp_setup_job_end (g_ptr_array_index (setup->jobs, 0));
        return FALSE;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Words of a list, NULL ended */
static char **
lsp_setup_words (const char *text)
{
    char **words = g_strsplit_set (text != NULL ? text : "", " \t,", -1);
    GPtrArray *out = g_ptr_array_new ();
    int i;

    for (i = 0; words[i] != NULL; i++)
        if (*words[i] != '\0')
            g_ptr_array_add (out, g_strdup (words[i]));
    g_strfreev (words);
    g_ptr_array_add (out, NULL);
    return (char **) g_ptr_array_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* .clangd as it would be */
static char *
lsp_setup_file (const lsp_setup_t *setup, gboolean headers, const char *includes, gboolean tests,
                const char *skip)
{
    char *path = g_build_filename (setup->root, ".clangd", (char *) NULL);
    char **inc = headers ? lsp_setup_words (includes) : NULL;
    char **dirs = tests ? lsp_setup_words (skip) : NULL;
    char *rules = lsp_setup_rules (inc, dirs, NULL);
    char *old = NULL, *file;

    (void) g_file_get_contents (path, &old, NULL, NULL);
    file = lsp_setup_clangd (old, rules);
    g_free (old);
    g_free (rules);
    g_strfreev (inc);
    g_strfreev (dirs);
    g_free (path);
    return file;
}

/* --------------------------------------------------------------------------------------------- */

/* A text broken into lines of LSP_SETUP_TEXT_COLS at most, at its spaces */
static void
lsp_setup_wrap (GString *text)
{
    gsize i, line_start = 0, last_space = 0;

    for (i = 0; i < text->len; i++)
    {
        if (text->str[i] == '\n')
        {
            line_start = i + 1;
            last_space = 0;
        }
        else if (text->str[i] == ' ')
            last_space = i;
        if (i - line_start >= LSP_SETUP_TEXT_COLS && last_space > line_start)
        {
            text->str[last_space] = '\n';
            line_start = last_space + 1;
            last_space = 0;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* A text shown as it is, its lines from the left */
static void
lsp_setup_show (const char *title, const char *text)
{
    char **lines = g_strsplit (text, "\n", -1);
    int width = 40, n = 0, i;
    Listbox *list;

    for (i = 0; lines[i] != NULL; i++)
    {
        width = MAX (width, str_term_width1 (lines[i]) + 2);
        n++;
    }
    list = listbox_window_new (MIN (n, LINES - 8), MIN (width, COLS - 8), title, NULL);
    list->dlg->help_file = LSP_HELP_FILE;
    for (i = 0; lines[i] != NULL; i++)
        LISTBOX_APPEND_TEXT (list, 0, lines[i], NULL, FALSE);
    (void) listbox_run (list);
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

/* What was found, and what the user takes of it; TRUE when .clangd is written */
static gboolean
lsp_setup_found (lsp_setup_t *setup)
{
    char *includes, *skip;
    gboolean headers = setup->chosen->len > 0, tests;
    gboolean written = FALSE, done = FALSE;
    GString *build = g_string_new (NULL), *head = g_string_new (NULL), *test = g_string_new (NULL);
    guint i;

    {
        GPtrArray *names = g_ptr_array_new ();

        for (i = 0; i < setup->chosen->len; i++)
            g_ptr_array_add (names, g_ptr_array_index (setup->chosen, i));
        g_ptr_array_add (names, NULL);
        includes = g_strjoinv (" ", (char **) names->pdata);
        g_ptr_array_free (names, TRUE);
    }
    skip = setup->shadowed->len > 0 ? g_strjoinv (" ", setup->tests) : g_strdup ("");
    tests = setup->shadowed->len > 0;

    if (setup->build_dir != NULL)
        g_string_printf (build, _ ("The build: compile_commands.json is in %s."), setup->build_dir);
    else
        g_string_assign (build,
                         _ ("The build: no compile_commands.json. meson writes it in its "
                            "build directory, cmake with "
                            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON."));
    if (setup->headers_bad == 0)
        g_string_printf (head, _ ("The %u headers checked have no error alone."),
                         setup->headers->len);
    else
    {
        g_string_printf (head,
                         _ ("%d of the %u headers have errors alone, %d of them, that the build "
                            "has not: they count on what the files that include them include."),
                         setup->headers_bad, setup->headers->len, setup->errors_before);
        if (headers)
            g_string_append_printf (head, _ ("\nWith these first: %d headers, %d errors."),
                                    setup->headers_after, setup->errors_after);
    }
    if (setup->shadowed->len == 0)
        g_string_assign (test, _ ("The tests define no function of the program again."));
    else
    {
        g_string_printf (test, _ ("The tests define %u functions of the program again: "),
                         setup->shadowed->len);
        for (i = 0; i < setup->shadowed->len && i < 4; i++)
            g_string_append_printf (test, "%s%s", i > 0 ? ", " : "",
                                    (const char *) g_ptr_array_index (setup->shadowed, i));
        g_string_append (test, setup->shadowed->len > 4 ? "... " : ". ");
        g_string_append (test,
                         _ ("Their copies take the place of the real ones in Go to "
                            "definition."));
    }

    lsp_setup_wrap (build);
    lsp_setup_wrap (head);
    lsp_setup_wrap (test);
    while (!done)
    {
        char *new_includes = NULL, *new_skip = NULL;
        char *where =
            g_strdup_printf (_ ("Written in %s/.clangd, in a block of its own."), setup->root);
        int answer;
        quick_widget_t widgets[] = {
            QUICK_LABEL (build->str, NULL),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABEL (head->str, NULL),
            QUICK_CHECKBOX (_ ("&Include first in each header:"), &headers, NULL),
            QUICK_INPUT (includes, "lsp-setup-includes", &new_includes, NULL, FALSE, FALSE,
                         INPUT_COMPLETE_NONE),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABEL (test->str, NULL),
            QUICK_CHECKBOX (_ ("&Leave out of the index these directories:"), &tests, NULL),
            QUICK_INPUT (skip, "lsp-setup-skip", &new_skip, NULL, FALSE, FALSE,
                         INPUT_COMPLETE_NONE),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABEL (where, NULL),
            QUICK_START_BUTTONS (TRUE, TRUE),
            QUICK_BUTTON (_ ("&Apply"), B_ENTER, NULL, NULL),
            QUICK_BUTTON (_ ("&Show the file"), LSP_SETUP_SHOW, NULL, NULL),
            QUICK_BUTTON (_ ("&Cancel"), B_CANCEL, NULL, NULL),
            QUICK_END,
        };
        quick_dialog_t dialog = {
            .rect = { -1, -1, 0, LSP_SETUP_TEXT_COLS + 6 },
            .title = _ ("Project setup: clangd"),
            .help = NULL,
            .help_file = LSP_HELP_FILE,
            .widgets = widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        answer = quick_dialog (&dialog);
        g_free (where);
        if (answer == B_CANCEL || answer == B_EXIT)
            done = TRUE;
        else
        {
            char *file;

            g_free (includes);
            includes = new_includes != NULL ? new_includes : g_strdup ("");
            new_includes = NULL;
            g_free (skip);
            skip = new_skip != NULL ? new_skip : g_strdup ("");
            new_skip = NULL;
            file = lsp_setup_file (setup, headers, includes, tests, skip);
            if (answer == LSP_SETUP_SHOW)
                lsp_setup_show (".clangd", file);
            else
            {
                char *path = g_build_filename (setup->root, ".clangd", (char *) NULL);
                GError *error = NULL;

                written = g_file_set_contents (path, file, -1, &error);
                if (!written)
                {
                    message (D_ERROR, _ ("Project setup"), _ ("Cannot write %s:\n%s"), path,
                             error->message);
                    g_error_free (error);
                }
                g_free (path);
                done = TRUE;
            }
            g_free (file);
        }
        g_free (new_includes);
        g_free (new_skip);
    }
    g_free (includes);
    g_free (skip);
    g_string_free (build, TRUE);
    g_string_free (head, TRUE);
    g_string_free (test, TRUE);
    return written;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_setup_knows (const char *program)
{
    char *name = program != NULL ? g_path_get_basename (program) : NULL;
    const gboolean knows = name != NULL && g_str_has_prefix (name, "clangd");

    g_free (name);
    return knows;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_setup_run (const char *root, const char *program)
{
    lsp_setup_t *setup = g_new0 (lsp_setup_t, 1);
    gboolean written = FALSE;
    guint i, step;

    setup->root = g_strdup (root);
    setup->program = g_strdup (program);
    setup->build_dir = lsp_setup_build_dir (root);
    setup->files = project_list_files (root);
    setup->jobs = g_ptr_array_new ();
    setup->headers = g_ptr_array_new_with_free_func (g_free);
    setup->which = g_array_new (FALSE, FALSE, sizeof (guint));
    setup->failing = g_array_new (FALSE, FALSE, sizeof (guint));
    setup->sample = g_array_new (FALSE, FALSE, sizeof (guint));
    setup->chosen = g_ptr_array_new_with_free_func (g_free);
    setup->tmp = g_dir_make_tmp ("coole-lsp-setup-XXXXXX", NULL);
    if (setup->build_dir != NULL)
    {
        char *file = g_build_filename (setup->build_dir, "compile_commands.json", (char *) NULL);

        (void) g_file_get_contents (file, &setup->cdb, NULL, NULL);
        g_free (file);
    }
    if (setup->files == NULL || setup->tmp == NULL)
    {
        message (D_ERROR, _ ("Project setup"), _ ("Cannot read the files of %s."), root);
        lsp_setup_free (setup);
        return FALSE;
    }
    setup->tests = lsp_setup_test_dirs (setup->files);
    setup->candidates =
        lsp_setup_first_includes (root, setup->files, setup->tests, LSP_SETUP_CANDIDATES);
    setup->useless = g_new0 (gboolean, LSP_SETUP_CANDIDATES + 1);
    setup->tried = g_new (int, LSP_SETUP_CANDIDATES + 1);
    for (i = 0; i <= LSP_SETUP_CANDIDATES; i++)
        setup->tried[i] = -1;
    setup->shadowed = lsp_setup_shadowed (root, setup->files, setup->tests);
    {
        GPtrArray *all = g_ptr_array_new ();

        for (i = 0; i < setup->files->len; i++)
        {
            const char *file = g_ptr_array_index (setup->files, i);

            if (lsp_setup_is_header (file) && !lsp_setup_in_dirs (file, setup->tests))
                g_ptr_array_add (all, (gpointer) file);
        }
        // a big project: some of its headers, taken evenly
        step = all->len > LSP_SETUP_MAX_HEADERS ? all->len / LSP_SETUP_MAX_HEADERS + 1 : 1;
        for (i = 0; i < all->len; i += step)
            g_ptr_array_add (setup->headers, g_strdup (g_ptr_array_index (all, i)));
        g_ptr_array_free (all, TRUE);
    }
    setup->errors = g_new0 (int, setup->headers->len + 1);

    if (lsp_setup_checks (setup))
        written = lsp_setup_found (setup);
    lsp_setup_free (setup);
    return written;
}

/* --------------------------------------------------------------------------------------------- */
