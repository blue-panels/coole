/** \file query-export.h
 *  \brief Header: the rows of a query written to a file, CSV, JSON, SQL, Markdown
 */

#ifndef MC__QUERY_EXPORT_H
#define MC__QUERY_EXPORT_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* The documents of mongo whose keys are the columns of a table, CSV and the like; a key of a
   document after them is left out */
#define QUERY_EXPORT_SAMPLE 200

/* The line mongosh prints with a document of the export: "\x1e" and the tag, its Extended JSON
   after it */
#define QUERY_EXPORT_TAG  "bluerig-row"
#define QUERY_EXPORT_LINE "\x1e" QUERY_EXPORT_TAG " "

/*** enums ***************************************************************************************/

typedef enum
{
    QUERY_EXPORT_CSV,
    QUERY_EXPORT_JSON,    // an array of objects
    QUERY_EXPORT_NDJSON,  // an object on each line
    QUERY_EXPORT_SQL,     // INSERT
    QUERY_EXPORT_MARKDOWN,
    QUERY_EXPORT_FORMATS
} query_export_format_t;

/* What a value is, for the formats that write it by its type: JSON and SQL */
typedef enum
{
    QUERY_VALUE_TEXT,
    QUERY_VALUE_NUMBER,
    QUERY_VALUE_BOOL,  // "t", "true", "f", "false"
    QUERY_VALUE_JSON   // a JSON of its own, written as it is
} query_value_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    query_export_format_t format;
    // CSV
    char delimiter;
    gboolean header;     // the names of the columns on the first line
    gboolean quote_all;  // every value in quotes, else those that need them
    gboolean null_word;  // NULL is the word NULL, else nothing
    // JSON: an object on lines of their own, a key a line
    gboolean indent;
    // SQL: INSERT INTO @table, @per_insert rows a statement, after its CREATE TABLE when
    // @create_table
    char *table;
    int per_insert;
    gboolean create_table;
    // Markdown: NULL is the word NULL, else nothing
    gboolean null_markdown;
} query_export_options_t;

typedef struct query_export_t query_export_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* The extension of the files of @format, "csv" */
const char *query_export_extension (query_export_format_t format);

/* A writer of rows in the format of @options, which are copied */
query_export_t *query_export_new (const query_export_options_t *options);
void query_export_free (query_export_t *x);

/* The types of SQL of the columns, "integer", "character varying(20)", before their names: those
   of CREATE TABLE, "text" without them */
void query_export_types (query_export_t *x, GPtrArray *types);
/* The names of the columns of the rows, before the first of them */
void query_export_columns (query_export_t *x, GPtrArray *names);
/* A row: its values of @lens bytes, of @kinds, as many as the columns; a NULL value is NULL */
void query_export_row (query_export_t *x, const char *const *values, const gsize *lens,
                       const query_value_t *kinds);
/* A document of mongo, Extended JSON @json, as a row: JSON as it is, else its keys the columns,
   those of the first QUERY_EXPORT_SAMPLE documents; FALSE with @error when it is no JSON */
gboolean query_export_document (query_export_t *x, const char *json, GError **error);
/* The end of the rows: what is to be written after the last one */
void query_export_end (query_export_t *x);

/* What is written so far and not yet taken: the caller writes it out and empties it */
GString *query_export_text (query_export_t *x);
/* The rows written */
gint64 query_export_rows (const query_export_t *x);
/* The keys of documents left out, no column of them, "a, b"; NULL for none */
char *query_export_skipped (const query_export_t *x);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_EXPORT_H */
