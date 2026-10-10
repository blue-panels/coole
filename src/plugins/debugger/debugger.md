# Debugger <!-- help:notitle -->

**Debugger plugin of the editor**

Runs a program of the project under GDB, or under the debug adapter of its
language: breakpoints in the gutter of the source, steps through it from the
source window, and the call stack, the local variables and the watches
beside it.

**The start**

Put a breakpoint on a line with **Ctrl-B** and run with **Alt-Shift-R** (or
**Debug → Toggle breakpoint** and **Debug → Start or continue**). The first
time, the debugger makes a configuration of its own:

1. The project is the one the project plugin finds for the file: no
directory has to be named. **Debug → Open project...** names another.
2. The program is the one the build has made, found by the build plugin
among the programs of the project with debug information; of several, the
newest come first in a list to choose from.
3. A program without debug information, or built with optimization, is
told of, with the command that builds it fit for debugging.
4. The form **Debug configuration** shows it all at once: the name, the
program, its arguments, the directory it runs in, its environment, GDB, the
options of ctags for the index of the symbols of the project (when the
plugin ctags is on; a change indexes the project again), and whether the
project is built before every start. Enter takes it.

Then the project is built (its modified files saved first), GDB starts, and
the program runs to the first breakpoint. When the build fails, the start
waits: the lines with errors are marked, and **Alt-Shift-J** goes from one
to the next.

**The program in a terminal**

With **Run in a terminal window** checked in the form, as it is at first,
the program runs in a terminal of its own, the tab **Program** at the
bottom: what it writes is there, and what is typed there goes to it, every
key, F1 and F10 too. A program that takes the whole screen, an editor or
coole itself, runs there as in any terminal. A click on the tab, or
**Window → Program**, goes to it; **Alt-Shift-G** leaves it for the panel of
the debugger. The tab stays when the program ends, to be read, and is
cleared when it starts again. Without the plugin terminal, or unchecked,
the output of the program comes in the console, and **Send line...** gives
it a line to read.

The arguments and the environment are split as a shell would split them,
but no shell runs them: an entry of the environment with spaces is
*GREETING="hello world"*.

**Debug mode**

*coole --debug DIR* (or *-D DIR*) opens the project of that directory to
debug it: its latest file instead of an empty one, and at the right the tree
of the project with the panel of the debugger under it. The panel says what
to do next while nothing runs.

In debug mode the file windows take the keys of the debugger all the time,
not only while the program is stopped: F3 to F8 are the debugger's, F2, F9
and F10 stay Save, the menu and Quit, and the text can be edited as ever.
**Debug → Debug keys in files** turns debug mode on and off without
*--debug*.

In debug mode the editor asks before it ends, F10 being easy to press by a
slip; while a program runs under the debugger it asks whether to stop it.

**F3**
: Index the symbols of the project again, in the background (with the
plugin ctags); **Navigate** jumps by them.

**F6**
: Toggle a breakpoint on the line of the cursor.

**F5**
: Build and run, or go on when the program is stopped.

**The keys anywhere**

These work in a file window too, since the editor has nothing on them:

**Alt-Shift-G**
: Go to the panel of the debugger, and back to the file.

**Ctrl-B**
: Toggle a breakpoint on the line of the cursor.

**Alt-Shift-R**
: Run: build the program and start it, or go on when it is stopped. The
first time it asks what to run.

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

**Shift-F6**
: Pause the running program.

**F7**, **F8**, **Shift-F8**
: Step into, step over, step out.

**Shift-F7**, **Shift-F4**
: Step into, step over, by one instruction of the machine (**Ctrl-F7** and
**Ctrl-F8** too, where the terminal sends them). These two keys are the
debugger's only while the program is stopped: the rest of the time they are
Search again and Replace again, in debug mode too.

**F4**
: Run to the line of the cursor, **Debug → Run to cursor** too: from where
the program stopped, it stops at that line, at a breakpoint before it, or
when the function it is in returns. A line of another function is reached
with a breakpoint, or with **Debug → Run to function...**.

**F6**
: Toggle a breakpoint on the line of the cursor.

**F3**
: Index the symbols of the project again.

F2, F9 and F10 stay Save, the menu and Quit.

**Enter**
: Evaluate the selection or the word under the cursor; the value comes in a
dialog that can add it to the watches.

**Shift-F5**
: Stop the program.

**Esc**
: Go back to editing until the next stop.

