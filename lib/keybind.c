/*
   Definitions of key bindings.

   Copyright (C) 2005-2026
   Free Software Foundation, Inc.

   Written by:
   Vitja Makarov, 2005
   Ilia Maslakov <il.smind@gmail.com>, 2009-2012, 2014, 2026
   Andrew Borodin <aborodin@vmail.ru>, 2009-2020

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "lib/global.h"
#include "lib/tty/key.h"  // KEY_M_
#include "lib/keybind.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define ADD_KEYMAP_NAME(name)            { #name, CK_##name, NULL }
#define ADD_KEYMAP_NAME_DESC(name, desc) { #name, CK_##name, desc }

/*** file scope type declarations ****************************************************************/

typedef struct name_keymap_t
{
    const char *name;
    long val;
    const char *description; /* N_() translatable, NULL = use name */
} name_keymap_t;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static name_keymap_t command_names[] = {
    // common
    ADD_KEYMAP_NAME (InsertChar),
    ADD_KEYMAP_NAME (Enter),
    ADD_KEYMAP_NAME (Up),
    ADD_KEYMAP_NAME (Down),
    ADD_KEYMAP_NAME (Left),
    ADD_KEYMAP_NAME (Right),
    ADD_KEYMAP_NAME (Home),
    ADD_KEYMAP_NAME (End),
    ADD_KEYMAP_NAME (PageUp),
    ADD_KEYMAP_NAME (PageDown),
    ADD_KEYMAP_NAME_DESC (HalfPageUp, N_ ("Half page up")),
    ADD_KEYMAP_NAME_DESC (HalfPageDown, N_ ("Half page down")),
    ADD_KEYMAP_NAME_DESC (Top, N_ ("Go to top")),
    ADD_KEYMAP_NAME_DESC (Bottom, N_ ("Go to bottom")),
    ADD_KEYMAP_NAME_DESC (TopOnScreen, N_ ("Top of screen")),
    ADD_KEYMAP_NAME_DESC (BottomOnScreen, N_ ("Bottom of screen")),
    ADD_KEYMAP_NAME_DESC (WordLeft, N_ ("Word left")),
    ADD_KEYMAP_NAME_DESC (WordRight, N_ ("Word right")),
    ADD_KEYMAP_NAME_DESC (Copy, N_ ("Copy file(s)")),
    ADD_KEYMAP_NAME_DESC (Move, N_ ("Move/rename file(s)")),
    ADD_KEYMAP_NAME_DESC (Delete, N_ ("Delete")),
    ADD_KEYMAP_NAME (Remove),
    ADD_KEYMAP_NAME (BackSpace),
    ADD_KEYMAP_NAME (Redo),
    ADD_KEYMAP_NAME (Clear),
    ADD_KEYMAP_NAME_DESC (ClearAll, N_ ("Clear screen and scrollback")),
    ADD_KEYMAP_NAME_DESC (Menu, N_ ("Main menu")),
    ADD_KEYMAP_NAME_DESC (UserMenu, N_ ("User-defined menu")),
    ADD_KEYMAP_NAME_DESC (EditUserMenu, N_ ("Edit user menu")),
    ADD_KEYMAP_NAME (Search),
    ADD_KEYMAP_NAME_DESC (SearchContinue, N_ ("Search next")),
    ADD_KEYMAP_NAME (Replace),
    ADD_KEYMAP_NAME_DESC (ReplaceContinue, N_ ("Replace next")),
    ADD_KEYMAP_NAME_DESC (Help, N_ ("Show help")),
    ADD_KEYMAP_NAME_DESC (Shell, N_ ("Terminal")),
    ADD_KEYMAP_NAME_DESC (Edit, N_ ("Edit file")),
    ADD_KEYMAP_NAME_DESC (EditNew, N_ ("Edit new file")),
    ADD_KEYMAP_NAME_DESC (SelectCodepage, N_ ("Select charset")),
    ADD_KEYMAP_NAME_DESC (History, N_ ("Command history")),
    ADD_KEYMAP_NAME_DESC (HistoryNext, N_ ("Next in history")),
    ADD_KEYMAP_NAME_DESC (HistoryPrev, N_ ("Previous in history")),
    ADD_KEYMAP_NAME_DESC (Complete, N_ ("Auto-complete")),
    ADD_KEYMAP_NAME (Save),
    ADD_KEYMAP_NAME_DESC (SaveAs, N_ ("Save as...")),
    ADD_KEYMAP_NAME_DESC (Goto, N_ ("Go to line")),
    ADD_KEYMAP_NAME_DESC (Refresh, N_ ("Refresh screen")),
    ADD_KEYMAP_NAME_DESC (Suspend, N_ ("Suspend to shell")),
    ADD_KEYMAP_NAME_DESC (ScreenList, N_ ("List open screens")),
    ADD_KEYMAP_NAME_DESC (ScreenNext, N_ ("Next screen")),
    ADD_KEYMAP_NAME_DESC (ScreenPrev, N_ ("Previous screen")),
    ADD_KEYMAP_NAME_DESC (FileNext, N_ ("Next file")),
    ADD_KEYMAP_NAME_DESC (FilePrev, N_ ("Previous file")),
    ADD_KEYMAP_NAME_DESC (DeleteToHome, N_ ("Delete to line start")),
    ADD_KEYMAP_NAME_DESC (DeleteToEnd, N_ ("Delete to line end")),
    ADD_KEYMAP_NAME_DESC (DeleteToWordBegin, N_ ("Delete to word start")),
    ADD_KEYMAP_NAME_DESC (DeleteToWordEnd, N_ ("Delete to word end")),
    ADD_KEYMAP_NAME_DESC (Cut, N_ ("Cut to clipboard")),
    ADD_KEYMAP_NAME_DESC (Store, N_ ("Copy to clipboard")),
    ADD_KEYMAP_NAME_DESC (Paste, N_ ("Paste from clipboard")),
    ADD_KEYMAP_NAME_DESC (Mark, N_ ("Toggle selection")),
    ADD_KEYMAP_NAME_DESC (MarkLeft, N_ ("Select left")),
    ADD_KEYMAP_NAME_DESC (MarkRight, N_ ("Select right")),
    ADD_KEYMAP_NAME_DESC (MarkUp, N_ ("Select up")),
    ADD_KEYMAP_NAME_DESC (MarkDown, N_ ("Select down")),
    ADD_KEYMAP_NAME_DESC (MarkToWordBegin, N_ ("Select to word start")),
    ADD_KEYMAP_NAME_DESC (MarkToWordEnd, N_ ("Select to word end")),
    ADD_KEYMAP_NAME_DESC (MarkToHome, N_ ("Select to line start")),
    ADD_KEYMAP_NAME_DESC (MarkToEnd, N_ ("Select to line end")),
    ADD_KEYMAP_NAME_DESC (Sort, N_ ("Sort order...")),
    ADD_KEYMAP_NAME_DESC (Options, N_ ("General options")),
    ADD_KEYMAP_NAME_DESC (LearnKeys, N_ ("Learn terminal keys")),
    ADD_KEYMAP_NAME_DESC (KeyBindings, N_ ("Key bindings")),
    ADD_KEYMAP_NAME_DESC (KeySniffer, N_ ("Key sniffer")),
    ADD_KEYMAP_NAME_DESC (ManagePlugins, N_ ("Manage plugins")),
    ADD_KEYMAP_NAME_DESC (Bookmark, N_ ("Set bookmark")),
    ADD_KEYMAP_NAME_DESC (Quit, N_ ("Quit program")),
    ADD_KEYMAP_NAME_DESC (ExtendedKeyMap, N_ ("Extended key prefix")),

    ADD_KEYMAP_NAME_DESC (Find, N_ ("Find declaration")),
    ADD_KEYMAP_NAME_DESC (OptionsAppearance, N_ ("Appearance settings")),
    ADD_KEYMAP_NAME_DESC (SaveSetup, N_ ("Save setup")),
    ADD_KEYMAP_NAME (Select),

    // dialog
    ADD_KEYMAP_NAME (Ok),
    ADD_KEYMAP_NAME (Cancel),

    // input line
    ADD_KEYMAP_NAME_DESC (Yank, N_ ("Yank (paste kill buffer)")),

    // help
    ADD_KEYMAP_NAME_DESC (Index, N_ ("Help index")),
    ADD_KEYMAP_NAME_DESC (Back, N_ ("Go back")),
    ADD_KEYMAP_NAME_DESC (LinkNext, N_ ("Next link")),
    ADD_KEYMAP_NAME_DESC (LinkPrev, N_ ("Previous link")),
    ADD_KEYMAP_NAME_DESC (NodeNext, N_ ("Next node")),
    ADD_KEYMAP_NAME_DESC (NodePrev, N_ ("Previous node")),

    // tree

    ADD_KEYMAP_NAME (ShowNumbers),

    // chattr dialog

    // the embedded terminal, and the editor which named them first
    ADD_KEYMAP_NAME_DESC (ScrollUp, N_ ("Scroll up")),
    ADD_KEYMAP_NAME_DESC (ScrollDown, N_ ("Scroll down")),
    ADD_KEYMAP_NAME_DESC (Unmark, N_ ("Deselect all")),
    ADD_KEYMAP_NAME_DESC (MarkPageUp, N_ ("Select page up")),
    ADD_KEYMAP_NAME_DESC (MarkPageDown, N_ ("Select page down")),

    ADD_KEYMAP_NAME_DESC (Close, N_ ("Close editor")),
    ADD_KEYMAP_NAME (Tab),
    ADD_KEYMAP_NAME (Undo),
    ADD_KEYMAP_NAME (Return),
    ADD_KEYMAP_NAME_DESC (ParagraphUp, N_ ("Paragraph up")),
    ADD_KEYMAP_NAME_DESC (ParagraphDown, N_ ("Paragraph down")),
    ADD_KEYMAP_NAME_DESC (EditFile, N_ ("Open file in editor")),
    ADD_KEYMAP_NAME_DESC (MarkWord, N_ ("Select word")),
    ADD_KEYMAP_NAME_DESC (MarkLine, N_ ("Select line")),
    ADD_KEYMAP_NAME_DESC (MarkAll, N_ ("Select all")),
    ADD_KEYMAP_NAME_DESC (MarkColumn, N_ ("Column selection mode")),
    ADD_KEYMAP_NAME_DESC (BlockSave, N_ ("Save selected block")),
    ADD_KEYMAP_NAME_DESC (InsertFile, N_ ("Insert file")),
    ADD_KEYMAP_NAME_DESC (InsertOverwrite, N_ ("Toggle insert/overwrite")),
    ADD_KEYMAP_NAME_DESC (Date, N_ ("Insert date/time (Lua action)")),
    ADD_KEYMAP_NAME_DESC (DeleteLine, N_ ("Delete line")),
    ADD_KEYMAP_NAME_DESC (UndoHistory, N_ ("Undo history")),
    ADD_KEYMAP_NAME_DESC (ParagraphFormat, N_ ("Format paragraph (Lua action)")),
    ADD_KEYMAP_NAME_DESC (MatchBracket, N_ ("Match bracket")),
    ADD_KEYMAP_NAME_DESC (Sort, N_ ("Sort selection (Lua action)")),
    ADD_KEYMAP_NAME_DESC (ExternalCommand, N_ ("Insert command output (Lua action)")),
    ADD_KEYMAP_NAME_DESC (MacroStartRecord, N_ ("Start macro recording")),
    ADD_KEYMAP_NAME_DESC (MacroStopRecord, N_ ("Stop macro recording")),
    ADD_KEYMAP_NAME_DESC (MacroStartStopRecord, N_ ("Toggle macro recording")),
    ADD_KEYMAP_NAME_DESC (MacroExplorer, N_ ("Macro explorer")),
    ADD_KEYMAP_NAME_DESC (RepeatStartStopRecord, N_ ("Repeat last macro")),
    ADD_KEYMAP_NAME_DESC (SpellCheck, N_ ("Spell check")),
    ADD_KEYMAP_NAME_DESC (SpellCheckCurrentWord, N_ ("Spell check current word")),
    ADD_KEYMAP_NAME_DESC (SpellCheckSelectLang, N_ ("Select spell language")),
    ADD_KEYMAP_NAME_DESC (BookmarkFlush, N_ ("Clear all bookmarks")),
    ADD_KEYMAP_NAME_DESC (BookmarkNext, N_ ("Next bookmark")),
    ADD_KEYMAP_NAME_DESC (BookmarkPrev, N_ ("Previous bookmark")),
    ADD_KEYMAP_NAME_DESC (FoldToggle, N_ ("Toggle code fold")),
    ADD_KEYMAP_NAME_DESC (UnfoldAll, N_ ("Unfold all")),
    ADD_KEYMAP_NAME_DESC (FilterToggle, N_ ("Toggle line filter")),
    ADD_KEYMAP_NAME_DESC (FilterWord, N_ ("Filter by selection or word")),
    ADD_KEYMAP_NAME_DESC (QuickFilter, N_ ("Quick filter")),
    ADD_KEYMAP_NAME_DESC (MarkToFileBegin, N_ ("Select to file start")),
    ADD_KEYMAP_NAME_DESC (MarkToFileEnd, N_ ("Select to file end")),
    ADD_KEYMAP_NAME_DESC (MarkToPageBegin, N_ ("Select to page start")),
    ADD_KEYMAP_NAME_DESC (MarkToPageEnd, N_ ("Select to page end")),
    ADD_KEYMAP_NAME_DESC (MarkScrollUp, N_ ("Select scroll up")),
    ADD_KEYMAP_NAME_DESC (MarkScrollDown, N_ ("Select scroll down")),
    ADD_KEYMAP_NAME_DESC (MarkParagraphUp, N_ ("Select paragraph up")),
    ADD_KEYMAP_NAME_DESC (MarkParagraphDown, N_ ("Select paragraph down")),
    ADD_KEYMAP_NAME_DESC (MarkColumnPageUp, N_ ("Column select page up")),
    ADD_KEYMAP_NAME_DESC (MarkColumnPageDown, N_ ("Column select page down")),
    ADD_KEYMAP_NAME_DESC (MarkColumnLeft, N_ ("Column select left")),
    ADD_KEYMAP_NAME_DESC (MarkColumnRight, N_ ("Column select right")),
    ADD_KEYMAP_NAME_DESC (MarkColumnUp, N_ ("Column select up")),
    ADD_KEYMAP_NAME_DESC (MarkColumnDown, N_ ("Column select down")),
    ADD_KEYMAP_NAME_DESC (MarkColumnScrollUp, N_ ("Column select scroll up")),
    ADD_KEYMAP_NAME_DESC (MarkColumnScrollDown, N_ ("Column select scroll down")),
    ADD_KEYMAP_NAME_DESC (MarkColumnParagraphUp, N_ ("Column select paragraph up")),
    ADD_KEYMAP_NAME_DESC (MarkColumnParagraphDown, N_ ("Column select paragraph down")),
    ADD_KEYMAP_NAME_DESC (BlockShiftLeft, N_ ("Shift block left")),
    ADD_KEYMAP_NAME_DESC (BlockShiftRight, N_ ("Shift block right")),
    ADD_KEYMAP_NAME_DESC (InsertLiteral, N_ ("Insert literal character (Lua action)")),
    ADD_KEYMAP_NAME_DESC (ShowTabTws, N_ ("Toggle visible tabs/spaces")),
    ADD_KEYMAP_NAME_DESC (ShowControlChars, N_ ("Toggle control characters")),
    ADD_KEYMAP_NAME_DESC (SyntaxOnOff, N_ ("Toggle syntax highlighting")),
    ADD_KEYMAP_NAME_DESC (SyntaxChoose, N_ ("Choose syntax highlighting")),
    ADD_KEYMAP_NAME_DESC (ShowMargin, N_ ("Toggle right margin")),
    ADD_KEYMAP_NAME_DESC (OptionsSaveMode, N_ ("Save mode options")),
    ADD_KEYMAP_NAME_DESC (About, N_ ("About editor")),
    // An action to run external script from macro
    { "ExecuteScript", CK_PipeBlock (0), N_ ("Execute script") },
    ADD_KEYMAP_NAME_DESC (WindowMove, N_ ("Move window")),
    ADD_KEYMAP_NAME_DESC (WindowResize, N_ ("Resize window")),
    ADD_KEYMAP_NAME_DESC (WindowFullscreen, N_ ("Toggle fullscreen")),
    ADD_KEYMAP_NAME_DESC (WindowList, N_ ("Window list")),
    ADD_KEYMAP_NAME_DESC (WindowSticky, N_ ("Toggle sticky windows")),
    ADD_KEYMAP_NAME_DESC (WindowNext, N_ ("Next window")),
    ADD_KEYMAP_NAME_DESC (WindowPrev, N_ ("Previous window")),
    ADD_KEYMAP_NAME_DESC (DebugStartContinue, N_ ("Start or continue debugging")),
    ADD_KEYMAP_NAME_DESC (DebugPause, N_ ("Pause debugging")),
    ADD_KEYMAP_NAME_DESC (DebugStepInto, N_ ("Step into")),
    ADD_KEYMAP_NAME_DESC (DebugStepOver, N_ ("Step over")),
    ADD_KEYMAP_NAME_DESC (DebugStepOut, N_ ("Step out")),
    ADD_KEYMAP_NAME_DESC (DebugStop, N_ ("Stop debugging")),
    ADD_KEYMAP_NAME_DESC (DebugClose, N_ ("Close debug session window")),

    { NULL, CK_IgnoreKey, NULL }
};

