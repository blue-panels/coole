#!/usr/bin/env python3
"""Drive coole in a pty under valgrind and report the errors of each scenario.

    meson devenv -C <builddir> python3 <srcdir>/maint/valgrind_live.py [scenario ...]

Needs the Python module pyte. The reports stay in the directory printed at the end.
"""

import fcntl
import os
import pathlib
import pty
import re
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

import pyte

F2 = b"\x1bOQ"
F7 = b"\x1b[18~"
F9 = b"\x1b[20~"
F10 = b"\x1b[21~"
DOWN = b"\x1bOB"
RIGHT = b"\x1bOC"
HOME = b"\x1bOH"
ESC = b"\x1b"
CTRL_N = b"\x0e"
CTRL_O = b"\x0f"
ENTER = b"\r"
F5 = b"\x1b[15~"
TAB = b"\t"
CTRL_END = b"\x1b[1;5F"
CTRL_SPACE = b"\x00"
CTRL_P = b"\x10"
CTRL_BACKSLASH = b"\x1c"
CTRL_J = b"\n"
SHIFT_TAB = b"\x1b[Z"
ALT_ENTER = b"\x1b\r"
ALT_F8 = b"\x1b[19;3~"
ALT_F10 = b"\x1b[21;3~"
SHIFT_F5 = b"\x1b[15;2~"

MAINT = pathlib.Path(__file__).resolve().parent
WAIT = 90


class Timeout(Exception):
    pass


class Screen(pyte.Screen):
    # pyte passes private=True here for CSI ? r, which set_margins does not take
    def set_margins(self, *args, private=False, **kwargs):
        if not private:
            super().set_margins(*args, **kwargs)


class Session:
    def __init__(self, work, log, args, env=None):
        self.screen = Screen(160, 30)
        self.stream = pyte.ByteStream(self.screen)
        self.finished = False
        self.status = None
        command = [
            "valgrind", "--leak-check=full", "--show-leak-kinds=definite,indirect",
            "--errors-for-leak-kinds=definite,indirect", "--error-exitcode=99",
            "--num-callers=30", f"--suppressions={MAINT / 'valgrind.supp'}",
            f"--log-file={log}",
        ]
        for supp in ("/usr/share/glib-2.0/valgrind/glib.supp", "/usr/lib/valgrind/ncurses.supp"):
            if os.path.exists(supp):
                command.append(f"--suppressions={supp}")
        command += [shutil.which("coole"), *args]

        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(work)
            os.environ.update(HOME=str(work), XDG_CONFIG_HOME=str(work / ".config"),
                              XDG_DATA_HOME=str(work / ".local/share"),
                              XDG_CACHE_HOME=str(work / ".cache"), TERM="xterm",
                              LANG="C.UTF-8", G_DEBUG="gc-friendly",
                              MC_MONGO_URI="mongodb://127.0.0.1:1/test")
            os.environ.update(env or {})
            os.execvp(command[0], command)
        self.resize(30, 160)

    def resize(self, rows, cols):
        self.screen.resize(rows, cols)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end and not self.finished:
            ready, _, _ = select.select([self.fd], [], [], min(0.1, seconds))
            if not ready:
                continue
            try:
                data = os.read(self.fd, 65536)
            except OSError:
                data = b""
            if not data:
                self.finished = True
                break
            self.stream.feed(data)

    def text(self):
        return "\n".join(self.screen.display)

    def wait(self, needle):
        end = time.time() + WAIT
        while needle not in self.text():
            if self.finished or time.time() > end:
                raise Timeout(f"no {needle!r} on the screen")
            self.pump(0.2)

    def send(self, keys, settle=1.5):
        os.write(self.fd, keys)
        self.pump(settle)

    def close(self):
        end = time.time() + WAIT
        while not self.finished and time.time() < end:
            self.pump(0.5)
        if not self.finished:
            os.kill(self.pid, signal.SIGTERM)
            self.pump(5)
            os.waitpid(self.pid, 0)
            raise Timeout("the editor did not exit")
        _, status = os.waitpid(self.pid, 0)
        self.status = os.waitstatus_to_exitcode(status)
        return self.status


def open_quit(s):
    s.wait("hello")
    s.send(F10, 0)


