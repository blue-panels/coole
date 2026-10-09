/** \file git-private.h
 *  \brief Header: what the files of the git plugin share: its state, its window and their
 *  functions
 */

#ifndef MC__GIT_PRIVATE_H
#define MC__GIT_PRIVATE_H

#include "lib/global.h"
#include "lib/editor-plugin.h"
#include "lib/tty/tty.h"
#include "lib/widget.h"

#include "src/editor/editwindow.h"

#include "git-core.h"

/*** typedefs(not structures) and defined constants **********************************************/
/* the message files: that of a commit is kept when git refuses it, for the next commit */
#define GIT_MESSAGE_FILE "COOLE_EDITMSG"
#define GIT_AMEND_FILE   "COOLE_AMEND_EDITMSG"
#define GIT_REWORD_FILE  "COOLE_REWORD_EDITMSG"
#define GIT_LOG_STEP     2000 /* the commits read at a time, more as the cursor gets to the last */
#define GIT_CONFIG_GROUP "Git"
#define GIT_CONFIG_GRAPH "graph_refs"

/*** enums ***************************************************************************************/

/* the commands of the plugin, in the [git] section of the keymap */
enum
{
    GIT_CMD_NONE = -1,
    GIT_CMD_STATUS,
    GIT_CMD_LOG,
    GIT_CMD_COMMIT,
    GIT_CMD_COUNT
};

/* the actions of the menus */
enum
{
    GIT_ACT_STATUS,
    GIT_ACT_LOG,
    GIT_ACT_COMMIT
};

enum
{
    GIT_TAB_STATUS,
    GIT_TAB_LOG,
    GIT_TAB_GRAPH
};

/* the lists of the window */
enum
{
    GIT_LIST_UNSTAGED,
    GIT_LIST_STAGED,
    GIT_LIST_COMMITS,
    GIT_LIST_GRAPH,
    GIT_LIST_FILES,
    GIT_LIST_COUNT
};

/* what the message being written is for */
typedef enum
{
    GIT_MSG_NONE,
    GIT_MSG_COMMIT,
    GIT_MSG_AMEND,
    GIT_MSG_REWORD
} git_msg_mode_t;

/* the characters of the graph, [git-graph] of the skin, else its [Lines] */
enum
{
    GIT_GLYPH_VERT,
    GIT_GLYPH_HORIZ,
    GIT_GLYPH_LEFTTOP,
    GIT_GLYPH_RIGHTTOP,
    GIT_GLYPH_LEFTBOTTOM,
    GIT_GLYPH_RIGHTBOTTOM,
    GIT_GLYPH_LEFTMIDDLE,
    GIT_GLYPH_RIGHTMIDDLE,
    GIT_GLYPH_TOPMIDDLE,
    GIT_GLYPH_BOTTOMMIDDLE,
    GIT_GLYPH_CROSS,
    GIT_GLYPH_COMMIT,
    GIT_GLYPH_MERGE,
    GIT_GLYPH_ARROW_LEFT,
    GIT_GLYPH_ARROW_RIGHT,
    GIT_GLYPH_MORE,
    GIT_GLYPH_RAIL,
    GIT_GLYPH_COUNT
};

/* What the graph reads the commits of */
typedef enum
{
    GIT_GRAPH_REFS_CURRENT,  // HEAD alone
    GIT_GRAPH_REFS_LOCAL,    // the branches, the tags and HEAD
    GIT_GRAPH_REFS_REMOTE,   // those and the branches of the remotes
    GIT_GRAPH_REFS_ALL,      // every ref: refs/pr/ and the stash too
    GIT_GRAPH_REFS_COUNT
} git_graph_refs_t;

typedef struct git_window_t git_window_t;

typedef struct
{
    mc_editor_host_t *host;
    long commands[GIT_CMD_COUNT];
    git_window_t *win;

    char *root;  // of the work tree, NULL till one is found
    git_branch_t branch;
    GPtrArray *staged;         // git_change_t
    GPtrArray *unstaged;       // git_change_t
    GPtrArray *commits;        // git_commit_t, NULL till the log is read
    char *log_head;            // the HEAD the log was read at
    char *log_branch;          // its branch name, for HEAD decorations
    GPtrArray *graph_commits;  // git_commit_t of the refs of graph_refs, --topo-order
    git_graph_refs_t graph_refs;
    GPtrArray *graph_rows;  // git_graph_row_t
    int graph_cols;         // the columns of the graph, a branch in each
    int graph_limit;        // the commits it is read with, 0 till it is
    gboolean log_more;      // the log has more commits than those read
    gboolean graph_more;
    GPtrArray *files;          // git_change_t of the commit opened, NULL when none is
    git_commit_t *commit;      // that commit, one of commits or own_commit
    git_commit_t *own_commit;  // a commit opened that is too old for the log, NULL when none

    // the message being written: its file, and what it is for
    git_msg_mode_t msg_mode;
    char *msg_file;
    char *msg_root;
    char *msg_sha;        // of the commit it rewords
    char *msg_head;       // the branch and the HEAD an amend or a reword was started on
    gboolean msg_failed;  // git said no to it: it is kept for the next commit
    void *msg_edit;       // its window, to close once it is committed
    void *msg_window;     // its window among those of the window Git, NULL when it is not
    void *opened;         // the file window F4 opened from the window Git, NULL when none
    char *over_path;      // the file the menu of the message opens next, over the window Git
    long over_line;
    gboolean opened_from_message;  // the file opened was opened from it: back to it

    // the tab of the window to open once the editor is idle: the file window that took the key
    // draws itself after the key
    int open_tab;
    void *open_edit;

    // coole --git: the work tree it names, the editor ending with the window Git; with
    // --git-commit and the like, once the message is done
    char *home;
    gboolean standalone;
    gboolean once;

    // the viewer window of the diff of a message, 0 when none
    gint64 msg_diff_window;

    // a push, a pull or a fetch in the shell of the terminal, its end to come as a signal
    guint terminal_signal;
    gboolean remote_running;
    // the branch an amend or a reword made again: its push is with a lease
    char *rewrote;

    // the colors of the diff, made at the first draw
    gboolean colors;
    int color_add;
    int color_del;
    int color_hunk;
    int color_sha;
    // how far a branch is from another, the characters of the skin
    char *ahead;
    char *behind;
    int color_head;
    // the graph: the colors of its branches, the kinds of git flow then the others in turn, and
    // its characters, those of the skin
    int color_kind[GIT_GRAPH_COLOR_OTHER];
    int *color_other;
    int n_other;
    mc_tty_char_t glyph[GIT_GLYPH_COUNT];
} git_t;

