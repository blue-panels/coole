/** \file terminal.h
 *  \brief Header: the terminal plugin of the editor
 */

#ifndef MC__EDITOR_PLUGIN_TERMINAL_H
#define MC__EDITOR_PLUGIN_TERMINAL_H

#include "lib/editor-plugin.h"

/*** declarations of public functions ************************************************************/

/* MC_EDITOR_PLUGIN_ENTRY: what the editor calls when it loads the module */
const mc_editor_plugin_t *mc_editor_plugin_register (void);

#endif
