/** \file query-tree.h
 *  \brief Header: the tree of the window Schema, whatever the database
 *
 *  The schemas or the databases, the groups of their objects, the objects, their sections and
 *  the items of those, nodes of an mctree shown by a tree window of the editor.  What is
 *  unfolded, and the node of the cursor, stay by the path of their names when the backend reads
 *  the tree again.  The backend fills the tree; this shows it and does what F3, F5 and Insert do.
 */

#ifndef MC__QUERY_TREE_H
#define MC__QUERY_TREE_H

#include "lib/global.h"
#include "lib/mctree/mctree-model.h"

#include "src/editor/edittree.h"

#include "query-int.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* A node of the tree: an mctree node, its key the name, its value the text after it, its data a
   query_item_t */
typedef mctree_node_t query_node_t;

#define QUERY_ITEM(n) ((query_item_t *) (n)->data)

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

/* What a node is to Schema */
typedef struct
{
    query_node_kind_t kind;
    char *insert;  // what Insert puts in the file, NULL for nothing

    // an object
    char *rows;           // F5: the query of its rows, NULL for none
    guint64 id;           // the backend's: the Oid of a relation
    char tag;             // the backend's: the relkind of a relation
    gboolean read;        // its sections are read
    gboolean asked;       // and are asked for
    query_node_t *parts;  // a section after its sections: the partitions, NULL for none
} query_item_t;

typedef struct
{
    query_t *q;
    mctree_model_t *model;  // the tree read, its root of no name holding the roots shown
    edit_tree_t *window;    // the window Schema, NULL when there is none
    GHashTable *paths;      // the paths open before the tree was read again, NULL the first time
    char *keep;             // the path of the node of the cursor then
    gboolean shown;         // the window was made once
    gboolean make;          // and is made again when the user has closed it: QuerySchema
    gboolean read;          // the tree was read once
    char *error;            // why it could not be, NULL for none

    // an object opened whose sections are to be read: the backend asks for them
    void (*details) (query_node_t *o, void *data);
    void *data;

    // F3: the query of the rows of @o of its @fields, the inserts of its first section, all of
    // them or one, an array of none for none known; of the backend
    char *(*query) (const query_node_t *o, GPtrArray *fields);
    char *pending;  // the path of the object whose query waits for its sections, NULL for none
    char *pending_field;  // and the field it is of, NULL for all of them
    char *pending_file;   // the file of queries F3 was pressed in, NULL for one with no name
    void *pending_edit;   // its window then

    // F4: the DDL asked for waits to be put in the file F4 was pressed in, NULL for one with no
    // name, and its window then
    gboolean ddl_wait;
    guint ddl_id;  // of the last F4: the answer of another is not put
    char *ddl_file;
    void *ddl_edit;
} query_tree_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* @details asks for the sections of an object opened, with @data */
void query_tree_init (query_tree_t *t, query_t *q, void (*details) (query_node_t *o, void *data),
                      void *data);
void query_tree_done (query_tree_t *t);

/* A node of @kind under @parent, NULL for a root; an object is lazy, its sections read when it
   opens.  @name, @text and @insert are taken. */
query_node_t *query_node_new (query_tree_t *t, query_node_t *parent, query_node_kind_t kind,
                              char *name, char *text, char *insert);
/* The object @n is of, itself or what is under it; NULL for none */
query_node_t *query_node_object (query_node_t *n);
/* The group @name of @root, made when it has none, open or not as before, else @open */
query_node_t *query_node_group (query_tree_t *t, query_node_t *root, const char *name,
                                gboolean open);
/* The section of the partitions of @o, made when it has none, after its sections */
query_node_t *query_node_parts (query_tree_t *t, query_node_t *o, const char *name);
/* The sections of @o read: @o is lazy no more, and its section of partitions goes after them */
void query_node_read (query_node_t *o);

/* About @n rows or documents, the server's reckoning: *~950*, *~50k*, *~3M* */
char *query_tree_about (gint64 n);

/* The tree read again from nothing: what was open, and the node of the cursor, remembered by
   their paths */
void query_tree_forget (query_tree_t *t);
/* The roots of the tree: the children of its root of no name, NULL for none */
GPtrArray *query_tree_roots (const query_tree_t *t);
/* Whether @n was open before the tree was read again; @fallback the first time */
gboolean query_tree_was_open (const query_tree_t *t, const query_node_t *n, gboolean fallback);
/* The object for which @match is true, of @data, NULL for none */
query_node_t *query_tree_find (const query_tree_t *t,
                               gboolean (*match) (const query_node_t *n, const void *data),
                               const void *data);
/* No object is asked for, nor a query of F3: the backend lost what it asked */
void query_tree_unask (query_tree_t *t);

/* The window made again from the tree, made itself when it is to be */
void query_tree_render (query_tree_t *t);
/* QuerySchema: the window hidden when it has the focus, else shown with it; FALSE when there is
   none, to be made */
gboolean query_tree_toggle (query_tree_t *t);
/* Whether the window has the focus: its keys are its own, nodes or none */
gboolean query_tree_has_focus (const query_tree_t *t);

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
/* F4: the DDL the backend reads goes to the file of queries in front now; the id of the DDL
   asked for, which the backend gives back with it */
guint query_tree_ddl_ask (query_tree_t *t);
/* The DDL of F4 @id read, @text, or not, @error, NULL for a word of its own: put in the file F4
   was pressed in, or told; that of an F4 of before is not */
void query_tree_ddl_put (query_tree_t *t, guint id, const char *text, const char *error);

/*** inline functions ****************************************************************************/

#endif /* MC__QUERY_TREE_H */
