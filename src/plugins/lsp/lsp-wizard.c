/*
   The settings dialog of the language servers: when they start, when they complete, and each
   server, written to the user's lsp.json.

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

#include <string.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/tty/tty.h"
#include "lib/widget.h"

#include "lsp-settings.h"
#include "lsp-wizard.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define LSP_HELP_FILE "lsp.md"

/* the buttons of the dialog of a server */
#define LSP_WIZARD_DROP (B_USER + 1)

/*** file scope type declarations ****************************************************************/

/* A server as the dialog has it */
typedef struct
{
    char *id;
    char *command;     // the arguments, quoted as a shell would take them
    char *extensions;  // ".c=c .h=c"
    char *markers;     // "compile_commands.json compile_flags.txt"
    gboolean enabled;
    gboolean single_file;
    int autostart;  // 0: as in the settings, then never, ide, always
} lsp_server_form_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* the settings the dialog is on: its buttons have no other way to them */
static lsp_settings_t *lsp_wizard_settings = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
lsp_form_free (lsp_server_form_t *form)
{
    g_free (form->id);
    g_free (form->command);
    g_free (form->extensions);
    g_free (form->markers);
}

/* --------------------------------------------------------------------------------------------- */

static JsonArray *
lsp_array (JsonObject *object, const char *name)
{
    JsonNode *node =
        json_object_has_member (object, name) ? json_object_get_member (object, name) : NULL;

    return node != NULL && JSON_NODE_HOLDS_ARRAY (node) ? json_node_get_array (node) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_bool (JsonObject *object, const char *name, gboolean fallback)
{
    JsonNode *node =
        json_object_has_member (object, name) ? json_object_get_member (object, name) : NULL;

    return node != NULL && JSON_NODE_HOLDS_VALUE (node)
            && json_node_get_value_type (node) == G_TYPE_BOOLEAN
        ? json_node_get_boolean (node)
        : fallback;
}

/* --------------------------------------------------------------------------------------------- */

/* The strings of an array, a space between them; with @shell, one that has a space or a quote is
   quoted as a shell takes it */
static char *
lsp_join (JsonArray *array, gboolean shell)
{
    GString *out = g_string_new (NULL);
    guint i;

    for (i = 0; array != NULL && i < json_array_get_length (array); i++)
    {
        JsonNode *node = json_array_get_element (array, i);
        const char *text;

        if (!JSON_NODE_HOLDS_VALUE (node) || json_node_get_value_type (node) != G_TYPE_STRING)
            continue;
        text = json_node_get_string (node);
        if (out->len > 0)
            g_string_append_c (out, ' ');
        if (shell && (*text == '\0' || strpbrk (text, " \t'\"\\$`") != NULL)
            && strstr (text, "${build_dir}") == NULL)
        {
            char *quoted = g_shell_quote (text);

            g_string_append (out, quoted);
            g_free (quoted);
        }
        else
            g_string_append (out, text);
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The program of a backend: whether it can be run */
static gboolean
lsp_installed (JsonObject *backend)
{
    JsonArray *command = lsp_array (backend, "command");
    const char *program;
    char *found;

    if (command == NULL || json_array_get_length (command) == 0)
        return FALSE;
    program = json_array_get_string_element (command, 0);
    if (program == NULL)
        return FALSE;
    if (g_path_is_absolute (program))
        return g_file_test (program, G_FILE_TEST_IS_EXECUTABLE);
    found = g_find_program_in_path (program);
    g_free (found);
    return found != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
lsp_form_of (lsp_server_form_t *form, JsonObject *backend)
{
    JsonNode *extensions = json_object_has_member (backend, "extensions")
        ? json_object_get_member (backend, "extensions")
        : NULL;
    lsp_when_t when;
    GString *out = g_string_new (NULL);

    form->id = g_strdup (json_object_get_string_member (backend, "id"));
    form->command = lsp_join (lsp_array (backend, "command"), TRUE);
    if (extensions != NULL && JSON_NODE_HOLDS_OBJECT (extensions))
    {
        JsonObject *o = json_node_get_object (extensions);
        GList *names = json_object_get_members (o), *l;

        for (l = names; l != NULL; l = l->next)
        {
            const char *language = json_object_get_string_member (o, l->data);

            if (language != NULL)
                g_string_append_printf (out, "%s%s=%s", out->len > 0 ? " " : "",
                                        (const char *) l->data, language);
        }
        g_list_free (names);
    }
    form->extensions = g_string_free (out, FALSE);
    form->markers = lsp_join (lsp_array (backend, "root_markers"), FALSE);
    form->enabled = lsp_bool (backend, "enabled", TRUE);
    form->single_file = lsp_bool (backend, "single_file", TRUE);
    form->autostart = json_object_has_member (backend, "autostart")
            && lsp_when_parse (json_object_get_member (backend, "autostart"), &when)
        ? (int) when + 1
        : 0;
}

/* --------------------------------------------------------------------------------------------- */

/* A member that is the default goes, unless the backend had it already */
static void
lsp_set_bool (JsonObject *backend, const char *name, gboolean value, gboolean fallback,
              gboolean had)
{
    if (value != fallback || had)
        json_object_set_boolean_member (backend, name, value);
    else
        json_object_remove_member (backend, name);
}

/* --------------------------------------------------------------------------------------------- */

/* The backend of a form: what the form has not, the settings, the environment, kept from @old.
   NULL with @why when the form is wrong. */
static JsonObject *
lsp_backend_of (const lsp_server_form_t *form, JsonObject *old, char **why)
{
    JsonObject *backend;
    JsonArray *array;
    JsonObject *extensions;
    char **argv = NULL, **words;
    GError *error = NULL;
    int i;

    if (form->id == NULL || *g_strstrip (form->id) == '\0')
    {
        *why = g_strdup (_ ("The server has no id."));
        return NULL;
    }
    if (!g_shell_parse_argv (form->command, NULL, &argv, &error))
    {
        *why = g_strdup_printf (_ ("The command: %s"), error->message);
        g_error_free (error);
        return NULL;
    }

    if (old == NULL)
        backend = json_object_new ();
    else
    {
        /* a copy, the settings keeping theirs until the form is taken; json_node_copy () would
           share the object */
        JsonNode *node = json_node_init_object (json_node_alloc (), old);
        char *text = json_to_string (node, FALSE);
        JsonNode *copy = json_from_string (text, NULL);

        backend = json_object_ref (json_node_get_object (copy));
        json_node_unref (copy);
        json_node_unref (node);
        g_free (text);
    }
    json_object_set_string_member (backend, "id", form->id);
    array = json_array_new ();
    for (i = 0; argv[i] != NULL; i++)
        json_array_add_string_element (array, argv[i]);
    json_object_set_array_member (backend, "command", array);
    g_strfreev (argv);

    extensions = json_object_new ();
    words = g_strsplit_set (form->extensions != NULL ? form->extensions : "", " \t,", -1);
    for (i = 0; words[i] != NULL; i++)
    {
        char *eq = strchr (words[i], '=');

        if (*words[i] == '\0')
            continue;
        if (eq == NULL || eq == words[i] || eq[1] == '\0')
        {
            *why = g_strdup_printf (_ ("\"%s\" is no end of a file name with its language, as "
                                       "\".c=c\"."),
                                    words[i]);
            g_strfreev (words);
            json_object_unref (extensions);
            json_object_unref (backend);
            return NULL;
        }
        *eq = '\0';
        json_object_set_string_member (extensions, words[i], eq + 1);
    }
    g_strfreev (words);
    json_object_set_object_member (backend, "extensions", extensions);

    array = json_array_new ();
    words = g_strsplit_set (form->markers != NULL ? form->markers : "", " \t,", -1);
    for (i = 0; words[i] != NULL; i++)
        if (*words[i] != '\0')
            json_array_add_string_element (array, words[i]);
    g_strfreev (words);
    if (json_array_get_length (array) > 0 || json_object_has_member (backend, "root_markers"))
        json_object_set_array_member (backend, "root_markers", array);
    else
        json_array_unref (array);

    lsp_set_bool (backend, "enabled", form->enabled, TRUE,
                  old != NULL && json_object_has_member (old, "enabled"));
    lsp_set_bool (backend, "single_file", form->single_file, TRUE,
                  old != NULL && json_object_has_member (old, "single_file"));
    if (form->autostart == 0)
        json_object_remove_member (backend, "autostart");
    else
        json_object_set_string_member (backend, "autostart",
                                       lsp_when_name ((lsp_when_t) (form->autostart - 1)));
    return backend;
}

/* --------------------------------------------------------------------------------------------- */

/* The dialog of a server, @id NULL for a new one; FALSE when nothing has changed */
static gboolean
lsp_wizard_server (lsp_settings_t *settings, const char *id)
{
    JsonObject *old = id != NULL ? lsp_settings_backend (settings, id) : NULL;
    const gboolean in_system = id != NULL && lsp_settings_in_system (settings, id);
    lsp_server_form_t form = { 0 };
    gboolean done = FALSE, changed = FALSE;

    if (old != NULL)
        lsp_form_of (&form, old);
    else
    {
        form.enabled = TRUE;
        form.single_file = TRUE;
    }

    while (!done)
    {
        const char *starts[] = {
            _ ("As in the settings"),
            _ ("&Never"),
            _ ("On a project of -&D"),
            _ ("&Always"),
        };
        char *new_id = NULL, *command = NULL, *extensions = NULL, *markers = NULL;
        // a server of the user is taken away, one of the system's set back as it has it
        const char *drop = in_system ? _ ("&Reset") : _ ("De&lete");
        char *title = id != NULL ? g_strdup_printf (_ ("Language server %s"), id)
                                 : g_strdup (_ ("New language server"));
        int answer;

        quick_widget_t widgets[] = {
            QUICK_LABELED_INPUT (_ ("Id:"), input_label_above, form.id != NULL ? form.id : "",
                                 "lsp-id", &new_id, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (_ ("Command (${build_dir}: the build directory):"),
                                 input_label_above, form.command != NULL ? form.command : "",
                                 "lsp-command", &command, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_COMMANDS),
            QUICK_LABELED_INPUT (_ ("Files (.end=language ...):"), input_label_above,
                                 form.extensions != NULL ? form.extensions : "", "lsp-extensions",
                                 &extensions, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (_ ("Root of a project, marked by:"), input_label_above,
                                 form.markers != NULL ? form.markers : "", "lsp-markers", &markers,
                                 NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_SEPARATOR (TRUE),
            QUICK_START_COLUMNS,
            QUICK_CHECKBOX (_ ("&Enabled"), &form.enabled, NULL),
            QUICK_CHECKBOX (_ ("Files in &no project too"), &form.single_file, NULL),
            QUICK_NEXT_COLUMN,
            QUICK_START_GROUPBOX (_ ("Start by itself")),
            QUICK_RADIO (4, starts, &form.autostart, NULL),
            QUICK_STOP_GROUPBOX,
            QUICK_STOP_COLUMNS,
            QUICK_START_BUTTONS (TRUE, TRUE),
            QUICK_BUTTON (_ ("&OK"), B_ENTER, NULL, NULL),
            QUICK_BUTTON (drop, LSP_WIZARD_DROP, NULL, NULL),
            QUICK_BUTTON (_ ("&Cancel"), B_CANCEL, NULL, NULL),
            QUICK_END,
        };
        quick_dialog_t dialog = {
            .rect = { -1, -1, 0, 64 },
            .title = title,
            .help = NULL,
            .help_file = LSP_HELP_FILE,
            .widgets = widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        answer = quick_dialog (&dialog);
        g_free (title);
        if (answer == B_ENTER)
        {
            lsp_server_form_t now = form;
            JsonObject *backend;
            char *why = NULL;

            now.id = new_id;
            now.command = command;
            now.extensions = extensions;
            now.markers = markers;
            backend = lsp_backend_of (&now, old, &why);
            if (backend != NULL && id == NULL
                && lsp_settings_backend (settings, json_object_get_string_member (backend, "id"))
                    != NULL)
                why = g_strdup_printf (_ ("There is a server %s already."),
                                       json_object_get_string_member (backend, "id"));
            else if (backend != NULL && id != NULL
                     && strcmp (json_object_get_string_member (backend, "id"), id) != 0)
                why = g_strdup (_ ("The id of a server stays: a new server has another one."));
            if (why != NULL)
            {
                message (D_ERROR, _ ("Language server"), "%s", why);
                g_free (why);
                if (backend != NULL)
                    json_object_unref (backend);
                // the dialog again, with what was typed
                g_free (form.id);
                g_free (form.command);
                g_free (form.extensions);
                g_free (form.markers);
                form.id = new_id;
                form.command = command;
                form.extensions = extensions;
                form.markers = markers;
                continue;
            }
            lsp_settings_put (settings, backend);
            changed = TRUE;
        }
        else if (answer == LSP_WIZARD_DROP && id != NULL)
        {
            lsp_settings_drop (settings, id);
            changed = TRUE;
        }
        g_free (new_id);
        g_free (command);
        g_free (extensions);
        g_free (markers);
        done = TRUE;
    }
    lsp_form_free (&form);
    return changed;
}

/* --------------------------------------------------------------------------------------------- */

/* The list of the servers; a server chosen is shown in its dialog, until Esc */
static int
lsp_wizard_servers (WButton *button, int action)
{
    lsp_settings_t *settings = lsp_wizard_settings;
    int current = 0;

    (void) button;
    (void) action;
    while (settings != NULL)
    {
        GPtrArray *ids = lsp_settings_ids (settings);
        GPtrArray *rows = g_ptr_array_new_with_free_func (g_free);
        int width = 40, chosen;
        Listbox *list;
        guint i;

        for (i = 0; i < ids->len; i++)
        {
            const char *id = g_ptr_array_index (ids, i);
            JsonObject *backend = lsp_settings_backend (settings, id);
            JsonNode *node = json_object_has_member (backend, "extensions")
                ? json_object_get_member (backend, "extensions")
                : NULL;
            GString *ends = g_string_new (NULL);
            const char *state;

            if (node != NULL && JSON_NODE_HOLDS_OBJECT (node))
            {
                GList *names = json_object_get_members (json_node_get_object (node)), *l;

                for (l = names; l != NULL; l = l->next)
                    g_string_append_printf (ends, "%s%s", ends->len > 0 ? " " : "",
                                            (const char *) l->data);
                g_list_free (names);
            }
            if (!lsp_bool (backend, "enabled", TRUE))
                state = _ ("off");
            else if (lsp_installed (backend))
                state = _ ("installed");
            else
                state = _ ("not installed");
            g_ptr_array_add (
                rows,
                g_strdup_printf ("%-16s %-14s %s%s", id, state, ends->str,
                                 lsp_settings_changed (settings, id) ? _ ("  (changed)") : ""));
            g_string_free (ends, TRUE);
        }
        g_ptr_array_add (rows, g_strdup (_ ("<New server...>")));
        for (i = 0; i < rows->len; i++)
            width = MAX (width, str_term_width1 (g_ptr_array_index (rows, i)) + 2);
        list = listbox_window_new (MIN ((int) rows->len, LINES - 8), MIN (width, COLS - 8),
                                   _ ("Language servers"), NULL);
        list->dlg->help_file = LSP_HELP_FILE;
        for (i = 0; i < rows->len; i++)
            LISTBOX_APPEND_TEXT (list, 0, g_ptr_array_index (rows, i), NULL, FALSE);
        listbox_set_current (list->list, MIN (current, (int) rows->len - 1));
        chosen = listbox_run (list);
        if (chosen >= 0)
        {
            current = chosen;
            (void) lsp_wizard_server (
                settings,
                chosen < (int) ids->len ? (const char *) g_ptr_array_index (ids, chosen) : NULL);
        }
        g_ptr_array_free (rows, TRUE);
        g_ptr_array_free (ids, TRUE);
        if (chosen < 0)
            break;
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_wizard_run (const char *system_file, const char *user_file)
{
    GError *error = NULL;
    lsp_settings_t *settings = lsp_settings_load (system_file, user_file, &error);
    int start, complete, arguments;
    gboolean saved = FALSE;

    if (settings == NULL)
    {
        message (D_ERROR, _ ("Language servers"), _ ("Fix the file first:\n%s"), error->message);
        g_error_free (error);
        return FALSE;
    }
    start = lsp_settings_when (settings, "autostart");
    complete = lsp_settings_when (settings, "completion_on_trigger");
    arguments = lsp_settings_when (settings, "completion_on_arguments");
    lsp_wizard_settings = settings;
    {
        const char *starts[] = {
            _ ("&Never: by LSP Start"),
            _ ("On a project of -&D"),
            _ ("&Always"),
        };
        const char *completes[] = {
            _ ("N&ever: by Ctrl-Space"),
            _ ("On a pro&ject of -D"),
            _ ("Al&ways"),
        };
        const char *arguments_when[] = {
            _ ("Ne&ver: by Ctrl-Space"),
            _ ("On a projec&t of -D"),
            _ ("Alwa&ys"),
        };
        quick_widget_t widgets[] = {
            QUICK_START_COLUMNS,
            QUICK_START_GROUPBOX (_ ("Start a server by itself")),
            QUICK_RADIO (3, starts, &start, NULL),
            QUICK_STOP_GROUPBOX,
            QUICK_NEXT_COLUMN,
            QUICK_START_GROUPBOX (_ ("Complete after a dot")),
            QUICK_RADIO (3, completes, &complete, NULL),
            QUICK_STOP_GROUPBOX,
            QUICK_START_GROUPBOX (_ ("Complete the arguments")),
            QUICK_RADIO (3, arguments_when, &arguments, NULL),
            QUICK_STOP_GROUPBOX,
            QUICK_STOP_COLUMNS,
            QUICK_START_BUTTONS (TRUE, TRUE),
            QUICK_BUTTON (_ ("&Servers..."), B_USER, lsp_wizard_servers, NULL),
            QUICK_BUTTON (_ ("&OK"), B_ENTER, NULL, NULL),
            QUICK_BUTTON (_ ("&Cancel"), B_CANCEL, NULL, NULL),
            QUICK_END,
        };
        quick_dialog_t dialog = {
            .rect = { -1, -1, 0, 0 },
            .title = _ ("Language servers"),
            .help = NULL,
            .help_file = LSP_HELP_FILE,
            .widgets = widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        if (quick_dialog (&dialog) == B_ENTER)
        {
            lsp_settings_set_when (settings, "autostart", (lsp_when_t) start);
            lsp_settings_set_when (settings, "completion_on_trigger", (lsp_when_t) complete);
            lsp_settings_set_when (settings, "completion_on_arguments", (lsp_when_t) arguments);
            saved = lsp_settings_save (settings, &error);
            if (!saved)
            {
                message (D_ERROR, _ ("Language servers"), _ ("Cannot write %s:\n%s"), user_file,
                         error->message);
                g_error_free (error);
            }
        }
    }
    lsp_wizard_settings = NULL;
    lsp_settings_free (settings);
    return saved;
}

/* --------------------------------------------------------------------------------------------- */
