#ifndef MC__KEYBIND_H
#define MC__KEYBIND_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/* keymap sections */
#define KEYMAP_SECTION_DIALOG     "dialog"
#define KEYMAP_SECTION_MENU       "menu"
#define KEYMAP_SECTION_INPUT      "input"
#define KEYMAP_SECTION_LISTBOX    "listbox"
#define KEYMAP_SECTION_RADIO      "radio"
#define KEYMAP_SECTION_HELP       "help"
#define KEYMAP_SECTION_EDITOR     "editor"
#define KEYMAP_SECTION_EDITOR_EXT "editor:xmap"

#define KEYMAP_SHORTCUT_LENGTH    32  // FIXME: is 32 bytes enough for shortcut?

#define CK_PipeBlock(i)           (10000 + (i))
#define CK_Macro(i)               (20000 + (i))
#define CK_MacroLast              CK_Macro (0x7FFF)

/*** enums ***************************************************************************************/

enum
{
    // special commands
    CK_InsertChar = -1L,
    CK_IgnoreKey = 0L,

    // common
    CK_Enter = 1L,
    CK_Up,
    CK_Down,
    CK_Left,
    CK_Right,
    CK_Home,
    CK_End,
    CK_PageUp,
    CK_PageDown,
    CK_HalfPageUp,
    CK_HalfPageDown,
    CK_Top,
    CK_Bottom,
    CK_TopOnScreen,
    CK_BottomOnScreen,
    CK_WordLeft,
    CK_WordRight,
    CK_Copy,
    CK_Move,
    CK_Delete,
    CK_Remove,
    CK_BackSpace,
    CK_Redo,
    CK_Clear,
    CK_Menu,
    CK_UserMenu,
    CK_EditUserMenu,
    CK_Search,
    CK_SearchContinue,
    CK_Replace,
    CK_ReplaceContinue,
    CK_Help,
    CK_Edit,
    CK_EditNew,
    CK_Shell,
    CK_SelectCodepage,
    CK_History,
    CK_HistoryNext,
    CK_HistoryPrev,
    CK_Complete,
    CK_Save,
    CK_SaveAs,
    CK_Goto,
    CK_Refresh,
    CK_Suspend,
    CK_Mark,
    CK_ScreenList,
    CK_ScreenNext,
    CK_ScreenPrev,
    CK_FilePrev,
    CK_FileNext,
    CK_DeleteToHome,
    CK_DeleteToEnd,
    CK_DeleteToWordBegin,
    CK_DeleteToWordEnd,
    CK_ShowNumbers,
    CK_Store,
    CK_Cut,
    CK_Paste,
    CK_MarkLeft,
    CK_MarkRight,
    CK_MarkUp,
    CK_MarkDown,
    CK_MarkToWordBegin,
    CK_MarkToWordEnd,
    CK_MarkToHome,
    CK_MarkToEnd,
    CK_Sort,
    CK_Options,
    CK_LearnKeys,
    CK_KeyBindings,
    CK_KeySniffer,
    CK_ManagePlugins,
    CK_Bookmark,
    CK_Quit,
    // C-x or similar
    CK_ExtendedKeyMap,

    CK_Find,
    CK_OptionsAppearance,
    CK_SaveSetup,
    CK_Select,

    // dialog
    CK_Ok = 300L,
    CK_Cancel,

    // input
    CK_Yank = 350L,

    // help
    CK_Index = 400L,
    CK_Back,
    CK_LinkNext,
    CK_LinkPrev,
    CK_NodeNext,
    CK_NodePrev,

    // editor
    // cursor movements
    CK_Tab = 500L,
    CK_Undo,
    CK_ScrollUp,
    CK_ScrollDown,
    CK_Return,
    CK_ParagraphUp,
    CK_ParagraphDown,
    // file commands
    CK_EditFile,
    CK_InsertFile,
    CK_EditSyntaxFile,
    CK_Close,
    // block commands
    CK_BlockSave,
    CK_BlockShiftLeft,
    CK_BlockShiftRight,
    CK_DeleteLine,
    // bookmarks
    CK_BookmarkFlush,
    CK_BookmarkNext,
    CK_BookmarkPrev,
    // code folding
    CK_FoldToggle,
    CK_UnfoldAll,
    CK_FilterToggle,
    CK_FilterWord,
    // mark commands
    CK_MarkColumn,
    CK_MarkWord,
    CK_MarkLine,
    CK_MarkAll,
    CK_Unmark,
    CK_MarkPageUp,
    CK_MarkPageDown,
    CK_MarkToFileBegin,
    CK_MarkToFileEnd,
    CK_MarkToPageBegin,
    CK_MarkToPageEnd,
    CK_MarkScrollUp,
    CK_MarkScrollDown,
    CK_MarkParagraphUp,
    CK_MarkParagraphDown,
    // column mark commands
    CK_MarkColumnPageUp,
    CK_MarkColumnPageDown,
    CK_MarkColumnLeft,
    CK_MarkColumnRight,
    CK_MarkColumnUp,
    CK_MarkColumnDown,
    CK_MarkColumnScrollUp,
    CK_MarkColumnScrollDown,
    CK_MarkColumnParagraphUp,
    CK_MarkColumnParagraphDown,
    // macros
    CK_MacroStartRecord,
    CK_MacroStopRecord,
    CK_MacroStartStopRecord,
    CK_MacroExplorer,
    CK_RepeatStartRecord,
    CK_RepeatStopRecord,
    CK_RepeatStartStopRecord,
    // window commands
    CK_WindowMove,
    CK_WindowResize,
    CK_WindowFullscreen,
    CK_WindowList,
    CK_WindowNext,
    CK_WindowPrev,
    // misc commands
    CK_SpellCheck,
    CK_SpellCheckCurrentWord,
    CK_SpellCheckSelectLang,
    CK_InsertOverwrite,
    CK_ParagraphFormat,
    CK_MatchBracket,
    CK_OptionsSaveMode,
    CK_About,
    CK_ShowMargin,
    CK_ShowTabTws,
    CK_ShowControlChars,
    CK_SyntaxOnOff,
    CK_SyntaxChoose,
    CK_EditPluginsInfo,
    CK_EditLuaScripts,
    CK_InsertLiteral,
    CK_ExternalCommand,
    CK_Date,
    CK_UndoHistory
};

/*** structures declarations (and typedefs of structures)*****************************************/

/* The global keymaps are of this type */
typedef struct global_keymap_t
{
    long key;
    long command;
    char caption[KEYMAP_SHORTCUT_LENGTH];
} global_keymap_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void keybind_cmd_bind (GArray *keymap, const char *keybind, long action);
long keybind_lookup_action (const char *name);
const char *keybind_lookup_actionname (long action);
const char *keybind_lookup_actiondesc (long action);
const char *keybind_lookup_keymap_shortcut (const global_keymap_t *keymap, long action);
long keybind_lookup_keymap_command (const global_keymap_t *keymap, long key);

/*** inline functions ****************************************************************************/

#endif
