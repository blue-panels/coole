/*
   The build plugin: builds the project, shows what the compiler says, and goes to it.

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

/** \file build.c
 *  \brief Source: the build plugin
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
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/plugin-service.h"

#include "build-core.h"
#include "build.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define BUILD_SERVICE        "build"
#define BUILD_KEYMAP_SECTION "build"
#define BUILD_OUTPUT_MAX     (512 * 1024)

/*** file scope type declarations ****************************************************************/

enum
{
    BUILD_CMD_NONE = -1,
    BUILD_CMD_RUN,
    BUILD_CMD_NEXT,
    BUILD_CMD_PREV,
    BUILD_CMD_COUNT
};

enum
{
    BUILD_ACT_RUN,
    BUILD_ACT_NEXT,
    BUILD_ACT_PREV,
    BUILD_ACT_CONFIGURE,
    BUILD_ACT_OUTPUT
};

enum
{
    BUILD_MARK_ERROR,
    BUILD_MARK_WARNING,
    BUILD_MARK_COUNT
};

typedef struct
{
    mc_editor_host_t *host;
    char *root;     // of the build going on, or of the last one
    char *command;  // that it runs
    char *dir;      // where the compiler runs: the names it gives are from there
    GPid pid;
    int output_fd;
    GString *output;
    GString *pending;  // a line not ended yet
    gint64 window;
    // asked for by another plugin, before a start: the window goes when the build is fine
    gboolean quiet;
    GPtrArray *diagnostics;  // build_diagnostic_t
    int current;
    long commands[BUILD_CMD_COUNT];
    int marks[BUILD_MARK_COUNT];
    gboolean service;
} build_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static const mc_ep_command_t build_commands[BUILD_CMD_COUNT + 1] = {
    { "BuildRun", N_ ("Build the project"), "alt-shift-b" },
    { "BuildNextError", N_ ("Go to the next error of the build"), "alt-shift-j" },
    { "BuildPrevError", N_ ("Go to the previous error of the build"), "alt-shift-k" },
    { NULL, NULL, NULL },
};

static const mc_ep_marker_kind_t build_mark_kinds[BUILD_MARK_COUNT] = {
    { "build.error", "build-error-char", "✖", "E", "builderror", NULL, NULL, 6, "brightred" },
    { "build.warning", "build-warning-char", "!", "!", "buildwarning", NULL, NULL, 5, "yellow" },
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
build_diagnostic_free_cb (gpointer p)
{
    build_diagnostic_free ((build_diagnostic_t *) p);
}

/* --------------------------------------------------------------------------------------------- */

/* The project of the file in front, as the project plugin sees it, else its directory */
static char *
build_root_of (build_t *build, void *edit)
{
    char *file = edit != NULL ? build->host->get_current_file (build->host, edit) : NULL;
    GVariantDict args;
    GVariant *reply;
    char *root = NULL;

    g_variant_dict_init (&args, NULL);
    if (file != NULL)
        g_variant_dict_insert (&args, "file", "s", file);
    reply = build->host->service_call (build->host, "project", "root", g_variant_dict_end (&args),
                                       NULL);
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "root", "s", &root);
        g_variant_unref (reply);
    }
    if (root == NULL)
        root = file != NULL ? g_path_get_dirname (file) : g_get_current_dir ();
    g_free (file);
    return root;
}

/* --------------------------------------------------------------------------------------------- */

static char *
build_config_path (const char *root)
{
    char *hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, root, -1);
    char *name = g_strconcat (hash, ".ini", (char *) NULL);
    char *path = g_build_filename (g_get_user_config_dir (), "coole", "build", name, (char *) NULL);

    g_free (name);
    g_free (hash);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* The command the user gave the project, NULL when none */