The keys can be changed in **Options → Key bindings → Editor → Debugger**, or
in the *[debugger]* section of the keymap, *DebugStepOver = alt-o* for one;
*f15* is the name of Shift-F5, *f18* that of Shift-F8, *f17* that of
Shift-F7.

**The layout of debugging**

When the debugger starts, the windows are put as the layout *Debug* has
them: the tree of the project at the right, the panel of the debugger under
it, the console coming at the bottom when there is something in it. When
it stops, they are put back as they were, the console staying to be read.
**Window → Layout...** says which windows *Debug* has (save another as
*Debug*), and turns this off.

**The panel and the console**

The start puts the panel of the debugger at the right of the source, all its
height, and the console under the source. The panel has the state of the
program, the local variables of the frame, the watches, the call stack and
the breakpoints; the console has the output of the program and what GDB
says. **Window → Debugger panel** and **Window → Debug console** open them
again, beside the windows of the other plugins.

In the panel the debugger keys work as in step mode (there F6 and F4 work on
the cursor of the topmost file window, F10 closes the panel), and:

**Up**, **Down**, **PgUp**, **PgDn**, **Home**, **End**
: Move from one row to another.

**Enter**
: On a variable or a watch, its whole value in a dialog that can add it to
the watches; on a frame, go to it; on a breakpoint, go to its line.

A value with members, a structure, an array, a pointer to one, an object of
Python, comes as a tree, its members shown. **Enter** or **Right** opens a
member, **Left** closes it or goes up to the one it is of; **Add watch**
watches the member of the cursor, *p->corner[1]* for one, by the expression
the debugger gives or one made as C has it. The members come from the
debugger as they are opened. **Evaluate expression...** and **Enter** in a
source window give the same tree.

**Space**
: Disable a breakpoint, or enable it again: GDB keeps it but does not stop
on it, and its mark is an empty circle.

**Del**
: Remove a watch or a breakpoint.

**Ins**
: Add a watch.

**:**
: A command of GDB itself, for what the panel does not have: *p x*,
*info registers*, *x/8x buf*. What GDB answers comes in the console. Under
a debug adapter it is an expression or a command of the adapter's console,
*p 1+2* for GDB, a line of Python for debugpy.

**The registers**

Under the local variables the panel has the registers of the frame: the
general ones, the program counter and the flags, in hexadecimal; those the
last step has changed are in bold. They are asked of the debugger only while
they are shown: Enter on their title shows and hides them, and a stop in code
with no source, or in a source of assembler (*.s*, *.S*, *.asm*), shows them.
Enter on a register gives its value in the dialog
that adds it to the watches, *$rax* for one. A program of a language with no
registers, Python say, has no such title.

**The instructions**

**Debug → Disassembly** opens a window under the source, a tab beside the
console, with the instructions of the function of the frame: each source line
before its instructions, when the program has its source, and *>* on the
instruction the program is at. A stop in code with no source opens it by
itself, and so does the first step by an instruction, which moves in no line of
the source. It follows the steps and the frame chosen in the call stack.

**Up**, **Down**, **PgUp**, **PgDn**, **Home**, **End**
: Move from one instruction to another.

**F6**
: Put a breakpoint on the instruction of the cursor, or take it off. Such a
breakpoint is in the panel too, *● \*0x401136*, where Del takes it off; it
is not kept for the next session.

**Enter**
: Go to the source line of the instruction.

The other keys of the debugger work there as in the panel.

**The values in the source**

While the program is stopped the lines of the function, from its start down
to the line it stopped on, show the values of the local variables they name,
after their text. In a source of assembler they are the registers, from the
label above: *add %rcx, %rdi* gets *rcx = 0xa, rdi = 0x0*; a register is
named by its parts too, *eax* or *al* for *rax*. The values go when the
program runs on and when the text is edited. The skin gives their color,
*editnote* in *[editor]*.

A step that ends in code whose source is not on this machine, a library
with debug information but without its sources, goes on out of it: a step
into *printf* comes back to the call, a step out of *main* ends the program.
A step by an instruction stays there: the panel names the function and the
address, and the instructions and the registers come. A frame without its
source is named in the panel and opens nothing.

**The marks**

**●**
: a breakpoint

**○**
: a breakpoint that is disabled

**◊**
: a breakpoint GDB has not taken yet, or has refused

**>**
: the line the program stopped on

**♦**
: the line the program stopped on, with a breakpoint

