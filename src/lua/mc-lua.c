/*
   Lua runtime extension for coole.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole, a text editor based on GNU Midnight Commander.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/** \file src/lua/mc-lua.c
 *  \brief Source: optional Lua implementation of the runtime extension ABI
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gmodule.h>
#include <glib/gstdio.h>

#include <lua.h>

#include "lib/glibcompat.h"
#include <lauxlib.h>
#include <lualib.h>

#include "lib/extension-runtime.h"
#include "lib/fileloc.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#ifndef MC_LUA_SYSTEM_SCRIPTS_DIR
#define MC_LUA_SYSTEM_SCRIPTS_DIR "/usr/share/coole/lua/scripts"
#endif

#ifndef MC_LUA_SYSTEM_MODULES_DIR
#define MC_LUA_SYSTEM_MODULES_DIR "/usr/share/coole/lua/lib"
#endif

#define MC_LUA_API_VERSION    1
#define MC_LUA_ID_MAX_LENGTH  64
#define MC_LUA_MANIFEST_FILE  "lua.ini"
#define MC_LUA_MANIFEST_GROUP "Lua"
/* The workspace of every package: the editor, whose scripts live in the
   "editor" directory of a script root. */
#define MC_LUA_EDITOR_WORKSPACE "editor"
#define MC_LUA_REGISTRY_PACKAGE "mc.lua.package"
#define MC_LUA_REGISTRY_MODULES "mc.lua.modules"
#define MC_LUA_HANDLE_METATABLE "mc.runtime.handle"
#define MC_LUA_HOST_API_UI_SIZE                                                                    \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_message)                                        \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_message))
#define MC_LUA_HOST_API_LOG_SIZE                                                                   \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, log)                                               \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->log))
#define MC_LUA_HOST_API_OBJECTS_SIZE                                                               \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, string_free)                                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->string_free))
#define MC_LUA_HOST_API_EDITOR_SELECTED_TEXT_SIZE                                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_selected_text)                              \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_selected_text))
#define MC_LUA_HOST_API_RUNTIME_ERROR_SIZE                                                         \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, runtime_error)                                     \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->runtime_error))
#define MC_LUA_HOST_API_DIALOG_SIZE                                                                \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, dialog_result_free)                                \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->dialog_result_free))
#define MC_LUA_HOST_API_EDITOR_INFO_SIZE                                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_info_free)                                  \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_info_free))
#define MC_LUA_HOST_API_EDITOR_SELECTION_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_selection_free)                             \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_selection_free))
#define MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_SIZE                                              \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_replace_selection)                          \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_replace_selection))
#define MC_LUA_HOST_API_EDITOR_REPLACE_SIZE                                                        \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_replace)                                    \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_replace))
#define MC_LUA_HOST_API_SCREEN_SIZE                                                                \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, screen_close)                                      \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->screen_close))
#define MC_LUA_HOST_API_PROCESS_SIZE                                                               \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, process_result_free)                               \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->process_result_free))
#define MC_LUA_HOST_API_INDICATORS_SIZE                                                            \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_indicators_clear_owner)                         \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_indicators_clear_owner))
#define MC_LUA_HOST_API_EDITOR_TAB_WIDTH_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_tab_width)                                  \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_tab_width))
#define MC_LUA_HOST_API_EDITOR_OVERWRITE_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_overwrite)                                  \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_overwrite))
#define MC_LUA_HOST_API_EDITOR_SET_OVERWRITE_SIZE                                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_set_overwrite)                              \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_set_overwrite))
#define MC_LUA_HOST_API_EDITOR_TEXT_SIZE                                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_text)                                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_text))
#define MC_LUA_HOST_API_EDITOR_EDIT_SIZE                                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_edit)                                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_edit))
#define MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_V2_SIZE                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_replace_selection_v2)                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_replace_selection_v2))
#define MC_LUA_HOST_API_UI_TEXT_WIDTH_SIZE                                                         \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_text_width)                                     \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_text_width))
#define MC_LUA_HOST_API_SYNTAX_SIZE                                                                \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, syntax_result_free)                                \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->syntax_result_free))
#define MC_LUA_HOST_API_TTY_SIZE                                                                   \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, tty_info)                                          \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->tty_info))
#define MC_LUA_HOST_API_SERVICES_SIZE                                                              \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, service_disconnect)                                \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->service_disconnect))
#define MC_LUA_SERVICE_MT          "mc.service"
#define MC_LUA_VARIANT_MAX_DEPTH   8
#define MC_LUA_DIALOG_MAX_CONTROLS 32
#define MC_LUA_DIALOG_MAX_OPTIONS  64
#define MC_LUA_DIALOG_MAX_DEPTH    8

/*** file scope type declarations ****************************************************************/

typedef enum
{
    MC_LUA_PACKAGE_SYSTEM,
    MC_LUA_PACKAGE_USER
} mc_lua_package_origin_t;

typedef enum
{
    MC_LUA_PROVIDES_NONE = 0,
    MC_LUA_PROVIDES_EVENTS = (1 << 0),
    MC_LUA_PROVIDES_MACROS = (1 << 1)
} mc_lua_provides_t;

typedef struct mc_lua_runtime mc_lua_runtime_t;
typedef struct mc_lua_package mc_lua_package_t;
typedef struct mc_lua_subscription mc_lua_subscription_t;
typedef struct mc_lua_macro mc_lua_macro_t;
typedef struct mc_lua_package_candidate mc_lua_package_candidate_t;
typedef struct mc_lua_package_info mc_lua_package_info_t;

typedef struct
{
    mc_runtime_handle_t handle;
} mc_lua_handle_t;

struct mc_lua_runtime
{
    const mc_runtime_host_api_v1_t *host;
    mc_runtime_plugin_context_t *context;
    GPtrArray *packages;
    GPtrArray *catalog;
    GPtrArray *macros;
    GHashTable *screens; /* id -> mc_lua_screen_t * (userdata owned by Lua) */
    guint64 next_screen_id;
    GHashTable *disabled_package_ids;
    char *user_scripts_dir;
    char *user_modules_dir;
    gboolean stopping;
    mc_runtime_subscription_t macro_subscription;
};

struct mc_lua_package
{
    mc_lua_runtime_t *runtime;
    char *id;
    char *workspace;
    char *root;
    char *entry_path;
    mc_lua_package_origin_t origin;
    guint provides;
    lua_State *lua;
    GHashTable *subscriptions;
    GPtrArray *macros;
    int settings_ref;              // the callback mc.settings() registered, or LUA_NOREF
    GPtrArray *service_listeners;  // mc_lua_service_listener_t
    gboolean closed;
    guint callback_depth;
    mc_runtime_event_id_t active_event;
};

typedef struct
{
    mc_runtime_dialog_t dialog;
    guint control_count;
} mc_lua_dialog_spec_t;

/* A service, by its name: the object mc.service() gives */
typedef struct
{
    char *name;
} mc_lua_service_t;

/* A callback of a script that listens to a service */
typedef struct
{
    mc_lua_package_t *package;
    guint id;
    int callback_ref;
    char *signal;  // the signal it waits for, "*" for any
} mc_lua_service_listener_t;

struct mc_lua_subscription
{
    mc_lua_package_t *package;
    guint64 token;
    guint64 *token_key;
    int callback_ref;
};

struct mc_lua_macro
{
    mc_lua_package_t *package;
    char *id;
    char *area;
    char *key;
    char *display_key;
    char *description;
    char *menu_path;
    char *menu_label;
    int menu_position;
    int priority;
    int action_ref;
    guint errors;
    gboolean disabled;
    gboolean listed;
};

struct mc_lua_package_candidate
{
    char *id;
    char *name;
    char *workspace;
    char *root;
    char *entry;
    mc_lua_package_origin_t origin;
    guint provides;
};

struct mc_lua_package_info
{
    char *id;
    char *name;
    char *workspace;
    char *root;
    mc_lua_package_origin_t origin;
    guint provides;
    gboolean disabled;
};

/*** forward declarations (file scope functions) *************************************************/

static void mc_lua_subscription_destroy (gpointer data);
static void mc_lua_service_listener_free (mc_lua_service_listener_t *listener);
static void mc_lua_macro_destroy (gpointer data);
static void mc_lua_package_destroy (mc_lua_package_t *package);
static mc_runtime_event_result_t mc_lua_event_callback (gpointer runtime_context,
                                                        const mc_runtime_event_snapshot_t *snapshot,
                                                        gpointer user_data);
static mc_runtime_event_result_t
mc_lua_macro_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                             gpointer user_data);
static int mc_lua_handle_index (lua_State *lua);
static int mc_lua_editor_current (lua_State *lua);
G_MODULE_EXPORT const mc_runtime_plugin_descriptor_v1_t *mc_runtime_plugin_register_v1 (void);

/*** file scope variables ************************************************************************/

static mc_lua_runtime_t *mc_lua_runtime_current = NULL;
static char mc_lua_module_loading_sentinel;

/*** file scope functions ************************************************************************/

