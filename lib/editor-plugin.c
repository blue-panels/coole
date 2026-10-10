/*
   Editor plugins: the registry and the loader of the dynamic ones.

   Copyright (C) 2025-2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2025-2026.

   This file is part of coole,
   a text editor based on GNU Midnight Commander.

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

/** \file editor-plugin.c
 *  \brief Source: editor plugins: the registry and the loader of the dynamic ones
 *
 *  Scans the directories of COOLE_PLUGINS_DIR, then MC_PLUGINS_DIR and
 *  ~/.local/lib/coole/plugins for shared objects exporting
 *  MC_EDITOR_PLUGIN_ENTRY, loads them, and registers the returned
 *  mc_editor_plugin_t descriptor via mc_editor_plugin_add(); a plugin of a
 *  name already registered is not loaded again.
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/fileloc.h"
#include "lib/editor-plugin.h"
#include "lib/plugin-prefs.h"

#ifdef HAVE_GMODULE
#include <gmodule.h>
#endif

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static GSList *editor_plugin_registry = NULL;

#ifdef HAVE_GMODULE
static gboolean editor_plugins_loaded = FALSE;
static GPtrArray *editor_plugin_modules = NULL;
#endif

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

#ifdef HAVE_GMODULE
static gboolean
module_filename_has_native_suffix (const gchar *filename)
{
    if (filename == NULL)
        return FALSE;

    return g_str_has_suffix (filename, ".so") || g_str_has_suffix (filename, ".dylib")
        || g_str_has_suffix (filename, ".bundle") || g_str_has_suffix (filename, ".dll");
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_editor_plugins_load_from_dir (const gchar *plugins_dir)
{
    GDir *dir;
    const gchar *filename;

    dir = g_dir_open (plugins_dir, 0, NULL);
    if (dir == NULL)
        return;

    while ((filename = g_dir_read_name (dir)) != NULL)
    {
        GModule *module;
        mc_editor_plugin_register_fn register_fn;
        const mc_editor_plugin_t *plugin;
        gchar *path;

        if (!module_filename_has_native_suffix (filename))
            continue;

        path = g_build_filename (plugins_dir, filename, (char *) NULL);
        module = g_module_open (path, 0);
        if (module == NULL)
        {
            fprintf (stderr, "Editor plugin %s not loaded: %s\n", filename, g_module_error ());
            g_free (path);
            continue;
        }

        if (!g_module_symbol (module, MC_EDITOR_PLUGIN_ENTRY, (gpointer *) &register_fn))
        {
            fprintf (stderr, "Editor plugin %s: symbol %s not found\n", filename,
                     MC_EDITOR_PLUGIN_ENTRY);
            g_module_close (module);
            g_free (path);
            continue;
        }

        plugin = register_fn ();
        if (plugin == NULL || !mc_editor_plugin_add (plugin))
        {
            g_module_close (module);
            g_free (path);
            continue;
        }

        g_module_make_resident (module);
        g_ptr_array_add (editor_plugin_modules, module);
        g_free (path);
    }

    g_dir_close (dir);
}
#endif /* HAVE_GMODULE */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
mc_editor_plugin_add (const mc_editor_plugin_t *plugin)
{
    if (plugin == NULL)
        return FALSE;

    if (plugin->api_version != MC_EDITOR_PLUGIN_API_VERSION)
    {
        fprintf (stderr, "Editor plugin \"%s\": API version %d, expected %d\n",
                 plugin->name != NULL ? plugin->name : "(null)", plugin->api_version,
                 MC_EDITOR_PLUGIN_API_VERSION);
        return FALSE;
    }

    if (plugin->name == NULL || plugin->open == NULL || plugin->close == NULL)
    {
        fprintf (stderr, "Editor plugin \"%s\": missing required callbacks\n",
                 plugin->name != NULL ? plugin->name : "(null)");
        return FALSE;
    }

    /* User opted to disable this plugin via Manage Plugins. */
    if (mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_EDITOR, plugin->name))
        return FALSE;

    // The idempotent path used by Manage Plugins: must not write to stderr.
    if (mc_editor_plugin_find_by_name (plugin->name) != NULL)
        return FALSE;

    editor_plugin_registry = g_slist_append (editor_plugin_registry, (gpointer) plugin);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

const GSList *
mc_editor_plugin_list (void)
{
    return editor_plugin_registry;
}

/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
mc_editor_plugin_find_by_name (const char *name)
{
    const GSList *iter;

    if (name == NULL)
        return NULL;

    for (iter = editor_plugin_registry; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_editor_plugin_t *p = (const mc_editor_plugin_t *) iter->data;

        if (strcmp (p->name, name) == 0)
            return p;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_editor_plugins_load (void)
{
#ifdef HAVE_GMODULE
    gchar *user_dir;

    if (editor_plugins_loaded)
        return;

    editor_plugins_loaded = TRUE;
    editor_plugin_modules = g_ptr_array_new ();

    /* first the directories of COOLE_PLUGINS_DIR, separated by ':': the program run from its
       build directory takes the plugins built with it (meson devenv sets it), not those
       installed */
    if (g_getenv ("COOLE_PLUGINS_DIR") != NULL)
    {
        gchar **dirs = g_strsplit (g_getenv ("COOLE_PLUGINS_DIR"), G_SEARCHPATH_SEPARATOR_S, -1);
        gchar **d;

        for (d = dirs; *d != NULL; d++)
            if (**d != '\0')
                mc_editor_plugins_load_from_dir (*d);
        g_strfreev (dirs);
    }

    // load from system plugin directory
    mc_editor_plugins_load_from_dir (MC_PLUGINS_DIR);

    // load from user plugin directory (~/.local/lib/coole/plugins)
    user_dir = g_build_filename (g_get_home_dir (), ".local", "lib", MC_USERCONF_DIR, "plugins",
                                 (char *) NULL);
    mc_editor_plugins_load_from_dir (user_dir);
    g_free (user_dir);
#endif
}

/* --------------------------------------------------------------------------------------------- */

void
mc_editor_plugins_shutdown (void)
{
    g_slist_free (editor_plugin_registry);
    editor_plugin_registry = NULL;
}

/* --------------------------------------------------------------------------------------------- */
