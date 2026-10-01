# coole

coole is a console text editor. It runs full screen in any terminal, from
the Linux console to tmux and a remote session over ssh.

coole continues cooledit, the editor written by Paul Sheer, whose terminal
version lived on for many years as the internal editor of GNU Midnight
Commander. coole takes that editor with the fixes and features it gained
there and develops it as a program of its own.

## Features

- **Syntax highlighting** for a large set of languages and file formats, with
  rules in plain text files that can be changed or added. A file with no
  syntax definition still has its numbers, strings and symbols coloured.
- **Code folding** of a bracket block from the gutter or the Command menu;
  a folded line keeps its text and shows the matching bracket and the number
  of lines.
- **Undo history browser** (`Alt+Shift+U`): the undoable and redoable groups,
  with line, action and a preview, and a jump to any point in the history.
- **Macros** recorded from the keyboard, and a **macro explorer**
  (`Alt+Shift+M`) that lists them, shows their actions, runs, deletes or
  opens them at their definition.
- **ctags and etags navigation**: jump to definitions and declarations from a
  `tags` or `TAGS` file.
- **Spell checking** with aspell or hunspell, whichever is installed; the
  library is loaded at run time.
- **Search and replace** with regular expressions, and a line filter that hides
  the lines that do not match.
- **User menu** (`F11`) of commands run on the file or the selection, from
  `coole.menu` in the system configuration or `.coole.menu` in the current
  directory.
- **Multiple windows**: several files open at once, moved, resized, shown full
  screen and listed.
- **Terminal** (`Ctrl-O`): your shell in a window at the bottom of the screen,
  under the file, hidden and shown again as it was; the terminal of Midnight
  Commander, with its scrollback, search and marking of the output.
- **Preview** (`Ctrl-Alt-P`): the file in a window beside it, the way it is
  meant to read, as you type and following the cursor.  The viewer tells the
  type of the file and asks a renderer for it: markdown comes out with tables,
  code in colors, formulas and mermaid diagrams; JSON indented and colored,
  with the sizes of its objects and arrays and an array of objects as a table;
  XML as a tree, with a run of like elements as a table;
  a type without a renderer is shown as its text.  Scripts and plugins can show any text of theirs in the
  windows of the viewer.
- **Editor plugins**: a plugin framework the ctags, etags and spell plugins are
  built on; see [`doc/PLUGINS`](doc/PLUGINS).
- **Lua scripts** for the editor, run by the Lua runtime plugin and managed
  from the Options menu; see [`doc/LUA_PLUGINS.md`](doc/LUA_PLUGINS.md).
- **Skins**, including 256-colour and true-colour ones.
- **Key bindings** that can be changed in a dialog, with keymaps in the
  default, Emacs and vim styles, a dialog that learns the keys a terminal
  sends, and a key sniffer that shows what a key press arrives as.
- Column blocks, bookmarks, codepage conversion, and saving a file that needs
  root through sudo.

## Building

