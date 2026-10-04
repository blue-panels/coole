# Debugger

The **Debug** menu runs a local executable under GDB. Build the program with
debug symbols first, for example:

```sh
cc -g -O0 -Iinclude src/main.c src/calc.c -o build/calculator
```

1. Open any source file in coole, then choose **File → Open debug project** and
   enter the root directory containing the sources. The root is explicit: the
   first file's parent directory is only the suggested value in the dialog.
2. Choose **Debug → New configuration**. Give it a name and specify the
   executable, program arguments, working directory, optional environment
   entries and GDB executable. An executable path relative to the project root
   is converted to an absolute path. Arguments and environment entries use
   shell-style quoting only to group words; a shell does not run them. For
   example, an environment entry with spaces is `GREETING="hello world"`.
   Use **Select configuration** to switch between executable targets.
3. Open any source file, move to a line, and choose **Toggle breakpoint**. A
   `●` appears in the gutter. The breakpoint belongs to the project, so it
   remains after that file window closes, and it moves with its line when
   lines are inserted or deleted above it. On **Start**, GDB opens the source
   file where the program stops and `▶` marks the line it stopped on.

   | Mark | Meaning                                              |
   |------|------------------------------------------------------|
   | `●`  | a breakpoint                                         |
   | `◌`  | a breakpoint GDB has not taken yet, or has refused   |
   | `▶`  | the line the program stopped on                      |
   | `◉`  | the line the program stopped on, with a breakpoint   |

   GDB stops only on lines with code: a breakpoint on another line moves to
   the next line GDB stops on. The skin sets the marks and their colors
   (`breakpoint-char`, `exec-char` and the others in `[widget-editor]`,
   `breakpoint`, `execmark` and `execline` in `[editor]`); a terminal that is
   not UTF-8 shows `o`, `?`, `>` and `@`.

4. **Start** opens a **Debug session** window and runs the program to the
   first breakpoint. While the program is stopped, the source windows are in
   **step mode**: they take the debugger keys, and the keys that move around
   the text, search, select or switch windows work as usual.

   | Key      | In step mode                                  |
   |----------|-----------------------------------------------|
   | F5       | continue                                      |
   | F6       | pause the running program                     |
   | F7       | step into                                     |
   | F8       | step over                                     |
   | F9       | step out                                      |
   | F4       | run to the line of the cursor                 |
   | F2       | toggle a breakpoint on the line of the cursor |
   | Enter    | evaluate the selection or the word under the cursor |
   | Shift-F5 | stop the program                              |
   | Esc      | go back to editing until the next stop        |

   A key that would change the text only beeps. The value of an evaluated
   expression comes in a dialog that can add it to the Watches.

   The Debug session window takes the same keys when it has the focus, where
   F2 and F4 work on the cursor of the topmost file window and F10 closes the
   window. Reopen it with **Debug → Debug session...**.
   These bindings can be changed in **Options → Key bindings → Editor →
   Debugger**, independently of the source editor. The same actions can be
   set in the `[debugger]` section of the user keymap, for example
   `DebugStepOver = alt-o`. The bar labels reflect bindings to F1–F10;
   `f15` is the keymap name for Shift-F5.
5. Use **Call stack** to choose a frame. Its variables appear in **Local
   variables**.
   **Add watch** saves an expression for the project; **Watches** shows its
   value in the selected frame. An expression outside that frame's scope shows
   GDB's error message. A Watch displays a value; it does not stop execution
   when memory changes.
6. Program output appears in **Debug output**. **Send line** provides input to
   line-oriented programs. **Stop** ends the session without removing the
   project's breakpoints, configurations or Watches.

coole asks to save modified project files before starting GDB. Rebuild the
executable after source changes; this version does not build it for you. The
debugger handles one local program at a time. Full-screen terminal programs
still need a terminal emulator and are outside this version's input window.