static char *
build_saved_command (const char *root)
{
    GKeyFile *keyfile = g_key_file_new ();
    char *path = build_config_path (root);
    char *command = NULL;

    if (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
        command = g_key_file_get_string (keyfile, "Build", "command", NULL);
    g_key_file_free (keyfile);
    g_free (path);
    if (command != NULL && *command == '\0')
        g_clear_pointer (&command, g_free);
    return command;
}

/* --------------------------------------------------------------------------------------------- */

static void
build_save_command (const char *root, const char *command)
{
    GKeyFile *keyfile = g_key_file_new ();
    char *path = build_config_path (root);
    char *dir = g_path_get_dirname (path);
    char *contents;
    gsize len;

    g_key_file_set_string (keyfile, "Build", "root", root);
    g_key_file_set_string (keyfile, "Build", "command", command != NULL ? command : "");
    contents = g_key_file_to_data (keyfile, &len, NULL);
    if (g_mkdir_with_parents (dir, 0700) == 0)
        (void) g_file_set_contents (path, contents, (gssize) len, NULL);
    g_free (contents);
    g_free (dir);
    g_free (path);
    g_key_file_free (keyfile);
}

/* --------------------------------------------------------------------------------------------- */

/* How the project is built: the command the user gave it, else the one of its build system;
   the directory the compiler runs in */
static gboolean
build_plan (const char *root, char **command, char **dir)
{
    build_info_t *info = build_detect (root);

    *command = build_saved_command (root);
    *dir = info != NULL && info->build_dir != NULL ? g_strdup (info->build_dir) : g_strdup (root);
    if (*command == NULL && info != NULL)
        *command = g_strdup (info->command);
    build_info_free (info);
    return *command != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static GVariant *
build_viewer (build_t *build, const char *method, GVariantDict *dict)
{
    return build->host->service_call (build->host, "viewer", method, g_variant_dict_end (dict),
                                      NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
build_output_show (build_t *build)
{
    GVariantDict dict;
    GVariant *reply;
    long lines = 0;
    const char *p;

    for (p = build->output->str; *p != '\0'; p++)
        if (*p == '\n')
            lines++;
    g_variant_dict_init (&dict, NULL);
    g_variant_dict_insert (&dict, "text", "s", build->output->str);
    if (build->window != 0)
    {
        g_variant_dict_insert (&dict, "id", "x", build->window);
        reply = build_viewer (build, "set_text", &dict);
        if (reply == NULL)
            build->window = 0;
        else
            g_variant_unref (reply);
    }
    if (build->window == 0)
    {
        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "text", "s", build->output->str);
        g_variant_dict_insert (&dict, "title", "s", _ ("Build"));
        g_variant_dict_insert (&dict, "place", "s", "bottom");
        g_variant_dict_insert (&dict, "size", "i", 30);
        g_variant_dict_insert (&dict, "focus", "b", FALSE);
        reply = build_viewer (build, "open", &dict);
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "id", "x", &build->window);
            g_variant_unref (reply);
        }
    }
    // the end of the output in sight
    if (build->window != 0)
    {
        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "id", "x", build->window);
        g_variant_dict_insert (&dict, "line", "x", (gint64) MAX (1, lines));
        reply = build_viewer (build, "scroll_to", &dict);
        if (reply != NULL)
            g_variant_unref (reply);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
build_marks_clear (build_t *build)
{
    int i;

    for (i = 0; i < BUILD_MARK_COUNT; i++)
        build->host->clear_markers (build->host, NULL, build->marks[i]);
}

/* --------------------------------------------------------------------------------------------- */

static void
build_mark (build_t *build, const build_diagnostic_t *d)
{
    build->host->set_marker (build->host, d->file, d->line,
                             build->marks[d->error ? BUILD_MARK_ERROR : BUILD_MARK_WARNING], TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* A whole line of the compiler */
static void
build_line (build_t *build, const char *line)
{
    build_diagnostic_t *d = build_parse_diagnostic (line, build->dir);

    // a note goes with the error before it
    if (d == NULL || strstr (line, ": note:") != NULL)
    {
        build_diagnostic_free (d);
        return;
    }
    build_mark (build, d);
    g_ptr_array_add (build->diagnostics, d);
}

/* --------------------------------------------------------------------------------------------- */

static void
build_finished (build_t *build, int status)
{
    guint errors = 0, i;
    char *summary;

    for (i = 0; i < build->diagnostics->len; i++)
        if (((build_diagnostic_t *) g_ptr_array_index (build->diagnostics, i))->error)
            errors++;
    if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
        summary =
            g_strdup_printf (_ ("\nBuilt. Warnings: %u.\n"), build->diagnostics->len - errors);
    else
        summary = g_strdup_printf (_ ("\nThe build failed. Errors: %u, warnings: %u.\n"), errors,
                                   build->diagnostics->len - errors);
    g_string_append (build->output, summary);
    g_free (summary);
    build_output_show (build);
    build->current = -1;
    if (build->quiet && WIFEXITED (status) && WEXITSTATUS (status) == 0 && build->window != 0)
    {
        GVariantDict dict;
        GVariant *reply;

        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "id", "x", build->window);
        reply = build_viewer (build, "close", &dict);
        if (reply != NULL)
            g_variant_unref (reply);
        build->window = 0;
    }
    if (build->service)
    {
        GVariantDict dict;

        g_variant_dict_init (&dict, NULL);
        g_variant_dict_insert (&dict, "root", "s", build->root);
        g_variant_dict_insert (&dict, "ok", "b", WIFEXITED (status) && WEXITSTATUS (status) == 0);
        g_variant_dict_insert (&dict, "errors", "i", (gint32) errors);
        build->host->service_emit (build->host, BUILD_SERVICE, "finished",
                                   g_variant_dict_end (&dict));
    }
    widget_draw (WIDGET (build->host->host_data));
    tty_refresh ();
}

/* --------------------------------------------------------------------------------------------- */

static int
build_output_ready (int fd, void *data)
{
    build_t *build = (build_t *) data;
    char buf[4096];
    ssize_t n = read (fd, buf, sizeof (buf));

    if (n > 0)
    {
        char *newline;
        char *valid = g_utf8_make_valid (buf, n);

        g_string_append (build->output, valid);
        g_string_append (build->pending, valid);
        g_free (valid);
        while ((newline = strchr (build->pending->str, '\n')) != NULL)
        {
            char *line = g_strndup (build->pending->str, (gsize) (newline - build->pending->str));

            g_string_erase (build->pending, 0, newline - build->pending->str + 1);
            build_line (build, line);
            g_free (line);
        }
        if (build->output->len > BUILD_OUTPUT_MAX)
            g_string_erase (build->output, 0, build->output->len - BUILD_OUTPUT_MAX);
        build_output_show (build);
        tty_refresh ();
    }
    else if (n == 0 || (errno != EAGAIN && errno != EINTR))
    {
        int status = 0;

        delete_select_channel (fd);
        close (fd);
        build->output_fd = -1;
        if (build->pending->len > 0)
            build_line (build, build->pending->str);
        g_string_truncate (build->pending, 0);
        (void) waitpid (build->pid, &status, 0);
        g_spawn_close_pid (build->pid);
        build->pid = 0;
        build_finished (build, status);
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Build the project of @root; FALSE when it could not start */
static gboolean
build_start (build_t *build, const char *root)
{
    char *command = NULL, *dir = NULL, *shell_command;
    char *argv[4];
    GError *error = NULL;
    int out;

    if (build->pid != 0)
    {
        build->host->message (build->host, D_NORMAL, _ ("Build"), _ ("The build is running."));
        return FALSE;
    }
    if (!build_plan (root, &command, &dir))
    {
        g_free (command);
        command = input_dialog (_ ("Build"), _ ("Command that builds the project:"),
                                "build-command", "make", INPUT_COMPLETE_COMMANDS);
        if (command == NULL || *command == '\0')
        {
            g_free (command);
            g_free (dir);
            return FALSE;
        }
        build_save_command (root, command);
    }
    if (build->host->save_modified_files != NULL
        && !build->host->save_modified_files (build->host, root))
    {
        g_free (command);
        g_free (dir);
        return FALSE;
    }

    g_free (build->root);
    build->root = g_strdup (root);
    g_free (build->command);
    build->command = command;
    g_free (build->dir);
    build->dir = dir;
    g_ptr_array_set_size (build->diagnostics, 0);
    build_marks_clear (build);
    g_string_printf (build->output, "$ %s\n", command);
    g_string_truncate (build->pending, 0);

    shell_command = g_strconcat ("exec 2>&1; ", command, (char *) NULL);
    argv[0] = (char *) "/bin/sh";
    argv[1] = (char *) "-c";
    argv[2] = shell_command;
    argv[3] = NULL;
    if (!g_spawn_async_with_pipes (root, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, NULL,
                                   &build->pid, NULL, &out, NULL, &error))
    {
        g_string_append_printf (build->output, "%s\n", error->message);
        g_clear_error (&error);
        build_output_show (build);
        g_free (shell_command);
        build->pid = 0;
        return FALSE;
    }
    g_free (shell_command);
    build->output_fd = out;
    (void) fcntl (out, F_SETFL, fcntl (out, F_GETFL) | O_NONBLOCK);
    add_select_channel (out, build_output_ready, build);
    build_output_show (build);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
build_go (build_t *build, int step)
{
    const build_diagnostic_t *d;
    guint n = build->diagnostics->len;
    guint i;

    if (n == 0)
    {
        build->host->message (build->host, D_NORMAL, _ ("Build"),
                              _ ("The build has said nothing about the files."));
        return;
    }
    // the errors first: the warnings only when there is no error
    for (i = 0; i < n; i++)
    {
        build->current = (build->current + step + (int) n) % (int) n;
        if (build->current < 0)
            build->current = step > 0 ? 0 : (int) n - 1;
        d = g_ptr_array_index (build->diagnostics, build->current);
        if (d->error)
            break;
    }
    d = g_ptr_array_index (build->diagnostics, build->current);
    (void) build->host->show_location (build->host, d->file, d->line);
    {
        char *where = g_strdup_printf ("%s:%ld: %s", x_basename (d->file), d->line, d->message);

        // the message stays in sight on the output
        g_string_append_printf (build->output, "\n> %s\n", where);
        build_output_show (build);
        g_free (where);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
build_run_command (build_t *build, int cmd, void *edit)
{
    switch (cmd)
    {
    case BUILD_CMD_RUN:
    {
        build->quiet = FALSE;
        char *root =
            build_root_of (build, edit != NULL ? edit : build->host->window_top_file (build->host));

        (void) build_start (build, root);
        g_free (root);
        return TRUE;
    }
    case BUILD_CMD_NEXT:
        build_go (build, 1);
        return TRUE;
    case BUILD_CMD_PREV:
        build_go (build, -1);
        return TRUE;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
build_command (const build_t *build, long command)
{
    int i;

    for (i = 0; command != CK_IgnoreKey && i < BUILD_CMD_COUNT; i++)
        if (build->commands[i] == command)
            return i;
    return BUILD_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
build_handle_key (void *data, int key, void *edit)
{
    build_t *build = (build_t *) data;
    const int cmd =
        build_command (build, build->host->command_lookup (build->host, BUILD_KEYMAP_SECTION, key));

    return build_run_command (build, cmd, edit) ? MC_EPR_OK : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
build_handle_action (void *data, long command, void *edit)
{
    build_t *build = (build_t *) data;

    return build_run_command (build, build_command (build, command), edit) ? MC_EPR_OK
                                                                           : MC_EPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

/* The marks of the build come back on a file opened again */
static mc_ep_result_t
build_file_open (void *data, void *edit)
{
    build_t *build = (build_t *) data;
    char *file = build->host->get_current_file (build->host, edit);
    guint i;

    for (i = 0; file != NULL && i < build->diagnostics->len; i++)
    {
        const build_diagnostic_t *d = g_ptr_array_index (build->diagnostics, i);

        if (strcmp (d->file, file) == 0)
            build_mark (build, d);
    }
    g_free (file);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
build_act_run (void *data, void *edit)
{
    return build_run_command (data, BUILD_CMD_RUN, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
build_act_next (void *data, void *edit)
{
    return build_run_command (data, BUILD_CMD_NEXT, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

static mc_ep_result_t
build_act_prev (void *data, void *edit)
{
    return build_run_command (data, BUILD_CMD_PREV, edit) ? MC_EPR_OK : MC_EPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
build_act_configure (void *data, void *edit)
{
    build_t *build = (build_t *) data;
    char *root = build_root_of (build, edit);
    char *command = NULL, *dir = NULL, *chosen;

    (void) build_plan (root, &command, &dir);
    chosen = input_dialog (_ ("Build"), _ ("Command that builds the project:"), "build-command",
                           command != NULL ? command : "make", INPUT_COMPLETE_COMMANDS);
    if (chosen != NULL)
        build_save_command (root, chosen);
    g_free (chosen);
    g_free (command);
    g_free (dir);
    g_free (root);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* The service */
/* --------------------------------------------------------------------------------------------- */

static GVariant *
build_call (void *data, const char *method, GVariant *args, GError **error)
{
    build_t *build = (build_t *) data;
    GVariantDict reply;
    const char *root = NULL, *path = NULL;

    if (args != NULL)
    {
        (void) g_variant_lookup (args, "root", "&s", &root);
        (void) g_variant_lookup (args, "path", "&s", &path);
    }
    g_variant_dict_init (&reply, NULL);

    if (strcmp (method, "info") == 0 && root != NULL)
    {
        // how the project is built, and the programs it has built
        build_info_t *info = build_detect (root);
        char *command = NULL, *dir = NULL;
        GPtrArray *programs;

        (void) build_plan (root, &command, &dir);
        if (command != NULL)
            g_variant_dict_insert (&reply, "command", "s", command);
        g_variant_dict_insert (&reply, "dir", "s", dir);
        g_variant_dict_insert (&reply, "system", "s",
                               info == NULL                      ? "none"
                                   : info->system == BUILD_MESON ? "meson"
                                   : info->system == BUILD_CMAKE ? "cmake"
                                                                 : "make");
        programs = build_find_programs (dir);
        g_variant_dict_insert_value (
            &reply, "programs",
            g_variant_new_strv ((const gchar *const *) programs->pdata, (gssize) programs->len));
        g_ptr_array_free (programs, TRUE);
        build_info_free (info);
        g_free (command);
        g_free (dir);
    }
    else if (strcmp (method, "elf") == 0 && path != NULL)
    {
        build_elf_t elf;

        if (!build_elf_read (path, &elf))
            memset (&elf, 0, sizeof (elf));
        g_variant_dict_insert (&reply, "executable", "b", elf.executable);
        g_variant_dict_insert (&reply, "debug_info", "b", elf.debug_info);
        g_variant_dict_insert (&reply, "optimized", "b", elf.optimized);
    }
    else if (strcmp (method, "run") == 0 && root != NULL)
    {
        const gboolean started = build_start (build, root);

        build->quiet = started;
        g_variant_dict_insert (&reply, "started", "b", started);
    }
    else if (strcmp (method, "running") == 0)
        g_variant_dict_insert (&reply, "running", "b", build->pid != 0);
    else
    {
        g_variant_dict_clear (&reply);
        g_set_error (error, MC_SERVICE_ERROR, MC_SERVICE_ERROR_METHOD,
                     "build: no method %s, or not its arguments", method);
        return NULL;
    }
    return g_variant_dict_end (&reply);
}

/* --------------------------------------------------------------------------------------------- */

static void *
build_open (mc_editor_host_t *host, void *editor_dialog)
{
    build_t *build = g_new0 (build_t, 1);
    int i;

    (void) editor_dialog;
    build->host = host;
    build->output_fd = -1;
    build->output = g_string_new (NULL);
    build->pending = g_string_new (NULL);
    build->diagnostics = g_ptr_array_new_with_free_func (build_diagnostic_free_cb);
    build->current = -1;
    host->commands_register (host, BUILD_KEYMAP_SECTION, N_ ("&Build"), build_commands);
    for (i = 0; i < BUILD_CMD_COUNT; i++)
        build->commands[i] = host->command_id (host, build_commands[i].name);
    for (i = 0; i < BUILD_MARK_COUNT; i++)
        build->marks[i] = host->marker_kind (host, &build_mark_kinds[i]);
    build->service = host->service_register (host, BUILD_SERVICE, build_call, build, NULL);
    return build;
}

/* --------------------------------------------------------------------------------------------- */

static void
build_close (void *data)
{
    build_t *build = (build_t *) data;

    if (build->output_fd >= 0)
    {
        delete_select_channel (build->output_fd);
        close (build->output_fd);
    }
    if (build->pid != 0)
    {
        (void) kill (build->pid, SIGTERM);
        (void) waitpid (build->pid, NULL, 0);
        g_spawn_close_pid (build->pid);
    }
    if (build->service)
        build->host->service_unregister (build->host, BUILD_SERVICE);
    g_ptr_array_free (build->diagnostics, TRUE);
    g_string_free (build->output, TRUE);
    g_string_free (build->pending, TRUE);
    g_free (build->root);
    g_free (build->command);
    g_free (build->dir);
    g_free (build);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
build_ok_to_quit (void *data)
{
    build_t *build = (build_t *) data;

    return build->pid == 0
        || query_dialog (_ ("Build"), _ ("Stop the build and quit?"), D_NORMAL, 2, _ ("&Stop"),
                         _ ("&Cancel"))
        == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* The window of the output of the build, again */
static mc_ep_result_t
build_act_output (void *data, void *edit)
{
    build_t *build = (build_t *) data;

    (void) edit;
    if (build->output->len == 0)
        g_string_assign (build->output, _ ("No build yet: Alt-Shift-B builds the project.\n"));
    build->quiet = FALSE;
    build_output_show (build);
    return MC_EPR_OK;
}

static const mc_ep_action_t build_actions[] = {
    { "Build", build_act_run },           { "Next error", build_act_next },
    { "Previous error", build_act_prev }, { "Configure", build_act_configure },
    { "Output", build_act_output },
};

static const mc_ep_cmd_menu_entry_t build_menu[] = {
    { MC_EP_MENU_COMMAND, NULL, 0, NULL },
    { MC_EP_MENU_COMMAND, N_ ("B&uild project"), BUILD_ACT_RUN, NULL },
    { MC_EP_MENU_COMMAND, N_ ("Next build error"), BUILD_ACT_NEXT, NULL },
    { MC_EP_MENU_COMMAND, N_ ("Previous build error"), BUILD_ACT_PREV, NULL },
    { MC_EP_MENU_COMMAND, N_ ("Build command..."), BUILD_ACT_CONFIGURE, NULL },
    { MC_EP_MENU_PLUGINS, N_ ("&Build output"), BUILD_ACT_OUTPUT, NULL },
};

static const mc_editor_plugin_t build_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "build",
    .display_name = "Build",
    .flags = MC_EPF_NONE,
    .open = build_open,
    .close = build_close,
    .handle_key = build_handle_key,
    .handle_action = build_handle_action,
    .on_file_open = build_file_open,
    .ok_to_quit = build_ok_to_quit,
    .actions = build_actions,
    .action_count = G_N_ELEMENTS (build_actions),
    .cmd_menu_entries = build_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (build_menu),
};

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
build_get_plugin (void)
{
    return &build_plugin;
}

/* --------------------------------------------------------------------------------------------- */
