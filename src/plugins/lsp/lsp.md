# LSP <!-- help:notitle -->

**Language servers**

A language server knows a language as a compiler does: it tells the problems of
the code, what a name is, where it is defined and how a word goes on. The LSP
plugin runs the servers of the languages and asks them for the files of the
editor. The servers are programs of their own, clangd for C and C++, pylsp or
pyright for Python, rust-analyzer, gopls and so on; they have to be installed.

**When a server runs**

A file whose language has a server in
**lsp.json**
is served by it. The server starts by itself when the file is opened, if
coole was started on a project with -D; else LSP Start in the LSP menu starts
it. One server serves all the files of a project: the directory with
**.coole**,
else the nearest one with a file the server names, compile_commands.json for
clangd for one, else the root of the git, hg or svn repository. A file in no
project is served from its own directory.

A server with no file open stops after a minute. One that fails is started
again, three times in a minute at most; then LSP Restart starts it.

**The keys**

**Ctrl-Space**
: The list of completion of the word at the cursor, of all the server offers
when there is no word yet. On a project of -D the list comes by itself as a
word is typed, and after a dot, "->" or "::", with the members of what is
before it.

The list is under the word and leaves the keys to the text: what is typed
goes on in the word, and the list keeps what fits it, the best first. The
characters typed are found anywhere in a name, in their order, "mkg" finding
make_global; those found are in another color. A letter at the left says what
an item is: f a field, m a method, F a function, v a variable, C a class, S a
struct, T a type parameter, # a constant, k a keyword; what the item is, its
type, is at the right.

**Up, Down, PgUp, PgDn**
: Choose in the list.

**Enter, Tab**
: Put the item chosen in place of the word, with what the server adds
elsewhere, an include for one; one Undo takes all of it back.

**., ->, ::**
: Put the item chosen in place of the word, then the dot, and show the list
of its members.

**Esc**
: Close the list. Any other key that is no part of a word closes it too, and
does what it does; Tab is the editor's again once there is no list.

**The menu Navigate**
: Go to definition (Ctrl-J), Find references (Alt-g) and the others of ctags
ask the language server first, when it serves the file; the tags answer only
when it does not.

**Alt-., Ctrl with a click**
: Go to the definition of the name at the cursor, or clicked; to a list of them when
there are several. A file not open yet is opened as Navigate opens one, and
Back returns.

**Alt-Shift-.**
: Go to the prototype of a function in its header, the declaration of the name
at the cursor; in the menu Navigate.

**Alt-g**
: Find the uses of the name at the cursor: a list of them with their lines,
the definition among them, the one the cursor is on chosen; Enter goes to
one.

**Alt-,**
: Quick documentation: what the server says of the name at the cursor, its
type and its documentation, after the problems of the line, in a box under
the cursor, above it when there is no room under it. The text is as the
server wrote it, at the left, in half of the screen at most; when it is
longer, the mouse wheel, PgUp and PgDn scroll it, and the keys say so on its
frame. The Markdown of the server is drawn in the colors of the skin: the
headings as the title of a dialog, the names in backquotes as its hot keys,
the rules as its lines, and the blocks of code on the colors of the editor.
The code the server prints of the name gives way to the source of its
definition when that is longer: a structure with its fields, a function with
its first lines; ten lines are shown at most, "..." for the rest. The
expansion of a macro, longer than the macro, stays. Alt-, again has it read:
the arrows, Home and End scroll it too, and Alt-, takes it away. Esc only
takes it away; any other key takes it away and does what it does; it goes
too when the cursor moves.

