---
date: September 2026
---

<!-- help:topics "Topics:" -->
# NAME <!-- help:skip -->

coole - a text editor.

# SYNOPSIS <!-- help:skip -->

**coole**
[options] [+lineno] [file1[:lineno]] [file2[:lineno]] ...

# DESCRIPTION

**coole**
is a console text editor. It continues
**cooledit**,
the editor by Paul Sheer, whose terminal version was developed for many years
as the internal editor of GNU Midnight Commander; coole carries that editor on
as a program of its own.

It edits several files at once, each in a window of its own, and it can edit
binary files. It has block copy, move, delete, cut and paste; key for key
undo and redo; pull-down menus; file insertion; macros; regular expression
search and replace; a line filter; folding; bookmarks; shift-arrow text
highlighting (where the terminal supports it); column blocks; insert and
overwrite modes; autoindent; word wrap and paragraph formatting; a tunable
tab size; syntax highlighting for many file types; a user menu that pipes the
text through shell commands; editor plugins (tags navigation, spell
checking) and Lua scripts.

Every file named on the command line is opened in a window of its own. With
no file, the editor opens an empty window. A file larger than
*editor_filesize_threshold*
(64 MB by default) is opened after a question, not refused.

# OPTIONS

*+lineno*
: Go to the line specified by number (do not put a space between the
*+*
sign and the number). Several line numbers are allowed but only the last one
is used, and it is applied to the first file only.

*file:lineno[:]*
: Open the file at that line, the way compilers and grep print a place in a
file. When a file of the whole name, colon and number included, exists, that
file is opened instead.

*-V, --version*
: Display the version of the program.

*-f, --datadir*
: Display the compiled-in search path for the data files of coole.

*-F, --datadir-info*
: Display extended information about the directories coole uses: the data
files, the user files and the plugins.

*--configure-options*
: Display the options the program was configured with.

*--no-lua*
: Start without the Lua runtime: no Lua script is loaded.

*-D, --debug=directory*
: Open the project of that directory to debug it: the latest file of the
project, the tree of the project and the panel of the debugger at
the right. The file windows take the keys of the debugger: F6 puts a
breakpoint, F5 builds the program and runs it, making its debug
configuration the first time, F7 and F8 step, F3 indexes the symbols of the
project; F2, F9 and F10 stay Save, the menu and Quit, and Alt-Shift-G goes
to the panel and back. It needs the project, build and debugger plugins,
and ctags for F3.

*-h, -?, --help*
: Show the options and what they do.

*--help-terminal, --help-color*
: Show the terminal options or the color options below.

The terminal options:

*-x, --xterm*
: Force xterm mode. Used when running on xterm-capable terminals (two
screen modes, and able to send mouse escape sequences).

*-X, --no-x11*
: Do not use the X Window System to get the state of the modifier keys.

*-g, --oldmouse*
: Use the old highlight mouse tracking of the terminal.

*-d, --nomouse*
: Disable mouse support.

*-t, --termcap*
: Use the termcap database instead of terminfo. The option exists only when
coole was built with the S-Lang library.

*-s, --slow*
: Run on a slow terminal: the screen is drawn with fewer updates.

*-a, --stickchars*
: Draw the frames with plain characters instead of the line drawing
characters of the terminal.

