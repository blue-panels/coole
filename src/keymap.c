/*
   Default values and initialization of keybinding engine

   Copyright (C) 2009-2025
   Free Software Foundation, Inc.
   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Vitja Makarov, 2005
   Ilia Maslakov <il.smind@gmail.com>, 2009, 2010
   Andrew Borodin <aborodin@vmail.ru>, 2010-2021
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole,
   a text editor based on GNU Midnight Commander.

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

#include <config.h>

#include "lib/global.h"

#include "lib/fileloc.h"
#include "lib/keybind.h"
#include "lib/mcconfig.h"  // mc_config_t
#include "lib/util.h"
#include "lib/widget.h"  // dialog_map, input_map, listbox_map, menu_map, radio_map

#include "args.h"  // mc_args__keymap_file

#include "keymap.h"

/*** global variables ****************************************************************************/

GArray *dialog_keymap = NULL;
GArray *menu_keymap = NULL;
GArray *input_keymap = NULL;
GArray *listbox_keymap = NULL;
GArray *radio_keymap = NULL;
GArray *help_keymap = NULL;
GArray *editor_keymap = NULL;
GArray *editor_x_keymap = NULL;
GArray *mcterm_keymap = NULL;

const global_keymap_t *help_map = NULL;
const global_keymap_t *editor_map = NULL;
const global_keymap_t *editor_x_map = NULL;
const global_keymap_t *mcterm_map = NULL;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/* default keymaps in ini (key=value) format */
typedef struct global_keymap_ini_t
{
    const char *key;
    const char *value;
} global_keymap_ini_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* dialog */
static const global_keymap_ini_t default_dialog_keymap[] = {
    { "Ok", "enter" },
    { "Cancel", "f10; esc; ctrl-g" },
    { "Up", "up; left" },
    { "Down", "down; right" },
#if 0
    {"Left", "up; left"},
    {"Right", "down; right"},
#endif
    { "Help", "f1" },
    { "Suspend", "ctrl-z" },
    { "Refresh", "ctrl-l" },
    { "ScreenList", "alt-prime" },
    { "ScreenNext", "alt-rbrace" },
    { "ScreenPrev", "alt-lbrace" },
    {
        NULL,
        NULL,
    },
};

/* menubar */
static const global_keymap_ini_t default_menu_keymap[] = {
    { "Help", "f1" },
    { "Left", "left; ctrl-b" },
    { "Right", "right; ctrl-f" },
    { "Up", "up; ctrl-p" },
    { "Down", "down; ctrl-n" },
    { "Home", "home; alt-lt; ctrl-a" },
    { "End", "end; alt-gt; ctrl-e" },
    { "Enter", "enter" },
    { "Quit", "f10; ctrl-g; esc" },
    {
        NULL,
        NULL,
    },
};

/* input line */
static const global_keymap_ini_t default_input_keymap[] = {
    // Motion
    { "Home", "ctrl-a; alt-lt; home; a1" },
    { "End", "ctrl-e; alt-gt; end; c1" },
    { "Left", "left; alt-left; ctrl-b" },
    { "Right", "right; alt-right; ctrl-f" },
    { "WordLeft", "ctrl-left; alt-b" },
    { "WordRight", "ctrl-right; alt-f" },
    // Mark
    { "MarkLeft", "shift-left" },
    { "MarkRight", "shift-right" },
    { "MarkToWordBegin", "ctrl-shift-left" },
    { "MarkToWordEnd", "ctrl-shift-right" },
    { "MarkToHome", "shift-home" },
    { "MarkToEnd", "shift-end" },
    // Editing
    { "Backspace", "backspace; ctrl-h" },
    { "Delete", "delete; ctrl-d" },
    { "DeleteToWordEnd", "alt-d" },
    { "DeleteToWordBegin", "alt-backspace" },
    // Region manipulation
    { "Remove", "ctrl-w" },
    { "Store", "alt-w; ctrl-insert" },
    { "Cut", "shift-delete" },
    { "Paste", "shift-insert" },
    { "Yank", "ctrl-y" },
    { "DeleteToEnd", "ctrl-k" },
    // History
    { "History", "alt-h" },
    { "HistoryPrev", "alt-p; ctrl-down" },
    { "HistoryNext", "alt-n; ctrl-up" },
    // Completion
    { "Complete", "alt-tab" },
    {
        NULL,
        NULL,
    },
};

