/** \file edittree.h
 *  \brief Header: a window of the editor that shows a tree, an mctree of its owner
 *
 *  The window draws the rows of an mctree_view: the depth, + or - when a node opens, its key and
 *  its value; the keys move over them, open and close them.  What a node is, and what the other
 *  keys do on it, are of the owner: a plugin, by the functions it gives.
 */

#ifndef MC__EDIT_TREE_H
#define MC__EDIT_TREE_H

#include "lib/global.h"
#include "lib/mctree/mctree-view.h"
#include "lib/widget.h"

#include "src/editor/editwindow.h"

/*** typedefs(not structures) and defined constants **********************************************/

typedef struct edit_tree_t edit_tree_t;

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* What the owner does */
typedef struct
{
    /* The name of the row of @node after its marker, and what is at the right of the row;
       NULL: its key, and its value at the right */
    void (*row) (edit_tree_t *tree, const mctree_node_t *node, GString *name, GString *info);
    /* Enter or a double click on @node: TRUE when it is taken, else the node opens or closes */
    gboolean (*activate) (edit_tree_t *tree, mctree_node_t *node);
    /* Whether the row of @node stands out, the file in front for one */
    gboolean (*bold) (edit_tree_t *tree, const mctree_node_t *node);
    /* The colors of the name of @node and of what is at its right, -1 for those of the row; the
       row of the cursor has its own */
    void (*colors) (edit_tree_t *tree, const mctree_node_t *node, int *name, int *info);
    /* The same on the row of the cursor, on its background; NULL: the row of one color */
    void (*cursor_colors) (edit_tree_t *tree, const mctree_node_t *node, int *name, int *info);
    /* A key the window does not take, on @node, NULL for none: TRUE when it is taken */
    gboolean (*key) (edit_tree_t *tree, mctree_node_t *node, int key);
    /* A command of the button bar of the owner on @node, NULL for none: TRUE when it is taken */
    gboolean (*action) (edit_tree_t *tree, mctree_node_t *node, long command);
    /* The labels of the button bar when the window has the focus, but F1 and F10 */
    void (*buttonbar) (edit_tree_t *tree, WButtonBar *bb);
    /* The window is closed and gone: the owner forgets it */
    void (*closed) (edit_tree_t *tree);
} edit_tree_ops_t;

struct edit_tree_t
{
    WEditWindow window;
    char *title;
    mctree_view_t *view;  // of the model of the owner, which keeps it
    char *message;        // shown in place of the rows: "Reading...", an error; NULL for none
    const edit_tree_ops_t *ops;
    void *data;  // the owner's
    // the marks of a node closed and open, from the skin, as those of the tree of the project
    char *glyph_closed;
    char *glyph_open;
};

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* A tree window of @title, not added yet: the owner adds it with the host, window_add () and
   window_dock_right () */
/* The key in [editor] of the skin of the cursor of a tree, and its color */
const char *edit_tree_cursor_key (void);
int edit_tree_cursor_color (void);
edit_tree_t *edit_tree_new (const char *title, const edit_tree_ops_t *ops, void *data);
/* The title of the frame, @title */
void edit_tree_set_title (edit_tree_t *tree, const char *title);
/* The model the window shows, the owner's; the cursor at its start */
void edit_tree_set_model (edit_tree_t *tree, mctree_model_t *model);
/* The rows made again from the model, which has changed: the cursor stays on its node, else on
   the nearest of its parents shown */
void edit_tree_rebuild (edit_tree_t *tree);
/* @message shown in place of the rows, NULL for the rows */
void edit_tree_set_message (edit_tree_t *tree, const char *message);
/* The node of the cursor, NULL for none */
mctree_node_t *edit_tree_current (const edit_tree_t *tree);

/*** inline functions ****************************************************************************/

#endif /* MC__EDIT_TREE_H */