**Alt-(**
: Parameter info: the signature of the call the cursor is in, in a box over
its line, the parameter of the moment hot; under it, what the parameter is, as
the comment of the function says it after "@param", or else the first sentence
of the comment: not all of it, so that the box stays small. In the mode of an IDE, -D, the characters the server names
open it as they are typed, "(" and "," for clangd, as completion_on_trigger
says for the list of a completion. It follows the cursor as the arguments are
typed, the hot parameter moving after a ","; it goes when the cursor leaves
the call, at Esc, and when the window loses the focus. A list of a completion
has it wait until the list goes. When a function has several signatures, the
box says which of them it shows, "1/3", and Alt-Up and Alt-Down go through
them.

The keys are those of the section
**[lsp]**
of the keymap, which Options > Key bindings changes. A key of LSP in a file no
server serves is the editor's.

**The problems**

The lines with a problem have a mark in the gutter:
**E**
for an error,
**W**
for a warning,
**i**
for a note. LSP Problems lists them all, Enter goes to the one chosen.

**The menu LSP**

**Start server, Stop server, Restart server**
: The server of the file of the window. A server stopped stays so until it is
started again.

**Status**
: The servers, the projects they serve, their state, and why the file of the
window is not served when it is not.

**Log**
: What the server of the file has written on its error output and the
messages it has sent, the newest last.

**Project setup**
: Look at the project as its server sees it, and mend what goes wrong; it
knows clangd for now. It checks every header alone, as clangd reads it: the
headers of many C projects count on what the files that include them include
first, and have errors alone the build has not. It finds the headers the files
of the project include first and tries them, and says how many errors are
left with the ones that help. It finds the functions the tests define again,
whose copies take the place of the real ones in Go to definition, and offers
to leave the directories of the tests out of the index. Show the file shows
what it would write; Apply writes it in .clangd at the root of the project, in
a block of its own between the lines "# >>> coole" and "# <<< coole", the
rest of the file kept, and starts the server again. The checks take a few
seconds to a minute; Cancel stops them.

**Settings**
: When the servers start and when they complete by themselves, and the
servers: the list says which are installed, Enter opens one, its command, the
files it serves, the files that mark the root of a project, whether it is on,
whether it serves files in no project, and when it starts. Reset sets a server
of the system back as the system has it, Delete takes away one of the user's,
New server adds one. OK writes ~/.config/coole/lsp.json, with only what is
not as the system has it, and the servers start again as it says. The same
dialog is Configure of the plugin in Options > Manage plugins.

**lsp.json**

LSP Settings changes them; what it does not have is in the files. The servers
are those of the lsp.json of the system, /etc/coole/lsp.json, and of
**~/.config/coole/lsp.json**.
A server of the user's file takes the place of the one of the same id, and a
setting of the file that of the system's. For example

```
{
  "autostart": "always",
  "completion_on_trigger": "always",
  "backends": [
    {
      "id": "clangd",
      "command": ["clangd", "--background-index", "--clang-tidy",
                  "--compile-commands-dir=${build_dir}"],
      "extensions": {".c": "c", ".h": "c", ".cpp": "cpp", ".hpp": "cpp"},
      "root_markers": ["compile_commands.json", "compile_flags.txt"],
      "single_file": true,
      "settings": {}
    }
  ]
}
```

**autostart**
: "ide", the default, starts a server by itself on a project of -D only;
"always" on any file; "never" by LSP Start only. A backend may have its own.

**completion_on_trigger**
: The list of completion shown by itself after a character the server
completes after, a dot, "->" or "::", as it is typed: "ide", the default, on a
project of -D only; "always"; "never", by Ctrl-Space only. When the server has
nothing to offer there, nothing is shown. Parameter info opens by itself after
"(" and "," as the list does, by Alt-( only with "never".

**completion_on_arguments**
: The list of completion shown by itself after the "(" of a call and after
each ",": all that may be an argument, as the server ranks it. "never", the
default: a list at each call is in the way, and Ctrl-Space shows it; "ide", on
a project of -D only; "always". Parameter info stays over the line while the
list is under it.

**command**
: The program and its arguments, without a shell. ${build_dir} in an
argument is the build directory of meson or cmake of the project, in its root
or next to it; an argument with it is left out when there is none. clangd
finds compile_commands.json so.

**extensions**
: The ends of the names of the files the server serves, with their language.

**root_markers**
: The files or the directories that mark the root of a project for the
server; "*.csproj" is a pattern.

**single_file**
: Whether a file in no project is served; true by default.

**environment, initialization_options, settings**
: Variables for the program, null taking one away; what the server is given
when it starts; and the settings it asks for.

**enabled**
: false: the server is there and serves nothing; true by default.

The file is read when coole starts, and again when the settings dialog writes
it.
