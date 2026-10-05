/** \file build-core.h
 *  \brief Header: how a project is built, what it builds, and what the compiler says
 */

#ifndef MC__BUILD_CORE_H
#define MC__BUILD_CORE_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

typedef enum
{
    BUILD_NONE,
    BUILD_MESON,
    BUILD_CMAKE,
    BUILD_MAKE
} build_system_t;

/*** structures declarations (and typedefs of structures)*****************************************/

/* How a project is built */
typedef struct
{
    build_system_t system;
    char *build_dir; /* where the build is: the build directory of meson or cmake, else the root */
    char *command;   /* the command that builds it, run in the root */
} build_info_t;

/* What an executable says of itself */
typedef struct
{
    gboolean executable; /* an ELF program, not a library or an object */
    gboolean debug_info; /* it has .debug_info */
    gboolean optimized;  /* the compiler was told -O1 or more */
} build_elf_t;

/* A line of the compiler about a file */
typedef struct
{
    char *file; /* absolute */
    long line;
    long column;
    gboolean error; /* else a warning or a note */
    char *message;
} build_diagnostic_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* How the project of @root is built: meson with a build directory in it or next to it whose
   source is @root, cmake likewise, else make with a Makefile in @root; NULL when none */
build_info_t *build_detect (const char *root);
void build_info_free (build_info_t *info);

/* Read the ELF headers of a file; FALSE when it is no ELF file */
gboolean build_elf_read (const char *path, build_elf_t *elf);

/* The programs with debug information under @dir, a few levels down, the newest first: a
   GPtrArray of absolute names */
GPtrArray *build_find_programs (const char *dir);

/* A line of gcc or clang: "file:line:col: error: message"; NULL when it is none.  A relative
   name is taken from @dir, where the compiler ran. */
build_diagnostic_t *build_parse_diagnostic (const char *line, const char *dir);
void build_diagnostic_free (build_diagnostic_t *d);

/*** inline functions ****************************************************************************/

#endif /* MC__BUILD_CORE_H */
