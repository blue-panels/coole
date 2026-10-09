# Git <!-- help:notitle -->

**Git plugin of the editor**

Shows the changes of the git work tree a file is in and the commits of its
branch, the way gitui does, and has the message of a commit written in a
window of the editor: a new commit, the last one amended, or any commit of
the branch given another message.

The work tree is that of the file in front, else that of the project, else
that of the current directory. The plugin runs the *git* of the system, so
the hooks, the configuration and the identity of the user apply.

**The window Git**

**Alt-Shift-C** opens it on the tab **Status**, **Alt-Shift-L** on the tab
**Log**. The menu **Window** has an entry for each of its windows, **Git:
commit message**, **Git: status and log**, **Git: staged changes** and
**Git: diff**: one opens the window Git when it is not, and gives its window
the keys. It is windows frame to frame where the windows of the files are:

```
 ┌─[COOLE_EDITMSG]────────────────────┐┌─[Diff]────────────────┐
 │ the message of                     ││ the diff of the file  │
 │ the next commit                    ││ or of the commit      │
 └────────────────────────────────────┘│                       │
 ╔═[ 1 Status ] [ 2 Log ] [ 3 Graph ]═╗│                       │
 ║ changes not staged                 ║│                       │
 ╚════════════════════════════════════╝│                       │
 ┌─[Staged changes]───────────────────┐│                       │
 │ staged changes                     ││                       │
 └────────────────────────────────────┘└───────────────────────┘
```

At the left the message of the next commit, a third of the height; under it
the changes not staged, with the tabs and the branch in its title, how far it
is ahead of and behind its upstream with it; the staged changes at the
bottom. At the right the diff of what the cursor is on. The lists and the
diff have scrollbars on their frames. With sticky windows on (**Window ->
Toggle sticky windows**) the windows are resized together; each one can be
made fullscreen. Closing one of them, the button of its frame or **File ->
Close**, takes it off the screen, the others taking its room; its entry in
the menu **Window** brings it back. Closing the last one, or **Esc** and **F10** in any of them, closes
the window Git, the message kept for the next time.

**1**, **2**, **3**
: The tabs Status, Log and Graph; a click on a tab in the title does the same.

**Tab**
: The keys go to the next window: the changes, the staged ones, the diff.

**Right**, **Left**
: To the diff, and back from it. In the diff they scroll it sideways; Up,
Down, PgUp, PgDn, Home, End and Space scroll it.

**F5**, **Ctrl-R**
: Read everything again. The status is read again by itself whenever the
window comes to the front.

**F6**, **F7**, **Shift-F5**
: *git push*, *git pull*, *git fetch*, typed into the shell of the window of
the terminal (Ctrl-o), which comes at the bottom and gets the keys, so that
git can ask for a password there. When git is done everything is read again,
and the keys come back to the window Git; when git says no, they stay with
what it said. Without the plugin terminal git runs on the terminal of the
editor, the editor out of the way till a key.

**F6** asks first: the branch and how far it is ahead of and behind its
upstream, the remote to push to, the one git would push to chosen
(*branch.<name>.pushRemote*, *remote.pushDefault*, the remote of the
upstream, *origin*), and three options. The push names the remote and the
branch: the branch of the upstream on its remote, else one of the same name.
*Force with lease* is *--force-with-lease*: checked when the branch has gone
apart from its upstream after an amend or a reword made here; gone apart
otherwise, others having pushed, it is not, the first line saying so. A push
with no lease is never made. *Set upstream* is *-u*, checked for a branch
with no upstream. *Include tags* is *--follow-tags*: the annotated tags of
the commits pushed. **Enter** pushes, **Esc** gives up; space chooses a
remote.

While a push, a pull or a fetch runs in the terminal another one is not
started; closing the window of the terminal, or its shell ending, ends the
wait.

**F1**
: This help.

**Esc**, **F10**
: Back out of the diff, out of the files of a commit, else close the window
Git; under *coole -G* the editor ends with it.

**The tab Status**

Two lists: the changes not staged, files git does not track among them
(marked *?*), and the changes staged for the next commit. A file whose
merge has a conflict is marked *U*.

