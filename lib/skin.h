#ifndef MC_SKIN_H
#define MC_SKIN_H

#include "lib/global.h"

#include "lib/mcconfig.h"

#include "lib/tty/color.h"
#include "lib/tty/tty.h"  // mc_tty_char_t

/*** typedefs(not structures) and defined constants **********************************************/

/* Beware! When using Slang with color, not all the indexes are free.
   See color-slang.h (A_*) */

/* cache often used colors */
#define CORE_DEFAULT_COLOR  mc_skin_color__cache[0]
#define CORE_NORMAL_COLOR   mc_skin_color__cache[1]
#define CORE_DISABLED_COLOR mc_skin_color__cache[2]
#define CORE_REVERSE_COLOR  mc_skin_color__cache[3]
#define CORE_SHADOW_COLOR   mc_skin_color__cache[4]
#define CORE_FRAME_COLOR    mc_skin_color__cache[5]

/* Dialog colors */
#define DIALOG_NORMAL_COLOR          mc_skin_color__cache[6]
#define DIALOG_FOCUS_COLOR           mc_skin_color__cache[7]
#define DIALOG_HOT_NORMAL_COLOR      mc_skin_color__cache[8]
#define DIALOG_HOT_FOCUS_COLOR       mc_skin_color__cache[9]
#define DIALOG_SELECTED_NORMAL_COLOR mc_skin_color__cache[10]
#define DIALOG_SELECTED_FOCUS_COLOR  mc_skin_color__cache[11]
#define DIALOG_TITLE_COLOR           mc_skin_color__cache[12]
#define DIALOG_FRAME_COLOR           mc_skin_color__cache[13]

/* Error dialog colors */
#define ERROR_NORMAL_COLOR     mc_skin_color__cache[14]
#define ERROR_FOCUS_COLOR      mc_skin_color__cache[15]
#define ERROR_HOT_NORMAL_COLOR mc_skin_color__cache[16]
#define ERROR_HOT_FOCUS_COLOR  mc_skin_color__cache[17]
#define ERROR_TITLE_COLOR      mc_skin_color__cache[18]
#define ERROR_FRAME_COLOR      mc_skin_color__cache[19]

/* Menu colors */
#define MENU_ENTRY_COLOR    mc_skin_color__cache[20]
#define MENU_SELECTED_COLOR mc_skin_color__cache[21]
#define MENU_HOT_COLOR      mc_skin_color__cache[22]
#define MENU_HOTSEL_COLOR   mc_skin_color__cache[23]
#define MENU_INACTIVE_COLOR mc_skin_color__cache[24]
#define MENU_FRAME_COLOR    mc_skin_color__cache[25]

/* Popup menu colors */
#define PMENU_ENTRY_COLOR      mc_skin_color__cache[26]
#define PMENU_SELECTED_COLOR   mc_skin_color__cache[27]
#define PMENU_HOT_COLOR        mc_skin_color__cache[28]  // unused: not implemented yet
#define PMENU_HOTSEL_COLOR     mc_skin_color__cache[29]  // unused: not implemented yet
#define PMENU_TITLE_COLOR      mc_skin_color__cache[30]
#define PMENU_FRAME_COLOR      mc_skin_color__cache[31]

#define BUTTONBAR_HOTKEY_COLOR mc_skin_color__cache[32]
#define BUTTONBAR_BUTTON_COLOR mc_skin_color__cache[33]

#define STATUSBAR_COLOR        mc_skin_color__cache[34]

/*
 * This should be selectable independently. Default has to be black background
 * foreground does not matter at all.
 */
#define CORE_GAUGE_COLOR           mc_skin_color__cache[35]
#define CORE_INPUT_COLOR           mc_skin_color__cache[36]
#define CORE_INPUT_UNCHANGED_COLOR mc_skin_color__cache[37]
#define CORE_INPUT_MARK_COLOR      mc_skin_color__cache[38]
#define CORE_INPUT_HISTORY_COLOR   mc_skin_color__cache[39]

