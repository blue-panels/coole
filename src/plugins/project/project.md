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
opened gives the project; **Project → Open project...** chooses another.
**Project → Close project** leaves the project: the files opened then make
none, till a project is opened again. The debugger works on the project
chosen here, and leaves it when it is closed.

The files of the project are what git knows and does not ignore, in a
repository; else everything under the project but hidden directories and
build trees.

**The keys**

These are the default keys; they can be changed in **Options → Key
bindings → Editor → Project**, or in the *[project]* section of the keymap.

**Alt-P**
: Open a file of the project. Type some letters of its name: they are looked
for in order, in the last part of the name first, so *edwi* finds
*src/editor/editwidget.c*. With nothing typed the recent files are listed.
The same line goes further:

*main:42*
: The file at that line.

*:42*
: The file in front at that line.

*@divi*
: The functions, structures and the other symbols of the project whose names
have these letters: *divide* in *calc.c*.

*calc@*
: The symbols of a file, in their order; *calc@tw* those of them that match.

The symbols come from the index of the plugin ctags: without it the line
says so.

**Ctrl-E**
: The recent files of the project, the latest first.

**Alt-T**
: Show the tree of the project, give it the focus, and hide it; it is
**Project → Project tree** and **Window → Project tree** too.

**Alt-A**
: Switch between a source and its header: *calc.c* and *calc.h*, next to it
or anywhere in the project.

**Alt-F7**
: Find a text in the files of the project, **Project → Find in project...**
too. The word of the cursor is offered, and the list follows the line as it
is typed, from two letters: *src/calc.c:42: the line*, the first 5000 of
them, the file around the line of the cursor under it. The text is looked
for as it is, not as a pattern; with no capital letter in it, in any case.
**Up**, **Down**, **PgUp** and **PgDn** move in the list; Enter goes to the
line chosen, to the text in it. Binary files and *.coole* are skipped.

**The tree**

The title of the tree is the root of the project, the home as *~*:
*[~/dev/sh-debug]*; when it does not fit, its end is kept.

At the right of a file stand its lines as they are on the disk: *336*. A file
larger than 1M or one that is not text shows its size instead: *27K*,
*3M*. They are counted again when the file changes.

Before a name the tree can show a mark of what it is, in its color, from the
section *[project]* of the skin: *source-char = x* and *source = green*. The
keys are file, source, header, build, doc, config, script and binary for the
files, and function, prototype, type, enum, macro and variable for the
symbols. A mark of *%e* is the first letter of the extension: *c* for
*calc.c*, *h* for *calc.h*. A skin without them shows no marks; *darkfar*
has them.

What a file or a symbol is, by the name, the extension or the kind from
ctags, is in *project-marks.ini*; *~/.config/coole/project-marks.ini* adds
to it, and a key there replaces the same key of coole.

**Up**, **Down**, **PgUp**, **PgDn**, **Home**, **End**
: Move in the tree.

**Enter**
: Open the file, or open or close the directory; on a symbol, go to its
line.

**Right**, **Left**
: Open the directory, close it or go to the one above; on the file of the
outline, show its symbols or fold them.

**A letter**
: Go to the next name that starts with it.

**Esc**, **F10**
: Close the tree.

The file of the topmost window is shown in bold, and the tree opens down to
it whenever another file comes to the front.

Under that file come its symbols, its outline: the functions, the
structures, the types, the macros and the variables, in their order, the
one the cursor is in shown in bold. The outline is made by the plugin
ctags from the text as it is, saved or not, whenever the editor rests
after a change; without ctags there is none.

**For the other plugins**

The plugin offers the service *project*: *root* gives the project of a file
(argument *file*) or the project there is, *files* its files, *recent* the
recent ones, *open* (argument *root*) chooses the project of a directory, as
**Open project** does; the signal *changed* says the project is another one
(*root*, *chosen*), an empty *root* that it was closed.
