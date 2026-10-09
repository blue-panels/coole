/** \file git-core.h
 *  \brief Header: the git of a repository: what it runs, what its answers say
 */

#ifndef MC__GIT_CORE_H
#define MC__GIT_CORE_H

#include <stdarg.h>

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* What git prints, and how it ended */
typedef struct
{
    GString *out;
    GString *err;
    int status; /* the exit code, -1 when git did not run or was killed */
} git_output_t;

/* A file of the status: in the index (@code the X of git status) or in the work tree (the Y) */
typedef struct
{
    char code;  /* M A D R C T U, '?' for a file git does not track */
    char *path; /* relative to the root */
    char *orig; /* of a rename or a copy: the name it had, else NULL */
} git_change_t;

/* A commit of the log */
typedef struct
{
    char *sha;
    char *author;
    gint64 time; /* of the author, seconds since the epoch */
    char *refs;  /* "HEAD -> master, origin/master", "" for none */
    char *subject;
    int parents;
    char **parent_shas; /* NULL-terminated, in the order git gave them */
} git_commit_t;

/* What a column of a row of the graph has: lines to its sides, the commit of the row */
enum
{
    GIT_GRAPH_UP = 1,
    GIT_GRAPH_DOWN = 2,
    GIT_GRAPH_LEFT = 4,
    GIT_GRAPH_RIGHT = 8,
    GIT_GRAPH_NODE = 16,
    GIT_GRAPH_MERGE = 32 /* the commit is a merge */
};

/* What joins two columns of a row */
enum
{
    GIT_GRAPH_LINK_NONE,
    GIT_GRAPH_LINK_LINE,
    GIT_GRAPH_LINK_TO_LEFT, /* the line of a merge, its arrow into the commit at the left */
    GIT_GRAPH_LINK_TO_RIGHT
};

/* The colors of the branches: those of the kinds of git flow, then the others in turn */
enum
{
    GIT_GRAPH_COLOR_MAIN,
    GIT_GRAPH_COLOR_DEVELOP,
    GIT_GRAPH_COLOR_RELEASE,
    GIT_GRAPH_COLOR_HOTFIX,
    GIT_GRAPH_COLOR_OTHER /* the first of the others */
};

/* A row of the graph, that of a commit or one between two commits that joins lines: each branch
   in a column of its own, as git-graph has them.  One block, freed with g_free() */
typedef struct
{
    int commit;         /* its index in the log, -1 for a row that only joins lines */
    int cols;           /* the columns of the graph, the same in all its rows */
    int node;           /* that of the commit */
    guint8 *line;       /* @cols: GIT_GRAPH_* */
    guint8 *link;       /* @cols - 1: what joins column k and k + 1, GIT_GRAPH_LINK_* */
    guint8 *color;      /* @cols: GIT_GRAPH_COLOR_* and those after it */
    guint8 *link_color; /* @cols - 1 */
} git_graph_row_t;

/* The branch of the work tree */
typedef struct
{
    char *head;     /* the branch, NULL when HEAD is detached */
    char *sha;      /* of HEAD, NULL when the branch has no commit yet */
    char *upstream; /* NULL when none */
    int ahead;
    int behind;
} git_branch_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Run git in @root with @args, @env added to its environment (each "NAME=value", NULL for
   none); FALSE when it could not be started.  Free @output with git_output_clear() */
gboolean git_run (const char *root, const char *const *args, const char *const *env,
                  git_output_t *output);
void git_output_clear (git_output_t *output);
/* What git said of the failure: its errors, else its output, else the exit code */
char *git_output_error (const git_output_t *output);
/* The arguments of @ap up to a NULL, as an array ending with it; g_free() it */
const char **git_args_va (va_list ap);
/* What git in @root prints for @args, "" when it could not be started; its length in @len when
   that is not NULL */
char *git_capture_args (const char *root, const char *const *args, gsize *len);
/* The same for the arguments after @len, up to a NULL */
char *git_capture (const char *root, gsize *len, ...) G_GNUC_NULL_TERMINATED;
/* Whether git in @root, with the arguments up to a NULL, ran and ended with 0 */
gboolean git_succeeds (const char *root, ...) G_GNUC_NULL_TERMINATED;
/* The exit code of git in @root with the arguments up to a NULL, -1 when it did not run */
int git_exit_code (const char *root, ...) G_GNUC_NULL_TERMINATED;

/* Run @command with sh in @dir, @input on its stdin, @env added to its environment (each
   "NAME=value", NULL for none); FALSE when it could not be started */
gboolean git_run_shell (const char *dir, const char *command, const char *input,
                        const char *const *env, git_output_t *output);

/* The work tree a file or a directory is in, NULL when it is in none */
char *git_toplevel (const char *path);
/* The directory of the repository of the work tree @root (.git, or that of a worktree) */
char *git_dir (const char *root);

/* The answer of git status --porcelain=v2 -z --branch, @len bytes: the changes in the index and in
   the work tree, git_change_t, and the branch */
void git_parse_status (const char *text, gsize len, GPtrArray *staged, GPtrArray *unstaged,
                       git_branch_t *branch);
void git_branch_clear (git_branch_t *branch);
void git_change_free (gpointer change);