A breakpoint moves with its line as lines are inserted or deleted above it;
GDB keeps the old line till the next start, since the program running was
built from the text as it was. GDB stops only on lines with code: a
breakpoint on another line moves to the next line GDB stops on. The skin
gives the marks in *[widget-editor]* (*breakpoint-char*, *exec-char* and the
others) and their colors in *[editor]* (*breakpoint*, *execmark*,
*execline*); a terminal that is not UTF-8 shows *o*, *?*, *>* and *@*.

**Functions by name**

**Debug → Breakpoint on function...** lists the functions of the project
whose names have the letters typed, with their files, and puts a breakpoint
on the one chosen, on its line, as Ctrl-B would there. **Debug → Run to
function...** runs the stopped program on to the function chosen, with a
breakpoint GDB takes off when it stops there. The panel names the function
of every breakpoint, *● calc.c:13  twice*.

The functions come from the index of the plugin ctags. Without it Run to
function takes a name typed as it is, and a breakpoint goes on a line.

**The rest of the Debug menu**

**Call stack...** chooses a frame in a list, as Enter in the panel does.
**Evaluate expression...** is Enter of a source window: the value of an
expression, in the dialog that can add it to the watches. **Add watch...**
keeps an expression for the project; a watch shows a value, it does not
stop the program when the value changes. Both offer the selection, or the
word under the cursor, to be taken as it is or changed. **Send line...** gives
a line to a program that reads lines. **Stop** ends the session and keeps the
breakpoints, the configurations and the watches.

**Debug adapters**

The debuggers of most languages speak the Debug Adapter Protocol: debugpy for
Python, Delve for Go, bash-dap for the shell, js-debug for
JavaScript, lldb-dap, and GDB itself from version 14 (*gdb -i dap*). In
**Debug configuration** the choice **Debug adapter**, instead of **GDB**,
has one run the program:

**Adapter**
: The command of the adapter, which speaks on its stdin and stdout, or on a
socket when **Address** is set.

**Address**
: *host:port* of an adapter that listens already; with port 0 the
command is run, and the port is the one it says on its output, *listening
at: 127.0.0.1:38697*.

**Launch (JSON)**
: Members added to the request that launches the program, each adapter
having its own: *{"mode": "debug"}* for Delve, *{"justMyCode": false}* for
debugpy. The form checks it: an error is told with its line and column.

A program by the name of its file has its adapter set when the form is
taken, if the configuration has none:

```
.py              python3 -m debugpy.adapter
.sh, .bash       bash-dap
.go              dlv dap --listen=127.0.0.1:0
.js, .mjs, .ts   js-debug-adapter 0, on 127.0.0.1:0
a program        gdb -i dap
```

A new configuration without a program the build has made takes the file in
front, when it is such a script. An adapter that is not there, or that ends
before the program starts, is told with how to get it; what it says is in
the console.

**Python**

debugpy is had once, with *apt install python3-debugpy* or *pip install --user
debugpy*. Then a script is debugged as a program of C is: F6 on a line, F5,
and Enter in the form, which has the adapter of Python already.

A project with a virtual environment, *.venv*, *venv*, *.env* or *env* with
*bin/python* in it, is debugged with its own Python and the packages of the
project: the adapter becomes *.venv/bin/python -m debugpy.adapter*. debugpy
has to be in that environment too, *.venv/bin/python -m pip install
debugpy*: without it the form says so, and the script runs with the Python
of the system.

A script that reads what is typed, *input()* for one, needs the terminal of
the program: *{"console": "integratedTerminal"}*. Else what it writes comes
in the console, and it reads nothing.

**The shell**

bash-dap debugs a script of bash, with Python 3 alone: *pipx install bash-dap*,
or *pip install --user bash-dap*. Then as for Python: F6 on a line, F5, Enter in
the form. The script runs in the terminal of the program, and reads what is
typed there; it stops in the functions of *$(...)* and of pipes too, and the
variables of a stop are all those it sees, bash having no others. An array
opens as a tree.

The adapter runs in a session of its own, away from the terminal of the
editor, and ends with it. The program has the terminal of the program when
the adapter asks for one (*runInTerminal*: debugpy with *{"console":
"integratedTerminal"}*, bash-dap); else what it writes comes in the
console, and it reads nothing. js-debug runs the program in a session it
starts on its socket: the debugger connects to it there, and the steps are of
that session.

Under *gdb -i dap* GDB has the breakpoints before it runs the program, which
it does as soon as it is launched; the program writes in the console. A
breakpoint the adapter has on no code yet is pending, *◊*, until it says it
is on some.

The configuration keeps these as *backend*, *adapter*, *address* and
*launch_extra* in *debug.ini*.

The debugger runs one local program at a time.
