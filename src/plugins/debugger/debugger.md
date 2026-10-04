# Debugger <!-- help:notitle -->

**Debugger plugin of the editor**

Runs a program of the project under GDB: breakpoints in the gutter of the
source, steps through it from the source window, and the call stack, the
local variables and the watches beside it.

**The start**

Put a breakpoint on a line with **Debug → Toggle breakpoint** and choose
**Debug → Start**. The first time, the debugger makes a configuration of its
own:

1. The project is the one the project plugin finds for the file: no
directory has to be named. **Debug → Open project...** names another.
2. The program is the one the build has made, found by the build plugin
among the programs of the project with debug information; of several, the
newest come first in a list to choose from.
3. A program without debug information, or built with optimization, is
told of, with the command that builds it fit for debugging.
4. The form **Debug configuration** shows it all at once: the name, the
program, its arguments, the directory it runs in, its environment, GDB, and
whether the project is built before every start. Enter takes it.

Then the project is built (its modified files saved first), GDB starts, and
the program runs to the first breakpoint. When the build fails, the start
waits: the lines with errors are marked, and **Alt-Shift-J** goes from one
to the next.

The arguments and the environment are split as a shell would split them,
but no shell runs them: an entry of the environment with spaces is
*GREETING="hello world"*.

**The configurations**

A project may have several: **New configuration...** guesses another one,
**Select configuration...** chooses the one **Start** runs, **Configure
selected...** changes it. They are kept with the settings of the user, or,
when **Keep in the project** is checked, in *.coole/debug.ini* in the project,
with the names relative to it, for everyone who works on it. The breakpoints
and the watches are one's own: they stay with the settings of the user.

**Step mode**

While the program is stopped the source windows take the debugger keys; the
keys that move around the text, search, select or switch windows stay the
editor's, and a key that would change the text beeps.

**F5**
: Continue.

**F6**
: Pause the running program.

**F7**, **F8**, **F9**
: Step into, step over, step out.

**F4**
: Run to the line of the cursor.

**F2**
: Toggle a breakpoint on the line of the cursor.

**Enter**
: Evaluate the selection or the word under the cursor; the value comes in a
dialog that can add it to the watches.

**Shift-F5**
: Stop the program.

**Esc**
: Go back to editing until the next stop.

The Debug session window takes the same keys when it has the focus; there F2
and F4 work on the cursor of the topmost file window, and F10 closes the
window. **Debug → Debug session...** opens it again. The keys can be changed
in **Options → Key bindings → Editor → Debugger**, or in the *[debugger]*
section of the keymap, *DebugStepOver = alt-o* for one; *f15* is the name of
Shift-F5.

**The marks**

**●**
: a breakpoint

**◌**
: a breakpoint GDB has not taken yet, or has refused

**▶**
: the line the program stopped on

**◉**
: the line the program stopped on, with a breakpoint

A breakpoint moves with its line as lines are inserted or deleted above it;
GDB keeps the old line till the next start, since the program running was
built from the text as it was. GDB stops only on lines with code: a
breakpoint on another line moves to the next line GDB stops on. The skin
gives the marks in *[widget-editor]* (*breakpoint-char*, *exec-char* and the
others) and their colors in *[editor]* (*breakpoint*, *execmark*,
*execline*); a terminal that is not UTF-8 shows *o*, *?*, *>* and *@*.

**What the program is at**

**Call stack...** chooses a frame: the mark of the current line goes to it,
and its variables come in **Local variables**. **Add watch...** keeps an
expression for the project; **Watches** shows its value in the frame. A
watch shows a value: it does not stop the program when the value changes.

The output of the program comes in **Debug output**; **Send line...** gives
a line to a program that reads lines. **Stop** ends the session and keeps the
breakpoints, the configurations and the watches.

The debugger runs one local program at a time. A program that takes the
whole terminal needs a terminal of its own.
