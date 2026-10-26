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
 *  Its text is made again from the tree each time a line is folded or unfolded, or the backend
 *  has read more of it, the cursor staying on what it was on, by the names of its path.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"  // COLS
#include "lib/widget.h"   // D_NORMAL

#include "query-tree.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

// what joins the keys of the nodes of a path, which no name of a catalog has
#define QUERY_TREE_PATH_SEP "\x1f"

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

static void query_tree_drop_pending (query_tree_t *t);

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* The path of @n, the keys of the nodes from its root to it: their kind and name */
static char *
query_node_path (const query_node_t *n)
{
    GString *path = g_string_new (NULL);

    for (; n != NULL; n = n->parent)
    {
        char *key = g_strdup_printf ("%d%s%s", (int) n->kind, n->name,
                                     path->len > 0 ? QUERY_TREE_PATH_SEP : "");

        g_string_prepend (path, key);
        g_free (key);
    }
    return g_string_free (path, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* The path of @n and those of the nodes under it, open or not, in @seen */
static void
query_node_remember (const query_node_t *n, GHashTable *seen)
{
    guint i;

    g_hash_table_insert (seen, query_node_path (n), GINT_TO_POINTER (n->open));
    for (i = 0; n->children != NULL && i < n->children->len; i++)
        query_node_remember (g_ptr_array_index (n->children, i), seen);
    if (n->parts != NULL)
        query_node_remember (n->parts, seen);
}

/* --------------------------------------------------------------------------------------------- */

static query_node_t *
query_node_find (query_node_t *n, gboolean (*match) (const query_node_t *n, const void *data),
                 const void *data)
{
    query_node_t *found = NULL;
    guint i;

    if (n->kind == QUERY_NODE_OBJECT && match (n, data))
        return n;
    for (i = 0; found == NULL && n->children != NULL && i < n->children->len; i++)
        found = query_node_find (g_ptr_array_index (n->children, i), match, data);
    if (found == NULL && n->parts != NULL)
        found = query_node_find (n->parts, match, data);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

static void
query_node_unask (query_node_t *n)
{
    guint i;

    n->asked = FALSE;
    for (i = 0; n->children != NULL && i < n->children->len; i++)
        query_node_unask (g_ptr_array_index (n->children, i));
    if (n->parts != NULL)
        query_node_unask (n->parts);
}

/* --------------------------------------------------------------------------------------------- */

static void
query_tree_add_line (query_tree_t *t, GString *text, query_node_t *n, const char *line)
{
    g_string_append (text, line);
    g_string_append_c (text, '\n');
    g_ptr_array_add (t->lines, n);
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of @what, each a line of the window that is of nothing, cut at the blanks to the
   width of the window: an error is a long line */
static void
query_tree_add_lines (query_tree_t *t, GString *text, const char *what)
{
    const long width = MAX (20, (long) COLS * QUERY_SCHEMA_SIZE / 100 - 4);
    char **lines = g_strsplit (what, "\n", -1);
    char **l;

    for (l = lines; *l != NULL; l++)
    {
        const char *rest = *l;

        while (*rest != '\0')
        {
            const char *end = rest, *cut = NULL;
            long n;
            char *part;

            for (n = 0; *end != '\0' && n < width; n++, end = g_utf8_next_char (end))
                if (*end == ' ')
                    cut = end;
            if (*end == '\0' || cut == NULL)
                cut = end;
            part = g_strndup (rest, (gsize) (cut - rest));
            query_tree_add_line (t, text, NULL, part);
            g_free (part);
            rest = cut;
            while (*rest == ' ')
                rest++;
        }
    }
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

/* The number a root, a group or a section shows: what it has; a root what its groups have */
static guint
query_node_count (const query_node_t *n)
{
    guint count = 0, i;

    if (n->kind != QUERY_NODE_ROOT)
        return n->children != NULL ? n->children->len : 0;
    for (i = 0; i < n->children->len; i++)
        count += ((query_node_t *) g_ptr_array_index (n->children, i))->children->len;
    return count;
}

/* --------------------------------------------------------------------------------------------- */

/* The lines of @n at @depth, and those under it when it is open; @at the line of the path kept,
   @near that of the nearest of its parents shown, of @near_len bytes of path */
static void
query_tree_render_node (query_tree_t *t, GString *text, query_node_t *n, int depth, long *at,
                        long *near, gsize *near_len)
{
    const gboolean folds = query_node_folds (n);
    char *path = query_node_path (n);
    const gsize len = strlen (path);
    GString *line = g_string_new (NULL);
    guint i;

    if (t->keep != NULL && strcmp (path, t->keep) == 0)
        *at = (long) t->lines->len;
    else if (t->keep != NULL && len > *near_len && strncmp (path, t->keep, len) == 0
             && strncmp (t->keep + len, QUERY_TREE_PATH_SEP, 1) == 0)
    {
        *near = (long) t->lines->len;
        *near_len = len;
    }
    g_free (path);

    g_string_append_printf (line, "%*s%c %s", depth * 2, "",
                            !folds        ? ' '
                                : n->open ? '-'
                                          : '+',
                            n->name);
    if (n->kind == QUERY_NODE_ROOT || n->kind == QUERY_NODE_GROUP || n->kind == QUERY_NODE_SECTION)
        g_string_append_printf (line, " (%u)", query_node_count (n));
    if (n->text != NULL)
        g_string_append_printf (line, " %s", n->text);
    query_tree_add_line (t, text, n, line->str);
    g_string_free (line, TRUE);

    if (!folds || !n->open)
        return;
    if (n->kind == QUERY_NODE_OBJECT && !n->read)
    {
        char *reading = g_strdup_printf ("%*s%s", depth * 2 + 4, "", _ ("Reading..."));

        query_tree_add_line (t, text, n, reading);
        g_free (reading);
    }
    for (i = 0; n->children != NULL && i < n->children->len; i++)
        query_tree_render_node (t, text, g_ptr_array_index (n->children, i), depth + 1, at, near,
                                near_len);
    if (n->parts != NULL)
        query_tree_render_node (t, text, n->parts, depth + 1, at, near, near_len);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
query_tree_init (query_tree_t *t, query_t *q)
{
    memset (t, 0, sizeof (*t));
    t->q = q;
    t->roots = g_ptr_array_new_with_free_func (query_node_free);
    t->lines = g_ptr_array_new ();
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_done (query_tree_t *t)
{
    g_ptr_array_free (t->lines, TRUE);
    g_ptr_array_free (t->roots, TRUE);
    if (t->seen != NULL)
        g_hash_table_destroy (t->seen);
    g_free (t->error);
    g_free (t->keep);
    query_tree_drop_pending (t);
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_new (query_node_t *parent, query_node_kind_t kind, char *name, char *text, char *insert)
{
    query_node_t *n = g_new0 (query_node_t, 1);

    n->kind = kind;
    n->name = name;
    n->text = text;
    n->insert = insert;
    n->parent = parent;
    if (kind != QUERY_NODE_ITEM && kind != QUERY_NODE_OBJECT)
        n->children = g_ptr_array_new_with_free_func (query_node_free);
    if (parent != NULL && parent->children != NULL)
        g_ptr_array_add (parent->children, n);
    return n;
}

/* --------------------------------------------------------------------------------------------- */

void
query_node_free (gpointer data)
{
    query_node_t *n = (query_node_t *) data;

    g_free (n->name);
    g_free (n->text);
    g_free (n->insert);
    g_free (n->rows);
    if (n->children != NULL)
        g_ptr_array_free (n->children, TRUE);
    if (n->parts != NULL)
        query_node_free (n->parts);
    g_free (n);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_node_folds (const query_node_t *n)
{
    switch (n->kind)
    {
    case QUERY_NODE_OBJECT:
        return !n->leaf;
    case QUERY_NODE_SECTION:
        return n->children->len > 0;
    case QUERY_NODE_ITEM:
        return FALSE;
    default:
        return TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_object (query_node_t *n)
{
    for (; n != NULL && n->kind != QUERY_NODE_OBJECT; n = n->parent)
        ;
    return n;
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_node_group (query_tree_t *t, query_node_t *root, const char *name, gboolean open)
{
    query_node_t *group;
    guint i;

    for (i = 0; i < root->children->len; i++)
    {
        group = g_ptr_array_index (root->children, i);
        if (strcmp (group->name, name) == 0)
            return group;
    }
    group = query_node_new (root, QUERY_NODE_GROUP, g_strdup (name), NULL, NULL);
    group->open = query_tree_was_open (t, group, open);
    return group;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_forget (query_tree_t *t)
{
    guint i;

    if (t->seen != NULL)
        g_hash_table_destroy (t->seen);
    t->seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    for (i = 0; i < t->roots->len; i++)
        query_node_remember (g_ptr_array_index (t->roots, i), t->seen);
    g_ptr_array_set_size (t->roots, 0);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
query_tree_was_open (const query_tree_t *t, const query_node_t *n, gboolean fallback)
{
    char *path = query_node_path (n);
    gpointer open = NULL;
    const gboolean known =
        t->seen != NULL && g_hash_table_lookup_extended (t->seen, path, NULL, &open);

    g_free (path);
    return known ? GPOINTER_TO_INT (open) : fallback;
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_tree_find (const query_tree_t *t, gboolean (*match) (const query_node_t *n, const void *data),
                 const void *data)
{
    query_node_t *found = NULL;
    guint i;

    for (i = 0; found == NULL && i < t->roots->len; i++)
        found = query_node_find (g_ptr_array_index (t->roots, i), match, data);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_unask (query_tree_t *t)
{
    guint i;

    // what was asked is lost: a query of F3 waiting for it with it
    query_tree_drop_pending (t);

    for (i = 0; i < t->roots->len; i++)
        query_node_unask (g_ptr_array_index (t->roots, i));
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_tree_node_at (const query_tree_t *t, long line)
{
    if (line < 0 || (guint) line >= t->lines->len)
        return NULL;
    return g_ptr_array_index (t->lines, line);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_mark (query_tree_t *t)
{
    const query_node_t *n = query_tree_node_at (t, query_schema_line (t->q));

    // a line of none keeps the path of before: the one of "Reading..." is gone a moment after
    if (n != NULL)
    {
        g_free (t->keep);
        t->keep = query_node_path (n);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_render (query_tree_t *t)
{
    GString *text = g_string_new (NULL);
    long at = -1, near = -1;
    gsize near_len = 0;
    guint i;

    g_ptr_array_set_size (t->lines, 0);
    query_tree_add_line (t, text, NULL, _ ("Enter, F3 query, F5 rows, Ins name"));
    if (t->error != NULL)
        query_tree_add_lines (t, text, t->error);
    else if (!t->read)
        query_tree_add_line (t, text, NULL, _ ("Reading..."));
    for (i = 0; i < t->roots->len; i++)
        query_tree_render_node (t, text, g_ptr_array_index (t->roots, i), 0, &at, &near, &near_len);
    // what the cursor was on is gone: the nearest of its parents, the first root at the start
    if (at < 0)
        at = near;
    if (at < 0)
        at = t->keep != NULL || t->lines->len < 2 ? MAX (query_schema_line (t->q), 0) : 1;
    query_schema_set (t->q, text->str, at, t->make || !t->shown);
    t->make = FALSE;
    t->shown = TRUE;
    g_string_free (text, TRUE);
    // an answer comes with no key: the screen is drawn again for it
    query_redraw (t->q);
}

/* --------------------------------------------------------------------------------------------- */

query_node_t *
query_tree_enter (query_tree_t *t, query_node_t *n)
{
    if (n == NULL || !query_node_folds (n))
        return NULL;
    query_tree_mark (t);
    n->open = !n->open;
    if (n->kind == QUERY_NODE_OBJECT && n->open && !n->read && !n->asked)
    {
        n->asked = TRUE;
        return n;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_insert (query_tree_t *t, const query_node_t *n)
{
    void *edit = t->q->host->window_top_file (t->q->host);

    if (n == NULL || n->insert == NULL || *n->insert == '\0' || edit == NULL)
        return;
    t->q->host->insert_text (t->q->host, edit, n->insert, 0);
    query_redraw (t->q);
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_rows (query_tree_t *t, query_node_t *n)
{
    const query_node_t *o = query_node_object (n);

    if (o == NULL || o->rows == NULL)
        return;
    if (t->q->backend->running (t->q->backend_data))
    {
        t->q->host->message (t->q->host, D_NORMAL, _ ("Query"),
                             _ ("A query is running: its rows come once it ends."));
        return;
    }
    query_run_alone (t->q, o->rows);
}

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
    const query_node_t *first =
        o->children != NULL && o->children->len > 0 ? g_ptr_array_index (o->children, 0) : NULL;
    GPtrArray *fields = g_ptr_array_new ();
    char *text;
    guint i;

    for (i = 0; first != NULL && i < first->children->len; i++)
    {
        const query_node_t *f = g_ptr_array_index (first->children, i);

        if (f->insert != NULL && (field == NULL || strcmp (f->name, field) == 0))
            g_ptr_array_add (fields, f->insert);
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

query_node_t *
query_tree_query (query_tree_t *t, query_node_t *n)
{
    query_node_t *o = query_node_object (n);
    // a field, an item of the first section of its object, alone
    const char *field = n != NULL && n->kind == QUERY_NODE_ITEM && o != NULL && o->children != NULL
            && o->children->len > 0 && n->parent == g_ptr_array_index (o->children, 0)
        ? n->name
        : NULL;
    // the file of queries in front now: the query goes there, whatever is in front once it is read
    void *edit = t->q->host->window_top_file (t->q->host);

    if (o == NULL || t->query == NULL || o->rows == NULL)
        return NULL;
    // the last F3 is the one: a query of before waiting for its fields is put no more
    if (t->pending != NULL)
    {
        query_tree_drop_pending (t);
        query_tree_say (t, _ ("The query of F3 of before is put no more: that of this one is."),
                        NULL);
    }
    if (o->read || o->leaf)
    {
        query_tree_put (t, o, field, edit);
        return NULL;
    }
    // its fields are read first, the query put once they are
    t->pending = query_node_path (o);
    t->pending_field = g_strdup (field);
    t->pending_edit = edit;
    t->pending_file = edit != NULL ? t->q->host->get_current_file (t->q->host, edit) : NULL;
    if (o->asked)
        return NULL;
    o->asked = TRUE;
    return o;
}

/* --------------------------------------------------------------------------------------------- */

void
query_tree_read (query_tree_t *t, query_node_t *o, gboolean ok)
{
    char *path;

    if (t->pending == NULL)
        return;
    path = query_node_path (o);
    if (strcmp (path, t->pending) == 0)
    {
        char *field = g_strdup (t->pending_field);
        // the window of F3, by its file; one with no name while it is in front
        void *edit = t->pending_file != NULL ? query_file_window (t->q, t->pending_file)
            : t->q->host->window_top_file (t->q->host) == t->pending_edit ? t->pending_edit
                                                                          : NULL;

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
    query_tree_drop_pending (t);
}

/* --------------------------------------------------------------------------------------------- */
