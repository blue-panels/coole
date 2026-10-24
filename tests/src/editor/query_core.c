/*
   src/plugins/query - tests for the core of the query plugin

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

#include <config.h>

/* -Dassert=false sets G_DISABLE_ASSERT in config.h: g_test_init () would then end the test and
   g_assert () check nothing, and the checks are the point of a test */
#undef G_DISABLE_ASSERT

#include <string.h>

#include <glib.h>

#include "src/plugins/query/query-core.h"

static void
check_for_table (const char *text, const char *want)
{
    char *s = query_mongo_for_table (text);

    g_assert_cmpstr (s, ==, want);
    g_free (s);
}

static void
check_limit (const char *text, long limit, const char *want)
{
    gboolean done = FALSE;
    char *s = query_mongo_with_limit (text, limit, &done);

    g_assert_cmpstr (s, ==, want != NULL ? want : text);
    g_assert_true (done == (want != NULL));
    g_free (s);
}

static void
check_section (const char *text, long line, const char *want, long want_first)
{
    long first = -1;
    char *s = query_section (text, line, &first);

    g_assert_cmpstr (s, ==, want);
    if (want != NULL)
        g_assert_cmpint (first, ==, want_first);
    g_free (s);
}

static void
test_sections (void)
{
    const char *script = "const a = 1;\n\nprint(a)\n";
    const char *text = "db.a.find()\n"
                       "\n"
                       "// --- second\n"
                       "const x = 1;\n"
                       "\n"
                       "print(x)\n"
                       "  //---\n"
                       "db.c.count()\n";

    g_assert_true (query_line_separator ("// ---"));
    g_assert_true (query_line_separator ("  //------ users\n"));
    g_assert_false (query_line_separator ("// -- x"));
    g_assert_false (query_line_separator ("x // ---"));

    // no separator: the whole text is the query, empty lines in it or not
    check_section (script, 2, NULL, 0);
    check_section (text, 0, "db.a.find()\n\n", 0);
    check_section (text, 1, "db.a.find()\n\n", 0);
    // the separator heads the section after it
    check_section (text, 2, "const x = 1;\n\nprint(x)\n", 3);
    check_section (text, 4, "const x = 1;\n\nprint(x)\n", 3);
    check_section (text, 7, "db.c.count()\n", 7);
    check_section (text, 8, "db.c.count()\n", 7);
    check_section ("// ---\n", 0, "", 1);
}

static gboolean
taken_first_two (const char *name, void *data)
{
    (void) data;
    return strcmp (name, "20261024-173750.js") == 0 || strcmp (name, "20261024-173750-2.js") == 0;
}

static void
test_versions (void)
{
    GTimeZone *utc = g_time_zone_new_utc ();
    GDateTime *now = g_date_time_new (utc, 2026, 10, 24, 17, 37, 50);
    char *names_in[] = { (char *) "a", (char *) "b", (char *) "c", (char *) "d", NULL };
    char **over;
    char *s;

    // a file of the folder of queries by its name; another by its name and its path
    s = query_versions_dir ("/d/q/.history", "/d/q", "/d/q/cvm.js");
    g_assert_cmpstr (s, ==, "/d/q/.history/cvm.js");
    g_free (s);
    s = query_versions_dir ("/d/q/.history", "/d/q", "/home/u/q.js");
    g_assert_true (g_str_has_prefix (s, "/d/q/.history/q.js-"));
    g_assert_cmpuint (strlen (s), ==, strlen ("/d/q/.history/q.js-") + 8);
    g_free (s);

    s = query_version_name (now, ".js", NULL, NULL);
    g_assert_cmpstr (s, ==, "20261024-173750.js");
    g_free (s);
    s = query_version_name (now, ".sql", NULL, NULL);
    g_assert_cmpstr (s, ==, "20261024-173750.sql");
    g_free (s);
    // two in a second: numbered
    s = query_version_name (now, ".js", taken_first_two, NULL);
    g_assert_cmpstr (s, ==, "20261024-173750-3.js");
    g_free (s);

    // those of a second by their number, not by the letters of it
    g_assert_cmpint (query_version_cmp ("20261024-173750.js", "20261024-173750-2.js"), <, 0);
    g_assert_cmpint (query_version_cmp ("20261024-173750-2.js", "20261024-173750-10.js"), <, 0);
    g_assert_cmpint (query_version_cmp ("20261024-173750-10.js", "20261024-173751.js"), <, 0);
    g_assert_cmpint (query_version_cmp ("20261024-173750.js", "20261024-173750.js"), ==, 0);

    // the oldest beyond those kept
    over = query_versions_over (names_in, 2);
    g_assert_cmpuint (g_strv_length (over), ==, 2);
    g_assert_cmpstr (over[0], ==, "a");
    g_assert_cmpstr (over[1], ==, "b");
    g_strfreev (over);
    over = query_versions_over (names_in, 10);
    g_assert_cmpuint (g_strv_length (over), ==, 0);
    g_strfreev (over);

    s = query_version_label ("20261024-173750-2.js");
    g_assert_cmpstr (s, ==, "24.10 17:37:50");
    g_free (s);
    s = query_version_label ("odd.js");
    g_assert_cmpstr (s, ==, "odd.js");
    g_free (s);

    {
        int added = -1, removed = -1;

        query_line_changes ("a\nb\nc\n", "a\nc\nd\ne\n", &added, &removed);
        g_assert_cmpint (added, ==, 2);
        g_assert_cmpint (removed, ==, 1);
        // a line twice counts twice
        query_line_changes ("x\n", "x\nx\n", &added, &removed);
        g_assert_cmpint (added, ==, 1);
        g_assert_cmpint (removed, ==, 0);
        query_line_changes (NULL, "a\nb", &added, &removed);
        g_assert_cmpint (added, ==, 2);
        g_assert_cmpint (removed, ==, 0);
    }

    g_date_time_unref (now);
    g_time_zone_unref (utc);
}

