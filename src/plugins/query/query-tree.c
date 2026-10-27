/*
   The query plugin: the tree of the window Schema, whatever the database.

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

/** \file query-tree.c
 *  \brief Source: the tree of the window Schema, whatever the database
 *
 *  The nodes are those of an mctree, what a node is to Schema their data; a tree window of the
 *  editor shows them, its keys moving over them, opening and closing them.  The tree is read
 *  anew when the schemas change: what was open is opened again, and the cursor goes back to its
 *  node, by the names of their paths.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/widget.h"  // D_NORMAL

#include "query-tree.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

static void query_tree_drop_pending (query_tree_t *t);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
query_item_free (gpointer data)
{
    query_item_t *item = (query_item_t *) data;

    g_free (item->insert);
    g_free (item->rows);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

/* An object opened: its sections asked of the backend, once */
static void
query_tree_lazy (mctree_model_t *model, mctree_node_t *node, void *data)
{
    query_tree_t *t = (query_tree_t *) data;
    query_item_t *item = QUERY_ITEM (node);

    (void) model;
    if (item == NULL || item->read || item->asked || t->details == NULL)
        return;
    item->asked = TRUE;
    t->details (node, t->data);
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
query_tree_model_new (query_tree_t *t)
{
    mctree_model_t *model = mctree_model_new (G_MAXSIZE);

    (void) mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "", NULL);
    model->root->expanded = TRUE;
    model->children = query_tree_lazy;
    model->children_data = t;
    return model;
}

/* --------------------------------------------------------------------------------------------- */

/* The number a root, a group or a section shows: what it has; a root what its groups have */
static guint
query_node_count (const query_node_t *n)
{
    guint count = 0, i;

    if (QUERY_ITEM (n)->kind != QUERY_NODE_ROOT)
        return mctree_node_child_count (n);
    for (i = 0; n->children != NULL && i < n->children->len; i++)
        count += mctree_node_child_count (g_ptr_array_index (n->children, i));
    return count;
}

/* --------------------------------------------------------------------------------------------- */

/* The row of a node: its name; at the right the number a root, a group and a section have, its
   text */
static void
query_tree_row (edit_tree_t *tree, const mctree_node_t *node, GString *name, GString *info)
{
    const query_item_t *item = QUERY_ITEM (node);

    (void) tree;
    g_string_append (name, node->key != NULL ? node->key : "");
    if (node->value != NULL)
        g_string_append (info, node->value);
    if (item != NULL
        && (item->kind == QUERY_NODE_ROOT || item->kind == QUERY_NODE_GROUP
            || item->kind == QUERY_NODE_SECTION))
        g_string_append_printf (info, "%s(%u)", info->len > 0 ? " " : "", query_node_count (node));
}

/* --------------------------------------------------------------------------------------------- */

