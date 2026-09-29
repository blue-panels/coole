/*
   Init VFS plugins.

   Copyright (C) 2011-2025
   Free Software Foundation, Inc.

   Written by:
   Slava Zanko <slavazanko@gmail.com>, 2011.

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

/** \file
 *  \brief This is a template file (here goes brief description).
 *  \author Author1
 *  \author Author2
 *  \date 20xx
 *
 *  Detailed description.
 */

#include <config.h>

#include "lib/global.h"

#include "local/local.h"

#include "plugins_init.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
vfs_plugins_init (void)
{
    vfs_init_localfs ();
}

/* --------------------------------------------------------------------------------------------- */

void
vfs_plugins_done (void)
{
    // Nothing to undo: the local file system is statically linked.
}

/* --------------------------------------------------------------------------------------------- */