static void
test_lines (void)
{
    const char *text = "db.a.find()\n"
                       "\n"
                       "db.b.find({x: 1})\n"
                       "  .sort({ts: -1})\n"
                       "   \n"
                       "db.c.count()";

    g_assert_cmpint (query_line_of (text, 0), ==, 0);
    g_assert_cmpint (query_line_of (text, 12), ==, 1);
    g_assert_cmpint (query_line_of (text, 13), ==, 2);
}

static void
test_mongo (void)
{
    long line = 0, column = -1;
    char *s;

    // a chain of a query: made an array
    check_for_table ("db.a.find()\n  .limit(2);  \n", "db.a.find()\n  .limit(2)\n.toArray()");
    check_for_table ("db.a.find({x: f(1)})", "db.a.find({x: f(1)})\n.toArray()");
    check_for_table ("db.a.aggregate([{$sample: {size: 3}}])",
                     "db.a.aggregate([{$sample: {size: 3}}])\n.toArray()");
    check_for_table ("db.a.find().sort ({ts: -1})", "db.a.find().sort ({ts: -1})\n.toArray()");
    // anything else as it is: an array already, a count, what prints
    check_for_table ("db.a.find().toArray()", "db.a.find().toArray()");
    check_for_table ("x.sort((a, b) => b - a).slice(0, 10)",
                     "x.sort((a, b) => b - a).slice(0, 10)");
    check_for_table ("db.a.countDocuments()", "db.a.countDocuments()");
    check_for_table ("rows.forEach(r => print(r))", "rows.forEach(r => print(r))");
    check_for_table ("print(1)\n", "print(1)\n");
    check_for_table ("", "");
    // a comment after the call, a ')' of a string, a ';' and a comment of lines after it
    check_for_table ("// @table\ndb.a.find({}) // recent\n",
                     "// @table\ndb.a.find({})\n.toArray()");
    check_for_table ("db.a.find({n: \")\"})", "db.a.find({n: \")\"})\n.toArray()");
    check_for_table ("db.a.find({n: '(\\'('}).limit(2); /* 2 ) */",
                     "db.a.find({n: '(\\'('}).limit(2)\n.toArray()");
    check_for_table ("db.a.find() // .limit(2)\n.toArray()",
                     "db.a.find() // .limit(2)\n.toArray()");
    check_for_table ("db.a.count() // find()", "db.a.count() // find()");

    g_assert_true (query_wants_table ("// @table\ndb.a.find()"));
    g_assert_true (query_wants_table ("db.a.find()\n  //@table"));
    g_assert_false (query_wants_table ("db.a.find() // @tables"));
    g_assert_false (query_wants_table ("db.a.find()"));
    g_assert_false (query_wants_table ("print(\"@table\")"));

    g_assert_cmpint (query_wants_limit ("// @limit 20\ndb.a.find()"), ==, 20);
    g_assert_cmpint (query_wants_limit ("//@limit\ndb.a.find()"), ==, QUERY_LIMIT_DEFAULT);
    g_assert_cmpint (query_wants_limit ("// @limit 0"), ==, QUERY_LIMIT_DEFAULT);
    g_assert_cmpint (query_wants_limit ("// @limits 5"), ==, 0);
    g_assert_cmpint (query_wants_limit ("db.a.find()"), ==, 0);

    // a chain of find (): .limit () after it, unless it has one
    check_limit ("db.a.find({}) // recent", 100, "db.a.find({})\n.limit(100)");
    check_limit ("db.getCollection(\"a\").find().sort({ts: -1});", 5,
                 "db.getCollection(\"a\").find().sort({ts: -1})\n.limit(5)");
    check_limit ("db.a.find().limit(3)", 100, NULL);
    check_limit ("db.a.find().limit(3).sort({x: 1})", 100, NULL);
    check_limit ("db.a.find().toArray()", 100, NULL);
    check_limit ("db.a.countDocuments({})", 100, NULL);
    // the find () of an array is none of a collection
    check_limit ("db.a.find().toArray().find(d => d.x)", 100, NULL);
    check_limit ("db.getCollection(\"a\").find()", 5, "db.getCollection(\"a\").find()\n.limit(5)");
    check_limit ("print(1)", 100, NULL);
    // aggregate ([...]): a stage $limit at the end of its pipeline, on the same line
    check_limit ("db.a.aggregate([\n  {$match: {x: 1}}\n])", 7,
                 "db.a.aggregate([\n  {$match: {x: 1}}\n, { $limit: 7 }])");
    // a comment after the last stage does not take the stage in
    check_limit ("db.a.aggregate([\n {$match: {}} // recent\n])", 7,
                 "db.a.aggregate([\n {$match: {}} // recent\n, { $limit: 7 }])");
    check_limit ("db.a.aggregate([{$match: {}} /* x, */])", 7,
                 "db.a.aggregate([{$match: {}} /* x, */, { $limit: 7 }])");
    // a limit of its own, or a stage no other may follow: as it is
    check_limit ("db.a.aggregate([{$match: {}}, {$limit: 500}])", 7, NULL);
    check_limit ("db.a.aggregate([{$match: {}}, {$out: \"x\"}])", 7, NULL);
    check_limit ("db.a.aggregate([{$merge: {into: \"x\"}}])", 7, NULL);
    check_limit ("db.a.aggregate([{$match: {}},])", 7,
                 "db.a.aggregate([{$match: {}},{ $limit: 7 }])");
    check_limit ("db.a.aggregate([])", 7, "db.a.aggregate([{ $limit: 7 }])");
    check_limit ("db.a.aggregate(pipeline)", 7, NULL);
    // use () as getSiblingDB (), not in strings, comments, methods or other names
    s = query_mongo_use ("use(\"a\");\nuse (\"b\")\nx.use(1); reuse(1); user(1)\n"
                         "// use(\"c\")\nprint(\"use(d)\") /* use( */");
    g_assert_cmpstr (
        s, ==,
        "db = db.getSiblingDB(\"a\");\ndb = db.getSiblingDB (\"b\")\nx.use(1); reuse(1); "
        "user(1)\n// use(\"c\")\nprint(\"use(d)\") /* use( */");
    g_free (s);

    // the use () above a part run alone: the last one, as its string is written
    {
        const char *file = "use(\"a\");\n// ---\ndb.x.find()\n/* use(\"c\")\n */\n"
                           "use ( 'b' )\nprint(\"use(d)\")\n// ---\ndb.y.find()\n";

        g_assert_null (query_use_before (file, 0));
        s = query_use_before (file, 1);
        g_assert_cmpstr (s, ==, "\"a\"");
        g_free (s);
        // not in a comment or a string, the lines counted through them
        s = query_use_before (file, 5);
        g_assert_cmpstr (s, ==, "\"a\"");
        g_free (s);
        s = query_use_before (file, 8);
        g_assert_cmpstr (s, ==, "'b'");
        g_free (s);
        g_assert_null (query_use_before ("use(name)\nx\n", 1));
    }

    g_assert_true (query_has_use ("// x\nuse(\"a\");\ndb.c.find()"));
    g_assert_false (query_has_use ("db.c.find() // use(\"a\")\nx.use(1); print(\"use(b)\")"));
    s = query_use_line ("cvmenginev2");
    g_assert_cmpstr (s, ==, "use(\"cvmenginev2\");\n\n");
    g_free (s);
    s = query_use_line ("a\"b\\c");
    g_assert_cmpstr (s, ==, "use(\"a\\\"b\\\\c\");\n\n");
    g_free (s);

    // what for_table then makes of it
    check_for_table ("db.a.find()\n.limit(100)", "db.a.find()\n.limit(100)\n.toArray()");

    s = query_mongo_clean ("DeprecationWarning: DBQuery.shellBatchSize is deprecated, please use "
                           "config.set(\"displayBatchSize\") instead\n23\n");
    g_assert_cmpstr (s, ==, "23\n");
    g_free (s);
    s = query_mongo_clean ("23\n");
    g_assert_cmpstr (s, ==, "23\n");
    g_free (s);

    g_assert_true (query_mongo_error_position ("SyntaxError: Unexpected token (3:0)\n\n"
                                               "  1 | 1\n> 3 | )\n",
                                               &line, &column));
    g_assert_cmpint (line, ==, 3);
    g_assert_cmpint (column, ==, 0);
    g_assert_true (
        query_mongo_error_position ("x\nSyntaxError: Missing semicolon. (12:7)", &line, &column));
    g_assert_cmpint (line, ==, 12);
    g_assert_cmpint (column, ==, 7);
    g_assert_false (
        query_mongo_error_position ("MongoServerError: unknown operator: $bad\n", &line, &column));
    g_assert_false (
        query_mongo_error_position ("SyntaxError: Unexpected token ')'\n", &line, &column));

    g_assert_true (query_mongo_is_cursor (
        "{\n  \"message\": \"Converting circular structure to EJSON:\\n    (root) -> _mongo\"\n}"));
    g_assert_false (query_mongo_is_cursor ("[]"));

    s = query_mongo_json_error (
        "{\"message\": \"boom\", \"stack\": \"at x\", \"name\": \"Error\"}");
    g_assert_cmpstr (s, ==, "Error: boom");
    g_free (s);
    g_assert_null (query_mongo_json_error ("{\"message\": \"a document\"}"));
    // as mongosh --json tells a SyntaxError, after what was printed
    s = query_mongo_json_error (
        query_mongo_json_tail ("x\n{\n  \"code\": \"BABEL_PARSE_ERROR\",\n  \"message\": "
                               "\"Unexpected token (3:0)\\n\\n> 3 | )\",\n  \"stack\": \"s\",\n"
                               "  \"name\": \"SyntaxError\"\n}\n"));
    g_assert_cmpstr (s, ==, "SyntaxError: Unexpected token (3:0)\n\n> 3 | )");
    g_assert_true (query_mongo_error_position (s, &line, &column));
    g_assert_cmpint (line, ==, 3);
    g_free (s);
    g_assert_null (query_mongo_json_error ("[1, 2]"));
    g_assert_null (query_mongo_json_error (NULL));

    // the database: the one named, else that of the path of the URI
    s = query_mongo_db ("mongodb://u:p@h:1/fromuri?x=1", NULL);
    g_assert_cmpstr (s, ==, "fromuri");
    g_free (s);
    s = query_mongo_db ("mongodb://u:p@h:1/fromuri", "named");
    g_assert_cmpstr (s, ==, "named");
    g_free (s);
    g_assert_null (query_mongo_db ("mongodb://u:p@h:1/?directConnection=true", NULL));
    g_assert_null (query_mongo_db ("mongodb://h:1", ""));
    g_assert_null (query_mongo_db (NULL, NULL));

    s = query_mongo_endpoint ("mongodb://noop:noop@127.0.0.1:27018/?directConnection=true",
                              "cvmenginev2");
    g_assert_cmpstr (s, ==, "127.0.0.1:27018/cvmenginev2");
    g_free (s);
    s = query_mongo_endpoint ("mongodb+srv://u:p%40ss@cluster.example.net/shop?retryWrites=true",
                              NULL);
    g_assert_cmpstr (s, ==, "cluster.example.net/shop");
    g_free (s);
    s = query_mongo_endpoint ("mongodb://h1:1,h2:2", "");
    g_assert_cmpstr (s, ==, "h1:1,h2:2");
    g_free (s);
}