typedef struct
{
    int selected;
    int top;
} git_cursor_t;

/* the three windows of the window Git, glued: the changes not staged (or the log) at the top
   left, the staged ones under them, the diff at the right */
enum
{
    GIT_PANE_LIST,
    GIT_PANE_STAGED,
    GIT_PANE_DIFF,
    GIT_PANE_COUNT
};

typedef struct
{
    WEditWindow window;
    git_window_t *group;  // NULL once the plugin is gone
    int role;
} git_pane_t;

struct git_window_t
{
    git_t *git;
    git_pane_t *pane[GIT_PANE_COUNT];
    gboolean hidden[GIT_PANE_COUNT];  // closed by the user: the others take its room
    gboolean moving_focus;            // the plugin takes the focus to another of its windows
    gint64 unfocused;                 // when one of its windows lost the focus
    int tab;
    int list;          // the list the cursor is in
    gboolean in_diff;  // the keys scroll the diff
    git_cursor_t cursor[GIT_LIST_COUNT];
    GPtrArray *diff;  // its lines, tabs made spaces
    char *diff_of;    // what it is the diff of, so that it is not made again
    char *diff_title;
    int diff_top;
    int diff_left;
    int refs_x, refs_cols;  // the choice of the refs of the graph on its list, 0 wide if not
    int diff_width;         // of its widest line, for the scrollbar along it
};

/*** global variables defined in .c file *********************************************************/

extern const char *const git_graph_refs_key[GIT_GRAPH_REFS_COUNT];
extern const char *const git_graph_refs_label[GIT_GRAPH_REFS_COUNT];

/*** declarations of public functions ************************************************************/

/* git.c */
gboolean git_do (git_t *git, const char *title, ...) G_GNUC_NULL_TERMINATED;
char *git_root_of (git_t *git, void *edit);
void git_close_commit (git_t *git);
void git_forget_log (git_t *git);
void git_read_status (git_t *git);
void git_read_log (git_t *git);
gboolean git_read_log_more (git_t *git);
void git_open_commit (git_t *git, git_commit_t *c);
void git_help (void);
void git_quit (git_t *git);
git_commit_t *git_find_commit (git_t *git, const char *name, gboolean *own);

/* git-window.c */
gboolean git_do_args (git_t *git, const char *title, const char *const *args);
git_commit_t *git_list_commit (const git_window_t *win);
void git_cursor_show (git_window_t *win, int list);
void git_group_draw (git_window_t *win);
void git_diff_update (git_window_t *win);
void git_window_reload (git_window_t *win);
gboolean git_pane_close (WEditWindow *ew);
void git_window_reread (git_window_t *win);
void git_group_arrange (git_window_t *win);
void git_group_show (git_window_t *win);
void git_window_open (git_t *git, void *edit, int tab);
void git_group_show_later (void *data);

/* git-graph.c */
void git_read_graph (git_t *git);
int git_graph_commit_of_row (const git_t *git, int row);
void git_graph_cursor_skip (git_window_t *win, int dir);
void git_graph_colors (git_t *git);
void git_graph_glyphs (git_t *git);
void git_draw_graph_row (git_window_t *win, Widget *w, int y, int x, int cols, int index,
                         int highlight);
void git_graph_reread (git_window_t *win, int limit);
void git_graph_choose_refs (git_window_t *win);

/* git-remote.c */
char *git_config_value (const char *root, const char *name);
void git_terminal_finished (const char *name, const char *signal, GVariant *args, void *user_data);
void git_remote (git_window_t *win, const char *what);

/* git-message.c */
void git_message_forget (git_t *git);
gboolean git_conflicts (const char *root, const char *title);
gboolean git_message_set_aside (git_t *git);
void git_message_draft (git_t *git);
void git_message_draft_later (void *data);
void git_message_start (git_t *git, git_msg_mode_t mode, const git_commit_t *commit);
void git_commit (git_t *git, gboolean amend);
void git_close_later (void *data);
void git_message_saved (git_t *git, void *edit);
gboolean git_is_message_file (const git_t *git, void *edit);

/* git-menu.c */
gboolean git_is_any_message_file (const git_t *git, void *edit);
void git_message_menu (git_t *git, WEdit *edit);

/*** inline functions ****************************************************************************/

#endif /* MC__GIT_PRIVATE_H */
