/** \file setup.h
 *  \brief Header: setup loading/saving
 */

#ifndef MC__SETUP_H
#define MC__SETUP_H

#include <config.h>

#include "lib/global.h"  // GError

/*** typedefs(not structures) and defined constants **********************************************/

/* TAB length for the editor */
#define DEFAULT_TAB_SPACING 8

#define MAX_MACRO_LENGTH    1024

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct macro_action_t
{
    long action;
    int ch;
} macro_action_t;

typedef struct macros_t
{
    int hotkey;
    GArray *macro;
} macros_t;

/*** global variables defined in .c file *********************************************************/

/* global parameters */
extern gboolean clear_before_exec;
extern gboolean drop_menus;
extern gboolean verbose;
extern gboolean easy_patterns;
extern int option_tab_spacing;
extern gboolean auto_save_setup;
extern gboolean output_starts_shell;

extern int default_source_codepage;
extern char *autodetect_codeset;
extern gboolean is_autodetect_codeset_enabled;

#ifdef HAVE_ASPELL
extern char *spell_language;
#endif

extern int quit;

/* index to record_macro_buf[], -1 if not recording a macro */
extern int macro_index;

/* macro stuff */
extern struct macro_action_t record_macro_buf[MAX_MACRO_LENGTH];

extern GArray *macros_list;

/*** declarations of public functions ************************************************************/

const char *setup_init (void);
void load_setup (void);
gboolean save_setup (void);
/* Save the setup and say where it went */
void save_setup_cmd (void);
void done_setup (void);

void load_key_defs (void);
char *mc_term_keys_path (void);

/*** inline functions ****************************************************************************/

#endif