coole is built with [meson](https://mesonbuild.com) (0.61 or newer) and
ninja. See [`INSTALL`](INSTALL) for all the options and for cross compiling.

### Dependencies

Required: a C compiler, meson, ninja, pkg-config, gettext, GLib 2.58 or newer,
and S-Lang or ncurses. Optional: Lua 5.3 or newer (the Lua scripts), gpm, the
X11 headers, and the check library for the tests.

Debian and Ubuntu:

```sh
sudo apt install build-essential meson ninja-build pkg-config gettext \
    libglib2.0-dev libslang2-dev libgpm-dev libx11-dev liblua5.4-dev check
```

Fedora:

```sh
sudo dnf install gcc meson pkgconf-pkg-config gettext \
    glib2-devel slang-devel gpm-devel libX11-devel lua-devel check-devel
```

Arch Linux:

```sh
sudo pacman -S base-devel meson pkgconf gettext glib2 slang gpm libx11 lua check
```

Alpine:

```sh
sudo apk add build-base meson pkgconf gettext-dev glib-dev slang-dev \
    gpm-dev lua5.4-dev check-dev
```

FreeBSD:

```sh
sudo pkg install meson ninja pkgconf gettext glib libslang2 libX11 lua54 check
```

macOS (Homebrew):

```sh
brew install meson pkg-config gettext glib s-lang check
```

### Build, test and install

The build happens in a directory of its own; the source tree stays clean.

```sh
meson setup build
meson compile -C build
meson test -C build            # the unit tests, needs the check library
./build/src/coole file.c       # run it without installing
sudo meson install -C build    # into /usr/local
```

The options can be listed with `meson configure build` and changed with
`meson configure build -Dname=value`; for example `-Dscreen=ncurses` builds
with ncurses instead of S-Lang, and `-Dlua=disabled` builds without Lua.

### Installing into /usr, and uninstalling

The installation directories can be changed in an existing build directory.
They are compiled into the program, so rebuild as yourself before installing:
`sudo meson install` would otherwise rebuild as root and leave root-owned
files in the build directory.

```sh
meson configure build --prefix=/usr --sysconfdir=/etc
meson compile -C build
sudo meson install -C build
```

To install into your home directory without sudo, use
`--prefix=$HOME/.local` and `meson install -C build`.

`uninstall` removes every file the last install put in place, and the
directories it created:

```sh
sudo ninja -C build uninstall
```

### Debug, release and sanitizer builds

Build directories of different kinds live side by side:

```sh
meson setup build-debug   --buildtype=debug
meson setup build-release --buildtype=release
meson setup build-asan    -Db_sanitize=address
```

Each is built and tested the same way, e.g.
`meson compile -C build-asan && meson test -C build-asan --print-errorlogs`.
The tests pass under AddressSanitizer and LeakSanitizer; a leak or a memory
error fails the test that caused it.

## Usage

```sh
coole [options] [+lineno] file1[:lineno] [file2[:lineno] ...]
```

`+lineno` or `file:lineno` opens a file at that line. Each file opens in a
window of its own. The options:

| Option | Meaning |
| --- | --- |
| `-V`, `--version` | Show the version and the build configuration |
| `-f`, `--datadir` | Print the data directory |
| `-F`, `--datadir-info` | Print the directories used for data and configuration |
| `--configure-options` | Print the build options the program was built with |
| `--no-lua` | Do not load Lua support |
| `-x`, `--xterm` | Force xterm features |
| `-X`, `--no-x11` | Do not use the X11 connection |
| `-g`, `--oldmouse` | Use the old highlight mouse tracking |
| `-d`, `--nomouse` | Disable the mouse |
| `-t`, `--termcap` | Use termcap instead of terminfo (S-Lang builds) |
| `-s`, `--slow` | Draw for a slow terminal |
| `-a`, `--stickchars` | Draw lines with ASCII characters |
| `-K FILE`, `--keymap=FILE` | Load key bindings from FILE |
| `--nokeymap` | Use the built-in key bindings |
| `-b`, `--nocolor` | Black and white |
| `-c`, `--color` | Force colour |
| `-S NAME`, `--skin=NAME` | Use the skin NAME |

The user configuration is in `~/.config/coole`, the data (history, macros,
the positions in files) in `~/.local/share/coole` and temporary files in
`~/.cache/coole`. The system configuration is in `/etc/coole` for a build
with `--sysconfdir=/etc`.

## Packages

The packages are **`coole`** and **`coole-lua`**, the Lua runtime and the
editor scripts. They install one program, `coole`.

There are no releases yet. The recipes for Debian and Ubuntu, Fedora, Arch and
Gentoo are in [`packaging/`](packaging/README.md), and a release builds them
from its tag.

## Documentation

- Built-in help: press `F1` in the editor
- Manual page: `coole(1)`
- [Wiki](https://github.com/blue-panels/coole/wiki)

## Translations

Translations are merged into `po/` by a maintainer, so please do not open pull
requests that edit `po/*.po` by hand.

An ordinary build compiles the checked-in translations without updating the
`.po` files. Maintainers remake the template with
`meson compile -C build coole-pot` and merge the `.po` files with it using
`meson compile -C build coole-update-po`.

## Reporting problems

Open an issue: <https://github.com/blue-panels/coole/issues>

Include `coole --version`, your OS and distribution, and the compiler and
build options if you know them (`coole --configure-options`). For a crash,
attach a `gdb` backtrace (`gdb coole core`, then `where`).

## License

GNU General Public License, version 3 or any later version. See
[`COPYING`](COPYING). The authors of cooledit and of the code coole took
over are listed in [`AUTHORS`](AUTHORS).
