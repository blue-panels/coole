/** \file lsp-config.h
 *  \brief Header: the language servers the user has, from lsp.json
 */

#ifndef MC__LSP_CONFIG_H
#define MC__LSP_CONFIG_H

#include <glib.h>
#include <json-glib/json-glib.h>

/*** typedefs(not structures) and defined constants **********************************************/

#define LSP_CONFIG_FILE "lsp.json"

/*** enums ***************************************************************************************/

/* When something happens by itself: a server starts as a file it serves is opened, a completion
   comes after a dot */
typedef enum
{
    LSP_WHEN_UNSET = -1,  // a backend that has the setting of the file
    LSP_WHEN_NEVER = 0,   // by the command only: LSP Start, LSP Completion
    LSP_WHEN_IDE,         // when coole was started on a project, with -D
    LSP_WHEN_ALWAYS
} lsp_when_t;

/*** structures declarations (and typedefs of structures)*****************************************/

/* A language server and the files it serves */
typedef struct
{
    char *id;                // "clangd"
    char **command;          // its argv, NULL ended
    GHashTable *extensions;  // the end of a file name to the language id: ".c" to "c"
    char **root_markers;     // names, or patterns of one name, that mark the root of a project
    gboolean single_file;    // a file in no project is served all the same, from its directory
    gboolean enabled;        // false: the backend is there, and serves nothing
    lsp_when_t autostart;
    JsonObject *environment;           // the variables to set, a null one taken away; NULL: none
    JsonNode *initialization_options;  // NULL: none
    JsonNode *settings;                // NULL: none
    char *startup;       // serves only when coole was started with this option, "query" for -Q,
                         // then before the others; NULL: whatever the option
    JsonNode *requests;  // sent after initialized, an array of {method, params}; NULL: none
    char **words;        // completed with what the server says, not after a dot; NULL: none
    char *ascii_text;    // "sql": the characters not of ASCII of the comments and the strings of
                         // the text it gets made '_', the positions in UTF-16; NULL: as it is
} lsp_backend_t;

typedef struct
{
    lsp_when_t autostart;
    lsp_when_t completion_on_trigger;
    lsp_when_t completion_on_arguments;  // the list after "(" and ","
    GPtrArray *backends;                 // lsp_backend_t, the user's first
    // whether the file said them: those of the user's file take the place of the system's
    gboolean has_autostart;
    gboolean has_completion_on_trigger;
    gboolean has_completion_on_arguments;
} lsp_config_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* "never", "ide" or "always", true or false: FALSE when the node is none of them */
gboolean lsp_when_parse (JsonNode *node, lsp_when_t *when);
const char *lsp_when_name (lsp_when_t when);

/* The configuration of the system file and of the user's, either of them NULL or missing: a
   setting or a backend of the user takes the place of the system's one.  What is wrong in a
   file goes to @errors, a line each, and the file is not taken. */
lsp_config_t *lsp_config_load (const char *system_file, const char *user_file, GString *errors);
/* The configuration of a text; FALSE with @error when it is no configuration */
lsp_config_t *lsp_config_parse (const char *json, gssize len, GError **error);
void lsp_config_free (lsp_config_t *config);

/* The backend of a file and the language of the file for it: the first enabled that has the end
   of its name, the longest end of a backend counting, and whose program is there; NULL when
   none */
const lsp_backend_t *lsp_config_backend_for (const lsp_config_t *config, const char *path,
                                             const char **language_id);
const lsp_backend_t *lsp_config_backend (const lsp_config_t *config, const char *id);
/* Whether the program of @backend can be run */
gboolean lsp_backend_runnable (const lsp_backend_t *backend);
lsp_when_t lsp_backend_autostart (const lsp_config_t *config, const lsp_backend_t *backend);
/* The backends for how coole was started, @started telling whether it was with an option: one
   with "startup" serves only then, and comes before the others */
void lsp_config_for_startup (lsp_config_t *config,
                             gboolean (*started) (const char *option, void *data), void *data);
/* A copy of @node, its strings with ${env:NAME} as the variable of the environment, empty when
   it has none, and ${command_dir} as @command_dir */
JsonNode *lsp_config_expand (JsonNode *node, const char *command_dir);

/* The root the server of @backend has for @path: @project when it holds the file, else the
   nearest directory with .coole, else with a root marker of the backend, else with .git, .hg or
   .svn, else the directory of the file when the backend serves single files; NULL when none. */
char *lsp_find_root (const lsp_backend_t *backend, const char *path, const char *project);

/* The command of a server for a project: "${build_dir}" in an argument is the build directory of
   the project; an argument with it is left out when the project has none (@build_dir NULL).
   NULL ended, free with g_strfreev(). */
char **lsp_command_expand (char *const *command, const char *build_dir);

/* The setting @section, "a.b.c", of @settings; all of them for a NULL or empty section; NULL when
   there is none.  The node is one of @settings. */
JsonNode *lsp_settings_lookup (JsonNode *settings, const char *section);

/*** inline functions ****************************************************************************/

#endif