**Enter**, **Space**
: Stage the file, or unstage it.

**a**
: Stage all the changes, or unstage all of them.

**F2**, **c**
: To the message of the next commit, at the top left; **F2** there commits
what is staged, and with nothing staged offers to stage everything first.

**F3**, **A**
: Amend the last commit with what is staged, and its message: its message
takes the place of that of the next commit till it is done.

**F4**, **e**
: Open the file in a window of the editor, on the whole screen; closing it,
**Esc** and **F10** there too, comes back to the window Git.

**F8**, **d**, **Delete**
: Throw away the change of the file in the work tree, after a question: a
file git tracks goes back to what the index has, one git does not track, or
one only added with *git add -N*, is deleted.

**The tab Log**

The commits of the branch, the newest first, with the branches and tags at
them; the diff side shows the commit: its author, its dates, its message and
the files it changes. They are read 2000 at a time: the cursor going down
past the last one read, or **End** at it, reads the next ones; till all are
read the title says so, as *Commits (2000+)*.

**Enter**
: The files of the commit; the first line is the commit itself. Enter on a
file takes the keys to its diff, *e* opens the file of the work tree, Left
or Esc goes back to the commits.

**F4**, **r**
: Give the commit another message: it takes the place of the message of the
next commit, the files of the commit under it, till it is done.

**The tab Graph**

The commits of the branches, read as on Log, as *git-graph* draws them: each branch in a column of its own,
down from its last commit to the commit it went off from. The columns go
from the left: *main* or *master*, *develop*, *release*, *hotfix*, then the
other branches, those of the remotes after the local ones, then the branches
merged and gone, named by the subjects of their merges ("Merge branch 'x'").
A column is taken again by a branch further down. A merge is a hollow node,
the line of the branch it took in coming into it with an arrow; a branch
that went off from a commit has its line end at the row of that commit, or
at a row of its own above it when the line of a merge is there. The list is
half the width of the screen; the branches that do not fit are cut, a mark
at the right of the graph, and the button of the frame gives the list the
whole screen.

The branches have colors: *main* blue, *develop* yellow, *release* green,
*hotfix* red, the others in turn, sixteen colors on a terminal of 256, eight
on one of 16. The refs of a commit are in the color of its branch, the tags
in that of the commits. The cursor row is in the color of the selection.

What the graph reads is at the right of the line *Commits*; a click on it
or *v* chooses from a list, kept in the config for the next time:

- *Current*: the commits of HEAD alone, those of Log;
- *Local*: the local branches, the tags and HEAD;
- *Local+remote*, the first time: the branches of the remotes too;
- *All*: every ref, those a fetch of pull requests made (*refs/pr/...*) and
  the stash too, commits of no branch in a line with no name.

Enter opens the files of a commit as on Log, Esc or Left goes back to the
graph. F4 or *r* gives a commit another message only when it is on the
current branch.

The lines are those of the frames of the skin. The section *[git-graph]* of
the skin can give the graph its own, rounded corners for instance, and its
colors:

```
[git-graph]
    lefttop = <the corner at the left top>
    righttop = <the corner at the right top>
    leftbottom = <the corner at the left bottom>
    rightbottom = <the corner at the right bottom>
    commit = *
    merge = o
    rail = (
    others = magenta;cyan;brightmagenta;brightcyan
```

