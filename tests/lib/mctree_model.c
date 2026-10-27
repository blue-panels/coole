/*
   Tests for mctree model.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/lib/mctree-model"

#include <string.h>

#include "tests/mctest.h"

#include "lib/mctree/mctree-model.h"

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_visible_rows_follow_expanded_state)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *field;
    GArray *rows;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "JSON", NULL);
    field = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "name", NULL);
    mctree_model_add_node (model, field, MCTREE_NODE_SCALAR, NULL, "value");

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 0);
    g_array_free (rows, TRUE);

    mctree_model_expand_to_depth (model, 2);
    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 2);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 0).node, field);
    ck_assert_int_eq (g_array_index (rows, mctree_visible_row_t, 1).depth, 1);

    g_array_free (rows, TRUE);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_scalar_preview_is_bounded)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *scalar;

    model = mctree_model_new (8);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "root", NULL);
    scalar = mctree_model_add_node (model, root, MCTREE_NODE_SCALAR, NULL, "abcdefghijklmnop");

    ck_assert_str_eq (scalar->value, "abcde...");
    ck_assert_uint_eq (scalar->original_value_len, 16);
    mctest_assert_true (scalar->value_truncated);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_transparent_object_containers_are_hidden)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *root_object;
    mctree_node_t *field;
    mctree_node_t *object;
    mctree_node_t *leaf;
    GArray *rows;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "YAML", NULL);
    root_object = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, NULL, NULL);
    field = mctree_model_add_node (model, root_object, MCTREE_NODE_FIELD, "customer", NULL);
    object = mctree_model_add_node (model, field, MCTREE_NODE_OBJECT, NULL, NULL);
    leaf = mctree_model_add_node (model, object, MCTREE_NODE_FIELD, "first_name", "Dorothy");

    root->expanded = TRUE;
    field->expanded = TRUE;

    ck_assert_uint_eq (mctree_node_child_count (field), 1);
    ck_assert_uint_eq (mctree_node_descendant_count (field), 1);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 2);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 0).node, field);
    ck_assert_int_eq (g_array_index (rows, mctree_visible_row_t, 0).depth, 0);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 1).node, leaf);
    ck_assert_int_eq (g_array_index (rows, mctree_visible_row_t, 1).depth, 1);

    g_array_free (rows, TRUE);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_child_and_descendant_counts)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *left;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "root", NULL);
    left = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "left", NULL);
    mctree_model_add_node (model, left, MCTREE_NODE_SCALAR, "leaf", "1");
    mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "right", NULL);

    ck_assert_uint_eq (mctree_node_child_count (root), 2);
    ck_assert_uint_eq (mctree_node_descendant_count (root), 3);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Match on the node key, the way a filter over the rows of the tree does. */
static gboolean
key_is (const mctree_node_t *node, void *user_data)
{
    return node->key != NULL && strcmp (node->key, (const char *) user_data) == 0;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_filter_keeps_matches_with_path_and_subtree)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *books;
    mctree_node_t *book;
    mctree_node_t *title;
    mctree_node_t *other;
    GArray *rows;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "XML", NULL);
    books = mctree_model_add_node (model, root, MCTREE_NODE_ELEMENT, "books", NULL);
    book = mctree_model_add_node (model, books, MCTREE_NODE_ELEMENT, "book", NULL);
    title = mctree_model_add_node (model, book, MCTREE_NODE_ELEMENT, "title", "Dune");
    other = mctree_model_add_node (model, books, MCTREE_NODE_ELEMENT, "magazine", NULL);

    ck_assert_uint_eq (mctree_model_filter_apply (model, key_is, (void *) "book"), 1);

    mctest_assert_true (book->filter_hit);
    mctest_assert_false (books->filter_hit);
    // the path to the match stays, and is opened
    mctest_assert_false (books->filter_hidden);
    mctest_assert_true (books->expanded);
    mctest_assert_true (root->expanded);
    // what is inside the match stays too, so the match can be browsed
    mctest_assert_false (title->filter_hidden);
    // the rest is gone
    mctest_assert_true (other->filter_hidden);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 2);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 0).node, books);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 1).node, book);
    g_array_free (rows, TRUE);

    mctree_model_filter_clear (model);
    mctest_assert_false (model->filter_on);
    mctest_assert_false (other->filter_hidden);
    mctest_assert_false (book->filter_hit);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_filter_without_matches_leaves_no_rows)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *field;
    GArray *rows;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "JSON", NULL);
    field = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "name", "value");
    mctree_model_expand_to_depth (model, 2);

    ck_assert_uint_eq (mctree_model_filter_apply (model, key_is, (void *) "absent"), 0);
    mctest_assert_true (field->filter_hidden);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 0);
    g_array_free (rows, TRUE);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

static int children_calls = 0;
static int data_freed = 0;

static void
test_children (mctree_model_t *model, mctree_node_t *node, void *user_data)
{
    (void) user_data;
    children_calls++;
    mctree_model_add_node (model, node, MCTREE_NODE_FIELD, "id", "integer");
    mctree_node_set_lazy (node, FALSE);
}