def edit_save(s):
    s.wait("hello")
    s.send(b"xyz")
    s.send(F2)
    s.wait("Confirm save")
    s.send(ENTER, 3)
    s.send(F10, 0)


def menus(s):
    s.wait("hello")
    s.send(F9, 3)
    for _ in range(9):
        s.send(RIGHT)
    s.send(ESC, 2)
    s.send(ESC, 2)
    s.send(F10, 0)


def search(s):
    s.wait("hello")
    s.send(F7, 3)
    s.send(b"world")
    s.send(ENTER, 3)
    s.send(F10, 0)


def resize(s):
    s.wait("hello")
    for rows, cols in ((24, 80), (40, 120), (12, 40), (30, 160)):
        s.resize(rows, cols)
        s.pump(2)
    s.send(F10, 0)


def close_window(s):
    s.send(F9, 3)
    # the menu remembers that it was open
    if "Close" not in s.text():
        s.send(ENTER, 3)
    s.wait("Close")
    s.send(HOME, 1)
    s.send(DOWN, 1)
    s.send(DOWN, 1)
    s.send(ENTER, 3)


def windows(count):
    def run(s):
        s.wait("hello")
        for _ in range(count):
            s.send(CTRL_N, 3)
            close_window(s)
        s.send(F10, 0)

    return run


def terminal(s):
    s.wait("hello")
    s.send(CTRL_O, 8)
    s.send(b"echo vg$((6*7))ok\r", 4)
    s.wait("vg42ok")
    s.send(CTRL_O, 3)
    s.send(F10, 0)


def idle_quit(seconds):
    def run(s):
        s.pump(seconds)
        s.send(F10, 0)

    return run


def debugger(s):
    s.pump(12)
    s.send(F10)
    s.wait("Quit the editor?")
    s.send(ENTER, 0)


def query(s):
    s.pump(12)
    s.send(F10)
    s.wait("Save before close?")
    s.send(b"n", 0)


def selected(s, row, col):
    return s.screen.buffer[row][col].bg == "black"


def menu(s, title, item):
    """Run an entry of the menu bar, whatever the menu remembers from the last time."""
    s.send(F9, 2)
    for _ in range(14):
        col = s.screen.display[0].find(title)
        if col >= 0 and selected(s, 0, col + 1):
            break
        s.send(RIGHT, 1)
    else:
        raise Timeout(f"no menu {title}")
    if item not in s.text():
        s.send(ENTER, 2)
    s.wait(item)
    s.send(HOME, 1)
    for _ in range(40):
        for row in range(1, len(s.screen.display)):
            col = s.screen.display[row].find(item)
            if col >= 0 and selected(s, row, col + 1):
                s.send(ENTER, 3)
                return
        s.send(DOWN, 0.7)
    raise Timeout(f"no entry {item}")


def quit_editor(s):
    s.send(F10, 2)
    end = time.time() + 40
    again = time.time() + 8
    while not s.finished and time.time() < end:
        text = s.text()
        if "Save before close?" in text:
            s.send(b"n", 2)
        elif "Quit the editor?" in text:
            s.send(ENTER, 2)
        elif time.time() > again:
            # F10 closed something that was open over the file
            s.send(F10, 2)
            again = time.time() + 8
        else:
            s.pump(1)


def frames(s):
    return s.text().count("\u250c")


def dialog(s, keys, wait=4):
    """Press the keys; Esc only when they opened something, in a file it quits the editor."""
    before = frames(s)
    s.send(keys, wait)
    if frames(s) > before:
        s.send(ESC, 2.5)


def preview(s):
    s.wait("Title")
    menu(s, "Plugins", "Preview")
    s.pump(8)
    quit_editor(s)


def ctags_plugin(s):
    s.wait("helper")
    s.pump(5)
    before = frames(s)
    s.send(b"\x1bq", 4)
    if frames(s) > before:
        s.send(b"hel", 4)
        s.send(ENTER, 4)
    dialog(s, b"\x1by")
    dialog(s, b"\x1bh")
    dialog(s, b"\x1b=")
    dialog(s, CTRL_BACKSLASH)
    dialog(s, SHIFT_TAB)
    dialog(s, CTRL_J)
    s.send(b"\x1b-", 3)
    menu(s, "Plugins", "Ctags")
    s.pump(2)
    s.send(ESC, 2.5)
    quit_editor(s)