The keys of the lines are those of *[Lines]*: *vert*, *horiz*, *lefttop*,
*righttop*, *leftbottom*, *rightbottom*, *leftmiddle*, *rightmiddle*,
*topmiddle*, *bottommiddle*, *cross*; the others are *commit*, *merge*,
*arrow-left*, *arrow-right* and *more*, the mark of the branches cut.
*rail* puts a commit beside the line of its branch, after that character,
rather than on it; a merge stays on its line. With *rail = (* a branch of
three commits merged reads

```
o<+       Merge branch 'x'
| (*      the third
| (*      the second
| (*      the first
+-+
```

The colors are *main*, *develop*, *release*, *hotfix*, and *others*, separated by
*;*. A character is one; on a terminal of 8 bits one that is not ASCII is not
taken, the frames of the skin being used.

**The message of a commit**

It is written in a window of the editor, the file *COOLE_EDITMSG* in the git
directory of the repository (*COOLE_AMEND_EDITMSG* for an amend,
*COOLE_REWORD_EDITMSG* for a reword). The window Git has the message of the
next commit at all times: it can be written whenever, the files staged
before or after. An amend or a reword takes its place till it is done, and
what was written of the message of the commit comes back after it; it is
kept too when the window Git is closed. The message of an amend or a reword
is there already; under it, after lines starting with *#*, what is being
committed. Those lines are left out.

**Saving** the file (**F2**) makes the commit; the message of the next one
takes its place, empty. With nothing staged the plugin offers to stage
everything first, unless a merge left files with conflicts. An amend or a
reword is not made when HEAD has moved since its message was started, by a
pull or a commit in the terminal: its window is to be closed and the amend
or the reword started again. An empty message commits nothing. When git says no, a
hook *commit-msg* for one, or there is nothing to commit, what it said is
shown and the window stays, to be saved again, the message kept for the next
commit when the window is closed. Closing the window of a message that was
not refused throws it away; **F2** in the lists brings an empty one back.
When the editor ends, the message of the next commit is kept for the next
time; that of an amend or a reword is not.

A commit and an amend are *git commit*, with the hooks. A reword makes the
commit again with the new message and the same files, and the commits after
it again on top of it, as *git rebase* would; the index and the work tree are
not touched, and the hooks do not run. A commit with a merge after it cannot
be reworded here. Commits made again lose their signatures.

**The menu of the message: F11**

In the window of a message, one of those files, or *COMMIT_EDITMSG* when the
editor is the editor of *git commit*, **F11** opens a menu of its own instead
of the menu of the user. Its items are those of *~/.config/coole/git-menu.ini*,
else those of *git-menu.ini* of coole in the directory of its configuration
(*/etc/coole*), those below; **m** in it makes the file of the user from it and
opens it. The
trailers go to the end of the message, before the lines git leaves out,
after an empty line or into the block of trailers there is, once; **Ctrl-U**
takes back what an item did.

**s**
: *Signed-off-by:* with the name and the mail of *git config*.

**c**
: *Co-authored-by:* an author chosen among those of the history.

**f**
: *Fixes:* a commit asked for, as *Fixes: 1e1573e28a1b ("subject")*.

**r**
: The message the commit has now, back in place of what was written: for a
reword and an amend.

**d**
: The diff of the commit reworded, or of what is committed, in a window of
the viewer at the right.

**l**
: Check the shape of the message: the length of the subject, a period at its
end, the empty second line, lines of the body over 72 columns.

**g**
: Generate the message with AI: the command of *ai.ini* writes it from the
diff; it takes the place of the message, the trailers stay. With nothing
staged for a commit it offers to stage everything first, as **F2** does; with
no diff at all the command is not run.

**p**
: Edit the prompt of the AI: *ai.ini* in a window of the editor, at the
prompt of the kind of the message (*prompt-reword* for a reword) or else at
*prompt*, on the whole screen, **Esc** back; that of the project when it has a
prompt, else that of the user, made from the *ai.ini* of coole when there is
none.

**m**
: Edit this menu: *~/.config/coole/git-menu.ini* in a window of the editor,
made from the menu of coole when there is none; **Esc** back to the message.

**git-menu.ini**

A group for each item, named by its key, the items in the order of the
groups:

```
[s]
action = signoff

[x]
label = Reviewed-by: a reviewer
command = echo 'Reviewed-by: A Reviewer <a@example.org>'
output = trailer
```

*label*
: What the menu shows; an action has a label of its own.

*action*
: One of the plugin: *signoff*, *coauthor*, *fixes*, *restore*, *diff*,
*check*, *generate*, *prompt*, *menu*: the items above.

*command*
: Instead of an action, a command run by *sh* in the work tree: the message
on its standard input, *COOLE_GIT_MODE* and *COOLE_GIT_COMMIT* in its
environment as for the command of *ai.ini*. It is taken as it is written: a
backslash in it is one for *sh*.

*output*
: What is done with what the command prints: *trailer*, each line a trailer
at the end of the message; *replace*, the message, the trailers kept;
*insert*, at the cursor, the default; *show*, in a window of the viewer at
the right; *none*.

*modes*
: *commit*, *amend*, *reword*, separated by *;*: the messages the item is in
the menu for; all of them without it.

A repository gives no menu: its commands would run whatever whoever made it
wrote.

**ai.ini**

The command that writes messages is given in the group *[commit-message]*
of *~/.config/coole/ai.ini*:

```
[commit-message]
command = claude -p --tools "" --no-session-persistence
prompt = Write the git commit message for the changes below ...
input = diff
```

*--tools ""* leaves *claude* no tool: it writes the text and does nothing
else. The command may give more than the diff, the last messages of the
repository for their style:

```
command = { cat; echo; echo 'The last messages:'; git log -n 8 --format='%B---' ${COOLE_GIT_COMMIT:+$COOLE_GIT_COMMIT^}; } | claude -p --tools ""
```

The command gets *COOLE_GIT_MODE*, *commit*, *amend* or *reword*, and
*COOLE_GIT_COMMIT*, the commit of an amend or a reword, empty for a commit:
*${COOLE_GIT_COMMIT:+$COOLE_GIT_COMMIT^}* above is the history before that
commit, so that its own message is no example of style.

The values of *ai.ini* are read as GLib reads its key files: a backslash
starts an escape there (*\\n* is a new line), and *%* is no escape at all.

*command*
: Run by *sh* in the work tree. It reads the prompt and the diff on its
standard input and prints the message: *claude -p*, another agent run the
same way, or a script of your own.

*prompt*
: What the command is asked first; without it, the prompt of coole, for a
subject of 50 characters and a body wrapped at 72 columns.

*prompt-reword*, *prompt-amend*, *prompt-commit*
: The prompt for that kind of message, before *prompt*. For a reword the
command gets the commit with its message as it is now; without either of
the two, the prompt of coole for a reword asks for a better message for it.

*input*
: *diff*, the default: the diff after the prompt; *none*: the prompt alone.

A project can have prompts and an input of its own, in the group
*[commit-message]* of *.coole/ai.ini* in its work tree, which go first. It
gives no command: a command written in a repository would run whatever
whoever made it wrote. The prompts of coole are in its *ai.ini*, in the
directory of its configuration (*/etc/coole*), and go last; it gives no
command either.

The editor waits for the command, with a message on the screen that shows
the command. It knows
nothing of keys or tokens: the command logs in as it does on its own, and
gets the environment of the editor. A key is kept in the environment or in a
file the command reads, never in *ai.ini*. The diff goes to whatever service
the command calls.

The files *COMMIT_EDITMSG*, those of coole, *MERGE_MSG* and *TAG_EDITMSG*
are colored as messages of git: the lines git leaves out, the trailers such
as *Signed-off-by:*, and the numbers of issues.

**From a file manager**

The panel of git of mc, or a menu of the user, starts the editor for one
thing and gets it back when it ends:

*coole -G* directory
: The window Git; closing it (**F10**) ends the editor.

*coole -G* directory *--git-log*
: The same, on the log.

*coole -G* directory *--git-show* commit
: The same, on the files and the diffs of that commit.

*coole -G* directory *--git-commit*
: The window Git with the message of a commit of what is staged; the editor
ends once it is committed, or when the window of the message is closed.
*--git-amend* amends the last commit, *--git-reword* commit gives that
commit another message, the files of the commit under it.

Without *-G* directory these are for the current directory. The windows the
project had last time, its tree and the panel of the debugger, are not
shown, and what the editor started so leaves is not kept for the project.
The plugins of a project are not started either: project, build, debugger,
ctags and etags, with their menus and windows; **Options -> Manage
plugins** is not touched.

**The keys**

These are the default keys; they can be changed in **Options -> Key
bindings -> Editor -> Git**, or in the *[git]* section of the keymap.

**Alt-Shift-C**
: The window Git, on the tab Status.

**Alt-Shift-L**
: The window Git, on the tab Log.

*GitCommit*
: Commit what is staged, from any window; no key by default. **Command -> Git
commit...** does the same.
