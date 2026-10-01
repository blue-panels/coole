/** \file  execute.h
 *  \brief Header: execution routines
 */

#ifndef MC__EXECUTE_H
#define MC__EXECUTE_H

#include "lib/util.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/* If true, after executing a command, wait for a keystroke */
enum
{
    pause_never,
    pause_on_dumb_terminals,
    pause_always
};

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

extern int pause_after_run;

/*** declarations of public functions ************************************************************/

/* Execute functions that use the shell to execute */
void shell_execute (const char *command, int flags);

/* Show the terminal behind the editor: Ctrl-O */

/* Suspend the editor: Ctrl-Z */
gboolean execute_suspend (const gchar *event_group_name, const gchar *event_name,
                          gpointer init_data, gpointer data);

void pre_exec (void);

/*** inline functions ****************************************************************************/

#endif
