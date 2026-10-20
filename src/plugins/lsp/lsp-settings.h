/** \file lsp-settings.h
 *  \brief Header: lsp.json as the settings dialog changes it
 *
 *  The settings are those of the system's lsp.json with the user's over them.  What the dialog
 *  changes goes to the user's file, and only as far as it is not the system's: a backend set back
 *  as the system has it leaves the user's file.  What the user's file has besides is kept.
 */

#ifndef MC__LSP_SETTINGS_H
#define MC__LSP_SETTINGS_H

#include <glib.h>
#include <json-glib/json-glib.h>

#include "lsp-config.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct lsp_settings_t lsp_settings_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The settings of the two files; NULL with @error when the user's file is there and broken: it
   is not written over then.  A broken system file counts as none. */
lsp_settings_t *lsp_settings_load (const char *system_file, const char *user_file, GError **error);
void lsp_settings_free (lsp_settings_t *settings);
/* The user's file, with what has been changed; FALSE with @error when it cannot be written */
gboolean lsp_settings_save (lsp_settings_t *settings, GError **error);

/* "autostart" or "completion_on_trigger" */
lsp_when_t lsp_settings_when (const lsp_settings_t *settings, const char *key);
void lsp_settings_set_when (lsp_settings_t *settings, const char *key, lsp_when_t when);

/* The ids of the backends, the user's first; free with g_ptr_array_free() */
GPtrArray *lsp_settings_ids (const lsp_settings_t *settings);
/* A backend as it is now, the user's or the system's; NULL when there is none.  It is the
   settings': a change goes by lsp_settings_put(). */
JsonObject *lsp_settings_backend (const lsp_settings_t *settings, const char *id);
/* Whether the system has the backend, and whether the user has one of the same id over it */
gboolean lsp_settings_in_system (const lsp_settings_t *settings, const char *id);
gboolean lsp_settings_changed (const lsp_settings_t *settings, const char *id);
/* A backend, @backend taken: the user's from now, unless it is just the system's */
void lsp_settings_put (lsp_settings_t *settings, JsonObject *backend);
/* The user's backend @id taken away: the system's again, or none */
void lsp_settings_drop (lsp_settings_t *settings, const char *id);

/*** inline functions ****************************************************************************/

#endif
