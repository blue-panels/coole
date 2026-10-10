/*
   The debugger plugin: the project, the configurations of the programs it runs, and the form that
   sets them.

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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"

#include "src/keymap.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editwindow.h"

#include "lib/editor-plugin.h"
#include "src/plugins/project/project-core.h"  // project_find_root (), without the project plugin
#include "debug-backend.h"
#include "debugger-int.h"
#ifdef ENABLE_DAP
#include <json-glib/json-glib.h>
#endif

/*** forward declarations (file scope functions) */
/*** end of forward declarations */

/*** file scope functions *********************************************************************/

void
debug_launch_free (gpointer data)
{
    debug_launch_t *launch = (debug_launch_t *) data;

    g_free (launch->name);
    g_free (launch->executable);
    g_free (launch->arguments);
    g_free (launch->directory);
    g_free (launch->environment);
    g_free (launch->gdb_path);
    g_free (launch->backend);
    g_free (launch->adapter);
    g_free (launch->address);
    g_free (launch->launch_extra);
    g_free (launch);
}

debug_launch_t *
debug_active_launch (const debugger_t *debug)
{
    return debug->active_launch < debug->launches->len
        ? g_ptr_array_index (debug->launches, debug->active_launch)
        : NULL;
}

static char *
debug_config_path (const debugger_t *debug)
{
    char *hash, *filename, *path;

    if (debug->project_dir == NULL)
        return NULL;
    hash = g_compute_checksum_for_string (G_CHECKSUM_SHA256, debug->project_dir, -1);
    filename = g_strconcat (hash, ".ini", NULL);
    path = g_build_filename (g_get_user_config_dir (), "coole", "debug", filename, NULL);
    g_free (filename);
    g_free (hash);
    return path;
}

/* The configurations kept in the project, which go with it */
static char *
debug_project_config_path (const debugger_t *debug)
{
    return debug->project_dir != NULL
        ? g_build_filename (debug->project_dir, ".coole", "debug.ini", (char *) NULL)
        : NULL;
}

/* A name in the project as it is kept there: from the root of the project */
static char *
debug_path_from_root (const debugger_t *debug, const char *path, gboolean relative)
{
    const gsize len = strlen (debug->project_dir);

    if (relative && path != NULL && strncmp (path, debug->project_dir, len) == 0
        && (path[len] == '/' || path[len] == '\0'))
        return g_strdup (path[len] == '\0' ? "." : path + len + 1);
    return g_strdup (path != NULL ? path : "");
}

static void
debug_launches_write (const debugger_t *debug, GKeyFile *keyfile, gboolean relative)
{
    guint i;

    g_key_file_set_integer (keyfile, "Debug", "launch_count", (gint) debug->launches->len);
    g_key_file_set_integer (keyfile, "Debug", "active_launch", (gint) debug->active_launch);
    for (i = 0; i < debug->launches->len; i++)
    {
        const debug_launch_t *launch = g_ptr_array_index (debug->launches, i);
        char *group = g_strdup_printf ("Launch %u", i);
        char *executable = debug_path_from_root (debug, launch->executable, relative);
        char *directory = debug_path_from_root (debug, launch->directory, relative);

        g_key_file_set_string (keyfile, group, "name", launch->name);
        g_key_file_set_string (keyfile, group, "executable", executable);
        g_key_file_set_string (keyfile, group, "arguments",
                               launch->arguments != NULL ? launch->arguments : "");
        g_key_file_set_string (keyfile, group, "directory", directory);
        g_key_file_set_string (keyfile, group, "environment",
                               launch->environment != NULL ? launch->environment : "");
        g_key_file_set_string (keyfile, group, "gdb_path",
                               launch->gdb_path != NULL ? launch->gdb_path : "gdb");
        g_key_file_set_boolean (keyfile, group, "build", launch->build);
        g_key_file_set_boolean (keyfile, group, "terminal", launch->terminal);
        g_key_file_set_string (keyfile, group, "backend",
                               launch->backend != NULL ? launch->backend : "gdb-mi");
        if (launch->adapter != NULL)
            g_key_file_set_string (keyfile, group, "adapter", launch->adapter);
        if (launch->address != NULL)
            g_key_file_set_string (keyfile, group, "address", launch->address);
        if (launch->launch_extra != NULL)
            g_key_file_set_string (keyfile, group, "launch_extra", launch->launch_extra);
        g_free (executable);
        g_free (directory);
        g_free (group);
    }
}