/* listbox */
static const global_keymap_ini_t default_listbox_keymap[] = {
    { "Up", "up; ctrl-p" },
    { "Down", "down; ctrl-n" },
    { "Top", "home; alt-lt; a1" },
    { "Bottom", "end; alt-gt; c1" },
    { "PageUp", "pgup; alt-v" },
    { "PageDown", "pgdn; ctrl-v" },
    { "Delete", "delete; d" },
    { "Clear", "shift-delete; shift-d" },
    { "Edit", "f4" },
    { "Enter", "enter" },
    { "Search", "ctrl-s; alt-s" },
    {
        NULL,
        NULL,
    },
};

/* radio */
static const global_keymap_ini_t default_radio_keymap[] = {
    { "Up", "up; ctrl-p" },
    { "Down", "down; ctrl-n" },
    { "Top", "home; alt-lt; a1" },
    { "Bottom", "end; alt-gt; c1" },
    { "Select", "space" },
    {
        NULL,
        NULL,
    },
};

/* help */
static const global_keymap_ini_t default_help_keymap[] = {
    { "Help", "f1" },
    { "Index", "f2; c" },
    { "Back", "f3; left; l" },
    { "Quit", "f10; esc" },
    { "Up", "up; ctrl-p" },
    { "Down", "down; ctrl-n" },
    { "PageDown", "f; space; pgdn; ctrl-v" },
    { "PageUp", "b; pgup; alt-v; backspace" },
    { "HalfPageDown", "d" },
    { "HalfPageUp", "u" },
    { "Top", "home; ctrl-home; ctrl-pgup; a1; alt-lt; g" },
    { "Bottom", "end; ctrl-end; ctrl-pgdn; c1; alt-gt; shift-g" },
    { "Enter", "right; enter" },
    { "LinkNext", "tab" },
    { "LinkPrev", "alt-tab" },
    { "NodeNext", "n" },
    { "NodePrev", "p" },
    {
        NULL,
        NULL,
    },
};

