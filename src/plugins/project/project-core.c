/*
   The project of a file, its files, and the matching of their names.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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

/** \file project-core.c
 *  \brief Source: the project of a file, its files, and the matching of their names
 */

#include <config.h>

#include <string.h>
#include <sys/wait.h>

#include "lib/global.h"

#include "project-core.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* what makes a directory the root of a build, nearest last */
static const char *const build_files[] = {
    "meson.build",  "CMakeLists.txt", "Makefile",     "GNUmakefile",    "configure.ac",
    "Cargo.toml",   "go.mod",         "package.json", "pyproject.toml", "setup.py",
    "build.gradle", "pom.xml",        NULL,
};

/* the directories of a tree that are no part of the project */
static const char *const skipped_dirs[] = {
    "node_modules", "target", "_build", "build", "builddir", "dist", "__pycache__", NULL,
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
project_dir_has (const char *dir, const char *name)
{
    char *path = g_build_filename (dir, name, (char *) NULL);
    const gboolean has = g_file_test (path, G_FILE_TEST_EXISTS);

    g_free (path);
    return has;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
project_dir_has_build_file (const char *dir)
{
    int i;

    for (i = 0; build_files[i] != NULL; i++)
        if (project_dir_has (dir, build_files[i]))
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gint
project_path_compare (gconstpointer a, gconstpointer b)
{
    return strcmp (*(const char *const *) a, *(const char *const *) b);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
project_dir_skipped (const char *name)
{
    int i;

    if (name[0] == '.')
        return TRUE;
    for (i = 0; skipped_dirs[i] != NULL; i++)
        if (strcmp (name, skipped_dirs[i]) == 0)
            return TRUE;
    // a build tree of meson or of cmake, whatever its name
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
project_walk (const char *root, const char *rel, GPtrArray *files, gboolean deep)
{
    char *dir_path = rel != NULL ? g_build_filename (root, rel, (char *) NULL) : g_strdup (root);
    GDir *dir = g_dir_open (dir_path, 0, NULL);
    const char *name;

    if (dir == NULL)
    {
        g_free (dir_path);
        return;
    }
    while ((name = g_dir_read_name (dir)) != NULL && files->len < PROJECT_FILES_MAX)
    {
        char *child_rel =
            rel != NULL ? g_build_filename (rel, name, (char *) NULL) : g_strdup (name);
        char *child = g_build_filename (root, child_rel, (char *) NULL);

        // a link to a directory is not walked, for no loops; a link to a file is a file
        if (g_file_test (child, G_FILE_TEST_IS_SYMLINK) && g_file_test (child, G_FILE_TEST_IS_DIR))
            g_free (child_rel);
        else if (g_file_test (child, G_FILE_TEST_IS_DIR))
        {
            char *meson_private = g_build_filename (child, "meson-private", (char *) NULL);
            char *cmake_cache = g_build_filename (child, "CMakeCache.txt", (char *) NULL);

            if (deep && !project_dir_skipped (name)
                && !g_file_test (meson_private, G_FILE_TEST_EXISTS)
                && !g_file_test (cmake_cache, G_FILE_TEST_EXISTS))
                project_walk (root, child_rel, files, deep);
            g_free (meson_private);
            g_free (cmake_cache);
            g_free (child_rel);
        }
        else if (name[0] != '.')
            g_ptr_array_add (files, child_rel);
        else
            g_free (child_rel);
        g_free (child);
    }
    g_dir_close (dir);
    g_free (dir_path);
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
project_git_files (const char *root)
{
    const char *argv[] = { "git",      "-C", root,       "-c",       "core.quotepath=off",
                           "ls-files", "-z", "--cached", "--others", "--exclude-standard",
                           NULL };
    char *out = NULL;
    gint status = 0;
    GPtrArray *files;
    const char *name;

    if (!g_spawn_sync (NULL, (char **) argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                       NULL, NULL, &out, NULL, &status, NULL)
        || !WIFEXITED (status) || WEXITSTATUS (status) != 0)
    {
        g_free (out);
        return NULL;
    }
    /* -z: the names as they are, ended by a NUL, quotes and new lines in them too; the last one
       is followed by the NUL that ends the output */
    files = g_ptr_array_new_with_free_func (g_free);
    for (name = out; *name != '\0' && files->len < PROJECT_FILES_MAX; name += strlen (name) + 1)
        g_ptr_array_add (files, g_strdup (name));
    g_free (out);
    return files;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
project_word_start (const char *path, const char *at)
{
    const char prev = at > path ? at[-1] : '/';

    return prev == '/' || prev == '_' || prev == '-' || prev == '.' || prev == ' '
        || (g_ascii_islower (prev) && g_ascii_isupper (*at));
}

/* --------------------------------------------------------------------------------------------- */

/* The best score of @query in @path from @from on, its characters in order, 0 when it does not
   match: each character counts, more at the start of a word and after the one before, less after
   a gap.  The best of all the ways the characters fit, not the first. */
static int
project_score_in (const char *path, const char *from, const char *query)
{
    const int n = (int) strlen (from);
    const int m = (int) strlen (query);
    int *best, *prev;
    int i, j, k, result = G_MININT;

    if (m == 0)
        return 1;
    if (m > n || n > 4096)
        return 0;

    // best[j]: the best score of the first i characters with the last of them at j
    best = g_new (int, n);
    prev = g_new (int, n);
    for (i = 0; i < m; i++)
    {
        const char want = g_ascii_tolower (query[i]);

        for (j = 0; j < n; j++)
        {
            int here = G_MININT;

            if (g_ascii_tolower (from[j]) == want)
            {
                const int bonus = 1 + (project_word_start (path, from + j) ? 8 : 0);

                if (i == 0)
                    here = bonus;
                else
                    for (k = j - 1; k >= 0; k--)
                        if (prev[k] != G_MININT)
                        {
                            const int gap = j - k - 1;
                            const int score = prev[k] + bonus + (gap == 0 ? 6 : -MIN (gap, 5));

                            here = MAX (here, score);
                        }
            }
            best[j] = here;
        }
        memcpy (prev, best, sizeof (int) * (gsize) n);
    }
    for (j = 0; j < n; j++)
        result = MAX (result, prev[j]);
    g_free (best);
    g_free (prev);
    // a match that is there scores at least 1, however far apart its characters are
    return result == G_MININT ? 0 : MAX (1, result);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

char *
project_find_root (const char *path)
{
    char *dir, *build_root = NULL, *found = NULL;
    const char *home = g_get_home_dir ();

    if (path == NULL)
        return NULL;
    dir = g_file_test (path, G_FILE_TEST_IS_DIR) ? g_canonicalize_filename (path, NULL)
                                                 : g_path_get_dirname (path);
    if (!g_path_is_absolute (dir))
    {
        char *absolute = g_canonicalize_filename (dir, NULL);

        g_free (dir);
        dir = absolute;
    }

    while (TRUE)
    {
        char *parent;

        if (project_dir_has (dir, ".coole") || project_dir_has (dir, ".git"))
        {
            found = g_strdup (dir);
            break;
        }
        if (project_dir_has_build_file (dir))
        {
            g_free (build_root);
            build_root = g_strdup (dir);
        }
        if (g_strcmp0 (dir, home) == 0)
            break;
        parent = g_path_get_dirname (dir);
        if (strcmp (parent, dir) == 0)
        {
            g_free (parent);
            break;
        }
        g_free (dir);
        dir = parent;
    }
    g_free (dir);

    if (found != NULL)
    {
        g_free (build_root);
        return found;
    }
    if (build_root != NULL)
        return build_root;
    return g_file_test (path, G_FILE_TEST_IS_DIR) ? g_canonicalize_filename (path, NULL)
                                                  : g_path_get_dirname (path);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
project_is_project (const char *root)
{
    return root != NULL
        && (project_dir_has (root, ".coole") || project_dir_has (root, ".git")
            || project_dir_has_build_file (root));
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
project_list_files (const char *root)
{
    GPtrArray *files = NULL;

    if (root == NULL || !g_file_test (root, G_FILE_TEST_IS_DIR))
        return NULL;
    if (project_dir_has (root, ".git"))
        files = project_git_files (root);
    if (files == NULL)
    {
        files = g_ptr_array_new_with_free_func (g_free);
        // the directory of a file that is in no project, the home one for one: its files alone
        project_walk (root, NULL, files, project_is_project (root));
    }
    g_ptr_array_sort (files, project_path_compare);
    return files;
}

/* --------------------------------------------------------------------------------------------- */

int
project_match_score (const char *path, const char *query)
{
    const char *base;
    int score;

    if (path == NULL || query == NULL)
        return 0;
    if (*query == '\0')
        return 1;

    base = strrchr (path, '/');
    base = base != NULL ? base + 1 : path;
    // all in the last part of the name is best, then the whole name
    if (strchr (query, '/') == NULL && (score = project_score_in (path, base, query)) > 0)
    {
        score += 100;
        if (g_ascii_strncasecmp (base, query, strlen (query)) == 0)
            score += 50;
        // of two names that match alike, the shorter is likelier the one
        return MAX (1, score - (int) (strlen (base) / 3) - (int) (strlen (path) / 16));
    }
    score = project_score_in (path, path, query);
    return score > 0 ? MAX (1, score - (int) (strlen (path) / 8)) : 0;
}

/* --------------------------------------------------------------------------------------------- */

char *
project_alternate_file (const char *file, const char *root, const GPtrArray *files)
{
    static const char *const sources[] = { "c", "cc", "cpp", "cxx", "c++", "m", "mm", NULL };
    static const char *const headers[] = { "h", "hh", "hpp", "hxx", "h++", NULL };
    const char *const *others;
    const char *dot, *slash;
    char *stem, *base_stem;
    char *result = NULL;
    int i;

    if (file == NULL)
        return NULL;
    dot = strrchr (file, '.');
    slash = strrchr (file, '/');
    if (dot == NULL || (slash != NULL && dot < slash))
        return NULL;

    others = NULL;
    for (i = 0; sources[i] != NULL && others == NULL; i++)
        if (g_ascii_strcasecmp (dot + 1, sources[i]) == 0)
            others = headers;
    for (i = 0; headers[i] != NULL && others == NULL; i++)
        if (g_ascii_strcasecmp (dot + 1, headers[i]) == 0)
            others = sources;
    if (others == NULL)
        return NULL;

    stem = g_strndup (file, (gsize) (dot - file));
    // next to it first
    for (i = 0; others[i] != NULL && result == NULL; i++)
    {
        char *candidate = g_strconcat (stem, ".", others[i], (char *) NULL);

        if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
            result = candidate;
        else
            g_free (candidate);
    }

    // then anywhere in the project: include/calc.h for src/calc.c
    base_stem = g_path_get_basename (stem);
    if (result == NULL && files != NULL && root != NULL)
    {
        guint k;

        for (i = 0; others[i] != NULL && result == NULL; i++)
        {
            char *name = g_strconcat (base_stem, ".", others[i], (char *) NULL);

            for (k = 0; k < files->len && result == NULL; k++)
            {
                const char *rel = g_ptr_array_index (files, k);
                const char *rel_base = strrchr (rel, '/');

                rel_base = rel_base != NULL ? rel_base + 1 : rel;
                if (strcmp (rel_base, name) == 0)
                    result = g_build_filename (root, rel, (char *) NULL);
            }
            g_free (name);
        }
    }
    g_free (base_stem);
    g_free (stem);
    return result;
}

/* --------------------------------------------------------------------------------------------- */
