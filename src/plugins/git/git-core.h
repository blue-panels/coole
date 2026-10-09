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

/*** inline functions ****************************************************************************/

#endif /* MC__GIT_CORE_H */
