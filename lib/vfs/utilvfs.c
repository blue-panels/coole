/*
   Utilities for VFS modules.

   Copyright (C) 1988-2025
   Free Software Foundation, Inc.

   Copyright (C) 1995, 1996 Miguel de Icaza

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

/**
 * \file
 * \brief Source: Utilities for VFS modules
 * \author Miguel de Icaza
 * \date 1995, 1996
 */

#include <config.h>

#include <ctype.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>

#if !defined(HAVE_UTIMENSAT) && defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#include "lib/global.h"
#include "lib/unixcompat.h"
#include "lib/widget.h"   // message()
#include "lib/strutil.h"  // INVALID_CONV

#include "vfs.h"
#include "utilvfs.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#ifndef TUNMLEN
#define TUNMLEN 256
#endif
#ifndef TGNMLEN
#define TGNMLEN 256
#endif

#define MC_HISTORY_VFS_PASSWORD "mc.vfs.password"

/*
 * FIXME2, the "-993" is to reduce the chance of a hit on the first lookup.
 */
#define GUID_DEFAULT_CONST -993

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/** Get current username
 *
 * @return g_malloc()ed string with the name of the currently logged in
 *         user ("anonymous" if uid is not registered in the system)
 */

char *
vfs_get_local_username (void)
{
    struct passwd *p_i;

    p_i = getpwuid (geteuid ());

    // Unknown UID, strange
    return (p_i != NULL && p_i->pw_name != NULL) ? g_strdup (p_i->pw_name) : g_strdup ("anonymous");
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Look up a user or group name from a uid/gid, maintaining a cache.
 * FIXME, for now it's a one-entry cache.
 * This file should be modified for non-unix systems to do something
 * reasonable.
 */

/* --------------------------------------------------------------------------------------------- */
/**
 * Create a temporary file with a name resembling the original.
 * Some programs may look at the extension.
 * We also protect stupid scripts against dangerous names.
 */

int
vfs_mkstemps (vfs_path_t **pname_vpath, const char *prefix, const char *param_basename)
{
    const char *p;
    GString *suffix;
    int fd;

    // Strip directories
    p = strrchr (param_basename, PATH_SEP);
    if (p == NULL)
        p = param_basename;
    else
        p++;

    // Protection against very long names
    const size_t len = strlen (p);

    if (len > (MC_MAXPATHLEN - 16))
        p += len - (MC_MAXPATHLEN - 16);

    suffix = g_string_sized_new (32);

    // Protection against unusual characters
    for (; *p != '\0' && *p != '#'; p++)
        if (strchr (".-_@", *p) != NULL || g_ascii_isalnum (*p))
            g_string_append_c (suffix, *p);

    fd = mc_mkstemps (pname_vpath, prefix, suffix->str);
    g_string_free (suffix, TRUE);

    return fd;
}

/* --------------------------------------------------------------------------------------------- */
/**  Extract the hostname and username from the path
 *
 * Format of the path is [user@]hostname:port/remote-dir, e.g.:
 *
 * ftp://sunsite.unc.edu/pub/linux
 * ftp://miguel@sphinx.nuclecu.unam.mx/c/nc
 * ftp://tsx-11.mit.edu:8192/
 * ftp://joe@foo.edu:11321/private
 * ftp://joe:password@foo.se
 *
 * @param path is an input string to be parsed
 * @param default_port is an input default port
 * @param flags are parsing modifier flags (@see vfs_url_flags_t)
 *
 * @return g_malloc()ed url info.
 *         If the user is empty, e.g. ftp://@roxanne/private, and URL_USE_ANONYMOUS
 *         is not set, then the current login name is supplied.
 *         Return value is a g_malloc()ed structure with the pathname relative to the
 *         host.
 */

/* --------------------------------------------------------------------------------------------- */

void __attribute__ ((noreturn))
vfs_die (const char *m)
{
    message (D_ERROR, _ ("Internal error:"), "%s", m);
    exit (EXIT_FAILURE);
}

/* --------------------------------------------------------------------------------------------- */

int
vfs_utime (const char *path, mc_timesbuf_t *times)
{
#ifdef HAVE_UTIMENSAT
    return utimensat (AT_FDCWD, path, *times, AT_SYMLINK_NOFOLLOW);
#else
    return utime (path, times);
#endif
}

/* --------------------------------------------------------------------------------------------- */

void
vfs_copy_stat_times (const struct stat *src, struct stat *dst)
{
    dst->st_atime = src->st_atime;
    dst->st_mtime = src->st_mtime;
    dst->st_ctime = src->st_ctime;

#ifdef HAVE_STRUCT_STAT_ST_MTIM
    dst->st_atim.tv_nsec = src->st_atim.tv_nsec;
    dst->st_mtim.tv_nsec = src->st_mtim.tv_nsec;
    dst->st_ctim.tv_nsec = src->st_ctim.tv_nsec;
#elif HAVE_STRUCT_STAT_ST_MTIMESPEC
    dst->st_atimespec.tv_nsec = src->st_atimespec.tv_nsec;
    dst->st_mtimespec.tv_nsec = src->st_mtimespec.tv_nsec;
    dst->st_ctimespec.tv_nsec = src->st_ctimespec.tv_nsec;
#elif HAVE_STRUCT_STAT_ST_MTIMENSEC
    dst->st_atimensec = src->st_atimensec;
    dst->st_mtimensec = src->st_mtimensec;
    dst->st_ctimensec = src->st_ctimensec;
#endif
}

/* --------------------------------------------------------------------------------------------- */
