/*
   The places the jumps of the editor go from and to: Go back, Go forward and the Navigation
   history.

   Copyright (C) 2026
   Ilia Maslakov <il.smind@gmail.com>

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of coole.

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

#include <stdlib.h>
#include <string.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/tty/tty.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "src/plugins/project/project-core.h"  // project_display_path ()

#include "edit-impl.h"
#include "editwidget.h"
#include "editwindow.h"

#include "editnav.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    char *file;   // absolute
    long line;    // from 1
    long column;  // bytes into the line
} edit_nav_place_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *edit_nav_places = NULL;  // edit_nav_place_t, the oldest first
static int edit_nav_at = -1;               // the place the editor is at in them

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
edit_nav_place_free (gpointer data)
{
    edit_nav_place_t *place = (edit_nav_place_t *) data;

    g_free (place->file);
    g_free (place);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_nav_same_file (const char *a, const char *b)
{
    char *ra, *rb;
    gboolean same;

    if (a == NULL || b == NULL)
        return FALSE;
    if (strcmp (a, b) == 0)
        return TRUE;
    ra = realpath (a, NULL);
    rb = realpath (b, NULL);
    same = ra != NULL && rb != NULL && strcmp (ra, rb) == 0;
    free (ra);
    free (rb);
    return same;
}

/* --------------------------------------------------------------------------------------------- */

/* The place of the cursor of @edit; NULL for a window with no file */
static edit_nav_place_t *
edit_nav_here (const WEdit *edit)
{
    edit_nav_place_t *place;

    if (edit == NULL || edit->filename == NULL)
        return NULL;
    place = g_new0 (edit_nav_place_t, 1);
    place->file = g_canonicalize_filename (edit->filename, NULL);
    place->line = edit->buffer.curs_line + 1;
    place->column = (long) (edit->buffer.curs1 - edit_buffer_get_current_bol (&edit->buffer));
    return place;
}

/* --------------------------------------------------------------------------------------------- */

/* The place @at taken by where @edit is: that is what Go forward or back returns to */
static void
edit_nav_record_here (const WEdit *edit, int at)
{
    edit_nav_place_t *here = edit_nav_here (edit);

    if (here == NULL || at < 0 || at >= (int) edit_nav_places->len)
    {
        if (here != NULL)
            edit_nav_place_free (here);
        return;
    }
    edit_nav_place_free (g_ptr_array_index (edit_nav_places, at));
    g_ptr_array_index (edit_nav_places, at) = here;
}

/* --------------------------------------------------------------------------------------------- */