*-K file, --keymap=file*
: Load the key bindings from that file. See
[Redefine key bindings](#keys_redefine).

*--nokeymap*
: Do not load any keymap file: use the key bindings built into the program.

The color options:

*-b, --nocolor*
: Force black and white display.

*-c, --color*
: Force color mode on terminals that don't seem to have color support.

*-S skin, --skin=skin*
: Use the named skin. See
[Skins](#skins).

# Overview

The screen of coole has the
[menu bar](#menu-bar)
at the top, the editor windows under it and the labels of the function keys at
the bottom. The menu bar may be hidden: F9 or a click on the top line of the
screen brings it up.

Every window shows one file: its name, the state of the file and the place of
the cursor stand in the status line at its top. Several windows may be open
at a time; the
[Window menu](#window-menu)
moves between them, and each of them can be moved and resized.

# The editor <a id="internal-file-editor"></a>

The editor is easy to use and can be used without learning. The pull-down
menu is invoked by pressing F9, and the keys of the commands stand next to
them in the menus and in the labels of the function keys.

Sections:
: [The keys](#keys)
: [The menus](#menu-bar)
: [Search, replace and the line filter](#search-and-replace)
: [Folding](#folding)
: [Macros](#macros)
: [The user menu](#edit-menu-file)
: [Code navigation](#code-navigation)
: [Syntax highlighting](#syntax-highlighting)
: [Editor options](#editor-options)
: [Settings in the ini file](#internal-file-editor-options)

Each file is opened in its own window in full-screen mode. Window control is
similar to the window control of other multi-window programs: a double click
on the window title maximizes the window to full screen or restores its size
and position; a left click on the title and a mouse drag move the window; a
left click on the lower right corner of the frame and a mouse drag resize it.
The same is done from the
[Window menu](#window-menu).
A window that is not full screen has a scrollbar down the right side of its
frame and one along the bottom, after the position of the cursor: an arrow
scrolls a line or a few columns, the bar beside the thumb a page, and the thumb
is dragged. Up and down the cursor goes along with the view; sideways the view
moves alone, as far as the widest line on the screen, and the next key or a
click on the text brings it back to the cursor. The bars are drawn as the
section
**[scrollbar]**
of the skin has them: the vertical thumb six cells long and the horizontal one
four, when they fit, by default. The horizontal bar
measures the lines in view, not the whole file. The wheel tilted, or turned
with Shift, scrolls sideways.

Shift combined with the arrows highlights text (if the terminal supports it).
**Ctrl-Ins**
copies to the clipboard file
**~/.local/share/coole/clipboard**,
**Shift-Ins**
pastes from it,
**Shift-Del**
cuts to it, and
**Ctrl-Del**
deletes the highlighted text. Mouse highlighting also works, and the mouse of
the terminal itself is had by holding down the Shift key while dragging; that
selection is not shared with the clipboard file of coole.

**Ctrl-o**
shows a window with your shell at the bottom of the screen, the file in a
window above it, and hides it again as it is, the file taking the whole screen
back; see [Terminal](#terminal).

# Keys

Some commands involve the use of the
*Control*
(sometimes labeled CTRL or CTL) and the
*Meta*
(sometimes labeled ALT or even Compose) keys. In this manual we will
use the following abbreviations:

**Ctrl-\<chr>**
: means hold the Control key while typing the character \<chr>.
Thus Ctrl-f would be: hold the Control key and type f.

**Alt-\<chr>**
: means hold the Meta or Alt key down while typing \<chr>.
If there is no Meta or Alt key, type
*Esc,*
release it, then type the character \<chr>.

**Shift-\<chr>**
: means hold the Shift key down while typing \<chr>.

These are the default keys of the editor. Every one of them can be changed:
see
[Redefine key bindings](#keys_redefine).

## Files and windows

**F2**
: Save the file.

**F12, Ctrl-F2**
: Save the file under another name. See
[Save As](#save-file-as).

**Ctrl-n**
: Open a new, empty window.

**F15**
: Insert a file at the cursor.

**Ctrl-f**
: Copy the highlighted block to a file.

**Alt-Shift-e**
: The history of the files that were edited, to open one of them again.

**Alt-e**
: Choose the encoding of the text. See
[Choose codepage](#codepages-translation).

**F10, Esc**
: Close the window; the editor ends with its last window.

**F1**
: This help.

**F9**
: The menu bar.

**F11**
: The [user menu](#edit-menu-file).

**Ctrl-o**
: Show or hide the window of the terminal. See [Terminal](#terminal).

**Ctrl-Alt-p**
: Show or hide the Preview of the file. See [Preview](#preview).

**Ctrl-l**
: Redraw the screen.

## Moving around

**Ctrl-Left, Ctrl-Right**
: One word to the left or to the right.

**Home, End**
: The beginning or the end of the line.

**PgUp, PgDn**
: One page up or down.

**Ctrl-Home, Ctrl-End**
: The beginning or the end of the file.

**Ctrl-PgUp, Ctrl-PgDn**
: The top or the bottom line of the screen.

**Ctrl-Up, Ctrl-Down**
: Scroll the text one line, leaving the cursor where it is in the text.

**Alt-l**
: Go to a line, given by its number.

**Alt-b**
: Go to the bracket matching the one under the cursor.

**Alt-k**
: Set or clear a bookmark on the current line.

**Alt-j, Alt-i**
: Go to the next or the previous bookmark.

**Alt-o**
: Clear all the bookmarks.

## Editing

**Insert**
: Switch between insert and overwrite mode.

**Ctrl-u**
: Undo.

**Alt-r**
: Redo.

**Alt-Shift-u**
: The undo history, to take back several changes at once.

**Ctrl-y**
: Delete the line.

**Ctrl-k**
: Delete to the end of the line.

**Alt-Backspace, Alt-d**
: Delete to the beginning or to the end of the word.

**Alt-Tab**
: Complete the word before the cursor from the words of the file (and of the
other open files). See [Completion](#completion).

**Alt-n**
: Show or hide the line numbers.

**Alt-\_**
: Show or hide the tabs and the trailing spaces.

**Ctrl-s**
: Switch the syntax highlighting on or off.

## Blocks

**F3**
: Start or end the highlighting of a block.

**F13**
: Start a column block.

**Shift-arrows, Shift-PgUp, Shift-PgDn, Shift-Home, Shift-End**
: Highlight the text the cursor goes over.

**Alt-arrows, Alt-PgUp, Alt-PgDn**
: Highlight a column block.

**F5**
: Copy the block to the cursor.

**F6**
: Move the block to the cursor.

**F8**
: Delete the block.

**Ctrl-Ins, Shift-Ins, Shift-Del, Ctrl-Del**
: Copy to the clipboard file, paste from it, cut to it, and delete the block.

## Search keys

**F7**
: Search.

**F17, Ctrl-F7**
: Search again.

**F4**
: Replace.

**F14, Ctrl-F4**
: Replace again.

**Alt-s**
: Lift the line filter, or put the last search back on as a filter.

**Alt-Shift-s**
: Filter by the highlighted text or by the word under the cursor.

**Alt-Shift-f**
: Fold the block, or the lines between a bracket and its match, or unfold.

## Macros and plugins

**Ctrl-r**
: Start and stop recording a macro. See [Macros](#macros).

**Ctrl-a**
: Followed by a key, run the macro assigned to it.

**Alt-Shift-m**
: The [Macro Explorer](#macro-explorer).

**Alt-Enter**
: Look up the definitions of the name under the cursor in the TAGS file. See
[Code navigation](#code-navigation).

**Alt-Minus, Alt-Plus**
: Go back and forward in the list of places the tags lookup went to.

These commands have keys but no menu entry of their own:
**Alt-t**
sorts the selected lines,
**Alt-u**
runs a shell command and puts its output in,
**Alt-p**
formats the paragraph the cursor is in,
**Ctrl-q**
inserts the next key, or a code point written as U+XXXX, as it stands, and
**Ctrl-p**
checks the spelling of the word under the cursor. The first four are Lua
scripts of the editor and go away with them; the last one belongs to the
[spell plugin](#spell-checking),
which also has an entry in the Plugins menu, as every editor plugin that
carries one does.

## Redefine key bindings <a id="keys_redefine"></a>

The
[Key bindings](#key-bindings)
dialog of the Options menu lists every action with the keys it answers to,
changes them and writes the result to
**~/.config/coole/keymap.ini**.
The
[Learn keys](#learn-keys)
dialog is about the other end of the problem: it teaches the program the
sequences a terminal sends for keys it gets wrong. The
[Key sniffer](#key-sniffer)
shows what arrives for a key that is pressed, with the action it is bound to
in the current keymap, which is what to look at when a binding seems to do
nothing.

The key bindings may be read from a keymap file as well. The program starts
from the keymap built into its source. Then the files
**{{pkgdatadir}}/keymap.ini**
and
**{{sysconfdir}}/coole/keymap.ini**
are always loaded, each reassigning the keys the one before defined. The
package puts its own keymaps in
**{{sysconfdir}}/coole**:
**keymap.default.ini**,
**keymap.emacs.ini**
and
**keymap.vim.ini**,
with
**keymap.ini**
a link to the default one.
The option
**--nokeymap**
leaves every file out and keeps the bindings of the source code.

The keymap file of the user is the first one found of:

```
1) command line option -K <keymap>, --keymap=<keymap>
2) environment variable COOLE_KEYMAP
3) setting keymap of the [coole] section of the ini file
4) file ~/.config/coole/keymap.ini
```

The first three take a name or an absolute path. A name that does not end in
**.keymap**
gets that extension added, and is looked for in (to the first one found):

```
1) ~/.config/coole/
2) {{pkgdatadir}}/
```

Because of that extension the keymaps of the package, whose names end in
**.ini**,
cannot be chosen this way. To take one of them, copy or link it to
**~/.config/coole/keymap.ini**,
which is read last and needs no option:

```
ln -s {{sysconfdir}}/coole/keymap.vim.ini \
      ~/.config/coole/keymap.ini
```

A keymap file has a section for the editor,
**[editor]**,
one for the keys that follow Ctrl-x,
**[editor:xmap]**,
and one each for the dialogs, the menus, the input lines, the lists, the radio
buttons and the help window. A line of a section names an action and the keys
it answers to:

```
[editor]
Save = f2
SaveAs = f12; ctrl-f2
```

## Input Line Keys

The input lines of the dialogs accept these keys:

**Ctrl-a, Home**
: puts the cursor at the beginning of line.

**Ctrl-e, End**
: puts the cursor at the end of the line.

**Ctrl-b, Left**
: move the cursor one position left.

**Ctrl-f, Right**
: move the cursor one position right.

**Alt-f, Ctrl-Right**
: moves one word forward.

**Alt-b, Ctrl-Left**
: moves one word backward.

**Ctrl-h, Backspace**
: delete the previous character.

**Ctrl-d, Delete**
: delete the character in the point (over the cursor).

**Ctrl-@**
: sets the mark for cutting.

**Ctrl-w**
: copies the text between the cursor and the mark to a kill buffer and
removes the text from the input line.

**Alt-w**
: copies the text between the cursor and the mark to a kill buffer.

**Ctrl-y**
: yanks back the contents of the kill buffer.

**Ctrl-k**
: kills the text from the cursor to the end of the line.

**Ctrl-Insert**
: copies the selected text, or else the whole line, to the clipboard file.

**Shift-Delete**
: cuts the selected text to the clipboard file.

**Shift-Insert**
: pastes the clipboard file into the line as one line: line breaks and other
control characters become spaces.

**Alt-p, Alt-n**
: browse through the history of the line: Alt-p takes you to the previous
entry, Alt-n to the next one.

**Alt-h**
: shows the [history](#history-query) of the line in a list.

**Alt-Backspace**
: delete one word backward.

**Alt-Tab**
: does the file name, variable and user name
[completion](#completion)
for you.

## General Movement Keys

The help window and the lists of the program share the code that moves in
them, so they accept the same keys:

**Up, Ctrl-p**
: moves one line backward.

**Down, Ctrl-n**
: moves one line forward.

**Page Up, Alt-v**
: moves one page up.

**Page Down, Ctrl-v**
: moves one page down.

**Home, A1**
: moves to the beginning.

**End, C1**
: move to the end.

The help window accepts the following keys in addition to the ones mentioned
above:

**b, Backspace**
: moves one page up.

**Space bar, f**
: moves one page down.

**u, d**
: moves one half of a page up or down.

**g, G**
: moves to the beginning or to the end.

# Mouse Support

coole comes with mouse support. It is activated whenever you are running on
an
**xterm(1)**
terminal (it even works if you take a telnet or ssh connection to another
machine from the xterm) or if you are running on a Linux console and have the
**gpm**
mouse server running.

A click puts the cursor where it is, a drag highlights text, and a click on a
menu, a button or a function key label chooses it. The wheel scrolls the
text.

The default auto repeat rate for the mouse buttons is 400 milliseconds. This
may be changed with the
*mouse_repeat_rate*
setting of the ini file, and the time within which two clicks are a double
click with
*double_click_speed*.

If you are running coole with the mouse support, you can get the default mouse
behavior of the terminal (cutting and pasting text) by holding down the Shift
key.

# Menu Bar

The menu bar pops up when you press F9 or click the mouse on the top row of
the screen. Its menus are File, Edit, Search, Command, Navigate (where a
plugin or a script puts something in it), Window, Plugins and Options. A Lua
script may add entries to any of them, or menus of its own.

## File menu

**Open file...**
: Open a file in a new window.

**New**
: Open an empty window.

**Close**
: Close the window.

**History...**
: The files that were edited before, to open one of them again.

**Save, Save as...**
: Write the file, under its name or another one. See
[Save As](#save-file-as).

**Insert file..., Copy to file...**
: Insert a file at the cursor, or write the highlighted block to a file.

**User menu...**
: The [user menu](#edit-menu-file).

**About...**
: The version of the program.

**Quit**
: Leave the editor, asking about the files that are not saved.

## Edit menu

Undo, Redo and the Undo history; the switch between insert and overwrite
mode; the commands that highlight a block, a column block or the whole file,
and remove the highlighting; copy, move and delete of the block; copy to, cut
to and paste from the clipboard file; and the moves to the beginning and to
the end of the file.

## Search menu

Search, Search again and Replace; the line filter (Toggle line filter, Filter
by word; see [Search, replace and the line filter](#search-and-replace)); and
the bookmarks: toggle a bookmark on the current line, go to the next or to
the previous one, and clear them all.

## Command menu

**Go to line...**
: Go to a line, given by its number.

**Toggle line state**
: Show or hide the column of line numbers.

**Go to matching bracket**
: Go to the bracket that matches the one under the cursor.

**Toggle syntax highlighting, Toggle right margin, Toggle control characters**
: Switch these on and off.

**Encoding...**
: The codepage the text is written in. See
[Choose codepage](#codepages-translation).

**Fold / Unfold block, Unfold all**
: See [Folding](#folding).

**Refresh screen**
: Redraw the screen.

**Start/Stop record macro, Macro explorer..., Record/Repeat actions**
: See [Macros](#macros).

## Navigate menu

The commands of the tags plugins, where one of them is loaded: go to the
declaration of the name under the cursor, find its references, the history of
the jumps, back and forward, the symbols of the file and so on. See
[Code navigation](#code-navigation).

## Window menu <a id="window-menu"></a>

**Move, Resize**
: Move or resize the window with the arrow keys; Enter ends it.

**Toggle fullscreen**
: Switch the window between full screen and its own size.

**Toggle sticky windows**
: With sticky windows, windows whose frames stand next to each other resize
together: the corner of a window moves the edge it is on with all the windows
along it, on both sides, and no gap opens between them. Where four windows
meet, the corner moves both edges. A side that is on the edge of the screen
when the resize begins stays there, a window keeps its smallest size, and an
edge stops at a window in its way, which from then on resizes along. Esc puts
every window back. A window moved by its title goes alone. The setting is kept as
**editor_sticky_windows**
in the ini file.

**Next, Previous**
: Go to the next or to the previous window.

**List...**
: The windows that are open, to go to one of them. See
[Open files](#open-files).

Under them come the windows of the plugins: the tree of the project, the
output of the build, the panel and the console of the debugger, the
terminal, the Preview. An entry opens its window, brings it to the front
when it is behind another, and closes it when it is in front already; an
open one is marked with *\**. A plugin that is switched off has no entry.

The windows of the plugins stand in docks. The tree, the panel of the
debugger and the Preview go in the column at the right, one above the other,
all the height of the screen. The console, the output of the build and the
terminal go in the row at the bottom, under the windows of the files: they
are tabs, one of them seen, their names on its title. A click on a name, or
**Ctrl-Alt-PgDn** and **Ctrl-Alt-PgUp** (*WindowTabNext* and *WindowTabPrev*
in the keymap), shows another. A file window that was fullscreen takes the
rest of the screen, and the screen again when the docks are empty. A window
of a dock resized gives the dock its size; moved, it leaves the dock.

**Layout...**
: The layouts of the windows: which windows of the plugins are open, in
which dock, and how big the docks are. *Edit* has the files alone,
*Project* the tree of the project, *Debug* the tree and the panel of the
debugger. **Use** puts the windows so; **Save as...** keeps them as they are
under a name, the name of one of these too; **Delete** forgets a layout
kept, the one of the editor of that name coming back. With **Debug layout
while debugging** the start of the debugger puts the windows as *Debug* has
them, and its stop puts them back, the tabs of the bottom staying to be
read.

The windows of a project are kept when the editor ends, and come back when
it starts with a file of the project.
*coole --debug* starts with the layout *Debug*. The layouts are kept in
*layouts.ini* of the settings.

## Plugins menu

The editor plugins that carry an entry of their own, the spell checker for
one; their windows are in the [Window menu](#window-menu). A plugin that is switched off in
[Manage plugins](#manage-plugins)
is not in the menu.

## Options menu

**General...**
: The [editor options](#editor-options).

**Save mode...**
: How a file is written. See [Edit Save Mode](#edit-save-mode).

**Syntax highlighting...**
: Choose the syntax rules for the file by hand, or leave them to the name and
the first line of the file.

**Appearance...**
: The skin and the shadows. See [Appearance](#appearance).

**Learn keys...**
: See [Learn keys](#learn-keys).

**Key bindings...**
: See [Key bindings](#key-bindings).

**Key sniffer...**
: See [Key sniffer](#key-sniffer).

**Manage plugins...**
: See [Manage plugins](#manage-plugins).

**Plugin info...**
: See [Plugin info](#plugin-info).

**Lua scripts...**
: See [Lua scripts](#lua-scripts). The entry is there only where coole was
built with Lua.

**Syntax file**
: Open the syntax file of the user,
*~/.local/share/coole/syntax/Syntax*,
in the editor. Where it does not exist, it is made with a short comment on what
it holds, not as a copy of the system file, which is still read after it. See
[Syntax Highlighting](#syntax-highlighting).

**Menu file**
: Open the [user menu](#edit-menu-file) file in the editor.

**Save setup**
: Write the settings to
*~/.config/coole/ini*.
With the
*auto_save_setup*
setting on, they are written whenever the editor ends.

### Editor options <a id="editor-options"></a>

The settings of the editor, which the
**General...**
item of the Options menu opens. They are written to the ini file under the
names the
[Settings in the ini file](#internal-file-editor-options)
section describes.

*Wrap mode.*
Off, dynamic paragraph formatting, or the typewriter wrap that breaks a line
at the word wrap line length while it is typed.

*Fake half tabs.*
Move and indent by half a tab between the text and the left margin, with
spaces, while a tab stays a tab everywhere else.

*Backspace through tabs.*
One backspace deletes all the space to the left margin when there is no text
between the cursor and the margin.

*Fill tabs with spaces.*
Insert spaces up to the next tab stop instead of a tab character.

*Tab spacing.*
The width a tab character stands for. 8 by default, and other editors and
viewers assume 8 as well.

*Return does autoindent.*
A new line starts at the indentation of the line above.

*Confirm before saving.*
Ask before the file is written.

*Save file position.*
Open a file at the place it was left the last time.

*Visible trailing spaces.*
Mark the spaces at the end of a line.

*Visible tabs.*
Mark the tab characters.

*Show control characters.*
Print the control characters of the text instead of hiding them.

*Syntax highlighting.*
Color the text by the syntax rules of its file type.

*Cursor after inserted block.*
Leave the cursor at the end of a block that was just inserted, not at its
start.

*Persistent selection.*
A selection stays when the cursor moves, instead of being dropped.

*Cursor beyond end of line.*
The cursor may stand past the last character of a line.

*Group undo.*
One undo takes back a run of the same kind of change, not a single key.

*Word wrap line length.*
The column the wrap modes break a line at. 72 by default.

*Esc timeout, ms.*
How long a single Esc waits for the rest of a key before it counts as Esc,
in milliseconds. 1000 by default. It is written to the ini file as
*old_esc_mode_timeout*,
in microseconds, and the
**KEYBOARD_KEY_TIMEOUT_US**
variable overrides it when the editor starts.

### Edit Save Mode <a id="edit-save-mode"></a>

How a file is written, the same three modes the
*editor_option_save_mode*
setting takes:

**Quick save**
: Write over the file at once. Fast, and a failure in the middle leaves the
file half written.

**Safe save**
: Write a temporary file first and rename it over the original when it is
whole, so a failure leaves the original untouched.

**Do backups with following extension**
: Safe save, and the original is kept under its name with the extension of the
input line added, "~" by default. Saving twice replaces the backup as well.

**Check POSIX new line**
: Ask about the missing newline at the end of the file before it is written.

### Appearance

The skin the program is drawn with, and whether the dialogs and the drop-down
menus have a shadow.

**Skin**
: The button opens the list of the skins that are installed and those of the
user; the one chosen is applied at once.

**Shadows**
: Draw a shadow under every dialog and drop-down menu. Without colors there
are no shadows.

The choice is written to the
*skin*
and
*shadows*
settings of the ini file. See the
[Skins](#skins)
section for the skin files themselves.

### Learn keys

This dialog teaches coole the escape sequences your terminal sends for the
function keys, the arrows and the navigation keys. Its title names the
terminal, from the
**TERM**
variable.

Select a modifier combination (Ctrl, Alt, Shift) using the checkboxes, then
press the button of a key. Press the key on the keyboard and wait for the
capture message to disappear. The learned sequence appears next to the button.
A sequence that another key has already is reported as a conflict.

**Del**
: Clear the learned key the cursor is on.

**Save**
: Write the learned keys to
*~/.config/coole/term/\<TERM>*.

**Edit term file**
: Open that file in the editor.

### Key bindings <a id="key-bindings"></a>

The actions of the program with the keys they answer to, in a table. The
buttons at the top choose the group, Editor or Widgets, and then the part of
it: Main and Ctrl-X for the editor; Dialog, Menu, Input, Listbox, Radio and
Help for the widgets. Back returns to the groups. Actions marked with \* have
keys that differ from the defaults.

**Enter**
: Replace the key of the action: press the key to assign.

**F5**
: Add another key to the action.

**F8, Del**
: Remove the key.

**Save**
: Write the changes to
*~/.config/coole/keymap.ini*.

**Edit keymap file**
: Open
*keymap.ini*
in the editor.

**Edit term file**
: Open the key sequences of the terminal, the file
[Learn keys](#learn-keys)
writes.

A key that is taken by another action is reported, and the question is
whether to take it from there.

### Key sniffer <a id="key-sniffer"></a>

Press
**Capture key**,
then any key. The dialog shows what arrived:

```
Shortcut   the symbolic name, e.g. Ctrl-F5
Action     the action of the key in the editor keymap
Raw        the escape sequence and its bytes in hex
Keycode    the internal numeric code
```

It is the place to look when a key does nothing or the wrong thing: a key
that arrives as a sequence the program does not recognize is to be taught in
[Learn keys](#learn-keys),
and one that arrives but has no action is to be bound in
[Key bindings](#key-bindings).

### Manage plugins <a id="manage-plugins"></a>

The plugins the program has loaded, in a table: the kind, the name and what the
plugin says about itself. The editor plugins are listed here, and, where coole
was built with Lua, the Lua engine as a plugin of the kind
*runtime*.
A plugin is switched off and on with the checkbox of its row, and what is
switched off is not loaded the next time either; the choice is kept in
*~/.config/coole/plugins.ini*.
A plugin that is switched off stays in the list, so that it can be switched
on again.

**Enter, F4**
: Open the settings of the plugin the cursor is on. A plugin that has none says
so. On the row of the Lua engine it opens the
[Lua scripts](#lua-scripts).

**IDE: project, build, debugger**
: Switch the three plugins that make an IDE of the editor on together, or off
together when all of them are on: the project and its files, the build, and
the debugger. Each of them works without the others too.

### Plugin info <a id="plugin-info"></a>

The plugins the editor has loaded: the name, whether it is on, what it
provides and what it does. It is a list to look at; a plugin is switched off
and its settings are opened in the
[Manage plugins](#manage-plugins)
dialog.

### Lua scripts <a id="lua-scripts"></a>

The Lua scripts that were found, the system ones and those of the user, in a
table: whether it is on, where the script lives (global or user), its
identifier, its workspace, what it provides and what it does. A script is
switched off and on with the checkbox of its row; the change takes effect the
next time coole starts.

**Settings**
: Run the script that carries the settings of the package, where it has one.

See [Lua scripting](#lua-scripting) for the scripts themselves.

# Search, replace and the line filter <a id="search-and-replace"></a>

The search dialog
(**F7**)
takes the text to look for and the type of the search: a plain string, a
regular expression, hexadecimal bytes or a shell pattern; whether the case
matters; whether it looks backwards, only in the highlighted block, or for
whole words; and whether it looks in every encoding the file might be in.
**Find all**
sets a bookmark on every line that matches. The replace dialog
(**F4**)
takes the text to put in as well, and asks at every match unless it is told to
replace them all. In a regular expression replacement, \\1 to \\9 stand for
the groups of the match.

The
**Filter**
button of the search dialog hides every line that does not match the search
string, using the same type, case and whole-word settings as the search. Line
numbers keep their original values and the line state gutter marks each
hidden run. The set of hidden lines is fixed at the moment the button is
pressed: editing never re-applies the match, so a shown line that is split or
joined stays shown, and lines typed afterwards stay shown even if they do not
match.
**Alt-s**
lifts the filter; pressed again it puts the last search back on as a filter.
**Alt-Shift-s**
filters by the highlighted text, or by the word under the cursor, at once.
"Unfold all" in the Command menu lifts the filter as well.

# Folding

A fold hides a run of lines behind its first line, which stays on the screen
with a mark in the line state gutter.
**Alt-Shift-f**
(Fold / Unfold block in the Command menu) folds the highlighted lines, or,
with nothing highlighted, the lines from a bracket on the current line to the
bracket that matches it. On the first line of a fold it unfolds it.
**Unfold all**
opens every fold of the file, and lifts the line filter.

# Macros

To define a macro, press
**Ctrl-r**
and then type out the keys you want to be executed. Press
**Ctrl-r**
again when finished, and assign the macro to a key by pressing that key. The
macro is executed by that key, and by
**Ctrl-a**
followed by the assigned key.

The macros are kept in the section
**[editor]**
of the file
**~/.local/share/coole/macros**,
and a macro is deleted by deleting its line there, or in the
[Macro Explorer](#macro-explorer).

**Record/Repeat actions**
in the Command menu records the actions that follow until it is chosen again,
and then asks how many times to repeat them.

A shell script can be assigned to a key as well, by a line of the
**macros**
file like this:

```
[editor]
ctrl-W=ExecuteScript:25;
```

This means that Ctrl-W runs the
*ExecuteScript(25)*
action, which runs the shell script
**~/.local/share/coole/macros.d/macro.25.sh**.
The scripts are kept in the directory
**~/.local/share/coole/macros.d/**
and must be named
**macro.XXXX.sh**,
where
**XXXX**
is a number from 0 to 9999. Examples are installed in
**{{pkgdatadir}}/examples/macros.d/**.
A script has the format of the entries of the
[user menu](#edit-menu-file)
and takes the same macros. Its first line may be

*#silent*
: The script is run without asking for its parameters.

Here is a sample script:

```
l       comment selection
	TMPFILE=`mktemp ${COOLE_TMPDIR:-/tmp}/up.XXXXXX` || exit 1
	echo #if 0 > $TMPFILE
	cat %b >> $TMPFILE
	echo #endif >> $TMPFILE
	cat $TMPFILE > %b
	rm -f $TMPFILE
```

## Macro Explorer <a id="macro-explorer"></a>

The macros that are recorded, with the key each one answers to and what it
does. The buttons are

**Run**
: Play the macro the cursor is on.

**Delete**
: Remove it, after a question.

**Edit file**
: Open the file the macros live in, which the
[Macros](#macros)
section describes.

# User menu <a id="edit-menu-file"></a>

The user menu
(**F11**)
is a menu of useful actions that can be customized by the user: each entry is
a shell script that is run with the text of the editor at hand. The menu is
read from the first one found of:

```
1) ./.coole.menu in the current directory
2) ~/.config/coole/menu
3) ~/coole.menu
4) {{sysconfdir}}/coole/coole.menu
5) {{pkgdatadir}}/coole.menu
```

The file of the current directory is read only where it belongs to the user or
to root and nobody else can write it, because its entries run commands. The
**Menu file**
item of the Options menu opens the menu file in the editor.

The format of the menu file is very simple. Lines that start with anything but
space or tab are considered entries for the menu (in order to be able to use
it like a hot key, the first character should be a letter). All the lines that
start with a space or a tab are the commands that will be executed when the
entry is selected. Comments are started with '#'.

When an entry is selected all its command lines are copied to a temporary file
and then that file is executed by the shell. This allows the user to put
normal shell constructs in the menus. Also simple macro substitution takes
place before executing the menu code:

*%f, %p*
: The name of the current file.

*%n*
: The current file name without extension.

*%x*
: The extension of the current file name.

*%d*
: The current directory.

*%c*
: The position of the cursor in the file.

*%i*
: The indent of blank space, equal to the cursor column.

*%y*
: The syntax type of the current file.

*%b*
: The block file: the highlighted text is written to
*~/.cache/coole/block*
before the command runs, and what the command leaves there replaces the
block afterwards.

*%m*
: The name of the menu file.

*%%*
: The % character.

*%{some text}*
: Prompt for the substitution. An input box is shown and the text inside the
braces is used as a prompt. The macro is substituted by the text typed by the
user. The user can press Esc or F10 to cancel.

*%view*
: Run the commands and open their standard output in a new editor window. The
words in braces after %view, as in %view{ascii}, are accepted and ignored.

Uppercase and lowercase macros are the same. With a number after the %
character quoting is turned on (the default) and off: %f and %1f quote the
expanded macro, %0f does not.

Here is a sample menu file:

```
s       Sort the block
	sort %b > %b.tmp && mv -f %b.tmp %b

w       Count the words of the file
	%view wc %f

m       Run make in the directory of the file
	make
```

**Default Conditions**

Each menu entry may be preceded by a condition. The condition must start from
the first column with a '=' character. If the condition is true, the menu
entry will be the default entry.

```
Condition syntax:   = <sub-cond>
  or:               = <sub-cond> | <sub-cond> ...
  or:               = <sub-cond> & <sub-cond> ...

Sub-condition is one of following:

  y <pattern>   syntax of current file matching pattern?
  f <pattern>   current file matching pattern?
  d <pattern>   current directory matching pattern?
  t <type>      current file of type?
  x <filename>  is it executable filename?
  ! <sub-cond>  negate the result of sub-condition
```

Pattern is a normal shell pattern or a regular expression, according to the
*shell_patterns*
setting. You can override the global value of that setting by writing
"shell_patterns=x" on the first line of the menu file (where "x" is either 0
or 1).

Type is one or more of the following characters:

```
  n   not a directory
  r   regular file
  d   directory
  l   link
  c   character device
  b   block device
  f   FIFO (pipe)
  s   socket
  x   executable file
```

For example 'rlf' means either regular file, link or fifo.

If the condition starts with '=?' instead of '=' a debug trace will be shown
whenever the value of the condition is calculated.

The conditions are calculated from left to right. This means

```
	= f *.c | f *.h & y C
```

is calculated as

```
	( (f *.c) | (f *.h) ) & (y C)
```

**Addition Conditions**

If the condition begins with '+' (or '+?') instead of '=' (or '=?') it is an
addition condition. If the condition is true the menu entry will be included
in the menu. If the condition is false the menu entry will not be included in
the menu.

You can combine default and addition conditions by starting condition with
'+=' or '=+' (or '+=?' or '=+?' if you want debug trace). If you want to use
two different conditions, one for adding and another for defaulting, you can
precede a menu entry with two condition lines, one starting with '+' and
another starting with '='.

# Code navigation

The editor finds the definition of a name in a tags file, with one of two
plugins.

The etags plugin reads the
**TAGS**
file of the project, in the Emacs format, which etags or
*ctags -e*
builds. For example, with exuberant-ctags for the C language:

```
ctags -e --language-force=C -R ./
```

**Alt-Enter**
shows the list of the definitions of the name under the cursor (the cursor
should stand at the end of the word); with a single one it goes there at
once. See
[Definitions](etags.md#definitions).

**Alt-Minus**
goes to the previous place in the navigation list (like the Back button of a
browser), and
**Alt-Plus**
to the next one (like its Forward button).

The ctags plugin reads the
**tags**
file of ctags, and fills the Navigate menu: go to the declaration, find the
references, complete a symbol, the symbols of the file, the members of a class
or structure, and more. See
[Ctags](ctags.md#ctags).

Both plugins can be switched off in
[Manage plugins](#manage-plugins),
and then their keys do nothing.

# Spell checking

The spell plugin checks the text with aspell or hunspell.
**Ctrl-p**
checks the word under the cursor, and the Plugins menu checks the whole file.
See [Spell](spell.md#spell).

# Terminal

The terminal plugin runs your shell in a window of the editor.
**Ctrl-o**
shows the window at the bottom of the screen, a file shown full screen going
into a window above it, and the windows already there, a file beside the
Preview for one, going up out of its way; in the terminal it hides it again,
and each of them goes back where it was. The shell goes on running while the
window is hidden.
The window is moved, resized, shown full screen and closed like the window of
a file. The scrollbar on the right of its frame scrolls the history of the
output. See [Terminal](terminal.md#terminal).

# Preview

The viewer plugin shows the file the way it is meant to read, whatever the
file is, in a window named Preview at the right of it.
**Ctrl-Alt-p**,
or Preview in the Window menu, shows and hides it. The file keeps the
focus; the Preview is drawn again as the text changes, follows the cursor,
and shows the file window that comes to the front. The window is moved,
resized and closed like the window of a file. Once it has the focus, it has
a cursor of its own, which the keys of the editor move as in a file: the
arrows, Home and End, the pages, the top and the bottom of the text; the keys
of the words, Ctrl-Left and Ctrl-Right, move it eight columns. The view
follows it, sideways too for what is wider than it. A click
on the text puts the cursor there.
The mouse wheel scrolls it without the focus, sideways when it is tilted or
turned with Shift. Its frame has a scrollbar on the right and one at the
bottom: an arrow scrolls a step, the bar beside the thumb a page, and the
thumb is dragged. The key is set in
**viewer.ini**,
section
**[Preview]**,
entry
**key**.

The renderers tell the viewer what files they draw, by the ends of their names
or how their texts start, and the viewer has the file drawn by the renderer of
its type: a markdown file comes out with its headings, lists,
tables with rules, blocks of code in the colors of the editor, formulas as
symbols and mermaid diagrams as drawings; that renderer is the Lua script
render-markdown, lua-markdown of Midnight Commander, whose settings, from
Manage plugins, are kept in
**~/.config/coole/render-markdown.ini**.
A JSON file, and JSON Lines, comes out one value to a line in the colors the
editor gives JSON, the size of every object and array beside it, a long array
packed to the width or cut short, and an array of objects of one kind as a
table; comments and a comma before a closing bracket are taken. While it does
not read, halfway through a change, the error and its place are shown above
the last view that read. That renderer is the Lua script render-json.
An XML file, by the end of its name or by a text that starts with <?xml, comes
out an element to a line in the colors the editor gives XML: a short text on
the line of its element, a long one wrapped under it, the attributes one to a
line when they do not fit, the number of the elements an element holds beside
it, a long run of elements of one name cut short, and a run of elements of one
name that hold attributes and plain text as a table. That renderer is the Lua
script render-xml, and it shows its errors the way render-json does.
A string of base64 in JSON or XML, a text, a value or a data: URI, is cut to
its first letters, and what it holds is shown beside it in parentheses: the
start of the text, or the kind of file its bytes start like, and the size.
Only the start of it is decoded, so a string of megabytes costs no more than a
short one; a string of the letters of base64 that holds neither text nor a
known kind of file, a hash for one, is left as it is.
A file of a type no renderer knows is shown as its text.

# Dialogs

## Save As <a id="save-file-as"></a>

The name to write the file under, and the line breaks to write it with: as the
file has them, Unix (LF), Windows and DOS (CR LF), or Macintosh (CR). The
choice holds for that save; the file keeps what it is given.

## Open files <a id="open-files"></a>

The files the editor has open, one to a line. Enter goes to the file the
cursor is on, Esc leaves the file that is shown where it is. The same list is
the
**List...**
item of the Window menu.

## Screen selector

Besides the editor, a Lua script may open screens of its own, full-screen
views that stay open while you go back to the editor. There are three ways to
switch between the screens:

**Alt-}**
: switch to the next screen;

**Alt-{**
: switch to the previous screen;

**Alt-\`**
: open a dialog window with the list of the screens that are open, to choose
one.

## Completion

Let the program type for you.

In the editor,
**Alt-Tab**
completes the word before the cursor from the words of the file, and, with the
*editor_wordcompletion_collect_all_files*
setting on (the default), of the other open files as well. Where more than one
word fits, a list of them pops up to choose from.

In an input line of a dialog,
**Alt-Tab**
attempts completion treating the text as a variable (if the text begins with
**$**),
a user name (if the text begins with
**~**)
or a file name. If the completion is ambiguous, the program beeps, and with
the
*show_all_if_ambiguous*
setting on, a list of all the possibilities pops up next to the current
position: select one with the arrow keys and
**Enter**,
or type the first letters in which the possibilities differ to narrow the
list. As soon as there is no ambiguity, the list disappears; Esc, F10 and the
left and right arrow keys hide it as well.

## Choose codepage <a id="codepages-translation"></a>

The list of the codepages the program knows, from
**{{pkgdatadir}}/charsets**.
Choosing one tells the editor in which codepage the text of the file is
written, and the text is recoded from it into the codepage of the terminal
for the screen;
**\<No translation>**
leaves it as bytes. The list is opened by
**Alt-e**
and by the Encoding item of the Command menu.

## History <a id="history-query"></a>

The list of what was typed into an input line before, newest first, which
**Alt-h**
opens for the line the cursor is in. Enter takes the entry the cursor is on
into the line, Esc leaves the line as it was, and
**F8, Del**
removes the entry the cursor is on from the history. The histories are kept in
*~/.local/share/coole/history*;
*num_history_items_recorded*
says how many entries each of them keeps.

# Syntax Highlighting

**coole**
supports syntax highlighting.  This means that keywords and contexts
(like C comments, string constants, etc) are highlighted in different
colors.  The following section explains the format of the file
**~/.local/share/coole/syntax/Syntax**.
The system-wide
**{{pkgdatadir}}/syntax/Syntax**
is read after it, for every file type it does not name: the file of the user
holds only what the user added or wants otherwise, and an entry of its own
wins over an installed one for the files it matches.  The files its
**include**
lines name are looked for in
*~/.local/share/coole/syntax/*
first and among the installed ones after.  A full copy of the system file, as
older versions made, hides every file type installed after it: keep in it only
the entries you changed.
The file
**~/.local/share/coole/syntax/Syntax**
is rescanned on opening of every new editor file.  The file contains
rules for highlighting, each of which is given on a separate line, and
define which keywords will be highlighted with what color.

The file is divided into sections, each beginning with a line with the
**file**
command.  The sections are normally put into separate files using the
**include**
command.

The
**file**
command has three arguments.  The first argument is a regular expression
that is applied to the file name to determine if the following section
applies to the file.  The second argument is the description of the file
type.  It is used in
**cooledit**;
future versions of
**coole**
may use it as well.  The third optional argument is a regular expression
to match the first line of text of the file.  The rules in the following
section apply if either the file name or the first line of text matches.

A section ends with the start of another section.  Each section is
divided into contexts, and each context contains rules.  A context is a
scope within the text that a particular set of rules belongs to.  For
instance, the text within a C style comment (i.e. between
**/\***
and
**\*/**)
has its own color.  This is a context, although it has no further rules
inside it because there is probably nothing that we want highlighted
within a C comment.

A trivial C programming section might look like this:

```
file .\*\\.c C\sProgram\sFile (#include|/\\\*)

wholechars abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_

# default colors
define  comment   brown
context default
  keyword  whole  if       yellow
  keyword  whole  else     yellow
  keyword  whole  for      yellow
  keyword  whole  while    yellow
  keyword  whole  do       yellow
  keyword  whole  switch   yellow
  keyword  whole  case     yellow
  keyword  whole  static   yellow
  keyword  whole  extern   yellow
  keyword         {        brightcyan
  keyword         }        brightcyan
  keyword         '*'      green

# C comments
context /\* \*/ comment

# C preprocessor directives
context linestart # \n red
  keyword  \\\n  brightred

# C string constants
context " " green
  keyword  %d    brightgreen
  keyword  %s    brightgreen
  keyword  %c    brightgreen
  keyword  \\"   brightgreen
```

Each context starts with a line of the form:

**context**
[**exclusive**]
[**oneline**]
[**whole**|**wholeright**|**wholeleft**]
[**linestart**]
*delim*
[**linestart**]
*delim*
*[foreground]*
*[background]*
*[attributes]*

The first context is an exception.  It must start with the command

**context default**
*[foreground]*
*[background]*
*[attributes]*

otherwise
**coole**
will report an error.  The
**linestart**
option specifies that
*delim*
must start at the beginning of a line.  The
**whole**
option tells that
*delim*
must be a whole word.  To specify that a word must begin on the word
boundary only on the left side, you can use the
**wholeleft**
option, and similarly a word that must end on the word boundary is specified by
**wholeright**.

The set of characters that constitute a whole word can be changed at any
point in the file with the
**wholechars**
command.  The left and right set of characters can be set separately
with

**wholechars**
[**left**|**right**]
*characters*

The
**exclusive**
option causes the text between the delimiters to be highlighted, but not
the delimiters themselves.

The
**oneline**
option, after
**exclusive**
if both are given, ends the context at the end of its line when the right
delimiter is not there, so that a delimiter left open colors no more than
the rest of its line.

A context can hand the text between its delimiters to the rules of
another syntax, with a line right below it:

**embed**
[**soft**]
*type*

where
*type*
is the name of the rules as the file line of the Syntax file gives it,
with \\s for a space.  It is looked up in the Syntax file the rules are
read from, and then in the one installed with coole, so that a copy of the
Syntax file older than the rules still finds it; a type found in neither
leaves the context as if it embedded nothing, in its own colors.  The delimiters keep the colors of the context.  The
right delimiter ends the embedded text wherever it stands, inside a string
or a comment of the embedded rules as well.  An embedded syntax can embed
another one in turn, a few levels deep.  For example, HTML hands scripts to
JavaScript:

```
  context <SCRIPT*> </SCRIPT> brightcyan
      embed JavaScript\sProgram
```

With
**soft**
the right delimiter ends the embedded text only outside the strings,
comments and keywords of the embedded rules, so that in {{ "}}" }} of a
Jinja2 template the first }} is part of the string, and in ${x/\\}/y} of a
shell script the first } is part of the escape.

A whole syntax can instead be a layer over another one, with a line

**overlay**
*type*

anywhere among its rules.  Its contexts then color what they hold wherever it
stands, and the rest of the text is read by the rules of
*type*,
which see what the contexts hold as blanks and never know it was there.  This
is how the tags of a template language are colored in the text, inside an HTML
tag, in a string of an attribute and in a script alike, while HTML colors the
rest.  The default context of a layer colors nothing: a keyword there only
keeps a context from starting.  A type found in no Syntax file leaves the
layer over plain text.  For example, Smarty over HTML:

```
  overlay HTML\sFile
  context default
      keyword {\s
  context {\* \*} brown
  context { } brightcyan
      embed soft Smarty\sTag
```

Each rule is a line of the form:

**keyword**
[**whole**|**wholeright**|**wholeleft**]
[**linestart**]
*string foreground*
*[background]*
*[attributes]*

Context or keyword strings are interpreted, so that you can include tabs
and spaces with the sequences \\t and \\s.  Newlines and backslashes are
specified with \\n and \\\\ respectively.  Since whitespace is used as a
separator, it may not be used as is.  Also, \\\* must be used to specify
an asterisk.  The \* itself is a wildcard that matches any length of
characters.  For example,

```
  keyword         '*'      green
```

colors all C single character constants green.  You also could use

```
  keyword         "*"      green
```

to color string constants, but the matched string would not be allowed
to span across multiple newlines.  The wildcard may be used within
context delimiters as well, but you cannot have a wildcard as the last
or first character.

Important to note is the line

```
  keyword  \\\n  brightgreen
```

This line defines a keyword containing the backslash and newline
characters.  Since the keywords are matched before the context
delimiters, this keyword prevents the context from ending at the end of
the lines that end in a backslash, thus allowing C preprocessor
directive to continue across multiple lines.

The possible colors are: black, gray, red, brightred, green,
brightgreen, brown, yellow, blue, brightblue, magenta, brightmagenta,
cyan, brightcyan, lightgray and white. The special keyword "default" means
the terminal's default. Another special keyword "base" means the main
colors, it is useful as a placeholder if you want to specify attributes
without modifying the background color. When 256 colors are available,
they can be specified either as color16 to color255, or as rgb000 to rgb555
and gray0 to gray23.

If the syntax file is shared with
**cooledit**,
it is possible to specify different colors for
**coole**
and
**cooledit**
by separating them with a slash, e.g.

```
keyword  #include  red/Orange
```

**coole**
uses the color before the slash.  See cooledit(1) for supported
**cooledit**
colors.

Attributes can be any of bold, italic, underline, reverse and blink, appended by a
plus sign if more than one are desired.

Comments may be put on a separate line starting with the hash sign (#).

If you are describing case insensitive language you need to use
**caseinsensitive**
directive. It should be specified at the beginning of syntax file.

Because of the simplicity of the implementation, there are a few
intricacies that will not be dealt with correctly but these are a minor
irritation.  On the whole, a broad spectrum of quite complicated
situations are handled with these simple rules.  It is a good idea to
take a look at the syntax file to see some of the nifty tricks you can
do with a little imagination.  If you cannot get by with the rules I
have coded, and you think you have a rule that would be useful, please
email me with your request.  However, do not ask for regular expression
support, because this is flatly impossible.

The special
**line-local**
syntax mode is intended for plain text.  It resets at every newline and
screen boundary, and only scans the displayed text.  It supports
**number**
*max foreground*
for decimal numbers up to
*max*
digits,
**string**
*quote foreground*
for single- or double-quoted strings, and
**symbols**
*characters foreground*
for individually colored punctuation characters.

A useful hint is to work with as much as possible with the things you
can do rather than try to do things that this implementation cannot deal
with.  Also remember that the aim of syntax highlighting is to make
programming less prone to error, not to make code look pretty.

The syntax highlighting can be toggled using Ctrl-s shortcut.

# Colors

coole will try to detect if your terminal supports color using the terminal
database and your terminal name. Sometimes it gets confused, so you may force
color mode or disable color mode using the -c and -b flag respectively.

If the program is compiled with the S-Lang screen manager instead of ncurses,
it will also check the variable
**COLORTERM,**
if it is set, it has the same effect as the -c flag.

You may specify terminals that always force color mode by adding the
*color_terminals*
variable to the Colors section of the initialization file. This will prevent
coole from trying to detect if your terminal supports color. Example:

```
[Colors]
color_terminals=linux,xterm
color_terminals=terminal-name1,terminal-name2...
```

The program can be compiled with both ncurses and S-Lang, ncurses does not
provide a way to force color mode: ncurses uses just the information in the
terminal database.

# Skins

The colors of the program and the lines of its frames are taken from a skin
file. The
[Appearance](#appearance)
dialog chooses one from a list.

If your skin contains any true-color definitions, you should define the
'truecolors' key set to TRUE value in [skin] section. If true-color is not
used but 256-color is, you should define '256colors' instead.

The skin is the first one named by:

```
1) command line option -S <skin>, --skin=<skin>
2) environment variable COOLE_SKIN
3) setting skin of the [coole] section of the ini file
4) the skin called default
```

A name may be the absolute path of a skin file (with the extension .ini or
without it). Otherwise the skin file is looked for in (to the first one
found):

```
1) ~/.local/share/coole/skins/
2) {{sysconfdir}}/coole/skins/
3) {{pkgdatadir}}/skins/
```

The format of skin files is described in
**{{pkgdatadir}}/skins/README.txt**.
The colors of the syntax highlighting come from the syntax files, not from
the skin; see
[Syntax Highlighting](#syntax-highlighting).

# Terminal databases

coole provides a way to fix your system terminal database without requiring
root privileges. The
[Learn keys](#learn-keys)
dialog writes what it learns to
**~/.config/coole/term/\<TERM>**,
one line for every key in its
**[keys]**
section. coole also searches the system file
**defaults.ini**
(in
**{{sysconfdir}}/coole**
or
**{{pkgdatadir}}**)
and the
**~/.config/coole/ini**
file for the section "terminal:your-terminal-name" and then for the section
"terminal:general". Each line of the section contains a key symbol that you
want to define, followed by an equal sign and the definition for the key. You
can use the special \\e form to represent the escape character and the ^x to
represent the control-x character. A line
*copy=other*
takes every key of the section "terminal:other" as well.

The possible key symbols are:

```
f0 to f20     Function keys f0-f20
bs            backspace
home          home key
end           end key
up            up arrow key
down          down arrow key
left          left arrow key
right         right arrow key
pgdn          page down key
pgup          page up key
insert        the insert character
delete        the delete character
complete      to do completion
```

For example, to define the key insert to be the Escape + [ + O + p, you set
this in the ini file:

```
insert=\e[Op
```

A key symbol may take modifiers:

```
    ctrl-alt-right=\e[[1;6C
    ctrl-alt-left=\e[[1;6D
```

This means that ctrl+alt+left sends a \\e[[1;6D escape sequence and therefore
coole interprets "\\e[[1;6D" as Ctrl-Alt-Left.

The
*complete*
key symbol represents the escape sequences used to invoke the completion
process, this is invoked with Alt-tab, but you can define other keys to do the
same work (on those keyboard with tons of nice and unused keys everywhere).

# Lua scripting

Where coole was built with Lua, it runs Lua scripts that extend the editor:
actions bound to keys and to menus, reactions to the events of the editor
(a file is opened, saved, a key is pressed), dialogs and full-screen views.
Several commands of the editor are such scripts: sorting the lines, formatting
the paragraph, inserting the output of a command, inserting a character by its
code point, inserting the date, decoding base64 and drawing tables.

The system scripts are in
**{{pkgdatadir}}/lua/scripts/**,
and the scripts of the user in
**~/.local/share/coole/lua/scripts/**,
one directory for every script, with its
**lua.ini**
and its
**init.lua**.
The
[Lua scripts](#lua-scripts)
dialog switches them on and off.

The option
**--no-lua**,
or the variable
**COOLE_NO_LUA**
set to 1, starts the editor without any Lua script, and
*enabled=false*
in the
**[Lua]**
section of the ini file does so for good.

How to write a script, and the functions a script can call, is described in
**LUA_PLUGINS.md**
and
**LUA_API_REFERENCE.md**,
which are installed with the documentation of coole.

# General settings

These settings of the
**[coole]**
section of the ini file concern the program rather than the editor. Unless
said otherwise, 1 turns a setting on and 0 turns it off.

*auto_save_setup*
: Write the settings to the ini file whenever the editor ends.

*skin*
: The name of the skin. See [Skins](#skins).

*shadows*
: Draw a shadow under the dialogs and the drop-down menus.

*keymap*
: The keymap file to use. See [Redefine key bindings](#keys_redefine).

*drop_menus*
: Open the first menu at once when F9 is pressed, instead of only the menu
bar.

*mouse_close_dialog*
: A click outside a dialog closes it.

*mouse_repeat_rate, double_click_speed*
: The auto repeat rate of the mouse buttons, and the time within which two
clicks make a double click, in milliseconds.

*old_esc_mode, old_esc_mode_timeout*
: With old_esc_mode on, a single Esc is taken as Esc after
old_esc_mode_timeout microseconds, instead of waiting for the next key.

*show_all_if_ambiguous*
: Show the list of the possible completions at once where the completion is
ambiguous. See [Completion](#completion).

*shell_patterns*
: The patterns of the user menu conditions are shell patterns (1) or regular
expressions (0).

*num_history_items_recorded*
: How many entries every history of an input line keeps.

*confirm_history_cleanup*
: Ask before a history is cleared.

*clear_before_exec*
: Clear the screen before a command of the user menu runs.

*pause_after_run*
: Wait for a key after a command of the user menu ran: never (0), on dumb
terminals only (1, the default) or always (2).

*alternate_plus_minus*
: Keep the plus and the minus keys of the numeric keypad as plus and minus.

*verbose*
: Show the progress of long operations.

*keybar_visible*
: In the
**[Layout]**
section: show the labels of the function keys at the bottom of the screen.

# Settings in the ini file <a id="internal-file-editor-options"></a>

Most settings are made in the dialogs of the
**Options**
menu, and
**Save setup**
writes them to the
**[coole]**
section of
**~/.config/coole/ini**.
The file can be edited by hand as well; these are the names the editor
settings have there. Unless said otherwise, 1 turns a setting on and 0 turns
it off.

*editor_tab_spacing*
: Interpret the tab character as being of this length.
Default is 8. You should avoid using
other than 8 since most other editors and text viewers
assume a tab spacing of 8. Use
**editor_fake_half_tabs**
to simulate a smaller tab spacing.

*editor_fill_tabs_with_spaces*
: Never insert a tab character. Rather insert spaces (ascii 32) to fill to the
desired tab size.

*editor_return_does_auto_indent*
: Pressing return will tab across to match the indentation
of the first line above that has text on it.

*editor_backspace_through_tabs*
: Make a single backspace delete all the space to the left
margin if there is no text between the cursor and the left
margin.

*editor_fake_half_tabs*
: This will emulate a half tab for those who want to program
with a tab spacing of 4, but do not want the tab size changed
from 8 (so that the code will be formatted the same when displayed
by other programs). When editing between text and the left
margin, moving and tabbing will be as though a tab space were
4, while actually using spaces and normal tabs for an optimal fill.
When editing anywhere else, a normal tab is inserted.

*editor_option_save_mode*
: Possible values 0, 1 and 2.  The save mode (see the options menu also)
allows you to change the method of saving a file.  Quick save (0) saves
the file immediately, truncating the disk file to zero length (i.e.
erasing it) and then writing the editor contents to the file.  This
method is fast, but dangerous, since a system error during a file save
will leave the file only partially written, possibly rendering the data
irretrievable.  When saving, the safe save (1) option enables creation
of a temporary file into which the file contents are first written.  In
the event of a problem, the original file is untouched.  When the
temporary file is successfully written, it is renamed to the name of the
original file, thus replacing it.  The safest method is create backups
(2): a backup file is created before any changes are made.  You
can specify your own backup file extension in the dialog.  Note that
saving twice will replace your backup as well as your original file.

*editor_word_wrap_line_length*
: Line length to wrap at. Default is 72.

*editor_backup_extension*
: Symbol to add to name of backup files. Default is "~".

*editor_line_state*
: Show the line state column of the editor: the line numbers, and the marks of
the folds and of the lines the filter hides. Alt-n toggles this option.

*editor_visible_spaces*
: Toggle "show visible trailing spaces".  If editor_visible_spaces=1, they are shown
as '.'

*editor_visible_tabs*
: Toggle "show visible tabs".  If editor_visible_tabs=1, tabs are shown as '<---->'

*editor_show_control_chars*
: Show control characters in caret notation, for example '^M' or '^L'.
If editor_show_control_chars=0, each of them takes one blank cell instead.
The "Toggle control characters" item of the Command menu switches this option.

*editor_persistent_selections*
: Do not remove block selection after cursor movement.

*editor_drop_selection_on_copy*
: Reset selection after copy to clipboard.

*editor_cursor_beyond_eol*
: Allow moving cursor beyond the end of line.

*editor_cursor_after_inserted_block*
: Allow moving cursor after inserted block.

*editor_syntax_highlighting*
: enable syntax highlighting.

*editor_edit_confirm_save*
: Show confirmation dialog on save.

*editor_option_typewriter_wrap*
: Break the line at
**editor_word_wrap_line_length**
while it is typed, the way a typewriter does. One of the three wrap modes of
the options dialog.

*editor_option_auto_para_formatting*
: Format the paragraph the cursor is in while it is typed. The other of the
three wrap modes; off means no wrapping at all.

*editor_option_save_position*
: Save file position on exit.

*editor_group_undo*
: Combine UNDO actions for several of the same type of action (inserting/overwriting,
deleting, navigating, typing)

*editor_wordcompletion_collect_entire_file*
: Search autocomplete candidates in entire file (1) or just from
beginning of file to cursor position (0).

*editor_wordcompletion_collect_all_files*
: Search autocomplete candidates from all loaded files (1, default), not only from
the currently edited one (0).

*editor_stop_format_chars*
: Set of characters to stop paragraph formatting. If one of those characters
is found in the beginning of line, that line and all following lines of paragraph
will be untouched. Default value is
"**-+\*\\,.;:&>**".

*editor_state_full_filename*
: Show full path name in the status line. If disabled (default), only base name of the
file is shown.

*editor_filesize_threshold*
: The size above which a file is opened only after a question. The value takes
a suffix, as in 64M or 512K. Default is 64M.

*editor_show_right_margin*
: Mark the column of
**editor_word_wrap_line_length**
as the right margin. The "Toggle right margin" item of the Command menu
switches this option. Disabled by default.

*editor_simple_statusbar*
: Show a shorter status line, without the offset and the character under the
cursor. Disabled by default.

*editor_check_new_line*
: Ask about the missing newline at the end of the file when it is saved. The
"Check POSIX new line" option of the save mode dialog switches it. Disabled
by default.

# ENVIRONMENT

The variables below are the ones coole reads. Variables such as **TERM**,
**SHELL**, **HOME** or **PATH** are not listed here: the program reads them
to find out where it runs, not to be configured by them.

**COOLE_DATADIR**
: The directory the data files are taken from, in place of
{{pkgdatadir}}. See [FILES](#files).

**COOLE_PROFILE_ROOT**
: The root of the user files, as an absolute path, in place of the home
directory. See [FILES](#files).

**COOLE_SKIN**
: The skin to use, by name or by path. See [Skins](#skins).

**COOLE_KEYMAP**
: The keymap file to use. See [Redefine key bindings](#keys_redefine).

**COOLE_TMPDIR**, **TMPDIR**
: The directory for the temporary files of the program, the first of the two
that is set; /tmp without either.

**COOLE_NO_LUA**
: Set to 1 to start without the Lua runtime. No Lua script is loaded.

**KEYBOARD_KEY_TIMEOUT_US**
: How long to wait for the rest of an escape sequence, in microseconds.

**COLORTERM**
: Read when the colors are chosen. See [Colors](#colors).

**COOLE_LOG_ENABLE**, **COOLE_LOG_FILE**
: A debug log is written when COOLE_LOG_ENABLE is 1. Without **COOLE_LOG_FILE** the
file named by *logfile* in the *[Logging]* section of the *ini* file is used,
and without that entry *~/.cache/coole/coole.log*.

**COOLE_SPELL_LOG**
: The file the spell checker writes its debug log to. It has no switch of its
own: the log is written when the variable names a file.

# FILES

Full paths below may vary between installations. The data files are also
affected by the
**COOLE_DATADIR**
environment variable: if it's set, its value is used instead of
{{pkgdatadir}} in the paths below. The user files follow the XDG base
directories:
*~/.config*,
*~/.local/share*
and
*~/.cache*
stand for
**XDG_CONFIG_HOME**,
**XDG_DATA_HOME**
and
**XDG_CACHE_HOME**
where these are set.

*{{pkgdatadir}}/help/coole.md*
: The help file of the program, and the help files of the editor plugins
beside it.

*{{sysconfdir}}/coole/coole.ini*, *{{pkgdatadir}}/coole.ini*
: System-wide setup files, used only if the user doesn't have his own
**~/.config/coole/ini**
file. If {{sysconfdir}}/coole/coole.ini exists, {{pkgdatadir}}/coole.ini isn't
used.

*{{sysconfdir}}/coole/defaults.ini*, *{{pkgdatadir}}/defaults.ini*
: Global settings, which affect all users, whether they have
~/.config/coole/ini or not. Currently, only
[terminal settings](#terminal-databases)
are loaded from it.

*{{sysconfdir}}/coole/keymap.ini*, *{{pkgdatadir}}/keymap.ini*
: The system-wide key bindings. See
[Redefine key bindings](#keys_redefine).

*{{sysconfdir}}/coole/coole.menu*, *{{pkgdatadir}}/coole.menu*
: The system-wide [user menu](#edit-menu-file).

*{{sysconfdir}}/coole/spell.keymap, ctags.keymap*
: The keys of the spell and the ctags plugins; a file of the same name in
~/.config/coole overrides them.

*{{pkgdatadir}}/syntax/\**
: The system-wide syntax files, used only if the corresponding user's own
file in
**~/.local/share/coole/syntax/**
is missing.

*{{pkgdatadir}}/skins/\**
: The skins that come with the program.

*{{pkgdatadir}}/charsets*
: The list of the codepages.

*{{pkgdatadir}}/lua/scripts/\**
: The Lua scripts that come with the program.

*{{pkgdatadir}}/examples/macros.d/\**
: Example scripts for the macros.

*{{plugins_dir}}/*
: The editor plugins that are loaded at run time.

*~/.config/coole/ini*
: User's own setup. If this file is present then the setup is loaded
from here instead of the system-wide setup file.

*~/.config/coole/keymap.ini*
: User's own key bindings.

*~/.config/coole/menu*
: User's own [user menu](#edit-menu-file).

*~/.config/coole/plugins.ini*
: The plugins and the Lua scripts that are switched off.

*~/.config/coole/term/\<TERM>*
: The keys taught in [Learn keys](#learn-keys).

*./.coole.menu*
: The user menu of the current directory. If this file is present in the
current directory, it is used instead of the home or system-wide menu.

*~/.local/share/coole/history*
: The histories of the input lines.

*~/.local/share/coole/filepos*
: The places the files were left at, for
*editor_option_save_position*.

*~/.local/share/coole/clipboard*
: The clipboard file of the editor.

*~/.local/share/coole/macros*
: The recorded [macros](#macros).

*~/.local/share/coole/macros.d/macro.N.sh*
: The shell scripts of the macros.

*~/.local/share/coole/syntax/Syntax*
: User's own syntax file.

*~/.local/share/coole/skins/*
: User's own skins.

*~/.local/share/coole/lua/scripts/*
: User's own Lua scripts.

*~/.cache/coole/block*
: The block file the user menu and the macro scripts work on.

*~/.cache/coole/temp*
: A temporary file of the editor.

To change the root directory of the user files, you can use the
**COOLE_PROFILE_ROOT**
environment variable. The value of COOLE_PROFILE_ROOT must be an absolute path.
If COOLE_PROFILE_ROOT is unset or empty, the HOME variable is used. If HOME is
unset or empty, the directories are taken from the GLib library.

# LICENSE <!-- help:skip -->

This program is distributed under the terms of the GNU General Public
License as published by the Free Software Foundation. See the built-in
help for details on the License and the lack of warranty.

# AVAILABILITY

The latest version of this program can be found at
<https://github.com/blue-panels/coole/releases> .

# SEE ALSO

cooledit(1), aspell(1), hunspell(1), ctags(1), etags(1), gpm(1),
terminfo(5), sh(1).

# AUTHORS

Paul Sheer (psheer@obsidian.co.za) is the original author of cooledit, which
coole continues. The authors and
contributors are listed in the AUTHORS file in the source distribution.

# BUGS

Bugs should be reported at
<https://github.com/blue-panels/coole/issues> .

Provide a detailed description of the bug, the version of the program you are
running
*(coole -V*
displays this information), and the operating system you are running the
program on. If the program crashes, we would appreciate a stack trace.
