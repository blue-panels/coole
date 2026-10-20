/** \file lsp-wizard.h
 *  \brief Header: the settings dialog of the language servers
 */

#ifndef MC__LSP_WIZARD_H
#define MC__LSP_WIZARD_H

#include <glib.h>

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The dialog on the settings of the two files: TRUE when the user's file has been written */
gboolean lsp_wizard_run (const char *system_file, const char *user_file);

/*** inline functions ****************************************************************************/

#endif