/* The window of @file in the editor of @edit, NULL when none has it */
static WEdit *
edit_nav_window_of (const WEdit *edit, const char *file)
{
    const WGroup *g = CONST_GROUP (CONST_WIDGET (edit)->owner);
    GList *l;

    for (l = g->widgets; l != NULL; l = g_list_next (l))
        if (edit_widget_is_editor (CONST_WIDGET (l->data))
            && edit_nav_same_file (EDIT (l->data)->filename, file))
            return EDIT (l->data);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* To a place: the window of its file, the file loaded else */
static gboolean
edit_nav_go (WEdit *edit, const edit_nav_place_t *place)
{
    WDialog *h = DIALOG (CONST_WIDGET (edit)->owner);
    WEdit *target = edit_nav_window_of (edit, place->file);
    off_t bol, eol;

    if (target != NULL)
        edit_window_show (EDIT_WINDOW (target));
    else
    {
        edit_arg_t arg;
        gboolean ok;

        edit_arg_init (&arg, g_strdup (place->file), place->line);
        // a file changed here is not let go: the other one in a window of its own
        if (edit->modified == 0)
        {
            ok = edit_reload_line (edit, &arg);
            target = edit;
        }
        else
        {
            ok = edit_load_file_from_filename (h, &arg);
            target = edit_nav_window_of (edit, place->file);
        }
        g_free (arg.file_name);
        if (!ok || target == NULL)
            return FALSE;
    }
    edit_move_to_line (target, place->line - 1);
    bol = edit_buffer_get_current_bol (&target->buffer);
    eol = edit_buffer_get_current_eol (&target->buffer);
    edit_cursor_move (target, MIN (bol + place->column, eol) - target->buffer.curs1);
    target->search_start = target->buffer.curs1;
    target->prev_col = edit_get_col (target);
    target->force |= REDRAW_PAGE;
    edit_scroll_screen_over_cursor (target);
    widget_draw (WIDGET (h));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* The text of the line of a place, to show: in its window, else in its file, the lines of which
   @files keeps; the tabs spaces, what the terminal would take as a control '?' */
static char *
edit_nav_line_text (const WEdit *edit, const edit_nav_place_t *place, GHashTable *files)
{
    const WEdit *window = edit_nav_window_of (edit, place->file);
    GString *line = g_string_new (NULL);
    char *p;

    if (window != NULL)
    {
        off_t at = edit_buffer_get_forward_offset (&window->buffer, 0, place->line - 1, 0);

        for (; at < window->buffer.size; at++)
        {
            const int c = edit_buffer_get_byte (&window->buffer, at);

            if (c == '\n')
                break;
            g_string_append_c (line, (char) c);
        }
    }
    else
    {
        char **lines = g_hash_table_lookup (files, place->file);

        if (lines == NULL)
        {
            char *text = NULL;

            lines = g_file_get_contents (place->file, &text, NULL, NULL)
                ? g_strsplit (text, "\n", -1)
                : g_new0 (char *, 1);
            g_free (text);
            g_hash_table_insert (files, g_strdup (place->file), lines);
        }
        if (g_strv_length (lines) >= (guint) place->line)
            g_string_assign (line, lines[place->line - 1]);
    }
    for (p = line->str; *p != '\0'; p++)
        if (*p == '\t' || *p == '\r')
            *p = ' ';
        else if ((unsigned char) *p < 0x20 || *p == 0x7f)
            *p = '?';
    g_strstrip (line->str);
    p = g_strdup (line->str);
    g_string_free (line, TRUE);
    return p;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
edit_nav_jump (WEdit *edit, const char *file, long line, long column)
{
    edit_nav_place_t *from, *to;

    if (edit == NULL || file == NULL || line < 1)
        return FALSE;
    if (edit_nav_places == NULL)
        edit_nav_places = g_ptr_array_new_with_free_func (edit_nav_place_free);

    // what was ahead, past Go back, is no more
    if (edit_nav_at >= 0 && edit_nav_at + 1 < (int) edit_nav_places->len)
        g_ptr_array_set_size (edit_nav_places, (guint) edit_nav_at + 1);
    from = edit_nav_here (edit);
    if (from != NULL)
    {
        const edit_nav_place_t *last = edit_nav_places->len > 0
            ? g_ptr_array_index (edit_nav_places, edit_nav_places->len - 1)
            : NULL;

        if (last != NULL && strcmp (last->file, from->file) == 0 && last->line == from->line)
            g_ptr_array_remove_index (edit_nav_places, edit_nav_places->len - 1);
        g_ptr_array_add (edit_nav_places, from);
    }
    to = g_new0 (edit_nav_place_t, 1);
    to->file = g_canonicalize_filename (file, NULL);
    to->line = line;
    to->column = MAX (column, 0);
    g_ptr_array_add (edit_nav_places, to);
    while (edit_nav_places->len > EDIT_NAV_MAX)
        g_ptr_array_remove_index (edit_nav_places, 0);
    edit_nav_at = (int) edit_nav_places->len - 1;

    if (!edit_nav_go (edit, to))
    {
        // where it did not go is not recorded
        g_ptr_array_remove_index (edit_nav_places, edit_nav_places->len - 1);
        edit_nav_at = (int) edit_nav_places->len - 1;
        return FALSE;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_nav_back (WEdit *edit)
{
    if (edit_nav_places == NULL || edit_nav_at <= 0)
        return FALSE;
    edit_nav_record_here (edit, edit_nav_at);
    edit_nav_at--;
    return edit_nav_go (edit, g_ptr_array_index (edit_nav_places, edit_nav_at));
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_nav_forward (WEdit *edit)
{
    if (edit_nav_places == NULL || edit_nav_at + 1 >= (int) edit_nav_places->len)
        return FALSE;
    edit_nav_record_here (edit, edit_nav_at);
    edit_nav_at++;
    return edit_nav_go (edit, g_ptr_array_index (edit_nav_places, edit_nav_at));
}

/* --------------------------------------------------------------------------------------------- */

void
edit_nav_history (WEdit *edit)
{
    GPtrArray *rows;
    GHashTable *files;
    Listbox *list;
    int width = 40, chosen;
    guint i;

    if (edit_nav_places == NULL || edit_nav_places->len == 0)
    {
        message (D_NORMAL, _ ("Navigation history"), "%s",
                 _ ("No jump yet: Go to definition, Find references and the like record "
                    "the places they go from and to."));
        return;
    }
    // where the editor is now, in its place
    edit_nav_record_here (edit, edit_nav_at);
    rows = g_ptr_array_new_with_free_func (g_free);
    files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_strfreev);
    for (i = 0; i < edit_nav_places->len; i++)
    {
        const edit_nav_place_t *place = g_ptr_array_index (edit_nav_places, i);
        char *name = project_display_path (place->file, edit_startup_option ("debug"));
        char *where = g_strdup_printf ("%s:%ld", name, place->line);
        char *text = edit_nav_line_text (edit, place, files);
        char *row = g_strdup_printf ("%-32s %s", where, text != NULL ? text : "");

        width = MAX (width, str_term_width1 (row) + 2);
        g_ptr_array_add (rows, row);
        g_free (text);
        g_free (where);
        g_free (name);
    }
    list = listbox_window_new (MIN ((int) rows->len, LINES - 8), MIN (width, COLS - 8),
                               _ ("Navigation history"), NULL);
    for (i = 0; i < rows->len; i++)
        LISTBOX_APPEND_TEXT (
            list, 0,
            str_fit_to_term (g_ptr_array_index (rows, i), MIN (width, COLS - 8) - 2, J_LEFT_FIT),
            NULL, FALSE);
    g_hash_table_destroy (files);
    listbox_set_current (list->list, edit_nav_at);
    chosen = listbox_run (list);
    g_ptr_array_free (rows, TRUE);
    if (chosen >= 0 && chosen != edit_nav_at)
    {
        edit_nav_at = chosen;
        (void) edit_nav_go (edit, g_ptr_array_index (edit_nav_places, edit_nav_at));
    }
}

/* --------------------------------------------------------------------------------------------- */

void
edit_nav_free (void)
{
    if (edit_nav_places != NULL)
        g_ptr_array_free (edit_nav_places, TRUE);
    edit_nav_places = NULL;
    edit_nav_at = -1;
}

/* --------------------------------------------------------------------------------------------- */
