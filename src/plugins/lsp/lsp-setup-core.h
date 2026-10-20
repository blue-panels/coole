/** \file lsp-setup-core.h
 *  \brief Header: what the setup of a project for clangd finds in it, and the .clangd it writes
 *
 *  clangd reads a header alone, and the headers of many C projects count on what the files that
 *  include them include first: alone they have errors the build has not.  And the functions the
 *  tests define again, mocks, take the place of the real ones in the index of clangd.  The setup
 *  finds both in the files of the project and writes the rules that mend them in .clangd, in a
 *  block of its own.
 */

#ifndef MC__LSP_SETUP_CORE_H
#define MC__LSP_SETUP_CORE_H

#include <glib.h>

/*** typedefs(not structures) and defined constants **********************************************/

/* the lines of .clangd around what the setup writes */
#define LSP_SETUP_BEGIN                                                                            \
    "# >>> coole: LSP > Project setup writes what is down to the line \"<<< coole\""
#define LSP_SETUP_END "# <<< coole"

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Whether a file name ends as a header of C or C++ */
gboolean lsp_setup_is_header (const char *name);
/* The directories of tests among @files (names from the root): the start of a name up to a part
   named "test" or "tests"; sorted, NULL ended */
char **lsp_setup_test_dirs (const GPtrArray *files);
/* Whether @file is in one of @dirs */
gboolean lsp_setup_in_dirs (const char *file, char *const *dirs);
/* The headers of the project the .c files, those of the tests aside, include first, as they name
   them ("lib/global.h"), when there is such a file under @root: the most used first, @max at
   most; NULL ended */
char **lsp_setup_first_includes (const char *root, const GPtrArray *files, char *const *tests,
                                 int max);
/* The functions defined both in the directories @tests and outside them: their names, sorted */
GPtrArray *lsp_setup_shadowed (const char *root, const GPtrArray *files, char *const *tests);
/* compile_commands.json, @text, with -include of @includes, files under @root, in every command:
   clangd takes the commands of the headers from those of the files near them, so the headers are
   checked so with no configuration.  NULL when @text is no compilation database. */
char *lsp_setup_cdb_with (const char *text, const char *root, char *const *includes);
/* The errors in what `clangd --check` writes */
int lsp_setup_count_errors (const char *output);
/* The rules: the headers read after @includes, the directories @skip not in the index of the
   background; @absolute_root, when not NULL, makes the paths absolute, for a configuration of
   the user.  NULL when there is no rule. */
char *lsp_setup_rules (char *const *includes, char *const *skip, const char *absolute_root);
/* .clangd as it is, @old (NULL for none), with the block of the setup holding @rules (NULL: no
   block); what is out of the block is kept */
char *lsp_setup_clangd (const char *old, const char *rules);

/*** inline functions ****************************************************************************/

#endif