/* editor */
static const global_keymap_ini_t default_editor_keymap[] = {
    { "Enter", "enter" },
    { "Return", "shift-enter; ctrl-enter; ctrl-shift-enter" },  // useful for pasting multiline text
    { "Tab", "tab; shift-tab; ctrl-tab; ctrl-shift-tab" },      // ditto
    { "BackSpace", "backspace; ctrl-h" },
    { "Delete", "delete; ctrl-d" },
    { "Left", "left" },
    { "Right", "right" },
    { "Up", "up" },
    { "Down", "down" },
    // a tab stop at a time, since there are no words to speak of in output
    { "WordLeft", "ctrl-left" },
    { "WordRight", "ctrl-right" },
    { "Home", "home" },
    { "End", "end" },
    { "Home", "home" },
    { "End", "end" },
    { "PageUp", "pgup" },
    { "PageDown", "pgdn" },
    { "WordLeft", "ctrl-left; ctrl-z" },
    { "WordRight", "ctrl-right; ctrl-x" },
    { "InsertOverwrite", "insert" },
    { "Help", "f1" },
    { "Save", "f2" },
    { "Mark", "f3" },
    { "Replace", "f4" },
    { "Copy", "f5" },
    { "Move", "f6" },
    { "Search", "f7" },
    { "Remove", "f8; ctrl-delete" },
    { "Menu", "f9" },
    { "Quit", "f10; esc" },
    { "UserMenu", "f11" },
    { "SaveAs", "f12; ctrl-f2" },
    { "MarkColumn", "f13" },
    { "ReplaceContinue", "f14; ctrl-f4" },
    { "InsertFile", "f15" },
    { "SearchContinue", "f17; ctrl-f7" },
    { "EditNew", "ctrl-n" },
    { "DeleteToWordBegin", "alt-backspace" },
    { "DeleteToWordEnd", "alt-d" },
    { "DeleteLine", "ctrl-y" },
    { "DeleteToEnd", "ctrl-k" },
    { "Undo", "ctrl-u; ctrl-backspace" },
    { "Redo", "alt-r" },
    { "UndoHistory", "alt-shift-u" },
    { "SelectCodepage", "alt-e" },
    { "Goto", "alt-l; alt-shift-l" },
    { "Refresh", "ctrl-l" },
    { "Shell", "ctrl-o" },
    { "Top", "ctrl-home; ctrl-pgup; alt-lt" },
    { "Bottom", "ctrl-end; ctrl-pgdn; alt-gt" },
    { "TopOnScreen", "ctrl-pgup" },
    { "BottomOnScreen", "ctrl-pgdn" },
    { "ScrollUp", "ctrl-up" },
    { "ScrollDown", "ctrl-down" },
    { "Store", "ctrl-insert" },
    { "Paste", "shift-insert" },
    { "Cut", "shift-delete" },
    { "BlockSave", "ctrl-f" },
    { "MarkLeft", "shift-left" },
    { "MarkRight", "shift-right" },
    { "MarkUp", "shift-up" },
    { "MarkDown", "shift-down" },
    { "MarkPageUp", "shift-pgup" },
    { "MarkPageDown", "shift-pgdn" },
    { "MarkToWordBegin", "ctrl-shift-left" },
    { "MarkToWordEnd", "ctrl-shift-right" },
    { "MarkToHome", "shift-home" },
    { "MarkToEnd", "shift-end" },
    { "MarkToFileBegin", "ctrl-shift-home" },
    { "MarkToFileEnd", "ctrl-shift-end" },
    { "MarkToPageBegin", "ctrl-shift-pgup" },
    { "MarkToPageEnd", "ctrl-shift-pgdn" },
    { "MarkScrollUp", "ctrl-shift-up" },
    { "MarkScrollDown", "ctrl-shift-down" },
    { "MarkColumnLeft", "alt-left" },
    { "MarkColumnRight", "alt-right" },
    { "MarkColumnUp", "alt-up" },
    { "MarkColumnDown", "alt-down" },
    { "MarkColumnPageUp", "alt-pgup" },
    { "MarkColumnPageDown", "alt-pgdn" },
    { "Complete", "alt-tab" },
    { "MatchBracket", "alt-b" },
    { "Bookmark", "alt-k" },
    { "BookmarkFlush", "alt-o" },
    { "BookmarkNext", "alt-j" },
    { "BookmarkPrev", "alt-i" },
    { "MacroStartStopRecord", "ctrl-r" },
    { "MacroExecute", "ctrl-a" },
    { "ShowNumbers", "alt-n" },
    { "ShowTabTws", "alt-underline" },
    { "SyntaxOnOff", "ctrl-s" },
    { "Find", "alt-enter" },
    { "FilePrev", "alt-minus" },
    { "FileNext", "alt-plus" },
    { "FoldToggle", "alt-shift-f" },
    { "UnfoldAll", "alt-shift-u" },
    { "FilterToggle", "alt-s" },
    { "FilterWord", "alt-shift-s" },
    { "ExtendedKeyMap", "ctrl-x" },
    {
        NULL,
        NULL,
    },
};

/* emacs keyboard layout emulation */
static const global_keymap_ini_t default_editor_x_keymap[] = {
    { NULL, NULL },
};

