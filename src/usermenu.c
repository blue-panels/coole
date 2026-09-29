/*
   User Menu implementation

   Copyright (C) 1994-2026
   Free Software Foundation, Inc.
   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Slava Zanko <slavazanko@gmail.com>, 2013
   Andrew Borodin <aborodin@vmail.ru>, 2013
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

/** \file usermenu.c
 *  \brief Source: user menu implementation
 */

#include <config.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/fileloc.h"
#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/search.h"
#include "lib/vfs/vfs.h"
#include "lib/strutil.h"
#include "lib/util.h"

#include "lib/widget.h"

#include "src/editor/edit.h"  // WEdit
#include "src/execute.h"
#include "src/setup.h"
#include "src/history.h"
#include "src/util.h"  // file_error_message()

#include "usermenu.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define MAX_ENTRY_LEN 60

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static gboolean debug_flag = FALSE;
static gboolean debug_error = FALSE;
static char *menu = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/** strip file's extension */
static char *
strip_ext (char *ss)
{
    char *s;
    char *e = NULL;

    if (ss == NULL)
        return NULL;

    for (s = ss; *s != '\0'; s++)
    {
        if (*s == '.')
            e = s;
        if (IS_PATH_SEP (*s) && e != NULL)
            e = NULL;  // '.' in *directory* name
    }

    if (e != NULL)
        *e = '\0';

    return (*ss == '\0' ? NULL : ss);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Check for the "shell_patterns" directive.  If it's found and valid,
 * interpret it and move the pointer past the directive.  Return the
 * current pointer.
 */

static char *
check_patterns (char *p)
{
    static const char def_name[] = "shell_patterns=";
    char *p0 = p;

    if (strncmp (p, def_name, sizeof (def_name) - 1) != 0)
        return p0;

    p += sizeof (def_name) - 1;
    if (*p == '1')
        easy_patterns = TRUE;
    else if (*p == '0')
        easy_patterns = FALSE;
    else
        return p0;

    // Skip spaces
    p++;
    while (whiteness (*p))
        p++;
    return p;
}

/* --------------------------------------------------------------------------------------------- */
/** Copies a whitespace separated argument from p to arg. Returns the
   point after argument. */

static char *
extract_arg (char *p, char *arg, int size)
{
    while (*p != '\0' && whiteness (*p))
        p++;

    // support quote space .mnu
    while (*p != '\0' && (*p != ' ' || *(p - 1) == '\\') && *p != '\t' && *p != '\n')
    {
        char *np;

        np = str_get_next_char (p);
        if (np - p >= size)
            break;
        memcpy (arg, p, np - p);
        arg += np - p;
        size -= np - p;
        p = np;
    }
    *arg = '\0';
    if (*p == '\0' || *p == '\n')
        str_prev_char (&p);
    return p;
}

/* --------------------------------------------------------------------------------------------- */
/* Tests whether the edited file is of any of the types specified in argument. */

static gboolean
test_type (const char *filename, char *arg)
{
    struct stat st;
    int result = 0;  // False by default
    mode_t st_mode;

    if (filename == NULL || lstat (filename, &st) != 0)
        return FALSE;

    st_mode = st.st_mode;

    for (; *arg != '\0'; arg++)
    {
        switch (*arg)
        {
        case 'n':  // Not a directory
            result |= !S_ISDIR (st_mode);
            break;
        case 'r':  // Regular file
            result |= S_ISREG (st_mode);
            break;
        case 'd':  // Directory
            result |= S_ISDIR (st_mode);
            break;
        case 'l':  // Link
            result |= S_ISLNK (st_mode);
            break;
        case 'c':  // Character special
            result |= S_ISCHR (st_mode);
            break;
        case 'b':  // Block special
            result |= S_ISBLK (st_mode);
            break;
        case 'f':  // Fifo (named pipe)
            result |= S_ISFIFO (st_mode);
            break;
        case 's':  // Socket
            result |= S_ISSOCK (st_mode);
            break;
        case 'x':  // Executable
            result |= (st_mode & 0111) != 0 ? 1 : 0;
            break;
        default:
            debug_error = TRUE;
            break;
        }
    }

    return (result != 0);
}

/* --------------------------------------------------------------------------------------------- */
/** Calculates the truth value of the next condition starting from
   p. Returns the point after condition. */

static char *
test_condition (const Widget *edit_widget, char *p, gboolean *condition)
{
    char arg[256];
    const mc_search_type_t search_type = easy_patterns ? MC_SEARCH_T_GLOB : MC_SEARCH_T_REGEX;
    const WEdit *e = CONST_EDIT (edit_widget);
    const char *edit_filename = e != NULL ? edit_get_file_name (e) : NULL;

    // Handle one condition
    for (; *p != '\n' && *p != '&' && *p != '|'; p++)
    {
        // support quote space .mnu
        if ((*p == ' ' && *(p - 1) != '\\') || *p == '\t')
            continue;

        // the upper case letters are the same as the lower case ones
        *p |= 0x20;

        switch (*p++)
        {
        case '!':
            p = test_condition (edit_widget, p, condition);
            *condition = !*condition;
            str_prev_char (&p);
            break;
        case 'f':  // file name pattern
            p = extract_arg (p, arg, sizeof (arg));
            *condition = edit_filename != NULL && mc_search (arg, NULL, edit_filename, search_type);
            break;
        case 'y':  // syntax pattern
            if (e != NULL)
            {
                const char *syntax_type = edit_get_syntax_type (e);

                if (syntax_type != NULL)
                {
                    p = extract_arg (p, arg, sizeof (arg));
                    *condition = mc_search (arg, NULL, syntax_type, MC_SEARCH_T_NORMAL);
                }
            }
            break;
        case 'd':  // current directory pattern
            p = extract_arg (p, arg, sizeof (arg));
            *condition = mc_search (arg, NULL, vfs_get_current_dir (), search_type);
            break;
        case 't':  // type of the edited file
            p = extract_arg (p, arg, sizeof (arg));
            *condition = test_type (edit_filename, arg);
            break;
        case 'x':  // executable
        {
            struct stat status;

            p = extract_arg (p, arg, sizeof (arg));
            *condition = stat (arg, &status) == 0 && is_exe (status.st_mode);
            break;
        }
        default:
            debug_error = TRUE;
            break;
        }  // switch
    }  // while
    return p;
}

/* --------------------------------------------------------------------------------------------- */
/** General purpose condition debug output handler */

static void
debug_out (char *start, char *end, gboolean condition)
{
    static char *msg = NULL;

    if (start == NULL && end == NULL)
    {
        // Show output
        if (debug_flag && msg != NULL)
        {
            size_t len;

            len = strlen (msg);
            if (len != 0)
                msg[len - 1] = '\0';
            message (D_NORMAL, _ ("Debug"), "%s", msg);
        }
        debug_flag = FALSE;
        MC_PTR_FREE (msg);
    }
    else
    {
        const char *type;
        char *p;

        // Save debug info for later output
        if (!debug_flag)
            return;
        // Save the result of the condition
        if (debug_error)
        {
            type = _ ("ERROR:");
            debug_error = FALSE;
        }
        else if (condition)
            type = _ ("True:");
        else
            type = _ ("False:");
        // This is for debugging, don't need to be super efficient.
        if (end == NULL)
            p = g_strdup_printf ("%s %s %c \n", msg ? msg : "", type, *start);
        else
            p = g_strdup_printf ("%s %s %.*s \n", msg ? msg : "", type, (int) (end - start), start);
        g_free (msg);
        msg = p;
    }
}

/* --------------------------------------------------------------------------------------------- */
/** Calculates the truth value of one lineful of conditions. Returns
   the point just before the end of line. */

static char *
test_line (const Widget *edit_widget, char *p, gboolean *result)
{
    char operator;

    // Repeat till end of line
    while (*p != '\0' && *p != '\n')
    {
        char *debug_start, *debug_end;
        gboolean condition = TRUE;

        // support quote space .mnu
        while ((*p == ' ' && *(p - 1) != '\\') || *p == '\t')
            p++;
        if (*p == '\0' || *p == '\n')
            break;
        operator = *p++;
        if (*p == '?')
        {
            debug_flag = TRUE;
            p++;
        }
        // support quote space .mnu
        while ((*p == ' ' && *(p - 1) != '\\') || *p == '\t')
            p++;
        if (*p == '\0' || *p == '\n')
            break;

        debug_start = p;
        p = test_condition (edit_widget, p, &condition);
        debug_end = p;
        // Add one debug statement
        debug_out (debug_start, debug_end, condition);

        switch (operator)
        {
        case '+':
        case '=':
            // Assignment
            *result = condition;
            break;
        case '&':  // Logical and
            *result = *result && condition;
            break;
        case '|':  // Logical or
            *result = *result || condition;
            break;
        default:
            debug_error = TRUE;
            break;
        }  // switch
        // Add one debug statement
        debug_out (&operator, NULL, *result);

    }  // while (*p != '\n')
    // Report debug message
    debug_out (NULL, NULL, TRUE);

    if (*p == '\0' || *p == '\n')
        str_prev_char (&p);
    return p;
}

/* --------------------------------------------------------------------------------------------- */
/** Run the command and open what it prints in an editor window of its own. */

static void
run_menu_command_to_editor (const char *cmd)
{
    vfs_path_t *out_vpath;
    char *full_cmd;
    int out_fd;
    int status;

    out_fd = mc_mkstemps (&out_vpath, "output", NULL);
    if (out_fd == -1)
    {
        file_error_message (_ ("Cannot create temporary file"), NULL);
        return;
    }
    close (out_fd);

    full_cmd = g_strconcat (cmd, " > ", vfs_path_as_str (out_vpath), " 2>&1", (char *) NULL);

    tty_reset_shell_mode ();
    status = system (full_cmd);
    tty_raw_mode ();
    g_free (full_cmd);

    if (status == -1)
        message (D_ERROR, MSG_ERROR, "%s", _ ("Error calling program"));
    else
        edit_file_at_line (out_vpath, 0);

    mc_unlink (out_vpath);
    vfs_path_free (out_vpath, TRUE);
    repaint_screen ();
}

/* --------------------------------------------------------------------------------------------- */
/** FIXME: recode this routine on version 3.0, it could be cleaner */

static void
execute_menu_command (const Widget *edit_widget, const char *commands, gboolean show_prompt)
{
    FILE *cmd_file;
    int cmd_file_fd;
    gboolean expand_prefix_found = FALSE;
    char *parameter = NULL;
    gboolean do_quote = FALSE;
    char lc_prompt[80];
    int col;
    vfs_path_t *file_name_vpath;
    gboolean run_view = FALSE;
    char *cmd;

    // Skip menu entry title line
    commands = strchr (commands, '\n');
    if (commands == NULL)
        return;

    cmd_file_fd = mc_mkstemps (&file_name_vpath, "usermenu", SCRIPT_SUFFIX);

    if (cmd_file_fd == -1)
    {
        file_error_message (_ ("Cannot create temporary command file"), NULL);
        return;
    }

    cmd_file = fdopen (cmd_file_fd, "w");
    fputs ("#! /bin/sh\nrm -f \"$0\"\n", cmd_file);
    commands++;

    for (col = 0; *commands != '\0'; commands++)
    {
        if (col == 0)
        {
            if (!whitespace (*commands))
                break;
            while (whitespace (*commands))
                commands++;
            if (*commands == '\0')
                break;
        }
        col++;
        if (*commands == '\n')
            col = 0;
        if (parameter != NULL)
        {
            if (*commands == '}')
            {
                *parameter = '\0';
                parameter = input_dialog (
                    _ ("Parameter"), lc_prompt, MC_HISTORY_EDIT_MENU_EXEC_PARAM, "",
                    INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_CD | INPUT_COMPLETE_HOSTNAMES
                        | INPUT_COMPLETE_VARIABLES | INPUT_COMPLETE_USERNAMES);
                if (parameter == NULL || *parameter == '\0')
                {
                    // User canceled
                    g_free (parameter);
                    fclose (cmd_file);
                    mc_unlink (file_name_vpath);
                    vfs_path_free (file_name_vpath, TRUE);
                    return;
                }
                if (do_quote)
                {
                    char *tmp;

                    tmp = name_quote (parameter, FALSE);
                    if (tmp != NULL)
                    {
                        fputs (tmp, cmd_file);
                        g_free (tmp);
                    }
                }
                else
                    fputs (parameter, cmd_file);

                MC_PTR_FREE (parameter);
            }
            else if (parameter < lc_prompt + sizeof (lc_prompt) - 1)
                *parameter++ = *commands;
        }
        else if (expand_prefix_found)
        {
            expand_prefix_found = FALSE;
            if (g_ascii_isdigit ((gchar) *commands))
            {
                do_quote = (atoi (commands) != 0);
                while (g_ascii_isdigit ((gchar) *commands))
                    commands++;
            }
            if (*commands == '{')
                parameter = lc_prompt;
            else
            {
                char *text;

                text = expand_format (edit_widget, *commands, do_quote);
                if (text != NULL)
                {
                    fputs (text, cmd_file);
                    g_free (text);
                }
            }
        }
        else if (*commands == '%')
        {
            int i;

            i = check_format_view (commands + 1);
            if (i != 0)
            {
                commands += i;
                run_view = TRUE;
            }
            else
            {
                do_quote = TRUE;  // Default: Quote expanded macro
                expand_prefix_found = TRUE;
            }
        }
        else
            fputc (*commands, cmd_file);
    }

    fclose (cmd_file);
    mc_chmod (file_name_vpath, S_IRWXU);

    // Execute the command indirectly to allow execution even on no-exec filesystems.
    cmd = g_strconcat ("/bin/sh ", vfs_path_as_str (file_name_vpath), (char *) NULL);

    if (run_view)
        run_menu_command_to_editor (cmd);
    else if (show_prompt)
        shell_execute (cmd, EXECUTE_HIDE);
    else
    {
        gboolean ok;

        /* Prepare the terminal by setting its flag to the initial ones. This will cause \r
         * to work as expected, instead of being ignored. */
        tty_reset_shell_mode ();

        ok = (system (cmd) != -1);

        // Restore terminal configuration.
        tty_raw_mode ();

        // Redraw the original screen's contents.
        tty_clear_screen ();
        repaint_screen ();

        if (!ok)
            message (D_ERROR, MSG_ERROR, "%s", _ ("Error calling program"));
    }

    g_free (cmd);

    vfs_path_free (file_name_vpath, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/**
 **     Check owner of the menu file. Using menu file is allowed, if
 **     owner of the menu is root or the actual user. In either case
 **     file should not be group and word-writable.
 **
 **     Q. Should we apply this routine to system and home menu (and .ext files)?
 */

static gboolean
menu_file_own (char *path)
{
    struct stat st;

    if (stat (path, &st) == 0 && (st.st_uid == 0 || (st.st_uid == geteuid ()) != 0)
        && ((st.st_mode & (S_IWGRP | S_IWOTH)) == 0))
        return TRUE;

    if (verbose)
        message (D_NORMAL, _ ("Warning -- ignoring file"),
                 _ ("File %s is not owned by root or you or is world writable.\n"
                    "Using it may compromise your security"),
                 path);

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/*  Formats defined:

        %f The current file name without the path.
        %p Same as %f.
        %n The current file name without extension.
        %x The extension of the current file name.
        %d The current working directory.

        Uppercase and lowercase macros above are the same.

        %c The cursor column position number.
        %i The indent of blank space, equal the cursor column position.
        %y The syntax type of current file.
        %b The block file name.
        %m The menu file name.

        %% The % character

        %view Runs the commands and opens their standard output in an editor window.
            The keywords in braces after %view, such as %view{ascii,nroff}, are accepted
            and ignored.

        %{some text} Prompt for the substitution. An input box is shown and the text inside
            the braces is used as a prompt. The macro is substituted by the text typed
            by the user. The user can press ESC or F10 to cancel.

        With a number followed the % character you can turn quoting on (default)
        and off. For example:
            %f  quote expanded macro
            %1f ditto
            %0f don't quote expanded macro.

    Expand_format returns a memory block that must be free()d.
 */

/* Returns how many characters we should advance if %view was found */
int
check_format_view (const char *p)
{
    const char *q = p;

    if (strncmp (p, "view", 4) != 0)
        return 0;

    q += 4;
    if (*q == '{')
    {
        for (q++; *q != '\0' && *q != '}'; q++)
            ;
        if (*q == '}')
            q++;
    }

    return q - p;
}

/* --------------------------------------------------------------------------------------------- */
char *
expand_format (const Widget *edit_widget, char c, gboolean do_quote)
{
    char *(*quote_func) (const char *, gboolean);
    const char *fname = NULL;
    char *result;
    char c_lc;

    const WEdit *e = CONST_EDIT (edit_widget);

    if (c == '%')
        return g_strdup ("%");

    if (e != NULL)
        fname = edit_get_file_name (e);

    if (do_quote)
        quote_func = name_quote;
    else
        quote_func = fake_name_quote;

    c_lc = g_ascii_tolower ((gchar) c);

    switch (c_lc)
    {
    case 'f':
    case 'p':
        result = quote_func (fname, FALSE);
        goto ret;
    case 'x':
        result = quote_func (extension (fname), FALSE);
        goto ret;
    case 'n':  // strip extension
        result = strip_ext (quote_func (fname, FALSE));
        goto ret;
    case 'd':
        result = quote_func (vfs_get_current_dir (), FALSE);
        goto ret;
    case 'c':
        if (e != NULL)
        {
            result = g_strdup_printf ("%u", (unsigned int) edit_get_cursor_offset (e));
            goto ret;
        }
        break;
    case 'i':  // indent equal number cursor position in line
        if (e != NULL)
        {
            result = g_strnfill (edit_get_curs_col (e), ' ');
            goto ret;
        }
        break;
    case 'y':  // syntax type
        if (e != NULL)
        {
            const char *syntax_type;

            syntax_type = edit_get_syntax_type (e);
            if (syntax_type != NULL)
            {
                result = g_strdup (syntax_type);
                goto ret;
            }
        }
        break;
    case 'b':  // block file name
        if (e != NULL)
        {
            char *file;

            file = mc_config_get_full_path (EDIT_HOME_BLOCK_FILE);
            result = quote_func (file, FALSE);
            g_free (file);
            goto ret;
        }
        break;
    case 'm':  // menu file name
        if (menu != NULL)
        {
            result = quote_func (menu, FALSE);
            goto ret;
        }
        break;
    case 's':
        result = quote_func (fname, FALSE);
        goto ret;
    default:
        break;
    }  // switch

    result = g_strdup ("% ");
    result[1] = c;
ret:
    return result;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Show the user menu of the editor and run the chosen entry.
 *
 * @param edit_widget the editor the menu is for
 * @param menu_file the menu file; NULL to look for it in the usual places
 * @param selected_entry the entry to run without asking; -1 to ask
 */

gboolean
user_menu_cmd (const Widget *edit_widget, const char *menu_file, int selected_entry)
{
    char *data, *p;
    GPtrArray *entries = NULL;
    int max_cols = 0;
    int col = 0;
    gboolean accept_entry = TRUE;
    int selected = -1;
    gboolean old_patterns;
    gboolean res = FALSE;
    gboolean interactive = TRUE;

    if (!vfs_current_is_local ())
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("Cannot execute commands on non-local filesystems"));
        return FALSE;
    }

    menu = g_strdup (menu_file != NULL ? menu_file : EDIT_LOCAL_MENU);

    if (!exist_file (menu) || !menu_file_own (menu))
    {
        if (menu_file != NULL)
        {
            file_error_message (_ ("Cannot open file\n%s"), menu);
            MC_PTR_FREE (menu);
            return FALSE;
        }

        g_free (menu);
        menu = mc_config_get_full_path (EDIT_HOME_MENU);
        if (!exist_file (menu))
        {
            g_free (menu);
            menu = mc_build_filename (mc_config_get_home_dir (), EDIT_GLOBAL_MENU, (char *) NULL);
            if (!exist_file (menu))
            {
                g_free (menu);
                menu = mc_build_filename (mc_global.sysconfig_dir, EDIT_GLOBAL_MENU, (char *) NULL);
                if (!exist_file (menu))
                {
                    g_free (menu);
                    menu = mc_build_filename (mc_global.share_data_dir, EDIT_GLOBAL_MENU,
                                              (char *) NULL);
                }
            }
        }
    }

    if (!g_file_get_contents (menu, &data, NULL, NULL))
    {
        file_error_message (_ ("Cannot open file\n%s"), menu);
        MC_PTR_FREE (menu);
        return FALSE;
    }

    old_patterns = easy_patterns;

    // Parse the menu file
    for (p = check_patterns (data); *p != '\0'; str_next_char (&p))
    {
        unsigned int menu_lines = entries == NULL ? 0 : entries->len;

        if (col == 0 && (entries == NULL || menu_lines == entries->len))
            switch (*p)
            {
            case '#':
                // do not show prompt if first line of external script is #silent
                if (selected_entry >= 0 && strncmp (p, "#silent", 7) == 0)
                    interactive = FALSE;
                // A commented menu entry
                accept_entry = TRUE;
                break;

            case '+':
                if (*(p + 1) == '=')
                {
                    // Combined adding and default
                    p = test_line (edit_widget, p + 1, &accept_entry);
                    if (selected < 0 && accept_entry)
                        selected = menu_lines;
                }
                else
                {
                    // A condition for adding the entry
                    p = test_line (edit_widget, p, &accept_entry);
                }
                break;

            case '=':
                if (*(p + 1) == '+')
                {
                    // Combined adding and default
                    p = test_line (edit_widget, p + 1, &accept_entry);
                    if (selected < 0 && accept_entry)
                        selected = menu_lines;
                }
                else
                {
                    // A condition for making the entry default
                    gboolean ok = TRUE;

                    p = test_line (edit_widget, p, &ok);
                    if (selected < 0 && ok)
                        selected = menu_lines;
                }
                break;

            default:
                if (!whitespace (*p) && str_isprint (p))
                {
                    // A menu entry title line
                    if (accept_entry)
                    {
                        if (entries == NULL)
                            entries = g_ptr_array_new ();
                        g_ptr_array_add (entries, p);
                    }
                    else
                        accept_entry = TRUE;
                }
                break;
            }

        if (*p == '\n')
        {
            if (entries != NULL && entries->len > menu_lines)
                accept_entry = TRUE;
            max_cols = MAX (max_cols, col);
            col = 0;
        }
        else
        {
            if (*p == '\t')
                *p = ' ';
            col++;
        }
    }

    if (entries == NULL)
        message (D_ERROR, MSG_ERROR, _ ("No suitable entries found in %s"), menu);
    else
    {
        if (selected_entry >= 0)
            selected = selected_entry;
        else
        {
            Listbox *listbox;
            unsigned int i;

            max_cols = MIN (MAX (max_cols, col), MAX_ENTRY_LEN);

            // Create listbox
            listbox = listbox_window_new (entries->len, max_cols + 2, _ ("User menu"),
                                          "[Edit Menu File]");
            // insert all the items found
            for (i = 0; i < entries->len; i++)
            {
                p = g_ptr_array_index (entries, i);
                LISTBOX_APPEND_TEXT (listbox, (unsigned char) p[0],
                                     extract_line (p, p + MAX_ENTRY_LEN, NULL), p, FALSE);
            }
            // Select the default entry
            listbox_set_current (listbox->list, selected);

            selected = listbox_run (listbox);
        }

        if (selected >= 0)
        {
            execute_menu_command (edit_widget, g_ptr_array_index (entries, selected), interactive);
            res = TRUE;
        }

        g_ptr_array_free (entries, TRUE);

        do_refresh ();
    }

    easy_patterns = old_patterns;
    MC_PTR_FREE (menu);
    g_free (data);
    return res;
}

/* --------------------------------------------------------------------------------------------- */
