/** \file query-pg-schema.h
 *  \brief Header: the window Schema of the queries of PostgreSQL
 *
 *  The schemas of the database, their relations and the columns of those, read by a connection
 *  of their own: a query of the user running, or a transaction of it that failed, leaves them
 *  read all the same.
 */

#ifndef MC__QUERY_PG_SCHEMA_H
#define MC__QUERY_PG_SCHEMA_H

#include "lib/global.h"

#include "query-int.h"
#include "query-pq.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct query_pg_schema_t query_pg_schema_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* @schema the schema of MC_PG_SCHEMA, first and unfolded, NULL for none */
query_pg_schema_t *query_pg_schema_new (query_t *q, const query_pq_t *pq, gboolean read_only,
                                        const char *schema);
void query_pg_schema_free (query_pg_schema_t *s);
void query_pg_schema_do (query_pg_schema_t *s, query_schema_action_t action, long line);
/* A command that changes the schemas is committed: they are read again when they were read */
void query_pg_schema_changed (query_pg_schema_t *s);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_PG_SCHEMA_H */
