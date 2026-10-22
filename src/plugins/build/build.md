# Build <!-- help:notitle -->

**Build plugin of the editor**

Builds the project of the file in front, shows what the compiler says, marks
the lines it names and goes to them.

**How the project is built**

The project is the one the project plugin finds for the file. Its build is
found by itself:

*meson*
: a build directory of meson in the project or next to it, whose source is
the project: *meson compile -C* that directory.

*cmake*
: likewise, a build directory of cmake: *cmake --build* that directory.

*make*
: a *Makefile* in the project: *make* there.

*the language*
: else the command of the language of the project, as *presets.ini* has it:
*cargo build --message-format=short* for a project with a *Cargo.toml*,
*go build ./...* for one with a *go.mod*. Its errors are listed as those of
gcc are; a line of go, *./main.go:12:2: undefined: x*, has no word *error*,
and is an error when the file it names is there.

**Presets of the languages**

*presets.ini* of coole, in its directory of configuration, has a group for
each language: the files in the root of a project that tell it (*markers*),
the endings of its files (*extensions*), the command that builds it
(*build*), and how it is debugged (see the help of the debugger). The
language of a project is that of a marker in its root, else that of the file
in front, by its ending, when the file is in the project.
*~/.config/coole/presets.ini* has groups of its own: they come first, and a
group of it replaces that of coole of its name, all of it. A mistake in it
is told once, with its line.

**The settings of a project**

A project opened as one, with *coole -D* or **Open project**, takes a copy
of the preset of its language: *.coole/project.ini* in the project, its
group *[project]*, with the keys of *presets.ini* and *language*, the name of
the language. From then on the project has it: what is changed there stays.
A key the copy has not is that of the language in *presets.ini*, of the user
first, so that what coole adds to a preset later reaches the projects copied
before; a key left empty, *build=*, has none. A project with a build
system of its own, meson, cmake or a *Makefile*, has no *build* in its copy.
A file opened alone, without a project, leaves no file in its directory. A
*.coole/project.ini* that cannot be read is told of, with its line, and left
as it is until it is put right.

**Command → Build command...** gives a project a command of its own,
*command* in *.coole/project.ini*, which then goes before what was found.
With nothing found the plugin asks for one the first time. A command given
before there was this file, kept with the settings of the user, goes into
it with the copy.

**Command → Language defaults of the project...** makes *.coole/project.ini*
the preset of the language again, without the command given to the project.
The debug configurations stay as they are, until they are made anew.

**The keys**

These are the default keys; they can be changed in **Options → Key
bindings → Editor → Build**, or in the *[build]* section of the keymap.

**Alt-Shift-B**
: Build the project: the modified files of it are saved first, and the
output comes in the window **Build** at the bottom of the screen;
**Window → Build output** shows it again.

**Alt-Shift-J**
: Go to the next error, or to the next warning when there is no error.

**Alt-Shift-K**
: Go to the previous one.

**The marks**

A line the compiler has named an error of is marked with a cross in the
gutter, one with a warning with an exclamation mark, till the next build.
The skin gives them in *[widget-editor]* (*build-error-char*,
*build-warning-char*) and their colors in *[editor]* (*builderror*,
*buildwarning*).

**For the other plugins**

The plugin offers the service *build*: *info* (argument *root*) tells how the
project is built and the programs with debug information it has built,
newest first; *elf* (argument *path*) whether a file is a program, has debug
information and was built with optimization; *run* (argument *root*) starts
a build, whose end the signal *finished* tells (*root*, *ok*, *errors*).
