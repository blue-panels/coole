/** \file mctree-model.h
 *  \brief Header: structured tree model for document-like content
 */

#ifndef MC__MCTREE_MODEL_H
#define MC__MCTREE_MODEL_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MCTREE_DEFAULT_SCALAR_PREVIEW_LIMIT 160

/* What joins the keys of a path, which no key has */
#define MCTREE_PATH_SEP "\x1f"

/*** enums ***************************************************************************************/

typedef enum
{
    MCTREE_NODE_ROOT,
    MCTREE_NODE_OBJECT,
    MCTREE_NODE_ARRAY,
    MCTREE_NODE_FIELD,
    MCTREE_NODE_ITEM,
    MCTREE_NODE_SCALAR,
    MCTREE_NODE_ELEMENT,
    MCTREE_NODE_ATTRIBUTE,
    MCTREE_NODE_TEXT
} mctree_node_type_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct mctree_node_t mctree_node_t;
typedef struct mctree_model_t mctree_model_t;

/* Filter predicate: TRUE when the node itself matches. */
typedef gboolean (*mctree_node_match_fn) (const mctree_node_t *node, void *user_data);
/* A lazy node is opened: the owner reads its children, adds them with mctree_model_add_node ()
   and calls mctree_node_set_lazy (node, FALSE), then rebuilds the view; it may be called again
   while they are read */
typedef void (*mctree_node_children_fn) (mctree_model_t *model, mctree_node_t *node,
                                         void *user_data);

typedef struct
{
    mctree_node_t *node;
    int depth;
} mctree_visible_row_t;

struct mctree_node_t
{
    mctree_node_type_t type;
    char *key;
    char *value;
    gsize original_value_len;
    gboolean value_truncated;
    gboolean expanded;
    gboolean filter_hit;    /* the node itself matches the filter */
    gboolean filter_hidden; /* dropped by the filter: no match in it, above it or below it */
    mctree_node_t *parent;
    GPtrArray *children;
    gboolean lazy;            /* its children are read when it opens, the first time */
    void *data;               /* the owner's, freed by data_free */
    GDestroyNotify data_free; /* NULL: data is not freed */
};

struct mctree_model_t
{
    GPtrArray *nodes;
    mctree_node_t *root;
    gsize scalar_preview_limit;
    gboolean filter_on;
    mctree_node_children_fn children; /* a lazy node opened: NULL for none */
    void *children_data;
};

/*** declarations of public functions ************************************************************/

mctree_model_t *mctree_model_new (gsize scalar_preview_limit);
void mctree_model_free (mctree_model_t *model);

mctree_node_t *mctree_model_add_node (mctree_model_t *model, mctree_node_t *parent,
                                      mctree_node_type_t type, const char *key, const char *value);

void mctree_model_expand_to_depth (mctree_model_t *model, int depth);
GArray *mctree_model_build_visible_rows (const mctree_model_t *model);

guint mctree_model_filter_apply (mctree_model_t *model, mctree_node_match_fn match,
                                 void *user_data);
void mctree_model_filter_clear (mctree_model_t *model);

guint mctree_node_child_count (const mctree_node_t *node);
/* Whether the node opens: it has children, or is lazy */
gboolean mctree_node_expandable (const mctree_node_t *node);
/* A lazy node with no children yet, or one that is read */
void mctree_node_set_lazy (mctree_node_t *node, gboolean lazy);
/* The data of the owner, freed with the node by data_free */
void mctree_node_set_data (mctree_node_t *node, void *data, GDestroyNotify data_free);
/* The node opened: a lazy one asks the owner for its children */
void mctree_node_expand (mctree_model_t *model, mctree_node_t *node);

/* The path of the node: the keys from the root to it, the type for a node with none, joined by
   MCTREE_PATH_SEP; "" for the root */
char *mctree_node_path (const mctree_node_t *node);
/* The node of the path, NULL for none */
mctree_node_t *mctree_model_find_path (const mctree_model_t *model, const char *path);
/* The paths of the nodes open, to open them again in a model read anew; free it with
   g_hash_table_destroy () */
GHashTable *mctree_model_expanded_paths (const mctree_model_t *model);
/* The paths open in @model, and those of @before, NULL for none, whose nodes it has not read: a
   node under a lazy one never opened is open again once that one is; free it with
   g_hash_table_destroy () */
GHashTable *mctree_model_kept_paths (const mctree_model_t *model, GHashTable *before);
/* The nodes of @node and under it whose path @paths has opened, lazy ones asking for their
   children; the nodes it has not keep their state */
void mctree_node_restore_expanded (mctree_model_t *model, mctree_node_t *node, GHashTable *paths);
guint mctree_node_descendant_count (const mctree_node_t *node);
const char *mctree_node_type_name (mctree_node_type_t type);

#endif
