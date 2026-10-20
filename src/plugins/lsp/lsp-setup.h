/** \file lsp-setup.h
 *  \brief Header: the setup of a project for its language server
 *
 *  LSP > Project setup looks at the project as the server sees it and says what goes wrong and
 *  how to mend it: for clangd, the headers that have errors alone the build has not, and the
 *  functions the tests define again.  What the user takes is written in the configuration of the
 *  server in the project, .clangd.
 */

#ifndef MC__LSP_SETUP_H
#define MC__LSP_SETUP_H

#include <glib.h>

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Whether the setup knows the server of the command @program */
gboolean lsp_setup_knows (const char *program);
/* The setup of the project at @root for the server @program: the checks, with a dialog of their
   progress, then what they found; TRUE when the configuration of the server has been written */
gboolean lsp_setup_run (const char *root, const char *program);

/*** inline functions ****************************************************************************/

#endif