static const char *
mc_lua_system_scripts_dir (void)
{
#ifdef HAVE_TESTS
    const char *directory = g_getenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR");

    if (directory != NULL && directory[0] != '\0')
        return directory;
#endif

    return MC_LUA_SYSTEM_SCRIPTS_DIR;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mc_lua_system_modules_dir (void)
{
#ifdef HAVE_TESTS
    const char *directory = g_getenv ("MC_LUA_TEST_SYSTEM_MODULES_DIR");

    if (directory != NULL && directory[0] != '\0')
        return directory;
#endif

    return MC_LUA_SYSTEM_MODULES_DIR;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mc_lua_catalog_origin_name (mc_lua_package_origin_t origin)
{
    return origin == MC_LUA_PACKAGE_USER ? "user" : "global";
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_host_has_capability (const mc_lua_package_t *package, guint64 capability, gsize minimum_size)
{
    return package != NULL && package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->struct_size >= minimum_size
        && (package->runtime->host->capability_flags & capability) != 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_log (const mc_lua_package_t *package, const char *level, const char *message)
{
    const char *id = package != NULL && package->id != NULL ? package->id : "?";

    if (mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_LOG, MC_LUA_HOST_API_LOG_SIZE)
        && package->runtime->host->log != NULL)
    {
        char *source = g_strdup_printf ("lua/%s", id);

        package->runtime->host->log (package->runtime->context, source, level, message);
        g_free (source);
        return;
    }

    fprintf (stderr, "lua/%s %s: %s\n", package != NULL && package->id != NULL ? package->id : "?",
             level, message != NULL ? message : "unknown error");
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_id_is_valid (const char *id)
{
    gsize length;
    gsize i;

    if (id == NULL)
        return FALSE;

    length = strlen (id);
    if (length == 0 || length > MC_LUA_ID_MAX_LENGTH)
        return FALSE;

    for (i = 0; i < length; i++)
        if (!g_ascii_isalnum (id[i]) && id[i] != '_' && id[i] != '.' && id[i] != '-')
            return FALSE;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_provides_parse (const char *value, guint *provides)
{
    gchar **items;
    gchar **item;
    guint parsed = MC_LUA_PROVIDES_NONE;

    if (provides == NULL)
        return FALSE;

    *provides = MC_LUA_PROVIDES_NONE;
    if (value == NULL)
        return TRUE;
    if (value[0] == '\0')
        return FALSE;

    items = g_strsplit_set (value, ",; \t\r\n", -1);
    for (item = items; item != NULL && *item != NULL; item++)
    {
        char *name = g_strstrip (*item);

        if (name[0] == '\0')
            continue;
        if (g_ascii_strcasecmp (name, "events") == 0)
            parsed |= MC_LUA_PROVIDES_EVENTS;
        else if (g_ascii_strcasecmp (name, "macros") == 0)
            parsed |= MC_LUA_PROVIDES_MACROS;
        else
        {
            g_strfreev (items);
            return FALSE;
        }
    }
    g_strfreev (items);

    if (parsed == MC_LUA_PROVIDES_NONE)
        return FALSE;

    *provides = parsed;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mc_lua_key_name_normalize (const char *key)
{
    char *normalized;
    gsize i;

    if (key == NULL)
        return NULL;

    normalized = g_ascii_strdown (key, -1);
    g_strstrip (normalized);
    if (normalized[0] == '\0' || strlen (normalized) > MC_LUA_ID_MAX_LENGTH)
    {
        g_free (normalized);
        return NULL;
    }

    for (i = 0; normalized[i] != '\0'; i++)
        if (!g_ascii_isprint (normalized[i]) || g_ascii_isspace (normalized[i]))
        {
            g_free (normalized);
            return NULL;
        }

    return normalized;
}

/* --------------------------------------------------------------------------------------------- */

/* Lua code runs with the editor's full privileges.  Reject paths that another account
   can replace, including symbolic links and writable intermediate directories. */
static gboolean
mc_lua_path_is_trusted (const char *path, gboolean expect_directory)
{
    struct stat st;

    if (path == NULL || g_lstat (path, &st) != 0)
        return FALSE;

    if (expect_directory ? !S_ISDIR (st.st_mode) : !S_ISREG (st.st_mode))
        return FALSE;

    return (st.st_uid == 0 || st.st_uid == geteuid ()) && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_file_is_trusted_under (const char *root, const char *path)
{
    char *canonical_root;
    char *canonical_path;
    char *directory;
    gboolean trusted = FALSE;
    gsize root_length;

    if (root == NULL || path == NULL)
        return FALSE;

    canonical_root = g_canonicalize_filename (root, NULL);
    canonical_path = g_canonicalize_filename (path, NULL);
    root_length = strlen (canonical_root);

    if (!g_str_has_prefix (canonical_path, canonical_root)
        || (root_length > 1 && canonical_path[root_length] != G_DIR_SEPARATOR)
        || !mc_lua_path_is_trusted (canonical_root, TRUE))
        goto done;

    directory = g_path_get_dirname (canonical_path);
    while (TRUE)
    {
        char *parent;

        if (!mc_lua_path_is_trusted (directory, TRUE))
            break;
        if (g_strcmp0 (directory, canonical_root) == 0)
        {
            trusted = mc_lua_path_is_trusted (canonical_path, FALSE);
            break;
        }

        parent = g_path_get_dirname (directory);
        if (g_strcmp0 (parent, directory) == 0)
        {
            g_free (parent);
            break;
        }
        g_free (directory);
        directory = parent;
    }
    g_free (directory);

done:
    g_free (canonical_path);
    g_free (canonical_root);
    return trusted;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_relative_lua_file_is_valid (const char *filename)
{
    const char *part;
    const char *next;

    if (filename == NULL || filename[0] == '\0' || g_path_is_absolute (filename)
        || !g_str_has_suffix (filename, ".lua") || strchr (filename, '\\') != NULL)
        return FALSE;

    part = filename;
    while (part != NULL)
    {
        next = strchr (part, G_DIR_SEPARATOR);
        if (next == part || (next == NULL && strcmp (part, ".") == 0)
            || (next == NULL && strcmp (part, "..") == 0)
            || (next != NULL && next - part == 1 && part[0] == '.')
            || (next != NULL && next - part == 2 && part[0] == '.' && part[1] == '.'))
            return FALSE;

        part = next != NULL ? next + 1 : NULL;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_module_name_is_valid (const char *name)
{
    gboolean beginning = TRUE;
    const char *cursor;

    if (name == NULL || name[0] == '\0')
        return FALSE;

    for (cursor = name; *cursor != '\0'; cursor++)
    {
        if (*cursor == '.')
        {
            if (beginning)
                return FALSE;
            beginning = TRUE;
        }
        else if (beginning)
        {
            if (!g_ascii_isalpha (*cursor) && *cursor != '_')
                return FALSE;
            beginning = FALSE;
        }
        else if (!g_ascii_isalnum (*cursor) && *cursor != '_')
            return FALSE;
    }

    return !beginning;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_id_t
mc_lua_event_id_from_name (const char *event_name)
{
    static const char *const event_names[MC_RUNTIME_EVENT_COUNT] = {
        NULL,
        MCEVENT_RUNTIME_STARTUP,
        MCEVENT_RUNTIME_SHUTDOWN,
        MCEVENT_RUNTIME_EDITOR_OPEN,
        MCEVENT_RUNTIME_EDITOR_SAVE,
        MCEVENT_RUNTIME_EDITOR_KEY,
    };
    mc_runtime_event_id_t event_id;

    if (event_name == NULL)
        return MC_RUNTIME_EVENT_INVALID;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
        if (strcmp (event_name, event_names[event_id]) == 0)
            return event_id;

    return MC_RUNTIME_EVENT_INVALID;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mc_lua_event_name (mc_runtime_event_id_t event_id)
{
    static const char *const event_names[MC_RUNTIME_EVENT_COUNT] = {
        NULL,
        MCEVENT_RUNTIME_STARTUP,
        MCEVENT_RUNTIME_SHUTDOWN,
        MCEVENT_RUNTIME_EDITOR_OPEN,
        MCEVENT_RUNTIME_EDITOR_SAVE,
        MCEVENT_RUNTIME_EDITOR_KEY,
    };

    if (event_id <= MC_RUNTIME_EVENT_INVALID || event_id >= MC_RUNTIME_EVENT_COUNT)
        return NULL;

    return event_names[event_id];
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_handle_is_valid (const mc_runtime_handle_t *handle)
{
    return handle != NULL && handle->kind > MC_RUNTIME_HANDLE_INVALID
        && handle->kind <= MC_RUNTIME_HANDLE_EDITOR && handle->id != 0 && handle->generation != 0;
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_package_t *
mc_lua_package_from_state (lua_State *lua)
{
    mc_lua_package_t *package;

    lua_getfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_PACKAGE);
    package = (mc_lua_package_t *) lua_touserdata (lua, -1);
    lua_pop (lua, 1);

    return package;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_return_error (lua_State *lua, const char *message)
{
    lua_pushnil (lua);
    lua_pushstring (lua, message != NULL ? message : "failed");
    return 2;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_package_has_active_context (const mc_lua_package_t *package)
{
    return package != NULL && package->callback_depth != 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_require_active_context (lua_State *lua, const mc_lua_package_t *package)
{
    if (mc_lua_package_has_active_context (package))
        return TRUE;

    (void) mc_lua_return_error (lua, "no active runtime context");
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_require_object_capability (lua_State *lua, const mc_lua_package_t *package,
                                  guint64 capability)
{
    if (mc_lua_host_has_capability (package, capability, MC_LUA_HOST_API_OBJECTS_SIZE))
        return TRUE;

    (void) mc_lua_return_error (lua, "not_ready");
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_get_handle (lua_State *lua, int index, mc_runtime_handle_kind_t expected_kind,
                   mc_runtime_handle_t *handle)
{
    const mc_lua_handle_t *lua_handle =
        (const mc_lua_handle_t *) luaL_testudata (lua, index, MC_LUA_HANDLE_METATABLE);

    if (lua_handle == NULL || lua_handle->handle.kind != expected_kind)
    {
        (void) mc_lua_return_error (lua, "invalid_handle");
        return FALSE;
    }

    if (handle != NULL)
        *handle = lua_handle->handle;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_check_string_without_nul (lua_State *lua, int index, const char **value)
{
    size_t length;
    const char *string = luaL_checklstring (lua, index, &length);

    if (memchr (string, '\0', length) != NULL)
    {
        (void) mc_lua_return_error (lua, "invalid_argument");
        return FALSE;
    }

    if (value != NULL)
        *value = string;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_host_string (lua_State *lua, const mc_lua_package_t *package,
                         mc_runtime_string_t *string)
{
    if (string != NULL && string->data != NULL)
        lua_pushlstring (lua, string->data, string->length);
    else
        lua_pushliteral (lua, "");

    if (package != NULL && package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->string_free != NULL)
        package->runtime->host->string_free (package->runtime->context, string);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_set_string_field (lua_State *lua, const char *name, const char *value)
{
    if (value != NULL)
        lua_pushlstring (lua, value, strlen (value));
    else
        lua_pushnil (lua);
    lua_setfield (lua, -2, name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_set_boolean_field (lua_State *lua, const char *name, gboolean value)
{
    lua_pushboolean (lua, value);
    lua_setfield (lua, -2, name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_set_integer_field (lua_State *lua, const char *name, gint64 value)
{
    lua_pushinteger (lua, (lua_Integer) value);
    lua_setfield (lua, -2, name);
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_handle_tostring (lua_State *lua)
{
    const mc_lua_handle_t *handle = (const mc_lua_handle_t *) lua_touserdata (lua, 1);
    const char *kind = "invalid";

    if (handle != NULL)
    {
        switch (handle->handle.kind)
        {
        case MC_RUNTIME_HANDLE_EDITOR:
            kind = "editor";
            break;
        case MC_RUNTIME_HANDLE_INVALID:
        default:
            break;
        }
    }

    lua_pushfstring (lua, "mc.%s handle", kind);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_handle (lua_State *lua, const mc_runtime_handle_t *handle)
{
    mc_lua_handle_t *lua_handle;

    if (!mc_lua_handle_is_valid (handle))
    {
        lua_pushnil (lua);
        return;
    }

    lua_handle = (mc_lua_handle_t *) lua_newuserdata (lua, sizeof (*lua_handle));
    lua_handle->handle = *handle;

    if (luaL_newmetatable (lua, MC_LUA_HANDLE_METATABLE))
    {
        lua_pushcfunction (lua, mc_lua_handle_tostring);
        lua_setfield (lua, -2, "__tostring");
        lua_pushcfunction (lua, mc_lua_handle_index);
        lua_setfield (lua, -2, "__index");
    }
    lua_setmetatable (lua, -2);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_event (lua_State *lua, const mc_runtime_event_snapshot_t *snapshot)
{
    lua_createtable (lua, 0, 7);
    mc_lua_set_string_field (lua, "name", mc_lua_event_name (snapshot->event_id));

    switch (snapshot->event_id)
    {
    case MC_RUNTIME_EVENT_STARTUP:
        mc_lua_set_string_field (lua, "config_dir", snapshot->data.startup.config_dir);
        mc_lua_set_string_field (lua, "data_dir", snapshot->data.startup.data_dir);
        break;

    case MC_RUNTIME_EVENT_SHUTDOWN:
        mc_lua_set_string_field (lua, "reason", snapshot->data.shutdown.reason);
        break;

    case MC_RUNTIME_EVENT_EDITOR_OPEN:
        mc_lua_push_handle (lua, &snapshot->data.editor_open.editor);
        lua_setfield (lua, -2, "editor");
        mc_lua_set_string_field (lua, "path", snapshot->data.editor_open.path);
        mc_lua_set_boolean_field (lua, "readonly", snapshot->data.editor_open.readonly);
        mc_lua_set_integer_field (lua, "line", snapshot->data.editor_open.line);
        mc_lua_set_integer_field (lua, "column", snapshot->data.editor_open.column);
        break;

    case MC_RUNTIME_EVENT_EDITOR_SAVE:
        mc_lua_push_handle (lua, &snapshot->data.editor_save.editor);
        lua_setfield (lua, -2, "editor");
        mc_lua_set_string_field (lua, "path", snapshot->data.editor_save.path);
        mc_lua_set_string_field (lua, "previous_path", snapshot->data.editor_save.previous_path);
        mc_lua_set_boolean_field (lua, "save_as", snapshot->data.editor_save.save_as);
        break;

    case MC_RUNTIME_EVENT_EDITOR_KEY:
        mc_lua_push_handle (lua, &snapshot->data.editor_key.editor);
        lua_setfield (lua, -2, "editor");
        lua_createtable (lua, 0, 6);
        mc_lua_set_string_field (lua, "name", snapshot->data.editor_key.key.name);
        mc_lua_set_integer_field (lua, "code", snapshot->data.editor_key.key.code);
        mc_lua_set_string_field (lua, "text", snapshot->data.editor_key.key.text);
        lua_createtable (lua, 0, 3);
        mc_lua_set_boolean_field (lua, "shift", snapshot->data.editor_key.key.shift);
        mc_lua_set_boolean_field (lua, "ctrl", snapshot->data.editor_key.key.ctrl);
        mc_lua_set_boolean_field (lua, "alt", snapshot->data.editor_key.key.alt);
        lua_setfield (lua, -2, "modifiers");
        lua_setfield (lua, -2, "key");
        break;

    case MC_RUNTIME_EVENT_INVALID:
    case MC_RUNTIME_EVENT_COUNT:
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:path() -> string|nil, error? @workspace editor @capability editor @mutation no
 * @summary Return the document path. */
static int
mc_lua_editor_path (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t path = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_path (package->runtime->context, &handle, &path, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &path);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:info() -> DocumentInfo|nil, error? @workspace editor @capability editor @mutation no
 * @summary Return document metadata and the current revision. */
static int
mc_lua_editor_info (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_info_t info = { 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_INFO_SIZE)
        || package->runtime->host->editor_info == NULL
        || package->runtime->host->editor_info_free == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_info (package->runtime->context, &handle, &info, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    lua_createtable (lua, 0, 7);
    if (info.has_path)
    {
        lua_pushlstring (lua, info.path, info.path_length);
        lua_setfield (lua, -2, "path");
    }
    lua_pushlstring (lua, info.name, info.name_length);
    lua_setfield (lua, -2, "name");
    mc_lua_set_boolean_field (lua, "modified", info.modified);
    mc_lua_set_boolean_field (lua, "readonly", info.readonly);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) info.revision);
    mc_lua_set_integer_field (lua, "byte_length", (lua_Integer) info.byte_length);
    mc_lua_set_integer_field (lua, "line_count", (lua_Integer) info.line_count);
    package->runtime->host->editor_info_free (package->runtime->context, &info);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_editor_position (lua_State *lua, const mc_runtime_editor_position_t *position)
{
    lua_createtable (lua, 0, 3);
    mc_lua_set_integer_field (lua, "offset", (lua_Integer) position->offset);
    mc_lua_set_integer_field (lua, "line", (lua_Integer) position->line);
    mc_lua_set_integer_field (lua, "column", (lua_Integer) position->column);
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:selection() -> Selection|nil, error? @workspace editor @capability editor @mutation
 * no @summary Return the current selection snapshot. */
static int
mc_lua_editor_selection (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_selection_t selection = { 0 };
    const char *error = NULL;
    const char *kind;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_SELECTION_SIZE)
        || package->runtime->host->editor_selection == NULL
        || package->runtime->host->editor_selection_free == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_selection (package->runtime->context, &handle, &selection,
                                                   &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    switch (selection.kind)
    {
    case MC_RUNTIME_EDITOR_SELECTION_NONE:
        kind = "none";
        break;
    case MC_RUNTIME_EDITOR_SELECTION_LINEAR:
        kind = "linear";
        break;
    case MC_RUNTIME_EDITOR_SELECTION_COLUMN:
        kind = "column";
        break;
    default:
        package->runtime->host->editor_selection_free (package->runtime->context, &selection);
        return mc_lua_return_error (lua, "invalid_result");
    }

    lua_createtable (lua, 0, 7);
    lua_pushstring (lua, kind);
    lua_setfield (lua, -2, "kind");
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) selection.revision);
    mc_lua_push_editor_position (lua, &selection.anchor);
    lua_setfield (lua, -2, "anchor");
    mc_lua_push_editor_position (lua, &selection.cursor);
    lua_setfield (lua, -2, "cursor");
    lua_createtable (lua, (int) selection.ranges_count, 0);
    for (guint i = 0; i < selection.ranges_count; i++)
    {
        lua_createtable (lua, 0, 2);
        mc_lua_set_integer_field (lua, "from", (lua_Integer) selection.ranges[i].from);
        mc_lua_set_integer_field (lua, "to", (lua_Integer) selection.ranges[i].to);
        lua_rawseti (lua, -2, (lua_Integer) i + 1);
    }
    lua_setfield (lua, -2, "ranges");
    if (selection.has_text)
    {
        lua_pushlstring (lua, selection.text, selection.text_length);
        lua_setfield (lua, -2, "text");
    }
    mc_lua_set_boolean_field (lua, "text_truncated", selection.text_truncated);
    package->runtime->host->editor_selection_free (package->runtime->context, &selection);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:replace_selection(text, options?) -> EditResult|nil, error? @workspace editor
 * @capability editor @mutation yes @summary Replace the selection, or insert when it is empty. */
static int
mc_lua_editor_replace_selection (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_edit_result_t result = { 0 };
    mc_runtime_editor_selection_t selection = { 0 };
    const char *error = NULL;
    const char *replacement;
    size_t replacement_length;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    replacement = luaL_checklstring (lua, 2, &replacement_length);
    if (mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                    MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_V2_SIZE)
        && package->runtime->host->editor_replace_selection_v2 != NULL
        && package->runtime->host->editor_selection != NULL
        && package->runtime->host->editor_selection_free != NULL)
    {
        if (!package->runtime->host->editor_selection (package->runtime->context, &handle,
                                                       &selection, &error))
            return mc_lua_return_error (lua, error != NULL ? error : "failed");
        if (!package->runtime->host->editor_replace_selection_v2 (
                package->runtime->context, &handle, selection.revision, replacement,
                replacement_length, &result, &error))
        {
            package->runtime->host->editor_selection_free (package->runtime->context, &selection);
            return mc_lua_return_error (lua, error != NULL ? error : "failed");
        }
        package->runtime->host->editor_selection_free (package->runtime->context, &selection);
        goto success;
    }
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_SIZE)
        || package->runtime->host->editor_replace_selection == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_replace_selection (
            package->runtime->context, &handle, replacement, replacement_length, &result, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

success:
    lua_createtable (lua, 0, 2);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) result.revision);
    mc_lua_push_editor_position (lua, &result.cursor);
    lua_setfield (lua, -2, "cursor");
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean mc_lua_table_uint64 (lua_State *lua, int index, const char *field, guint64 *value,
                                     gboolean required);

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:replace(range, text) -> EditResult|nil, error? @workspace editor @capability editor
 * @mutation yes @summary Replace a byte range in the editor buffer. */
static int
mc_lua_editor_replace (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_edit_result_t result = { 0 };
    lua_Integer from;
    lua_Integer to;
    const char *replacement;
    size_t replacement_length;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (lua_istable (lua, 2))
    {
        mc_runtime_editor_change_t change = { 0 };
        mc_runtime_editor_edit_t edit_spec = { 0 };

        (void) mc_lua_table_uint64 (lua, 2, "from", &change.from, TRUE);
        (void) mc_lua_table_uint64 (lua, 2, "to", &change.to, TRUE);
        (void) mc_lua_table_uint64 (lua, 2, "revision", &edit_spec.revision, TRUE);
        replacement = luaL_checklstring (lua, 3, &replacement_length);
        change.text = replacement;
        change.text_length = replacement_length;
        edit_spec.changes = &change;
        edit_spec.changes_count = 1;
        if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                         MC_LUA_HOST_API_EDITOR_EDIT_SIZE)
            || package->runtime->host->editor_edit == NULL)
            return mc_lua_return_error (lua, "not_ready");
        if (!package->runtime->host->editor_edit (package->runtime->context, &handle, &edit_spec,
                                                  &result, &error))
            return mc_lua_return_error (lua, error != NULL ? error : "failed");
        goto success;
    }
    from = luaL_checkinteger (lua, 2);
    to = luaL_checkinteger (lua, 3);
    replacement = luaL_checklstring (lua, 4, &replacement_length);
    if (from < 0 || to < from)
        return mc_lua_return_error (lua, "invalid_range");
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_REPLACE_SIZE)
        || package->runtime->host->editor_replace == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_replace (package->runtime->context, &handle, (guint64) from,
                                                 (guint64) to, replacement, replacement_length,
                                                 &result, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

success:
    lua_createtable (lua, 0, 2);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) result.revision);
    mc_lua_push_editor_position (lua, &result.cursor);
    lua_setfield (lua, -2, "cursor");
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_table_uint64 (lua_State *lua, int index, const char *field, guint64 *value,
                     gboolean required)
{
    lua_Integer integer;

    lua_getfield (lua, index, field);
    if (lua_isnil (lua, -1) && !required)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    integer = luaL_checkinteger (lua, -1);
    lua_pop (lua, 1);
    if (integer < 0)
        luaL_error (lua, "%s must be non-negative", field);
    *value = (guint64) integer;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:text(range?) -> string|nil, error? @workspace editor @capability editor @mutation no
 * @summary Read the complete buffer or a byte range. */
static int
mc_lua_editor_text (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_range_t range = { 0 };
    mc_runtime_string_t text = { 0 };
    guint64 revision = 0;
    gboolean has_range = !lua_isnoneornil (lua, 2);
    gboolean has_revision = FALSE;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_TEXT_SIZE)
        || package->runtime->host->editor_text == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (has_range)
    {
        luaL_checktype (lua, 2, LUA_TTABLE);
        (void) mc_lua_table_uint64 (lua, 2, "from", &range.from, TRUE);
        (void) mc_lua_table_uint64 (lua, 2, "to", &range.to, TRUE);
        has_revision = mc_lua_table_uint64 (lua, 2, "revision", &revision, FALSE);
    }
    if (!package->runtime->host->editor_text (package->runtime->context, &handle,
                                              has_range ? &range : NULL, has_revision, revision,
                                              &text, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    mc_lua_push_host_string (lua, package, &text);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:edit(spec) -> EditResult|nil, error? @workspace editor @capability editor @mutation
 * yes @summary Apply replacements atomically as one undo operation. */
static int
mc_lua_editor_edit (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_edit_t edit_spec = { 0 };
    mc_runtime_editor_edit_result_t result = { 0 };
    mc_runtime_editor_change_t *changes;
    const char *error = NULL;
    int changes_table_index;
    size_t changes_count;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    luaL_checktype (lua, 2, LUA_TTABLE);
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_EDIT_SIZE)
        || package->runtime->host->editor_edit == NULL)
        return mc_lua_return_error (lua, "not_ready");

    (void) mc_lua_table_uint64 (lua, 2, "revision", &edit_spec.revision, TRUE);
    lua_getfield (lua, 2, "changes");
    luaL_checktype (lua, -1, LUA_TTABLE);
    changes_table_index = lua_gettop (lua);
    changes_count = lua_rawlen (lua, -1);
    if (changes_count == 0 || changes_count > 1024)
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "invalid_edit");
    }
    /* Keep the temporary native array owned by Lua.  luaL_check* below may
     * raise a longjmp on malformed input, in which case a heap allocation
     * made with g_new0() would leak. */
    changes = (mc_runtime_editor_change_t *) lua_newuserdata (
        lua, sizeof (mc_runtime_editor_change_t) * changes_count);
    memset (changes, 0, sizeof (mc_runtime_editor_change_t) * changes_count);
    for (size_t i = 0; i < changes_count; i++)
    {
        size_t length;

        lua_rawgeti (lua, changes_table_index, (lua_Integer) i + 1);
        luaL_checktype (lua, -1, LUA_TTABLE);
        (void) mc_lua_table_uint64 (lua, lua_gettop (lua), "from", &changes[i].from, TRUE);
        (void) mc_lua_table_uint64 (lua, lua_gettop (lua), "to", &changes[i].to, TRUE);
        lua_getfield (lua, -1, "text");
        changes[i].text = luaL_checklstring (lua, -1, &length);
        changes[i].text_length = length;
        lua_pop (lua, 2);
    }
    edit_spec.changes = changes;
    edit_spec.changes_count = (guint) changes_count;
    lua_getfield (lua, 2, "cursor");
    if (!lua_isnil (lua, -1))
    {
        luaL_checktype (lua, -1, LUA_TTABLE);
        edit_spec.has_cursor = TRUE;
        (void) mc_lua_table_uint64 (lua, lua_gettop (lua), "offset", &edit_spec.cursor.offset,
                                    TRUE);
    }
    lua_pop (lua, 1);

    if (!package->runtime->host->editor_edit (package->runtime->context, &handle, &edit_spec,
                                              &result, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    lua_createtable (lua, 0, 2);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) result.revision);
    mc_lua_push_editor_position (lua, &result.cursor);
    lua_setfield (lua, -2, "cursor");
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:cursor() -> Position|nil, error? @workspace editor @capability editor @mutation no
 * @summary Return the current cursor position. */
static int
mc_lua_editor_cursor (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    guint64 line;
    guint64 column;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_cursor (package->runtime->context, &handle, &line, &column,
                                                &error))
        return mc_lua_return_error (lua, error);

    lua_pushinteger (lua, (lua_Integer) line);
    lua_pushinteger (lua, (lua_Integer) column);
    return 2;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:set_cursor(position) -> boolean|nil, error? @workspace editor @capability editor
 * @mutation yes @summary Move the cursor to a validated position. */
static int
mc_lua_editor_set_cursor (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    lua_Integer line;
    lua_Integer column;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    line = luaL_checkinteger (lua, 2);
    column = luaL_checkinteger (lua, 3);
    if (line <= 0 || column <= 0)
        return mc_lua_return_error (lua, "invalid_argument");

    if (!package->runtime->host->editor_set_cursor (package->runtime->context, &handle,
                                                    (guint64) line, (guint64) column, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:is_readonly() -> boolean|nil, error? @workspace editor @capability editor @mutation
 * no @summary Report whether the document is read-only. */
static int
mc_lua_editor_is_readonly (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    gboolean readonly;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_is_readonly (package->runtime->context, &handle, &readonly,
                                                     &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, readonly);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:tab_width() -> integer|nil, error? @workspace editor @capability editor @mutation no
 * @summary Return the configured tab width. */
static int
mc_lua_editor_tab_width (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    guint tab_width;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_TAB_WIDTH_SIZE)
        || package->runtime->host->editor_tab_width == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_tab_width (package->runtime->context, &handle, &tab_width,
                                                   &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    lua_pushinteger (lua, (lua_Integer) tab_width);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:overwrite() -> boolean|nil, error? @workspace editor @capability editor @mutation no
 * @summary Report whether typing overwrites instead of inserting. */
static int
mc_lua_editor_overwrite (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    gboolean overwrite;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_OVERWRITE_SIZE)
        || package->runtime->host->editor_overwrite == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_overwrite (package->runtime->context, &handle, &overwrite,
                                                   &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    lua_pushboolean (lua, overwrite);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:set_overwrite(flag) -> boolean|nil, error? @workspace editor @capability editor
 * @mutation yes @summary Switch typing between overwrite and insert. */
static int
mc_lua_editor_set_overwrite (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    luaL_checktype (lua, 2, LUA_TBOOLEAN);
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_SET_OVERWRITE_SIZE)
        || package->runtime->host->editor_set_overwrite == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_set_overwrite (package->runtime->context, &handle,
                                                       lua_toboolean (lua, 2), &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:get_text() -> string|nil, error? @workspace editor @capability editor @mutation no
 * @summary Read the complete buffer using the compatibility API. */
static int
mc_lua_editor_get_text (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    lua_Integer from;
    lua_Integer to;
    mc_runtime_string_t text = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    from = luaL_checkinteger (lua, 2);
    to = luaL_checkinteger (lua, 3);
    if (!package->runtime->host->editor_get_text (package->runtime->context, &handle, from, to,
                                                  &text, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &text);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:selected_text() -> string|nil, error? @workspace editor @capability editor @mutation
 * no @summary Read selected text using the compatibility API. */
static int
mc_lua_editor_selected_text (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t text = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (package->runtime->host->struct_size < MC_LUA_HOST_API_EDITOR_SELECTED_TEXT_SIZE
        || package->runtime->host->editor_selected_text == NULL)
        return mc_lua_return_error (lua, "not_supported");
    if (!package->runtime->host->editor_selected_text (package->runtime->context, &handle, &text,
                                                       &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &text);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:insert(text) -> boolean|nil, error? @workspace editor @capability editor @mutation
 * yes @summary Insert text at the cursor using the compatibility API. */
static int
mc_lua_editor_insert (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *text;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle)
        || !mc_lua_check_string_without_nul (lua, 2, &text))
        return 2;

    if (!package->runtime->host->editor_insert (package->runtime->context, &handle, text, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua editor:save() -> boolean|nil, error? @workspace editor @capability editor @mutation yes
 * @summary Save the document with the native editor operation. */
static int
mc_lua_editor_save (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_save (package->runtime->context, &handle, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_handle_index (lua_State *lua)
{
    const mc_lua_handle_t *handle =
        (const mc_lua_handle_t *) luaL_testudata (lua, 1, MC_LUA_HANDLE_METATABLE);
    const char *method;

    if (handle == NULL || !lua_isstring (lua, 2))
    {
        lua_pushnil (lua);
        return 1;
    }

    method = lua_tostring (lua, 2);
    switch (handle->handle.kind)
    {
    case MC_RUNTIME_HANDLE_EDITOR:
        if (strcmp (method, "path") == 0)
            lua_pushcfunction (lua, mc_lua_editor_path);
        else if (strcmp (method, "info") == 0)
            lua_pushcfunction (lua, mc_lua_editor_info);
        else if (strcmp (method, "selection") == 0)
            lua_pushcfunction (lua, mc_lua_editor_selection);
        else if (strcmp (method, "replace_selection") == 0)
            lua_pushcfunction (lua, mc_lua_editor_replace_selection);
        else if (strcmp (method, "replace") == 0)
            lua_pushcfunction (lua, mc_lua_editor_replace);
        else if (strcmp (method, "text") == 0)
            lua_pushcfunction (lua, mc_lua_editor_text);
        else if (strcmp (method, "edit") == 0)
            lua_pushcfunction (lua, mc_lua_editor_edit);
        else if (strcmp (method, "cursor") == 0)
            lua_pushcfunction (lua, mc_lua_editor_cursor);
        else if (strcmp (method, "set_cursor") == 0)
            lua_pushcfunction (lua, mc_lua_editor_set_cursor);
        else if (strcmp (method, "is_readonly") == 0)
            lua_pushcfunction (lua, mc_lua_editor_is_readonly);
        else if (strcmp (method, "tab_width") == 0)
            lua_pushcfunction (lua, mc_lua_editor_tab_width);
        else if (strcmp (method, "overwrite") == 0)
            lua_pushcfunction (lua, mc_lua_editor_overwrite);
        else if (strcmp (method, "set_overwrite") == 0)
            lua_pushcfunction (lua, mc_lua_editor_set_overwrite);
        else if (strcmp (method, "get_text") == 0)
            lua_pushcfunction (lua, mc_lua_editor_get_text);
        else if (strcmp (method, "selected_text") == 0)
            lua_pushcfunction (lua, mc_lua_editor_selected_text);
        else if (strcmp (method, "insert") == 0)
            lua_pushcfunction (lua, mc_lua_editor_insert);
        else if (strcmp (method, "save") == 0)
            lua_pushcfunction (lua, mc_lua_editor_save);
        else
            lua_pushnil (lua);
        break;

    case MC_RUNTIME_HANDLE_INVALID:
    default:
        lua_pushnil (lua);
        break;
    }

    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.editor.current() -> editor|nil, error? @workspace editor @capability editor @mutation no
 * @summary Return the editor associated with the active callback. */
static int
mc_lua_editor_current (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR))
        return 2;

    handle = package->runtime->host->editor_current (package->runtime->context);
    if (!mc_lua_handle_is_valid (&handle))
        return mc_lua_return_error (lua, "not_ready");
    mc_lua_push_handle (lua, &handle);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_report_error (mc_lua_package_t *package, mc_runtime_error_phase_t phase, const char *summary)
{
    lua_State *lua = package->lua;
    const char *message = lua_tostring (lua, -1);
    const char *traceback;

    luaL_traceback (lua, lua, message != NULL ? message : "Lua error", 1);
    traceback = lua_tostring (lua, -1);
    if (package->runtime->host->struct_size >= MC_LUA_HOST_API_RUNTIME_ERROR_SIZE
        && package->runtime->host->runtime_error != NULL)
        package->runtime->host->runtime_error (package->runtime->context, "lua", package->id, phase,
                                               summary,
                                               traceback != NULL ? traceback : "Lua error");
    else
        mc_lua_log (package, "error", traceback != NULL ? traceback : "Lua error");
    lua_pop (lua, 2);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_subscription_destroy (gpointer data)
{
    mc_lua_subscription_t *subscription = (mc_lua_subscription_t *) data;
    mc_lua_package_t *package;

    if (subscription == NULL)
        return;

    package = subscription->package;
    if (package != NULL && package->subscriptions != NULL && subscription->token != 0)
        (void) g_hash_table_remove (package->subscriptions, &subscription->token);

    if (package != NULL && package->lua != NULL && subscription->callback_ref != LUA_NOREF)
        luaL_unref (package->lua, LUA_REGISTRYINDEX, subscription->callback_ref);

    g_free (subscription->token_key);
    g_free (subscription);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_macro_destroy (gpointer data)
{
    mc_lua_macro_t *macro = (mc_lua_macro_t *) data;
    mc_lua_package_t *package;

    if (macro == NULL)
        return;

    package = macro->package;
    if (package != NULL && package->runtime != NULL && package->runtime->macros != NULL)
        (void) g_ptr_array_remove_fast (package->runtime->macros, macro);

    if (package != NULL && package->lua != NULL && macro->action_ref != LUA_NOREF)
        luaL_unref (package->lua, LUA_REGISTRYINDEX, macro->action_ref);

    g_free (macro->id);
    g_free (macro->area);
    g_free (macro->key);
    g_free (macro->display_key);
    g_free (macro->description);
    g_free (macro->menu_path);
    g_free (macro->menu_label);
    g_free (macro);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_unsubscribe_all (mc_lua_package_t *package)
{
    GList *values;
    GList *link;

    if (package == NULL || package->subscriptions == NULL || package->runtime == NULL
        || package->runtime->host == NULL)
        return;

    values = g_hash_table_get_values (package->subscriptions);
    for (link = values; link != NULL; link = g_list_next (link))
    {
        const mc_lua_subscription_t *subscription = (const mc_lua_subscription_t *) link->data;

        if (!package->runtime->host->unsubscribe (package->runtime->context, subscription->token))
            mc_lua_log (package, "warning", "could not remove a Lua event subscription");
    }
    g_list_free (values);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_close (mc_lua_package_t *package)
{
    if (package == NULL || package->closed)
        return;

    mc_lua_package_unsubscribe_all (package);

    if (package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->struct_size >= MC_LUA_HOST_API_INDICATORS_SIZE
        && package->runtime->host->ui_indicators_clear_owner != NULL)
        package->runtime->host->ui_indicators_clear_owner (package->runtime->context, package->id);

    if (package->macros != NULL)
        g_ptr_array_set_size (package->macros, 0);
    if (package->service_listeners != NULL)
    {
        guint i;

        for (i = 0; i < package->service_listeners->len; i++)
            mc_lua_service_listener_free (
                (mc_lua_service_listener_t *) g_ptr_array_index (package->service_listeners, i));
        g_ptr_array_set_size (package->service_listeners, 0);
    }
    if (package->settings_ref != LUA_NOREF && package->lua != NULL)
    {
        luaL_unref (package->lua, LUA_REGISTRYINDEX, package->settings_ref);
        package->settings_ref = LUA_NOREF;
    }

    package->closed = TRUE;
    if (package->lua != NULL)
    {
        lua_close (package->lua);
        package->lua = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_destroy (mc_lua_package_t *package)
{
    if (package == NULL)
        return;

    mc_lua_package_close (package);
    if (package->subscriptions != NULL)
        g_hash_table_destroy (package->subscriptions);
    if (package->macros != NULL)
        g_ptr_array_free (package->macros, TRUE);
    if (package->service_listeners != NULL)
        g_ptr_array_free (package->service_listeners, TRUE);
    g_free (package->id);
    g_free (package->workspace);
    g_free (package->root);
    g_free (package->entry_path);
    g_free (package);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_destroy (gpointer data)
{
    mc_lua_runtime_t *runtime = (mc_lua_runtime_t *) data;

    if (runtime == NULL)
        return;

    if (mc_lua_runtime_current == runtime)
        mc_lua_runtime_current = NULL;
    if (runtime->packages != NULL)
        g_ptr_array_free (runtime->packages, TRUE);
    if (runtime->catalog != NULL)
        g_ptr_array_free (runtime->catalog, TRUE);
    if (runtime->macros != NULL)
        g_ptr_array_free (runtime->macros, TRUE);
    if (runtime->screens != NULL)
        g_hash_table_destroy (runtime->screens);
    if (runtime->disabled_package_ids != NULL)
        g_hash_table_destroy (runtime->disabled_package_ids);
    g_free (runtime->user_scripts_dir);
    g_free (runtime->user_modules_dir);
    g_free (runtime);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mc_lua_dup_table_string (lua_State *lua, int table, const char *field)
{
    char *value = NULL;

    lua_getfield (lua, table, field);
    if (lua_isstring (lua, -1))
        value = g_strdup (lua_tostring (lua, -1));
    lua_pop (lua, 1);
    return value;
}

/* The "file" of a help table: a relative name is taken from the script's directory. */
static char *
mc_lua_dup_help_file (lua_State *lua, int table, const mc_lua_package_t *package)
{
    char *file = mc_lua_dup_table_string (lua, table, "file");

    if (file != NULL && !g_path_is_absolute (file) && package != NULL && package->root != NULL)
    {
        char *absolute = g_build_filename (package->root, file, (char *) NULL);

        g_free (file);
        file = absolute;
    }
    return file;
}

/* --------------------------------------------------------------------------------------------- */

static guint64
mc_lua_table_uint64_default (lua_State *lua, int table, const char *field, guint64 fallback)
{
    guint64 value = fallback;

    lua_getfield (lua, table, field);
    if (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) >= 0)
        value = (guint64) lua_tointeger (lua, -1);
    lua_pop (lua, 1);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_table_boolean (lua_State *lua, int table, const char *field, gboolean fallback)
{
    gboolean value = fallback;

    lua_getfield (lua, table, field);
    if (lua_isboolean (lua, -1))
        value = lua_toboolean (lua, -1) != 0;
    lua_pop (lua, 1);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.on(event, callback, options?) -> integer|nil, error? @capability events @mutation yes
 * @summary Subscribe the package to a named event.
 * @lua-callback event(snapshot) -> nil
 */
static int
mc_lua_on (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *event_name;
    mc_runtime_event_id_t event_id;
    lua_Integer priority = 0;
    mc_lua_subscription_t *subscription;
    mc_runtime_subscription_t token;
    GError *mcerror = NULL;

    if (package == NULL || package->runtime == NULL || package->runtime->stopping)
        return luaL_error (lua, "Lua runtime is stopping");

    event_name = luaL_checkstring (lua, 1);
    event_id = mc_lua_event_id_from_name (event_name);
    if (event_id == MC_RUNTIME_EVENT_INVALID)
    {
        lua_pushnil (lua);
        lua_pushfstring (lua, "unknown event '%s'", event_name);
        return 2;
    }

    luaL_checktype (lua, 2, LUA_TFUNCTION);
    if (!lua_isnoneornil (lua, 3))
    {
        luaL_checktype (lua, 3, LUA_TTABLE);
        lua_getfield (lua, 3, "priority");
        if (!lua_isnil (lua, -1))
        {
            if (!lua_isinteger (lua, -1))
                return luaL_error (lua, "options.priority must be an integer");
            priority = lua_tointeger (lua, -1);
        }
        lua_pop (lua, 1);
    }

    if (priority < -100 || priority > 100)
    {
        lua_pushnil (lua);
        lua_pushliteral (lua, "options.priority must be between -100 and 100");
        return 2;
    }

    subscription = g_new0 (mc_lua_subscription_t, 1);
    subscription->package = package;
    subscription->callback_ref = LUA_NOREF;
    lua_pushvalue (lua, 2);
    subscription->callback_ref = luaL_ref (lua, LUA_REGISTRYINDEX);

    token = package->runtime->host->subscribe (package->runtime->context, event_id, (int) priority,
                                               mc_lua_event_callback, subscription,
                                               mc_lua_subscription_destroy, &mcerror);
    if (token == 0)
    {
        const char *message = mcerror != NULL ? mcerror->message : "could not register callback";

        luaL_unref (lua, LUA_REGISTRYINDEX, subscription->callback_ref);
        g_free (subscription);
        lua_pushnil (lua);
        lua_pushstring (lua, message);
        g_clear_error (&mcerror);
        return 2;
    }

    subscription->token = token;
    subscription->token_key = g_new (guint64, 1);
    *subscription->token_key = token;
    g_hash_table_insert (package->subscriptions, subscription->token_key, subscription);

    lua_pushinteger (lua, (lua_Integer) token);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.off(subscription) -> boolean @capability events @mutation yes @summary Remove an event
 * subscription owned by the package. */
static int
mc_lua_off (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    lua_Integer lua_token;
    guint64 token;
    mc_lua_subscription_t *subscription;
    gboolean removed;

    if (package == NULL || package->runtime == NULL)
    {
        lua_pushboolean (lua, FALSE);
        return 1;
    }

    lua_token = luaL_checkinteger (lua, 1);
    if (lua_token <= 0)
    {
        lua_pushboolean (lua, FALSE);
        return 1;
    }

    token = (guint64) lua_token;
    subscription = (mc_lua_subscription_t *) g_hash_table_lookup (package->subscriptions, &token);
    if (subscription == NULL)
    {
        lua_pushboolean (lua, FALSE);
        return 1;
    }

    removed = package->runtime->host->unsubscribe (package->runtime->context, token);
    lua_pushboolean (lua, removed);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.macro(spec) -> boolean|nil, error? @workspace editor @capability events @mutation yes
 * @summary Register an editor action with optional key and menu placement.
 * @lua-callback action(event) -> mc.PASS|mc.CONSUME
 */
static int
mc_lua_macro (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *id;
    const char *area;
    const char *key = NULL;
    const char *description;
    char *normalized_key;
    lua_Integer priority = 50;
    guint i;
    mc_lua_macro_t *macro;

    if (package == NULL || package->runtime == NULL || package->runtime->stopping)
        return luaL_error (lua, "Lua runtime is stopping");
    if ((package->provides & MC_LUA_PROVIDES_MACROS) == 0)
        return mc_lua_return_error (lua, "manifest_does_not_declare_macros");

    luaL_checktype (lua, 1, LUA_TTABLE);

    lua_getfield (lua, 1, "id");
    id = luaL_checkstring (lua, -1);
    if (!mc_lua_id_is_valid (id))
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "invalid_macro_id");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "area");
    area = luaL_checkstring (lua, -1);
    if (g_ascii_strcasecmp (area, "editor") != 0)
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "unsupported_macro_area");
    }
    lua_pop (lua, 1);

    if (g_strcmp0 (package->workspace, MC_LUA_EDITOR_WORKSPACE) != 0)
        return mc_lua_return_error (lua, "macro_area_not_allowed_by_workspace");

    lua_getfield (lua, 1, "key");
    if (lua_isnil (lua, -1))
        normalized_key = NULL;
    else
    {
        key = luaL_checkstring (lua, -1);
        normalized_key = mc_lua_key_name_normalize (key);
    }
    lua_pop (lua, 1);
    if (key != NULL && normalized_key == NULL)
        return mc_lua_return_error (lua, "invalid_macro_key");

    lua_getfield (lua, 1, "description");
    description = luaL_checkstring (lua, -1);
    if (description[0] == '\0')
    {
        lua_pop (lua, 1);
        g_free (normalized_key);
        return mc_lua_return_error (lua, "invalid_macro_description");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "priority");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isinteger (lua, -1))
        {
            lua_pop (lua, 1);
            g_free (normalized_key);
            return luaL_error (lua, "macro.priority must be an integer");
        }
        priority = lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);
    if (priority < 0 || priority > 100)
    {
        g_free (normalized_key);
        return mc_lua_return_error (lua, "macro.priority must be between 0 and 100");
    }

    for (i = 0; package->macros != NULL && i < package->macros->len; i++)
    {
        const mc_lua_macro_t *existing =
            (const mc_lua_macro_t *) g_ptr_array_index (package->macros, i);

        if (g_strcmp0 (existing->id, id) == 0)
        {
            g_free (normalized_key);
            return mc_lua_return_error (lua, "duplicate_macro_id");
        }
    }

    lua_getfield (lua, 1, "action");
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        g_free (normalized_key);
        return luaL_error (lua, "macro.action must be a function");
    }

    macro = g_new0 (mc_lua_macro_t, 1);
    macro->package = package;
    macro->id = g_strdup (id);
    macro->area = g_strdup ("editor");
    macro->key = normalized_key;
    macro->display_key = g_strdup (key);
    macro->description = g_strdup (description);
    macro->priority = (int) priority;
    macro->menu_position = 1000;
    macro->action_ref = LUA_NOREF;
    macro->listed = TRUE;
    lua_getfield (lua, 1, "listed");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isboolean (lua, -1))
        {
            lua_pop (lua, 1);
            mc_lua_macro_destroy (macro);
            return luaL_error (lua, "macro.listed must be a boolean");
        }
        macro->listed = lua_toboolean (lua, -1);
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "menu");
    if (!lua_isnil (lua, -1))
    {
        const char *menu_path;
        const char *menu_label;
        lua_Integer menu_position = 1000;

        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            mc_lua_macro_destroy (macro);
            return luaL_error (lua, "macro.menu must be a table");
        }

        lua_getfield (lua, -1, "path");
        if (!lua_isstring (lua, -1))
        {
            lua_pop (lua, 2);
            mc_lua_macro_destroy (macro);
            return luaL_error (lua, "macro.menu.path must be a string");
        }
        menu_path = lua_tostring (lua, -1);
        if (menu_path[0] == '\0' || strchr (menu_path, '/') != NULL
            || strchr (menu_path, '\\') != NULL)
        {
            lua_pop (lua, 2);
            mc_lua_macro_destroy (macro);
            return mc_lua_return_error (lua, "invalid_macro_menu_path");
        }
        macro->menu_path = g_strdup (menu_path);
        lua_pop (lua, 1);

        lua_getfield (lua, -1, "label");
        if (lua_isnil (lua, -1))
            menu_label = description;
        else
        {
            if (!lua_isstring (lua, -1))
            {
                lua_pop (lua, 2);
                mc_lua_macro_destroy (macro);
                return luaL_error (lua, "macro.menu.label must be a string");
            }
            menu_label = lua_tostring (lua, -1);
        }
        if (menu_label[0] == '\0')
        {
            lua_pop (lua, 2);
            mc_lua_macro_destroy (macro);
            return mc_lua_return_error (lua, "invalid_macro_menu_label");
        }
        macro->menu_label = g_strdup (menu_label);
        lua_pop (lua, 1);

        lua_getfield (lua, -1, "position");
        if (!lua_isnil (lua, -1))
        {
            if (!lua_isinteger (lua, -1))
            {
                lua_pop (lua, 2);
                mc_lua_macro_destroy (macro);
                return luaL_error (lua, "macro.menu.position must be an integer");
            }
            menu_position = lua_tointeger (lua, -1);
        }
        lua_pop (lua, 1);
        if (menu_position < -100000 || menu_position > 100000)
        {
            lua_pop (lua, 1);
            mc_lua_macro_destroy (macro);
            return mc_lua_return_error (lua, "macro.menu.position_out_of_range");
        }
        macro->menu_position = (int) menu_position;
    }
    lua_pop (lua, 1);
    macro->action_ref = luaL_ref (lua, LUA_REGISTRYINDEX);

    g_ptr_array_add (package->macros, macro);
    g_ptr_array_add (package->runtime->macros, macro);
    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.log.debug(message) -> nil @summary Write a debug message to the Lua runtime log.
 * @lua mc.log.info(message) -> nil @summary Write an informational message to the Lua runtime log.
 * @lua mc.log.warn(message) -> nil @summary Write a warning to the Lua runtime log.
 * @lua mc.log.error(message) -> nil @summary Write an error to the Lua runtime log.
 */
static int
mc_lua_log_message (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *level = lua_tostring (lua, lua_upvalueindex (1));
    const char *message = luaL_checkstring (lua, 1);

    mc_lua_log (package, level != NULL ? level : "info", message);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.process.run(spec) -> ProcessResult|nil, error? @capability process @mutation yes
 * @summary Run a shell command and capture its bounded output. */
static int
mc_lua_process_run (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *command;
    const char *error = NULL;
    lua_Integer max_output = 8 * 1024 * 1024;
    mc_runtime_process_result_t result = { 0 };

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_PROCESS,
                                     MC_LUA_HOST_API_PROCESS_SIZE)
        || package->runtime->host->process_run_shell == NULL
        || package->runtime->host->process_result_free == NULL)
        return mc_lua_return_error (lua, "not_supported");

    luaL_checktype (lua, 1, LUA_TTABLE);
    lua_getfield (lua, 1, "command");
    command = luaL_checkstring (lua, -1);
    if (command[0] == '\0')
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "invalid_command");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "max_output");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isinteger (lua, -1))
        {
            lua_pop (lua, 1);
            return luaL_error (lua, "process.max_output must be an integer");
        }
        max_output = lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);
    if (max_output < 1 || max_output > 64 * 1024 * 1024)
        return mc_lua_return_error (lua, "max_output_out_of_range");

    if (!package->runtime->host->process_run_shell (package->runtime->context, command,
                                                    (gsize) max_output, &result, &error))
        return mc_lua_return_error (lua, error);

    lua_createtable (lua, 0, 7);
    lua_pushlstring (lua, result.out.data != NULL ? result.out.data : "", result.out.length);
    lua_setfield (lua, -2, "stdout");
    lua_pushlstring (lua, result.err.data != NULL ? result.err.data : "", result.err.length);
    lua_setfield (lua, -2, "stderr");
    if (result.exit_code >= 0)
        lua_pushinteger (lua, result.exit_code);
    else
        lua_pushnil (lua);
    lua_setfield (lua, -2, "exit_code");
    if (result.term_signal > 0)
        lua_pushinteger (lua, result.term_signal);
    else
        lua_pushnil (lua);
    lua_setfield (lua, -2, "signal");
    lua_pushboolean (lua, result.out_truncated);
    lua_setfield (lua, -2, "stdout_truncated");
    lua_pushboolean (lua, result.err_truncated);
    lua_setfield (lua, -2, "stderr_truncated");
    package->runtime->host->process_result_free (package->runtime->context, &result);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_not_ready (lua_State *lua)
{
    lua_pushnil (lua);
    lua_pushliteral (lua, "not_ready");
    return 2;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_dialog_controls_free (mc_runtime_dialog_control_t *controls, guint count)
{
    guint i;

    for (i = 0; controls != NULL && i < count; i++)
    {
        g_free ((char *) controls[i].id);
        g_free ((char *) controls[i].text);
        g_free ((char *) controls[i].label);
        g_free ((char *) controls[i].value);
        if (controls[i].options != NULL)
        {
            guint j;
            for (j = 0; j < controls[i].options_count; j++)
            {
                g_free ((char *) controls[i].options[j].id);
                g_free ((char *) controls[i].options[j].label);
            }
            g_free ((mc_runtime_dialog_option_t *) controls[i].options);
        }
        mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) controls[i].controls,
                                     controls[i].controls_count);
    }
    g_free (controls);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_string (lua_State *lua, int table, const char *name, gboolean required, gsize maximum,
                      char **value)
{
    const char *text;

    lua_getfield (lua, table, name);
    if (lua_isnil (lua, -1) && !required)
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_isstring (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    text = lua_tostring (lua, -1);
    if (text == NULL || strlen (text) > maximum || !g_utf8_validate (text, -1, NULL))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    *value = g_strdup (text);
    lua_pop (lua, 1);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_uint (lua_State *lua, int table, const char *name, guint *value, gboolean *present)
{
    lua_getfield (lua, table, name);
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_isinteger (lua, -1) || lua_tointeger (lua, -1) <= 0
        || (guint64) lua_tointeger (lua, -1) > G_MAXUINT)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    *value = (guint) lua_tointeger (lua, -1);
    *present = TRUE;
    lua_pop (lua, 1);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_boolean (lua_State *lua, int table, const char *name, gboolean *value)
{
    lua_getfield (lua, table, name);
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_isboolean (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    *value = lua_toboolean (lua, -1);
    lua_pop (lua, 1);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_completion_name_is_valid (const char *name)
{
    static const char *const names[] = { "files", "hosts", "commands", "variables",
                                         "users", "cd",    "shell",    NULL };
    int i;

    for (i = 0; names[i] != NULL; i++)
        if (strcmp (name, names[i]) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_input_completion (lua_State *lua, int table, mc_runtime_dialog_control_t *control)
{
    mc_runtime_dialog_option_t *options = NULL;
    guint count, i;
    gboolean valid = TRUE;

    table = lua_absindex (lua, table);
    lua_getfield (lua, table, "completion");
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_istable (lua, -1) || (count = (guint) lua_rawlen (lua, -1)) == 0 || count > 7)
    {
        lua_pop (lua, 1);
        return FALSE;
    }

    options = g_new0 (mc_runtime_dialog_option_t, count);
    for (i = 0; i < count && valid; i++)
    {
        const char *name;
        guint previous;

        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        name = lua_isstring (lua, -1) ? lua_tostring (lua, -1) : NULL;
        valid = name != NULL && mc_lua_dialog_completion_name_is_valid (name);
        for (previous = 0; valid && previous < i; previous++)
            if (strcmp (options[previous].id, name) == 0)
                valid = FALSE;
        if (valid)
            options[i].id = g_strdup (name);
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    if (!valid)
    {
        for (i = 0; i < count; i++)
            g_free ((char *) options[i].id);
        g_free (options);
        return FALSE;
    }

    control->options = options;
    control->options_count = count;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_parse_controls (lua_State *lua, int index, guint depth, mc_lua_dialog_spec_t *spec,
                              GHashTable *ids, mc_runtime_dialog_control_t **result, guint *count)
{
    guint i, length;
    mc_runtime_dialog_control_t *controls;

    if (!lua_istable (lua, index) || depth > MC_LUA_DIALOG_MAX_DEPTH
        || (length = (guint) lua_rawlen (lua, index)) == 0
        || spec->control_count + length > MC_LUA_DIALOG_MAX_CONTROLS)
        return FALSE;
    controls = g_new0 (mc_runtime_dialog_control_t, length);
    spec->control_count += length;
    for (i = 0; i < length; i++)
    {
        mc_runtime_dialog_control_t *control = &controls[i];
        const char *type;

        lua_rawgeti (lua, index, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
            goto fail;
        lua_getfield (lua, -1, "type");
        type = lua_tostring (lua, -1);
        lua_pop (lua, 1);
        if (type == NULL)
            goto fail;
        if (strcmp (type, "label") == 0)
            control->type = MC_RUNTIME_DIALOG_LABEL;
        else if (strcmp (type, "input") == 0)
            control->type = MC_RUNTIME_DIALOG_INPUT;
        else if (strcmp (type, "checkbox") == 0)
            control->type = MC_RUNTIME_DIALOG_CHECKBOX;
        else if (strcmp (type, "select") == 0)
            control->type = MC_RUNTIME_DIALOG_SELECT;
        else if (strcmp (type, "separator") == 0)
            control->type = MC_RUNTIME_DIALOG_SEPARATOR;
        else if (strcmp (type, "hbox") == 0)
            control->type = MC_RUNTIME_DIALOG_HBOX;
        else if (strcmp (type, "vbox") == 0)
            control->type = MC_RUNTIME_DIALOG_VBOX;
        else if (strcmp (type, "spacer") == 0)
            control->type = MC_RUNTIME_DIALOG_SPACER;
        else if (strcmp (type, "button") == 0)
            control->type = MC_RUNTIME_DIALOG_BUTTON;
        else
            goto fail;
        if (!mc_lua_dialog_uint (lua, -1, "x", &control->x, &control->has_x)
            || !mc_lua_dialog_uint (lua, -1, "y", &control->y, &control->has_y)
            || !mc_lua_dialog_uint (lua, -1, "width", &control->width, &control->has_width)
            || !mc_lua_dialog_uint (lua, -1, "height", &control->height, &control->has_height)
            || !mc_lua_dialog_boolean (lua, -1, "expand_x", &control->expand_x)
            || !mc_lua_dialog_boolean (lua, -1, "expand_y", &control->expand_y))
            goto fail;
        if (control->type == MC_RUNTIME_DIALOG_LABEL
            || control->type == MC_RUNTIME_DIALOG_SEPARATOR)
        {
            if (!mc_lua_dialog_string (
                    lua, -1, control->type == MC_RUNTIME_DIALOG_LABEL ? "text" : "label",
                    control->type == MC_RUNTIME_DIALOG_LABEL, 16384, (char **) &control->text))
                goto fail;
        }
        else if (control->type == MC_RUNTIME_DIALOG_HBOX || control->type == MC_RUNTIME_DIALOG_VBOX)
        {
            lua_getfield (lua, -1, "controls");
            if (!mc_lua_dialog_parse_controls (lua, lua_gettop (lua), depth + 1, spec, ids,
                                               (mc_runtime_dialog_control_t **) &control->controls,
                                               &control->controls_count))
                goto fail;
            lua_pop (lua, 1);
        }
        else if (control->type != MC_RUNTIME_DIALOG_SPACER)
        {
            if (!mc_lua_dialog_string (lua, -1, "id", TRUE, MC_LUA_ID_MAX_LENGTH,
                                       (char **) &control->id)
                || !mc_lua_id_is_valid (control->id) || g_hash_table_contains (ids, control->id))
                goto fail;
            g_hash_table_add (ids, (gpointer) control->id);
            if (control->type == MC_RUNTIME_DIALOG_BUTTON
                || control->type == MC_RUNTIME_DIALOG_CHECKBOX
                || control->type == MC_RUNTIME_DIALOG_SELECT)
                if (!mc_lua_dialog_string (lua, -1, "label", TRUE, 4096, (char **) &control->label))
                    goto fail;
            if (control->type == MC_RUNTIME_DIALOG_INPUT
                || control->type == MC_RUNTIME_DIALOG_SELECT)
                if (!mc_lua_dialog_string (lua, -1, "value", FALSE, 4096,
                                           (char **) &control->value))
                    goto fail;
            if (control->type == MC_RUNTIME_DIALOG_INPUT)
            {
                if (!mc_lua_dialog_string (lua, -1, "history", FALSE, MC_LUA_ID_MAX_LENGTH,
                                           (char **) &control->text)
                    || (control->text != NULL && control->text[0] != '\0'
                        && !mc_lua_id_is_valid (control->text))
                    || !mc_lua_dialog_input_completion (lua, -1, control)
                    || !mc_lua_dialog_boolean (lua, -1, "complete_on_tab", &control->checked)
                    || (control->checked && control->options_count == 0))
                    goto fail;
            }
            if (control->type == MC_RUNTIME_DIALOG_CHECKBOX
                && !mc_lua_dialog_boolean (lua, -1, "value", &control->checked))
                goto fail;
            if (control->type == MC_RUNTIME_DIALOG_SELECT)
            {
                guint option_count, option_index;
                mc_runtime_dialog_option_t *options;

                lua_getfield (lua, -1, "options");
                option_count = lua_istable (lua, -1) ? (guint) lua_rawlen (lua, -1) : 0;
                if (option_count == 0 || option_count > MC_LUA_DIALOG_MAX_OPTIONS)
                    goto fail;
                options = g_new0 (mc_runtime_dialog_option_t, option_count);
                for (option_index = 0; option_index < option_count; option_index++)
                {
                    guint previous;
                    lua_rawgeti (lua, -1, (lua_Integer) option_index + 1);
                    if (!lua_istable (lua, -1)
                        || !mc_lua_dialog_string (lua, -1, "id", TRUE, MC_LUA_ID_MAX_LENGTH,
                                                  (char **) &options[option_index].id)
                        || !mc_lua_id_is_valid (options[option_index].id)
                        || !mc_lua_dialog_string (lua, -1, "label", TRUE, 4096,
                                                  (char **) &options[option_index].label))
                    {
                        lua_pop (lua, 1);
                        g_free (options);
                        goto fail;
                    }
                    for (previous = 0; previous < option_index; previous++)
                        if (strcmp (options[previous].id, options[option_index].id) == 0)
                        {
                            lua_pop (lua, 1);
                            g_free (options);
                            goto fail;
                        }
                    lua_pop (lua, 1);
                }
                lua_pop (lua, 1);
                control->options = options;
                control->options_count = option_count;
                if (control->value != NULL)
                {
                    gboolean found = FALSE;
                    for (option_index = 0; option_index < option_count; option_index++)
                        if (strcmp (control->value, options[option_index].id) == 0)
                            found = TRUE;
                    if (!found)
                        goto fail;
                }
            }
            if (control->type == MC_RUNTIME_DIALOG_BUTTON
                && (!mc_lua_dialog_boolean (lua, -1, "default", &control->default_button)
                    || !mc_lua_dialog_boolean (lua, -1, "cancel", &control->cancel_button)))
                goto fail;
        }
        lua_pop (lua, 1);
    }
    *result = controls;
    *count = length;
    return TRUE;
fail:
    lua_pop (lua, 1);
    spec->control_count -= length;
    mc_lua_dialog_controls_free (controls, length);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_check_controls (const mc_runtime_dialog_control_t *controls, guint count,
                              guint *buttons, guint *defaults, guint *cancels)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        const mc_runtime_dialog_control_t *control = &controls[i];
        if (control->type == MC_RUNTIME_DIALOG_BUTTON)
        {
            (*buttons)++;
            if (control->default_button)
                (*defaults)++;
            if (control->cancel_button)
                (*cancels)++;
        }
        if ((control->type == MC_RUNTIME_DIALOG_HBOX || control->type == MC_RUNTIME_DIALOG_VBOX)
            && !mc_lua_dialog_check_controls (control->controls, control->controls_count, buttons,
                                              defaults, cancels))
            return FALSE;
    }
    return *defaults <= 1 && *cancels <= 1;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** screens: a full-screen grid of widgets the script fills and drives *************************/
/* --------------------------------------------------------------------------------------------- */

#define MC_LUA_SCREEN_MT          "mc.ui.screen"
#define MC_LUA_SCREEN_MAX_ROWS    64
#define MC_LUA_SCREEN_MAX_CELLS   16
#define MC_LUA_SCREEN_MAX_KEYS    64
#define MC_LUA_SCREEN_MAX_COLUMNS 256
#define MC_LUA_SCREEN_MAX_PAGE    4096

/* A table cell: its columns and the rows function. */
typedef struct
{
    char *id;
    int rows_ref;
    mc_runtime_screen_table_t table;
    mc_runtime_screen_column_t *columns;
} mc_lua_screen_table_t;

typedef struct
{
    mc_lua_package_t *package;
    guint64 id;
    int spec_ref; /* the Lua spec table: the callbacks are looked up in it by name */
    int self_ref; /* keeps the userdata alive while the screen runs */
    mc_runtime_screen_t spec;
    mc_runtime_screen_row_t *rows;
    mc_runtime_screen_key_t *keys;
    GPtrArray *tables; /* mc_lua_screen_table_t * */
    gboolean running;
    gboolean closed;
} mc_lua_screen_t;

static mc_lua_screen_t *
mc_lua_screen_check (lua_State *lua, int index)
{
    return (mc_lua_screen_t *) luaL_checkudata (lua, index, MC_LUA_SCREEN_MT);
}

static void
mc_lua_screen_control_free (mc_runtime_dialog_control_t *control)
{
    g_free ((char *) control->id);
    g_free ((char *) control->text);
    g_free ((char *) control->label);
    g_free ((char *) control->value);
}

static void
mc_lua_screen_spec_free (mc_lua_screen_t *screen)
{
    guint i;

    g_free ((char *) screen->spec.title);
    g_free ((char *) screen->spec.status);
    g_free ((char *) screen->spec.help_file);
    g_free ((char *) screen->spec.help_node);
    g_free ((char *) screen->spec.focus);
    screen->spec.title = screen->spec.status = screen->spec.help_file = screen->spec.help_node =
        screen->spec.focus = NULL;
    for (i = 0; screen->rows != NULL && i < screen->spec.rows_count; i++)
    {
        mc_runtime_screen_cell_spec_t *cells =
            (mc_runtime_screen_cell_spec_t *) screen->rows[i].cells;
        guint j;

        for (j = 0; cells != NULL && j < screen->rows[i].cells_count; j++)
        {
            mc_lua_screen_control_free ((mc_runtime_dialog_control_t *) cells[j].control);
            g_free ((mc_runtime_dialog_control_t *) cells[j].control);
        }
        g_free (cells);
    }
    g_free (screen->rows);
    screen->rows = NULL;
    screen->spec.rows = NULL;
    screen->spec.rows_count = 0;
    for (i = 0; screen->keys != NULL && i < screen->spec.keys_count; i++)
    {
        g_free ((char *) screen->keys[i].key);
        g_free ((char *) screen->keys[i].label);
        g_free ((char *) screen->keys[i].action);
    }
    g_free (screen->keys);
    screen->keys = NULL;
    screen->spec.keys = NULL;
    screen->spec.keys_count = 0;
    for (i = 0; screen->tables != NULL && i < screen->tables->len; i++)
    {
        mc_lua_screen_table_t *table = g_ptr_array_index (screen->tables, i);
        guint c;

        for (c = 0; table->columns != NULL && c < table->table.columns_count; c++)
        {
            g_free ((char *) table->columns[c].id);
            g_free ((char *) table->columns[c].title);
        }
        g_free (table->columns);
        if (screen->package != NULL && screen->package->lua != NULL && table->rows_ref != LUA_NOREF)
            luaL_unref (screen->package->lua, LUA_REGISTRYINDEX, table->rows_ref);
        g_free (table->id);
        g_free (table);
    }
    if (screen->tables != NULL)
        g_ptr_array_free (screen->tables, TRUE);
    screen->tables = NULL;
}

static void
mc_lua_screen_release (mc_lua_screen_t *screen)
{
    lua_State *lua = screen->package != NULL ? screen->package->lua : NULL;

    mc_lua_screen_spec_free (screen);
    if (lua != NULL && screen->spec_ref != LUA_NOREF)
        luaL_unref (lua, LUA_REGISTRYINDEX, screen->spec_ref);
    screen->spec_ref = LUA_NOREF;
    if (screen->package != NULL && screen->package->runtime->screens != NULL)
        g_hash_table_remove (screen->package->runtime->screens, &screen->id);
    screen->closed = TRUE;
}

static int
mc_lua_screen_gc (lua_State *lua)
{
    mc_lua_screen_t *screen = mc_lua_screen_check (lua, 1);

    if (!screen->closed && !screen->running)
        mc_lua_screen_release (screen);
    return 0;
}

static mc_lua_screen_table_t *
mc_lua_screen_find_table (const mc_lua_screen_t *screen, const char *id)
{
    guint i;

    for (i = 0; id != NULL && screen->tables != NULL && i < screen->tables->len; i++)
    {
        mc_lua_screen_table_t *table = g_ptr_array_index (screen->tables, i);

        if (strcmp (table->id, id) == 0)
            return table;
    }
    return NULL;
}

/* The align of a column: "left" (default), "right", "center". */
static mc_runtime_screen_align_t
mc_lua_screen_align (lua_State *lua, int table)
{
    char *align = mc_lua_dup_table_string (lua, table, "align");
    mc_runtime_screen_align_t result = g_strcmp0 (align, "right") == 0
        ? MC_RUNTIME_SCREEN_ALIGN_RIGHT
        : g_strcmp0 (align, "center") == 0 ? MC_RUNTIME_SCREEN_ALIGN_CENTER
                                           : MC_RUNTIME_SCREEN_ALIGN_LEFT;

    g_free (align);
    return result;
}

/* A table cell: columns, row_count, page_size, and the rows function. */
static gboolean
mc_lua_screen_parse_table (lua_State *lua, int index, mc_lua_screen_t *screen,
                           mc_runtime_dialog_control_t *control)
{
    mc_lua_screen_table_t *table;
    guint i, count;

    lua_getfield (lua, index, "columns");
    count = lua_istable (lua, -1) ? (guint) lua_rawlen (lua, -1) : 0;
    if (count == 0 || count > MC_LUA_SCREEN_MAX_COLUMNS)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    table = g_new0 (mc_lua_screen_table_t, 1);
    table->id = g_strdup (control->id);
    table->rows_ref = LUA_NOREF;
    table->columns = g_new0 (mc_runtime_screen_column_t, count);
    table->table.columns_count = count;
    g_ptr_array_add (screen->tables, table);
    for (i = 0; i < count; i++)
    {
        mc_runtime_screen_column_t *column = &table->columns[i];
        char *type;

        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 2);
            return FALSE;
        }
        column->id = mc_lua_dup_table_string (lua, -1, "id");
        column->title = mc_lua_dup_table_string (lua, -1, "title");
        if (column->id == NULL)
            column->id = g_strdup_printf ("column%u", i + 1);
        if (column->title == NULL)
            column->title = g_strdup (column->id);
        column->align = mc_lua_screen_align (lua, -1);
        column->min_width = (guint) mc_lua_table_uint64_default (lua, -1, "min_width", 1);
        column->expands = mc_lua_table_boolean (lua, -1, "expands", FALSE);
        type = mc_lua_dup_table_string (lua, -1, "type");
        column->type = g_strcmp0 (type, "check") == 0 ? MC_RUNTIME_SCREEN_COLUMN_CHECK
                                                      : MC_RUNTIME_SCREEN_COLUMN_TEXT;
        g_free (type);
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);

    lua_getfield (lua, index, "row_count");
    table->table.row_count = lua_isinteger (lua, -1) && lua_tointeger (lua, -1) >= 0
        ? (gint64) lua_tointeger (lua, -1)
        : -1;
    lua_pop (lua, 1);
    table->table.page_size = (guint) mc_lua_table_uint64_default (lua, index, "page_size", 0);
    if (table->table.page_size > MC_LUA_SCREEN_MAX_PAGE)
        table->table.page_size = MC_LUA_SCREEN_MAX_PAGE;
    lua_getfield (lua, index, "rows");
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    table->rows_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    table->table.struct_size = sizeof (table->table);
    table->table.columns = table->columns;
    control->table = &table->table;
    return TRUE;
}

/* One cell of the grid: width or weight, and the control in it. */
static gboolean
mc_lua_screen_parse_cell (lua_State *lua, int index, mc_lua_screen_t *screen, GHashTable *ids,
                          guint number, mc_runtime_screen_cell_spec_t *cell)
{
    mc_runtime_dialog_control_t *control = g_new0 (mc_runtime_dialog_control_t, 1);
    char *type;
    gboolean ok = TRUE;

    cell->control = control;
    cell->width = (guint) mc_lua_table_uint64_default (lua, index, "width", 0);
    cell->weight = (guint) mc_lua_table_uint64_default (lua, index, "weight", 0);
    type = mc_lua_dup_table_string (lua, index, "type");
    control->id = mc_lua_dup_table_string (lua, index, "id");
    control->text = mc_lua_dup_table_string (lua, index, "text");
    control->label = mc_lua_dup_table_string (lua, index, "label");
    control->value = mc_lua_dup_table_string (lua, index, "value");
    control->checked = mc_lua_table_boolean (lua, index, "value", FALSE);
    if (control->id == NULL)
        control->id = g_strdup_printf ("cell%u", number);
    if (!mc_lua_id_is_valid (control->id) || g_hash_table_contains (ids, control->id))
        ok = FALSE;
    else
        g_hash_table_add (ids, (gpointer) control->id);
    if (g_strcmp0 (type, "label") == 0)
        control->type = MC_RUNTIME_DIALOG_LABEL;
    else if (g_strcmp0 (type, "status") == 0)
        control->type = MC_RUNTIME_DIALOG_STATUS;
    else if (g_strcmp0 (type, "text") == 0)
        control->type = MC_RUNTIME_DIALOG_TEXT;
    else if (g_strcmp0 (type, "separator") == 0)
        control->type = MC_RUNTIME_DIALOG_SEPARATOR;
    else if (g_strcmp0 (type, "input") == 0)
        control->type = MC_RUNTIME_DIALOG_INPUT;
    else if (g_strcmp0 (type, "checkbox") == 0)
        control->type = MC_RUNTIME_DIALOG_CHECKBOX;
    else if (g_strcmp0 (type, "table") == 0)
    {
        control->type = MC_RUNTIME_DIALOG_TABLE;
        if (ok && !mc_lua_screen_parse_table (lua, index, screen, control))
            ok = FALSE;
    }
    else
        ok = FALSE;
    g_free (type);
    return ok;
}

/* layout = { { height = n | weight = n, cell, cell, ... }, ... } */
static gboolean
mc_lua_screen_parse_layout (lua_State *lua, int index, mc_lua_screen_t *screen)
{
    guint i, count, number = 0;
    GHashTable *ids;
    gboolean ok = TRUE;

    count = lua_istable (lua, index) ? (guint) lua_rawlen (lua, index) : 0;
    if (count == 0 || count > MC_LUA_SCREEN_MAX_ROWS)
        return FALSE;
    screen->rows = g_new0 (mc_runtime_screen_row_t, count);
    screen->spec.rows = screen->rows;
    screen->spec.rows_count = count;
    ids = g_hash_table_new (g_str_hash, g_str_equal);
    for (i = 0; i < count && ok; i++)
    {
        mc_runtime_screen_row_t *row = &screen->rows[i];
        mc_runtime_screen_cell_spec_t *cells;
        guint j, cells_count;

        lua_rawgeti (lua, index, (lua_Integer) i + 1);
        cells_count = lua_istable (lua, -1) ? (guint) lua_rawlen (lua, -1) : 0;
        if (cells_count == 0 || cells_count > MC_LUA_SCREEN_MAX_CELLS)
        {
            ok = FALSE;
            lua_pop (lua, 1);
            break;
        }
        row->height = (guint) mc_lua_table_uint64_default (lua, -1, "height", 0);
        row->weight = (guint) mc_lua_table_uint64_default (lua, -1, "weight", 0);
        cells = g_new0 (mc_runtime_screen_cell_spec_t, cells_count);
        row->cells = cells;
        row->cells_count = cells_count;
        for (j = 0; j < cells_count && ok; j++)
        {
            lua_rawgeti (lua, -1, (lua_Integer) j + 1);
            if (!lua_istable (lua, -1)
                || !mc_lua_screen_parse_cell (lua, lua_gettop (lua), screen, ids, ++number,
                                              &cells[j]))
                ok = FALSE;
            lua_pop (lua, 1);
        }
        lua_pop (lua, 1);
    }
    g_hash_table_destroy (ids);
    return ok;
}

static gboolean
mc_lua_screen_parse_keys (lua_State *lua, int index, mc_lua_screen_t *screen)
{
    guint i, count;

    lua_getfield (lua, index, "keys");
    count = lua_istable (lua, -1) ? (guint) lua_rawlen (lua, -1) : 0;
    if (count > MC_LUA_SCREEN_MAX_KEYS)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    screen->keys = g_new0 (mc_runtime_screen_key_t, count + 1);
    screen->spec.keys = screen->keys;
    screen->spec.keys_count = count;
    for (i = 0; i < count; i++)
    {
        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 2);
            return FALSE;
        }
        screen->keys[i].key = mc_lua_dup_table_string (lua, -1, "key");
        screen->keys[i].label = mc_lua_dup_table_string (lua, -1, "label");
        screen->keys[i].action = mc_lua_dup_table_string (lua, -1, "action");
        lua_pop (lua, 1);
        if (screen->keys[i].key == NULL)
        {
            lua_pop (lua, 1);
            return FALSE;
        }
    }
    lua_pop (lua, 1);
    return TRUE;
}

/* The event table of a callback: the control the user is on, and for a table
   its row (from 0) and column (its id, and its index from 1). */
static void
mc_lua_screen_push_event (lua_State *lua, const mc_lua_screen_t *screen,
                          const mc_runtime_screen_request_t *request)
{
    const mc_lua_screen_table_t *table = mc_lua_screen_find_table (screen, request->control_id);

    lua_createtable (lua, 0, 6);
    if (request->control_id != NULL)
    {
        lua_pushstring (lua, request->control_id);
        lua_setfield (lua, -2, "control");
    }
    if (table != NULL && request->row >= 0)
    {
        lua_pushinteger (lua, (lua_Integer) request->row);
        lua_setfield (lua, -2, "row");
    }
    if (table != NULL && request->column >= 0
        && (guint) request->column < table->table.columns_count)
    {
        lua_pushstring (lua, table->columns[request->column].id);
        lua_setfield (lua, -2, "column");
        lua_pushinteger (lua, request->column + 1);
        lua_setfield (lua, -2, "column_index");
    }
}

/* Call spec.<name>(screen, ...) with nargs arguments already pushed; FALSE
   when there is no such callback or it failed.  The result stays on the stack
   when *called. */
static gboolean
mc_lua_screen_call (mc_lua_screen_t *screen, const char *name, int nargs, gboolean *called)
{
    lua_State *lua = screen->package->lua;
    int base = lua_gettop (lua) - nargs;

    *called = FALSE;
    lua_rawgeti (lua, LUA_REGISTRYINDEX, screen->spec_ref);
    lua_getfield (lua, -1, name);
    lua_remove (lua, -2);
    if (!lua_isfunction (lua, -1))
    {
        lua_settop (lua, base);
        return FALSE;
    }
    /* the callback under the arguments: fn, screen, args... */
    lua_insert (lua, base + 1);
    lua_rawgeti (lua, LUA_REGISTRYINDEX, screen->self_ref);
    lua_insert (lua, base + 2);
    screen->package->callback_depth++;
    if (lua_pcall (lua, nargs + 1, 1, 0) != LUA_OK)
    {
        mc_lua_report_error (screen->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                             "Lua screen callback failed");
        screen->package->callback_depth--;
        lua_settop (lua, base);
        return FALSE;
    }
    screen->package->callback_depth--;
    *called = TRUE;
    return TRUE;
}

/* A callback's result: true or mc.CONSUME means handled; { close = true } closes. */
static void
mc_lua_screen_read_result (lua_State *lua, mc_runtime_screen_response_t *response)
{
    if (lua_istable (lua, -1))
    {
        response->close = mc_lua_table_boolean (lua, -1, "close", FALSE);
        response->handled = mc_lua_table_boolean (lua, -1, "handled", TRUE);
    }
    else if (lua_isboolean (lua, -1))
        response->handled = lua_toboolean (lua, -1);
    else if (lua_isinteger (lua, -1))
        response->handled = lua_tointeger (lua, -1) != 0;
    else
        response->handled = FALSE;
    lua_pop (lua, 1);
}

/* rows(first, count) -> { { cell, ... }, ... }; a cell is a string, a number, or
   { text = ..., color = "fg;bg" }. */
static gboolean
mc_lua_screen_rows (mc_lua_screen_t *screen, const mc_runtime_screen_request_t *request,
                    mc_runtime_screen_response_t *response)
{
    lua_State *lua = screen->package->lua;
    mc_lua_screen_table_t *table = mc_lua_screen_find_table (screen, request->control_id);
    mc_runtime_screen_cell_t *cells;
    guint rows, ncols, r, c;

    if (table == NULL || table->rows_ref == LUA_NOREF)
        return FALSE;
    ncols = table->table.columns_count;
    lua_rawgeti (lua, LUA_REGISTRYINDEX, table->rows_ref);
    lua_pushinteger (lua, (lua_Integer) request->first);
    lua_pushinteger (lua, (lua_Integer) request->count);
    screen->package->callback_depth++;
    if (lua_pcall (lua, 2, 1, 0) != LUA_OK)
    {
        mc_lua_report_error (screen->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                             "Lua screen rows callback failed");
        screen->package->callback_depth--;
        return FALSE;
    }
    screen->package->callback_depth--;
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    rows = (guint) lua_rawlen (lua, -1);
    if (rows > request->count)
        rows = request->count;
    if (rows > MC_LUA_SCREEN_MAX_PAGE)
        rows = MC_LUA_SCREEN_MAX_PAGE;
    cells = g_new0 (mc_runtime_screen_cell_t, (gsize) rows * ncols + 1);
    for (r = 0; r < rows; r++)
    {
        lua_rawgeti (lua, -1, (lua_Integer) r + 1);
        if (lua_istable (lua, -1))
            for (c = 0; c < ncols; c++)
            {
                mc_runtime_screen_cell_t *cell = &cells[r * ncols + c];

                lua_rawgeti (lua, -1, (lua_Integer) c + 1);
                if (lua_istable (lua, -1))
                {
                    lua_getfield (lua, -1, "text");
                    cell->text = g_strdup (lua_isstring (lua, -1) ? lua_tostring (lua, -1) : "");
                    lua_pop (lua, 1);
                    cell->color = mc_lua_dup_table_string (lua, -1, "color");
                }
                else
                    cell->text = g_strdup (lua_isstring (lua, -1) ? lua_tostring (lua, -1) : "");
                lua_pop (lua, 1);
            }
        else
            for (c = 0; c < ncols; c++)
                cells[r * ncols + c].text = g_strdup ("");
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    response->cells = cells;
    response->rows_count = rows;
    response->columns_count = ncols;
    return TRUE;
}

static void
mc_lua_screen_response_free (mc_runtime_plugin_context_t *context,
                             mc_runtime_screen_response_t *response)
{
    guint i;

    (void) context;
    if (response == NULL || response->cells == NULL)
        return;
    for (i = 0; i < response->rows_count * response->columns_count; i++)
    {
        g_free ((char *) response->cells[i].text);
        g_free ((char *) response->cells[i].color);
    }
    g_free ((mc_runtime_screen_cell_t *) response->cells);
    response->cells = NULL;
}

static gboolean
mc_lua_screen_dispatch (mc_runtime_plugin_context_t *context, guint64 screen_id,
                        mc_runtime_screen_operation_t operation,
                        const mc_runtime_screen_request_t *request,
                        mc_runtime_screen_response_t *response, const char **error)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    mc_lua_screen_t *screen;
    lua_State *lua;
    gboolean called = FALSE;

    (void) context;
    if (runtime == NULL || runtime->screens == NULL || request == NULL || response == NULL)
        return FALSE;
    screen = g_hash_table_lookup (runtime->screens, &screen_id);
    if (screen == NULL || screen->closed || screen->package->lua == NULL)
    {
        if (error != NULL)
            *error = "closed";
        return FALSE;
    }
    lua = screen->package->lua;

    switch (operation)
    {
    case MC_RUNTIME_SCREEN_ROWS:
        return mc_lua_screen_rows (screen, request, response);

    case MC_RUNTIME_SCREEN_SET_CHECKED:
        mc_lua_screen_push_event (lua, screen, request);
        lua_pushboolean (lua, request->value);
        lua_setfield (lua, -2, "value");
        if (mc_lua_screen_call (screen, "on_check", 1, &called) && called)
            mc_lua_screen_read_result (lua, response);
        return TRUE;

    case MC_RUNTIME_SCREEN_KEY:
        mc_lua_screen_push_event (lua, screen, request);
        lua_createtable (lua, 0, 2);
        lua_pushstring (lua, request->key_name != NULL ? request->key_name : "");
        lua_setfield (lua, -2, "name");
        lua_pushinteger (lua, request->key);
        lua_setfield (lua, -2, "code");
        lua_setfield (lua, -2, "key");
        if (mc_lua_screen_call (screen, "on_key", 1, &called) && called)
            mc_lua_screen_read_result (lua, response);
        return TRUE;

    case MC_RUNTIME_SCREEN_ENTER:
        mc_lua_screen_push_event (lua, screen, request);
        if (mc_lua_screen_call (screen, "on_enter", 1, &called) && called)
            mc_lua_screen_read_result (lua, response);
        return TRUE;

    case MC_RUNTIME_SCREEN_ACTION:
        lua_pushstring (lua, request->action != NULL ? request->action : "");
        mc_lua_screen_push_event (lua, screen, request);
        if (mc_lua_screen_call (screen, "on_action", 2, &called) && called)
            mc_lua_screen_read_result (lua, response);
        return TRUE;

    case MC_RUNTIME_SCREEN_ROW_CHANGED:
        mc_lua_screen_push_event (lua, screen, request);
        if (mc_lua_screen_call (screen, "on_row", 1, &called) && called)
            mc_lua_screen_read_result (lua, response);
        return TRUE;

    case MC_RUNTIME_SCREEN_RESIZE:
        lua_pushinteger (lua, request->columns);
        lua_pushinteger (lua, request->lines);
        if (mc_lua_screen_call (screen, "on_resize", 2, &called) && called)
            lua_pop (lua, 1);
        return TRUE;

    case MC_RUNTIME_SCREEN_CLOSE:
        if (mc_lua_screen_call (screen, "on_close", 0, &called) && called)
            lua_pop (lua, 1);
        return TRUE;

    default:
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
}

/** @lua mc.ui.screen(spec) -> screen|nil, error? @capability ui @mutation yes @summary Describe
 * a full-screen grid of widgets: a title, a status line on top, keys for the button bar at the
 * bottom, and between them spec.layout, rows of cells; a row is height = n lines or weight = n,
 * a cell is width = n columns or weight = n and holds one control: label, status, text,
 * separator, input, checkbox, or a table with columns and rows(first, count).  help =
 * {file, node}; the callbacks are on_key, on_enter, on_action, on_check, on_row, on_resize,
 * on_close.  palette = "editor" gives the whole screen the editor skin colors; the default is
 * the dialog palette.  A text control is read-only: it scrolls both ways and does not wrap.
 * screen:run() shows it and returns when it is closed. */
static int
mc_lua_ui_screen (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_lua_screen_t *screen;
    char *palette;
    guint64 *key;
    gboolean ok;

    luaL_checktype (lua, 1, LUA_TTABLE);
    if (package->runtime->screens == NULL)
        package->runtime->screens =
            g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);

    screen = (mc_lua_screen_t *) lua_newuserdata (lua, sizeof (*screen));
    memset (screen, 0, sizeof (*screen));
    screen->package = package;
    screen->spec_ref = screen->self_ref = LUA_NOREF;
    screen->tables = g_ptr_array_new ();
    luaL_getmetatable (lua, MC_LUA_SCREEN_MT);
    lua_setmetatable (lua, -2);

    screen->spec.struct_size = sizeof (screen->spec);
    screen->spec.title = mc_lua_dup_table_string (lua, 1, "title");
    screen->spec.status = mc_lua_dup_table_string (lua, 1, "status");
    screen->spec.focus = mc_lua_dup_table_string (lua, 1, "focus");
    palette = mc_lua_dup_table_string (lua, 1, "palette");
    if (palette == NULL || strcmp (palette, "dialog") == 0)
        screen->spec.palette = MC_RUNTIME_SCREEN_PALETTE_DIALOG;
    else if (strcmp (palette, "editor") == 0)
        screen->spec.palette = MC_RUNTIME_SCREEN_PALETTE_EDITOR;
    else
    {
        g_free (palette);
        mc_lua_screen_release (screen);
        return mc_lua_return_error (lua, "invalid_screen");
    }
    g_free (palette);
    lua_getfield (lua, 1, "help");
    if (lua_istable (lua, -1))
    {
        screen->spec.help_file = mc_lua_dup_help_file (lua, -1, package);
        screen->spec.help_node = mc_lua_dup_table_string (lua, -1, "node");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "layout");
    ok = mc_lua_screen_parse_layout (lua, lua_gettop (lua), screen);
    lua_pop (lua, 1);
    if (!ok || !mc_lua_screen_parse_keys (lua, 1, screen))
    {
        mc_lua_screen_release (screen);
        return mc_lua_return_error (lua, "invalid_screen");
    }

    lua_pushvalue (lua, 1);
    screen->spec_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    screen->id = ++package->runtime->next_screen_id;
    key = g_new (guint64, 1);
    *key = screen->id;
    g_hash_table_insert (package->runtime->screens, key, screen);
    return 1;
}

/** @lua screen:run() -> boolean|nil, error? @capability ui @mutation yes @summary Show the
 * screen and return when it is closed: by F10 or Esc, by the "close" action, by screen:close(),
 * or by a callback returning { close = true }. */
static int
mc_lua_screen_run (lua_State *lua)
{
    mc_lua_screen_t *screen = mc_lua_screen_check (lua, 1);
    mc_lua_package_t *package = screen->package;
    mc_runtime_screen_descriptor_t descriptor = { 0 };
    const char *error = NULL;
    gboolean ok;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (screen->closed)
        return mc_lua_return_error (lua, "closed");
    if (screen->running)
        return mc_lua_return_error (lua, "already_running");
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_SCREEN_SIZE)
        || package->runtime->host->screen_run == NULL)
        return mc_lua_not_ready (lua);

    descriptor.struct_size = sizeof (descriptor);
    descriptor.api_version = 1;
    descriptor.screen_id = screen->id;
    descriptor.spec = &screen->spec;
    descriptor.dispatch = mc_lua_screen_dispatch;
    descriptor.response_free = mc_lua_screen_response_free;

    lua_pushvalue (lua, 1);
    screen->self_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    screen->running = TRUE;
    ok = package->runtime->host->screen_run (package->runtime->context, &descriptor, &error);
    screen->running = FALSE;
    luaL_unref (lua, LUA_REGISTRYINDEX, screen->self_ref);
    screen->self_ref = LUA_NOREF;
    if (!ok)
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    lua_pushboolean (lua, TRUE);
    return 1;
}

static int
mc_lua_screen_send_patch (lua_State *lua, mc_lua_screen_t *screen, const char *control,
                          mc_runtime_screen_patch_t *patch)
{
    mc_lua_package_t *package = screen->package;
    const char *error = NULL;
    gboolean ok;

    if (!screen->running)
        return mc_lua_return_error (lua, "not_running");
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_SCREEN_SIZE)
        || package->runtime->host->screen_update == NULL)
        return mc_lua_not_ready (lua);
    patch->struct_size = sizeof (*patch);
    patch->control_id = control;
    ok = package->runtime->host->screen_update (package->runtime->context, screen->id, patch,
                                                &error);
    if (!ok)
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    lua_pushboolean (lua, TRUE);
    return 1;
}

/** @lua screen:update(control, patch) -> boolean|nil, error? @capability ui @mutation yes
 * @summary Change a running screen's control: patch.text for a label, status or text cell,
 * patch.value for an input or checkbox, and for a table patch.rows (a new rows function),
 * patch.row_count, patch.invalidate (drop the rows fetched so far) and patch.row (the current
 * row, from 0). */
static int
mc_lua_screen_update (lua_State *lua)
{
    mc_lua_screen_t *screen = mc_lua_screen_check (lua, 1);
    mc_runtime_screen_patch_t patch = { 0 };
    const char *control = luaL_checkstring (lua, 2);
    int result;

    luaL_checktype (lua, 3, LUA_TTABLE);
    patch.text = mc_lua_dup_table_string (lua, 3, "text");
    lua_getfield (lua, 3, "value");
    if (lua_isboolean (lua, -1))
        patch.value = g_strdup (lua_toboolean (lua, -1) ? "true" : "false");
    else if (lua_isstring (lua, -1))
        patch.value = g_strdup (lua_tostring (lua, -1));
    lua_pop (lua, 1);
    lua_getfield (lua, 3, "row_count");
    if (lua_isinteger (lua, -1))
    {
        patch.has_row_count = TRUE;
        patch.row_count = (gint64) lua_tointeger (lua, -1);
    }
    else if (!lua_isnil (lua, -1))
    {
        /* any other value: the count is unknown again */
        patch.has_row_count = TRUE;
        patch.row_count = -1;
    }
    lua_pop (lua, 1);
    lua_getfield (lua, 3, "row");
    if (lua_isinteger (lua, -1))
    {
        patch.has_row = TRUE;
        patch.row = (gint64) lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);
    patch.invalidate = mc_lua_table_boolean (lua, 3, "invalidate", FALSE);

    /* a new rows function: the rows fetched so far are stale */
    lua_getfield (lua, 3, "rows");
    if (lua_isfunction (lua, -1))
    {
        mc_lua_screen_table_t *table = mc_lua_screen_find_table (screen, control);

        if (table != NULL)
        {
            if (table->rows_ref != LUA_NOREF)
                luaL_unref (lua, LUA_REGISTRYINDEX, table->rows_ref);
            table->rows_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
            patch.invalidate = TRUE;
        }
        else
            lua_pop (lua, 1);
    }
    else
        lua_pop (lua, 1);

    result = mc_lua_screen_send_patch (lua, screen, control, &patch);
    g_free ((char *) patch.text);
    g_free ((char *) patch.value);
    return result;
}

/** @lua screen:status(text) -> boolean|nil, error? @capability ui @mutation yes @summary Change
 * the status line of a running screen. */
static int
mc_lua_screen_status (lua_State *lua)
{
    mc_lua_screen_t *screen = mc_lua_screen_check (lua, 1);
    mc_runtime_screen_patch_t patch = { 0 };

    patch.text = luaL_checkstring (lua, 2);
    return mc_lua_screen_send_patch (lua, screen, "", &patch);
}

/** @lua screen:close() -> boolean|nil, error? @capability ui @mutation yes @summary Close a
 * running screen; screen:run() then returns. */
static int
mc_lua_screen_close (lua_State *lua)
{
    mc_lua_screen_t *screen = mc_lua_screen_check (lua, 1);
    mc_lua_package_t *package = screen->package;
    const char *error = NULL;

    if (!screen->running)
        return mc_lua_return_error (lua, "not_running");
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_SCREEN_SIZE)
        || package->runtime->host->screen_close == NULL)
        return mc_lua_not_ready (lua);
    if (!package->runtime->host->screen_close (package->runtime->context, screen->id, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    lua_pushboolean (lua, TRUE);
    return 1;
}

static int
mc_lua_screen_index (lua_State *lua)
{
    const char *method = luaL_checkstring (lua, 2);

    (void) mc_lua_screen_check (lua, 1);
    if (strcmp (method, "run") == 0)
        lua_pushcfunction (lua, mc_lua_screen_run);
    else if (strcmp (method, "update") == 0 || strcmp (method, "set") == 0)
        lua_pushcfunction (lua, mc_lua_screen_update);
    else if (strcmp (method, "status") == 0)
        lua_pushcfunction (lua, mc_lua_screen_status);
    else if (strcmp (method, "close") == 0)
        lua_pushcfunction (lua, mc_lua_screen_close);
    else
        lua_pushnil (lua);
    return 1;
}

/** @lua mc.settings(handler) -> true|nil, error? @mutation yes
 * @errors invalid_settings_handler
 * @summary Register the dialog this package shows when its settings are asked
 * for in Manage Plugins.  The handler takes no argument and returns nothing;
 * it owns the dialog and whatever it keeps. */
static int
mc_lua_settings (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);

    if (package == NULL)
        return luaL_error (lua, "no Lua script context");
    if (!lua_isfunction (lua, 1))
        return mc_lua_return_error (lua, "invalid_settings_handler");

    if (package->settings_ref != LUA_NOREF)
        luaL_unref (lua, LUA_REGISTRYINDEX, package->settings_ref);
    lua_pushvalue (lua, 1);
    package->settings_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.ui.dialog(spec) -> DialogResult|nil, error? @capability ui @mutation yes
 * @summary Show a declarative native modal dialog.  spec.help = {file, node} is what F1 opens over
 * it; a relative file is taken from the script's directory, and a spec without a node has no
 * help. */
static int
mc_lua_ui_dialog (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_lua_dialog_spec_t spec = { { NULL, 0, 0, FALSE, FALSE, NULL, 0, NULL, NULL }, 0 };
    GHashTable *ids;
    mc_runtime_dialog_result_t result = { NULL, NULL, 0 };
    const char *error = NULL;
    guint buttons = 0, defaults = 0, cancels = 0, i;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (package->active_event == MC_RUNTIME_EVENT_STARTUP
        || package->active_event == MC_RUNTIME_EVENT_SHUTDOWN)
        return mc_lua_return_error (lua, "forbidden_in_phase");
    if (!lua_istable (lua, 1))
        return mc_lua_return_error (lua, "invalid_dialog");
    if (!mc_lua_dialog_string (lua, 1, "title", TRUE, 256, (char **) &spec.dialog.title)
        || !mc_lua_dialog_uint (lua, 1, "width", &spec.dialog.width, &spec.dialog.has_width)
        || !mc_lua_dialog_uint (lua, 1, "height", &spec.dialog.height, &spec.dialog.has_height))
        goto invalid;
    lua_getfield (lua, 1, "help");
    if (lua_istable (lua, -1))
    {
        spec.dialog.help_file = mc_lua_dup_help_file (lua, -1, package);
        spec.dialog.help_node = mc_lua_dup_table_string (lua, -1, "node");
    }
    lua_pop (lua, 1);
    ids = g_hash_table_new (g_str_hash, g_str_equal);
    lua_getfield (lua, 1, "controls");
    if (!mc_lua_dialog_parse_controls (lua, lua_gettop (lua), 1, &spec, ids,
                                       (mc_runtime_dialog_control_t **) &spec.dialog.controls,
                                       &spec.dialog.controls_count))
    {
        lua_pop (lua, 1);
        g_hash_table_destroy (ids);
        goto invalid;
    }
    lua_pop (lua, 1);
    g_hash_table_destroy (ids);
    if (!mc_lua_dialog_check_controls (spec.dialog.controls, spec.dialog.controls_count, &buttons,
                                       &defaults, &cancels)
        || buttons == 0)
        goto invalid;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_DIALOG_SIZE)
        || package->runtime->host->ui_dialog == NULL
        || package->runtime->host->dialog_result_free == NULL)
        goto not_ready;
    if (!package->runtime->host->ui_dialog (package->runtime->context, &spec.dialog, &result,
                                            &error))
    {
        mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                     spec.dialog.controls_count);
        g_free ((char *) spec.dialog.title);
        g_free ((char *) spec.dialog.help_file);
        g_free ((char *) spec.dialog.help_node);
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");
    }
    lua_newtable (lua);
    lua_pushstring (lua, result.button_id != NULL ? result.button_id : "");
    lua_setfield (lua, -2, "button");
    lua_newtable (lua);
    for (i = 0; i < result.values_count; i++)
    {
        if (result.values[i].is_boolean)
            lua_pushboolean (lua, result.values[i].checked);
        else
            lua_pushstring (lua, result.values[i].value != NULL ? result.values[i].value : "");
        lua_setfield (lua, -2, result.values[i].id);
    }
    lua_setfield (lua, -2, "values");
    package->runtime->host->dialog_result_free (package->runtime->context, &result);
    mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                 spec.dialog.controls_count);
    g_free ((char *) spec.dialog.title);
    g_free ((char *) spec.dialog.help_file);
    g_free ((char *) spec.dialog.help_node);
    return 1;
not_ready:
    mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                 spec.dialog.controls_count);
    g_free ((char *) spec.dialog.title);
    g_free ((char *) spec.dialog.help_file);
    g_free ((char *) spec.dialog.help_node);
    return mc_lua_not_ready (lua);
invalid:
    mc_lua_log (package, "error", "invalid Lua dialog specification");
    mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                 spec.dialog.controls_count);
    g_free ((char *) spec.dialog.title);
    g_free ((char *) spec.dialog.help_file);
    g_free ((char *) spec.dialog.help_node);
    return mc_lua_return_error (lua, "invalid_dialog");
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.ui.indicator(spec) -> boolean|nil, error? @capability ui @mutation yes @summary Set or
 * replace a package-owned persistent UI indicator. */
static int
mc_lua_ui_indicator (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *id;
    const char *area = "editor";
    const char *text;
    lua_Integer priority = 0;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    luaL_checktype (lua, 1, LUA_TTABLE);

    lua_getfield (lua, 1, "id");
    id = luaL_checkstring (lua, -1);
    lua_pop (lua, 1);
    lua_getfield (lua, 1, "area");
    if (!lua_isnil (lua, -1))
        area = luaL_checkstring (lua, -1);
    lua_pop (lua, 1);
    lua_getfield (lua, 1, "text");
    text = luaL_checkstring (lua, -1);
    lua_pop (lua, 1);
    lua_getfield (lua, 1, "priority");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isinteger (lua, -1))
            return luaL_error (lua, "indicator.priority must be an integer");
        priority = lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);

    if (!mc_lua_id_is_valid (id) || strcmp (area, "editor") != 0 || text[0] == '\0'
        || strlen (text) > 128 || !g_utf8_validate (text, -1, NULL) || priority < -100000
        || priority > 100000)
        return mc_lua_return_error (lua, "invalid_indicator");

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI,
                                     MC_LUA_HOST_API_INDICATORS_SIZE)
        || package->runtime->host->ui_indicator_set == NULL
        || !package->runtime->host->ui_indicator_set (package->runtime->context, package->id, area,
                                                      id, text, (gint) priority, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.ui.indicator_clear(id, area?) -> boolean|nil, error? @capability ui @mutation yes
 * @summary Remove a package-owned UI indicator. */
static int
mc_lua_ui_indicator_clear (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *id;
    const char *area;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    id = luaL_checkstring (lua, 1);
    area = luaL_optstring (lua, 2, "editor");

    if (!mc_lua_id_is_valid (id) || strcmp (area, "editor") != 0)
        return mc_lua_return_error (lua, "invalid_indicator");

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI,
                                     MC_LUA_HOST_API_INDICATORS_SIZE)
        || package->runtime->host->ui_indicator_clear == NULL
        || !package->runtime->host->ui_indicator_clear (package->runtime->context, package->id,
                                                        area, id, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.ui.status(text) -> boolean|nil, error? @capability ui @mutation yes @summary Display
 * transient text in the status line of the editor, in front of the indicators. */
static int
mc_lua_ui_status (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *text;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    text = luaL_checkstring (lua, 1);

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_UI_SIZE)
        || package->runtime->host->ui_status == NULL
        || !package->runtime->host->ui_status (package->runtime->context, text))
        return mc_lua_not_ready (lua);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.ui.message(title, text) -> boolean|nil, error? @capability ui @mutation yes @summary
 * Show a native informational message box. */
static int
mc_lua_ui_message (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *title;
    const char *text;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    title = luaL_checkstring (lua, 1);
    text = luaL_checkstring (lua, 2);

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_UI_SIZE)
        || package->runtime->host->ui_message == NULL
        || !package->runtime->host->ui_message (package->runtime->context, title, text))
        return mc_lua_not_ready (lua);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** The string option @name of the options table at index 2, or NULL. */
static const char *
mc_lua_syntax_option (lua_State *lua, const char *name)
{
    const char *value = NULL;

    lua_getfield (lua, 2, name);
    if (!lua_isnil (lua, -1))
    {
        if (lua_type (lua, -1) != LUA_TSTRING)
            luaL_error (lua, "options.%s must be a string", name);
        // the options table keeps the string alive
        value = lua_tostring (lua, -1);
    }
    lua_pop (lua, 1);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.tty.info(section?) -> table|nil, error? @capability tty @mutation no
 * @summary What the terminal shows and what the skin paints a section with.  The section is
 * named the way the skin names it ("editor", "dialog"); the default is the core.  Returns
 * { colors = 256, fg = "white", bg = "black" }, colors being 16, 256 or 16777216 for true
 * color, and fg and bg the color names of the skin, nil when it names none. */
static int
mc_lua_tty_info (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_tty_info_t info = { .struct_size = sizeof (info) };
    const char *section = NULL;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (!lua_isnoneornil (lua, 1))
        section = luaL_checkstring (lua, 1);

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_TTY, MC_LUA_HOST_API_TTY_SIZE)
        || package->runtime->host->tty_info == NULL)
        return mc_lua_not_ready (lua);

    if (!package->runtime->host->tty_info (package->runtime->context, section, &info, &error))
    {
        lua_pushnil (lua);
        lua_pushstring (lua, error != NULL ? error : "tty_info_failed");
        return 2;
    }

    lua_createtable (lua, 0, 3);
    lua_pushinteger (lua, (lua_Integer) info.colors);
    lua_setfield (lua, -2, "colors");
    if (info.fg != NULL)
    {
        lua_pushstring (lua, info.fg);
        lua_setfield (lua, -2, "fg");
    }
    if (info.bg != NULL)
    {
        lua_pushstring (lua, info.bg);
        lua_setfield (lua, -2, "bg");
    }
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.syntax.scan(text, options?) -> table|nil, error? @capability syntax @mutation no
 * @summary Color text with the syntax rules of the editor.  options.type names the rule set the
 * way the Syntax file does ("C Program"), options.filename picks it by name; without both, the
 * first line of the text decides.  Returns { type = "C Program", colors = { { fg = "yellow",
 * bg = nil, attrs = "bold" } }, runs = { { offset = 1, length = 6, color = 1 } } }, offsets
 * counting bytes from one and color indexing colors. */
static int
mc_lua_syntax_scan (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_syntax_result_t result;
    const char *text;
    size_t text_length = 0;
    const char *type = NULL;
    const char *filename = NULL;
    const char *error = NULL;
    gsize i;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    text = luaL_checklstring (lua, 1, &text_length);
    if (!lua_isnoneornil (lua, 2))
    {
        luaL_checktype (lua, 2, LUA_TTABLE);
        type = mc_lua_syntax_option (lua, "type");
        filename = mc_lua_syntax_option (lua, "filename");
    }

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_SYNTAX,
                                     MC_LUA_HOST_API_SYNTAX_SIZE)
        || package->runtime->host->syntax_scan == NULL)
        return mc_lua_not_ready (lua);

    memset (&result, 0, sizeof (result));
    if (!package->runtime->host->syntax_scan (package->runtime->context, text, (gsize) text_length,
                                              type, filename, &result, &error))
    {
        lua_pushnil (lua);
        lua_pushstring (lua, error != NULL ? error : "syntax_scan_failed");
        return 2;
    }

    lua_createtable (lua, 0, 3);
    if (result.type != NULL)
    {
        lua_pushstring (lua, result.type);
        lua_setfield (lua, -2, "type");
    }

    lua_createtable (lua, (int) result.colors_count, 0);
    for (i = 0; i < result.colors_count; i++)
    {
        lua_createtable (lua, 0, 3);
        if (result.colors[i].fg != NULL)
        {
            lua_pushstring (lua, result.colors[i].fg);
            lua_setfield (lua, -2, "fg");
        }
        if (result.colors[i].bg != NULL)
        {
            lua_pushstring (lua, result.colors[i].bg);
            lua_setfield (lua, -2, "bg");
        }
        if (result.colors[i].attrs != NULL)
        {
            lua_pushstring (lua, result.colors[i].attrs);
            lua_setfield (lua, -2, "attrs");
        }
        lua_rawseti (lua, -2, (int) i + 1);
    }
    lua_setfield (lua, -2, "colors");

    lua_createtable (lua, (int) result.runs_count, 0);
    for (i = 0; i < result.runs_count; i++)
    {
        lua_createtable (lua, 0, 3);
        lua_pushinteger (lua, (lua_Integer) result.runs[i].offset + 1);
        lua_setfield (lua, -2, "offset");
        lua_pushinteger (lua, (lua_Integer) result.runs[i].length);
        lua_setfield (lua, -2, "length");
        lua_pushinteger (lua, (lua_Integer) result.runs[i].color + 1);
        lua_setfield (lua, -2, "color");
        lua_rawseti (lua, -2, (int) i + 1);
    }
    lua_setfield (lua, -2, "runs");

    if (package->runtime->host->syntax_result_free != NULL)
        package->runtime->host->syntax_result_free (package->runtime->context, &result);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.ui.text_width(text) -> integer|nil, error? @capability ui @mutation no @summary Measure
 * UTF-8 text using terminal display columns. */
static int
mc_lua_ui_text_width (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *text;
    size_t text_length;
    guint width;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    text = luaL_checklstring (lua, 1, &text_length);
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI,
                                     MC_LUA_HOST_API_UI_TEXT_WIDTH_SIZE)
        || package->runtime->host->ui_text_width == NULL
        || !package->runtime->host->ui_text_width (package->runtime->context, text, text_length,
                                                   &width, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");
    lua_pushinteger (lua, (lua_Integer) width);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mc_lua_module_filename (const char *module_name)
{
    char *filename;
    char *cursor;
    char *result;

    filename = g_strdup (module_name);
    for (cursor = filename; *cursor != '\0'; cursor++)
        if (*cursor == '.')
            *cursor = G_DIR_SEPARATOR;

    result = g_strconcat (filename, ".lua", (char *) NULL);
    g_free (filename);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_require (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *module_name = luaL_checkstring (lua, 1);
    char *filename;
    char *package_module;
    char *shared_module;
    const char *module_path = NULL;
    char *message;

    if (package == NULL || package->runtime == NULL)
        return luaL_error (lua, "no Lua script context");

    if (!mc_lua_module_name_is_valid (module_name))
        return luaL_error (lua, "invalid Lua module name '%s'", module_name);

    lua_getfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_MODULES);
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        lua_newtable (lua);
        lua_pushvalue (lua, -1);
        lua_setfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_MODULES);
    }

    lua_getfield (lua, -1, module_name);
    if (!lua_isnil (lua, -1))
    {
        if (lua_islightuserdata (lua, -1)
            && lua_touserdata (lua, -1) == &mc_lua_module_loading_sentinel)
        {
            lua_pop (lua, 2);
            return luaL_error (lua, "circular require for module '%s'", module_name);
        }
        lua_remove (lua, -2);
        return 1;
    }
    lua_pop (lua, 1);

    filename = mc_lua_module_filename (module_name);
    package_module = g_build_filename (package->root, "lib", filename, (char *) NULL);
    shared_module = g_build_filename (package->origin == MC_LUA_PACKAGE_USER
                                          ? package->runtime->user_modules_dir
                                          : mc_lua_system_modules_dir (),
                                      filename, (char *) NULL);
    g_free (filename);

    if (mc_lua_file_is_trusted_under (package->root, package_module))
        module_path = package_module;
    else if (mc_lua_file_is_trusted_under (package->origin == MC_LUA_PACKAGE_USER
                                               ? package->runtime->user_modules_dir
                                               : mc_lua_system_modules_dir (),
                                           shared_module))
        module_path = shared_module;

    if (module_path == NULL)
    {
        g_free (package_module);
        g_free (shared_module);
        lua_pop (lua, 1);
        return luaL_error (lua, "module '%s' not found", module_name);
    }

    lua_pushlightuserdata (lua, &mc_lua_module_loading_sentinel);
    lua_setfield (lua, -2, module_name);

    if (luaL_loadfile (lua, module_path) != LUA_OK || lua_pcall (lua, 0, 1, 0) != LUA_OK)
    {
        message = g_strdup (lua_tostring (lua, -1));
        lua_pop (lua, 1);
        lua_pushnil (lua);
        lua_setfield (lua, -2, module_name);
        lua_pop (lua, 1);
        g_free (package_module);
        g_free (shared_module);
        return luaL_error (lua, "could not load module '%s': %s", module_name,
                           message != NULL ? message : "Lua error");
    }

    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        lua_pushboolean (lua, TRUE);
    }

    lua_pushvalue (lua, -1);
    lua_setfield (lua, -3, module_name);
    lua_remove (lua, -2);
    g_free (package_module);
    g_free (shared_module);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */
/* The services plugins offer one another: a table of Lua is the a{sv} of the call, and the a{sv}
   of the answer and of a signal a table again. */

static GVariant *mc_lua_to_variant (lua_State *lua, int index, int depth, const char **error);

/* --------------------------------------------------------------------------------------------- */

/* A table with the keys 1 to n and no other is a list; an empty table is a dictionary */
static gboolean
mc_lua_table_is_list (lua_State *lua, int index)
{
    const lua_Unsigned len = lua_rawlen (lua, index);
    lua_Unsigned count = 0;

    if (len == 0)
        return FALSE;

    lua_pushnil (lua);
    while (lua_next (lua, index) != 0)
    {
        count++;
        lua_pop (lua, 1);
    }

    return count == len;
}

/* --------------------------------------------------------------------------------------------- */

/* The table at @index as a{sv}; its keys are strings */
static GVariant *
mc_lua_table_to_vardict (lua_State *lua, int index, int depth, const char **error)
{
    GVariantBuilder builder;

    index = lua_absindex (lua, index);
    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

    lua_pushnil (lua);
    while (lua_next (lua, index) != 0)
    {
        GVariant *value;

        if (lua_type (lua, -2) != LUA_TSTRING)
        {
            lua_pop (lua, 2);
            g_variant_builder_clear (&builder);
            *error = "the keys of the arguments are strings";
            return NULL;
        }

        value = mc_lua_to_variant (lua, -1, depth + 1, error);
        if (value == NULL)
        {
            lua_pop (lua, 2);
            g_variant_builder_clear (&builder);
            return NULL;
        }
        g_variant_builder_add (&builder, "{sv}", lua_tostring (lua, -2), value);
        lua_pop (lua, 1);
    }

    return g_variant_builder_end (&builder);
}

/* --------------------------------------------------------------------------------------------- */

static GVariant *
mc_lua_to_variant (lua_State *lua, int index, int depth, const char **error)
{
    index = lua_absindex (lua, index);

    if (depth > MC_LUA_VARIANT_MAX_DEPTH)
    {
        *error = "the arguments are nested too deep";
        return NULL;
    }

    switch (lua_type (lua, index))
    {
    case LUA_TBOOLEAN:
        return g_variant_new_boolean (lua_toboolean (lua, index));

    case LUA_TNUMBER:
        if (lua_isinteger (lua, index))
            return g_variant_new_int64 ((gint64) lua_tointeger (lua, index));
        return g_variant_new_double ((double) lua_tonumber (lua, index));

    case LUA_TSTRING:
    {
        size_t len;
        const char *s = lua_tolstring (lua, index, &len);

        // text that is not UTF-8, or holds a zero, goes as the bytes it is
        if (g_utf8_validate (s, (gssize) len, NULL) && memchr (s, '\0', len) == NULL)
            return g_variant_new_string (s);
        return g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, s, len, 1);
    }

    case LUA_TTABLE:
        if (mc_lua_table_is_list (lua, index))
        {
            GVariantBuilder builder;
            const lua_Unsigned len = lua_rawlen (lua, index);
            lua_Unsigned i;

            g_variant_builder_init (&builder, G_VARIANT_TYPE ("av"));
            for (i = 1; i <= len; i++)
            {
                GVariant *value;

                lua_rawgeti (lua, index, (lua_Integer) i);
                value = mc_lua_to_variant (lua, -1, depth + 1, error);
                lua_pop (lua, 1);
                if (value == NULL)
                {
                    g_variant_builder_clear (&builder);
                    return NULL;
                }
                g_variant_builder_add (&builder, "v", value);
            }
            return g_variant_builder_end (&builder);
        }
        return mc_lua_table_to_vardict (lua, index, depth, error);

    default:
        *error = "a value of the arguments is not a string, a number, a boolean or a table";
        return NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_variant (lua_State *lua, GVariant *value, int depth)
{
    const GVariantType *type = g_variant_get_type (value);

    if (depth > MC_LUA_VARIANT_MAX_DEPTH)
    {
        lua_pushnil (lua);
        return;
    }

    if (g_variant_type_equal (type, G_VARIANT_TYPE_BOOLEAN))
        lua_pushboolean (lua, g_variant_get_boolean (value));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT64))
        lua_pushinteger (lua, (lua_Integer) g_variant_get_int64 (value));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_INT32))
        lua_pushinteger (lua, (lua_Integer) g_variant_get_int32 (value));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT32))
        lua_pushinteger (lua, (lua_Integer) g_variant_get_uint32 (value));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_UINT64))
        lua_pushinteger (lua, (lua_Integer) g_variant_get_uint64 (value));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_DOUBLE))
        lua_pushnumber (lua, (lua_Number) g_variant_get_double (value));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_STRING))
        lua_pushstring (lua, g_variant_get_string (value, NULL));
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_BYTESTRING))
    {
        gsize len;
        const char *bytes = g_variant_get_fixed_array (value, &len, 1);

        lua_pushlstring (lua, bytes, len);
    }
    else if (g_variant_type_equal (type, G_VARIANT_TYPE_VARIANT))
    {
        GVariant *inner = g_variant_get_variant (value);

        mc_lua_push_variant (lua, inner, depth + 1);
        g_variant_unref (inner);
    }
    else if (g_variant_type_is_subtype_of (type, G_VARIANT_TYPE ("a{s*}")))
    {
        GVariantIter iter;
        GVariant *entry;

        lua_createtable (lua, 0, (int) g_variant_n_children (value));
        g_variant_iter_init (&iter, value);
        while ((entry = g_variant_iter_next_value (&iter)) != NULL)
        {
            GVariant *key = g_variant_get_child_value (entry, 0);
            GVariant *v = g_variant_get_child_value (entry, 1);

            mc_lua_push_variant (lua, v, depth + 1);
            lua_setfield (lua, -2, g_variant_get_string (key, NULL));
            g_variant_unref (key);
            g_variant_unref (v);
            g_variant_unref (entry);
        }
    }
    else if (g_variant_type_is_array (type) || g_variant_type_is_tuple (type))
    {
        const gsize n = g_variant_n_children (value);
        gsize i;

        lua_createtable (lua, (int) n, 0);
        for (i = 0; i < n; i++)
        {
            GVariant *v = g_variant_get_child_value (value, i);

            mc_lua_push_variant (lua, v, depth + 1);
            lua_rawseti (lua, -2, (lua_Integer) (i + 1));
            g_variant_unref (v);
        }
    }
    else
    {
        char *text = g_variant_print (value, FALSE);

        lua_pushstring (lua, text);
        g_free (text);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_services_ready (const mc_lua_package_t *package)
{
    return mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_SERVICES,
                                       MC_LUA_HOST_API_SERVICES_SIZE)
        && package->runtime->host->service_call != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_service_t *
mc_lua_service_check (lua_State *lua, int index)
{
    return (mc_lua_service_t *) luaL_checkudata (lua, index, MC_LUA_SERVICE_MT);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_service_listener_free (mc_lua_service_listener_t *listener)
{
    const mc_lua_package_t *package = listener->package;

    if (package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->service_disconnect != NULL)
        package->runtime->host->service_disconnect (package->runtime->context, listener->id);
    if (package->lua != NULL && listener->callback_ref != LUA_NOREF)
        luaL_unref (package->lua, LUA_REGISTRYINDEX, listener->callback_ref);
    g_free (listener->signal);
    g_free (listener);
}

/* --------------------------------------------------------------------------------------------- */

/* A service told what happened: the callback of the script gets the arguments and the signal */
static void
mc_lua_service_signal (const char *name, const char *signal, GVariant *args, void *user_data)
{
    mc_lua_service_listener_t *listener = (mc_lua_service_listener_t *) user_data;
    mc_lua_package_t *package = listener->package;
    lua_State *lua = package->lua;

    (void) name;

    if (package->closed || lua == NULL)
        return;
    if (strcmp (listener->signal, "*") != 0 && strcmp (listener->signal, signal) != 0)
        return;

    lua_rawgeti (lua, LUA_REGISTRYINDEX, listener->callback_ref);
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        return;
    }

    mc_lua_push_variant (lua, args, 0);
    lua_pushstring (lua, signal);
    package->callback_depth++;
    if (lua_pcall (lua, 2, 0, 0) != LUA_OK)
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_EVENT, "Lua service callback failed");
    package->callback_depth--;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua mc.service(name) -> service @capability services @mutation no
 * @summary A service a plugin offers, by its name ("viewer").  The object is there whether the
 * service is yet or not: a call says "not_found" while it is not. */
static int
mc_lua_service (lua_State *lua)
{
    const char *name = luaL_checkstring (lua, 1);
    mc_lua_service_t *service;

    service = (mc_lua_service_t *) lua_newuserdatauv (lua, sizeof (*service), 0);
    service->name = g_strdup (name);
    luaL_setmetatable (lua, MC_LUA_SERVICE_MT);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua service:call(method, args?) -> table|nil, error? @capability services @mutation yes
 * @summary Call a method of the service.  args is a table of strings, numbers, booleans and
 * tables; the answer is a table the same way.  The error is the one the service gives, or
 * "not_found" when there is no such service. */
static int
mc_lua_service_call (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const mc_lua_service_t *service = mc_lua_service_check (lua, 1);
    const char *method = luaL_checkstring (lua, 2);
    GVariant *args = NULL;
    GVariant *answer;
    GError *error = NULL;
    const char *convert_error = NULL;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (!mc_lua_services_ready (package))
        return mc_lua_not_ready (lua);

    if (!lua_isnoneornil (lua, 3))
    {
        luaL_checktype (lua, 3, LUA_TTABLE);
        args = mc_lua_table_to_vardict (lua, 3, 0, &convert_error);
        if (args == NULL)
            return mc_lua_return_error (lua, convert_error);
    }

    if (!package->runtime->host->service_exists (package->runtime->context, service->name))
    {
        if (args != NULL)
            g_variant_unref (g_variant_ref_sink (args));
        return mc_lua_return_error (lua, "not_found");
    }

    answer = package->runtime->host->service_call (package->runtime->context, service->name, method,
                                                   args, &error);
    if (answer == NULL)
    {
        lua_pushnil (lua);
        lua_pushstring (lua, error != NULL ? error->message : "failed");
        g_clear_error (&error);
        return 2;
    }

    mc_lua_push_variant (lua, answer, 0);
    g_variant_unref (answer);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua service:on(signal, callback) -> integer|nil, error? @capability services @mutation yes
 * @summary Call callback(args, signal) when the service tells of signal ("closed"), or of any
 * signal for "*".  It listens whether the service is there yet or not.  The id is what
 * service:off() takes.
 * @lua-callback signal(args, name) -> nil */
static int
mc_lua_service_on (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const mc_lua_service_t *service = mc_lua_service_check (lua, 1);
    const char *signal = luaL_checkstring (lua, 2);
    mc_lua_service_listener_t *listener;

    luaL_checktype (lua, 3, LUA_TFUNCTION);
    if (package == NULL || package->closed || !mc_lua_services_ready (package))
        return mc_lua_not_ready (lua);

    listener = g_new0 (mc_lua_service_listener_t, 1);
    listener->package = package;
    listener->signal = g_strdup (signal);
    lua_pushvalue (lua, 3);
    listener->callback_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    listener->id = package->runtime->host->service_connect (
        package->runtime->context, service->name, mc_lua_service_signal, listener);
    if (listener->id == 0)
    {
        luaL_unref (lua, LUA_REGISTRYINDEX, listener->callback_ref);
        g_free (listener->signal);
        g_free (listener);
        return mc_lua_return_error (lua, "could not listen to the service");
    }

    g_ptr_array_add (package->service_listeners, listener);
    lua_pushinteger (lua, (lua_Integer) listener->id);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

/** @lua service:off(id) -> boolean @capability services @mutation yes
 * @summary Stop listening: id is what service:on() returned. */
static int
mc_lua_service_off (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const lua_Integer id = luaL_checkinteger (lua, 2);
    guint i;

    (void) mc_lua_service_check (lua, 1);

    for (i = 0; package != NULL && i < package->service_listeners->len; i++)
    {
        mc_lua_service_listener_t *listener =
            (mc_lua_service_listener_t *) g_ptr_array_index (package->service_listeners, i);

        if ((lua_Integer) listener->id == id)
        {
            g_ptr_array_remove_index_fast (package->service_listeners, i);
            mc_lua_service_listener_free (listener);
            lua_pushboolean (lua, TRUE);
            return 1;
        }
    }

    lua_pushboolean (lua, FALSE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_service_gc (lua_State *lua)
{
    mc_lua_service_t *service = mc_lua_service_check (lua, 1);

    g_free (service->name);
    service->name = NULL;
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_install_api (mc_lua_package_t *package)
{
    lua_State *lua = package->lua;
    const char *const levels[] = { "debug", "info", "warn", "error" };
    guint i;

    if (luaL_newmetatable (lua, MC_LUA_SERVICE_MT))
    {
        lua_pushcfunction (lua, mc_lua_service_gc);
        lua_setfield (lua, -2, "__gc");
        lua_createtable (lua, 0, 3);
        lua_pushcfunction (lua, mc_lua_service_call);
        lua_setfield (lua, -2, "call");
        lua_pushcfunction (lua, mc_lua_service_on);
        lua_setfield (lua, -2, "on");
        lua_pushcfunction (lua, mc_lua_service_off);
        lua_setfield (lua, -2, "off");
        lua_setfield (lua, -2, "__index");
    }
    lua_pop (lua, 1);

    if (luaL_newmetatable (lua, MC_LUA_SCREEN_MT))
    {
        lua_pushcfunction (lua, mc_lua_screen_gc);
        lua_setfield (lua, -2, "__gc");
        lua_pushcfunction (lua, mc_lua_screen_index);
        lua_setfield (lua, -2, "__index");
    }
    lua_pop (lua, 1);

    lua_pushlightuserdata (lua, package);
    lua_setfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_PACKAGE);

    lua_getglobal (lua, "package");
    if (lua_istable (lua, -1))
    {
        lua_pushliteral (lua, "");
        lua_setfield (lua, -2, "cpath");
        lua_pushnil (lua);
        lua_setfield (lua, -2, "loadlib");
        lua_newtable (lua);
        lua_setfield (lua, -2, "searchers");
    }
    lua_pop (lua, 1);

    lua_pushcfunction (lua, mc_lua_require);
    lua_setglobal (lua, "require");

    lua_createtable (lua, 0, 7);
    lua_pushcfunction (lua, mc_lua_on);
    lua_setfield (lua, -2, "on");
    lua_pushcfunction (lua, mc_lua_off);
    lua_setfield (lua, -2, "off");
    lua_pushcfunction (lua, mc_lua_macro);
    lua_setfield (lua, -2, "macro");
    lua_pushinteger (lua, 0);
    lua_setfield (lua, -2, "PASS");
    lua_pushinteger (lua, 1);
    lua_setfield (lua, -2, "CONSUME");

    lua_createtable (lua, 0, G_N_ELEMENTS (levels));
    for (i = 0; i < G_N_ELEMENTS (levels); i++)
    {
        lua_pushstring (lua, levels[i]);
        lua_pushcclosure (lua, mc_lua_log_message, 1);
        lua_setfield (lua, -2, levels[i]);
    }
    lua_setfield (lua, -2, "log");

    lua_createtable (lua, 0, 7);
    lua_pushcfunction (lua, mc_lua_ui_status);
    lua_setfield (lua, -2, "status");
    lua_pushcfunction (lua, mc_lua_ui_message);
    lua_setfield (lua, -2, "message");
    lua_pushcfunction (lua, mc_lua_ui_dialog);
    lua_setfield (lua, -2, "dialog");
    lua_pushcfunction (lua, mc_lua_ui_indicator);
    lua_setfield (lua, -2, "indicator");
    lua_pushcfunction (lua, mc_lua_ui_indicator_clear);
    lua_setfield (lua, -2, "indicator_clear");
    lua_pushcfunction (lua, mc_lua_ui_text_width);
    lua_setfield (lua, -2, "text_width");
    lua_pushcfunction (lua, mc_lua_ui_screen);
    lua_setfield (lua, -2, "screen");
    lua_setfield (lua, -2, "ui");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_syntax_scan);
    lua_setfield (lua, -2, "scan");
    lua_setfield (lua, -2, "syntax");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_tty_info);
    lua_setfield (lua, -2, "info");
    lua_setfield (lua, -2, "tty");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_process_run);
    lua_setfield (lua, -2, "run");
    lua_setfield (lua, -2, "process");

    lua_pushcfunction (lua, mc_lua_settings);
    lua_setfield (lua, -2, "settings");

    lua_pushcfunction (lua, mc_lua_service);
    lua_setfield (lua, -2, "service");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_editor_current);
    lua_setfield (lua, -2, "current");
    lua_setfield (lua, -2, "editor");

    lua_setglobal (lua, "mc");
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
mc_lua_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                       gpointer user_data)
{
    mc_lua_subscription_t *subscription = (mc_lua_subscription_t *) user_data;
    mc_lua_package_t *package;
    lua_State *lua;
    gboolean consume = FALSE;
    mc_runtime_event_id_t previous_event;

    (void) runtime_context;

    if (subscription == NULL || snapshot == NULL || subscription->package == NULL)
        return MC_RUNTIME_EVENT_PASS;

    package = subscription->package;
    lua = package->lua;
    if (package->closed || lua == NULL)
        return MC_RUNTIME_EVENT_PASS;

    lua_rawgeti (lua, LUA_REGISTRYINDEX, subscription->callback_ref);
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        mc_lua_log (package, "error", "event callback is no longer a function");
        return MC_RUNTIME_EVENT_ERROR;
    }

    previous_event = package->active_event;
    package->callback_depth++;
    package->active_event = snapshot->event_id;
    mc_lua_push_event (lua, snapshot);
    if (lua_pcall (lua, 1, 1, 0) != LUA_OK)
    {
        mc_lua_report_error (
            package,
            snapshot->event_id == MC_RUNTIME_EVENT_STARTUP ? MC_RUNTIME_ERROR_PHASE_STARTUP
                                                           : MC_RUNTIME_ERROR_PHASE_EVENT,
            snapshot->event_id == MC_RUNTIME_EVENT_STARTUP ? "Lua startup callback failed"
                                                           : "Lua event callback failed");
        package->active_event = previous_event;
        package->callback_depth--;
        return MC_RUNTIME_EVENT_ERROR;
    }

    if (snapshot->event_id == MC_RUNTIME_EVENT_EDITOR_KEY)
    {
        consume = lua_isboolean (lua, -1)
            ? lua_toboolean (lua, -1)
            : (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) == 1);
    }
    lua_pop (lua, 1);
    package->active_event = previous_event;
    package->callback_depth--;

    return consume ? MC_RUNTIME_EVENT_CONSUME : MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_macro_t *
mc_lua_find_editor_macro (mc_lua_runtime_t *runtime, const mc_runtime_event_snapshot_t *snapshot)
{
    char *key;
    mc_lua_macro_t *selected = NULL;
    guint i;

    if (runtime == NULL || runtime->macros == NULL || snapshot == NULL
        || snapshot->event_id != MC_RUNTIME_EVENT_EDITOR_KEY)
        return NULL;

    key = mc_lua_key_name_normalize (snapshot->data.editor_key.key.name);
    if (key == NULL)
        return NULL;

    for (i = 0; i < runtime->macros->len; i++)
    {
        mc_lua_macro_t *macro = (mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);

        if (macro == NULL || macro->disabled || macro->package == NULL || macro->package->closed
            || g_strcmp0 (macro->area, "editor") != 0 || g_strcmp0 (macro->key, key) != 0)
            continue;

        if (selected == NULL || macro->priority > selected->priority)
            selected = macro;
    }

    g_free (key);
    return selected;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
mc_lua_invoke_macro (mc_lua_macro_t *macro, const mc_runtime_event_snapshot_t *snapshot)
{
    mc_lua_package_t *package;
    lua_State *lua;
    mc_runtime_event_id_t previous_event;
    gboolean pass = FALSE;

    if (macro == NULL || macro->disabled || macro->package == NULL || macro->package->closed
        || snapshot == NULL)
        return MC_RUNTIME_EVENT_PASS;

    package = macro->package;
    lua = package->lua;
    if (lua == NULL)
        return MC_RUNTIME_EVENT_PASS;

    lua_rawgeti (lua, LUA_REGISTRYINDEX, macro->action_ref);
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        macro->disabled = TRUE;
        mc_lua_log (package, "error", "macro action is no longer a function");
        return MC_RUNTIME_EVENT_CONSUME;
    }

    previous_event = package->active_event;
    package->callback_depth++;
    package->active_event = snapshot->event_id;
    mc_lua_push_event (lua, snapshot);
    if (lua_pcall (lua, 1, 1, 0) != LUA_OK)
    {
        macro->errors++;
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_MACRO, "Lua macro callback failed");
        if (macro->errors >= 3)
        {
            macro->disabled = TRUE;
            mc_lua_log (package, "warning", "macro disabled after three errors");
        }
        package->active_event = previous_event;
        package->callback_depth--;
        return MC_RUNTIME_EVENT_CONSUME;
    }

    pass = (lua_isboolean (lua, -1) && !lua_toboolean (lua, -1))
        || (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) == 0);
    lua_pop (lua, 1);
    package->active_event = previous_event;
    package->callback_depth--;

    return pass ? MC_RUNTIME_EVENT_PASS : MC_RUNTIME_EVENT_CONSUME;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
mc_lua_macro_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                             gpointer user_data)
{
    mc_lua_runtime_t *runtime = (mc_lua_runtime_t *) user_data;

    if (runtime == NULL || runtime != mc_lua_runtime_current || runtime->context != runtime_context
        || runtime->stopping)
        return MC_RUNTIME_EVENT_PASS;

    return mc_lua_invoke_macro (mc_lua_find_editor_macro (runtime, snapshot), snapshot);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_candidate_destroy (mc_lua_package_candidate_t *candidate)
{
    if (candidate == NULL)
        return;

    g_free (candidate->id);
    g_free (candidate->name);
    g_free (candidate->workspace);
    g_free (candidate->root);
    g_free (candidate->entry);
    g_free (candidate);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_info_destroy (mc_lua_package_info_t *info)
{
    if (info == NULL)
        return;

    g_free (info->id);
    g_free (info->name);
    g_free (info->workspace);
    g_free (info->root);
    g_free (info);
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_package_candidate_t *
mc_lua_package_candidate_new (const char *parent, const char *directory_name,
                              mc_lua_package_origin_t origin, const char *workspace)
{
    char *root;
    char *ini_path;
    char *entry_path;
    GKeyFile *ini = NULL;
    GError *error = NULL;
    char *id = NULL;
    char *name = NULL;
    char *entry = NULL;
    char *provides = NULL;
    guint provides_flags = MC_LUA_PROVIDES_NONE;
    gint api_version;
    mc_lua_package_candidate_t *candidate = NULL;

    if (!mc_lua_id_is_valid (directory_name) || g_strcmp0 (workspace, MC_LUA_EDITOR_WORKSPACE) != 0)
        return NULL;

    root = g_build_filename (parent, directory_name, (char *) NULL);
    if (!mc_lua_path_is_trusted (root, TRUE))
        goto done;

    ini_path = g_build_filename (root, MC_LUA_MANIFEST_FILE, (char *) NULL);
    if (!mc_lua_file_is_trusted_under (root, ini_path))
        goto done_with_ini;
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, ini_path, G_KEY_FILE_NONE, &error))
        goto done_with_ini;

    id = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "id", &error);
    if (error != NULL || !mc_lua_id_is_valid (id) || strcmp (id, directory_name) != 0)
        goto done_with_id;

    api_version = g_key_file_get_integer (ini, MC_LUA_MANIFEST_GROUP, "api_version", &error);
    if (error != NULL || api_version != MC_LUA_API_VERSION)
        goto done_with_id;

    entry = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "entry", &error);
    if (error != NULL || !mc_lua_relative_lua_file_is_valid (entry))
        goto done_with_entry;

    entry_path = g_build_filename (root, entry, (char *) NULL);
    if (!mc_lua_file_is_trusted_under (root, entry_path))
    {
        g_free (entry_path);
        goto done_with_entry;
    }
    g_free (entry_path);

    provides = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "provides", NULL);
    if (!mc_lua_provides_parse (provides, &provides_flags))
        goto done_with_provides;

    candidate = g_new0 (mc_lua_package_candidate_t, 1);
    candidate->id = g_steal_pointer (&id);
    name = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "name", NULL);
    candidate->name =
        name != NULL && name[0] != '\0' ? g_steal_pointer (&name) : g_strdup (candidate->id);
    candidate->workspace = g_strdup (workspace);
    candidate->root = g_steal_pointer (&root);
    candidate->entry = g_steal_pointer (&entry);
    candidate->origin = origin;
    candidate->provides = provides_flags;

done_with_provides:
    g_free (provides);
done_with_entry:
    g_free (entry);
done_with_id:
    g_free (id);
    g_free (name);
done_with_ini:
    g_clear_error (&error);
    if (ini != NULL)
        g_key_file_free (ini);
    g_free (ini_path);
done:
    g_free (root);
    return candidate;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_discover_workspace_directory (const char *directory, mc_lua_package_origin_t origin,
                                     const char *workspace, GHashTable *candidates)
{
    GDir *dir;
    const char *entry;

    if (directory == NULL)
        return;

    /* A workspace script directory is optional.  Its absence is not a trust error. */
    if (!g_file_test (directory, G_FILE_TEST_IS_DIR))
        return;

    if (!mc_lua_path_is_trusted (directory, TRUE))
    {
        fprintf (stderr, "Lua scripts: ignoring insecure directory %s\n", directory);
        return;
    }

    dir = g_dir_open (directory, 0, NULL);
    if (dir == NULL)
        return;

    while ((entry = g_dir_read_name (dir)) != NULL)
    {
        mc_lua_package_candidate_t *candidate =
            mc_lua_package_candidate_new (directory, entry, origin, workspace);

        if (candidate != NULL)
            g_hash_table_replace (candidates, g_strdup (candidate->id), candidate);
    }

    g_dir_close (dir);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_discover_script_root (const char *directory, mc_lua_package_origin_t origin,
                             GHashTable *candidates)
{
    char *workspace_directory;

    if (directory == NULL)
        return;

    workspace_directory = g_build_filename (directory, MC_LUA_EDITOR_WORKSPACE, (char *) NULL);
    mc_lua_discover_workspace_directory (workspace_directory, origin, MC_LUA_EDITOR_WORKSPACE,
                                         candidates);
    g_free (workspace_directory);
}

/* --------------------------------------------------------------------------------------------- */

static gint
mc_lua_package_candidate_compare (gconstpointer first, gconstpointer second)
{
    const mc_lua_package_candidate_t *first_candidate = (const mc_lua_package_candidate_t *) first;
    const mc_lua_package_candidate_t *second_candidate =
        (const mc_lua_package_candidate_t *) second;

    return g_strcmp0 (first_candidate->id, second_candidate->id);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_package_load (mc_lua_runtime_t *runtime, const mc_lua_package_candidate_t *candidate)
{
    mc_lua_package_t *package;
    int status;

    package = g_new0 (mc_lua_package_t, 1);
    package->runtime = runtime;
    package->id = g_strdup (candidate->id);
    package->workspace = g_strdup (candidate->workspace);
    package->root = g_strdup (candidate->root);
    package->entry_path = g_build_filename (candidate->root, candidate->entry, (char *) NULL);
    package->origin = candidate->origin;
    package->provides = candidate->provides;
    package->subscriptions = g_hash_table_new (g_int64_hash, g_int64_equal);
    package->macros = g_ptr_array_new_with_free_func (mc_lua_macro_destroy);
    package->settings_ref = LUA_NOREF;
    package->service_listeners = g_ptr_array_new ();
    package->lua = luaL_newstate ();

    if (package->lua == NULL)
    {
        mc_lua_log (package, "error", "could not create a Lua state");
        mc_lua_package_destroy (package);
        return FALSE;
    }

    luaL_openlibs (package->lua);
    mc_lua_install_api (package);

    status = luaL_loadfile (package->lua, package->entry_path);
    if (status == LUA_OK)
        status = lua_pcall (package->lua, 0, 0, 0);
    if (status != LUA_OK)
    {
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_STARTUP, "Lua package startup failed");
        mc_lua_package_destroy (package);
        return FALSE;
    }

    g_ptr_array_add (runtime->packages, package);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_load_disabled_package_ids (mc_lua_runtime_t *runtime)
{
    char *path;
    GKeyFile *ini;
    gchar **keys;
    gsize key_count = 0;
    gsize i;

    if (runtime == NULL || runtime->disabled_package_ids == NULL)
        return;

    path =
        g_build_filename (g_get_user_config_dir (), MC_USERCONF_DIR, "plugins.ini", (char *) NULL);
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, path, G_KEY_FILE_NONE, NULL))
    {
        g_key_file_free (ini);
        g_free (path);
        return;
    }

    keys = g_key_file_get_keys (ini, "DisabledPlugins", &key_count, NULL);
    for (i = 0; keys != NULL && i < key_count; i++)
    {
        const char *id;

        if (!g_str_has_prefix (keys[i], "lua/"))
            continue;
        id = keys[i] + strlen ("lua/");
        if (mc_lua_id_is_valid (id))
            g_hash_table_add (runtime->disabled_package_ids, g_strdup (id));
    }

    g_strfreev (keys);
    g_key_file_free (ini);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_config_enabled (mc_lua_runtime_t *runtime)
{
    char *ini_path;
    GKeyFile *ini;
    GError *error = NULL;
    gboolean enabled = TRUE;
    char *user_scripts_dir = NULL;

    runtime->user_scripts_dir =
        g_build_filename (g_get_user_data_dir (), MC_USERCONF_DIR, "lua", "scripts", (char *) NULL);
    runtime->user_modules_dir =
        g_build_filename (g_get_user_data_dir (), MC_USERCONF_DIR, "lua", "lib", (char *) NULL);
    if (g_strcmp0 (g_getenv ("COOLE_NO_LUA"), "1") == 0)
        return FALSE;

    ini_path = g_build_filename (g_get_user_config_dir (), MC_USERCONF_DIR, "ini", (char *) NULL);
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, ini_path, G_KEY_FILE_NONE, &error))
    {
        g_clear_error (&error);
        g_key_file_free (ini);
        g_free (ini_path);
        return TRUE;
    }

    if (g_key_file_has_key (ini, "Lua", "enabled", NULL))
    {
        enabled = g_key_file_get_boolean (ini, "Lua", "enabled", &error);
        if (error != NULL)
        {
            g_clear_error (&error);
            enabled = TRUE;
        }
    }

    user_scripts_dir = g_key_file_get_string (ini, "Lua", "user_scripts_dir", NULL);
    if (user_scripts_dir != NULL && user_scripts_dir[0] != '\0')
    {
        if (g_path_is_absolute (user_scripts_dir))
        {
            g_free (runtime->user_scripts_dir);
            runtime->user_scripts_dir = g_steal_pointer (&user_scripts_dir);
        }
        else
            fprintf (stderr, "Lua scripts: ignoring relative user_scripts_dir\n");
    }

    g_free (user_scripts_dir);
    g_key_file_free (ini);
    g_free (ini_path);
    return enabled;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_load_packages (mc_lua_runtime_t *runtime)
{
    GHashTable *candidates;
    GList *values;
    GList *link;

    candidates = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify) mc_lua_package_candidate_destroy);
    mc_lua_discover_script_root (mc_lua_system_scripts_dir (), MC_LUA_PACKAGE_SYSTEM, candidates);
    mc_lua_discover_script_root (runtime->user_scripts_dir, MC_LUA_PACKAGE_USER, candidates);

    values = g_hash_table_get_values (candidates);
    values = g_list_sort (values, mc_lua_package_candidate_compare);
    for (link = values; link != NULL; link = g_list_next (link))
    {
        const mc_lua_package_candidate_t *candidate =
            (const mc_lua_package_candidate_t *) link->data;
        mc_lua_package_info_t *info = g_new0 (mc_lua_package_info_t, 1);

        info->id = g_strdup (candidate->id);
        info->name = g_strdup (candidate->name != NULL ? candidate->name : candidate->id);
        info->workspace = g_strdup (candidate->workspace != NULL ? candidate->workspace
                                                                 : MC_LUA_EDITOR_WORKSPACE);
        info->root = g_strdup (candidate->root);
        info->origin = candidate->origin;
        info->provides = candidate->provides;
        info->disabled = g_hash_table_contains (runtime->disabled_package_ids, candidate->id);
        g_ptr_array_add (runtime->catalog, info);

        if (!info->disabled)
            (void) mc_lua_package_load (runtime, candidate);
    }

    g_list_free (values);
    g_hash_table_destroy (candidates);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_packages (mc_runtime_plugin_context_t *context,
                                   mc_runtime_package_callback_t callback, gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || callback == NULL
        || runtime->catalog == NULL)
        return;

    for (i = 0; i < runtime->catalog->len; i++)
    {
        const mc_lua_package_info_t *info =
            (const mc_lua_package_info_t *) g_ptr_array_index (runtime->catalog, i);

        callback (info->id, info->name, !info->disabled, user_data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_package_details (mc_runtime_plugin_context_t *context,
                                          mc_runtime_package_details_callback_t callback,
                                          gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || callback == NULL
        || runtime->catalog == NULL)
        return;

    for (i = 0; i < runtime->catalog->len; i++)
    {
        const mc_lua_package_info_t *info =
            (const mc_lua_package_info_t *) g_ptr_array_index (runtime->catalog, i);

        callback (info->id, info->name, info->workspace, mc_lua_catalog_origin_name (info->origin),
                  info->root, !info->disabled, user_data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_actions (mc_runtime_plugin_context_t *context, const char *workspace,
                                  mc_runtime_action_callback_t callback, gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || runtime->stopping || callback == NULL
        || g_strcmp0 (workspace, MC_LUA_EDITOR_WORKSPACE) != 0)
        return;

    for (i = 0; i < runtime->macros->len; i++)
    {
        const mc_lua_macro_t *macro =
            (const mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);
        char *id;

        if (macro == NULL || macro->disabled || !macro->listed || macro->package == NULL
            || macro->package->closed || g_strcmp0 (macro->area, "editor") != 0)
            continue;

        id = g_strdup_printf ("%s:%s", macro->package->id, macro->id);
        callback (id, macro->description, macro->display_key, user_data);
        g_free (id);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_menu_actions (mc_runtime_plugin_context_t *context, const char *workspace,
                                       mc_runtime_menu_action_callback_t callback,
                                       gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || runtime->stopping || callback == NULL
        || g_strcmp0 (workspace, MC_LUA_EDITOR_WORKSPACE) != 0)
        return;

    for (i = 0; i < runtime->macros->len; i++)
    {
        const mc_lua_macro_t *macro =
            (const mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);
        char *id;

        if (macro == NULL || macro->disabled || macro->menu_path == NULL
            || macro->menu_label == NULL || macro->package == NULL || macro->package->closed
            || g_strcmp0 (macro->area, "editor") != 0)
            continue;

        id = g_strdup_printf ("%s:%s", macro->package->id, macro->id);
        callback (id, macro->menu_path, macro->menu_label, macro->display_key, macro->menu_position,
                  user_data);
        g_free (id);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_runtime_invoke_action (mc_runtime_plugin_context_t *context, const char *workspace,
                              const char *action_id, const char **error)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    mc_lua_macro_t *selected = NULL;
    mc_runtime_event_snapshot_t snapshot = { 0 };
    guint previous_errors;
    guint i;

    if (error != NULL)
        *error = NULL;
    if (runtime == NULL || runtime->context != context || runtime->stopping
        || g_strcmp0 (workspace, MC_LUA_EDITOR_WORKSPACE) != 0 || action_id == NULL)
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }

    for (i = 0; i < runtime->macros->len; i++)
    {
        mc_lua_macro_t *macro = (mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);
        char *id;
        gboolean matches;

        if (macro == NULL || macro->package == NULL)
            continue;
        id = g_strdup_printf ("%s:%s", macro->package->id, macro->id);
        matches = g_strcmp0 (id, action_id) == 0;
        g_free (id);
        if (matches)
        {
            selected = macro;
            break;
        }
    }
    if (selected == NULL || selected->disabled || selected->package->closed)
    {
        if (error != NULL)
            *error = "action_not_found";
        return FALSE;
    }

    snapshot.event_id = MC_RUNTIME_EVENT_EDITOR_KEY;
    snapshot.data.editor_key.editor = runtime->host->editor_current (context);
    if (snapshot.data.editor_key.editor.kind != MC_RUNTIME_HANDLE_EDITOR
        || snapshot.data.editor_key.editor.id == 0
        || snapshot.data.editor_key.editor.generation == 0)
    {
        if (error != NULL)
            *error = "no_active_editor";
        return FALSE;
    }
    snapshot.data.editor_key.key.name = selected->display_key;
    previous_errors = selected->errors;
    (void) mc_lua_invoke_macro (selected, &snapshot);
    if (selected->errors != previous_errors)
    {
        if (error != NULL)
            *error = "callback_failed";
        return FALSE;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_package_t *
mc_lua_runtime_package_by_id (mc_lua_runtime_t *runtime, const char *package_id)
{
    guint i;

    if (runtime == NULL || package_id == NULL)
        return NULL;

    for (i = 0; i < runtime->packages->len; i++)
    {
        mc_lua_package_t *candidate = (mc_lua_package_t *) g_ptr_array_index (runtime->packages, i);

        if (candidate != NULL && !candidate->closed && g_strcmp0 (candidate->id, package_id) == 0)
            return candidate;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/** Show that dialog.  The script owns it, so there is nothing to return. */

static gboolean
mc_lua_runtime_configure_package (mc_runtime_plugin_context_t *context, const char *package_id,
                                  const char **error)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    mc_lua_package_t *package;
    lua_State *lua;

    if (error != NULL)
        *error = NULL;
    if (runtime == NULL || runtime->context != context || runtime->stopping)
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }
    package = mc_lua_runtime_package_by_id (runtime, package_id);
    if (package == NULL)
    {
        if (error != NULL)
            *error = "package_not_found";
        return FALSE;
    }
    if (package->settings_ref == LUA_NOREF)
    {
        if (error != NULL)
            *error = "settings_not_found";
        return FALSE;
    }

    lua = package->lua;
    lua_rawgeti (lua, LUA_REGISTRYINDEX, package->settings_ref);
    package->callback_depth++;
    if (lua_pcall (lua, 0, 0, 0) != LUA_OK)
    {
        /* the report takes the error off the stack */
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_EVENT, "Lua settings callback failed");
        package->callback_depth--;
        if (error != NULL)
            *error = "script_error";
        return FALSE;
    }
    package->callback_depth--;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_runtime_init (const mc_runtime_host_api_v1_t *host, mc_runtime_plugin_context_t *context,
                     GError **error)
{
    mc_lua_runtime_t *runtime;
    GError *subscribe_error = NULL;

    if (mc_lua_runtime_current != NULL)
    {
        g_set_error (error, g_quark_from_static_string ("mc-lua"), 0,
                     "The Lua runtime is already initialized");
        return FALSE;
    }

    runtime = g_new0 (mc_lua_runtime_t, 1);
    runtime->host = host;
    runtime->context = context;
    runtime->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) mc_lua_package_destroy);
    runtime->catalog =
        g_ptr_array_new_with_free_func ((GDestroyNotify) mc_lua_package_info_destroy);
    runtime->macros = g_ptr_array_new ();
    runtime->disabled_package_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    runtime->macro_subscription =
        host->subscribe (context, MC_RUNTIME_EVENT_EDITOR_KEY, 50, mc_lua_macro_event_callback,
                         runtime, NULL, &subscribe_error);
    if (runtime->macro_subscription == 0)
    {
        if (subscribe_error != NULL)
        {
            if (error != NULL)
                g_propagate_error (error, subscribe_error);
            else
                g_error_free (subscribe_error);
        }
        else
            g_set_error (error, g_quark_from_static_string ("mc-lua"), 0,
                         "Could not subscribe Lua macro dispatcher");
        g_ptr_array_free (runtime->packages, TRUE);
        g_ptr_array_free (runtime->catalog, TRUE);
        g_ptr_array_free (runtime->macros, TRUE);
        g_hash_table_destroy (runtime->disabled_package_ids);
        g_free (runtime);
        return FALSE;
    }

    mc_lua_runtime_current = runtime;
    host->context_set_data (context, runtime, mc_lua_runtime_destroy);

    mc_lua_load_disabled_package_ids (runtime);
    if (mc_lua_config_enabled (runtime))
        mc_lua_runtime_load_packages (runtime);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_shutdown (mc_runtime_plugin_context_t *context)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || runtime->stopping)
        return;

    runtime->stopping = TRUE;
    runtime->host->unsubscribe_all (context);

    for (i = 0; i < runtime->packages->len; i++)
        mc_lua_package_close ((mc_lua_package_t *) g_ptr_array_index (runtime->packages, i));
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

G_MODULE_EXPORT const mc_runtime_plugin_descriptor_v1_t *
mc_runtime_plugin_register_v1 (void)
{
    static const mc_runtime_plugin_descriptor_v1_t descriptor = {
        .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
        .struct_size = sizeof (mc_runtime_plugin_descriptor_v1_t),
        .capability_flags = 0,
        .runtime_name = "lua",
        .required_host_capabilities = MC_RUNTIME_HOST_CAP_EVENTS | MC_RUNTIME_HOST_CAP_CONTEXT_DATA,
        .init = mc_lua_runtime_init,
        .shutdown = mc_lua_runtime_shutdown,
        .enumerate_packages = mc_lua_runtime_enumerate_packages,
        .enumerate_package_details = mc_lua_runtime_enumerate_package_details,
        .enumerate_actions = mc_lua_runtime_enumerate_actions,
        .invoke_action = mc_lua_runtime_invoke_action,
        .enumerate_menu_actions = mc_lua_runtime_enumerate_menu_actions,
        .display_name = "Lua engine",
        .configure_package = mc_lua_runtime_configure_package,
    };

    return &descriptor;
}

/* --------------------------------------------------------------------------------------------- */
