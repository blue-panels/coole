/*
   Various non-library utilities

   Copyright (C) 2003-2025
   Free Software Foundation, Inc.

   Written by:
   Adam Byrtek, 2003
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

#include <config.h>

#include <errno.h>

#include "lib/global.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "util.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
check_for_default (const char *default_file_path, const char *file_path)
{
    if (!exist_file (file_path))
    {
        GError *error = NULL;
        char *contents = NULL;
        gsize length = 0;
        gboolean ok;

        if (!exist_file (default_file_path))
            return FALSE;

        ok = g_file_get_contents (default_file_path, &contents, &length, &error)
            && g_file_set_contents (file_path, contents, (gssize) length, &error);
        g_free (contents);

        if (!ok)
        {
            message (D_ERROR, MSG_ERROR, _ ("Cannot copy %s to %s:\n%s"), default_file_path,
                     file_path, error->message);
            g_error_free (error);
            return FALSE;
        }
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Report error with one file using errno. Unlike file_error(), this function contains only one
 * button "OK" and returns nothing.
 *
 * @param format printf()-like format for message
 * @param file file name. Can be NULL.
 */

MC_MOCKABLE void
file_error_message (const char *format, const char *filename)
{
    const char *error_string = unix_error_string (errno);

    if (filename == NULL || *filename == '\0')
        message (D_ERROR, MSG_ERROR, "%s\n%s", format, error_string);
    else
    {
        char *full_format;

        full_format = g_strconcat (format, "\n", error_string, (char *) NULL);
        // delete password and try to show a full path
        message (D_ERROR, MSG_ERROR, full_format, path_trunc (filename, -1));
        g_free (full_format);
    }
}

/* --------------------------------------------------------------------------------------------- */
