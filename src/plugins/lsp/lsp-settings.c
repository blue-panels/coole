/*
   lsp.json as the settings dialog of the language servers changes it.

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

#include <glib/gstdio.h>

#include "lsp-settings.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

struct lsp_settings_t
{
    char *user_file;
    JsonObject *system;  // the system's file, empty when it has none
    JsonObject *user;    // the user's, empty when there is none
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The object of a file; an empty one when it is not there, NULL with @error when it is broken */
static JsonObject *
lsp_settings_read (const char *file, GError **error)
{
    char *text = NULL;
    gsize len = 0;
    lsp_config_t *config;
    JsonNode *root;
    JsonObject *object;

    if (file == NULL || !g_file_test (file, G_FILE_TEST_EXISTS))
        return json_object_new ();
    if (!g_file_get_contents (file, &text, &len, error))
        return NULL;
    // what lsp.json is: what the configuration takes
    config = lsp_config_parse (text, (gssize) len, error);
    if (config == NULL)
    {
        g_prefix_error (error, "%s: ", file);
        g_free (text);
        return NULL;
    }
    lsp_config_free (config);
    root = json_from_string (text, NULL);
    g_free (text);
    object = json_object_ref (json_node_get_object (root));
    json_node_unref (root);
    return object;
}

/* --------------------------------------------------------------------------------------------- */

