/** \file
 *  \brief Header: editor widget WEdit
 */

#ifndef MC__EDIT_WIDGET_H
#define MC__EDIT_WIDGET_H

#include <limits.h>  // MB_LEN_MAX

#include "lib/search.h"  // mc_search_t
#include "lib/widget.h"  // Widget

#include "src/syntax/syntax.h"

#include "edit-impl.h"
#include "editbuffer.h"
#include "editwindow.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define N_LINE_CACHES 32

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct edit_book_mark_t edit_book_mark_t;
struct edit_book_mark_t
{
    long line;  // line number
    int c;      // color
    edit_book_mark_t *next;
    edit_book_mark_t *prev;
};

typedef struct edit_fold_t edit_fold_t;
struct edit_fold_t
{
    long line_start;  // first line of folded region
    long line_count;  // number of hidden lines below line_start
    edit_fold_t *next;
    edit_fold_t *prev;
};

struct WEdit
{
    WEditWindow window;

    char *filename;  // Absolute name of the file, NULL for a new one

    // dynamic buffers and cursor position for editor:
    edit_buffer_t buffer;

    // multibyte support
    gboolean utf8;  // It's multibyte file codeset
    GIConv converter;
    char charbuf[MB_LEN_MAX + 1];
    int charpoint;

    // search handler
    mc_search_t *search;
    int replace_mode;
    // whether search conditions should be started with BOL(^) or ended with EOL($)
    mc_search_line_t search_line_type;

    char *last_search_string;  // String that have been searched
    off_t search_start;        // First character to start searching from
    unsigned long found_len;   // Length of found string or 0 if none was found
    off_t found_start;         // the found word from a search - start position

    // display information
    long start_display;             // First char displayed
    long start_col;                 // First displayed column, negative
    gboolean view_free;             // scrolled sideways away from the cursor, till the next command
    long curs_row;                  // row position of cursor on the screen
    long curs_col;                  // column position on screen
    long over_col;                  // pos after '\n'
    off_t curs_bol;                 // beginning of the line containing the cursor
    off_t curs_eol;                 // end (newline or EOF) of the line containing the cursor
    off_t curs_indent_end;          // first non-blank byte on the cursor's line (or its eol)
    off_t curs_indent_end_bol;      // bol that curs_indent_end was computed for
    GPtrArray *line_layout_caches;  // recently used byte offset to visual column indexes
    guint64 line_layout_cache_serial;
    guint64 runtime_revision;  // public runtime ABI document revision
    // what the runtime was told last: the revision of editor.change, the line of editor.cursor
    guint64 runtime_told_revision;
    long runtime_told_line;
    guint runtime_edit_depth;  // coalesces runtime mutations into one public revision
    gboolean runtime_edit_changed;
    unsigned int curs_bol_valid : 1;
    unsigned int curs_eol_valid : 1;
    unsigned int curs_indent_end_valid : 1;
    int force;                      // how much of the screen do we redraw?
    unsigned int overwrite : 1;     // Overwrite on type mode (as opposed to insert)
    unsigned int modified : 1;      // File has been modified and needs saving
    unsigned int closed_told : 1;   // the plugins know the window goes
    unsigned int loading_done : 1;  // File has been loaded into the editor
    unsigned int locked : 1;        // We hold lock on current file
    unsigned int delete_file : 1;   // New file, needs to be deleted unless modified
    unsigned int highlight : 1;     // There is a selected block
    unsigned int column_highlight : 1;
    unsigned int word_highlight : 1;  // Highlight each word the cursor crosses
    unsigned int line_highlight : 1;  // Highlight each line the cursor crosses
    long prev_col;                    /* recent column position of the cursor - used when moving
                                         up or down past lines that are shorter than the current line */
    long start_line;                  // line number of the top of the page

    // file info
    off_t mark1;          // position of highlight start
    off_t mark2;          // position of highlight end
    off_t end_mark_curs;  // position of cursor after end of highlighting
    long column1;         // position of column highlight start
    long column2;         // position of column highlight end
    off_t bracket;        // position of a matching bracket
    off_t last_bracket;   // previous position of a matching bracket

    // cache speedup for line lookups
    gboolean caches_valid;
    long line_numbers[N_LINE_CACHES];
    off_t line_offsets[N_LINE_CACHES];

    edit_book_mark_t *book_mark;
    GArray *serialized_bookmarks;
    // the notes of the plugins after the text of a line: line (from 0) to text
    GHashTable *line_notes;

    // code folding
    edit_fold_t *folds;
    // folds were made by the line filter: no fold indicator, edits keep new lines visible
    gboolean filter_active;

    // undo stack and pointers
    unsigned long undo_stack_pointer;
    long *undo_stack;
    unsigned long undo_stack_size;
    unsigned long undo_stack_size_mask;
    unsigned long undo_stack_bottom;
    long undo_content_seq;       /* counts content-changing ops pushed minus popped */
    long undo_content_saved;     /* undo_content_seq value at last load or save */
    long undo_content_gen;       /* increments each time edit history branches after undo */
    long undo_content_saved_gen; /* undo_content_gen value at last load or save */
    unsigned int undo_stack_disable : 1;  // If not 0, don't save events in the undo stack
    // an undo has put the mark of its key press on the redo stack: what it undoes is one redo
    unsigned int redo_key_pushed : 1;

    unsigned long redo_stack_pointer;
    long *redo_stack;
    unsigned long redo_stack_size;
    unsigned long redo_stack_size_mask;
    unsigned long redo_stack_bottom;
    gboolean redo_has_content;         /* redo stack has at least one content-changing op */
    unsigned int redo_stack_reset : 1; /* If 1, need clear redo stack */

    struct stat stat1;  // Result of fstat() on the file

    unsigned int skip_detach_prompt : 1;  // Do not prompt whether to detach a file anymore

    // syntax highlighting
    syntax_scanner_t *syntax;   // NULL when this file has none
    syntax_palette_t *palette;  // its colors as this skin draws them

    // line break
    LineBreaks lb;
};

/*** global variables defined in .c file *********************************************************/

extern const edit_window_class_t edit_class;

/*** declarations of public functions ************************************************************/

/*** inline functions ****************************************************************************/
#endif
