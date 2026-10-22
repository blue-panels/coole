/*
   The defaults of a project by its language, from presets.ini.

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

/** \file build-presets.c
 *  \brief Source: the defaults of a project by its language, from presets.ini
 */

#include <config.h>

#include <string.h>

#include "build-presets.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
build_preset_free (gpointer data)
{
    build_preset_t *p = (build_preset_t *) data;

    g_free (p->id);
    g_strfreev (p->markers);
    g_strfreev (p->extensions);
    g_free (p->build);
    g_strfreev (p->gdb);
    g_free (p->program);
    g_free (p->entry);
    g_strfreev (p->hide);
    g_free (p->adapter);
    g_free (p->address);
    g_free (p->launch);
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

/* A key of a group, NULL when it is not there or empty */
static char *
build_preset_string (GKeyFile *keyfile, const char *group, const char *key)
{
    char *value = g_key_file_get_string (keyfile, group, key, NULL);

    if (value != NULL && *g_strstrip (value) == '\0')
        g_clear_pointer (&value, g_free);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

/* A list of a group, ';' between the items, NULL when it has none */
static char **
build_preset_list (GKeyFile *keyfile, const char *group, const char *key)
{
    char **list = g_key_file_get_string_list (keyfile, group, key, NULL, NULL);
    guint i, kept = 0;

    if (list == NULL)
        return NULL;
    for (i = 0; list[i] != NULL; i++)
        if (*g_strstrip (list[i]) != '\0')
            list[kept++] = list[i];
        else
            g_free (list[i]);
    list[kept] = NULL;
    if (kept == 0)
        g_clear_pointer (&list, g_strfreev);
    return list;
}

/* --------------------------------------------------------------------------------------------- */

/* The groups of @text not yet in @presets; the group [project] of the copy of a project when
   @project */
static gboolean
build_presets_add (GPtrArray *presets, const char *text, gboolean project, GError **error)
{
    GKeyFile *keyfile = g_key_file_new ();
    char **groups;
    guint i;

    if (!g_key_file_load_from_data (keyfile, text, (gsize) -1, G_KEY_FILE_NONE, error))
    {
        g_key_file_free (keyfile);
        return FALSE;
    }
    groups = g_key_file_get_groups (keyfile, NULL);
    for (i = 0; groups[i] != NULL; i++)
    {
        build_preset_t *p;
        guint j;

        if (project != (strcmp (groups[i], BUILD_PROJECT_GROUP) == 0))
            continue;
        for (j = 0; j < presets->len; j++)
            if (strcmp (((build_preset_t *) g_ptr_array_index (presets, j))->id, groups[i]) == 0)
                break;
        if (j < presets->len)
            continue;
        // the file of a project with no copy, the command of the user alone, has no language
        if (project && !g_key_file_has_key (keyfile, groups[i], "language", NULL))
            continue;
        p = g_new0 (build_preset_t, 1);
        p->project = project;
        p->id = project ? build_preset_string (keyfile, groups[i], "language") : NULL;
        if (p->id == NULL)
            p->id = g_strdup (groups[i]);
        p->markers = build_preset_list (keyfile, groups[i], "markers");
        p->extensions = build_preset_list (keyfile, groups[i], "extensions");
        p->build = build_preset_string (keyfile, groups[i], "build");
        p->gdb = build_preset_list (keyfile, groups[i], "gdb");
        p->program = build_preset_string (keyfile, groups[i], "program");
        p->entry = build_preset_string (keyfile, groups[i], "entry");
        p->hide = build_preset_list (keyfile, groups[i], "hide");
        p->adapter = build_preset_string (keyfile, groups[i], "adapter");
        p->address = build_preset_string (keyfile, groups[i], "address");
        p->launch = build_preset_string (keyfile, groups[i], "launch");
        g_ptr_array_add (presets, p);
    }
    g_strfreev (groups);
    g_key_file_free (keyfile);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

GPtrArray *
build_presets_parse (const char *project_text, const char *user_text, const char *system_text,
                     GError **project_error, GError **user_error)
{
    GPtrArray *presets = g_ptr_array_new_with_free_func (build_preset_free);

    if (project_text != NULL)
        (void) build_presets_add (presets, project_text, TRUE, project_error);
    if (user_text != NULL)
        (void) build_presets_add (presets, user_text, FALSE, user_error);
    if (system_text != NULL)
        (void) build_presets_add (presets, system_text, FALSE, NULL);
    return presets;
}

/* --------------------------------------------------------------------------------------------- */

const build_preset_t *
build_presets_find (GPtrArray *presets, const char *root, const char *path)
{
    guint i, j;

    for (i = 0; root != NULL && i < presets->len; i++)
    {
        const build_preset_t *p = g_ptr_array_index (presets, i);

        if (p->project)
            return p;
        for (j = 0; p->markers != NULL && p->markers[j] != NULL; j++)
        {
            char *marker = g_build_filename (root, p->markers[j], (char *) NULL);
            const gboolean there = g_file_test (marker, G_FILE_TEST_EXISTS);

            g_free (marker);
            if (there)
                return p;
        }
    }
    for (i = 0; path != NULL && i < presets->len; i++)
    {
        const build_preset_t *p = g_ptr_array_index (presets, i);

        for (j = 0; p->extensions != NULL && p->extensions[j] != NULL; j++)
            if (g_str_has_suffix (path, p->extensions[j]))
                return p;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

char *
build_presets_project_text (const build_preset_t *preset, gboolean with_build)
{
    GKeyFile *keyfile = g_key_file_new ();
    const char *group = BUILD_PROJECT_GROUP;
    const struct
    {
        const char *key;
        const char *value;
    } strings[] = {
        { "build", with_build ? preset->build : NULL },
        { "program", preset->program },
        { "entry", preset->entry },
        { "adapter", preset->adapter },
        { "address", preset->address },
        { "launch", preset->launch },
    };
    const struct
    {
        const char *key;
        char **value;
    } lists[] = {
        { "extensions", preset->extensions },
        { "gdb", preset->gdb },
        { "hide", preset->hide },
    };
    char *text;
    guint i;

    g_key_file_set_string (keyfile, group, "language", preset->id);
    g_key_file_set_comment (
        keyfile, group, NULL,
        " How coole builds and debugs this project: a copy of the preset of its language in\n"
        " presets.ini, made when the project was first opened, the keys of presets.ini.\n"
        " What is changed here stays; command is the build command given in coole.\n"
        " Command > Language defaults of the project copies the preset again.",
        NULL);
    for (i = 0; i < G_N_ELEMENTS (strings); i++)
        if (strings[i].value != NULL)
            g_key_file_set_string (keyfile, group, strings[i].key, strings[i].value);
    for (i = 0; i < G_N_ELEMENTS (lists); i++)
        if (lists[i].value != NULL)
            g_key_file_set_string_list (keyfile, group, lists[i].key,
                                        (const char *const *) lists[i].value,
                                        g_strv_length (lists[i].value));
    text = g_key_file_to_data (keyfile, NULL, NULL);
    g_key_file_free (keyfile);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

char *
build_presets_expand (const char *value, const char *root)
{
    char *package, **parts, *result;

    if (value == NULL || strstr (value, "${package}") == NULL)
        return g_strdup (value);
    package = build_cargo_package (root);
    if (package == NULL)
        return NULL;
    parts = g_strsplit (value, "${package}", -1);
    result = g_strjoinv (package, parts);
    g_strfreev (parts);
    g_free (package);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

char *
build_cargo_package (const char *root)
{
    char *path = g_build_filename (root, "Cargo.toml", (char *) NULL);
    char *text = NULL, *name = NULL;
    char **lines;
    gboolean package = FALSE;
    guint i;

    if (!g_file_get_contents (path, &text, NULL, NULL))
    {
        g_free (path);
        return NULL;
    }
    g_free (path);
    lines = g_strsplit (text, "\n", -1);
    g_free (text);
    // name = "try-rust", in [package]; a quoted string, the escapes of TOML not minded
    for (i = 0; lines[i] != NULL && name == NULL; i++)
    {
        const char *line = g_strstrip (lines[i]);

        if (*line == '[')
            package = strcmp (line, "[package]") == 0;
        else if (package && strncmp (line, "name", 4) == 0)
        {
            const char *p = line + 4;
            const char *end;

            while (*p == ' ' || *p == '\t')
                p++;
            if (*p++ != '=')
                continue;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p != '"' && *p != '\'')
                continue;
            end = strchr (p + 1, *p);
            if (end != NULL && end > p + 1)
                name = g_strndup (p + 1, (gsize) (end - p - 1));
        }
    }
    g_strfreev (lines);
    return name;
}

/* --------------------------------------------------------------------------------------------- */
