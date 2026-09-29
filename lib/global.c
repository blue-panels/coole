/*
   Global structure for some library-related variables

   Copyright (C) 2009-2025
   Free Software Foundation, Inc.

   Written by:
   Slava Zanko <slavazanko@gmail.com>, 2009.

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

/** \file glibcompat.c
 *  \brief Source: global structure for some library-related variables
 *
 */

#include <config.h>

#include "mc-version.h"

#include "global.h"

/*** global variables ****************************************************************************/

mc_global_t mc_global = {
    .mc_version = MC_CURRENT_VERSION,

    .shutdown = FALSE,

    .sysconfig_dir = NULL,
    .share_data_dir = NULL,

    .profile_name = NULL,

    .source_codepage = -1,
    .display_codepage = -1,
    .utf8_display = FALSE,

    .keybar_visible = TRUE,

    .widget = { .confirm_history_cleanup = TRUE, .show_all_if_ambiguous = FALSE },

    .shell = NULL,

    .tty = { .skin = NULL,
             .shadows = TRUE,
             .setup_color_string = NULL,
             .term_color_string = NULL,
             .color_terminal_string = NULL,
             .xterm_flag = FALSE,
             .disable_x11 = FALSE,
             .slow_terminal = FALSE,
             .disable_colors = FALSE,
             .ugly_line_drawing = FALSE,
             .old_mouse = FALSE,
             .alternate_plus_minus = FALSE },
};

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/*** public functions ****************************************************************************/

char *
mc_get_package_copyright (void)
{
    const size_t last_year = 2025;
    return g_strdup_printf (_ ("Copyright (C) 1996-%zu the Free Software Foundation"), last_year);
}

/* --------------------------------------------------------------------------------------------- */