static JsonArray *
lsp_settings_backends_of (JsonObject *root)
{
    JsonNode *node = json_object_has_member (root, "backends")
        ? json_object_get_member (root, "backends")
        : NULL;

    return node != NULL && JSON_NODE_HOLDS_ARRAY (node) ? json_node_get_array (node) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The index of the backend @id in @root, -1 when it has none */
static int
lsp_settings_find (JsonObject *root, const char *id)
{
    JsonArray *backends = lsp_settings_backends_of (root);
    guint i;

    for (i = 0; backends != NULL && i < json_array_get_length (backends); i++)
    {
        JsonObject *backend = json_array_get_object_element (backends, i);

        if (backend != NULL && json_object_has_member (backend, "id")
            && g_strcmp0 (json_object_get_string_member (backend, "id"), id) == 0)
            return (int) i;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static JsonObject *
lsp_settings_get (JsonObject *root, const char *id)
{
    const int i = lsp_settings_find (root, id);

    return i < 0 ? NULL
                 : json_array_get_object_element (lsp_settings_backends_of (root), (guint) i);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
lsp_settings_equal (JsonObject *a, JsonObject *b)
{
    JsonNode *x = json_node_init_object (json_node_alloc (), a);
    JsonNode *y = json_node_init_object (json_node_alloc (), b);
    const gboolean equal = json_node_equal (x, y);

    json_node_unref (x);
    json_node_unref (y);
    return equal;
}

/* --------------------------------------------------------------------------------------------- */

/* What a file that does not say @key has, as lsp-config.c has it */
static lsp_when_t
lsp_settings_default (const char *key)
{
    return strcmp (key, "completion_on_arguments") == 0 ? LSP_WHEN_NEVER : LSP_WHEN_IDE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

lsp_settings_t *
lsp_settings_load (const char *system_file, const char *user_file, GError **error)
{
    lsp_settings_t *settings;
    JsonObject *user = lsp_settings_read (user_file, error);

    if (user == NULL)
        return NULL;
    settings = g_new0 (lsp_settings_t, 1);
    settings->user_file = g_strdup (user_file);
    settings->user = user;
    settings->system = lsp_settings_read (system_file, NULL);
    if (settings->system == NULL)
        settings->system = json_object_new ();
    return settings;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_settings_free (lsp_settings_t *settings)
{
    if (settings == NULL)
        return;
    json_object_unref (settings->system);
    json_object_unref (settings->user);
    g_free (settings->user_file);
    g_free (settings);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_settings_save (lsp_settings_t *settings, GError **error)
{
    JsonGenerator *generator = json_generator_new ();
    JsonNode *root = json_node_init_object (json_node_alloc (), settings->user);
    JsonArray *backends = lsp_settings_backends_of (settings->user);
    char *dir = g_path_get_dirname (settings->user_file);
    char *text;
    gboolean ok;

    if (backends != NULL && json_array_get_length (backends) == 0)
        json_object_remove_member (settings->user, "backends");
    json_generator_set_pretty (generator, TRUE);
    json_generator_set_indent (generator, 2);
    json_generator_set_root (generator, root);
    text = json_generator_to_data (generator, NULL);
    (void) g_mkdir_with_parents (dir, 0700);
    ok = g_file_set_contents (settings->user_file, text, -1, error);
    g_free (text);
    g_free (dir);
    json_node_unref (root);
    g_object_unref (generator);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

lsp_when_t
lsp_settings_when (const lsp_settings_t *settings, const char *key)
{
    lsp_when_t when = lsp_settings_default (key);

    if (json_object_has_member (settings->user, key)
        && lsp_when_parse (json_object_get_member (settings->user, key), &when))
        return when;
    if (json_object_has_member (settings->system, key))
        (void) lsp_when_parse (json_object_get_member (settings->system, key), &when);
    return when;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_settings_set_when (lsp_settings_t *settings, const char *key, lsp_when_t when)
{
    lsp_when_t system = lsp_settings_default (key);

    if (json_object_has_member (settings->system, key))
        (void) lsp_when_parse (json_object_get_member (settings->system, key), &system);
    if (when == system)
        json_object_remove_member (settings->user, key);
    else
        json_object_set_string_member (settings->user, key, lsp_when_name (when));
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
lsp_settings_ids (const lsp_settings_t *settings)
{
    GPtrArray *ids = g_ptr_array_new_with_free_func (g_free);
    JsonObject *roots[2] = { settings->user, settings->system };
    int r;

    for (r = 0; r < 2; r++)
    {
        JsonArray *backends = lsp_settings_backends_of (roots[r]);
        guint i, k;

        for (i = 0; backends != NULL && i < json_array_get_length (backends); i++)
        {
            JsonObject *backend = json_array_get_object_element (backends, i);
            const char *id;
            gboolean seen = FALSE;

            if (backend == NULL || !json_object_has_member (backend, "id"))
                continue;
            id = json_object_get_string_member (backend, "id");
            for (k = 0; k < ids->len && !seen; k++)
                seen = strcmp (g_ptr_array_index (ids, k), id) == 0;
            if (!seen)
                g_ptr_array_add (ids, g_strdup (id));
        }
    }
    return ids;
}

/* --------------------------------------------------------------------------------------------- */

JsonObject *
lsp_settings_backend (const lsp_settings_t *settings, const char *id)
{
    JsonObject *backend = lsp_settings_get (settings->user, id);

    return backend != NULL ? backend : lsp_settings_get (settings->system, id);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_settings_in_system (const lsp_settings_t *settings, const char *id)
{
    return lsp_settings_find (settings->system, id) >= 0;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
lsp_settings_changed (const lsp_settings_t *settings, const char *id)
{
    return lsp_settings_in_system (settings, id) && lsp_settings_find (settings->user, id) >= 0;
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_settings_put (lsp_settings_t *settings, JsonObject *backend)
{
    const char *id = json_object_get_string_member (backend, "id");
    JsonObject *system = lsp_settings_get (settings->system, id);
    JsonArray *backends;
    int i;

    if (system != NULL && lsp_settings_equal (system, backend))
    {
        lsp_settings_drop (settings, id);
        json_object_unref (backend);
        return;
    }
    backends = lsp_settings_backends_of (settings->user);
    if (backends == NULL)
    {
        backends = json_array_new ();
        json_object_set_array_member (settings->user, "backends", backends);
    }
    i = lsp_settings_find (settings->user, id);
    if (i < 0)
        json_array_add_object_element (backends, backend);
    else
    {
        // in its place: the order of the user's backends is the order they are chosen in
        JsonArray *now = json_array_sized_new (json_array_get_length (backends));
        guint k;

        for (k = 0; k < json_array_get_length (backends); k++)
            if ((int) k == i)
                json_array_add_object_element (now, backend);
            else
                json_array_add_element (now, json_node_copy (json_array_get_element (backends, k)));
        json_object_set_array_member (settings->user, "backends", now);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
lsp_settings_drop (lsp_settings_t *settings, const char *id)
{
    const int i = lsp_settings_find (settings->user, id);

    if (i >= 0)
        json_array_remove_element (lsp_settings_backends_of (settings->user), (guint) i);
}

/* --------------------------------------------------------------------------------------------- */
