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
    QUERY_CMD_SCHEMA_QUERY,
    QUERY_CMD_AI,
    QUERY_CMD_COUNT
};

/* What is done in the window Schema, on the node of its cursor */
typedef enum
{
    QUERY_SCHEMA_SHOW,     // made, its tree read the first time
    QUERY_SCHEMA_TOGGLE,   // QuerySchema: made and given the focus, given it, or hidden
    QUERY_SCHEMA_REFRESH,  // read again
    QUERY_SCHEMA_ROWS,     // the rows of the relation in Table
    QUERY_SCHEMA_INSERT,   // the name put in the file
    QUERY_SCHEMA_QUERY     // the query of the rows of the relation, of its columns, in the file
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
typedef struct query_ai_t query_ai_t;
typedef struct query_proc_t query_proc_t;

/* A program of query_proc_start () has ended, with what it printed, valid UTF-8, @cut when it
   printed more than QUERY_OUTPUT_MAX on stdout, the rest not kept, and its @status of waitpid ();
   @out and @err are taken */
typedef void (*query_proc_done_fn) (char *out, char *err, int status, gboolean cut, void *data);

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
    /* the window Schema: @action on the node of its cursor, by a key when @key, then only when the
       window has the focus; TRUE when it is done.  NULL for a backend with none */
    gboolean (*schema) (void *data, query_schema_action_t action, gboolean key);
    // F6, a query written by AI: the language it is told to write, the start of a line comment
    const char *language;
    const char *comment;
    // what AI is told of the database, given to query_ai_context () when read; @use the context
    // of the line of the request, as query_backend_t.context; NULL for a backend with no AI
    void (*ai_context) (void *data, const char *use);
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
    query_ai_t *ai;                // F6
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

/* The bytes @from to @to of the text of the file window @edit made @text, one change Undo
   takes back, the cursor after it and the window in front; FALSE when an edit of another goes on */
gboolean query_edit_put (query_t *q, void *edit, gsize from, gsize to, const char *text);
/* @text, a query, put in the file window @edit: on the line of its cursor when it is empty, a
   paragraph of its own, else after the paragraph of the cursor; FALSE when it could not be */
gboolean query_put_query (query_t *q, void *edit, const char *text);
/* The window of the file @file, NULL when none has it */
void *query_file_window (query_t *q, const char *file);

/* F6 in @edit: the query of the request of the selection, else of the paragraph of the cursor,
   written by AI under it; asked to stop when AI is writing one */
void query_ai_ask (query_t *q, void *edit);
/* What the backend tells AI of the database, @context, read for query_ai_ask (); @error why it
   could not be, NULL for none */
void query_ai_context (query_t *q, const char *context, const char *error);
/* The line of the head of Output on the program of AI, F6, which the backend puts in its startup */
void query_ai_startup (query_t *q, GString *text);
void query_ai_free (query_t *q);

/* Run @argv in @dir with @envp, NULL for that of coole, @input on its stdin: @done is called
   when it ends, unless it is killed first; NULL with @error when it does not start */
query_proc_t *query_proc_start (const char *const *argv, char **envp, const char *dir,
                                const char *input, query_proc_done_fn done, void *data,
                                GError **error);
/* Ask it to end: @done is called once it has */
void query_proc_stop (query_proc_t *p);
/* End it at once, @done not called */
void query_proc_kill (query_proc_t *p);

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