/* The format of the log git_parse_log() reads */
#define GIT_LOG_FORMAT "--format=%H%x1f%an%x1f%at%x1f%D%x1f%P%x1f%s%x1e"
GPtrArray *git_parse_log (const char *text, gsize len);
void git_commit_free (gpointer commit);
/* The graph of @commits, git log --topo-order of all the refs: a git_graph_row_t for each, and
   one before a commit whose row has the line of a merge and those of branches that went off from
   it.  @remotes, the names of the remotes, tell their branches from the local ones */
GPtrArray *git_graph_build (const GPtrArray *commits, const char *const *remotes);

/* The answer of git diff --name-status -z: git_change_t */
GPtrArray *git_parse_name_status (const char *text, gsize len);

/* A message as git commit --cleanup=strip makes it: no line that starts with #, no blanks at
   the ends of the lines, no empty lines at its ends nor two in a row; "" when nothing is left */
char *git_message_strip (const char *text);

/* The text of a message file with @trailer ("Signed-off-by: A <a@b>") at the end of the
   message, before the lines git leaves out: in the block of trailers when the message ends
   with one, else after an empty line; the text as it is when the trailer is there already */
char *git_message_add_trailer (const char *text, const char *trailer);
/* The text of a message file with @message instead of its message; the trailers it ended with
   stay after the new one when @keep_trailers, and the lines git leaves out stay */
char *git_message_replace (const char *text, const char *message, gboolean keep_trailers);
/* What is wrong with the shape of the message of a message file, a line for each thing; ""
   when nothing is */
char *git_message_check (const char *text);

/* An item of the menu of the message of a commit (F11), from git-menu.ini: a group for each,
   named by its hotkey */
typedef struct
{
    char key;
    char *label;   /* NULL: that of the action */
    char *action;  /* one of the plugin: signoff, coauthor, ...; NULL for a command */
    char *command; /* run by sh in the work tree, the message on its stdin */
    char *output;  /* what is done with what the command prints: trailer, replace, insert, show,
                      none */
    char **modes;  /* commit, amend, reword: those it is in the menu for; NULL for all */
} git_menu_item_t;

/* The menu as git-menu.ini gives it, @text; the groups of no item, without an action or a
   command, are left out */
GPtrArray *git_menu_parse (const char *text, GError **error);
gboolean git_menu_item_in_mode (const git_menu_item_t *item, const char *mode);

/* Give the commit @sha the message @message, the commits after it up to HEAD made again on
   top of it; the index and the work tree are not touched.  The commits after it must be no
   merges.  The new sha of HEAD in @new_head when it is not NULL */
gboolean git_reword (const char *root, const char *sha, const char *message, char **new_head,
                     GError **error);

/* A ref of the list of the branches */
typedef enum
{
    GIT_REF_LOCAL,
    GIT_REF_REMOTE,
    GIT_REF_TAG
} git_ref_kind_t;

typedef struct
{
    git_ref_kind_t kind;
    char *ref;     /* refs/heads/x, refs/remotes/origin/x, refs/tags/x */
    char *name;    /* x, origin/x, x */
    char *sha;     /* of the commit, a tag peeled */
    gint64 time;   /* of the last commit, of a tag that has one its own */
    char *author;  /* of the last commit, the tagger of a tag */
    char *subject; /* of the last commit, of the message of a tag */
    gboolean current;
    gboolean main;     /* main, master or trunk, of a remote or not */
    int ahead, behind; /* commits it has that HEAD has not, and the other way; -1 unknown */
    /* what the filter looks in, folded: the name; the author and the subject */
    char *fold_name, *fold_head;
    GPtrArray *own; /* git_own_commit_t of its own commits, those main has not, the last first */
} git_ref_t;

typedef struct
{
    char *subject;
    char *fold; /* the author and the message, folded */
} git_own_commit_t;

/* The format of for-each-ref git_parse_refs reads, @ahead_behind with the commits apart from
   HEAD, which needs git 2.41 */
const char *git_refs_format (gboolean ahead_behind);
/* The refs of for-each-ref with that format, those of HEAD of the remotes left out */
GPtrArray *git_parse_refs (const char *text, gsize len);
void git_ref_free (gpointer ref);
/* The format of git log git_refs_own reads */
#define GIT_OWN_FORMAT "--format=%H%x1f%P%x1f%an%x1f%B%x1e"
/* The commits of each ref that main has not, from git log GIT_OWN_FORMAT of all of them but
   main; a main has none.  The array returned has them, to be freed after the refs */
GPtrArray *git_refs_own (GPtrArray *refs, const char *log, gsize len);
/* Whether @ref has every word of @words, folded, in its name, its last commit or, with @own, its
   own commits; @score the more the more of them are in its name, @subject that of the commit of
   own commits they are found in, NULL when they are not */
gboolean git_ref_match (const git_ref_t *ref, char *const *words, gboolean own, int *score,
                        const char **subject);
/* The words of a filter, folded */
char **git_filter_words (const char *filter);
/* How long ago @time was at @now, "3 days ago" */
char *git_age (gint64 now, gint64 time);

/*** inline functions ****************************************************************************/

#endif /* MC__GIT_CORE_H */
