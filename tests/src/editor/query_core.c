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
check_put (const char *text, long line, const char *query, const char *want, long want_at)
{
    gsize offset = 0;
    long at = -1;
    char *put = query_put_section (text, line, query, &offset, &at);
    GString *s = g_string_new (text);

    g_string_insert (s, (gssize) offset, put);
    g_assert_cmpstr (s->str, ==, want);
    g_assert_cmpint (at, ==, want_at);
    g_string_free (s, TRUE);
    g_free (put);
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

    // after the last line of the section, a section of its own
    check_put (script, 0, "db.q.find()\n", "const a = 1;\n\nprint(a)\n\n// ---\ndb.q.find()\n", 5);
    check_put (text, 4, "db.q.find()",
               "db.a.find()\n\n// --- second\nconst x = 1;\n\nprint(x)\n\n// ---\ndb.q.find()\n"
               "  //---\ndb.c.count()\n",
               8);
    // a section of nothing, an empty file: the query is it
    check_put ("", 0, "db.q.find()", "db.q.find()\n", 0);
    check_put ("// ---\n", 0, "db.q.find()", "// ---\ndb.q.find()\n", 1);
    check_put ("x\n// ---", 1, "q", "x\n// ---\nq\n", 2);
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
    g_test_add_func ("/query/lines", test_lines);
    g_test_add_func ("/query/sections", test_sections);
    g_test_add_func ("/query/mongo", test_mongo);
    g_test_add_func ("/query/table", test_table);
    return g_test_run ();
}
