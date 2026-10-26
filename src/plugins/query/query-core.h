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

/* The comments that ask for the table of a query, "// @table", and for a limit to the documents
   of its cursor, "// @limit 20", 100 without a number */
#define QUERY_TABLE_MARK    "table"
#define QUERY_LIMIT_MARK    "limit"
#define QUERY_LIMIT_DEFAULT 100

/* The line printjson () prints with a copy of an array for the tab Table: "\x1e" and the tag */
#define QUERY_TABLE_TAG  "coole-table"
#define QUERY_TABLE_LINE "\x1e" QUERY_TABLE_TAG " "

/* The programs of AI MC_QUERY_AI names by a word, with what they are run with: the request on
   stdin, the answer alone on stdout, no tool of theirs run */
#define QUERY_AI_CLAUDE "claude"
#define QUERY_AI_CODEX  "codex"

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The end of the line at @p: its newline, or the end of the text */
const char *query_line_end (const char *p);
/* Whether the line at @p, up to @end, has nothing but blanks */
gboolean query_blank_line (const char *p, const char *end);
/* The 0-based line of the byte @offset of @text */
long query_line_of (const char *text, gsize offset);
/* Whether the line that starts at @p is a separator of the queries of a file: "// ---" */
gboolean query_line_separator (const char *p);
/* The section of the 0-based @line of @text: the lines between the separators around it, the
   separator before it being of it; @first its first line, 0-based.  NULL when the text has no
   separator, the whole text being the query. */
char *query_section (const char *text, long line, long *first);

/* What mongosh runs before the query: db is the database of MC_MONGO_URI, MC_MONGO_DB when given,
   and a cursor is shown to its 1000th document, not to its 20th */
const char *query_mongo_prefix (void);
/* Whether @text asks for the table: a line of it is the comment "// @table" */
gboolean query_wants_table (const char *text);
/* The number of a line "// @limit N" of @text, QUERY_LIMIT_DEFAULT without one; 0 when there is
   no such line */
long query_wants_limit (const char *text);
/* The query of @text with a limit to its documents: ".limit(N)" on a line after a chain of find
   (), a stage $limit at the end of the pipeline of aggregate ([...]); @done FALSE, and @text,
   when it has a limit of its own, or ends with neither */
char *query_mongo_with_limit (const char *text, long limit, gboolean *done);
/* The query of @text with its calls use ("name") as db = db.getSiblingDB ("name"): mongosh with
   no database of its own takes no use (), and a language server takes the database of the
   queries from it.  The lines stay where they are. */
char *query_mongo_use (const char *text);
/* The database of the last use ("name") of @text above its 0-based @line, as the string is
   written, quotes and all: that of a part of the file run alone; NULL when there is none */
char *query_use_before (const char *text, long line);
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
/* The database: @db when given, else that of the path of the URI; NULL when there is none */
char *query_mongo_db (const char *uri, const char *db);
/* Whether the query @text may make or drop a collection, a view or an index: it calls
   createCollection (), drop (), insertOne () and the like, an insert making its collection */
gboolean query_mongo_changes_schema (const char *text);
/* Whether @text has a call use ("name"), not in a string or a comment */
gboolean query_has_use (const char *text);
/* What the file of queries of the database @db starts with: use("db"); and an empty line */
char *query_use_line (const char *db);

/* The table of Markdown of the names @columns and of @cells, row after row, columns->len cells
   each, made one line each by query_cell_text (): a column for each name, a cell cut to 40
   columns of the terminal, the columns lined up */
char *query_grid (GPtrArray *columns, GPtrArray *cells);
/* @s as a cell of query_grid (): on one line, its '|' no end of the cell */
char *query_cell_text (const char *s);
/* The byte of the character @chars, from 0, of the UTF-8 @text of @len bytes, PostgreSQL telling
   where an error is in characters; @len for one past its end */
gsize query_utf8_offset (const char *text, gsize len, long chars);
/* @name as an identifier of SQL: as it is when it is one already, lower case letters of ASCII,
   digits and '_' and no keyword, else in double quotes, those in it doubled */
char *query_pg_ident (const char *name);
/* The query of the rows of the relation @relation of the schema @schema, NULL or "" for none:
   "SELECT * FROM sales.orders LIMIT 100;" */
char *query_pg_relation_query (const char *schema, const char *relation);

/* The request to AI of the lines @first to @last, 0-based, of @text; with @paragraph the lines
   around @first that are not empty; the place of the answer from the byte @start to @end, what
   is to stay of the request there @kept, the answer after it.  Comments, @comment "-- ", over a
   query are the request alone, the query being the answer of before, which goes.  Else all the
   lines are the request: the words of the user at their ends made comments, a query between them
   that of the user, kept, the answer a query of its own after it, @below; with no query all its
   words made comments, the answer under them.  A directive, "// @table", is of the query.  The
   text of the request, the comments taken off; NULL when it has none. */
char *query_ai_part (const char *text, long first, long last, gboolean paragraph,
                     const char *comment, gsize *start, gsize *end, char **kept, gboolean *below);
/* The query of what AI answered, @out: what its blocks ```...``` have, else all of it when no
   line of it is words; @notes what it wrote around those blocks, NULL for nothing.  NULL when it
   has no query: it asked something, or answered in words. */
char *query_ai_answer (const char *out, char **notes);
/* The command of MC_QUERY_AI, @value: claude without one, QUERY_AI_CLAUDE and QUERY_AI_CODEX
   with what they are run with, else a command line of the shell; NULL with @error when it is
   none.  Freed with g_strfreev (). */
char **query_ai_argv (const char *value, GError **error);
/* What AI is told: to write the query of @question in @language, with no comment in it, its
   notes after it, what it knows of the database being @context */
char *query_ai_prompt (const char *language, const char *context, const char *question);

/* The directory of the versions of the file of queries @file, under @versions: the name of
   the file in @queries, the folder of the files of queries of -Q, else the name and 8 letters of
   the hash of its path, for two q.js of two places not to mix */
char *query_versions_dir (const char *versions, const char *queries, const char *file);
/* The name of the version of the time @now, "20261024-173750.js", @extension that of the file of
   queries; "...-2.js" and on when @taken says that one is there */
char *query_version_name (GDateTime *now, const char *extension,
                          gboolean (*taken) (const char *name, void *data), void *data);
/* The names of @names, sorted, that go for the newest @keep to stay: those of the oldest;
   NULL ended, freed with g_strfreev () */
char **query_versions_over (char **names, guint keep);
/* The order of the versions @a and @b by their time, those of a second by their number:
   "...-173750.js", "...-173750-2.js", "...-173750-10.js" */
int query_version_cmp (const char *a, const char *b);
/* The time of the version @name, "20261024-173750-2.js", as History shows it: "24.10 17:37:50";
   @name itself when it is no such name */
char *query_version_label (const char *name);
/* The lines of @now that @before has not, @added, and those of @before that @now has not,
   @removed, each line counted as often as it is there; @before NULL has none */
void query_line_changes (const char *before, const char *now, int *added, int *removed);

/* The table of Markdown of the documents of the Extended JSON @json: a column for each key, the
   rows the first @max documents of an array, all of them when @max is 0; @rows the number of
   all of them.  NULL with @error when it is no JSON. */
char *query_table (const char *json, guint max, guint *rows, GError **error);
/* What mongosh printed without the copies of the arrays printjson () and print () printed, the
   lines that start with QUERY_TABLE_LINE; their Extended JSON added to @tables, in their order */
char *query_take_tables (const char *out, GPtrArray *tables);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_CORE_H */
