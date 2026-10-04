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

**Command → Build command...** gives a project a command of its own, which
then goes before what was found. With nothing found the plugin asks for one
the first time.

**The keys**

These are the default keys; they can be changed in **Options → Key
bindings → Editor → Build**, or in the *[build]* section of the keymap.

**Alt-Shift-B**
: Build the project: the modified files of it are saved first, and the
output comes in the window **Build** at the bottom of the screen;
**Plugins → Build output** shows it again.

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