/* the terminal: what is bound here is taken from the shell */
static const global_keymap_ini_t default_mcterm_keymap[] = {
    // marking the output, and taking it out
    { "Store", "ctrl-insert; enter; f2" },
    { "MarkAll", "f3" },
    { "Unmark", "ctrl-shift-u" },
    { "MarkLeft", "shift-left" },
    { "MarkRight", "shift-right" },
    { "MarkUp", "shift-up" },
    { "MarkDown", "shift-down" },
    { "MarkPageUp", "shift-pgup" },
    { "MarkPageDown", "shift-pgdn" },
    { "MarkToHome", "shift-home" },
    { "MarkToEnd", "shift-end" },
    // the cursor over the output, while the terminal holds the focus
    { "Left", "left" },
    { "Right", "right" },
    { "Up", "up" },
    { "Down", "down" },
    // the view alone, which moves whoever is typing
    { "ScrollUp", "ctrl-up" },
    { "ScrollDown", "ctrl-down" },
    { "PageUp", "pgup" },
    { "PageDown", "pgdn" },
    { "Top", "ctrl-home" },
    { "Bottom", "ctrl-end" },
    { "Clear", "ctrl-l" },
    { "ClearAll", "ctrl-shift-l; ctrl-alt-l; f6" },
    // the output cut down to the rows that match
    { "FilterWord", "f4" },
    { "FilterToggle", "f5" },
    // a pattern typed: the output searched, or cut down
    { "Search", "alt-s" },
    { "QuickFilter", "alt-shift-s" },
    { NULL, NULL },
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
create_default_keymap_section (mc_config_t *keymap, const char *section,
                               const global_keymap_ini_t *k)
{
    size_t i;

    for (i = 0; k[i].key != NULL; i++)
        mc_config_set_string_raw (keymap, section, k[i].key, k[i].value);
}

/* --------------------------------------------------------------------------------------------- */

static mc_config_t *
create_default_keymap (void)
{
    mc_config_t *keymap;

    keymap = mc_config_init (NULL, TRUE);

    create_default_keymap_section (keymap, KEYMAP_SECTION_DIALOG, default_dialog_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_MENU, default_menu_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_INPUT, default_input_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_LISTBOX, default_listbox_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_RADIO, default_radio_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_HELP, default_help_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_EDITOR, default_editor_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_EDITOR_EXT, default_editor_x_keymap);
    create_default_keymap_section (keymap, KEYMAP_SECTION_MCTERM, default_mcterm_keymap);

    return keymap;
}

/* --------------------------------------------------------------------------------------------- */

static void
load_keymap_from_section (const char *section_name, GArray *keymap, mc_config_t *cfg)
{
    gchar **profile_keys, **keys;

    if (section_name == NULL)
        return;

    keys = mc_config_get_keys (cfg, section_name, NULL);

    for (profile_keys = keys; *profile_keys != NULL; profile_keys++)
    {
        gchar **values;

        values = mc_config_get_string_list (cfg, section_name, *profile_keys, NULL);
        if (values != NULL)
        {
            long action;

            action = keybind_lookup_action (*profile_keys);
            if (action > 0)
            {
                gchar **curr_values;

                for (curr_values = values; *curr_values != NULL; curr_values++)
                    keybind_cmd_bind (keymap, *curr_values, action);
            }

            g_strfreev (values);
        }
    }

    g_strfreev (keys);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get name of config file.
 *
 * @param subdir If not NULL, config is also searched in specified subdir.
 * @param config_file_name If relative, file if searched in standard paths.
 *
 * @return newly allocated string with config name or NULL if file is not found.
 */

static char *
load_setup_get_full_config_name (const char *subdir, const char *config_file_name)
{
    /*
       TODO: IMHO, in future, this function shall be placed in mcconfig module.
     */
    char *lc_basename, *ret;
    char *file_name;

    if (config_file_name == NULL)
        return NULL;

    // check for .keymap suffix
    if (g_str_has_suffix (config_file_name, ".keymap"))
        file_name = g_strdup (config_file_name);
    else
        file_name = g_strconcat (config_file_name, ".keymap", (char *) NULL);

    canonicalize_pathname (file_name);

    if (g_path_is_absolute (file_name))
        return file_name;

    lc_basename = g_path_get_basename (file_name);
    g_free (file_name);

    if (lc_basename == NULL)
        return NULL;

    if (subdir != NULL)
        ret = g_build_filename (mc_config_get_path (), subdir, lc_basename, (char *) NULL);
    else
        ret = g_build_filename (mc_config_get_path (), lc_basename, (char *) NULL);

    if (exist_file (ret))
    {
        g_free (lc_basename);
        canonicalize_pathname (ret);
        return ret;
    }
    g_free (ret);

    if (subdir != NULL)
        ret = g_build_filename (mc_global.share_data_dir, subdir, lc_basename, (char *) NULL);
    else
        ret = g_build_filename (mc_global.share_data_dir, lc_basename, (char *) NULL);

    g_free (lc_basename);

    if (exist_file (ret))
    {
        canonicalize_pathname (ret);
        return ret;
    }

    g_free (ret);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/**
  Create new mc_config object from specified ini-file or
  append data to existing mc_config object from ini-file
*/

static void
load_setup_init_config_from_file (mc_config_t **config, const char *fname, gboolean read_only)
{
    /*
       TODO: IMHO, in future, this function shall be placed in mcconfig module.
     */
    if (exist_file (fname))
    {
        if (*config != NULL)
            mc_config_read_file (*config, fname, read_only, TRUE);
        else
            *config = mc_config_init (fname, read_only);
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_config_t *
load_setup_get_keymap_profile_config (gboolean load_from_file)
{
    /*
       TODO: IMHO, in future, this function shall be placed in mcconfig module.
     */
    mc_config_t *keymap_config;
    char *share_keymap, *sysconfig_keymap;
    char *fname, *fname2;

    // 0) Create default keymap
    keymap_config = create_default_keymap ();
    if (!load_from_file)
        return keymap_config;

    // load and merge global keymaps

    // 1) /usr/share/coole (mc_global.share_data_dir)
    share_keymap = g_build_filename (mc_global.share_data_dir, GLOBAL_KEYMAP_FILE, (char *) NULL);
    load_setup_init_config_from_file (&keymap_config, share_keymap, TRUE);

    // 2) /etc/coole (mc_global.sysconfig_dir)
    sysconfig_keymap =
        g_build_filename (mc_global.sysconfig_dir, GLOBAL_KEYMAP_FILE, (char *) NULL);
    load_setup_init_config_from_file (&keymap_config, sysconfig_keymap, TRUE);

    // then load and merge one of user-defined keymap

    // 3) --keymap=<keymap>
    fname = load_setup_get_full_config_name (NULL, mc_args__keymap_file);
    if (fname != NULL && strcmp (fname, sysconfig_keymap) != 0 && strcmp (fname, share_keymap) != 0)
    {
        load_setup_init_config_from_file (&keymap_config, fname, TRUE);
        goto done;
    }
    g_free (fname);

    // 4) getenv("COOLE_KEYMAP")
    fname = load_setup_get_full_config_name (NULL, g_getenv ("COOLE_KEYMAP"));
    if (fname != NULL && strcmp (fname, sysconfig_keymap) != 0 && strcmp (fname, share_keymap) != 0)
    {
        load_setup_init_config_from_file (&keymap_config, fname, TRUE);
        goto done;
    }

    MC_PTR_FREE (fname);

    // 5) main config; [coole] -> keymap
    fname2 = mc_config_get_string (mc_global.main_config, CONFIG_APP_SECTION, "keymap", NULL);
    if (fname2 != NULL && *fname2 != '\0')
        fname = load_setup_get_full_config_name (NULL, fname2);
    g_free (fname2);
    if (fname != NULL && strcmp (fname, sysconfig_keymap) != 0 && strcmp (fname, share_keymap) != 0)
    {
        load_setup_init_config_from_file (&keymap_config, fname, TRUE);
        goto done;
    }
    g_free (fname);

    // 6) ${XDG_CONFIG_HOME}/coole/coole.keymap
    fname = mc_config_get_full_path (GLOBAL_KEYMAP_FILE);
    load_setup_init_config_from_file (&keymap_config, fname, TRUE);

done:
    g_free (fname);
    g_free (sysconfig_keymap);
    g_free (share_keymap);

    return keymap_config;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
keymap_load (gboolean load_from_file)
{
    /*
     * Load keymap from GLOBAL_KEYMAP_FILE before ${XDG_CONFIG_HOME}/coole/coole.keymap, so that the
     * user definitions override global settings.
     */
    mc_config_t *mc_global_keymap;

    mc_global_keymap = load_setup_get_keymap_profile_config (load_from_file);

    if (mc_global_keymap != NULL)
    {
#define LOAD_KEYMAP(s, km)                                                                         \
    km##_keymap = g_array_new (TRUE, FALSE, sizeof (global_keymap_t));                             \
    load_keymap_from_section (KEYMAP_SECTION_##s, km##_keymap, mc_global_keymap)

        LOAD_KEYMAP (DIALOG, dialog);
        LOAD_KEYMAP (MENU, menu);
        LOAD_KEYMAP (INPUT, input);
        LOAD_KEYMAP (LISTBOX, listbox);
        LOAD_KEYMAP (RADIO, radio);
        LOAD_KEYMAP (HELP, help);
        LOAD_KEYMAP (EDITOR, editor);
        LOAD_KEYMAP (EDITOR_EXT, editor_x);
        LOAD_KEYMAP (MCTERM, mcterm);

#undef LOAD_KEYMAP
        mc_config_deinit (mc_global_keymap);
    }

#define SET_MAP(m) m##_map = (global_keymap_t *) m##_keymap->data

    SET_MAP (dialog);
    SET_MAP (menu);
    SET_MAP (input);
    SET_MAP (listbox);
    SET_MAP (radio);
    SET_MAP (help);
    SET_MAP (editor);
    SET_MAP (editor_x);
    SET_MAP (mcterm);

#undef SET_MAP
}

/* --------------------------------------------------------------------------------------------- */

void
keymap_free (void)
{
#define FREE_KEYMAP(km)                                                                            \
    if (km##_keymap != NULL)                                                                       \
        g_array_free (km##_keymap, TRUE);                                                          \
    km##_keymap = NULL

    FREE_KEYMAP (dialog);
    FREE_KEYMAP (menu);
    FREE_KEYMAP (input);
    FREE_KEYMAP (listbox);
    FREE_KEYMAP (radio);
    FREE_KEYMAP (help);
    FREE_KEYMAP (editor);
    FREE_KEYMAP (editor_x);
    FREE_KEYMAP (mcterm);

#undef FREE_KEYMAP
}

/* --------------------------------------------------------------------------------------------- */

/* old -> new map pairs for refresh */
typedef struct
{
    const global_keymap_t *old_map;
    const global_keymap_t **new_map;
} keymap_pair_t;

static keymap_pair_t keymap_old_maps[10];
static int keymap_old_count = 0;

void
keymap_save_old_maps (void)
{
    keymap_old_count = 0;

#define SAVE_MAP(m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (m##_map != NULL)                                                                       \
        {                                                                                          \
            keymap_old_maps[keymap_old_count].old_map = m##_map;                                   \
            keymap_old_maps[keymap_old_count].new_map = (const global_keymap_t **) &m##_map;       \
            keymap_old_count++;                                                                    \
        }                                                                                          \
    }                                                                                              \
    while (0)

    SAVE_MAP (dialog);
    SAVE_MAP (input);
    SAVE_MAP (listbox);
    SAVE_MAP (menu);
    SAVE_MAP (radio);
    SAVE_MAP (help);
    SAVE_MAP (editor);
    SAVE_MAP (editor_x);
    SAVE_MAP (mcterm);
#undef SAVE_MAP
}

/* --------------------------------------------------------------------------------------------- */

static const global_keymap_t *
keymap_refresh_map (const global_keymap_t *map)
{
    int i;

    for (i = 0; i < keymap_old_count; i++)
        if (map == keymap_old_maps[i].old_map)
            return *keymap_old_maps[i].new_map;

    return map;
}

/* --------------------------------------------------------------------------------------------- */

static void
keymap_refresh_widget (Widget *w)
{
    if (w == NULL)
        return;

    if (w->keymap != NULL)
        w->keymap = keymap_refresh_map (w->keymap);
    if (w->ext_keymap != NULL)
        w->ext_keymap = keymap_refresh_map (w->ext_keymap);
}

/* --------------------------------------------------------------------------------------------- */

void
keymap_refresh_widgets (void)
{
    GList *d;

    for (d = top_dlg; d != NULL; d = g_list_next (d))
    {
        WDialog *dlg = DIALOG (d->data);
        WGroup *g = GROUP (dlg);
        GList *l;

        /* refresh the dialog itself */
        keymap_refresh_widget (WIDGET (dlg));

        /* refresh direct children */
        for (l = g->widgets; l != NULL; l = g_list_next (l))
            keymap_refresh_widget (WIDGET (l->data));
    }
}

/* --------------------------------------------------------------------------------------------- */
