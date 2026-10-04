/** \file project-core.h
 *  \brief Header: the project of a file, its files, and the matching of their names
 */

#ifndef MC__PROJECT_CORE_H
#define MC__PROJECT_CORE_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* the most files a project lists */
#define PROJECT_FILES_MAX 50000

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The root of the project of a file or a directory: the nearest directory up with .coole in it,
   else with .git, else the outermost one with a build file (meson.build, CMakeLists.txt,
   Makefile...) up to the home directory, else the directory of the file itself. */
char *project_find_root (const char *path);

/* Whether a directory is the root of a project by a mark of its own: .coole, .git or a build
   file, not only the directory of a file found by nothing else */
gboolean project_is_project (const char *root);

/* The files of a project, names relative to its root, sorted: what git knows and does not ignore
   in a repository, else what is under the root but hidden directories and build trees; for a
   directory that is no project (project_is_project ()), its own files.  A GPtrArray of char *,
   NULL when the root is no directory. */
GPtrArray *project_list_files (const char *root);

/* How well a name matches what is typed: 0 when it does not, every character of @query in
   @path in order, case aside; more for the characters in the last part of the name, for runs of
   them and for those at the start of a word.  A query with a '/' matches the whole name. */
int project_match_score (const char *path, const char *query);

/* The file that pairs with a source: calc.c with calc.h, x.cpp with x.hpp or x.h...  Looked for
   next to it, then among @files of the project (relative to @root; may be NULL).  NULL when
   there is none. */
char *project_alternate_file (const char *file, const char *root, const GPtrArray *files);

/*** inline functions ****************************************************************************/

#endif /* MC__PROJECT_CORE_H */
