/*
   Various utilities

   Copyright (C) 1994-2025
   Free Software Foundation, Inc.

   Written by:
   Miguel de Icaza, 1994, 1995, 1996
   Janne Kukonlehto, 1994, 1995, 1996
   Dugan Porter, 1994, 1995, 1996
   Jakub Jelinek, 1994, 1995, 1996
   Mauricio Plaza, 1994, 1995, 1996
   Slava Zanko <slavazanko@gmail.com>, 2013

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

/** \file lib/util.c
 *  \brief Source: various utilities
 */

#include <config.h>

#include <ctype.h>
#include <stddef.h>  // ptrdiff_t
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/fileloc.h"
#include "lib/vfs/vfs.h"
#include "lib/strutil.h"
#include "lib/util.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define ismode(n, m) ((n & m) == m)

/* Number of attempts to create a temporary file */
#ifndef TMP_MAX
#define TMP_MAX 16384
#endif

#define TMP_SUFFIX ".tmp"

#define ASCII_A    (0x40 + 1)
#define ASCII_Z    (0x40 + 26)
#define ASCII_a    (0x60 + 1)
#define ASCII_z    (0x60 + 26)

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_util_write_backup_content (const char *from_file_name, const char *to_file_name)
{
    FILE *backup_fd;
    char *contents;
    gsize length;
    gboolean ret1 = TRUE;

    if (!g_file_get_contents (from_file_name, &contents, &length, NULL))
        return FALSE;

    backup_fd = fopen (to_file_name, "w");
    if (backup_fd == NULL)
    {
        g_free (contents);
        return FALSE;
    }

    if (fwrite ((const void *) contents, 1, length, backup_fd) != length)
        ret1 = FALSE;

    fflush (backup_fd);
    fclose (backup_fd);

    g_free (contents);
    return ret1;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
is_printable (int c)
{
    c &= 0xff;

    // 0x80..0x9F is a control range in the ISO 8859 family and a range of
    // letters in the DOS and Windows codepages: CP866 keeps the upper case
    // Cyrillic there.  Where mc draws with an 8-bit codepage, that codepage is
    // the one the locale describes, and the locale is the only thing that
    // tells the two apart.
    if (!mc_global.utf8_display && mc_global.display_codepage > 0)
        return isprint (c);

    // Without one -- a 7-bit locale, or no codepage list at all -- mc knows
    // nothing about the terminal, and what it did before stands: pass the high
    // bytes through rather than draw the screen as dots.
    if (mc_global.tty.xterm_flag)
        // "Full 8 bits output" doesn't work on xterm
        return (c > 31 && c < 127) || c >= 160;

    return isprint (c);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Quote the filename for the purpose of inserting it into the command
 * line.  If quote_percent is TRUE, replace "%" with "%%" - the percent is
 * processed by the mc command line.
 */
char *
name_quote (const char *s, gboolean quote_percent)
{
    GString *ret;

    if (s == NULL || *s == '\0')
        return NULL;

    ret = g_string_sized_new (64);

    if (*s == '-')
        g_string_append (ret, "." PATH_SEP_STR);

    for (; *s != '\0'; s++)
    {
        switch (*s)
        {
        case '%':
            if (quote_percent)
                g_string_append_c (ret, '%');
            break;
        case '\'':
        case '\\':
        case '\r':
        case '\n':
        case '\t':
        case '"':
        case ';':
        case ' ':
        case '?':
        case '|':
        case '[':
        case ']':
        case '{':
        case '}':
        case '<':
        case '>':
        case '`':
        case '!':
        case '$':
        case '&':
        case '*':
        case '(':
        case ')':
            g_string_append_c (ret, '\\');
            break;
        case '~':
        case '#':
            if (ret->len == 0)
                g_string_append_c (ret, '\\');
            break;
        default:
            break;
        }
        g_string_append_c (ret, *s);
    }

    return g_string_free (ret, ret->len == 0);
}

/* --------------------------------------------------------------------------------------------- */

char *
fake_name_quote (const char *s, gboolean quote_percent)
{
    (void) quote_percent;

    return (s == NULL || *s == '\0' ? NULL : g_strdup (s));
}

/* --------------------------------------------------------------------------------------------- */
/**
 * path_trunc() is the same as str_trunc() but it deletes possible password from path
 * for security reasons.
 *
 * @param path file path to trancate
 * @param width width (in characters) of truncation result. If negative, full path will be kept.
 *
 * @returns pointer to trancated path. It points to the internal static buffer, do not call
            g_free() to free it.
 */

const char *
path_trunc (const char *path, const ssize_t width)
{
    vfs_path_t *vpath;
    const char *ret;

    vpath = vfs_path_from_str_flags (path, VPF_STRIP_PASSWORD);

    const char *p = vfs_path_as_str (vpath);

    if (width < 0)
        ret = str_trunc (p, -1);
    else
        ret = str_trunc (p, (size_t) width);

    vfs_path_free (vpath, TRUE);

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

const char *
size_trunc (uintmax_t size, gboolean use_si)
{
    static char x[BUF_TINY];
    uintmax_t divisor = 1;
    const char *xtra = _ ("B");

    if (size > 999999999UL)
    {
        divisor = use_si ? 1000 : 1024;
        xtra = use_si ? _ ("kB") : _ ("KiB");

        if (size / divisor > 999999999UL)
        {
            divisor = use_si ? (1000 * 1000) : (1024 * 1024);
            xtra = use_si ? _ ("MB") : _ ("MiB");

            if (size / divisor > 999999999UL)
            {
                divisor = use_si ? (1000 * 1000 * 1000) : (1024 * 1024 * 1024);
                xtra = use_si ? _ ("GB") : _ ("GiB");
            }
        }
    }
    g_snprintf (x, sizeof (x), "%.0f %s", 1.0 * size / divisor, xtra);
    return x;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Print file SIZE to BUFFER, but don't exceed LEN characters,
 * not including trailing 0. BUFFER should be at least LEN+1 long.
 * Avoid floating point by any means.
 *
 * Units: size units (filesystem sizes are 1K blocks)
 *    0=bytes, 1=Kbytes, 2=Mbytes, etc.
 */

/* --------------------------------------------------------------------------------------------- */

const char *
extension (const char *filename)
{
    const char *d;

    d = strrchr (filename, '.');

    return d != NULL ? d + 1 : "";
}

/* --------------------------------------------------------------------------------------------- */
/** The name a file of a given language has: the tag goes before the extension where there is
 * one, so that coole.md becomes coole.ru.md and a file without one, say TAGS, becomes TAGS.ru.
 */

static char *
mc_localized_file_name (const char *path, const char *lang)
{
    const char *base;
    const char *dot;

    base = strrchr (path, PATH_SEP);
    base = base == NULL ? path : base + 1;
    dot = strrchr (base, '.');

    if (dot == NULL || dot == base)
        return g_strconcat (path, ".", lang, (char *) NULL);

    return g_strdup_printf ("%.*s.%s%s", (int) (dot - path), path, lang, dot);
}

/* --------------------------------------------------------------------------------------------- */

char *
load_mc_home_file (const char *from, const char *filename, char **allocated_filename,
                   size_t *length)
{
    char *hintfile_base, *hintfile;
    char *lang;
    char *data;

    hintfile_base = g_build_filename (from, filename, (char *) NULL);
    lang = guess_message_value ();

    hintfile = mc_localized_file_name (hintfile_base, lang);
    if (!g_file_get_contents (hintfile, &data, length, NULL))
    {
        // Fall back to the two-letter language code
        if (lang[0] != '\0' && lang[1] != '\0')
            lang[2] = '\0';
        g_free (hintfile);
        hintfile = mc_localized_file_name (hintfile_base, lang);
        if (!g_file_get_contents (hintfile, &data, length, NULL))
        {
            g_free (hintfile);
            hintfile = hintfile_base;
            g_file_get_contents (hintfile_base, &data, length, NULL);
        }
    }

    g_free (lang);

    if (hintfile != hintfile_base)
        g_free (hintfile_base);

    if (allocated_filename != NULL)
        *allocated_filename = hintfile;
    else
        g_free (hintfile);

    return data;
}

/* --------------------------------------------------------------------------------------------- */

const char *
extract_line (const char *s, const char *top, size_t *len)
{
    static char tmp_line[BUF_MEDIUM];
    char *t = tmp_line;

    while (*s != '\0' && *s != '\n' && (size_t) (t - tmp_line) < sizeof (tmp_line) - 1 && s < top)
        *t++ = *s++;
    *t = '\0';

    if (len != NULL)
        *len = (size_t) (t - tmp_line);

    return tmp_line;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * The basename routine
 */

const char *
x_basename (const char *s)
{
    const char *url_delim, *path_sep;

    url_delim = g_strrstr (s, VFS_PATH_URL_DELIMITER);
    path_sep = strrchr (s, PATH_SEP);

    if (path_sep == NULL)
        return s;

    if (url_delim == NULL || url_delim < path_sep - strlen (VFS_PATH_URL_DELIMITER)
        || url_delim - s + strlen (VFS_PATH_URL_DELIMITER) < strlen (s))
    {
        // avoid trailing PATH_SEP, if present
        if (!IS_PATH_SEP (s[strlen (s) - 1]))
            return path_sep + 1;

        while (--path_sep > s && !IS_PATH_SEP (*path_sep))
            ;
        return (path_sep != s) ? path_sep + 1 : s;
    }

    while (--url_delim > s && !IS_PATH_SEP (*url_delim))
        ;
    while (--url_delim > s && !IS_PATH_SEP (*url_delim))
        ;

    return url_delim == s ? s : url_delim + 1;
}

/* --------------------------------------------------------------------------------------------- */

const char *
unix_error_string (int error_num)
{
    static char buffer[BUF_LARGE];
    gchar *strerror_currentlocale;

    strerror_currentlocale = g_locale_from_utf8 (g_strerror (error_num), -1, NULL, NULL, NULL);
    g_snprintf (buffer, sizeof (buffer), "%s (%d)", strerror_currentlocale, error_num);
    g_free (strerror_currentlocale);

    return buffer;
}

/* --------------------------------------------------------------------------------------------- */

const char *
decompress_extension (int type)
{
    switch (type)
    {
    case COMPRESSION_ZIP:
        return "/uz" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_GZIP:
        return "/ugz" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_BZIP:
        return "/ubz" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_BZIP2:
        return "/ubz2" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_LZIP:
        return "/ulz" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_LZ4:
        return "/ulz4" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_LZMA:
        return "/ulzma" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_LZO:
        return "/ulzo" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_XZ:
        return "/uxz" VFS_PATH_URL_DELIMITER;
    case COMPRESSION_ZSTD:
        return "/uzst" VFS_PATH_URL_DELIMITER;
    default:
        break;
    }
    // Should never reach this place
    fprintf (stderr, "Fatal: decompress_extension called with an unknown argument\n");
    return 0;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Finds out a relative path from first to second, i.e. goes as many ..
 * as needed up in first and then goes down using second
 */

/* --------------------------------------------------------------------------------------------- */
/**
 * Append text to GList, remove all entries with the same text
 */

GList *
list_append_unique (GList *list, char *text)
{
    GList *lc_link;

    /*
     * Go to the last position and traverse the list backwards
     * starting from the second last entry to make sure that we
     * are not removing the current link.
     */
    list = g_list_append (list, text);
    list = g_list_last (list);
    lc_link = g_list_previous (list);

    while (lc_link != NULL)
    {
        GList *newlink;

        newlink = g_list_previous (lc_link);
        if (strcmp ((char *) lc_link->data, text) == 0)
        {
            GList *tmp;

            g_free (lc_link->data);
            tmp = g_list_remove_link (list, lc_link);
            (void) tmp;
            g_list_free_1 (lc_link);
        }
        lc_link = newlink;
    }

    return list;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Read and restore position for the given filename.
 * If there is no stored data, return line 1 and col 0.
 */

MC_MOCKABLE void
load_file_position (const vfs_path_t *filename_vpath, long *line, long *column, off_t *offset,
                    GArray **bookmarks)
{
    char *fn, *filename_str;
    FILE *f;
    char buf[MC_MAXPATHLEN + 100];
    const size_t len = vfs_path_len (filename_vpath);

    // defaults
    *line = 1;
    *column = 0;
    *offset = 0;

    // open file with positions
    fn = mc_config_get_full_path (MC_FILEPOS_FILE);
    f = fopen (fn, "r");
    g_free (fn);
    if (f == NULL)
        return;

    // prepare array for serialized bookmarks
    if (bookmarks != NULL)
        *bookmarks = g_array_sized_new (FALSE, FALSE, sizeof (size_t), MAX_SAVED_BOOKMARKS);

    filename_str = str_escape (vfs_path_as_str (filename_vpath), -1, "", TRUE);

    while (fgets (buf, sizeof (buf), f) != NULL)
    {
        const char *p;
        gchar **pos_tokens;

        // check if the filename matches the beginning of string
        if (strncmp (buf, filename_str, len) != 0)
            continue;

        // followed by single space
        if (buf[len] != ' ')
            continue;

        // and string without spaces
        p = &buf[len + 1];
        if (strchr (p, ' ') != NULL)
            continue;

        pos_tokens = g_strsplit (p, ";", 3 + MAX_SAVED_BOOKMARKS);
        if (pos_tokens[0] == NULL)
        {
            *line = 1;
            *column = 0;
            *offset = 0;
        }
        else
        {
            *line = strtol (pos_tokens[0], NULL, 10);
            if (pos_tokens[1] == NULL)
            {
                *column = 0;
                *offset = 0;
            }
            else
            {
                *column = strtol (pos_tokens[1], NULL, 10);
                if (pos_tokens[2] == NULL)
                    *offset = 0;
                else if (bookmarks != NULL)
                {
                    size_t i;

                    *offset = (off_t) g_ascii_strtoll (pos_tokens[2], NULL, 10);

                    for (i = 0; i < MAX_SAVED_BOOKMARKS && pos_tokens[3 + i] != NULL; i++)
                    {
                        size_t val;

                        val = strtoul (pos_tokens[3 + i], NULL, 10);
                        g_array_append_val (*bookmarks, val);
                    }
                }
            }
        }

        g_strfreev (pos_tokens);
    }

    g_free (filename_str);
    fclose (f);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Save position for the given file
 */

void
save_file_position (const vfs_path_t *filename_vpath, long line, long column, off_t offset,
                    GArray *bookmarks)
{
    static size_t filepos_max_saved_entries = 0;
    char *fn, *tmp_fn;
    char *filename_str = NULL;
    FILE *f, *tmp_f;
    char buf[MC_MAXPATHLEN + 100];
    size_t i;
    const size_t len = vfs_path_len (filename_vpath);
    gboolean src_error = FALSE;

    if (filepos_max_saved_entries == 0)
        filepos_max_saved_entries = mc_config_get_int (mc_global.main_config, CONFIG_APP_SECTION,
                                                       "filepos_max_saved_entries", 1024);

    fn = mc_config_get_full_path (MC_FILEPOS_FILE);
    if (fn == NULL)
        goto early_error;

    mc_util_make_backup_if_possible (fn, TMP_SUFFIX);

    // open file
    f = fopen (fn, "w");
    if (f == NULL)
        goto open_target_error;

    tmp_fn = g_strdup_printf ("%s" TMP_SUFFIX, fn);
    tmp_f = fopen (tmp_fn, "r");
    if (tmp_f == NULL)
    {
        src_error = TRUE;
        goto open_source_error;
    }

    filename_str = str_escape (vfs_path_as_str (filename_vpath), -1, "", TRUE);

    // put the new record
    if (line != 1 || column != 0 || bookmarks != NULL)
    {
        if (fprintf (f, "%s %ld;%ld;%" PRIuMAX, filename_str, line, column, (uintmax_t) offset) < 0)
            goto write_position_error;
        if (bookmarks != NULL)
            for (i = 0; i < bookmarks->len && i < MAX_SAVED_BOOKMARKS; i++)
                if (fprintf (f, ";%zu", g_array_index (bookmarks, size_t, i)) < 0)
                    goto write_position_error;

        if (fprintf (f, "\n") < 0)
            goto write_position_error;
    }

    i = 1;
    while (fgets (buf, sizeof (buf), tmp_f) != NULL)
    {
        if (buf[len] == ' ' && strncmp (buf, filename_str, len) == 0
            && strchr (&buf[len + 1], ' ') == NULL)
            continue;

        fprintf (f, "%s", buf);
        if (++i > filepos_max_saved_entries)
            break;
    }

write_position_error:
    g_free (filename_str);
    fclose (tmp_f);
open_source_error:
    g_free (tmp_fn);
    fclose (f);
    if (src_error)
        mc_util_restore_from_backup_if_possible (fn, TMP_SUFFIX);
    else
        mc_util_unlink_backup_if_possible (fn, TMP_SUFFIX);
open_target_error:
    g_free (fn);
early_error:
    if (bookmarks != NULL)
        g_array_free (bookmarks, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

extern int
ascii_alpha_to_cntrl (int ch)
{
    if ((ch >= ASCII_A && ch <= ASCII_Z) || (ch >= ASCII_a && ch <= ASCII_z))
        ch &= 0x1f;

    return ch;
}

/* --------------------------------------------------------------------------------------------- */

const char *
Q_ (const char *s)
{
    const char *result, *sep;

    result = _ (s);
    sep = strchr (result, '|');

    return sep != NULL ? sep + 1 : result;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_util_make_backup_if_possible (const char *file_name, const char *backup_suffix)
{
    struct stat stat_buf;
    char *backup_path;
    gboolean ret;

    if (!exist_file (file_name))
        return FALSE;

    backup_path = g_strdup_printf ("%s%s", file_name, backup_suffix);
    if (backup_path == NULL)
        return FALSE;

    ret = mc_util_write_backup_content (file_name, backup_path);
    if (ret)
    {
        // Backup file will have same ownership with main file.
        if (stat (file_name, &stat_buf) == 0)
            chmod (backup_path, stat_buf.st_mode);
        else
            chmod (backup_path, S_IRUSR | S_IWUSR);
    }

    g_free (backup_path);

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_util_restore_from_backup_if_possible (const char *file_name, const char *backup_suffix)
{
    gboolean ret;
    char *backup_path;

    backup_path = g_strdup_printf ("%s%s", file_name, backup_suffix);
    if (backup_path == NULL)
        return FALSE;

    ret = mc_util_write_backup_content (backup_path, file_name);
    g_free (backup_path);

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_util_unlink_backup_if_possible (const char *file_name, const char *backup_suffix)
{
    char *backup_path;

    backup_path = g_strdup_printf ("%s%s", file_name, backup_suffix);
    if (backup_path == NULL)
        return FALSE;

    if (exist_file (backup_path))
    {
        vfs_path_t *vpath;

        vpath = vfs_path_from_str (backup_path);
        mc_unlink (vpath);
        vfs_path_free (vpath, TRUE);
    }

    g_free (backup_path);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * partly taken from dcigettext.c, returns "" for default locale
 * value should be freed by calling function g_free()
 */

char *
guess_message_value (void)
{
    static const char *const var[] = {
        // Setting of LC_ALL overwrites all other.
        // Do not use LANGUAGE for check user locale and drowing hints
        "LC_ALL",
        // Next comes the name of the desired category.
        "LC_MESSAGES",
        // Last possibility is the LANG environment variable.
        "LANG",
        // NULL exit loops
        NULL
    };

    size_t i;
    const char *locale = NULL;

    for (i = 0; var[i] != NULL; i++)
    {
        locale = getenv (var[i]);
        if (locale != NULL && locale[0] != '\0')
            break;
    }

    if (locale == NULL)
        locale = "";

    return g_strdup (locale);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * The "profile root" is the tree under which all of MC's user data &
 * settings are stored.
 *
 * It defaults to the user's home dir. The user may override this default
 * with the environment variable $COOLE_PROFILE_ROOT.
 */
const char *
mc_get_profile_root (void)
{
    static const char *profile_root = NULL;

    if (profile_root == NULL)
    {
        profile_root = g_getenv ("COOLE_PROFILE_ROOT");
        if (profile_root == NULL || *profile_root == '\0')
            profile_root = mc_config_get_home_dir ();
    }

    return profile_root;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Propagate error in simple way.
 *
 * @param dest error return location
 * @param code error code
 * @param format printf()-style format for error message
 * @param ... parameters for message format
 */

void
mc_propagate_error (GError **dest, int code, const char *format, ...)
{
    if (dest != NULL && *dest == NULL)
    {
        GError *tmp_error;
        va_list args;

        va_start (args, format);
        tmp_error = g_error_new_valist (MC_ERROR, code, format, args);
        va_end (args);

        g_propagate_error (dest, tmp_error);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Replace existing error in simple way.
 *
 * @param dest error return location
 * @param code error code
 * @param format printf()-style format for error message
 * @param ... parameters for message format
 */

void
mc_replace_error (GError **dest, int code, const char *format, ...)
{
    if (dest != NULL)
    {
        GError *tmp_error;
        va_list args;

        va_start (args, format);
        tmp_error = g_error_new_valist (MC_ERROR, code, format, args);
        va_end (args);

        g_error_free (*dest);
        *dest = NULL;
        g_propagate_error (dest, tmp_error);
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Returns if the given duration has elapsed since the given timestamp,
 * and if it has then updates the timestamp.
 *
 * @param timestamp the last timestamp in microseconds, updated if the given time elapsed
 * @param delay amount of time in microseconds

 * @return TRUE if clock skew detected, FALSE otherwise
 */
gboolean
mc_time_elapsed (gint64 *timestamp, gint64 delay)
{
    gint64 now;

    now = g_get_monotonic_time ();

    if (now >= *timestamp && now < *timestamp + delay)
        return FALSE;

    *timestamp = now;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