static void
test_table (void)
{
    guint rows = 0;
    GError *error = NULL;
    char *s;

    s = query_table (
        "[\n"
        "  {\"_id\": {\"$oid\": \"6650a1000000000000000000\"}, \"status\": 1,\n"
        "   \"ts\": {\"$date\": \"2026-10-21T10:02:11Z\"},\n"
        "   \"user\": {\"a\": 1, \"b\": 2}, \"items\": [1, 2, 3]},\n"
        "  {\"status\": 2, \"_id\": {\"$oid\": \"6650a1000000000000000001\"},\n"
        "   \"name\": \"two\\nlines\", \"n\": {\"$numberLong\": \"9007199254740993\"}},\n"
        "  {\"_id\": 3, \"status\": null}\n"
        "]",
        0, &rows, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows, ==, 3);
    g_assert_cmpstr (s, ==,
                     "| _id                      | status | ts                   | user         "
                     "| items     | name       | n                |\n"
                     "|--------------------------|--------|----------------------|--------------"
                     "|-----------|------------|------------------|\n"
                     "| 6650a1000000000000000000 | 1      | 2026-10-21T10:02:11Z | {a: 1, b: 2} "
                     "| [1, 2, 3] |            |                  |\n"
                     "| 6650a1000000000000000001 | 2      |                      |              "
                     "|           | two\\nlines | 9007199254740993 |\n"
                     "| 3                        | null   |                      |              "
                     "|           |            |                  |\n");
    g_free (s);

    // not documents: a column of values
    s = query_table ("23", 0, &rows, &error);
    g_assert_cmpuint (rows, ==, 1);
    g_assert_cmpstr (s, ==, "| value |\n|-------|\n| 23    |\n");
    g_free (s);

    s = query_table ("[]", 0, &rows, &error);
    g_assert_cmpuint (rows, ==, 0);
    g_assert_cmpstr (s, ==, "");
    g_free (s);

    // a value longer than a column is cut
    s = query_table ("[{\"k\": \"0123456789012345678901234567890123456789XYZ\"}]", 0, &rows,
                     &error);
    g_assert_cmpstr (s, ==,
                     "| k                                        |\n"
                     "|------------------------------------------|\n"
                     "| 0123456789012345678901234567890123456... |\n");
    g_free (s);

    // a '|' of the data ends no cell
    s = query_table ("[{\"a|b\": \"x | y\"}]", 0, &rows, &error);
    g_assert_cmpstr (s, ==, "| a\\|b   |\n|--------|\n| x \\| y |\n");
    g_free (s);

    // what print () printed before the value
    s = query_table (
        query_mongo_json_tail ("collection: x\n[odd\n[\n  {\n    \"id\": \"a\"\n  }\n]\n"), 0,
        &rows, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows, ==, 1);
    g_assert_cmpstr (s, ==, "| id  |\n|-----|\n| a   |\n");
    g_free (s);
    g_assert_cmpstr (query_mongo_json_tail ("a\n23\n"), ==, "23\n");
    g_assert_null (query_mongo_json_tail ("collection: x\n"));

    g_assert_null (query_table ("DeprecationWarning: x", 0, &rows, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);

    // an object inside as mongosh shows it, a string of it in quotes: the keys of indexes; cut
    // as any value longer than 40 columns
    s = query_table (
        "[{\"v\": 2, \"key\": {\"ClientUuid\": \"hashed\"}},"
        " {\"key\": {\"ClientUuid\": 1, \"At\": {\"$date\": \"2026-01-01T00:00:00Z\"}}}]",
        0, &rows, &error);
    g_assert_no_error (error);
    g_assert_cmpstr (s, ==,
                     "| v   | key                                      |\n"
                     "|-----|------------------------------------------|\n"
                     "| 2   | {ClientUuid: \"hashed\"}                   |\n"
                     "|     | {ClientUuid: 1, At: 2026-01-01T00:00:... |\n");
    g_free (s);

    // the first rows of a big one, all of them counted
    s = query_table ("[{\"a\": 1}, {\"a\": 2}, {\"a\": 3}]", 2, &rows, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows, ==, 3);
    g_assert_cmpstr (s, ==, "| a   |\n|-----|\n| 1   |\n| 2   |\n");
    g_free (s);

    // the copies printjson () printed taken out of what is shown
    {
        GPtrArray *tables = g_ptr_array_new_with_free_func (g_free);
        char *rest = query_take_tables (
            "5\n[ 1, 2 ]\n" QUERY_TABLE_LINE "[1,2]\nx\n" QUERY_TABLE_LINE "[{\"a\":1}]", tables);

        g_assert_cmpstr (rest, ==, "5\n[ 1, 2 ]\nx\n");
        g_assert_cmpuint (tables->len, ==, 2);
        g_assert_cmpstr (g_ptr_array_index (tables, 0), ==, "[1,2]");
        g_assert_cmpstr (g_ptr_array_index (tables, 1), ==, "[{\"a\":1}]");
        g_free (rest);
        g_ptr_array_free (tables, TRUE);
    }
}

/* The table of the rows of PostgreSQL, and where an error is in characters */
static void
test_grid (void)
{
    GPtrArray *columns = g_ptr_array_new_with_free_func (g_free);
    GPtrArray *cells = g_ptr_array_new_with_free_func (g_free);
    const char *text = "-- \xd0\xba\xd0\xbe\xd0\xbc\n  SELECT x";
    char *s;

    g_ptr_array_add (columns, g_strdup ("id"));
    g_ptr_array_add (columns, g_strdup ("a|b"));
    g_ptr_array_add (cells, g_strdup ("1"));
    g_ptr_array_add (cells, query_cell_text ("x|y\nz"));
    g_ptr_array_add (cells, g_strdup ("22"));
    g_ptr_array_add (cells, g_strdup ("NULL"));
    s = query_grid (columns, cells);
    g_assert_cmpstr (s, ==,
                     "| id  | a\\|b    |\n|-----|---------|\n| 1   | x\\|y\\nz |\n"
                     "| 22  | NULL    |\n");
    g_free (s);

    // no rows: the names alone
    g_ptr_array_set_size (cells, 0);
    s = query_grid (columns, cells);
    g_assert_cmpstr (s, ==, "| id  | a\\|b |\n|-----|------|\n");
    g_free (s);
    g_ptr_array_free (cells, TRUE);
    g_ptr_array_free (columns, TRUE);

    // "-- " and three letters of two bytes each, a newline, two blanks: the 9th character is S
    g_assert_cmpuint (query_utf8_offset (text, strlen (text), 0), ==, 0);
    g_assert_cmpuint (query_utf8_offset (text, strlen (text), 9), ==, 12);
    g_assert_cmpuint (text[query_utf8_offset (text, strlen (text), 9)], ==, 'S');
    // past the end: the end
    g_assert_cmpuint (query_utf8_offset (text, strlen (text), 100), ==, strlen (text));
    // a selection: the characters of its own bytes alone
    g_assert_cmpuint (query_utf8_offset (text + 3, 4, 5), ==, 4);
}

/* The names of a relation as SQL has them: in quotes when quote_ident () would quote them */
static void
test_pg_ident (void)
{
    // the letters of "\xd0\xa1\xd1\x87" are not of ASCII: quoted, as quote_ident () does
    static const char *const cases[][2] = {
        { "users", "users" },     { "_a1", "_a1" },
        { "Users", "\"Users\"" }, { "1a", "\"1a\"" },
        { "a$b", "\"a$b\"" },     { "user", "\"user\"" },
        { "order", "\"order\"" }, { "all", "\"all\"" },
        { "with", "\"with\"" },   { "withx", "withx" },
        { "a\"b", "\"a\"\"b\"" }, { "\xd0\xa1\xd1\x87", "\"\xd0\xa1\xd1\x87\"" },
        { "", "\"\"" },
    };
    size_t i;
    char *s;

    for (i = 0; i < G_N_ELEMENTS (cases); i++)
    {
        s = query_pg_ident (cases[i][0]);
        g_assert_cmpstr (s, ==, cases[i][1]);
        g_free (s);
    }
    s = query_pg_relation_query ("sales", "Orders");
    g_assert_cmpstr (s, ==, "SELECT * FROM sales.\"Orders\" LIMIT 100;");
    g_free (s);
    s = query_pg_relation_query (NULL, "orders");
    g_assert_cmpstr (s, ==, "SELECT * FROM orders LIMIT 100;");
    g_free (s);
    s = query_pg_relation_query ("", "orders");
    g_assert_cmpstr (s, ==, "SELECT * FROM orders LIMIT 100;");
    g_free (s);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/query/lines", test_lines);
    g_test_add_func ("/query/versions", test_versions);
    g_test_add_func ("/query/sections", test_sections);
    g_test_add_func ("/query/mongo", test_mongo);
    g_test_add_func ("/query/table", test_table);
    g_test_add_func ("/query/grid", test_grid);
    g_test_add_func ("/query/pg-ident", test_pg_ident);
    return g_test_run ();
}
