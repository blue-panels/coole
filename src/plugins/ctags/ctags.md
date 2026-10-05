# Ctags <!-- help:notitle -->

**Ctags plugin of the editor**

Jumps to the definition of the name under the cursor, and finds where a name
is used, from a tags file that ctags built.

**The index**

The index of a project is kept by the plugin itself: when a file of a
project is opened (a directory with *.coole*, *.git* or a build file, the
project the project plugin finds), the plugin indexes it in the background
with ctags, into *.coole/tags* of the project, and does so again when the
index is older than the files and whenever a file of the project is saved.
The files indexed are those of the project: what git knows and does not
ignore, else what is under it but hidden directories and build trees.

A *tags* file at the root of the project is the user's: it is used as it is
and not built again by itself; Reindex builds it again. Without a project,
a *tags* file is looked for in the directory of the file and the directories
above it. The home directory and the root of the disk are not indexed by
themselves.

The plugin needs the program ctags, Universal Ctags best:

```
sudo apt install universal-ctags
```

When it is not there, a command of the plugin says so, once.

The options of ctags for the index of a project are those of the settings
(*ctags_args*), unless the project has its own: the form **Debug
configuration** of the debugger has a line for them, kept with the settings
of the user by the root of the project.

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
