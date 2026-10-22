/*
   The language servers the user has, from lsp.json, and the root of the files they serve.

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
#include <unistd.h>

#include <glib/gstdio.h>

#include "lsp-config.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define LSP_CONFIG_ERROR g_quark_from_static_string ("lsp-config")

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
lsp_backend_free (gpointer data)
{
    lsp_backend_t *backend = (lsp_backend_t *) data;

    g_free (backend->id);
    g_strfreev (backend->command);
    g_hash_table_destroy (backend->extensions);
    g_strfreev (backend->root_markers);
    if (backend->environment != NULL)
        json_object_unref (backend->environment);
    if (backend->initialization_options != NULL)
        json_node_unref (backend->initialization_options);
    if (backend->settings != NULL)
        json_node_unref (backend->settings);
    g_free (backend->startup);
    if (backend->requests != NULL)
        json_node_unref (backend->requests);
    g_free (backend);
}

/* --------------------------------------------------------------------------------------------- */

/* An array of strings, NULL with @error when it is anything else */
static char **
lsp_config_strings (JsonNode *node, const char *where, GError **error)
{
    JsonArray *array;
    GPtrArray *out;
    guint i;

    if (node == NULL || !JSON_NODE_HOLDS_ARRAY (node))
    {
        g_set_error (error, LSP_CONFIG_ERROR, 0, "%s: not an array of strings", where);
        return NULL;
    }
    array = json_node_get_array (node);
    out = g_ptr_array_new_with_free_func (g_free);
    for (i = 0; i < json_array_get_length (array); i++)
    {
        JsonNode *element = json_array_get_element (array, i);

        if (!JSON_NODE_HOLDS_VALUE (element) || json_node_get_value_type (element) != G_TYPE_STRING)
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s[%u]: not a string", where, i);
            g_ptr_array_free (out, TRUE);
            return NULL;
        }
        g_ptr_array_add (out, g_strdup (json_node_get_string (element)));
    }
    g_ptr_array_set_free_func (out, NULL);
    g_ptr_array_add (out, NULL);
    return (char **) g_ptr_array_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_config_when (JsonNode *node, const char *where, lsp_when_t *when, GError **error)
{
    if (lsp_when_parse (node, when))
        return TRUE;
    g_set_error (error, LSP_CONFIG_ERROR, 0, "%s: not \"never\", \"ide\" or \"always\"", where);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static lsp_backend_t *
lsp_config_backend_parse (JsonNode *node, guint index, GError **error)
{
    lsp_backend_t *backend;
    JsonObject *o;
    JsonNode *member;
    char *where;

    where = g_strdup_printf ("backends[%u]", index);
    if (!JSON_NODE_HOLDS_OBJECT (node))
    {
        g_set_error (error, LSP_CONFIG_ERROR, 0, "%s: not an object", where);
        g_free (where);
        return NULL;
    }
    o = json_node_get_object (node);
    backend = g_new0 (lsp_backend_t, 1);
    backend->extensions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    backend->single_file = TRUE;
    backend->enabled = TRUE;
    backend->autostart = LSP_WHEN_UNSET;

    member = json_object_get_member (o, "id");
    if (member == NULL || !JSON_NODE_HOLDS_VALUE (member)
        || json_node_get_value_type (member) != G_TYPE_STRING
        || *json_node_get_string (member) == '\0')
    {
        g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.id: not a name", where);
        goto fail;
    }
    backend->id = g_strdup (json_node_get_string (member));
    g_free (where);
    where = g_strdup_printf ("backend %s", backend->id);

    backend->command = lsp_config_strings (json_object_get_member (o, "command"), where, error);
    if (backend->command == NULL)
        goto fail;
    if (backend->command[0] == NULL || *backend->command[0] == '\0')
    {
        g_set_error (error, LSP_CONFIG_ERROR, 0, "%s: an empty command", where);
        goto fail;
    }

    member = json_object_get_member (o, "extensions");
    if (member != NULL)
    {
        JsonObject *extensions;
        GList *names, *l;

        if (!JSON_NODE_HOLDS_OBJECT (member))
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.extensions: not an object", where);
            goto fail;
        }
        extensions = json_node_get_object (member);
        names = json_object_get_members (extensions);
        for (l = names; l != NULL; l = l->next)
        {
            JsonNode *language = json_object_get_member (extensions, l->data);

            if (!JSON_NODE_HOLDS_VALUE (language)
                || json_node_get_value_type (language) != G_TYPE_STRING)
            {
                g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.extensions.%s: not a language id",
                             where, (const char *) l->data);
                g_list_free (names);
                goto fail;
            }
            g_hash_table_insert (backend->extensions, g_strdup (l->data),
                                 g_strdup (json_node_get_string (language)));
        }
        g_list_free (names);
    }

    member = json_object_get_member (o, "root_markers");
    if (member != NULL)
    {
        backend->root_markers = lsp_config_strings (member, where, error);
        if (backend->root_markers == NULL)
            goto fail;
    }

    member = json_object_get_member (o, "enabled");
    if (member != NULL)
    {
        if (!JSON_NODE_HOLDS_VALUE (member) || json_node_get_value_type (member) != G_TYPE_BOOLEAN)
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.enabled: not true or false", where);
            goto fail;
        }
        backend->enabled = json_node_get_boolean (member);
    }

    member = json_object_get_member (o, "single_file");
    if (member != NULL)
    {
        if (!JSON_NODE_HOLDS_VALUE (member) || json_node_get_value_type (member) != G_TYPE_BOOLEAN)
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.single_file: not true or false", where);
            goto fail;
        }
        backend->single_file = json_node_get_boolean (member);
    }

    member = json_object_get_member (o, "autostart");
    if (member != NULL && !lsp_config_when (member, where, &backend->autostart, error))
        goto fail;

    member = json_object_get_member (o, "environment");
    if (member != NULL && !JSON_NODE_HOLDS_NULL (member))
    {
        if (!JSON_NODE_HOLDS_OBJECT (member))
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.environment: not an object", where);
            goto fail;
        }
        backend->environment = json_object_ref (json_node_get_object (member));
    }

    member = json_object_get_member (o, "initialization_options");
    if (member != NULL && !JSON_NODE_HOLDS_NULL (member))
        backend->initialization_options = json_node_copy (member);
    member = json_object_get_member (o, "settings");
    if (member != NULL && !JSON_NODE_HOLDS_NULL (member))
        backend->settings = json_node_copy (member);

    member = json_object_get_member (o, "startup");
    if (member != NULL && !JSON_NODE_HOLDS_NULL (member))
    {
        if (!JSON_NODE_HOLDS_VALUE (member) || json_node_get_value_type (member) != G_TYPE_STRING
            || *json_node_get_string (member) == '\0')
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.startup: not the name of an option",
                         where);
            goto fail;
        }
        backend->startup = g_strdup (json_node_get_string (member));
    }

    member = json_object_get_member (o, "requests");
    if (member != NULL && !JSON_NODE_HOLDS_NULL (member))
    {
        JsonArray *requests;
        guint i;

        if (!JSON_NODE_HOLDS_ARRAY (member))
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.requests: not an array", where);
            goto fail;
        }
        requests = json_node_get_array (member);
        for (i = 0; i < json_array_get_length (requests); i++)
        {
            JsonNode *request = json_array_get_element (requests, i);
            JsonNode *method = JSON_NODE_HOLDS_OBJECT (request)
                ? json_object_get_member (json_node_get_object (request), "method")
                : NULL;

            if (method == NULL || !JSON_NODE_HOLDS_VALUE (method)
                || json_node_get_value_type (method) != G_TYPE_STRING)
            {
                g_set_error (error, LSP_CONFIG_ERROR, 0, "%s.requests[%u]: no method", where, i);
                goto fail;
            }
        }
        backend->requests = json_node_copy (member);
    }

    g_free (where);
    return backend;