def etags_plugin(s):
    s.wait("helper")
    s.pump(5)
    s.send(b"\x1bl", 3)
    s.send(b"10", 2)
    s.send(ENTER, 3)
    # the end of the name in "    int x = helper (41);"
    for _ in range(30):
        if re.search(r"\] +18 L:", s.text()):
            break
        s.send(RIGHT, 0.4)
    s.send(ALT_ENTER, 5)
    quit_editor(s)


def spell_plugin(s):
    s.wait("helo")
    dialog(s, CTRL_P, 6)
    menu(s, "Plugins", "Spell")
    s.pump(5)
    s.send(ESC, 2.5)
    quit_editor(s)


def project_plugin(s):
    s.wait("hello")
    s.pump(4)
    s.send(b"\x1bP", 4)
    s.send(b"b.c", 3)
    s.send(ENTER, 4)
    dialog(s, b"\x05", 4)
    s.send(b"\x1bT", 5)
    s.send(b"\x1bT", 4)
    quit_editor(s)


def lsp_work(s):
    s.wait("helper")
    s.pump(25)
    s.send(CTRL_END, 2)
    s.send(b"int y = hel", 4)
    s.send(CTRL_SPACE, 8)
    s.send(ESC, 2.5)
    dialog(s, b"\x1b,", 6)
    dialog(s, b"\x1b(", 6)
    for item in ("Problems", "Status", "Log"):
        menu(s, "LSP", item)
        s.pump(3)
        s.send(ESC, 2.5)
    menu(s, "LSP", "Restart server")
    s.pump(20)
    quit_editor(s)


def debugger_run(s):
    s.wait("helper")
    s.pump(8)
    s.send(b"\x1bl", 3)
    s.send(b"12", 2)
    s.send(ENTER, 3)
    menu(s, "Debug", "Toggle breakpoint")
    menu(s, "Debug", "Start or continue")
    s.wait("x = 42")
    s.pump(4)
    s.send(ALT_F10, 3)
    s.send(b"\x1b[19~", 6)
    s.send(b"\x1b[18~", 6)
    s.send(ALT_F8, 3)
    s.send(b"x", 2)
    s.send(ENTER, 4)
    s.send(ESC, 2.5)
    s.send(ALT_F10, 3)
    s.send(SHIFT_F5, 8)
    quit_editor(s)


def git_work(s):
    s.pump(12)
    s.send(b" ", 4)
    s.send(b"2", 4)
    s.send(b"3", 4)
    s.send(b"1", 4)
    s.send(TAB, 3)
    s.send(TAB, 3)
    s.send(TAB, 3)
    s.send(F5, 5)
    s.send(b"a", 4)
    s.send(b"a", 4)
    quit_editor(s)


def query_run(s):
    s.pump(15)
    s.send(F5, 15)
    s.send(F5, 10)
    quit_editor(s)


def prepare(work, name):
    (work / "a.txt").write_text("hello\nworld\n")
    (work / "b.c").write_text("#include <stdio.h>\n\nstatic int helper (int value)\n{\n"
                              "    return value + 1;\n}\n\nint main (void)\n{\n"
                              "    int x = helper (41);\n\n    printf (\"%d\\n\", x);\n"
                              "    return 0;\n}\n")
    (work / "e.txt").write_text("helo wrld\n")
    (work / "q.sql").write_text("select 1 as a, 'x' as b;\nselect * from no_such_table;\n")
    (work / "c.md").write_text("# Title\n\nhello\n")
    (work / "d.js").write_text("db.orders.find({})\n")
    repo = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
            "GIT_COMMITTER_EMAIL": "t@t", "HOME": str(work), "PATH": os.environ["PATH"]}
    for args in (["init", "-q", "-b", "main"], ["add", "a.txt"], ["commit", "-q", "-m", "first"]):
        subprocess.run(["git", *args], cwd=work, env=repo, check=True)
    (work / "a.txt").write_text("hello\nworld\nchanged\n")
    if name in ("ctags", "etags") and shutil.which(name):
        subprocess.run([name, "b.c"], cwd=work, check=False)
    if name == "debugger_run" and shutil.which("cc"):
        subprocess.run(["cc", "-g", "-O0", "-o", "prog", "b.c"], cwd=work, check=False)
        (work / ".coole").mkdir(exist_ok=True)
        (work / ".coole/debug.ini").write_text(
            "[Debug]\nlaunch_count=1\nactive_launch=0\n\n[Launch 0]\nname=prog\n"
            f"executable={work}/prog\narguments=\ndirectory={work}\nenvironment=\ngdb_path=gdb\n")


