/*
   Setup loading/saving.

   Copyright (C) 1994-2025
   Free Software Foundation, Inc.
   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

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

/** \file setup.c
 *  \brief Source: setup loading/saving
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/tty/key.h"
#include "lib/tty/mouse.h"  // double_click_speed
#include "lib/mcconfig.h"   // num_history_items_recorded
#include "lib/fileloc.h"
#include "lib/terminal.h"  // convert_controls()
#include "lib/util.h"
#include "lib/charsets.h"
#include "lib/widget.h"  // mouse_close_dialog

#include "execute.h"  // pause_after_run
#include "clipboard.h"
#include "selcodepage.h"

#include "src/editor/edit.h"

#include "setup.h"

/*** global variables ****************************************************************************/

/* Controls screen clearing before an exec */
gboolean clear_before_exec = TRUE;

/* This flag indicates if the pull down menus by default drop down */
gboolean drop_menus = FALSE;

/* Tab size */
int option_tab_spacing = DEFAULT_TAB_SPACING;

gboolean easy_patterns = TRUE;

/* It true saves the setup when quitting */
gboolean auto_save_setup = TRUE;

/* If set, running a command hands the screen over instead of using the terminal */

gboolean verbose = TRUE;

/* Numbers of (file I/O) and (input/display) codepages. -1 if not selected */
int default_source_codepage = -1;
char *autodetect_codeset = NULL;
gboolean is_autodetect_codeset_enabled = FALSE;

#ifdef HAVE_ASPELL
char *spell_language = NULL;
#endif

/* Set when main loop should be terminated */
int quit = 0;

/* index to record_macro_buf[], -1 if not recording a macro */
int macro_index = -1;

/* macro stuff */
struct macro_action_t record_macro_buf[MAX_MACRO_LENGTH];

GArray *macros_list = NULL;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static char *profile_name = NULL; /* ${XDG_CONFIG_HOME}/coole/ini */

static const struct
{
    const char *opt_name;
    gboolean *opt_addr;
} layout_bool_options[] = {
    { "keybar_visible", &mc_global.keybar_visible },
    {
        NULL,
        NULL,
    },
};

static const struct
{
    const char *opt_name;
    gboolean *opt_addr;
} bool_options[] = {
    { "verbose", &verbose },
    { "shell_patterns", &easy_patterns },
    { "auto_save_setup", &auto_save_setup },
    { "clear_before_exec", &clear_before_exec },
    { "confirm_history_cleanup", &mc_global.widget.confirm_history_cleanup },
    { "mouse_close_dialog", &mouse_close_dialog },
    { "drop_menus", &drop_menus },
    { "old_esc_mode", &old_esc_mode },
    { "show_all_if_ambiguous", &mc_global.widget.show_all_if_ambiguous },
    { "alternate_plus_minus", &mc_global.tty.alternate_plus_minus },
    { "editor_fill_tabs_with_spaces", &edit_options.fill_tabs_with_spaces },
    { "editor_return_does_auto_indent", &edit_options.return_does_auto_indent },
    { "editor_backspace_through_tabs", &edit_options.backspace_through_tabs },
    { "editor_fake_half_tabs", &edit_options.fake_half_tabs },
    { "editor_option_save_position", &edit_options.save_position },
    { "editor_option_auto_para_formatting", &edit_options.auto_para_formatting },
    { "editor_option_typewriter_wrap", &edit_options.typewriter_wrap },
    { "editor_edit_confirm_save", &edit_options.confirm_save },
    { "editor_syntax_highlighting", &edit_options.syntax_highlighting },
    { "editor_persistent_selections", &edit_options.persistent_selections },
    { "editor_drop_selection_on_copy", &edit_options.drop_selection_on_copy },
    { "editor_cursor_beyond_eol", &edit_options.cursor_beyond_eol },
    { "editor_cursor_after_inserted_block", &edit_options.cursor_after_inserted_block },
    { "editor_visible_tabs", &edit_options.visible_tabs },
    { "editor_visible_spaces", &edit_options.visible_tws },
    { "editor_line_state", &edit_options.line_state },
    { "editor_simple_statusbar", &edit_options.simple_statusbar },
    { "editor_check_new_line", &edit_options.check_nl_at_eof },
    { "editor_show_right_margin", &edit_options.show_right_margin },
    { "editor_show_control_chars", &edit_options.show_control_chars },
    { "editor_group_undo", &edit_options.group_undo },
    { "editor_state_full_filename", &edit_options.state_full_filename },
    { "shadows", &mc_global.tty.shadows },
    {
        NULL,
        NULL,
    },
};