static const size_t num_command_names = G_N_ELEMENTS (command_names) - 1;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
name_keymap_comparator (const void *p1, const void *p2)
{
    const name_keymap_t *m1 = (const name_keymap_t *) p1;
    const name_keymap_t *m2 = (const name_keymap_t *) p2;

    return g_ascii_strcasecmp (m1->name, m2->name);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
sort_command_names (void)
{
    static gboolean has_been_sorted = FALSE;

    if (!has_been_sorted)
    {
        qsort (command_names, num_command_names, sizeof (command_names[0]),
               &name_keymap_comparator);
        has_been_sorted = TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
keymap_add (GArray *keymap, long key, long cmd, const char *caption)
{
    if (key != 0 && cmd != CK_IgnoreKey)
    {
        global_keymap_t new_bind;

        new_bind.key = key;
        new_bind.command = cmd;
        g_snprintf (new_bind.caption, sizeof (new_bind.caption), "%s", caption);
        g_array_append_val (keymap, new_bind);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
keybind_cmd_bind (GArray *keymap, const char *keybind, long action)
{
    char *caption = NULL;
    int key;

    key = tty_keyname_to_keycode (keybind, &caption);
    keymap_add (keymap, key, action, caption);
    g_free (caption);
}

/* --------------------------------------------------------------------------------------------- */

long
keybind_lookup_action (const char *name)
{
    const name_keymap_t key = { name, 0, NULL };
    name_keymap_t *res;

    sort_command_names ();

    res = bsearch (&key, command_names, num_command_names, sizeof (command_names[0]),
                   name_keymap_comparator);

    return (res != NULL) ? res->val : CK_IgnoreKey;
}

/* --------------------------------------------------------------------------------------------- */

const char *
keybind_lookup_actionname (long action)
{
    size_t i;

    for (i = 0; command_names[i].name != NULL; i++)
        if (command_names[i].val == action)
            return command_names[i].name;

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
keybind_lookup_actiondesc (long action)
{
    size_t i;

    for (i = 0; command_names[i].name != NULL; i++)
        if (command_names[i].val == action)
            return command_names[i].description;

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
keybind_lookup_keymap_shortcut (const global_keymap_t *keymap, long action)
{
    if (keymap != NULL)
    {
        size_t i;

        for (i = 0; keymap[i].key != 0; i++)
            if (keymap[i].command == action)
                return (keymap[i].caption[0] != '\0') ? keymap[i].caption : NULL;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

long
keybind_lookup_keymap_command (const global_keymap_t *keymap, long key)
{
    if (keymap != NULL)
    {
        size_t i;

        for (i = 0; keymap[i].key != 0; i++)
            if (keymap[i].key == key)
                return keymap[i].command;
    }

    return CK_IgnoreKey;
}

/* --------------------------------------------------------------------------------------------- */