static void
query_tree_buttonbar (edit_tree_t *tree, WButtonBar *bb)
{
    const query_tree_t *t = (const query_tree_t *) tree->data;

    // the commands of the plugin, which act on the node of the cursor
    buttonbar_set_label_command (bb, 3, _ ("Query"), t->q->commands[QUERY_CMD_SCHEMA_QUERY], NULL);
    buttonbar_set_label_command (bb, 4, _ ("DDL"), t->q->commands[QUERY_CMD_SCHEMA_DDL], NULL);
    buttonbar_set_label_command (bb, 5, _ ("Rows"), t->q->commands[QUERY_CMD_SCHEMA_ROWS], NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_tree_closed (edit_tree_t *tree)
{
    ((query_tree_t *) tree->data)->window = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const edit_tree_ops_t query_tree_ops = {
    .row = query_tree_row,
    .buttonbar = query_tree_buttonbar,
    .closed = query_tree_closed,
};

/* --------------------------------------------------------------------------------------------- */

/* What F3 tells in Output: @what, and the query @text when it is not in the file */
static void
query_tree_say (query_tree_t *t, const char *what, const char *text)
{
    char *say =
        text != NULL ? g_strdup_printf ("%s\n\n%s\n", what, text) : g_strdup_printf ("%s\n", what);

    query_tab_set (t->q, QUERY_TAB_OUTPUT, say, TRUE);
    g_free (say);
    query_redraw (t->q);
}

/* --------------------------------------------------------------------------------------------- */

/* The query of @o of the field @field, of all of them when NULL, put in the file window @edit,
   NULL when it is gone */
static void
query_tree_put (query_tree_t *t, const query_node_t *o, const char *field, void *edit)
{
    const query_node_t *first = o->children != NULL && o->children->len > 0
            && g_ptr_array_index (o->children, 0) != QUERY_ITEM (o)->parts
        ? g_ptr_array_index (o->children, 0)
        : NULL;
    GPtrArray *fields = g_ptr_array_new ();
    char *text;
    guint i;

    for (i = 0; first != NULL && first->children != NULL && i < first->children->len; i++)
    {
        const query_node_t *f = g_ptr_array_index (first->children, i);
        const char *insert = QUERY_ITEM (f)->insert;

        if (insert != NULL && (field == NULL || strcmp (f->key, field) == 0))
            g_ptr_array_add (fields, (gpointer) insert);
    }
    text = t->query (o, fields);
    g_ptr_array_free (fields, TRUE);
    if (text == NULL)
        return;
    if (edit == NULL)
        query_tree_say (
            t, _ ("The file of queries F3 was pressed in is no more: the query is here."), text);
    else if (!query_put_query (t->q, edit, text))
        query_tree_say (t, _ ("The file of queries is being changed: the query is here."), text);
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_tree_drop_pending (query_tree_t *t)
{
    g_clear_pointer (&t->pending, g_free);
    g_clear_pointer (&t->pending_field, g_free);
    g_clear_pointer (&t->pending_file, g_free);
    t->pending_edit = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* The file window of @file, NULL for one with no name: that of @edit while it is in front */
static void *
query_tree_window_of (query_tree_t *t, const char *file, void *edit)
{
    if (file != NULL)
        return query_file_window (t->q, file);
    return t->q->host->window_top_file (t->q->host) == edit ? edit : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_tree_drop_ddl (query_tree_t *t)
{
    t->ddl_wait = FALSE;
    g_clear_pointer (&t->ddl_file, g_free);
    t->ddl_edit = NULL;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
query_tree_init (query_tree_t *t, query_t *q, void (*details) (query_node_t *o, void *data),
                 void *data)
{
    memset (t, 0, sizeof (*t));
    t->q = q;
    t->details = details;
    t->data = data;
    t->model = query_tree_model_new (t);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_done (query_tree_t *t)
{
    // the window is the editor's, destroyed before the plugin closes
    if (t->window != NULL)
        t->window->data = NULL;
    mctree_model_free (t->model);
    if (t->paths != NULL)
        g_hash_table_destroy (t->paths);
    g_free (t->error);
    g_free (t->keep);
    query_tree_drop_pending (t);
    query_tree_drop_ddl (t);
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_new (query_tree_t *t, query_node_t *parent, query_node_kind_t kind, char *name,
                char *text, char *insert)
{
    query_item_t *item = g_new0 (query_item_t, 1);
    query_node_t *n = mctree_model_add_node (
        t->model, parent != NULL ? parent : t->model->root,
        kind == QUERY_NODE_ITEM ? MCTREE_NODE_FIELD : MCTREE_NODE_OBJECT, name, text);

    item->kind = kind;
    item->insert = insert;
    mctree_node_set_data (n, item, query_item_free);
    // an object is read when it opens
    mctree_node_set_lazy (n, kind == QUERY_NODE_OBJECT);
    g_free (name);
    g_free (text);
    return n;
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_object (query_node_t *n)
{
    for (; n != NULL && n->data != NULL && QUERY_ITEM (n)->kind != QUERY_NODE_OBJECT; n = n->parent)
        ;
    return n != NULL && n->data != NULL ? n : NULL;
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_group (query_tree_t *t, query_node_t *root, const char *name, gboolean open)
{
    query_node_t *group;
    guint i;

    for (i = 0; root->children != NULL && i < root->children->len; i++)
    {
        group = g_ptr_array_index (root->children, i);
        if (strcmp (group->key, name) == 0)
            return group;
    }
    group = query_node_new (t, root, QUERY_NODE_GROUP, g_strdup (name), NULL, NULL);
    group->expanded = query_tree_was_open (t, group, open);
    return group;
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_parts (query_tree_t *t, query_node_t *o, const char *name)
{
    query_item_t *item = QUERY_ITEM (o);

    if (item->parts == NULL)
    {
        item->parts = query_node_new (t, o, QUERY_NODE_SECTION, g_strdup (name), NULL, NULL);
        item->parts->expanded = query_tree_was_open (t, item->parts, FALSE);
    }
    return item->parts;
}

/* --------------------------------------------------------------------------------------------- */

void
query_node_read (query_node_t *o)
{
    query_item_t *item = QUERY_ITEM (o);

    item->read = TRUE;
    item->asked = FALSE;
    mctree_node_set_lazy (o, FALSE);
    // the partitions after the sections read
    if (item->parts != NULL && o->children != NULL && g_ptr_array_remove (o->children, item->parts))
        g_ptr_array_add (o->children, item->parts);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_forget (query_tree_t *t)
{
    mctree_model_t *was = t->model;
    const mctree_node_t *current = t->window != NULL ? edit_tree_current (t->window) : NULL;
    GHashTable *paths;

    if (current != NULL)
    {
        g_free (t->keep);
        t->keep = mctree_node_path (current);
    }
    // nothing was open before the first time: the backend says what is; what was open under an
    // object not read stays so
    paths = t->read ? mctree_model_kept_paths (was, t->paths) : NULL;
    if (t->paths != NULL)
        g_hash_table_destroy (t->paths);
    t->paths = paths;
    t->model = query_tree_model_new (t);
    // the view of the window off the model of before, which goes
    if (t->window != NULL)
        mctree_view_set_model (t->window->view, t->model);
    mctree_model_free (was);
}

/* --------------------------------------------------------------------------------------------- */

char *
query_tree_about (gint64 n)
{
    if (n < 10000)
        return g_strdup_printf ("~%" G_GINT64_FORMAT, n);
    if (n < 10000000)
        return g_strdup_printf ("~%" G_GINT64_FORMAT "k", (n + 500) / 1000);
    if (n < G_GINT64_CONSTANT (10000000000))
        return g_strdup_printf ("~%" G_GINT64_FORMAT "M", (n + 500000) / 1000000);
    return g_strdup_printf ("~%" G_GINT64_FORMAT "G", (n + 500000000) / 1000000000);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
query_tree_roots (const query_tree_t *t)
{
    return t->model->root->children;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_tree_was_open (const query_tree_t *t, const query_node_t *n, gboolean fallback)
{
    char *path;
    gboolean open;

    if (t->paths == NULL)
        return fallback;
    path = mctree_node_path (n);
    open = g_hash_table_contains (t->paths, path);
    g_free (path);
    return open;
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_tree_find (const query_tree_t *t, gboolean (*match) (const query_node_t *n, const void *data),
                 const void *data)
{
    guint i;

    for (i = 0; i < t->model->nodes->len; i++)
    {
        query_node_t *n = g_ptr_array_index (t->model->nodes, i);

        if (n->data != NULL && QUERY_ITEM (n)->kind == QUERY_NODE_OBJECT && match (n, data))
            return n;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_unask (query_tree_t *t)
{
    guint i;

    // what was asked is lost: a query of F3 waiting for it with it, and the DDL of F4
    query_tree_drop_pending (t);
    if (t->ddl_wait)
    {
        query_tree_drop_ddl (t);
        query_tree_say (t, _ ("The DDL of F4 could not be read."), NULL);
    }
    for (i = 0; i < t->model->nodes->len; i++)
    {
        query_node_t *n = g_ptr_array_index (t->model->nodes, i);

        if (n->data != NULL)
            QUERY_ITEM (n)->asked = FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_render (query_tree_t *t)
{
    mc_editor_host_t *host = t->q->host;

    if (t->window == NULL)
    {
        void *was = host->window_current (host);
        WRect area;

        // closed by the user: what the backend reads does not bring it back, QuerySchema does
        if (t->shown && !t->make)
        {
            query_redraw (t->q);
            return;
        }
        t->window = edit_tree_new (_ ("Schema"), &query_tree_ops, t);
        mctree_view_set_model (t->window->view, t->model);
        host->window_add (host, t->window);
        host->window_area (host, &area);
        host->window_dock_right (host, t->window,
                                 CLAMP (area.cols * QUERY_SCHEMA_SIZE / 100, 24, area.cols / 2));
        // made, it keeps no focus: QuerySchema gives it
        if (was != NULL)
            host->window_show (host, was);
    }
    t->make = FALSE;
    t->shown = TRUE;
    edit_tree_set_message (t->window,
                           t->error != NULL ? t->error
                               : !t->read   ? _ ("Reading...")
                                            : NULL);
    if (t->window->view->model != t->model)
        mctree_view_set_model (t->window->view, t->model);
    if (t->keep != NULL && t->read)
    {
        // read anew: the cursor back on its node, or the nearest of its parents there is
        mctree_view_rebuild (t->window->view);
        (void) mctree_view_goto_path (t->window->view, t->keep);
        g_clear_pointer (&t->keep, g_free);
        widget_draw (WIDGET (t->window));
    }
    else
        edit_tree_rebuild (t->window);
    // an answer comes with no key: the screen is drawn again for it
    query_redraw (t->q);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_tree_toggle (query_tree_t *t)
{
    mc_editor_host_t *host = t->q->host;

    if (t->window == NULL)
        return FALSE;
    if (host->window_current (host) == t->window)
        host->window_hide (host, t->window);
    else
        host->window_show (host, t->window);
    query_redraw (t->q);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_tree_has_focus (const query_tree_t *t)
{
    return t->window != NULL && t->q->host->window_current (t->q->host) == t->window;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_insert (query_tree_t *t, const query_node_t *n)
{
    void *edit = t->q->host->window_top_file (t->q->host);
    const char *insert = n != NULL && n->data != NULL ? QUERY_ITEM (n)->insert : NULL;

    if (insert == NULL || *insert == '\0' || edit == NULL)
        return;
    t->q->host->insert_text (t->q->host, edit, insert, 0);
    query_redraw (t->q);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_rows (query_tree_t *t, query_node_t *n)
{
    const query_node_t *o = query_node_object (n);

    if (o == NULL || QUERY_ITEM (o)->rows == NULL)
        return;
    if (t->q->backend->running (t->q->backend_data))
    {
        t->q->host->message (t->q->host, D_NORMAL, _ ("Query"),
                             _ ("A query is running: its rows come once it ends."));
        return;
    }
    query_run_alone (t->q, QUERY_ITEM (o)->rows);
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_tree_query (query_tree_t *t, query_node_t *n)
{
    query_node_t *o = query_node_object (n);
    query_item_t *item;
    // a field, an item of the first section of its object, alone
    const char *field = n != NULL && n->data != NULL && QUERY_ITEM (n)->kind == QUERY_NODE_ITEM
            && o != NULL && o->children != NULL && o->children->len > 0
            && n->parent == g_ptr_array_index (o->children, 0)
        ? n->key
        : NULL;
    // the file of queries in front now: the query goes there, whatever is in front once it is read
    void *edit = t->q->host->window_top_file (t->q->host);

    if (o == NULL || t->query == NULL || QUERY_ITEM (o)->rows == NULL)
        return NULL;
    item = QUERY_ITEM (o);
    // the last F3 is the one: a query of before waiting for its fields is put no more
    if (t->pending != NULL)
    {
        query_tree_drop_pending (t);
        query_tree_say (t, _ ("The query of F3 of before is put no more: that of this one is."),
                        NULL);
    }
    if (item->read || !o->lazy)
    {
        query_tree_put (t, o, field, edit);
        return NULL;
    }
    // its fields are read first, the query put once they are
    t->pending = mctree_node_path (o);
    t->pending_field = g_strdup (field);
    t->pending_edit = edit;
    t->pending_file = edit != NULL ? t->q->host->get_current_file (t->q->host, edit) : NULL;
    if (item->asked)
        return NULL;
    item->asked = TRUE;
    return o;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_read (query_tree_t *t, query_node_t *o, gboolean ok)
{
    char *path;

    if (t->pending == NULL)
        return;
    path = mctree_node_path (o);
    if (strcmp (path, t->pending) == 0)
    {
        char *field = g_strdup (t->pending_field);
        // the window of F3, by its file; one with no name while it is in front
        void *edit = query_tree_window_of (t, t->pending_file, t->pending_edit);

        query_tree_drop_pending (t);
        if (ok)
            query_tree_put (t, o, field, edit);
        else
            query_tree_say (t, _ ("The columns could not be read: F3 puts no query."), NULL);
        g_free (field);
    }
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_cancel (query_tree_t *t)
{
    // the DDL of F4 is of no node: it is put all the same
    query_tree_drop_pending (t);
}

/* --------------------------------------------------------------------------------------------- */

guint
query_tree_ddl_ask (query_tree_t *t)
{
    // the file of queries in front now: the DDL goes there, whatever is in front once it is read
    void *edit = t->q->host->window_top_file (t->q->host);

    if (t->ddl_wait)
        query_tree_say (t, _ ("The DDL of F4 of before is put no more: that of this one is."),
                        NULL);
    query_tree_drop_ddl (t);
    t->ddl_wait = TRUE;
    t->ddl_edit = edit;
    t->ddl_file = edit != NULL ? t->q->host->get_current_file (t->q->host, edit) : NULL;
    // the answer of an F4 of before, read while this one waits, is not taken for it
    return ++t->ddl_id;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_ddl_put (query_tree_t *t, guint id, const char *text, const char *error)
{
    void *edit;

    if (!t->ddl_wait || id != t->ddl_id)
        return;
    edit = query_tree_window_of (t, t->ddl_file, t->ddl_edit);
    query_tree_drop_ddl (t);
    if (text == NULL)
        query_tree_say (t, error != NULL ? error : _ ("The DDL of F4 could not be read."), NULL);
    else if (edit == NULL)
        query_tree_say (t, _ ("The file of queries F4 was pressed in is no more: the DDL is here."),
                        text);
    else if (!query_put_query (t->q, edit, text))
        query_tree_say (t, _ ("The file of queries is being changed: the DDL is here."), text);
}

/* --------------------------------------------------------------------------------------------- */
