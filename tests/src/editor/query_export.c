/*
   src/plugins/query - tests for the export of the rows of a query

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of bluerig.

   bluerig is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   bluerig is distributed in the hope that it will be useful,
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

#include "src/plugins/query/query-export.h"

/* The rows of the test: id integer, name text, ok boolean, doc jsonb, price numeric */
static const query_value_t kinds[] = {
    QUERY_VALUE_NUMBER, QUERY_VALUE_TEXT, QUERY_VALUE_BOOL, QUERY_VALUE_JSON, QUERY_VALUE_NUMBER,
};

static const char *const rows[][5] = {
    { "1", "plain", "t", "{\"a\": 1}", "1.50" },
    { "2", "a, \"quoted\"\nline", "f", NULL, "NaN" },
    { "3", NULL, NULL, "[1, 2]", "-0.5e3" },
};

/* What the rows of the test are made in @options */
static char *
export_rows (query_export_options_t *options, guint count)
{
    query_export_t *x = query_export_new (options);
    GPtrArray *names = g_ptr_array_new ();
    char *text;
    guint r, c;

    g_ptr_array_add (names, (gpointer) "id");
    g_ptr_array_add (names, (gpointer) "name");
    g_ptr_array_add (names, (gpointer) "ok");
    g_ptr_array_add (names, (gpointer) "doc");
    g_ptr_array_add (names, (gpointer) "Price");
    query_export_columns (x, names);
    g_ptr_array_free (names, TRUE);
    for (r = 0; r < count; r++)
    {
        gsize lens[5];

        for (c = 0; c < 5; c++)
            lens[c] = rows[r][c] != NULL ? strlen (rows[r][c]) : 0;
        query_export_row (x, rows[r], lens, kinds);
    }
    query_export_end (x);
    g_assert_cmpint (query_export_rows (x), ==, count);
    text = g_strdup (query_export_text (x)->str);
    query_export_free (x);
    return text;
}