SCENARIOS = {
    "open_quit": (open_quit, ["a.txt"], None),
    "two_files": (idle_quit(12), ["a.txt", "c.md"], None),
    "edit_save": (edit_save, ["a.txt"], None),
    "menus": (menus, ["a.txt"], None),
    "search": (search, ["a.txt"], None),
    "resize": (resize, ["a.txt"], None),
    "windows_1": (windows(1), ["a.txt"], None),
    "windows_20": (windows(20), ["a.txt"], None),
    "terminal": (terminal, ["a.txt"], None),
    "git": (idle_quit(15), ["-G", "."], "git"),
    "lsp": (idle_quit(25), ["b.c"], "clangd"),
    "debugger": (debugger, ["-D", "."], "gdb"),
    "query_mongo": (query, ["-Q", "d.js"], "mongosh"),
    "preview": (preview, ["c.md"], None),
    "ctags": (ctags_plugin, ["b.c"], "ctags"),
    "etags": (etags_plugin, ["b.c"], "etags"),
    "spell": (spell_plugin, ["e.txt"], "aspell"),
    "project": (project_plugin, ["a.txt"], None),
    "lsp_work": (lsp_work, ["b.c"], "clangd"),
    "debugger_run": (debugger_run, ["-D", ".", "b.c"], "gdb"),
    "git_work": (git_work, ["-G", "."], "git"),
    "query_postgres": (query_run, ["-Q", "q.sql"], "env:PGHOST"),
    "query_mongo_server": (query_run, ["-Q", "d.js"], "env:VALGRIND_MONGO_URI"),
}

ENV = {
    "query_postgres": {"MC_QUERY_KIND": "postgres"},
    "query_mongo_server": {"MC_QUERY_KIND": "mongo",
                           "MC_MONGO_URI": os.environ.get("VALGRIND_MONGO_URI", "")},
}


def available(needs):
    if needs is None:
        return True
    if needs.startswith("env:"):
        return needs[4:] in os.environ
    return shutil.which(needs) is not None


def report(log):
    text = log.read_text(errors="replace") if log.exists() else ""
    errors = re.search(r"ERROR SUMMARY: ([\d,]+) errors", text)
    used = re.search(r"in use at exit: ([\d,]+) bytes in ([\d,]+) blocks", text)
    allocs = re.search(r"total heap usage: ([\d,]+) allocs", text)
    return (errors.group(1) if errors else "?", used.group(1) if used else "?",
            used.group(2) if used else "?", allocs.group(1) if allocs else "?")


def main():
    names = sys.argv[1:] or list(SCENARIOS)
    logs = pathlib.Path(tempfile.mkdtemp(prefix="coole-valgrind-"))
    failed = 0
    print(f"{'scenario':14} {'result':26} {'errors':>6} {'in use':>10} {'blocks':>7} {'allocs':>9}")
    for name in names:
        action, args, needs = SCENARIOS[name]
        log = logs / f"{name}.log"
        if not available(needs):
            print(f"{name:14} skipped: no {needs}")
            continue
        work = pathlib.Path(tempfile.mkdtemp(prefix="coole-work-"))
        prepare(work, name)
        session = Session(work, log, args, ENV.get(name))
        try:
            action(session)
            result = f"exit {session.close()}"
        except Timeout as error:
            if not session.finished:
                os.kill(session.pid, signal.SIGKILL)
            result = str(error)[:26]
            failed += 1
        else:
            failed += session.status != 0
        (logs / f"{name}.screen").write_text(session.text())
        shutil.rmtree(work, ignore_errors=True)
        errors, used, blocks, allocs = report(log)
        print(f"{name:14} {result:26} {errors:>6} {used:>10} {blocks:>7} {allocs:>9}", flush=True)
    print(f"reports: {logs}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
