# Definitions <!-- help:notitle --><a id="definitions"></a>

**The list of definitions**

What the tag lookup of the editor found for the name under the cursor: one
line for every definition, with the file it is in, the line number and the
text of that line.

**Enter**
: Open the definition under the cursor. The editor opens the file and puts the
cursor on the line.

**Esc**
: Leave the editor where it is.

The list appears only when a name has more than one definition; with a single
one the editor jumps to it at once. Where the name has none, the editor says
so. The lookup is started by
**Alt-Enter**,
with the cursor at the end of the name;
**Alt-Minus**
and
**Alt-Plus**
go back and forward in the list of the places it went to.

The definitions are read from the
*TAGS*
file of the project, in the format of Emacs, which etags or ctags builds. It
is looked for in the current directory and in the directories above it:

```
etags -R .
ctags -e -R .
```