static void
test_data_free (gpointer data)
{
    (void) data;
    data_freed++;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lazy_node_reads_its_children_when_opened)
{
    mctree_model_t *model;
    mctree_node_t *root, *table;
    GArray *rows;

    children_calls = 0;
    model = mctree_model_new (0);
    model->children = test_children;
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "db", NULL);
    table = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "users", NULL);
    mctree_node_set_lazy (table, TRUE);
    root->expanded = TRUE;

    // a lazy node opens though it has no children
    ck_assert (mctree_node_expandable (table));
    ck_assert_uint_eq (mctree_node_child_count (table), 0);

    mctree_node_expand (model, table);
    ck_assert_int_eq (children_calls, 1);
    ck_assert (!table->lazy);
    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 2);
    g_array_free (rows, TRUE);

    // read once: opened again, it asks nothing
    table->expanded = FALSE;
    mctree_node_expand (model, table);
    ck_assert_int_eq (children_calls, 1);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_data_is_freed_with_its_node)
{
    mctree_model_t *model;
    mctree_node_t *root, *node;

    data_freed = 0;
    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "db", NULL);
    node = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "users", NULL);
    mctree_node_set_data (node, g_strdup ("x"), test_data_free);
    // set again: the data of before is freed
    mctree_node_set_data (node, g_strdup ("y"), test_data_free);
    ck_assert_int_eq (data_freed, 1);
    mctree_model_free (model);
    ck_assert_int_eq (data_freed, 2);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_kept_paths_keep_those_of_nodes_not_read)
{
    mctree_model_t *model;
    mctree_node_t *root, *src, *doc;
    GHashTable *before, *paths;

    before = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_hash_table_add (before, g_strdup ("src" MCTREE_PATH_SEP "editor"));
    g_hash_table_add (before, g_strdup ("doc"));

    // src is lazy and not read: its editor stays open; doc is read and closed: it is so no more
    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "", NULL);
    src = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "src", NULL);
    mctree_node_set_lazy (src, TRUE);
    doc = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "doc", NULL);
    (void) doc;

    paths = mctree_model_kept_paths (model, before);
    ck_assert_uint_eq (g_hash_table_size (paths), 1);
    ck_assert (g_hash_table_contains (paths, "src" MCTREE_PATH_SEP "editor"));

    g_hash_table_destroy (paths);
    g_hash_table_destroy (before);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_expanded_paths_are_restored_in_a_new_model)
{
    mctree_model_t *model, *again;
    mctree_node_t *root, *nsp, *table, *found;
    GHashTable *paths;
    char *path;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "db", NULL);
    nsp = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "public", NULL);
    table = mctree_model_add_node (model, nsp, MCTREE_NODE_OBJECT, "users", NULL);
    mctree_model_add_node (model, table, MCTREE_NODE_FIELD, "id", NULL);
    nsp->expanded = TRUE;
    table->expanded = TRUE;

    path = mctree_node_path (table);
    ck_assert_str_eq (path, "public" MCTREE_PATH_SEP "users");
    ck_assert_ptr_eq (mctree_model_find_path (model, path), table);
    g_free (path);
    paths = mctree_model_expanded_paths (model);
    ck_assert_uint_eq (g_hash_table_size (paths), 2);
    mctree_model_free (model);

    // read anew: what was open is open, the lazy node asking for its children
    children_calls = 0;
    again = mctree_model_new (0);
    again->children = test_children;
    root = mctree_model_add_node (again, NULL, MCTREE_NODE_ROOT, "db", NULL);
    nsp = mctree_model_add_node (again, root, MCTREE_NODE_OBJECT, "public", NULL);
    table = mctree_model_add_node (again, nsp, MCTREE_NODE_OBJECT, "users", NULL);
    mctree_node_set_lazy (table, TRUE);
    found = mctree_model_add_node (again, nsp, MCTREE_NODE_OBJECT, "orders", NULL);
    mctree_node_restore_expanded (again, again->root, paths);
    ck_assert (nsp->expanded);
    ck_assert (table->expanded);
    ck_assert (!found->expanded);
    ck_assert_int_eq (children_calls, 1);

    g_hash_table_destroy (paths);
    mctree_model_free (again);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_visible_rows_follow_expanded_state);
    tcase_add_test (tc_core, test_scalar_preview_is_bounded);
    tcase_add_test (tc_core, test_transparent_object_containers_are_hidden);
    tcase_add_test (tc_core, test_child_and_descendant_counts);
    tcase_add_test (tc_core, test_filter_keeps_matches_with_path_and_subtree);
    tcase_add_test (tc_core, test_filter_without_matches_leaves_no_rows);
    tcase_add_test (tc_core, test_lazy_node_reads_its_children_when_opened);
    tcase_add_test (tc_core, test_data_is_freed_with_its_node);
    tcase_add_test (tc_core, test_expanded_paths_are_restored_in_a_new_model);
    tcase_add_test (tc_core, test_kept_paths_keep_those_of_nodes_not_read);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
