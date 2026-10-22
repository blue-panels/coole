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
check_block (const char *text, long line, const char *want, long want_first)
{
    long first = -1;
    char *block = query_block (text, line, &first);

    g_assert_cmpstr (block, ==, want);
    if (want != NULL)
        g_assert_cmpint (first, ==, want_first);
    g_free (block);
}

static void
check_for_table (const char *text, const char *want)
{
    char *s = query_mongo_for_table (text);

    g_assert_cmpstr (s, ==, want);
    g_free (s);
}

static void
test_block (void)
{
    const char *text = "db.a.find()\n"
                       "\n"
                       "db.b.find({x: 1})\n"
                       "  .sort({ts: -1})\n"
                       "   \n"
                       "db.c.count()";

    check_block (text, 0, "db.a.find()", 0);
    check_block (text, 1, NULL, 0);
    check_block (text, 2, "db.b.find({x: 1})\n  .sort({ts: -1})", 2);
    check_block (text, 3, "db.b.find({x: 1})\n  .sort({ts: -1})", 2);
    // a line of blanks is empty
    check_block (text, 4, NULL, 0);
    check_block (text, 5, "db.c.count()", 5);
    check_block (text, 6, NULL, 0);
    check_block ("db.x.find()\n", 0, "db.x.find()", 0);
    check_block ("", 0, NULL, 0);

    g_assert_cmpint (query_line_of (text, 0), ==, 0);
    g_assert_cmpint (query_line_of (text, 12), ==, 1);
    g_assert_cmpint (query_line_of (text, 13), ==, 2);

    g_assert_false (query_line_blank (text, 0));
    g_assert_true (query_line_blank (text, 1));
    g_assert_false (query_line_blank (text, 3));
    g_assert_true (query_line_blank (text, 4));
    g_assert_false (query_line_blank (text, 5));
    // past the end
    g_assert_true (query_line_blank (text, 6));
    g_assert_true (query_line_blank ("", 0));
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
        &rows, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows, ==, 3);
    g_assert_cmpstr (s, ==,
                     "| _id                      | status | ts                   | user     | "
                     "items | name       "
                     "| n                |\n"
                     "|--------------------------|--------|----------------------|----------|------"
                     "-|------------"
                     "|------------------|\n"
                     "| 6650a1000000000000000000 | 1      | 2026-10-21T10:02:11Z | {2 keys} | [3]  "
                     " |            "
                     "|                  |\n"
                     "| 6650a1000000000000000001 | 2      |                      |          |      "
                     " | two\\nlines "
                     "| 9007199254740993 |\n"
                     "| 3                        | null   |                      |          |      "
                     " |            "
                     "|                  |\n");
    g_free (s);

    // not documents: a column of values
    s = query_table ("23", &rows, &error);
    g_assert_cmpuint (rows, ==, 1);
    g_assert_cmpstr (s, ==, "| value |\n|-------|\n| 23    |\n");
    g_free (s);

    s = query_table ("[]", &rows, &error);
    g_assert_cmpuint (rows, ==, 0);
    g_assert_cmpstr (s, ==, "");
    g_free (s);

    // a value longer than a column is cut
    s = query_table ("[{\"k\": \"0123456789012345678901234567890123456789XYZ\"}]", &rows, &error);
    g_assert_cmpstr (s, ==,
                     "| k                                        |\n"
                     "|------------------------------------------|\n"
                     "| 0123456789012345678901234567890123456... |\n");
    g_free (s);

    // a '|' of the data ends no cell
    s = query_table ("[{\"a|b\": \"x | y\"}]", &rows, &error);
    g_assert_cmpstr (s, ==, "| a\\|b   |\n|--------|\n| x \\| y |\n");
    g_free (s);

    // what print () printed before the value
    s = query_table (
        query_mongo_json_tail ("collection: x\n[odd\n[\n  {\n    \"id\": \"a\"\n  }\n]\n"), &rows,
        &error);
    g_assert_no_error (error);
    g_assert_cmpuint (rows, ==, 1);
    g_assert_cmpstr (s, ==, "| id  |\n|-----|\n| a   |\n");
    g_free (s);
    g_assert_cmpstr (query_mongo_json_tail ("a\n23\n"), ==, "23\n");
    g_assert_null (query_mongo_json_tail ("collection: x\n"));

    g_assert_null (query_table ("DeprecationWarning: x", &rows, &error));
    g_assert_nonnull (error);
    g_clear_error (&error);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/query/block", test_block);
    g_test_add_func ("/query/mongo", test_mongo);
    g_test_add_func ("/query/table", test_table);
    return g_test_run ();
}
