/** \file  fileloc.h
 *  \brief Header: config files list
 *
 *  This file defines the locations of the various user specific
 *  configuration files of coole. The system wide and the user specific
 *  file names are not always the same, so don't use these names for
 *  finding system wide configuration files.
 */

#ifndef MC_FILELOC_H
#define MC_FILELOC_H

/*** typedefs(not structures) and defined constants **********************************************/

#ifndef MC_USERCONF_DIR
#define MC_USERCONF_DIR "coole"
#endif

#define TAGS_NAME             "TAGS"

#define MC_GLOBAL_CONFIG_FILE "defaults.ini"
#define MC_HELP_DIR           "help"
#define MC_HELP               MC_HELP_DIR PATH_SEP_STR "coole.md"
#define GLOBAL_KEYMAP_FILE    "keymap.ini"
#define CHARSETS_LIST         "charsets"
#define MC_MACRO_FILE         "macros"

#define MC_CONFIG_FILE        "ini"
#define MC_FILEPOS_FILE       "filepos"
#define MC_HISTORY_FILE       "history"

#define MC_SKINS_DIR          "skins"

#define MC_ZDOTDIR_SUBDIR     "zdotdir"  // the startup files the terminal gives zsh

/* file names */
#define EDIT_HOME_MACRO_FILE "macros.d" PATH_SEP_STR "macro"
#define EDIT_HOME_CLIP_FILE  "clipboard"
#define EDIT_HOME_BLOCK_FILE "block"
#define EDIT_HOME_TEMP_FILE  "temp"
#define EDIT_SYNTAX_DIR      "syntax"
#define EDIT_SYNTAX_FILE     EDIT_SYNTAX_DIR PATH_SEP_STR "Syntax"

#define EDIT_GLOBAL_MENU     "coole.menu"
#define EDIT_LOCAL_MENU      ".coole.menu"
#define EDIT_HOME_MENU       "menu"

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/*** inline functions ****************************************************************************/

#endif