/* The configurations of a keyfile; names from the root of the project are made whole */
static void
debug_launches_read (debugger_t *debug, GKeyFile *keyfile)
{
    gint launch_count;
    gsize i;

    launch_count = g_key_file_has_key (keyfile, "Debug", "launch_count", NULL)
        ? g_key_file_get_integer (keyfile, "Debug", "launch_count", NULL)
        : -1;
    if (launch_count < 0 || launch_count > 1000)
    {
        // the first version kept one, in the Debug group
        debug_launch_t *legacy = g_new0 (debug_launch_t, 1);

        legacy->name = g_strdup (_ ("Default"));
        legacy->executable = g_key_file_get_string (keyfile, "Debug", "executable", NULL);
        legacy->arguments = g_key_file_get_string (keyfile, "Debug", "arguments", NULL);
        legacy->directory = g_key_file_get_string (keyfile, "Debug", "directory", NULL);
        if (legacy->directory == NULL)
            legacy->directory = g_strdup (debug->project_dir);
        if (legacy->executable != NULL)
            g_ptr_array_add (debug->launches, legacy);
        else
            debug_launch_free (legacy);
        return;
    }
    for (i = 0; i < (gsize) launch_count; i++)
    {
        debug_launch_t *launch = g_new0 (debug_launch_t, 1);
        char *group = g_strdup_printf ("Launch %u", (guint) i);
        char *executable, *directory;

        launch->name = g_key_file_get_string (keyfile, group, "name", NULL);
        executable = g_key_file_get_string (keyfile, group, "executable", NULL);
        launch->arguments = g_key_file_get_string (keyfile, group, "arguments", NULL);
        directory = g_key_file_get_string (keyfile, group, "directory", NULL);
        launch->environment = g_key_file_get_string (keyfile, group, "environment", NULL);
        launch->gdb_path = g_key_file_get_string (keyfile, group, "gdb_path", NULL);
        launch->build = g_key_file_get_boolean (keyfile, group, "build", NULL);
        // a configuration from before there was a terminal for the program has one
        launch->terminal = !g_key_file_has_key (keyfile, group, "terminal", NULL)
            || g_key_file_get_boolean (keyfile, group, "terminal", NULL);
        // a configuration from before the debug adapters has GDB
        launch->backend = g_key_file_get_string (keyfile, group, "backend", NULL);
        launch->adapter = g_key_file_get_string (keyfile, group, "adapter", NULL);
        launch->address = g_key_file_get_string (keyfile, group, "address", NULL);
        launch->launch_extra = g_key_file_get_string (keyfile, group, "launch_extra", NULL);
        if (executable != NULL && *executable != '\0')
            launch->executable = g_canonicalize_filename (executable, debug->project_dir);
        launch->directory = g_canonicalize_filename (
            directory != NULL && *directory != '\0' ? directory : ".", debug->project_dir);
        g_free (executable);
        g_free (directory);
        if (launch->name == NULL || launch->executable == NULL)
            debug_launch_free (launch);
        else
            g_ptr_array_add (debug->launches, launch);
        g_free (group);
    }
    launch_count = g_key_file_get_integer (keyfile, "Debug", "active_launch", NULL);
    if (launch_count >= 0 && launch_count < (gint) debug->launches->len)
        debug->active_launch = (guint) launch_count;
}

static gboolean
debug_keyfile_save (debugger_t *debug, GKeyFile *keyfile, const char *path, int mode)
{
    char *contents, *directory;
    gsize length;
    GError *error = NULL;
    gboolean saved;

    contents = g_key_file_to_data (keyfile, &length, NULL);
    directory = g_path_get_dirname (path);
    saved = g_mkdir_with_parents (directory, 0700) == 0
        && g_file_set_contents (path, contents, length, &error) && g_chmod (path, mode) == 0;
    if (!saved)
        debug->host->message (debug->host, D_ERROR, _ ("Debug"),
                              error != NULL ? error->message
                                            : _ ("Could not save the debug project settings."));
    g_clear_error (&error);
    g_free (directory);
    g_free (contents);
    return saved;
}

/* The configurations go to the project or to the settings of the user; the breakpoints and the
   watches, which are one's own, to the user's always */
void
debug_config_save (debugger_t *debug)
{
    GKeyFile *keyfile;
    char *path;
    char **locations, **expressions;
    guint i;

    path = debug_config_path (debug);
    if (path == NULL)
        return;
    debug_breakpoints_sync (debug);
    keyfile = g_key_file_new ();
    g_key_file_set_string (keyfile, "Debug", "project", debug->project_dir);
    g_key_file_set_boolean (keyfile, "Debug", "in_project", debug->launches_in_project);
    if (debug->launches_in_project)
    {
        GKeyFile *shared = g_key_file_new ();
        char *shared_path = debug_project_config_path (debug);

        debug_launches_write (debug, shared, TRUE);
        (void) debug_keyfile_save (debug, shared, shared_path, 0644);
        g_free (shared_path);
        g_key_file_free (shared);
    }
    else
        debug_launches_write (debug, keyfile, FALSE);
    locations = g_new0 (char *, debug->breakpoints->len + 1);
    for (i = 0; i < debug->breakpoints->len; i++)
    {
        debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

        locations[i] = g_strdup_printf ("%s:%ld", bp->file, bp->line);
    }
    g_key_file_set_string_list (keyfile, "Debug", "breakpoints", (const gchar *const *) locations,
                                debug->breakpoints->len);
    {
        GPtrArray *off = g_ptr_array_new ();

        for (i = 0; i < debug->breakpoints->len; i++)
            if (((debug_breakpoint_t *) g_ptr_array_index (debug->breakpoints, i))->disabled)
                g_ptr_array_add (off, locations[i]);
        g_key_file_set_string_list (keyfile, "Debug", "breakpoints_disabled",
                                    (const gchar *const *) off->pdata, off->len);
        g_ptr_array_free (off, TRUE);
    }
    {
        // a breakpoint and its condition, one after the other
        GPtrArray *conditions = g_ptr_array_new ();

        for (i = 0; i < debug->breakpoints->len; i++)
        {
            const debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, i);

            if (bp->condition != NULL)
            {
                g_ptr_array_add (conditions, locations[i]);
                g_ptr_array_add (conditions, bp->condition);
            }
        }
        if (conditions->len > 0)
            g_key_file_set_string_list (keyfile, "Debug", "breakpoint_conditions",
                                        (const gchar *const *) conditions->pdata, conditions->len);
        g_ptr_array_free (conditions, TRUE);
    }
    expressions = g_new0 (char *, debug->watches->len + 1);
    for (i = 0; i < debug->watches->len; i++)
    {
        const debug_watch_t *watch = g_ptr_array_index (debug->watches, i);

        expressions[i] = g_strdup (watch->expression);
    }
    g_key_file_set_string_list (keyfile, "Debug", "watches", (const gchar *const *) expressions,
                                debug->watches->len);
    (void) debug_keyfile_save (debug, keyfile, path, 0600);
    g_strfreev (locations);
    g_strfreev (expressions);
    g_key_file_free (keyfile);
    g_free (path);
}