#define HELP_NORMAL_COLOR          mc_skin_color__cache[40]
#define HELP_ITALIC_COLOR          mc_skin_color__cache[41]
#define HELP_BOLD_COLOR            mc_skin_color__cache[42]
#define HELP_LINK_COLOR            mc_skin_color__cache[43]
#define HELP_SLINK_COLOR           mc_skin_color__cache[44]
#define HELP_TITLE_COLOR           mc_skin_color__cache[45]
#define HELP_FRAME_COLOR           mc_skin_color__cache[46]

/*
 * editor colors - only 4 for normal, search->found, select, and whitespace
 * respectively
 * Last is defined to view color.
 */
#define EDITOR_NORMAL_COLOR       mc_skin_color__cache[47]
#define EDITOR_NONPRINTABLE_COLOR mc_skin_color__cache[48]
#define EDITOR_BOLD_COLOR         mc_skin_color__cache[49]
#define EDITOR_MARKED_COLOR       mc_skin_color__cache[50]
#define EDITOR_WHITESPACE_COLOR   mc_skin_color__cache[51]
#define EDITOR_RIGHT_MARGIN_COLOR mc_skin_color__cache[52]
#define EDITOR_BACKGROUND_COLOR   mc_skin_color__cache[53]
#define EDITOR_FRAME_COLOR        mc_skin_color__cache[54]
#define EDITOR_FRAME_ACTIVE_COLOR mc_skin_color__cache[55]
#define EDITOR_FRAME_DRAG_COLOR   mc_skin_color__cache[56]
/* color of left 8 char status per line */
#define EDITOR_LINE_STATE_COLOR     mc_skin_color__cache[57]
#define EDITOR_BOOKMARK_COLOR       mc_skin_color__cache[58]
#define EDITOR_BOOKMARK_FOUND_COLOR mc_skin_color__cache[59]

/* The terminal */
#define MCTERM_NORMAL_COLOR       mc_skin_color__cache[60]
#define MCTERM_SELECTED_COLOR     mc_skin_color__cache[61]

#define MC_SKIN_COLOR_CACHE_COUNT 62

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* How a scrollbar is drawn: the arrows at its start and its end, the fill of its track, its
   thumb, and the cells of the thumb when it fits; [scrollbar] of the skin */
typedef struct
{
    mc_tty_char_t start;
    mc_tty_char_t end;
    mc_tty_char_t track;
    mc_tty_char_t thumb;
    int thumb_len;
} mc_skin_scrollbar_t;

typedef struct mc_skin_struct
{
    gchar *name;
    gchar *description;
    mc_config_t *config;
    GHashTable *colors;
    gboolean have_256_colors;
    gboolean have_true_colors;
} mc_skin_t;

/*** global variables defined in .c file *********************************************************/

extern int mc_skin_color__cache[];
extern mc_skin_t mc_skin__default;
/* the scrollbars of the skin, the vertical one and the horizontal one */
extern mc_skin_scrollbar_t mc_skin_scrollbar_vert;
extern mc_skin_scrollbar_t mc_skin_scrollbar_horiz;

/*** declarations of public functions ************************************************************/

gboolean mc_skin_init (const gchar *skin_override, GError **error);
/* replace the loaded skin by @config, the engine's from here on; @name NULL keeps the current */
gboolean mc_skin_load_from_config (mc_config_t *config, const char *name, GError **error);

void mc_skin_deinit (void);

int mc_skin_color_get (const gchar *group, const gchar *name);
gboolean mc_skin_color_is_set (const gchar *group, const gchar *name);

void mc_skin_lines_parse_ini_file (mc_skin_t *mc_skin);

gchar *mc_skin_get (const gchar *group, const gchar *key, const gchar *default_value);

const char *mc_skin_spinner_frame (unsigned int index);

GPtrArray *mc_skin_list (void);

#endif
