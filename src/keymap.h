#ifndef MC__KEYBIND_DEFAULTS_H
#define MC__KEYBIND_DEFAULTS_H

#include "lib/global.h"
#include "lib/keybind.h"   // global_keymap_t
#include "lib/mcconfig.h"  // mc_config_t

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

extern GArray *dialog_keymap;
extern GArray *menu_keymap;
extern GArray *input_keymap;
extern GArray *listbox_keymap;
extern GArray *radio_keymap;
extern GArray *help_keymap;
extern GArray *editor_keymap;
extern GArray *editor_x_keymap;

extern const global_keymap_t *help_map;
extern const global_keymap_t *editor_map;
extern const global_keymap_t *editor_x_map;

/*** declarations of public functions ************************************************************/

void keymap_load (gboolean load_from_file);
void keymap_free (void);
void keymap_save_old_maps (void);
void keymap_refresh_widgets (void);

/*** inline functions ****************************************************************************/

#endif
