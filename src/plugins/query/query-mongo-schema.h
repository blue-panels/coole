/** \file query-mongo-schema.h
 *  \brief Header: the window Schema of the queries of MongoDB
 *
 *  The databases, their collections, views and time series, and the fields and indexes of
 *  those, read by a mongosh of their own: mongosh keeps no connection, a query of the user
 *  running leaves them read all the same.
 */

#ifndef MC__QUERY_MONGO_SCHEMA_H
#define MC__QUERY_MONGO_SCHEMA_H

#include "lib/global.h"

#include "query-int.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct query_mongo_schema_t query_mongo_schema_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* @mongosh the program that reads them */
query_mongo_schema_t *query_mongo_schema_new (query_t *q, const char *mongosh);
void query_mongo_schema_free (query_mongo_schema_t *s);
void query_mongo_schema_do (query_mongo_schema_t *s, query_schema_action_t action, long line);
/* A query that may have made or dropped a collection or an index has ended: the tree is read
   again when it was read */
void query_mongo_schema_changed (query_mongo_schema_t *s);
/* What AI is told of the database of @use, the string of a use () above the request, NULL for
   that of the connection, read for query_ai_context () */
void query_mongo_schema_ai (query_mongo_schema_t *s, const char *use);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_MONGO_SCHEMA_H */
