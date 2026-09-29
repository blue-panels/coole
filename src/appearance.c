/*
   The Appearance dialog: the skin and the shadows.

   Copyright (C) 1994-2025
   Free Software Foundation, Inc.

   Written by:
   Miguel de Icaza, 1994, 1995
   Jakub Jelinek, 1995
   Andrew T. Veliath, 1995
   Slava Zanko <slavazanko@gmail.com>, 2013
   Andrew Borodin <aborodin@vmail.ru>, 2010-2022

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

/** \file appearance.c
 *  \brief Source: the Appearance dialog: the skin and the shadows
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/tty/color.h"  // tty_use_colors()
#include "lib/skin.h"
#include "lib/mcconfig.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "appearance.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define SKIN_NAME_DISPLAY_LEN 25

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *skin_names;
static gchar *current_skin_name;

static unsigned long shadows_id;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* after the skin engine has been reloaded: the color tables the widgets cache, then the screen */

static void
skin_apply (const gchar *skin_override)
{
    GError *mcerror = NULL;

    mc_skin_deinit ();
    mc_skin_init (skin_override, &mcerror);
    dlg_set_default_colors ();
    input_set_default_colors ();
    repaint_screen ();

    mc_error_message (&mcerror, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static const gchar *
skin_name_to_label (const gchar *name)
{
    if (strcmp (name, "default") == 0)
        return _ ("< Default >");
    return name;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
skin_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_RESIZE:
    {
        WDialog *d = DIALOG (w);
        const WRect *wd = &WIDGET (d->data.p)->rect;
        WRect r = w->rect;

        r.y = wd->y + (wd->lines - r.lines) / 2;
        r.x = wd->x + wd->cols / 2;

        return dlg_default_callback (w, NULL, MSG_RESIZE, 0, &r);
    }

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
sel_skin_button (WButton *button, int action)
{
    int result;
    WListbox *skin_list;
    WDialog *skin_dlg;
    const gchar *skin_name;
    unsigned int i;
    unsigned int pos = 1;

    (void) action;

    skin_dlg = dlg_create (TRUE, 0, 0, 13, SKIN_NAME_DISPLAY_LEN + 4, WPOS_KEEP_DEFAULT, TRUE,
                           dialog_colors, skin_dlg_callback, NULL, "[Appearance]", _ ("Skins"));
    // use Appearance dialog for positioning
    skin_dlg->data.p = WIDGET (button)->owner;

    // set dialog location before all
    send_message (skin_dlg, NULL, MSG_RESIZE, 0, NULL);

    skin_list = listbox_new (1, 1, 11, SKIN_NAME_DISPLAY_LEN + 2, FALSE, NULL);
    skin_name = "default";
    listbox_add_item (skin_list, LISTBOX_APPEND_AT_END, 0, skin_name_to_label (skin_name),
                      (void *) skin_name, FALSE);

    if (strcmp (skin_name, current_skin_name) == 0)
        listbox_set_current (skin_list, 0);

    for (i = 0; i < skin_names->len; i++)
    {
        skin_name = g_ptr_array_index (skin_names, i);
        if (strcmp (skin_name, "default") != 0)
        {
            listbox_add_item (skin_list, LISTBOX_APPEND_AT_END, 0, skin_name_to_label (skin_name),
                              (void *) skin_name, FALSE);
            if (strcmp (skin_name, current_skin_name) == 0)
                listbox_set_current (skin_list, pos);
            pos++;
        }
    }

    // make list stick to all sides of dialog, effectively make it be resized with dialog
    group_add_widget_autopos (GROUP (skin_dlg), skin_list, WPOS_KEEP_ALL, NULL);

    result = dlg_run (skin_dlg);
    if (result == B_ENTER)
    {
        gchar *skin_label;

        listbox_get_current (skin_list, &skin_label, (void **) &skin_name);
        g_free (current_skin_name);
        current_skin_name = g_strdup (skin_name);
        skin_apply (skin_name);

        button_set_text (button, str_fit_to_term (skin_label, SKIN_NAME_DISPLAY_LEN, J_LEFT_FIT));
    }
    widget_destroy (WIDGET (skin_dlg));

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
appearance_box_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
#ifdef ENABLE_SHADOWS
        if (!tty_use_colors ())
#endif
        {
            Widget *shadow;

            shadow = widget_find_by_id (w, shadows_id);
            CHECK (shadow)->state = FALSE;
            widget_disable (shadow, TRUE);
        }
        return MSG_HANDLED;

    case MSG_NOTIFY:
        if (sender != NULL && sender->id == shadows_id)
        {
            mc_global.tty.shadows = CHECK (sender)->state;
            repaint_screen ();
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
appearance_box (void)
{
    const gboolean shadows = mc_global.tty.shadows;

    current_skin_name = g_strdup (mc_skin__default.name);
    skin_names = mc_skin_list ();

    {
        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_START_COLUMNS,
                QUICK_LABEL (_ ("Skin:"), NULL),
            QUICK_NEXT_COLUMN,
                QUICK_BUTTON (str_fit_to_term (skin_name_to_label (current_skin_name), SKIN_NAME_DISPLAY_LEN,
                              J_LEFT_FIT), B_USER, sel_skin_button, NULL),
            QUICK_STOP_COLUMNS,
            QUICK_SEPARATOR (TRUE),
            QUICK_CHECKBOX (_ ("&Shadows"), &mc_global.tty.shadows, &shadows_id),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
            // clang-format on
        };

        WRect r = { -1, -1, 0, 54 };

        quick_dialog_t qdlg = {
            .rect = r,
            .title = _ ("Appearance"),
            .help = "[Appearance]",
            .widgets = quick_widgets,
            .callback = appearance_box_callback,
            .mouse_callback = NULL,
        };

        if (quick_dialog (&qdlg) == B_ENTER)
            mc_config_set_string (mc_global.main_config, CONFIG_APP_SECTION, "skin",
                                  current_skin_name);
        else
        {
            skin_apply (NULL);
            mc_global.tty.shadows = shadows;
        }
    }

    g_free (current_skin_name);
    g_ptr_array_free (skin_names, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
