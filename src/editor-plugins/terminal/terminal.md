# Terminal <!-- help:notitle -->

**Terminal**

Runs your shell in a window of the editor. The shell is the one named by the
**SHELL**
variable, and it starts in the directory of the file being edited. It works
with bash, zsh, fish and tcsh, and with the other shells that print a prompt.

**Ctrl-o**
: Show the window of the terminal, at the bottom of the screen the first time
and after that as it was when it was hidden. A file shown full screen becomes
a window above the terminal. Ctrl-o in the terminal hides the window again,
and the file takes the whole screen back, unless it has been moved or resized
in between. The shell goes on running while the window is hidden: what it
prints then is there when the window is shown. With the window on the screen
under another window, Ctrl-o brings it up.

The window is a window of the editor like the windows of the files: it is
moved, resized, shown full screen and closed with its frame or from the
Window menu, and Window > List shows it. Its title is the directory the shell
is in and the command it runs.

Closing the window ends the shell. When a command is running in it, the
editor asks first, and it asks the same when the editor ends. After the shell
has ended, with
**exit**
for one, Ctrl-o starts a new one in the window.

**The keys**

Every key not named below is typed into the shell, the function keys and Esc
among them, and the cursor keys, Home and End, which the line editor of the
shell and its history take. A full-screen program, an editor or a pager,
takes every key itself, Ctrl-o too. The menu and the button bar are there for
the mouse.

The terminal keeps a scrollback of everything the shell has printed. The
shifted cursor keys mark it, from where the shell is typing.

```
Ctrl-Insert    copy the marked output to the clipboard
Ctrl-Shift-u   take the mark back
Alt-s          search the output for what is typed next
Alt-Shift-s    show only the rows that match what is typed next
Ctrl-l         clear the screen, keeping the scrollback
Ctrl-Shift-l   clear the screen and the scrollback both
               (also Ctrl-Alt-l)
PgUp, PgDn     scroll the output
Ctrl-Home      the oldest row of the output
Ctrl-End       the newest row
F2             copy the marked output to the clipboard
F3             mark the whole output, or take back the mark
F4             show only the rows that match the mark,
               or the word under the cursor
F5             turn that filter off, and on again
F6             clear the screen and the scrollback both
```

Alt-s and Alt-Shift-s take a pattern typed on the top row of the window, and
the output follows it as it grows. Case does not matter. Enter ends the typing
and leaves the view on what was found; Esc ends it and puts the view back.

All these keys are in the
**[mcterm]**
section of the keymap file and can be changed there, or in the Key bindings
dialog of the Options menu.

**Settings**

The
**[Terminal]**
section of the ini file holds the settings of the terminal.

*window_height*
: The height of the window when it is shown the first time, in percent of the
screen. Default is 25.

*search_direction*
: The way Alt-s and Alt-Shift-s run through the output: down (the default),
or up, as in
**less**.
