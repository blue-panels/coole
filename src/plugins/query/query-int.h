/*
   The query plugin: what its files share, the state of the plugin and what a backend does.

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole.

   coole is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   coole is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/** \file query-int.h
 *  \brief Header: what the files of the query plugin share
 *
 *  The plugin keeps the tabs, History, the versions of the file and the marks; a backend runs a
 *  query in its database and fills Output and Table when it ends.
 */

#ifndef MC__QUERY_INT_H
#define MC__QUERY_INT_H

#include "lib/global.h"
#include "lib/editor-plugin.h"

/*** typedefs(not structures) and defined constants **********************************************/

// what a run prints that is shown; the rest is cut
#define QUERY_OUTPUT_MAX (8 * 1024 * 1024)
// the rows of a table of the tab Table at most, the rest counted
#define QUERY_TABLE_ROWS 100u
// the width of the window Schema, percent of the screen
#define QUERY_SCHEMA_SIZE 30

#define ANSI_BOLD         "\033[1m"
#define ANSI_RED          "\033[31m"
#define ANSI_GREEN        "\033[32m"
#define ANSI_CYAN         "\033[36m"
#define ANSI_RESET        "\033[0m"

/*** enums ***************************************************************************************/

enum
{
    QUERY_CMD_NONE = -1,
    QUERY_CMD_HELP,
    QUERY_CMD_RUN,
    QUERY_CMD_RUN_ALL,
    QUERY_CMD_STOP,
    QUERY_CMD_HISTORY_INSERT,
    QUERY_CMD_HISTORY_SHOW,
    QUERY_CMD_ROLLBACK,
    QUERY_CMD_SCHEMA,
    QUERY_CMD_SCHEMA_ROWS,
    QUERY_CMD_SCHEMA_INSERT,
    QUERY_CMD_SCHEMA_REFRESH,
    QUERY_CMD_COUNT
};

/* What is done in the window Schema, at a line of it */
typedef enum
{
    QUERY_SCHEMA_SHOW,     // made, its text read the first time
    QUERY_SCHEMA_REFRESH,  // read again
    QUERY_SCHEMA_ENTER,    // a schema folded or unfolded, the columns of a relation
    QUERY_SCHEMA_ROWS,     // the rows of the relation in Table
    QUERY_SCHEMA_INSERT    // the name put in the file
} query_schema_action_t;

enum
{
    QUERY_TAB_OUTPUT,
    QUERY_TAB_TABLE,
    QUERY_TAB_HISTORY,
    QUERY_TAB_COUNT
};

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct query_t query_t;

/* What runs the queries of a kind of database */
typedef struct
{
    const char *id;         // the value of the startup option "query"
    const char *extension;  // of its files of queries, ".js": that of the versions of one
    gboolean sections;      // F5 without a selection: the part between lines "// ---", not the file
    void *(*open) (query_t *q);
    void (*close) (void *data);  // a run going on is ended at once
    // the lines of the head of Output at the start; @edit the file of queries, NULL for none
    void (*startup) (void *data, void *edit, GString *text);
    // the context of a part of @text run alone from its 0-based @line, kept in History with
    // the query: the string of a use () above it for mongo; NULL for none
    char *(*context) (const char *text, long line);
    gboolean (*running) (void *data);
    // run q->text; the backend fills Output and Table, marks an error and adds the run to History
    void (*start) (void *data);
    void (*stop) (void *data);
    const char *rollback;  // the query QueryRollback runs, "ROLLBACK"; NULL for none
    // the window Schema: @action at its 0-based @line, -1 for none; NULL for a backend with none
    void (*schema) (void *data, query_schema_action_t action, long line);
} query_backend_t;

/* The plugin */
struct query_t
{
    mc_editor_host_t *host;
    gboolean on;  // coole -Q
    const query_backend_t *backend;
    void *backend_data;
    char *endpoint;  // what is connected to, for the head of Output: "host:port/db"

    // the run going on, or the last one
    gboolean stopped;
    char *text;
    char *file;         // the file of the query, NULL for a file with no name
    long first;         // the first line of the query in it, from 1
    long first_column;  // the characters before the query on that line: a selection from the
                        // middle of a line
    char *use;  // the context of a part run alone, of query_backend_t.context; NULL for none
    gint64 started;

    gint64 tabs[QUERY_TAB_COUNT];  // the windows of the viewer, 0 when none
    gint64 schema;                 // the window Schema at the right, 0 when none
    long commands[QUERY_CMD_COUNT];
    int mark;
    GPtrArray *history;  // query_entry_t, the last one last
    char *history_path;
    char *history_file;  // the file whose history it is
    GPtrArray *rows;     // the version of each line of the tab History, a name
    char *version;       // the version of the file of the run of the moment, NULL for none
    guint viewer_signal;
};

/*** global variables defined in .c file *********************************************************/

extern const query_backend_t query_mongo_backend;
extern const query_backend_t query_postgres_backend;

/*** declarations of public functions ************************************************************/

/* The text of a tab, the tab made again when the user has closed it; the one seen when @front */
void query_tab_set (query_t *q, int tab, const char *text, gboolean front);
/* The title of a tab, its own when @title is NULL */
void query_tab_title (query_t *q, int tab, const char *title);
void query_table_say (query_t *q, const char *text);
void query_redraw (query_t *q);

/* The text of the window Schema, made again when the user has closed it, the cursor on @line */
void query_schema_set (query_t *q, const char *text, long line);
/* The line of the cursor of the window Schema, from 0; -1 when there is none */
long query_schema_line (query_t *q);
/* Run @text, a query of no file, as QueryRollback does: an error of it is marked nowhere */
void query_run_alone (query_t *q, const char *text);
/* The start of the head of the output of a run, bold: its lines, the time it took, when it ended
   (@when, "14:02:11"), what it ran in; what a backend adds goes after it, then ANSI_RESET */
void query_output_head (query_t *q, GString *text, double seconds, const char *when);
/* The error at the 1-based @line of the query, @column of it, marked in the file and gone to */
void query_mark_error (query_t *q, long line, long column);
/* The run ended at @when, after @seconds, with @status: 0 is done, -1 stopped; @summary how it
   ended, shown in place of the exit code, "SELECT 12", NULL for none */
void query_history_add (query_t *q, const char *when, double seconds, int status,
                        const char *summary);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_INT_H */
