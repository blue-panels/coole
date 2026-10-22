/** \file query-core.h
 *  \brief Header: what the query plugin runs, and how it reads what the database says
 */

#ifndef MC__QUERY_CORE_H
#define MC__QUERY_CORE_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* The longest argument a program is given on Linux (MAX_ARG_STRLEN): a longer query cannot go to
   mongosh by --eval */
#define QUERY_TEXT_MAX (128 * 1024 - 1)

/* The comment that asks for the table of a query: "// @table" */
#define QUERY_TABLE_MARK "@table"

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The 0-based line of the byte @offset of @text */
long query_line_of (const char *text, gsize offset);
/* Whether the 0-based @line of @text has nothing but blanks, or is past its end */
gboolean query_line_blank (const char *text, long line);
/* Whether the line that starts at @p is a separator of the queries of a file: "// ---" */
gboolean query_line_separator (const char *p);
/* The section of the 0-based @line of @text: the lines between the separators around it, the
   separator before it being of it; @first its first line, 0-based.  NULL when the text has no
   separator, the whole text being the query. */
char *query_section (const char *text, long line, long *first);
/* A query put into @text as a section of its own: what to insert at the byte @offset, after the
   last line of the section of @line that is not empty, with a separator; the section itself
   when it has nothing in it.  @at is the 0-based line of the query once it is there. */
char *query_put_section (const char *text, long line, const char *query, gsize *offset, long *at);

/* What mongosh runs before the query: db is the database of MC_MONGO_URI, MC_MONGO_DB when given,
   and a cursor is shown to its 1000th document, not to its 20th */
const char *query_mongo_prefix (void);
/* Whether @text asks for the table: a line of it is the comment "// @table" */
gboolean query_wants_table (const char *text);
/* The query of @text for the table: ".toArray()" on a line after it when it ends with a chain of
   a query, find () or aggregate () and what a cursor is sorted and cut with; else @text */
char *query_mongo_for_table (const char *text);
/* The output of mongosh without the warning the prefix makes it print first */
char *query_mongo_clean (const char *out);
/* Where a SyntaxError of mongosh is, by what it printed: "SyntaxError: ... (3:0)" is the line 3,
   from 1, the column 0, of the text; FALSE when it is no SyntaxError */
gboolean query_mongo_error_position (const char *err, long *line, long *column);
/* Whether the output of mongosh --json is the error of a cursor it could not print */
gboolean query_mongo_is_cursor (const char *out);
/* The value mongosh --json printed last, after what print () printed: the start of a line from
   which the rest is JSON; NULL when there is none */
const char *query_mongo_json_tail (const char *out);
/* The message of the error that is the value @json, the tail of what mongosh --json printed;
   NULL when it is no error, or @json NULL */
char *query_mongo_json_error (const char *json);
/* The server of a mongodb:// URI and the database, "host:port/db", without the user and the
   password */
char *query_mongo_endpoint (const char *uri, const char *db);

/* The table of Markdown of the documents of the Extended JSON @json, the tail of what mongosh
   --json printed: a column for each key, the rows the documents of an array; @rows their
   number.  NULL with @error when it is no JSON. */
char *query_table (const char *json, guint *rows, GError **error);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_CORE_H */