fail:
    g_free (where);
    lsp_backend_free (backend);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static lsp_config_t *
lsp_config_new (void)
{
    lsp_config_t *config = g_new0 (lsp_config_t, 1);

    config->autostart = LSP_WHEN_IDE;
    config->completion_on_trigger = LSP_WHEN_IDE;
    // a list at each call is in the way, unless the user wants it
    config->completion_on_arguments = LSP_WHEN_NEVER;
    config->backends = g_ptr_array_new_with_free_func (lsp_backend_free);
    return config;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the directory @dir has an entry @name, a pattern of one name ("*.csproj") too */
static gboolean
lsp_dir_has (const char *dir, const char *name)
{
    gboolean found = FALSE;

    if (strpbrk (name, "*?[") == NULL)
    {
        char *path = g_build_filename (dir, name, (char *) NULL);

        found = g_file_test (path, G_FILE_TEST_EXISTS);
        g_free (path);
    }
    else
    {
        GDir *d = g_dir_open (dir, 0, NULL);
        const char *entry;

        while (d != NULL && !found && (entry = g_dir_read_name (d)) != NULL)
            found = g_pattern_match_simple (name, entry);
        if (d != NULL)
            g_dir_close (d);
    }
    return found;
}

/* --------------------------------------------------------------------------------------------- */

/* The nearest directory from @dir up that has one of @names, NULL when none has */
static char *
lsp_find_up (const char *dir, const char *const *names)
{
    char *d = g_strdup (dir);

    if (names == NULL)
    {
        g_free (d);
        return NULL;
    }
    while (TRUE)
    {
        char *parent;
        const char *const *name;

        for (name = names; *name != NULL; name++)
            if (**name != '\0' && strchr (*name, '/') == NULL && lsp_dir_has (d, *name))
                return d;
        parent = g_path_get_dirname (d);
        if (strcmp (parent, d) == 0)
        {
            g_free (parent);
            g_free (d);
            return NULL;
        }
        g_free (d);
        d = parent;
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_when_parse (JsonNode *node, lsp_when_t *when)
{
    const char *value = NULL;

    if (node == NULL || !JSON_NODE_HOLDS_VALUE (node))
        return FALSE;
    if (json_node_get_value_type (node) == G_TYPE_BOOLEAN)
    {
        *when = json_node_get_boolean (node) ? LSP_WHEN_ALWAYS : LSP_WHEN_NEVER;
        return TRUE;
    }
    if (json_node_get_value_type (node) == G_TYPE_STRING)
        value = json_node_get_string (node);
    if (g_strcmp0 (value, "never") == 0)
        *when = LSP_WHEN_NEVER;
    else if (g_strcmp0 (value, "ide") == 0)
        *when = LSP_WHEN_IDE;
    else if (g_strcmp0 (value, "always") == 0)
        *when = LSP_WHEN_ALWAYS;
    else
        return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

const char *
lsp_when_name (lsp_when_t when)
{
    switch (when)
    {
    case LSP_WHEN_NEVER:
        return "never";
    case LSP_WHEN_ALWAYS:
        return "always";
    default:
        return "ide";
    }
}

/* --------------------------------------------------------------------------------------------- */

lsp_config_t *
lsp_config_parse (const char *json, gssize len, GError **error)
{
    JsonParser *parser = json_parser_new ();
    lsp_config_t *config = NULL;
    JsonObject *o;
    JsonNode *root, *member;

    if (!json_parser_load_from_data (parser, json, len, error))
        goto done;
    root = json_parser_get_root (parser);
    if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    {
        g_set_error (error, LSP_CONFIG_ERROR, 0, "not an object");
        goto done;
    }
    o = json_node_get_object (root);
    config = lsp_config_new ();

    member = json_object_get_member (o, "autostart");
    if (member != NULL && !lsp_config_when (member, "autostart", &config->autostart, error))
        goto fail;
    config->has_autostart = member != NULL;

    member = json_object_get_member (o, "completion_on_trigger");
    if (member != NULL
        && !lsp_config_when (member, "completion_on_trigger", &config->completion_on_trigger,
                             error))
        goto fail;
    config->has_completion_on_trigger = member != NULL;

    member = json_object_get_member (o, "completion_on_arguments");
    if (member != NULL
        && !lsp_config_when (member, "completion_on_arguments", &config->completion_on_arguments,
                             error))
        goto fail;
    config->has_completion_on_arguments = member != NULL;

    member = json_object_get_member (o, "backends");
    if (member != NULL)
    {
        JsonArray *backends;
        guint i;

        if (!JSON_NODE_HOLDS_ARRAY (member))
        {
            g_set_error (error, LSP_CONFIG_ERROR, 0, "backends: not an array");
            goto fail;
        }
        backends = json_node_get_array (member);
        for (i = 0; i < json_array_get_length (backends); i++)
        {
            lsp_backend_t *backend =
                lsp_config_backend_parse (json_array_get_element (backends, i), i, error);

            if (backend == NULL)
                goto fail;
            if (lsp_config_backend (config, backend->id) != NULL)
            {
                g_set_error (error, LSP_CONFIG_ERROR, 0, "backend %s: twice", backend->id);
                lsp_backend_free (backend);
                goto fail;
            }
            g_ptr_array_add (config->backends, backend);
        }
    }
    goto done;

fail:
    lsp_config_free (config);
    config = NULL;
done:
    g_object_unref (parser);
    return config;
}

/* --------------------------------------------------------------------------------------------- */

lsp_config_t *
lsp_config_load (const char *system_file, const char *user_file, GString *errors)
{
    const char *files[2] = { system_file, user_file };
    lsp_config_t *parsed[2] = { NULL, NULL };
    lsp_config_t *config;
    int i;

    for (i = 0; i < 2; i++)
    {
        char *text = NULL;
        gsize len = 0;
        GError *error = NULL;

        if (files[i] == NULL || !g_file_test (files[i], G_FILE_TEST_EXISTS))
            continue;
        if (g_file_get_contents (files[i], &text, &len, &error))
            parsed[i] = lsp_config_parse (text, (gssize) len, &error);
        if (error != NULL && errors != NULL)
            g_string_append_printf (errors, "%s: %s\n", files[i], error->message);
        g_clear_error (&error);
        g_free (text);
    }

    if (parsed[1] == NULL)
        config = parsed[0] != NULL ? parsed[0] : lsp_config_new ();
    else
    {
        config = parsed[1];
        if (parsed[0] != NULL)
        {
            guint b;

            if (!config->has_autostart)
                config->autostart = parsed[0]->autostart;
            if (!config->has_completion_on_trigger)
                config->completion_on_trigger = parsed[0]->completion_on_trigger;
            if (!config->has_completion_on_arguments)
                config->completion_on_arguments = parsed[0]->completion_on_arguments;

            // the system's backends the user has not got, after the user's
            for (b = 0; b < parsed[0]->backends->len; b++)
            {
                lsp_backend_t *backend = g_ptr_array_index (parsed[0]->backends, b);

                if (lsp_config_backend (config, backend->id) == NULL)
                    g_ptr_array_add (config->backends,
                                     g_ptr_array_steal_index (parsed[0]->backends, b--));
            }
            lsp_config_free (parsed[0]);
        }
    }
    return config;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_config_free (lsp_config_t *config)
{
    if (config == NULL)
        return;
    g_ptr_array_free (config->backends, TRUE);
    g_free (config);
}

/* --------------------------------------------------------------------------------------------- */

const lsp_backend_t *
lsp_config_backend (const lsp_config_t *config, const char *id)
{
    guint i;

    for (i = 0; i < config->backends->len; i++)
    {
        const lsp_backend_t *backend = g_ptr_array_index (config->backends, i);

        if (g_strcmp0 (backend->id, id) == 0)
            return backend;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_backend_runnable (const lsp_backend_t *backend)
{
    char *program;

    if (g_path_is_absolute (backend->command[0]))
        return g_file_test (backend->command[0], G_FILE_TEST_IS_EXECUTABLE);
    program = g_find_program_in_path (backend->command[0]);
    g_free (program);
    return program != NULL;
}

/* --------------------------------------------------------------------------------------------- */

const lsp_backend_t *
lsp_config_backend_for (const lsp_config_t *config, const char *path, const char **language_id)
{
    const char *name;
    guint i;

    if (config == NULL || path == NULL)
        return NULL;
    name = strrchr (path, G_DIR_SEPARATOR);
    name = name != NULL ? name + 1 : path;
    for (i = 0; i < config->backends->len; i++)
    {
        const lsp_backend_t *backend = g_ptr_array_index (config->backends, i);
        const char *language = NULL;
        gsize longest = 0;
        GHashTableIter iter;
        gpointer key, value;

        g_hash_table_iter_init (&iter, backend->extensions);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            const gsize len = strlen (key);

            if (len > longest && g_str_has_suffix (name, key))
            {
                longest = len;
                language = value;
            }
        }
        if (language != NULL && backend->enabled && lsp_backend_runnable (backend))
        {
            if (language_id != NULL)
                *language_id = language;
            return backend;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

lsp_when_t
lsp_backend_autostart (const lsp_config_t *config, const lsp_backend_t *backend)
{
    return backend->autostart != LSP_WHEN_UNSET ? backend->autostart : config->autostart;
}

/* --------------------------------------------------------------------------------------------- */

char *
lsp_find_root (const lsp_backend_t *backend, const char *path, const char *project)
{
    static const char *const coole[] = { ".coole", NULL };
    static const char *const vcs[] = { ".git", ".hg", ".svn", NULL };
    char *dir, *root;

    if (path == NULL || !g_path_is_absolute (path))
        return NULL;
    dir = g_path_get_dirname (path);
    if (project != NULL && *project != '\0')
    {
        const gsize len = strlen (project);

        if (strcmp (dir, project) == 0
            || (strncmp (dir, project, len) == 0
                && (dir[len] == G_DIR_SEPARATOR || project[len - 1] == G_DIR_SEPARATOR)))
        {
            g_free (dir);
            return g_strdup (project);
        }
    }
    root = lsp_find_up (dir, coole);
    if (root == NULL)
        root = lsp_find_up (dir, (const char *const *) backend->root_markers);
    if (root == NULL)
        root = lsp_find_up (dir, vcs);
    if (root == NULL && backend->single_file)
        root = g_strdup (dir);
    g_free (dir);
    return root;
}

/* --------------------------------------------------------------------------------------------- */

JsonNode *
lsp_settings_lookup (JsonNode *settings, const char *section)
{
    JsonNode *node = settings;
    char **parts;
    int i;

    if (node == NULL || section == NULL || *section == '\0')
        return node;
    parts = g_strsplit (section, ".", -1);
    for (i = 0; parts[i] != NULL && node != NULL; i++)
    {
        JsonObject *o;

        if (!JSON_NODE_HOLDS_OBJECT (node))
            node = NULL;
        else
        {
            o = json_node_get_object (node);
            node =
                json_object_has_member (o, parts[i]) ? json_object_get_member (o, parts[i]) : NULL;
        }
    }
    g_strfreev (parts);
    return node;
}

/* --------------------------------------------------------------------------------------------- */

char **
lsp_command_expand (char *const *command, const char *build_dir)
{
    GPtrArray *out = g_ptr_array_new ();
    int i;

    for (i = 0; command[i] != NULL; i++)
    {
        if (strstr (command[i], "${build_dir}") == NULL)
            g_ptr_array_add (out, g_strdup (command[i]));
        else if (build_dir != NULL)
        {
            char **parts = g_strsplit (command[i], "${build_dir}", -1);

            g_ptr_array_add (out, g_strjoinv (build_dir, parts));
            g_strfreev (parts);
        }
    }
    g_ptr_array_add (out, NULL);
    return (char **) g_ptr_array_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

void
lsp_config_for_startup (lsp_config_t *config, gboolean (*started) (const char *option, void *data),
                        void *data)
{
    GPtrArray *first = g_ptr_array_new ();
    guint i;

    for (i = 0; i < config->backends->len; i++)
    {
        lsp_backend_t *backend = g_ptr_array_index (config->backends, i);

        if (backend->startup == NULL)
            continue;
        if (started (backend->startup, data))
            g_ptr_array_add (first, backend);
        else
            backend->enabled = FALSE;
    }
    // those of the option first, in their order; the array frees what it removes: taken out
    for (i = first->len; i > 0; i--)
    {
        guint at;

        if (g_ptr_array_find (config->backends, g_ptr_array_index (first, i - 1), &at))
            g_ptr_array_insert (config->backends, 0,
                                g_ptr_array_steal_index (config->backends, at));
    }
    g_ptr_array_free (first, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

/* @text with its variables put in */
static char *
lsp_config_expand_string (const char *text, const char *command_dir)
{
    GString *out = g_string_new (NULL);
    const char *p = text;

    while (*p != '\0')
    {
        const char *end;

        if (p[0] != '$' || p[1] != '{' || (end = strchr (p + 2, '}')) == NULL)
        {
            g_string_append_c (out, *p++);
            continue;
        }
        if (g_str_has_prefix (p + 2, "env:"))
        {
            char *name = g_strndup (p + 6, (gsize) (end - p - 6));
            const char *value = g_getenv (name);

            g_string_append (out, value != NULL ? value : "");
            g_free (name);
        }
        else if (end - p - 2 == (long) strlen ("command_dir")
                 && strncmp (p + 2, "command_dir", strlen ("command_dir")) == 0)
            g_string_append (out, command_dir != NULL ? command_dir : "");
        else
            // no variable of ours: as it is
            g_string_append_len (out, p, end + 1 - p);
        p = end + 1;
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

JsonNode *
lsp_config_expand (JsonNode *node, const char *command_dir)
{
    JsonNode *copy;

    if (node == NULL)
        return NULL;
    if (JSON_NODE_HOLDS_OBJECT (node))
    {
        JsonObject *from = json_node_get_object (node), *to = json_object_new ();
        GList *names = json_object_get_members (from), *l;

        for (l = names; l != NULL; l = l->next)
            json_object_set_member (
                to, l->data,
                lsp_config_expand (json_object_get_member (from, l->data), command_dir));
        g_list_free (names);
        copy = json_node_new (JSON_NODE_OBJECT);
        json_node_take_object (copy, to);
        return copy;
    }
    if (JSON_NODE_HOLDS_ARRAY (node))
    {
        JsonArray *from = json_node_get_array (node), *to = json_array_new ();
        guint i;

        for (i = 0; i < json_array_get_length (from); i++)
            json_array_add_element (
                to, lsp_config_expand (json_array_get_element (from, i), command_dir));
        copy = json_node_new (JSON_NODE_ARRAY);
        json_node_take_array (copy, to);
        return copy;
    }
    if (JSON_NODE_HOLDS_VALUE (node) && json_node_get_value_type (node) == G_TYPE_STRING)
    {
        char *text = lsp_config_expand_string (json_node_get_string (node), command_dir);

        copy = json_node_new (JSON_NODE_VALUE);
        json_node_set_string (copy, text);
        g_free (text);
        return copy;
    }
    return json_node_copy (node);
}

/* --------------------------------------------------------------------------------------------- */
