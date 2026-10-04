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
   `B` appears in the gutter. The breakpoint belongs to the project, so it
   remains after that file window closes. On **Start**, GDB opens the source
   file where the program stops and `>` marks the current execution line.
4. **Start** opens a **Debug session** window. While this window has focus, its
   function keys control the running program: F5 starts or continues, F6 pauses,
   F7 steps into, F8 steps over, and F9 steps out. Shift-F5 stops the program;
   F10 closes only the Debug session window. Reopen it with **Debug → Debug
   session...**. The source editor keeps its normal function keys when it has
   focus. After a step, coole shows the source location while keeping focus in
   Debug session, so repeated F8 presses keep stepping.
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