/* What the documents @docs are made in @format; @skipped the keys left out */
static char *
export_documents (query_export_format_t format, const char *const *docs, char **skipped)
{
    query_export_options_t options = { .format = format, .header = TRUE };
    query_export_t *x = query_export_new (&options);
    char *text;

    for (; *docs != NULL; docs++)
    {
        GError *error = NULL;

        g_assert_true (query_export_document (x, *docs, &error));
        g_assert_no_error (error);
    }
    query_export_end (x);
    text = g_strdup (query_export_text (x)->str);
    if (skipped != NULL)
        *skipped = query_export_skipped (x);
    query_export_free (x);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_csv (void)
{
    query_export_options_t options = { .format = QUERY_EXPORT_CSV, .header = TRUE };
    char *s;

    s = export_rows (&options, 3);
    g_assert_cmpstr (s, ==,
                     "id,name,ok,doc,Price\n"
                     "1,plain,true,\"{\"\"a\"\": 1}\",1.50\n"
                     "2,\"a, \"\"quoted\"\"\nline\",false,,NaN\n"
                     "3,,,\"[1, 2]\",-0.5e3\n");
    g_free (s);

    // ';', no header, NULL as a word, every value in quotes
    options.delimiter = ';';
    options.header = FALSE;
    options.null_word = TRUE;
    options.quote_all = TRUE;
    s = export_rows (&options, 2);
    g_assert_cmpstr (s, ==,
                     "\"1\";\"plain\";\"true\";\"{\"\"a\"\": 1}\";\"1.50\"\n"
                     "\"2\";\"a, \"\"quoted\"\"\nline\";\"false\";NULL;\"NaN\"\n");
    g_free (s);

    // a ',' is no delimiter of ';'
    options.quote_all = FALSE;
    options.header = TRUE;
    s = export_rows (&options, 0);
    g_assert_cmpstr (s, ==, "id;name;ok;doc;Price\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_json (void)
{
    query_export_options_t options = { .format = QUERY_EXPORT_JSON };
    char *s;

    s = export_rows (&options, 3);
    g_assert_cmpstr (
        s, ==,
        "[\n"
        "{\"id\":1,\"name\":\"plain\",\"ok\":true,\"doc\":{\"a\": 1},\"Price\":1.50},\n"
        "{\"id\":2,\"name\":\"a, \\\"quoted\\\"\\nline\",\"ok\":false,\"doc\":null,"
        "\"Price\":\"NaN\"},\n"
        "{\"id\":3,\"name\":null,\"ok\":null,\"doc\":[1, 2],\"Price\":-0.5e3}\n"
        "]\n");
    g_free (s);

    s = export_rows (&options, 0);
    g_assert_cmpstr (s, ==, "[]\n");
    g_free (s);

    // a key a line
    options.indent = TRUE;
    s = export_rows (&options, 1);
    g_assert_cmpstr (s, ==,
                     "[\n  {\n    \"id\": 1,\n    \"name\": \"plain\",\n    \"ok\": true,\n"
                     "    \"doc\": {\"a\": 1},\n    \"Price\": 1.50\n  }\n]\n");
    g_free (s);
    options.indent = FALSE;

    options.format = QUERY_EXPORT_NDJSON;
    s = export_rows (&options, 1);
    g_assert_cmpstr (
        s, ==, "{\"id\":1,\"name\":\"plain\",\"ok\":true,\"doc\":{\"a\": 1},\"Price\":1.50}\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_sql (void)
{
    query_export_options_t options = { .format = QUERY_EXPORT_SQL, .table = (char *) "s.t" };
    char *s;

    s = export_rows (&options, 2);
    g_assert_cmpstr (s, ==,
                     "INSERT INTO s.t (id, name, ok, doc, \"Price\") VALUES (1, 'plain', TRUE, "
                     "'{\"a\": 1}', 1.50);\n"
                     "INSERT INTO s.t (id, name, ok, doc, \"Price\") VALUES (2, 'a, \"quoted\"\n"
                     "line', FALSE, NULL, 'NaN');\n");
    g_free (s);

    // two rows a statement, the last one not full; a quote doubled; no table is "export"
    options.per_insert = 2;
    options.table = NULL;
    s = export_rows (&options, 3);
    g_assert_cmpstr (s, ==,
                     "INSERT INTO export (id, name, ok, doc, \"Price\") VALUES\n"
                     "  (1, 'plain', TRUE, '{\"a\": 1}', 1.50),\n"
                     "  (2, 'a, \"quoted\"\nline', FALSE, NULL, 'NaN');\n"
                     "INSERT INTO export (id, name, ok, doc, \"Price\") VALUES\n"
                     "  (3, NULL, NULL, '[1, 2]', -0.5e3);\n");
    g_free (s);

    s = export_rows (&options, 0);
    g_assert_cmpstr (s, ==, "");
    g_free (s);

    // the table made first, of the types given, text for those not given
    options.create_table = TRUE;
    options.table = (char *) "t";
    {
        query_export_t *x = query_export_new (&options);
        GPtrArray *names = g_ptr_array_new (), *types = g_ptr_array_new ();
        const char *values[] = { "1", NULL };
        const gsize lens[] = { 1, 0 };
        const query_value_t k[] = { QUERY_VALUE_NUMBER, QUERY_VALUE_TEXT };

        g_ptr_array_add (names, (gpointer) "id");
        g_ptr_array_add (names, (gpointer) "Name");
        g_ptr_array_add (types, (gpointer) "integer");
        query_export_types (x, types);
        query_export_columns (x, names);
        query_export_row (x, values, lens, k);
        query_export_end (x);
        g_assert_cmpstr (query_export_text (x)->str, ==,
                         "CREATE TABLE t (\n  id integer,\n  \"Name\" text\n);\n\n"
                         "INSERT INTO t (id, \"Name\") VALUES\n  (1, NULL);\n");
        query_export_free (x);
        g_ptr_array_free (names, TRUE);
        g_ptr_array_free (types, TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_names (void)
{
    static const char *const tables[][2] = {
        { "t", "t" },
        { "sales.orders", "sales.orders" },
        { "\"Sales\".\"Or\"\"ders\"", "\"Sales\".\"Or\"\"ders\"" },
        { "my-table", "\"my-table\"" },
        { "Orders 2026", "\"Orders 2026\"" },
        { "a.", "\"a.\"" },
    };
    guint i;

    // the table of INSERT as SQL takes it
    for (i = 0; i < G_N_ELEMENTS (tables); i++)
    {
        query_export_options_t options = { .format = QUERY_EXPORT_SQL,
                                           .table = (char *) tables[i][0] };
        query_export_t *x = query_export_new (&options);
        GPtrArray *names = g_ptr_array_new ();
        const char *values[] = { "1" };
        const gsize lens[] = { 1 };
        const query_value_t k[] = { QUERY_VALUE_NUMBER };
        char *want = g_strdup_printf ("INSERT INTO %s (a) VALUES (1);\n", tables[i][1]);

        g_ptr_array_add (names, (gpointer) "a");
        query_export_columns (x, names);
        query_export_row (x, values, lens, k);
        query_export_end (x);
        g_assert_cmpstr (query_export_text (x)->str, ==, want);
        g_free (want);
        g_ptr_array_free (names, TRUE);
        query_export_free (x);
    }

    // a name once: "id" of two tables of a join
    {
        query_export_options_t options = { .format = QUERY_EXPORT_CSV, .header = TRUE };
        query_export_t *x = query_export_new (&options);
        GPtrArray *names = g_ptr_array_new ();

        g_ptr_array_add (names, (gpointer) "id");
        g_ptr_array_add (names, (gpointer) "id");
        g_ptr_array_add (names, (gpointer) "id_2");
        g_ptr_array_add (names, (gpointer) "?column?");
        g_ptr_array_add (names, (gpointer) "?column?");
        query_export_columns (x, names);
        g_assert_cmpstr (query_export_text (x)->str, ==, "id,id_2,id_2_2,?column?,?column?_2\n");
        g_ptr_array_free (names, TRUE);
        query_export_free (x);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_markdown (void)
{
    query_export_options_t options = { .format = QUERY_EXPORT_MARKDOWN };
    char *s;

    // NULL as a word of its own option, not that of CSV
    options.null_word = TRUE;
    options.null_markdown = TRUE;
    s = export_rows (&options, 3);
    g_assert_nonnull (strstr (s, "| 3 | NULL | NULL | [1, 2] | -0.5e3 |\n"));
    g_free (s);
    options.null_word = FALSE;
    options.null_markdown = FALSE;

    s = export_rows (&options, 3);
    g_assert_cmpstr (s, ==,
                     "| id | name | ok | doc | Price |\n"
                     "|---|---|---|---|---|\n"
                     "| 1 | plain | true | {\"a\": 1} | 1.50 |\n"
                     "| 2 | a, \"quoted\"<br>line | false |  | NaN |\n"
                     "| 3 |  |  | [1, 2] | -0.5e3 |\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_documents (void)
{
    const char *const docs[] = {
        "{\"_id\": {\"$oid\": \"6650a1b2c3d4e5f601234567\"}, \"n\": 1, \"tags\": [\"a\"]}",
        "{\"n\": 2.5, \"_id\": {\"$oid\": \"6650a1b2c3d4e5f601234568\"}, \"at\": {\"$date\": "
        "\"2026-11-08T10:00:00Z\"}, \"big\": {\"$numberLong\": \"9007199254740993\"}}",
        "{\"_id\": 3, \"sub\": {\"x\": \"y|z\"}, \"ok\": true, \"none\": null}",
        NULL,
    };
    char *s, *skipped = NULL;

    // the keys of all of them the columns, _id first; an ObjectId by its hex, a date by its ISO
    s = export_documents (QUERY_EXPORT_CSV, docs, &skipped);
    g_assert_cmpstr (s, ==,
                     "_id,n,tags,at,big,sub,ok,none\n"
                     "6650a1b2c3d4e5f601234567,1,\"[\"\"a\"\"]\",,,,,\n"
                     "6650a1b2c3d4e5f601234568,2.5,,2026-11-08T10:00:00Z,9007199254740993,,,\n"
                     "3,,,,,\"{\"\"x\"\":\"\"y|z\"\"}\",true,\n");
    g_assert_null (skipped);
    g_free (s);

    // JSON as mongosh wrote it
    s = export_documents (QUERY_EXPORT_NDJSON, docs, NULL);
    g_assert_cmpstr (s, ==,
                     "{\"_id\": {\"$oid\": \"6650a1b2c3d4e5f601234567\"}, \"n\": 1, \"tags\": "
                     "[\"a\"]}\n"
                     "{\"n\": 2.5, \"_id\": {\"$oid\": \"6650a1b2c3d4e5f601234568\"}, \"at\": "
                     "{\"$date\": \"2026-11-08T10:00:00Z\"}, \"big\": {\"$numberLong\": "
                     "\"9007199254740993\"}}\n"
                     "{\"_id\": 3, \"sub\": {\"x\": \"y|z\"}, \"ok\": true, \"none\": null}\n");
    g_free (s);

    s = export_documents (QUERY_EXPORT_JSON, docs + 2, NULL);
    g_assert_cmpstr (
        s, ==, "[\n{\"_id\": 3, \"sub\": {\"x\": \"y|z\"}, \"ok\": true, \"none\": null}\n]\n");
    g_free (s);
    {
        // indented: a key a line, inside the array
        query_export_options_t options = { .format = QUERY_EXPORT_JSON, .indent = TRUE };
        query_export_t *x = query_export_new (&options);

        g_assert_true (query_export_document (x, "{\"_id\": 3, \"sub\": {\"x\": 1}}", NULL));
        query_export_end (x);
        g_assert_cmpstr (
            query_export_text (x)->str, ==,
            "[\n  {\n    \"_id\" : 3,\n    \"sub\" : {\n      \"x\" : 1\n    }\n  }\n]\n");
        query_export_free (x);
    }

    s = export_documents (QUERY_EXPORT_MARKDOWN, docs + 2, NULL);
    g_assert_cmpstr (s, ==,
                     "| _id | sub | ok | none |\n"
                     "|---|---|---|---|\n"
                     "| 3 | {\"x\":\"y\\|z\"} | true |  |\n");
    g_free (s);

    // no documents: nothing, an empty array
    s = export_documents (QUERY_EXPORT_CSV, docs + 3, NULL);
    g_assert_cmpstr (s, ==, "");
    g_free (s);
    s = export_documents (QUERY_EXPORT_JSON, docs + 3, NULL);
    g_assert_cmpstr (s, ==, "[]\n");
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_documents_after_sample (void)
{
    query_export_options_t options = { .format = QUERY_EXPORT_CSV, .header = TRUE };
    query_export_t *x = query_export_new (&options);
    GError *error = NULL;
    char *skipped;
    guint i;

    // the columns are those of the first QUERY_EXPORT_SAMPLE documents: a key after them is left
    for (i = 0; i < QUERY_EXPORT_SAMPLE; i++)
        g_assert_true (query_export_document (x, "{\"a\": 1}", NULL));
    g_assert_true (query_export_document (x, "{\"a\": 2, \"late\": 3, \"also\": 4}", NULL));
    g_assert_false (query_export_document (x, "{\"a\": ", &error));
    g_assert_nonnull (error);
    g_error_free (error);
    query_export_end (x);
    g_assert_cmpint (query_export_rows (x), ==, QUERY_EXPORT_SAMPLE + 1);
    g_assert_true (g_str_has_prefix (query_export_text (x)->str, "a\n1\n"));
    g_assert_true (g_str_has_suffix (query_export_text (x)->str, "1\n2\n"));
    skipped = query_export_skipped (x);
    g_assert_cmpstr (skipped, ==, "also, late");
    g_free (skipped);
    query_export_free (x);
}

/* --------------------------------------------------------------------------------------------- */

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/query-export/csv", test_csv);
    g_test_add_func ("/query-export/json", test_json);
    g_test_add_func ("/query-export/sql", test_sql);
    g_test_add_func ("/query-export/names", test_names);
    g_test_add_func ("/query-export/markdown", test_markdown);
    g_test_add_func ("/query-export/documents", test_documents);
    g_test_add_func ("/query-export/documents-after-sample", test_documents_after_sample);
    return g_test_run ();
}
