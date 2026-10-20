/** \file ctags-fuzzy.h
 *  \brief Header: fuzzy symbol matching
 */

#ifndef MC__CTAGS_FUZZY_H
#define MC__CTAGS_FUZZY_H

#include "lib/global.h"

#include "ctags-parser.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Search @entries for fuzzy matches against @query (src/fuzzy.h).
 * Returns a new GPtrArray of borrowed ctags_entry_t* pointers sorted by score descending,
 * or NULL if nothing matches.  Free with g_ptr_array_free(result, FALSE). */
GPtrArray *ctags_fuzzy_search (const GPtrArray *entries, const char *query);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_FUZZY_H */