static void
debug_config_load (debugger_t *debug)
{
    GKeyFile *keyfile, *shared;
    char *path, *shared_path;
    char **locations, **expressions;
    gsize count, i;
    gboolean own_launches = FALSE;

    path = debug_config_path (debug);
    if (path == NULL)
        return;
    keyfile = g_key_file_new ();
    if (g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, NULL))
    {
        // Keep in the project turned off: the user's configurations, the project's aside
        own_launches = g_key_file_has_key (keyfile, "Debug", "in_project", NULL)
            && !g_key_file_get_boolean (keyfile, "Debug", "in_project", NULL);
        debug_launches_read (debug, keyfile);
        expressions = g_key_file_get_string_list (keyfile, "Debug", "watches", &count, NULL);
        for (i = 0; expressions != NULL && i < count; i++)
        {
            debug_watch_t *watch;

            if (expressions[i][0] == '\0')
                continue;
            watch = g_new0 (debug_watch_t, 1);
            watch->expression = g_strdup (expressions[i]);
            g_ptr_array_add (debug->watches, watch);
        }
        g_strfreev (expressions);
        locations = g_key_file_get_string_list (keyfile, "Debug", "breakpoints", &count, NULL);
        for (i = 0; locations != NULL && i < count; i++)
        {
            char *separator = strrchr (locations[i], ':');
            char *end = NULL;
            gint64 line;
            debug_breakpoint_t *bp;

            if (separator == NULL)
                continue;
            line = g_ascii_strtoll (separator + 1, &end, 10);
            if (end == separator + 1 || *end != '\0' || line <= 0 || line > G_MAXLONG)
                continue;
            bp = g_new0 (debug_breakpoint_t, 1);
            bp->file = g_strndup (locations[i], separator - locations[i]);
            bp->line = (long) line;
            g_ptr_array_add (debug->breakpoints, bp);
        }
        g_strfreev (locations);
        locations =
            g_key_file_get_string_list (keyfile, "Debug", "breakpoints_disabled", &count, NULL);
        for (i = 0; locations != NULL && i < count; i++)
        {
            guint k;

            for (k = 0; k < debug->breakpoints->len; k++)
            {
                debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, k);
                char *location = g_strdup_printf ("%s:%ld", bp->file, bp->line);

                if (strcmp (location, locations[i]) == 0)
                    bp->disabled = TRUE;
                g_free (location);
            }
        }
        g_strfreev (locations);
        locations =
            g_key_file_get_string_list (keyfile, "Debug", "breakpoint_conditions", &count, NULL);
        for (i = 0; locations != NULL && i + 1 < count; i += 2)
        {
            guint k;

            for (k = 0; k < debug->breakpoints->len; k++)
            {
                debug_breakpoint_t *bp = g_ptr_array_index (debug->breakpoints, k);
                char *location = g_strdup_printf ("%s:%ld", bp->file, bp->line);

                if (strcmp (location, locations[i]) == 0 && locations[i + 1][0] != '\0')
                {
                    g_free (bp->condition);
                    bp->condition = g_strdup (locations[i + 1]);
                }
                g_free (location);
            }
        }
        g_strfreev (locations);
    }
    g_key_file_free (keyfile);
    g_free (path);

    // the configurations of the project go before the user's own
    shared_path = debug_project_config_path (debug);
    shared = g_key_file_new ();
    debug->launches_in_project = FALSE;
    if (!own_launches && g_key_file_load_from_file (shared, shared_path, G_KEY_FILE_NONE, NULL))
    {
        g_ptr_array_set_size (debug->launches, 0);
        debug->active_launch = 0;
        debug_launches_read (debug, shared);
        debug->launches_in_project = TRUE;
    }
    g_key_file_free (shared);
    g_free (shared_path);
}

/* The project of a file, as the project plugin sees it; NULL without that plugin */
char *
debug_project_of (debugger_t *debug, void *edit)
{
    char *file = edit != NULL ? debug->host->get_current_file (debug->host, edit) : NULL;
    GVariantDict args;
    GVariant *reply;
    char *root = NULL;

    if (debug->host->service_call != NULL)
    {
        g_variant_dict_init (&args, NULL);
        if (file != NULL)
            g_variant_dict_insert (&args, "file", "s", file);
        reply = debug->host->service_call (debug->host, "project", "root",
                                           g_variant_dict_end (&args), NULL);
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "root", "s", &root);
            g_variant_unref (reply);
        }
    }
    // without the project plugin, by the same rules, when the file is in a project
    if (root == NULL && file != NULL)
    {
        root = project_find_root (file);
        if (!project_is_project (root))
            g_clear_pointer (&root, g_free);
    }
    g_free (file);
    return root;
}

