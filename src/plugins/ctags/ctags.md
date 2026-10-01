# Ctags <!-- help:notitle -->

**Ctags plugin of the editor**

Jumps to the definition of the name under the cursor, and finds where a name
is used, from a tags file that ctags built.

**The tags file**

The plugin looks for
*tags*
in the directory of the file and in the directories above it, so a tags file
at the top of a project serves the whole tree. It is built by ctags, for
example

```
ctags -R .
```

or by the plugin itself: the Reindex repository command runs ctags over the
project. Without a tags file the commands say so and do nothing.

**The keys**

These are the default keys; they are kept in
*ctags.keymap*
(see Settings below).

**Ctrl-Enter**
: Jump to the declaration of the name under the cursor. Where several
declarations answer, a list asks which one.

**Ctrl-Backslash**
: Find a declaration by its name.

**Alt-g**
: Find the references to the name under the cursor.

**Alt-Minus, Alt-Plus**
: Back to where the last jump started, and forward again, like the back and
forward of a browser.

**Alt-h**
: The history of the jumps.

**Alt-=**
: The members of a class or a structure.

**Alt-q**
: Search a symbol of the project.

**Alt-y**
: The symbols of the current file.

**Shift-Tab**
: Complete the symbol before the cursor.

The same commands, and a few more (search a file, load a tags file by hand,
manage the repositories, reindex, the configuration), are in the Navigate
menu of the editor and in the menu of the plugin in the Plugins menu.

**The dialogs**

The list of definitions shows the file, the line and the kind of every hit;
Enter opens the one under the cursor, Esc leaves the editor where it is. The
list of references does the same for the places a name is used.

**Settings**

The settings of the plugin, which the Manage plugins dialog of the Options
menu opens, hold the ctags command and its arguments, whether the search is
case sensitive, and whether the tags file is looked for by itself. They are
kept in the
**[ctags]**
section of
*~/.config/coole/ini*.
The keys are read from
*ctags.keymap*
of the system configuration directory of coole, and then from
*~/.config/coole/ctags.keymap*,
which overrides it. The history of the jumps is kept in
*~/.local/share/coole/ctags/*.
