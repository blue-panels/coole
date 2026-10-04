# Project <!-- help:notitle -->

**Project plugin of the editor**

Keeps the project a file is part of at hand: any of its files is a few
letters away, the latest ones are listed first, and the tree of the project
stands at the left of the screen.

**The project**

The project of a file is found from the directory of the file up: the nearest
directory with a *.coole* or a *.git* in it, else the outermost one with a
build file (*meson.build*, *CMakeLists.txt*, *Makefile* and the like) up to
the home directory, else the directory of the file itself. The first file
opened gives the project; **File → Open project...** chooses another.

The files of the project are what git knows and does not ignore, in a
repository; else everything under the project but hidden directories and
build trees.

**The keys**

These are the default keys; they can be changed in **Options → Key
bindings → Editor → Project**, or in the *[project]* section of the keymap.

**Alt-Shift-P**
: Open a file of the project. Type some letters of its name: they are looked
for in order, in the last part of the name first, so *edwi* finds
*src/editor/editwidget.c*. With nothing typed the recent files are listed.

**Ctrl-E**
: The recent files of the project, the latest first.

**Alt-Shift-T**
: Show the tree of the project, give it the focus, and hide it.

**Alt-Shift-A**
: Switch between a source and its header: *calc.c* and *calc.h*, next to it
or anywhere in the project.

**The tree**

**Up**, **Down**, **PgUp**, **PgDn**, **Home**, **End**
: Move in the tree.

**Enter**
: Open the file, or open or close the directory.

**Right**, **Left**
: Open the directory, close it or go to the one above.

**A letter**
: Go to the next name that starts with it.

**Esc**, **F10**
: Close the tree.

The file of the topmost window is shown in bold, and the tree opens down to
it whenever another file comes to the front.

**For the other plugins**

The plugin offers the service *project*: *root* gives the project of a file
(argument *file*) or the project there is, *files* its files, *recent* the
recent ones; the signal *changed* says the project is another one.