/* Work on the project of that directory: its configurations, breakpoints and watches */
mc_ep_result_t
debug_project_switch (debugger_t *debug, char *project)
{
    if (!g_file_test (project, G_FILE_TEST_IS_DIR))
    {
        debug_error (debug, _ ("The project directory does not exist."));
        g_free (project);
        return MC_EPR_FAILED;
    }
    if (g_strcmp0 (debug->project_dir, project) == 0)
    {
        g_free (project);
        return MC_EPR_OK;
    }
    if (debug->state == DEBUG_STARTING || debug->state == DEBUG_RUNNING
        || debug->state == DEBUG_STOPPED)
    {
        if (query_dialog (_ ("Debug"), _ ("Stop the session and switch project?"), D_NORMAL, 2,
                          _ ("&Switch"), _ ("&Cancel"))
            != 0)
        {
            g_free (project);
            return MC_EPR_FAILED;
        }
    }
    if (debug->backend != NULL)
        debug->backend->ops->stop (debug->backend);
    debug_requests_clear (debug);
    debug_pty_close (debug);
    debug_clear_current (debug);
    debug->state = DEBUG_OFF;
    // the build of the project before is no start of this one
    debug->start_after_build = FALSE;
    debug->breakpoints_installed = FALSE;
    g_ptr_array_set_size (debug->breakpoints, 0);
    g_ptr_array_set_size (debug->address_breakpoints, 0);
    debug_marks_show (debug, NULL);
    g_ptr_array_set_size (debug->launches, 0);
    g_ptr_array_set_size (debug->watches, 0);
    debug->active_launch = 0;
    g_free (debug->project_dir);
    debug->project_dir = project;
    debug->venv_told = FALSE;
    g_ptr_array_set_size (debug->frames, 0);
    debug_config_load (debug);
    debug_marks_show (debug, NULL);
    if (debug->console_window != 0)
        debug_output_show (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

mc_ep_result_t
debug_open_project (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    char *file = edit != NULL ? debug->host->get_current_file (debug->host, edit) : NULL;
    char *suggested =
        debug->project_dir != NULL ? g_strdup (debug->project_dir) : debug_project_of (debug, edit);
    char *chosen;

    if (suggested == NULL)
        suggested = file != NULL ? g_path_get_dirname (file) : g_get_current_dir ();
    chosen = input_dialog (_ ("Open debug project"), _ ("Project directory:"), NULL, suggested,
                           INPUT_COMPLETE_FILENAMES);
    g_free (suggested);
    g_free (file);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    {
        char *project = g_canonicalize_filename (chosen, NULL);

        g_free (chosen);
        return debug_project_switch (debug, project);
    }
}

/* The debugger works on a project: the one the project plugin knows, else the one the user
   names */
gboolean
debug_require_project (debugger_t *debug, void *edit)
{
    char *root;

    if (debug->project_dir != NULL)
        return TRUE;
    root =
        debug_project_of (debug, edit != NULL ? edit : debug->host->window_top_file (debug->host));
    if (root != NULL)
        return debug_project_switch (debug, root) == MC_EPR_OK;
    return debug_open_project (debug, edit) == MC_EPR_OK;
}

mc_ep_result_t
debug_project_status (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    const debug_launch_t *launch = debug_active_launch (debug);
    char *message_text;

    (void) edit;
    message_text = g_strdup_printf (_ ("Project: %s\nConfiguration: %s\nExecutable: %s"),
                                    debug->project_dir != NULL ? debug->project_dir : _ ("<none>"),
                                    launch != NULL ? launch->name : _ ("<none>"),
                                    launch != NULL ? launch->executable : _ ("<none>"));
    debug->host->message (debug->host, D_NORMAL, _ ("Debug project"), message_text);
    g_free (message_text);
    return MC_EPR_OK;
}

gboolean
debug_parse_environment (debugger_t *debug, const char *value, char ***entries)
{
    GError *error = NULL;
    int count, i;

    *entries = NULL;
    if (value == NULL || *value == '\0')
        return TRUE;
    if (!g_shell_parse_argv (value, &count, entries, &error))
    {
        debug_error (debug, error->message);
        g_clear_error (&error);
        return FALSE;
    }
    for (i = 0; i < count; i++)
    {
        const char *p = (*entries)[i];

        if (!(g_ascii_isalpha (*p) || *p == '_'))
            break;
        for (p++; g_ascii_isalnum (*p) || *p == '_'; p++)
            ;
        if (*p != '=' || strchr (p + 1, '\n') != NULL || strchr (p + 1, '\r') != NULL)
            break;
    }
    if (i == count)
        return TRUE;
    debug_error (debug,
                 _ ("Environment entries must be NAME=VALUE, separated by spaces."
                    " Quote values containing spaces."));
    g_strfreev (*entries);
    *entries = NULL;
    return FALSE;
}

static void
debug_launch_copy (debug_launch_t *to, const debug_launch_t *from)
{
    to->name = g_strdup (from->name);
    to->executable = g_strdup (from->executable);
    to->arguments = g_strdup (from->arguments);
    to->directory = g_strdup (from->directory);
    to->environment = g_strdup (from->environment);
    to->gdb_path = g_strdup (from->gdb_path);
    to->build = from->build;
    to->terminal = from->terminal;
    to->backend = g_strdup (from->backend);
    to->adapter = g_strdup (from->adapter);
    to->address = g_strdup (from->address);
    to->launch_extra = g_strdup (from->launch_extra);
}

static void
debug_launch_clear (debug_launch_t *launch)
{
    g_free (launch->name);
    g_free (launch->executable);
    g_free (launch->arguments);
    g_free (launch->directory);
    g_free (launch->environment);
    g_free (launch->gdb_path);
    g_free (launch->backend);
    g_free (launch->adapter);
    g_free (launch->address);
    g_free (launch->launch_extra);
    memset (launch, 0, sizeof (*launch));
}

/* What the build plugin knows of a program: whether it has debug information, and whether the
   compiler optimized it; a word of advice when it is not fit for debugging */
static void
debug_check_program (debugger_t *debug, const char *program, const char *system,
                     const char *build_dir)
{
    GVariant *reply = debug_build_call (debug, "elf", "path", program);
    gboolean debug_info = TRUE, optimized = FALSE;
    const char *how;
    char *text_value;

    if (reply == NULL)
        return;
    (void) g_variant_lookup (reply, "debug_info", "b", &debug_info);
    (void) g_variant_lookup (reply, "optimized", "b", &optimized);
    g_variant_unref (reply);
    if (debug_info && !optimized)
        return;

    how = g_strcmp0 (system, "meson") == 0 ? "meson configure -Dbuildtype=debug %s"
        : g_strcmp0 (system, "cmake") == 0 ? "cmake -DCMAKE_BUILD_TYPE=Debug %s"
                                           : "CFLAGS='-g -O0'%s";
    {
        char *command = g_strdup_printf (
            how,
            g_strcmp0 (system, "meson") == 0 || g_strcmp0 (system, "cmake") == 0 ? build_dir : "");

        text_value = g_strdup_printf (
            debug_info
                ? _ ("%s\nwas built with optimization: the steps jump about and some variables\n"
                     "are gone.  For debugging it is built without, for example\n\n%s")
                : _ ("%s\nhas no debug information: the debugger cannot show its source.\n"
                     "It is built with it, for example\n\n%s"),
            program, command);
        g_free (command);
    }
    debug->host->message (debug->host, D_NORMAL, _ ("Debug"), text_value);
    g_free (text_value);
}

/* The debug adapter of a program by the name of its file: a program of the machine has none,
   GDB runs it */
typedef struct
{
    const char *suffix;
    const char *adapter;
    const char *address;  // the adapter is on a socket
    const char *extra;    // what its request launch needs
} debug_adapter_default_t;

static const debug_adapter_default_t debug_adapter_defaults[] = {
    { ".py", "python3 -m debugpy.adapter", NULL, NULL },
    // bash-dap runs the script in the terminal of the program by itself
    { ".sh", "bash-dap", NULL, NULL },
    { ".bash", "bash-dap", NULL, NULL },
    { ".go", "dlv dap --listen=127.0.0.1:0", "127.0.0.1:0", "{\"mode\": \"debug\"}" },
    { ".js", "js-debug-adapter 0", "127.0.0.1:0", "{\"type\": \"pwa-node\"}" },
    { ".mjs", "js-debug-adapter 0", "127.0.0.1:0", "{\"type\": \"pwa-node\"}" },
    { ".ts", "js-debug-adapter 0", "127.0.0.1:0", "{\"type\": \"pwa-node\"}" },
};

static const debug_adapter_default_t *
debug_adapter_for (const char *program)
{
    guint i;

    for (i = 0; program != NULL && i < G_N_ELEMENTS (debug_adapter_defaults); i++)
        if (g_str_has_suffix (program, debug_adapter_defaults[i].suffix))
            return &debug_adapter_defaults[i];
    return NULL;
}

/* How to get an adapter that is not there, NULL when it is not known */
const char *
debug_adapter_hint (const char *adapter)
{
    static const struct
    {
        const char *word;
        const char *how;
    } hints[] = {
        { "debugpy", N_ ("debugpy is had with: pip install debugpy") },
        { "bash-dap",
          N_ ("bash-dap, the debugger of bash scripts, needs Python 3 alone: "
              "pipx install bash-dap, or pip install --user bash-dap") },
        { "bash-debug-adapter",
          N_ ("bash-debug-adapter is the adapter of the VS Code extension Bash Debug (the "
              "package bash-debug-adapter of Mason): node out/bashDebug.js of the extension, "
              "with Node.js; its bashdb is in its bashdb_dir") },
        { "bashDebug.js",
          N_ ("bashDebug.js is the adapter of the VS Code extension Bash Debug: it "
              "needs Node.js, and its bashdb is in its bashdb_dir") },
        { "dlv", N_ ("Delve is had with: go install github.com/go-delve/delve/cmd/dlv@latest") },
        { "lldb",
          N_ ("lldb-dap comes with LLDB (apt install lldb); before LLVM 18 it is "
              "lldb-vscode") },
        { "js-debug",
          N_ ("js-debug-adapter is the server of vscode-js-debug, with Node.js "
              "(the package js-debug-adapter of Mason, or npm)") },
        { "gdb", N_ ("GDB speaks the protocol from version 14: gdb -i dap") },
    };
    guint i;

    for (i = 0; adapter != NULL && i < G_N_ELEMENTS (hints); i++)
        if (strstr (adapter, hints[i].word) != NULL)
            return _ (hints[i].how);
    return NULL;
}

/* The Python of the virtual environment of a project, NULL when it has none */
static char *
debug_project_python (const char *project_dir)
{
    static const char *const envs[] = { ".venv", "venv", ".env", "env" };
    guint i;

    for (i = 0; project_dir != NULL && i < G_N_ELEMENTS (envs); i++)
    {
        char *python = g_build_filename (project_dir, envs[i], "bin", "python", (char *) NULL);

        if (g_file_test (python, G_FILE_TEST_IS_EXECUTABLE))
            return python;
        g_free (python);
    }
    return NULL;
}

/* Whether a Python has debugpy: the program runs under it, and the server of debugpy in it */
static gboolean
debug_python_has_debugpy (const char *python)
{
    char *argv[] = { (char *) python, (char *) "-c", (char *) "import debugpy", NULL };
    int status = -1;

    return g_spawn_sync (NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                         NULL, NULL, NULL, NULL, &status, NULL)
        && status == 0;
}

/* A script of Python of a project with a virtual environment runs with its Python, with the
   packages of the project: the adapter is the debugpy of the environment, which runs the
   script with its own Python (the debugpy of the program and that of the adapter have to be of
   one version).  Without debugpy there the user is told how to get it. */
static void
debug_launch_python (debugger_t *debug, debug_launch_t *launch)
{
    char *python, *quoted;

    if (launch->executable == NULL || !g_str_has_suffix (launch->executable, ".py")
        || g_strcmp0 (launch->adapter, "python3 -m debugpy.adapter") != 0
        || (python = debug_project_python (debug->project_dir)) == NULL)
        return;
    if (!debug_python_has_debugpy (python))
    {
        if (!debug->venv_told)
        {
            char *text_value = g_strdup_printf (
                _ ("The project has a virtual environment, but its Python has no debugpy:\n"
                   "the script runs with the Python of the system, without the packages of\n"
                   "the project. With debugpy in it, it runs with them:\n\n"
                   "%s -m pip install debugpy"),
                python);

            debug->venv_told = TRUE;
            debug->host->message (debug->host, D_NORMAL, _ ("Debug"), text_value);
            g_free (text_value);
        }
        g_free (python);
        return;
    }
    // quoted only when the shell would split or change it
    quoted = strpbrk (python, " \t\n'\"\\$`*?[]{}()<>|&;~#") != NULL ? g_shell_quote (python)
                                                                     : g_strdup (python);
    g_free (launch->adapter);
    launch->adapter = g_strconcat (quoted, " -m debugpy.adapter", (char *) NULL);
    g_free (quoted);
    g_free (python);
}

/* A configuration has the adapter of its program, what the configuration has not set */
static void
debug_launch_adapter_defaults (debugger_t *debug, debug_launch_t *launch)
{
    const debug_adapter_default_t *d = debug_adapter_for (launch->executable);

    g_free (launch->backend);
    launch->backend = g_strdup ("dap");
    if ((launch->adapter == NULL || *launch->adapter == '\0')
        && (launch->address == NULL || *launch->address == '\0'))
    {
        g_free (launch->adapter);
        g_free (launch->address);
        // a program of the machine: GDB speaks the protocol too
        launch->adapter = g_strdup (d != NULL ? d->adapter : "gdb -i dap");
        launch->address = g_strdup (d != NULL ? d->address : NULL);
    }
    if ((launch->launch_extra == NULL || *launch->launch_extra == '\0') && d != NULL
        && d->extra != NULL)
    {
        g_free (launch->launch_extra);
        launch->launch_extra = g_strdup (d->extra);
    }
    debug_launch_python (debug, launch);
}

/* A new configuration as the project suggests it: the program the build has made, the root of
   the project to run it in, and a build before the start when the project can be built; else
   the file in front, when it is a script an adapter runs */
static void
debug_launch_guess (debugger_t *debug, debug_launch_t *launch, void *edit)
{
    GVariant *reply = debug_build_call (debug, "info", "root", debug->project_dir);
    const char **programs = NULL;
    const char *system = NULL, *command = NULL, *dir = NULL;
    char *program = NULL;

    launch->directory = g_strdup (debug->project_dir);
    launch->gdb_path = g_strdup ("gdb");
    launch->terminal = TRUE;
    if (reply != NULL)
    {
        (void) g_variant_lookup (reply, "programs", "^a&s", &programs);
        (void) g_variant_lookup (reply, "system", "&s", &system);
        (void) g_variant_lookup (reply, "command", "&s", &command);
        (void) g_variant_lookup (reply, "dir", "&s", &dir);
        launch->build = command != NULL;
    }

    if (programs != NULL && programs[0] != NULL && programs[1] == NULL)
        program = g_strdup (programs[0]);
    else if (programs != NULL && programs[0] != NULL)
    {
        // several: the newest first, to choose from
        const guint count = g_strv_length ((char **) programs);
        Listbox *selector;
        const char *chosen;
        guint i;

        selector = listbox_window_new (MIN ((int) count, 16), MIN (COLS - 8, 76),
                                       _ ("The program to debug"), NULL);
        for (i = 0; i < count; i++)
        {
            const char *p = programs[i];
            const gsize len = strlen (debug->project_dir);
            const char *shown =
                strncmp (p, debug->project_dir, len) == 0 && p[len] == '/' ? p + len + 1 : p;

            LISTBOX_APPEND_TEXT (selector, 0, shown, (void *) p, FALSE);
        }
        chosen = listbox_run_with_data (selector, NULL);
        program = g_strdup (chosen != NULL ? chosen : programs[0]);
    }

    if (program == NULL)
    {
        void *file_window = edit != NULL ? edit : debug->host->window_top_file (debug->host);
        char *file =
            file_window != NULL ? debug->host->get_current_file (debug->host, file_window) : NULL;

        if (debug_adapter_for (file) != NULL)
        {
            launch->executable = file;
            launch->name = g_path_get_basename (file);
            launch->build = FALSE;
            debug_launch_adapter_defaults (debug, launch);
            file = NULL;
        }
        g_free (file);
    }
    if (program != NULL)
    {
        launch->executable = program;
        launch->name = g_path_get_basename (program);
        debug_check_program (debug, program, system, dir);
    }
    else if (launch->name == NULL)
        launch->name = g_strdup (_ ("Debug"));
    g_free (programs);
    if (reply != NULL)
        g_variant_unref (reply);
}

/* All of a configuration in one form; FALSE when it is cancelled */
static gboolean
debug_launch_form (debugger_t *debug, debug_launch_t *launch, const debug_launch_t *self,
                   gboolean *in_project)
{
    // the options of the index of the symbols, when the plugin ctags is there
    char *index_options = NULL;
    gboolean result = FALSE;

    if (debug->project_dir != NULL)
    {
        GVariantDict args;
        GVariant *reply;

        g_variant_dict_init (&args, NULL);
        g_variant_dict_insert (&args, "root", "s", debug->project_dir);
        reply = debug->host->service_call (debug->host, "ctags", "options",
                                           g_variant_dict_end (&args), NULL);
        if (reply != NULL)
        {
            (void) g_variant_lookup (reply, "options", "s", &index_options);
            g_variant_unref (reply);
        }
    }

    while (TRUE)
    {
        char *name = NULL, *executable = NULL, *arguments = NULL, *directory = NULL;
        char *environment = NULL, *gdb_path = NULL, *ctags = NULL;
        char *adapter = NULL, *address = NULL, *extra = NULL, *problem_text = NULL;
        char **entries = NULL;
        gboolean build = launch->build, keep = *in_project, terminal = launch->terminal;
        const char *backend_items[] = { _ ("&GDB"), _ ("Debug &adapter") };
        int backend = debug_launch_is_dap (launch) ? 1 : 0;
        const char *problem = NULL;
        guint i;
        int ret;

        {
            quick_widget_t widgets[] = {
                QUICK_LABELED_INPUT (_ ("Name:"), input_label_above, launch->name, "debug-name",
                                     &name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("Program:"), input_label_above,
                                     launch->executable != NULL ? launch->executable : "",
                                     "debug-program", &executable, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES),
                QUICK_LABELED_INPUT (_ ("Arguments:"), input_label_above,
                                     launch->arguments != NULL ? launch->arguments : "",
                                     "debug-arguments", &arguments, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES),
                QUICK_LABELED_INPUT (_ ("Working directory:"), input_label_above,
                                     launch->directory != NULL ? launch->directory : "",
                                     "debug-directory", &directory, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_CD),
                QUICK_LABELED_INPUT (_ ("Environment (NAME=VALUE, quoted):"), input_label_above,
                                     launch->environment != NULL ? launch->environment : "",
                                     "debug-environment", &environment, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_RADIO (2, backend_items, &backend, NULL),
                QUICK_LABELED_INPUT (_ ("GDB:"), input_label_left,
                                     launch->gdb_path != NULL ? launch->gdb_path : "gdb",
                                     "debug-gdb", &gdb_path, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_COMMANDS),
                QUICK_LABELED_INPUT (_ ("Adapter:"), input_label_left,
                                     launch->adapter != NULL ? launch->adapter : "",
                                     "debug-adapter", &adapter, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_COMMANDS),
                QUICK_LABELED_INPUT (_ ("Address (host:port, 0: its own):"), input_label_left,
                                     launch->address != NULL ? launch->address : "",
                                     "debug-address", &address, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("Launch (JSON):"), input_label_left,
                                     launch->launch_extra != NULL ? launch->launch_extra : "",
                                     "debug-launch", &extra, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (_ ("Options of ctags, for the index of the symbols:"),
                                     input_label_above, index_options != NULL ? index_options : "",
                                     "debug-ctags", &ctags, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_SEPARATOR (TRUE),
                QUICK_CHECKBOX (_ ("&Build the project before the start"), &build, NULL),
                QUICK_CHECKBOX (_ ("Run in a &terminal window: its screen and keys"), &terminal,
                                NULL),
                QUICK_CHECKBOX (_ ("&Keep in the project, in .coole/debug.ini"), &keep, NULL),
                QUICK_BUTTONS_OK_CANCEL,
                QUICK_END,
            };
            WRect r = { -1, -1, 0, MIN (76, COLS - 4) };
            quick_dialog_t qdlg;

            // without the plugin ctags its line is not there
            if (index_options == NULL)
            {
                const size_t at = 10;

                memmove (&widgets[at], &widgets[at + 1],
                         sizeof (widgets) - (at + 1) * sizeof (widgets[0]));
            }
            qdlg = (quick_dialog_t) {
                .rect = r,
                .title = _ ("Debug configuration"),
                .help = "[Debugger]",
                .help_file = "debugger.md",
                .widgets = widgets,
                .callback = NULL,
                .mouse_callback = NULL,
            };

            ret = quick_dialog (&qdlg);
        }
        if (ret == B_CANCEL)
        {
            g_free (name);
            g_free (executable);
            g_free (arguments);
            g_free (directory);
            g_free (environment);
            g_free (gdb_path);
            g_free (ctags);
            g_free (adapter);
            g_free (address);
            g_free (extra);
            break;
        }
        if (ctags != NULL)
        {
            g_free (index_options);
            index_options = g_strdup (g_strstrip (ctags));
            g_free (ctags);
        }

        // what was typed stays for the next round
        debug_launch_clear (launch);
        launch->name = g_strstrip (name);
        launch->executable = *g_strstrip (executable) != '\0'
            ? g_canonicalize_filename (executable, debug->project_dir)
            : g_strdup ("");
        g_free (executable);
        launch->arguments = arguments;
        launch->directory = g_canonicalize_filename (
            *g_strstrip (directory) != '\0' ? directory : debug->project_dir, debug->project_dir);
        g_free (directory);
        launch->environment = environment;
        launch->gdb_path = *g_strstrip (gdb_path) != '\0' ? gdb_path : g_strdup ("gdb");
        if (*gdb_path == '\0')
            g_free (gdb_path);
        launch->build = build;
        launch->terminal = terminal;
        launch->backend = g_strdup (backend == 1 ? "dap" : "gdb-mi");
        launch->adapter = g_strdup (g_strstrip (adapter));
        launch->address = g_strdup (g_strstrip (address));
        launch->launch_extra = g_strdup (g_strstrip (extra));
        g_free (adapter);
        g_free (address);
        g_free (extra);
        // a script is no program of GDB: its adapter runs it; and an adapter needs a command
        if (debug_launch_is_dap (launch) || debug_adapter_for (launch->executable) != NULL)
            debug_launch_adapter_defaults (debug, launch);
        *in_project = keep;

        if (*launch->name == '\0')
            problem = _ ("Enter a configuration name.");
        for (i = 0; problem == NULL && i < debug->launches->len; i++)
        {
            const debug_launch_t *other = g_ptr_array_index (debug->launches, i);

            if (other != self && g_strcmp0 (other->name, launch->name) == 0)
                problem = _ ("A configuration with this name already exists.");
        }
        if (problem == NULL && *launch->executable == '\0')
            problem = _ ("Enter the program to debug.");
        if (problem == NULL && !g_file_test (launch->directory, G_FILE_TEST_IS_DIR))
            problem = _ ("The working directory does not exist.");
#ifdef ENABLE_DAP
        if (problem == NULL && *launch->launch_extra != '\0')
        {
            // checked here, where it can be put right, and not at the start
            JsonParser *parser = json_parser_new ();
            GError *error = NULL;

            if (!json_parser_load_from_data (parser, launch->launch_extra, -1, &error))
                problem = problem_text =
                    g_strdup_printf (_ ("The launch JSON is wrong: %s"), error->message);
            else if (!JSON_NODE_HOLDS_OBJECT (json_parser_get_root (parser)))
                problem = _ ("The launch JSON is an object: {\"name\": value, ...}.");
            g_clear_error (&error);
            g_object_unref (parser);
        }
#endif
        if (problem != NULL)
            debug_error (debug, problem);
        else if (!debug_parse_environment (debug, launch->environment, &entries))
            problem = "";
        g_strfreev (entries);
        g_free (problem_text);
        if (problem == NULL)
        {
            result = TRUE;
            break;
        }
    }

    if (result && index_options != NULL)
    {
        GVariantDict args;
        GVariant *reply;

        // the plugin ctags keeps them, and indexes again when they change
        g_variant_dict_init (&args, NULL);
        g_variant_dict_insert (&args, "root", "s", debug->project_dir);
        g_variant_dict_insert (&args, "options", "s", index_options);
        reply = debug->host->service_call (debug->host, "ctags", "set_options",
                                           g_variant_dict_end (&args), NULL);
        if (reply != NULL)
            g_variant_unref (reply);
    }
    g_free (index_options);
    return result;
}

/* A configuration made or changed: a new one is guessed from the project first */
mc_ep_result_t
debug_configure_impl (debugger_t *debug, void *edit, gboolean create_new)
{
    debug_launch_t *existing, form = { 0 };
    gboolean in_project;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    existing = create_new ? NULL : debug_active_launch (debug);
    if (existing != NULL)
        debug_launch_copy (&form, existing);
    else
        debug_launch_guess (debug, &form, edit);
    in_project = debug->launches_in_project;

    if (!debug_launch_form (debug, &form, existing, &in_project))
    {
        debug_launch_clear (&form);
        return MC_EPR_FAILED;
    }
    if (existing == NULL)
    {
        existing = g_new0 (debug_launch_t, 1);
        g_ptr_array_add (debug->launches, existing);
        debug->active_launch = debug->launches->len - 1;
    }
    else
        debug_launch_clear (existing);
    *existing = form;
    debug->launches_in_project = in_project;
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

mc_ep_result_t
debug_configure (void *data, void *edit)
{
    return debug_configure_impl ((debugger_t *) data, edit, FALSE);
}

mc_ep_result_t
debug_new_configuration (void *data, void *edit)
{
    return debug_configure_impl ((debugger_t *) data, edit, TRUE);
}

mc_ep_result_t
debug_select_configuration (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    Listbox *selector;
    debug_launch_t *chosen;
    guint i;

    if (!debug_require_project (debug, edit))
        return MC_EPR_FAILED;
    if (debug->launches->len == 0)
        return debug_new_configuration (data, edit);
    selector = listbox_window_new (MIN ((int) debug->launches->len, 16), 70,
                                   _ ("Select debug configuration"), NULL);
    for (i = 0; i < debug->launches->len; i++)
    {
        debug_launch_t *launch = g_ptr_array_index (debug->launches, i);
        char *label = g_strdup_printf ("%s  %s", launch->name, launch->executable);

        LISTBOX_APPEND_TEXT (selector, 0, label, launch, FALSE);
        g_free (label);
    }
    chosen = listbox_run_with_data (selector, NULL);
    if (chosen == NULL)
        return MC_EPR_FAILED;
    for (i = 0; i < debug->launches->len; i++)
        if (g_ptr_array_index (debug->launches, i) == chosen)
        {
            debug->active_launch = i;
            debug_config_save (debug);
            debug_session_refresh (debug);
            return MC_EPR_OK;
        }
    return MC_EPR_FAILED;
}

mc_ep_result_t
debug_delete_configuration (void *data, void *edit)
{
    debugger_t *debug = (debugger_t *) data;
    debug_launch_t *launch = debug_active_launch (debug);

    (void) edit;
    if (launch == NULL)
        return MC_EPR_FAILED;
    if (query_dialog (_ ("Debug"), _ ("Delete the selected debug configuration?"), D_NORMAL, 2,
                      _ ("&Delete"), _ ("&Cancel"))
        != 0)
        return MC_EPR_FAILED;
    g_ptr_array_remove_index (debug->launches, debug->active_launch);
    debug->active_launch = 0;
    debug_config_save (debug);
    debug_session_refresh (debug);
    return MC_EPR_OK;
}

/* Whether a debug adapter runs the program of a configuration, not GDB/MI */
gboolean
debug_launch_is_dap (const debug_launch_t *launch)
{
    return g_strcmp0 (launch->backend, "dap") == 0;
}

/* The command of the debug adapter of a configuration, NULL when it is not there to run, and
   the user told */
char *
debug_adapter_check (debugger_t *debug, const debug_launch_t *launch)
{
    char **argv = NULL;
    char *found = NULL;
    GError *error = NULL;

#ifndef ENABLE_DAP
    (void) launch;
    (void) argv;
    (void) found;
    (void) error;
    debug_error (debug, _ ("This coole is built without the debug adapters: they need json-glib."));
    return NULL;
#else
    if (launch->adapter == NULL || *launch->adapter == '\0')
    {
        // an adapter that listens already needs no command
        if (launch->address != NULL && *launch->address != '\0')
            return g_strdup ("");
        debug_error (debug, _ ("Enter the command of the debug adapter."));
        return NULL;
    }
    if (!g_shell_parse_argv (launch->adapter, NULL, &argv, &error))
    {
        debug_error (debug, error->message);
        g_error_free (error);
        return NULL;
    }
    found = strchr (argv[0], '/') != NULL
        ? (g_file_test (argv[0], G_FILE_TEST_IS_EXECUTABLE) ? g_strdup (argv[0]) : NULL)
        : g_find_program_in_path (argv[0]);
    if (found == NULL)
    {
        const char *hint = debug_adapter_hint (launch->adapter);
        char *text_value = hint != NULL
            ? g_strdup_printf (_ ("The debug adapter %s was not found.\n%s"), argv[0], hint)
            : g_strdup_printf (_ ("The debug adapter %s was not found."), argv[0]);

        debug_error (debug, text_value);
        g_free (text_value);
        g_strfreev (argv);
        return NULL;
    }
    g_free (found);
    g_strfreev (argv);
    return g_strdup (launch->adapter);
#endif
}
