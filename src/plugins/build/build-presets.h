/** \file build-presets.h
 *  \brief Header: the defaults of a project by its language, from presets.ini
 *
 *  How a project of a language is built and debugged when it says nothing of its own: a group
 *  of presets.ini for each language, the groups of the user first, those of coole of the same
 *  name left out.  The language of a project is told by a file in its root, a marker, else by
 *  the ending of a file.
 *
 *  A project opened as one takes a copy of the preset of its language, .coole/project.ini, its
 *  group [project]: the project has it from then on, whatever presets.ini becomes.
 */

#ifndef MC__BUILD_PRESETS_H
#define MC__BUILD_PRESETS_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define BUILD_PRESETS_FILE "presets.ini"
/* The copy of a project, from its root */
#define BUILD_PROJECT_DIR   ".coole"
#define BUILD_PROJECT_FILE  "project.ini"
#define BUILD_PROJECT_GROUP "project"

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* A language: NULL for what its group has not */
typedef struct
{
    char *id;          /* the name of its group; the language of the copy of a project */
    gboolean project;  /* the copy of a project, its language whatever its markers */
    char **markers;    /* files in the root of a project of it */
    char **extensions; /* endings of its files, the dot with them */
    char *build;       /* the command that builds a project of it, run in the root */
    char **gdb;        /* the gdbs that debug its programs, the first there in PATH */
    char *program;     /* the program to debug, from the root; ${package}: that of Cargo.toml */
    char *entry;       /* a line of a file that makes its directory a program an adapter runs */
    char **hide;       /* the programs not to choose from: patterns of names from the root */
    char *adapter;     /* the debug adapter of its scripts */
    char *address;     /* host:port the adapter listens on, 0 for a port of its own */
    char *launch;      /* what the request launch of the adapter needs, a JSON object */
} build_preset_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The copy of the project of @project_text, then the presets of @user_text, then those of
   @system_text whose name the user has not; any NULL for none.  A wrong @project_text or
   @user_text is left out, @project_error or @user_error set. */
GPtrArray *build_presets_parse (const char *project_text, const char *user_text,
                                const char *system_text, GError **project_error,
                                GError **user_error);
/* The language of the project of @root: its copy, else by the markers; else, when @path is not
   NULL, that of the file @path by its ending; NULL when none */
const build_preset_t *build_presets_find (GPtrArray *presets, const char *root, const char *path);
/* The text of .coole/project.ini of a project of the language of @preset; its build command
   only when @with_build: a project with a build system of its own builds with it */
char *build_presets_project_text (const build_preset_t *preset, gboolean with_build);
/* @value with ${package} put as the name of the package of the Cargo.toml of @root; NULL when
   it has ${package} and the project has no package */
char *build_presets_expand (const char *value, const char *root);

/* The name of the package of the Cargo.toml of @root; NULL when it has none */
char *build_cargo_package (const char *root);

/*** inline functions ****************************************************************************/

#endif /* MC__BUILD_PRESETS_H */
