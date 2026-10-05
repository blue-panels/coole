#ifndef MC__ARGS_H
#define MC__ARGS_H

#include "lib/global.h"  // gboolean

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

extern gboolean mc_args__force_xterm;
extern gboolean mc_args__nomouse;
extern gboolean mc_args__force_colors;
extern gboolean mc_args__nokeymap;
extern gboolean mc_args__no_lua;
extern char *mc_args__keymap_file;
extern char *mc_args__debug_project;

/* The files to edit: a list of edit_arg_t */
extern GList *mc_args__edit_files;

/*** declarations of public functions ************************************************************/

gboolean mc_args_parse (int *argc, char ***argv, const char *translation_domain, GError **mcerror);
gboolean mc_args_show_info (void);
gboolean mc_setup_by_args (int argc, char **argv, GError **mcerror);

/*** inline functions ****************************************************************************/

#endif