static const struct
{
    const char *opt_name;
    int *opt_addr;
} int_options[] = {
    { "pause_after_run", &pause_after_run },
    { "mouse_repeat_rate", &mou_auto_repeat },
    { "double_click_speed", &double_click_speed },
    { "old_esc_mode_timeout", &old_esc_mode_timeout },
    { "num_history_items_recorded", &num_history_items_recorded },
    { "editor_tab_spacing", &option_tab_spacing },
    { "editor_word_wrap_line_length", &edit_options.word_wrap_line_length },
    { "editor_option_save_mode", &edit_options.save_mode },
    {
        NULL,
        NULL,
    },
};

static const struct
{
    const char *opt_name;
    char **opt_addr;
    const char *opt_defval;
} str_options[] = {
    { "editor_backup_extension", &edit_options.backup_ext, "~" },
    { "editor_filesize_threshold", &edit_options.filesize_threshold, "64M" },
    { "editor_stop_format_chars", &edit_options.stop_format_chars, "-+*\\,.;:&>" },
    { NULL, NULL, NULL },
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
load_config (void)
{
    size_t i;
    const char *kt;

    // Load boolean options
    for (i = 0; bool_options[i].opt_name != NULL; i++)
        *bool_options[i].opt_addr =
            mc_config_get_bool (mc_global.main_config, CONFIG_APP_SECTION, bool_options[i].opt_name,
                                *bool_options[i].opt_addr);

    // Load integer options
    for (i = 0; int_options[i].opt_name != NULL; i++)
        *int_options[i].opt_addr =
            mc_config_get_int (mc_global.main_config, CONFIG_APP_SECTION, int_options[i].opt_name,
                               *int_options[i].opt_addr);

    // Load string options
    for (i = 0; str_options[i].opt_name != NULL; i++)
        *str_options[i].opt_addr =
            mc_config_get_string (mc_global.main_config, CONFIG_APP_SECTION,
                                  str_options[i].opt_name, str_options[i].opt_defval);

    // Load layout options
    for (i = 0; layout_bool_options[i].opt_name != NULL; i++)
        *layout_bool_options[i].opt_addr =
            mc_config_get_bool (mc_global.main_config, CONFIG_LAYOUT_SECTION,
                                layout_bool_options[i].opt_name, *layout_bool_options[i].opt_addr);

    // Overwrite some options
    if (edit_options.word_wrap_line_length <= 0)
        edit_options.word_wrap_line_length = DEFAULT_WRAP_LINE_LENGTH;

    if (option_tab_spacing <= 0)
        option_tab_spacing = DEFAULT_TAB_SPACING;

    kt = getenv ("KEYBOARD_KEY_TIMEOUT_US");
    if (kt != NULL && kt[0] != '\0')
        old_esc_mode_timeout = atoi (kt);
}

/* --------------------------------------------------------------------------------------------- */

static void
load_keys_from_section (const char *terminal, mc_config_t *cfg)
{
    char *section_name;
    gchar **profile_keys, **keys;
    char *valcopy, *value;

    if (terminal == NULL)
        return;

    section_name = g_strconcat ("terminal:", terminal, (char *) NULL);
    keys = mc_config_get_keys (cfg, section_name, NULL);

    for (profile_keys = keys; *profile_keys != NULL; profile_keys++)
    {
        // copy=other causes all keys from [terminal:other] to be loaded.
        if (g_ascii_strcasecmp (*profile_keys, "copy") == 0)
        {
            valcopy = mc_config_get_string (cfg, section_name, *profile_keys, "");
            load_keys_from_section (valcopy, cfg);
            g_free (valcopy);
            continue;
        }

        const int key_code = tty_normalize_keycode (tty_keyname_to_keycode (*profile_keys, NULL));

        if (key_code != 0)
        {
            gchar **values;

            values = mc_config_get_string_list (cfg, section_name, *profile_keys, NULL);
            if (values != NULL)
            {
                gchar **curr_values;

                for (curr_values = values; *curr_values != NULL; curr_values++)
                {
                    valcopy = convert_controls (*curr_values);
                    define_sequence (key_code, valcopy, MCKEY_NOACTION);
                    g_free (valcopy);
                }

                g_strfreev (values);
            }
            else
            {
                value = mc_config_get_string (cfg, section_name, *profile_keys, "");
                valcopy = convert_controls (value);
                define_sequence (key_code, valcopy, MCKEY_NOACTION);
                g_free (valcopy);
                g_free (value);
            }
        }
    }
    g_strfreev (keys);
    g_free (section_name);
}

/* --------------------------------------------------------------------------------------------- */

static void
load_keys_for_terminal (const char *terminal, mc_config_t *cfg)
{
    if (terminal == NULL)
        return;

    if (g_str_has_prefix (terminal, "xterm") && strcmp (terminal, "xterm") != 0)
        load_keys_from_section ("xterm", cfg);

    load_keys_from_section (terminal, cfg);
}

/* --------------------------------------------------------------------------------------------- */

static void
save_config (void)
{
    size_t i;

    // Save boolean options
    for (i = 0; bool_options[i].opt_name != NULL; i++)
        mc_config_set_bool (mc_global.main_config, CONFIG_APP_SECTION, bool_options[i].opt_name,
                            *bool_options[i].opt_addr);

    // Save integer options
    for (i = 0; int_options[i].opt_name != NULL; i++)
        mc_config_set_int (mc_global.main_config, CONFIG_APP_SECTION, int_options[i].opt_name,
                           *int_options[i].opt_addr);

    // Save string options
    for (i = 0; str_options[i].opt_name != NULL; i++)
        mc_config_set_string (mc_global.main_config, CONFIG_APP_SECTION, str_options[i].opt_name,
                              *str_options[i].opt_addr);

    // Save layout options
    for (i = 0; layout_bool_options[i].opt_name != NULL; i++)
        mc_config_set_bool (mc_global.main_config, CONFIG_LAYOUT_SECTION,
                            layout_bool_options[i].opt_name, *layout_bool_options[i].opt_addr);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Load learned keys from ~/.config/coole/term/<TERM>
 */

static void
load_term_keys_file (void)
{
    char *term_file;
    mc_config_t *term_cfg;
    gchar **keys;

    term_file = mc_term_keys_path ();

    if (!g_file_test (term_file, G_FILE_TEST_EXISTS))
    {
        g_free (term_file);
        return;
    }

    term_cfg = mc_config_init (term_file, TRUE);
    keys = mc_config_get_keys (term_cfg, "keys", NULL);

    if (keys != NULL)
    {
        gchar **pk;

        for (pk = keys; *pk != NULL; pk++)
        {
            int key_code;

            key_code = tty_normalize_keycode (tty_keyname_to_keycode (*pk, NULL));
            if (key_code != 0)
            {
                char *value;

                value = mc_config_get_string_raw (term_cfg, "keys", *pk, NULL);
                if (value != NULL)
                {
                    char *valcopy;

                    valcopy = convert_controls (value);
                    define_sequence (key_code, valcopy, MCKEY_NOACTION);
                    g_free (valcopy);
                    g_free (value);
                }
            }
        }
        g_strfreev (keys);
    }

    mc_config_deinit (term_cfg);
    g_free (term_file);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const char *
setup_init (void)
{
    if (profile_name == NULL)
    {
        char *profile;

        profile = mc_config_get_full_path (MC_CONFIG_FILE);
        if (!exist_file (profile))
        {
            char *inifile;

            inifile = mc_build_filename (mc_global.sysconfig_dir, "coole.ini", (char *) NULL);
            if (exist_file (inifile))
            {
                g_free (profile);
                profile = inifile;
            }
            else
            {
                g_free (inifile);
                inifile = mc_build_filename (mc_global.share_data_dir, "coole.ini", (char *) NULL);
                if (!exist_file (inifile))
                    g_free (inifile);
                else
                {
                    g_free (profile);
                    profile = inifile;
                }
            }
        }

        profile_name = profile;
    }

    return profile_name;
}

/* --------------------------------------------------------------------------------------------- */

void
load_setup (void)
{
    const char *profile;
    const char *cbuffer;

    load_codepages_list ();

    profile = setup_init ();

    /* defaults.ini is common for all users, but has priority lower than
       ${XDG_CONFIG_HOME}/coole/ini.  It is only used for keys now */
    mc_global.profile_name =
        g_build_filename (mc_global.sysconfig_dir, MC_GLOBAL_CONFIG_FILE, (char *) NULL);
    if (!exist_file (mc_global.profile_name))
    {
        g_free (mc_global.profile_name);
        mc_global.profile_name =
            g_build_filename (mc_global.share_data_dir, MC_GLOBAL_CONFIG_FILE, (char *) NULL);
    }

    mc_global.main_config = mc_config_init (profile, FALSE);

    load_config ();

    // The default color and the terminal dependent color
    mc_global.tty.setup_color_string =
        mc_config_get_string (mc_global.main_config, "Colors", "base_color", "");
    mc_global.tty.term_color_string =
        mc_config_get_string (mc_global.main_config, "Colors", getenv ("TERM"), "");
    mc_global.tty.color_terminal_string =
        mc_config_get_string (mc_global.main_config, "Colors", "color_terminals", "");

    if (codepages->len > 1)
    {
        char *buffer;

        // Detect display codepage
        const char *current_system_codepage = str_detect_termencoding ();

        mc_global.display_codepage = get_codepage_index (current_system_codepage);

        // Default to 7-bit ASCII
        if (mc_global.display_codepage == -1)
            mc_global.display_codepage = 0;

        cp_display = get_codepage_id (mc_global.display_codepage);

        mc_global.utf8_display = str_isutf8 (current_system_codepage);

        // Restore source codepage
        buffer = mc_config_get_string (mc_global.main_config, CONFIG_MISC_SECTION,
                                       "source_codepage", "");
        if (buffer[0] != '\0')
        {
            default_source_codepage = get_codepage_index (buffer);
            mc_global.source_codepage =
                default_source_codepage;  // May be source_codepage doesn't need this
            cp_source = get_codepage_id (mc_global.source_codepage);
        }
        g_free (buffer);
    }

    autodetect_codeset =
        mc_config_get_string (mc_global.main_config, CONFIG_MISC_SECTION, "autodetect_codeset", "");
    if ((autodetect_codeset[0] != '\0') && (strcmp (autodetect_codeset, "off") != 0))
        is_autodetect_codeset_enabled = TRUE;

    g_free (init_translation_table (mc_global.source_codepage, mc_global.display_codepage));
    cbuffer = get_codepage_id (mc_global.display_codepage);
    if (cbuffer != NULL)
        mc_global.utf8_display = str_isutf8 (cbuffer);

#ifdef HAVE_ASPELL
    spell_language =
        mc_config_get_string (mc_global.main_config, CONFIG_MISC_SECTION, "spell_language", "en");
#endif

    clipboard_store_path =
        mc_config_get_string (mc_global.main_config, CONFIG_MISC_SECTION, "clipboard_store", "");
    clipboard_paste_path =
        mc_config_get_string (mc_global.main_config, CONFIG_MISC_SECTION, "clipboard_paste", "");
}

/* --------------------------------------------------------------------------------------------- */

gboolean
save_setup (void)
{
    char *tmp_profile;
    gboolean ret;

    save_config ();

    mc_config_set_string (mc_global.main_config, CONFIG_MISC_SECTION, "display_codepage",
                          get_codepage_id (mc_global.display_codepage));
    mc_config_set_string (mc_global.main_config, CONFIG_MISC_SECTION, "source_codepage",
                          get_codepage_id (default_source_codepage));
    mc_config_set_string (mc_global.main_config, CONFIG_MISC_SECTION, "autodetect_codeset",
                          autodetect_codeset);

#ifdef HAVE_ASPELL
    mc_config_set_string (mc_global.main_config, CONFIG_MISC_SECTION, "spell_language",
                          spell_language);
#endif

    mc_config_set_string (mc_global.main_config, CONFIG_MISC_SECTION, "clipboard_store",
                          clipboard_store_path);
    mc_config_set_string (mc_global.main_config, CONFIG_MISC_SECTION, "clipboard_paste",
                          clipboard_paste_path);

    tmp_profile = mc_config_get_full_path (MC_CONFIG_FILE);
    ret = mc_config_save_to_file (mc_global.main_config, tmp_profile, NULL);
    g_free (tmp_profile);

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

void
save_setup_cmd (void)
{
    const char *home = mc_config_get_home_dir ();
    const char *config = mc_config_get_path ();
    char *path;

    // show the configuration directory as ~/... when it is in the home directory
    if (home != NULL && *home != '\0' && g_str_has_prefix (config, home)
        && IS_PATH_SEP (config[strlen (home)]))
        path = g_strconcat ("~", config + strlen (home), (char *) NULL);
    else
        path = g_strdup (config);

    if (save_setup ())
        message (D_NORMAL, _ ("Setup"), _ ("Setup saved to %s"), path);
    else
        message (D_ERROR, _ ("Setup"), _ ("Unable to save setup to %s"), path);

    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

void
done_setup (void)
{
    size_t i;

    g_free (clipboard_store_path);
    g_free (clipboard_paste_path);
    g_free (mc_global.profile_name);
    g_free (mc_global.tty.color_terminal_string);
    g_free (mc_global.tty.term_color_string);
    g_free (mc_global.tty.setup_color_string);
    g_free (profile_name);
    mc_config_deinit (mc_global.main_config);

    for (i = 0; str_options[i].opt_name != NULL; i++)
        g_free (*str_options[i].opt_addr);

    g_free (autodetect_codeset);
    free_codepages_list ();

#ifdef HAVE_ASPELL
    g_free (spell_language);
#endif
}

/* --------------------------------------------------------------------------------------------- */

char *
mc_term_keys_path (void)
{
    const char *term;
    char *path;
    char *dir;

    term = getenv ("TERM");
    if (term == NULL || term[0] == '\0')
        term = "unknown";

    path = g_build_filename (mc_config_get_path (), "term", term, (char *) NULL);

    /* ensure directory exists */
    dir = g_path_get_dirname (path);
    g_mkdir_with_parents (dir, 0700);
    g_free (dir);

    return path;
}

/* --------------------------------------------------------------------------------------------- */

void
load_key_defs (void)
{
    /*
     * Load keys from defaults.ini before ${XDG_CONFIG_HOME}/coole/ini, so that the user
     * definitions override global settings.
     */
    mc_config_t *mc_global_config;

    mc_global_config = mc_config_init (mc_global.profile_name, FALSE);
    if (mc_global_config != NULL)
    {
        load_keys_from_section ("general", mc_global_config);
        load_keys_for_terminal (getenv ("TERM"), mc_global_config);
        mc_config_deinit (mc_global_config);
    }

    load_keys_from_section ("general", mc_global.main_config);
    load_keys_for_terminal (getenv ("TERM"), mc_global.main_config);

    // load learned keys from ~/.config/coole/term/<TERM>
    load_term_keys_file ();
}

/* --------------------------------------------------------------------------------------------- */
