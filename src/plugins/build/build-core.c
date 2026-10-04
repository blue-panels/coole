/*
   How a project is built, what it builds, and what the compiler says.

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

/** \file build-core.c
 *  \brief Source: how a project is built, what it builds, and what the compiler says
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "lib/global.h"

#include "build-core.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* how deep under a build directory the programs are looked for */
#define BUILD_SEARCH_DEPTH 4
#define BUILD_PROGRAMS_MAX 200
/* how much of the debug strings is read for the flags of the compiler */
#define BUILD_DEBUG_STR_MAX (8 * 1024 * 1024)

/*** file scope type declarations ****************************************************************/

typedef struct
{
    char *path;
    gint64 mtime;
} build_program_t;

/*** forward declarations (file scope functions) *************************************************/

static gboolean build_elf_read_full (const char *path, build_elf_t *elf, gboolean flags);

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static char *
build_read_text (const char *path)
{
    char *text = NULL;

    if (!g_file_get_contents (path, &text, NULL, NULL))
        return NULL;
    return text;
}

/* --------------------------------------------------------------------------------------------- */

/* The string after "key": in a JSON text, as it stands: enough for the paths of meson */
static char *
build_json_string (const char *text, const char *key)
{
    char *needle = g_strdup_printf ("\"%s\": \"", key);
    const char *p = strstr (text, needle);
    GString *out;

    if (p == NULL)
    {
        g_free (needle);
        return NULL;
    }
    p += strlen (needle);
    g_free (needle);
    out = g_string_new (NULL);
    for (; *p != '\0' && *p != '"'; p++)
    {
        if (*p == '\\' && p[1] != '\0')
            p++;
        g_string_append_c (out, *p);
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* Whether a directory is a build directory of meson, or of cmake, for that source */
static build_system_t
build_dir_of (const char *dir, const char *root)
{
    char *path, *text, *source;
    build_system_t system = BUILD_NONE;

    path = g_build_filename (dir, "meson-info", "meson-info.json", (char *) NULL);
    text = build_read_text (path);
    g_free (path);
    if (text != NULL)
    {
        source = build_json_string (text, "source");
        if (source != NULL && strcmp (source, root) == 0)
            system = BUILD_MESON;
        g_free (source);
        g_free (text);
        return system;
    }

    path = g_build_filename (dir, "CMakeCache.txt", (char *) NULL);
    text = build_read_text (path);
    g_free (path);
    if (text != NULL)
    {
        const char *home = strstr (text, "CMAKE_HOME_DIRECTORY:INTERNAL=");

        if (home != NULL)
        {
            const char *start = home + strlen ("CMAKE_HOME_DIRECTORY:INTERNAL=");
            const char *end = strchr (start, '\n');

            source = end != NULL ? g_strndup (start, (gsize) (end - start)) : g_strdup (start);
            if (strcmp (source, root) == 0)
                system = BUILD_CMAKE;
            g_free (source);
        }
        g_free (text);
    }
    return system;
}

/* --------------------------------------------------------------------------------------------- */

/* A build directory of the project: in the root, or next to it, the newest one */
static build_system_t
build_find_dir (const char *root, char **found)
{
    char *parent = g_path_get_dirname (root);
    const char *places[] = { root, parent, NULL };
    build_system_t best_system = BUILD_NONE;
    gint64 best_time = 0;
    int i;

    *found = NULL;
    // the root itself: a build in the tree
    best_system = build_dir_of (root, root);
    if (best_system != BUILD_NONE)
    {
        *found = g_strdup (root);
        g_free (parent);
        return best_system;
    }

    for (i = 0; places[i] != NULL; i++)
    {
        GDir *dir = g_dir_open (places[i], 0, NULL);
        const char *name;

        if (dir == NULL)
            continue;
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            char *candidate = g_build_filename (places[i], name, (char *) NULL);
            build_system_t system;
            struct stat st;

            if (name[0] != '.' && g_file_test (candidate, G_FILE_TEST_IS_DIR)
                && strcmp (candidate, root) != 0
                && (system = build_dir_of (candidate, root)) != BUILD_NONE
                && stat (candidate, &st) == 0 && (gint64) st.st_mtime >= best_time)
            {
                g_free (*found);
                *found = candidate;
                best_system = system;
                best_time = (gint64) st.st_mtime;
            }
            else
                g_free (candidate);
        }
        g_dir_close (dir);
    }
    g_free (parent);
    return best_system;
}

/* --------------------------------------------------------------------------------------------- */

static guint64
build_get (const guchar *p, int size, gboolean big)
{
    guint64 v = 0;
    int i;

    for (i = 0; i < size; i++)
        v |= (guint64) p[big ? size - 1 - i : i] << (8 * i);
    return v;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
build_read_at (FILE *f, guint64 offset, void *buf, gsize size)
{
    return fseeko (f, (off_t) offset, SEEK_SET) == 0 && fread (buf, 1, size, f) == size;
}

/* --------------------------------------------------------------------------------------------- */

/* Whether the producer strings of the compiler name an optimization: -O1 and up, -Os, -Ofast */
static gboolean
build_flags_optimized (const guchar *text, gsize len)
{
    gsize i;

    for (i = 0; i + 3 < len; i++)
        if (text[i] == ' ' && text[i + 1] == '-' && text[i + 2] == 'O')
        {
            const guchar c = text[i + 3];

            if ((c >= '1' && c <= '9') || c == 's' || c == 'z' || c == 'f')
                return TRUE;
            if (c == ' ' || c == '\0')
                return TRUE;  // -O alone is -O1
        }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gint
build_program_compare (gconstpointer a, gconstpointer b)
{
    const build_program_t *x = (const build_program_t *) a;
    const build_program_t *y = (const build_program_t *) b;

    if (x->mtime != y->mtime)
        return x->mtime > y->mtime ? -1 : 1;
    return strcmp (x->path, y->path);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
build_dir_skipped (const char *name)
{
    static const char *const skipped[] = { "meson-private", "meson-logs", "meson-info",
                                           "CMakeFiles",    "po",         "doc",
                                           "node_modules",  NULL };
    int i;

    if (name[0] == '.' || g_str_has_suffix (name, ".p") || g_str_has_suffix (name, ".dir"))
        return TRUE;
    for (i = 0; skipped[i] != NULL; i++)
        if (strcmp (name, skipped[i]) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
build_walk_programs (const char *dir, int depth, GArray *programs)
{
    GDir *d;
    const char *name;

    if (depth > BUILD_SEARCH_DEPTH || programs->len >= BUILD_PROGRAMS_MAX)
        return;
    d = g_dir_open (dir, 0, NULL);
    if (d == NULL)
        return;
    while ((name = g_dir_read_name (d)) != NULL && programs->len < BUILD_PROGRAMS_MAX)
    {
        char *path = g_build_filename (dir, name, (char *) NULL);
        struct stat st;

        if (lstat (path, &st) != 0 || S_ISLNK (st.st_mode))
            ;
        else if (S_ISDIR (st.st_mode))
        {
            if (!build_dir_skipped (name))
                build_walk_programs (path, depth + 1, programs);
        }
        else if (S_ISREG (st.st_mode) && (st.st_mode & 0111) != 0 && st.st_size > 64)
        {
            build_elf_t elf;

            // the programs to choose from: their flags are not read, megabytes each
            if (build_elf_read_full (path, &elf, FALSE) && elf.executable && elf.debug_info)
            {
                build_program_t p = { g_strdup (path), (gint64) st.st_mtime };

                g_array_append_val (programs, p);
            }
        }
        g_free (path);
    }
    g_dir_close (d);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

build_info_t *
build_detect (const char *root)
{
    build_info_t *info;
    char *dir = NULL;
    build_system_t system;
    char *makefile;

    if (root == NULL)
        return NULL;
    system = build_find_dir (root, &dir);
    info = g_new0 (build_info_t, 1);
    info->system = system;
    switch (system)
    {
    case BUILD_MESON:
    {
        char *quoted = g_shell_quote (dir);

        info->build_dir = dir;
        info->command = g_strdup_printf ("meson compile -C %s", quoted);
        g_free (quoted);
        return info;
    }
    case BUILD_CMAKE:
    {
        char *quoted = g_shell_quote (dir);

        info->build_dir = dir;
        info->command = g_strdup_printf ("cmake --build %s", quoted);
        g_free (quoted);
        return info;
    }
    default:
        break;
    }
    g_free (dir);

    makefile = g_build_filename (root, "Makefile", (char *) NULL);
    if (g_file_test (makefile, G_FILE_TEST_IS_REGULAR))
    {
        info->system = BUILD_MAKE;
        info->build_dir = g_strdup (root);
        info->command = g_strdup ("make");
        g_free (makefile);
        return info;
    }
    g_free (makefile);
    g_free (info);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
build_info_free (build_info_t *info)
{
    if (info == NULL)
        return;
    g_free (info->build_dir);
    g_free (info->command);
    g_free (info);
}

/* --------------------------------------------------------------------------------------------- */

/* The ELF @path; with @flags, the flags of the compiler too, which takes reading its strings */
static gboolean
build_elf_read_full (const char *path, build_elf_t *elf, gboolean flags)
{
    guchar head[64];
    size_t got;
    FILE *f;
    gboolean wide, big;
    guint64 phoff, shoff;
    guint phentsize, phnum, shentsize, shnum, shstrndx, type, i;
    guchar *names = NULL;
    guint64 names_off = 0, names_size = 0;
    gboolean ok = FALSE;

    memset (elf, 0, sizeof (*elf));
    f = fopen (path, "rb");
    if (f == NULL)
        return FALSE;
    got = fread (head, 1, sizeof (head), f);
    if (got < 52 || memcmp (head, "\177ELF", 4) != 0)
        goto out;

    wide = head[4] == 2;
    // the header of 64 bits is 64 bytes long
    if (wide && got < 64)
        goto out;
    big = head[5] == 2;
    type = (guint) build_get (head + 16, 2, big);
    if (wide)
    {
        phoff = build_get (head + 32, 8, big);
        shoff = build_get (head + 40, 8, big);
        phentsize = (guint) build_get (head + 54, 2, big);
        phnum = (guint) build_get (head + 56, 2, big);
        shentsize = (guint) build_get (head + 58, 2, big);
        shnum = (guint) build_get (head + 60, 2, big);
        shstrndx = (guint) build_get (head + 62, 2, big);
    }
    else
    {
        phoff = build_get (head + 28, 4, big);
        shoff = build_get (head + 32, 4, big);
        phentsize = (guint) build_get (head + 42, 2, big);
        phnum = (guint) build_get (head + 44, 2, big);
        shentsize = (guint) build_get (head + 46, 2, big);
        shnum = (guint) build_get (head + 48, 2, big);
        shstrndx = (guint) build_get (head + 50, 2, big);
    }
    ok = TRUE;

    // a program: ET_EXEC, or ET_DYN with an interpreter (a PIE, not a library)
    elf->executable = type == 2;
    if (type == 3)
        for (i = 0; i < phnum && i < 256; i++)
        {
            guchar ph[4];

            if (build_read_at (f, phoff + (guint64) i * phentsize, ph, sizeof (ph))
                && build_get (ph, 4, big) == 3)
                elf->executable = TRUE;
        }

    // the names of the sections
    if (shstrndx < shnum && shentsize >= (wide ? 40u : 40u))
    {
        guchar sh[64];

        if (build_read_at (f, shoff + (guint64) shstrndx * shentsize, sh, MIN (shentsize, 64)))
        {
            names_off = wide ? build_get (sh + 24, 8, big) : build_get (sh + 16, 4, big);
            names_size = wide ? build_get (sh + 32, 8, big) : build_get (sh + 20, 4, big);
            if (names_size > 0 && names_size < 1024 * 1024)
            {
                names = g_malloc (names_size + 1);
                if (!build_read_at (f, names_off, names, names_size))
                    g_clear_pointer (&names, g_free);
                else
                    names[names_size] = '\0';
            }
        }
    }

    for (i = 0; names != NULL && i < shnum && i < 4096; i++)
    {
        guchar sh[64];
        guint64 name, offset, size;
        const char *section;

        if (!build_read_at (f, shoff + (guint64) i * shentsize, sh, MIN (shentsize, 64)))
            break;
        name = build_get (sh, 4, big);
        if (name >= names_size)
            continue;
        section = (const char *) names + name;
        offset = wide ? build_get (sh + 24, 8, big) : build_get (sh + 16, 4, big);
        size = wide ? build_get (sh + 32, 8, big) : build_get (sh + 20, 4, big);
        if (strcmp (section, ".debug_info") == 0)
            elf->debug_info = TRUE;
        else if (flags && strcmp (section, ".debug_str") == 0 && size > 0)
        {
            // the producer strings name the flags of the compiler
            const gsize len = (gsize) MIN (size, BUILD_DEBUG_STR_MAX);
            guchar *text = g_malloc (len);

            if (build_read_at (f, offset, text, len))
                elf->optimized = build_flags_optimized (text, len);
            g_free (text);
        }
    }

out:
    g_free (names);
    fclose (f);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
build_elf_read (const char *path, build_elf_t *elf)
{
    return build_elf_read_full (path, elf, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
build_find_programs (const char *dir)
{
    GArray *programs = g_array_new (FALSE, FALSE, sizeof (build_program_t));
    GPtrArray *result = g_ptr_array_new_with_free_func (g_free);
    guint i;

    if (dir != NULL)
        build_walk_programs (dir, 0, programs);
    g_array_sort (programs, build_program_compare);
    for (i = 0; i < programs->len; i++)
        g_ptr_array_add (result, g_array_index (programs, build_program_t, i).path);
    g_array_free (programs, TRUE);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

build_diagnostic_t *
build_parse_diagnostic (const char *line, const char *dir)
{
    static GRegex *re = NULL;
    GMatchInfo *match = NULL;
    build_diagnostic_t *d = NULL;

    if (line == NULL)
        return NULL;
    if (re == NULL)
        re = g_regex_new ("^([^:\\s][^:]*):(\\d+):(?:(\\d+):)?\\s*(fatal error|error|warning|note)"
                          ":\\s*(.*)$",
                          G_REGEX_OPTIMIZE, 0, NULL);
    if (re != NULL && g_regex_match (re, line, 0, &match))
    {
        char *file = g_match_info_fetch (match, 1);
        char *line_text = g_match_info_fetch (match, 2);
        char *column = g_match_info_fetch (match, 3);
        char *kind = g_match_info_fetch (match, 4);

        d = g_new0 (build_diagnostic_t, 1);
        d->file = g_path_is_absolute (file) || dir == NULL ? g_canonicalize_filename (file, NULL)
                                                           : g_canonicalize_filename (file, dir);
        d->line = atol (line_text);
        d->column = column != NULL && *column != '\0' ? atol (column) : 0;
        d->error = strstr (kind, "error") != NULL;
        d->message = g_match_info_fetch (match, 5);
        g_free (file);
        g_free (line_text);
        g_free (column);
        g_free (kind);
    }
    g_match_info_free (match);
    return d;
}

/* --------------------------------------------------------------------------------------------- */

void
build_diagnostic_free (build_diagnostic_t *d)
{
    if (d == NULL)
        return;
    g_free (d->file);
    g_free (d->message);
    g_free (d);
}

/* --------------------------------------------------------------------------------------------- */
