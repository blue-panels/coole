/** \file query-tree.h
 *  \brief Header: the tree of the window Schema, whatever the database
 *
 *  The schemas or the databases, the groups of their objects, the objects, their sections and
 *  the items of those: a line of the window each, folded or unfolded.  What is unfolded, and the
 *  line of the cursor, stay by the path of their names when the backend reads the tree again.
 *  The backend fills the tree; this draws it and does what the keys of the window do.
 */

#ifndef MC__QUERY_TREE_H
#define MC__QUERY_TREE_H

#include "lib/global.h"

#include "query-int.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

typedef enum
{
    QUERY_NODE_ROOT,     // a schema, a database
    QUERY_NODE_GROUP,    // the objects of a kind of it: Tables, Collections
    QUERY_NODE_OBJECT,   // a relation, a collection: its sections read when it is unfolded
    QUERY_NODE_SECTION,  // the columns of an object, its keys, its indexes
    QUERY_NODE_ITEM
} query_node_kind_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct query_node_t
{
    query_node_kind_t kind;
    char *name;    // what is shown, and its key among those beside it
    char *text;    // shown after the name, NULL for nothing
    char *insert;  // what Insert puts in the file, NULL for nothing
    gboolean open;

    // an object
    char *rows;      // F5: the query of its rows, NULL for none
    guint64 id;      // the backend's: the Oid of a relation
    char tag;        // the backend's: the relkind of a relation
    gboolean leaf;   // nothing under it: a sequence
    gboolean read;   // its sections are read
    gboolean asked;  // and are asked for

    GPtrArray *children;         // query_node_t; of an object its sections once read
    struct query_node_t *parts;  // of an object, a section after its sections: the partitions
    struct query_node_t *parent;
} query_node_t;

typedef struct
{
    query_t *q;
    GPtrArray *roots;  // query_node_t
    GPtrArray *lines;  // query_node_t of each line of the window, NULL for one of none
    char *keep;        // the path of what the cursor is on, for it to stay there
    GHashTable *seen;  // the paths of the nodes before the tree was read again, TRUE when open
    gboolean shown;    // the window is made
    gboolean make;     // and is made again when the user has closed it: QuerySchema
    gboolean read;     // the tree was read once
    char *error;       // why it could not be, NULL for none

    // F3: the query of the rows of @o of its @fields, the inserts of its first section, all of
    // them or one, an array of none for none known; of the backend
    char *(*query) (const query_node_t *o, GPtrArray *fields);
    char *pending;  // the path of the object whose query waits for its sections, NULL for none
    char *pending_field;  // and the field it is of, NULL for all of them
    char *pending_file;   // the file of queries F3 was pressed in, NULL for one with no name
    void *pending_edit;   // its window then
} query_tree_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void query_tree_init (query_tree_t *t, query_t *q);
void query_tree_done (query_tree_t *t);

/* A node of @kind under @parent, NULL for none; an object's section of its partitions is put
   in its parts by the backend.  @name, @text and @insert are taken. */
query_node_t *query_node_new (query_node_t *parent, query_node_kind_t kind, char *name, char *text,
                              char *insert);
void query_node_free (gpointer data);
/* Whether @n has lines under it to unfold */
gboolean query_node_folds (const query_node_t *n);
/* The object @n is of, itself or what is under it; NULL for none */
query_node_t *query_node_object (query_node_t *n);
/* The group @name of @root, made when it has none, open or not as before, else @open */
query_node_t *query_node_group (query_tree_t *t, query_node_t *root, const char *name,
                                gboolean open);

/* The roots read again: what was open remembered by the paths, the roots of before gone */
void query_tree_forget (query_tree_t *t);
/* Whether @n was open before the tree was read again; @fallback when it was not there */
gboolean query_tree_was_open (const query_tree_t *t, const query_node_t *n, gboolean fallback);
/* The object for which @match is true, of @data, NULL for none */
query_node_t *query_tree_find (const query_tree_t *t,
                               gboolean (*match) (const query_node_t *n, const void *data),
                               const void *data);
/* No object is asked for, nor a query of F3: the backend lost what it asked */
void query_tree_unask (query_tree_t *t);

/* The node of the 0-based @line of the window; NULL for none */
query_node_t *query_tree_node_at (const query_tree_t *t, long line);
/* What the cursor of the window is on, kept by its path before the text is made again */
void query_tree_mark (query_tree_t *t);
/* The text of the window made again from the tree, the cursor on what it was on */
void query_tree_render (query_tree_t *t);

/* Enter on @n: it is folded or unfolded; the object whose sections are to be read, NULL for
   none */
query_node_t *query_tree_enter (query_tree_t *t, query_node_t *n);
/* Insert on @n: what it names put at the cursor of the file of queries */
void query_tree_insert (query_tree_t *t, const query_node_t *n);
/* F5 on @n: the rows of its object in Table, a query of no file */
void query_tree_rows (query_tree_t *t, query_node_t *n);
/* F3 on @n: the query of the rows of its object put in the file of queries, of all its fields,
   or of the field @n alone; the object whose sections are to be read first, NULL for none */
query_node_t *query_tree_query (query_tree_t *t, query_node_t *n);
/* The sections of @o are read, @ok, or could not be: the query that waited for them put, or not
   and told */
void query_tree_read (query_tree_t *t, query_node_t *o, gboolean ok);
/* What was asked is asked no more, the tree read again: no query of F3 waits */
void query_tree_cancel (query_tree_t *t);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_TREE_H */
