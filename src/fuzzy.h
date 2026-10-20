/** \file fuzzy.h
 *  \brief Header: the fuzzy match of a name by what is typed
 *
 *  The characters typed are found in the name in their order, wherever they are: "wid" finds
 *  "Width", "fillWidth" and "WorkingId".  Of the ways they are found, the best is taken: the
 *  characters at the start of the words of the name, and those one after the other, count most.
 */

#ifndef MC__FUZZY_H
#define MC__FUZZY_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Whether every character of @query is in @name, in order, whatever their case */
gboolean fuzzy_match (const char *name, const char *query);
/* How well @query fits @name, higher the better; 0 when it does not, and for an empty @query.
   @positions, when not NULL, gets the bytes of @name the characters of @query are found at, as
   int. */
int fuzzy_score (const char *name, const char *query, GArray *positions);

/*** inline functions ****************************************************************************/

#endif
