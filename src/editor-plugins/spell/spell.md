# Spell <!-- help:notitle -->

**Spell checking**

Checks the text of the editor with aspell or hunspell and offers what it
suggests. The library of the engine and the dictionary of the language have
to be installed.

**The keys**

**Ctrl-p**
: Check the word under the cursor. The word is replaced by what is chosen, or
left as it is.

The whole file is checked from the Plugins menu of the editor, which walks the
words one by one and stops at every one the engine does not know.

**The dialog**

The word the engine stumbled on is shown with what it suggests, and the
language and the engine of the check.

**Enter, Replace**
: Take the suggestion under the cursor and go on.

**Skip**
: Leave the word as it is and go on.

**Add word**
: Add the word to the personal dictionary, so that it is never asked about
again.

**Esc, Cancel**
: Leave the word as it is and stop the check.

**Settings**

The settings of the plugin, which the Manage plugins dialog of the Options
menu opens, choose the engine, Aspell or Hunspell, and the language: a name
that the engine knows, en, ru and so on. Select... lists the dictionaries that
are installed; for aspell

```
aspell dump dicts
```

prints them as well. The language
**NONE**
switches the checking off. The settings are kept in the
**[EditorPluginSpell]**
section of
*~/.config/coole/ini*,
as
*engine*
and
*language*.
The keys of the plugin are read from
*spell.keymap*
of the system configuration directory of coole, and then from
*~/.config/coole/spell.keymap*,
which overrides it.
