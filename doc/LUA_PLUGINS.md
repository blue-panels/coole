# Lua runtime and scripts

coole can load optional Lua 5.3+ support.  Lua is not linked into the
`coole` executable: `mc-lua.so` is a runtime extension loaded through
`dlopen` only when it is available and enabled.  That extension discovers and
runs Lua scripts, which extend the editor: actions bound to keys and menus,
reactions to editor events, dialogs and full-screen views.

The extension is looked up in `${libdir}/coole/runtime-plugins/` and, before
that, in `~/.local/lib/coole/runtime-plugins/`.

Build it with:

```sh
meson setup build -Dlua=enabled
meson compile -C build
```

A Lua installed in an unusual place is found through pkg-config: set
`PKG_CONFIG_PATH` to its `lib/pkgconfig`.

`-Dlua=auto` is the default.  In that mode the runtime is not built when a
compatible Lua development package is unavailable.

## Enabling and disabling

Lua is enabled by default in a Lua-enabled build.  Put this in
`~/.config/coole/ini` to disable it persistently:

```ini
[Lua]
enabled=false
user_scripts_dir=
```

`user_scripts_dir` may override the user script directory, but must be an
absolute path.  `coole --no-lua` and `COOLE_NO_LUA=1 coole` prevent Lua code from
being loaded for one run.

**Manage Plugins** shows one native runtime, `runtime / lua / Lua engine`.
Clearing it disables all Lua code on the next start.  Enter or F4 on that row
opens the global and user scripts.  In the script
list, F4 opens the selected package's declared `entry` file in the internal
editor; **Run** lists and invokes only actions registered by the selected
package.  Event-only packages have no runnable action.  Individual script
choices are stored as `lua/<id>=true` in
`[DisabledPlugins]` of `~/.config/coole/plugins.ini`.  No Lua script is hot-reloaded
during a session.

## Script layout

System scripts live in `${datadir}/coole/lua/scripts/`; user scripts live in
`${XDG_DATA_HOME:-~/.local/share}/coole/lua/scripts/`.  Every script belongs to
the `editor` workspace and lives in the `editor/` directory immediately below
`scripts/`:

```text
scripts/editor/base64-decode/lua.ini
scripts/editor/base64-decode/init.lua
scripts/editor/base64-decode/lib/format.lua       # optional
```

The workspace is not a `lua.ini` property.  A top-level directory such as
`scripts/my-script/`, or one in any other directory below `scripts/`, is not
discovered; place it under `scripts/editor/` instead.

The script manifest is named `lua.ini`.  It must contain:

```ini
[Lua]
id=my-script
api_version=1
name=My script
entry=init.lua
provides=events
```

The ID is limited to 64 characters from `A-Z`, `a-z`, `0-9`, `_`, `.` and
`-`, and must match its directory name.
Scripts load in lexical ID order.  A user script with the same ID replaces the
whole system script.

`provides` describes the entry points declared by the script.  It is an
optional comma-separated list for compatibility with earlier scripts:
`events`, `macros`, or both.  The **Lua scripts** list displays this value, so
the user can distinguish a script that reacts to events from one that exposes
an action.  A script using `mc.macro()` must declare `provides=macros`.

Each script has its own Lua state.  `require("a.b")` searches the script's
`lib/a/b.lua` and then the shared library directory from the same origin.  C
modules are disabled for these lookups.

The shared directories are `${datadir}/coole/lua/lib/` for system scripts and
`${XDG_DATA_HOME:-~/.local/share}/coole/lua/lib/` for user scripts.  A system script
never searches the user shared directory.

## API

The whole API lives in the global table `mc`; the name is kept from the
program coole grew out of, and scripts written for it keep working.  The
generated reference of every function is `doc/LUA_API_REFERENCE.md`
(`doc/lua-api.json` for tools).

### Macros

A Lua macro is an action registered while its script is loaded, but executed
only when its key is pressed in the declared area.  This is distinct from an
event handler: loading the script registers the macro; it does not run its
action.

The supported area is `editor`.  The script must declare `provides=macros`:

```lua
mc.macro {
    id = "decode-base64",
    area = "editor",
    key = "F11",
    description = "Decode Base64 selection",
    priority = 50, -- optional; 0 through 100
    menu = {       -- optional direct menu entry
        path = "Tools",
        label = "Decode Base64",
        position = 100,
    },
    action = function (ev)
        -- ev is an editor.key-style event snapshot
        return mc.CONSUME
    end,
}
```

Macro IDs are unique inside their script.  Key names are case-insensitive and
use the same spelling as `ev.key.name`, for example `F11` or `Ctrl-S`.
The matching macro with the greatest priority runs; equal priorities retain
script load and registration order.  A macro consumes its key by default.
Return `false` or `mc.PASS` to allow normal editor processing to continue.
After three action errors, only that macro is disabled for the session.

`menu` optionally places the same action directly in an editor menu.  `path`
is the stable, untranslated top-level menu name.  Existing names such as
`Command` append to that menu; any other name creates a top-level menu.  The
optional `label` defaults to `description`.  `position` ranges from -100000 to
100000 and orders runtime-provided entries within the target menu; lower
values appear first.  Entries with the same position retain script load and
registration order.  Menu placement is independent of `listed`, which only
controls the **Run action** list.

### Processes

`mc.process.run()` synchronously runs a shell command through the core runtime
host and captures its output:

```lua
local result, err = mc.process.run {
    command = "git status --short",
    max_output = 8 * 1024 * 1024, -- optional, 1 byte through 64 MiB
}
```

On success, `result` contains binary-safe `stdout` and `stderr` strings,
`exit_code` (or `nil` if terminated by a signal), `signal`, and the booleans
`stdout_truncated` and `stderr_truncated`.  A non-zero command exit status is a
successful process invocation and must be handled by the script.  The call is
allowed only during a user-initiated callback and blocks the UI until the
command exits.  Commands are intentionally interpreted by `/bin/sh`; scripts
must not concatenate untrusted text into `command`.

### Event handlers

Register callbacks with `mc.on()` and remove them with `mc.off()`:

```lua
local token = mc.on("editor.save", function (ev)
    mc.ui.status("Saved " .. ev.path)
end, { priority = 10 })

mc.on("shutdown", function ()
    mc.off(token)
end)
```

Priorities range from `-100` to `100`; higher callbacks run first and equal
priorities keep registration order.  `mc.off()` is idempotent.  For
`editor.key`, returning `true` or `mc.CONSUME` stops normal editor processing;
all other event callbacks are notifications.  Prefer `mc.macro()` for a
user-visible key action; `editor.key` remains the low-level notification and
interception event.

Available event names are:

- `startup`, `shutdown`
- `editor.open`, `editor.save`, `editor.key`
- `editor.change`, `editor.cursor`

`editor.change` tells that the text changed and `editor.cursor` that the
cursor is on another line.  Both come once the editor is idle, after the keys
that came together have been handled: a paste or a fast typist gives one of
each, not one for every key.  A file window that comes to the front is told of
by both, so a script that follows the current file needs no other event.

`mc.on()` returns `nil` and an "unknown event" message for any other name.

Every callback receives a fresh, copied snapshot.  The event-specific fields
are:

| Event | Fields in `ev` |
| --- | --- |
| `startup` | `config_dir`, `data_dir` |
| `shutdown` | `reason` (`normal` or `quit`) |
| `editor.open` | `editor`, `path`, `readonly`, `line`, `column` |
| `editor.save` | `editor`, `path`, `previous_path`, `save_as` |
| `editor.key` | `editor`, `key` (`name`, `code`, optional `text`, `modifiers`) |
| `editor.change` | `editor`, `path`, `revision` (grows with every change) |
| `editor.cursor` | `editor`, `path`, `line`, `column` (from 1) |

### Objects and commands

An editor reference is opaque userdata, never a C pointer.  It is valid only
while its editor window is alive; a later call returns `nil, "closed"` if it
has gone away.

| Object | Creation and methods |
| --- | --- |
| Editor | `mc.editor.current()`, `ev.editor`; `:info()`, `:selection()`, `:text([range])`, `:replace(range, text)`, `:replace_selection(text)`, `:edit(spec)`, `:tab_width()`, `:overwrite()`, `:set_overwrite(flag)`, plus the legacy cursor, text, insert, path, readonly, and save methods |

`cursor()` and `set_cursor()` use one-based line and column numbers.
Their columns are editor display columns and therefore expand tabs using the
current `editor:tab_width()`.  The tab width query returns the configured
positive tab-stop width; scripts that align text must use it rather than
assuming eight columns.  When the editor permits the cursor beyond the end of
a line, `cursor()` includes that virtual-space distance in the returned
column; inserting at that position requires materializing the gap as spaces.
`get_text(from, to)` uses one-based inclusive byte positions.

`selected_text()` returns the current ordinary text selection, or
`nil, "no_selection"`.  Column selections return
`nil, "column_selection_not_supported"` rather than silently decoding a
different range.

New buffer operations use zero-based, half-open byte ranges.  A stored range
should carry the revision that produced it:

```lua
local info = assert(editor:info())
local bytes = assert(editor:text {
    from = 0, to = info.byte_length, revision = info.revision,
})
assert(editor:replace({ from = 0, to = 3, revision = info.revision }, "new"))
```

`editor:edit { revision = n, changes = {...}, cursor = { offset = n } }`
validates every range before changing the buffer, applies non-overlapping
changes atomically, and creates one undo entry.  A changed document returns
`nil, "stale_revision"`.  `mc.ui.text_width(text)` returns the terminal display
width for valid UTF-8 text and is intended for alignment and drawing scripts.

Object and UI methods require an active callback: an event handler, a macro
action or a settings handler.  Outside one they return
`nil, "no active runtime context"`; a service the host does not offer returns
`nil, "not_ready"`.

`mc.ui.status(text)` shows the text on the status line of the editor, in front
of the indicators, until the next status text replaces it; an empty text
clears it.  `mc.ui.message(title, text)` displays a modal message.  Both
return `nil, "not_ready"` before the editor is up.

`mc.ui.indicator { id, area = "editor", text, priority = 0 }` installs or
updates a persistent status-line indicator and `mc.ui.indicator_clear(id)`
removes it.  IDs are scoped to the owning package, so scripts cannot replace
one another's indicators.  Higher-priority indicators are placed first and
lower-priority ones are omitted when the status line is too narrow.  All
indicators owned by a package are removed automatically when it is unloaded.
The only area is `editor`.

```lua
mc.ui.indicator {
    id = "mode", area = "editor", text = "[╔═╗]", priority = 100,
}
-- later:
mc.ui.indicator_clear("mode")
```

### Screens

`mc.ui.screen(spec)` describes a full-screen grid of widgets that a script fills and drives.
The status line is at the top and the keys make the button bar at the bottom; between them
`layout`: rows of cells, one control per cell.  A row is `height = n` lines or `weight = n`,
a share of the lines left; a cell is `width = n` columns or `weight = n`.  `screen:run()`
shows it and returns when it is closed (F10 or Esc, the `close` action, `screen:close()`, or a
callback returning `{ close = true }`); it needs an active callback, a macro action for
instance.  Dialogs opened from a callback nest over the screen.  `palette = "editor"` paints
the whole screen in the colors of the editor; the default is the palette of dialogs.

```lua
local screen = mc.ui.screen {
    title  = "sample.dbf",
    status = "1234 records",
    help   = { file = "help.hlp", node = "[Records]" },
    layout = {
        { height = 1, { type = "label", id = "hint", text = "Enter: the card" } },
        { weight = 3,
          { weight = 1, id = "grid", type = "table",
            columns = {
                { id = "name", title = "NAME", align = "left", min_width = 4, expands = true },
                { id = "qty",  title = "QTY",  align = "right", min_width = 3 },
                { id = "sel",  title = "",     type = "check" },
            },
            row_count = 1234,     -- nil: unknown, rows are asked for until a short page
            page_size = 256,      -- rows asked for at a time
            rows = function(first, count)     -- first from 0; a cell is a string, a number,
                return read_page(first, count)    -- or { text = ..., color = "red;black" }
            end } },
        { weight = 1, { weight = 1, id = "card", type = "text", text = "" } },
    },
    keys = {
        { key = "f2",  label = "Struct", action = "structure" },
        { key = "f10", label = "Quit",   action = "close" },   -- built in, like "help"
    },
    on_row    = function(scr, ev) scr:update("card", { text = card_of(ev.row) }) end,
    on_enter  = function(scr, ev) ... end,
    on_action = function(scr, id, ev) ... end,
    on_key    = function(scr, ev) return ev.key.name == "x" end,  -- true: the key is taken
    on_check  = function(scr, ev) marks[ev.row] = ev.value end,
    on_resize = function(scr, columns, lines) end,
    on_close  = function(scr) end,
}
screen:run()
```

Cell types are `label` and `status` (`text`), `text` (a read-only text area: it scrolls up,
down and sideways, long lines are not wrapped, Shift with a motion marks and Ctrl-Insert copies;
every other key goes to `on_key`),
`separator`, `input` (`value`), `checkbox` (`label`, `value`) and `table`; a screen may hold
any number of tables, each with its own `rows()`.  A column is `text` or `check`; check cells
read `1`, `x`, `true` or `yes` as checked.  A table keeps the pages it fetched recently and
asks `rows()` again for the others; Left and Right scroll its columns when they do not fit,
Tab moves between the cells.  Every callback gets `ev.control` (the cell the user is on) and,
for a table, `ev.row` (from 0), `ev.column` (the column id) and `ev.column_index`; `on_key`
also gets `ev.key.name` and `ev.key.code`.  The keys of `keys` are taken before any widget
sees them; only F1 through F10 have a button.

While the screen runs, `screen:status(text)` changes the status line and
`screen:update(control, patch)` a cell: `patch.text` for a label, status or text cell,
`patch.value` for an input or checkbox, and for a table `patch.rows` (a new rows function),
`patch.row_count`, `patch.invalidate` (drop the rows fetched so far) and `patch.row` (the
current row).

`mc.ui.dialog(spec)` and `mc.ui.screen(spec)` accept
`help = { file = "help.hlp", node = "[Node]" }`: F1 opens that node.  A relative `file` is
taken from the script's directory, so a script ships its help next to its `init.lua` in the
help format of coole (`[Node]` headings); without `file` the node is looked up in the help of
coole itself.

An `input` control in `mc.ui.dialog()` accepts an optional `history` name and
an optional `completion` array.  `complete_on_tab = true` makes `Tab` invoke
completion while that input has focus; `Shift-Tab` still moves to the previous
control.  Completion providers are `files`, `hosts`, `commands`, `variables`,
`users`, `cd`, and `shell`; they use the input completion engine of coole and
may be combined.  For example:

```lua
{
    id = "command", type = "input", value = "",
    history = "my-command-history",
    complete_on_tab = true,
    completion = { "commands", "files", "variables", "shell" },
}
```

`mc.log.debug/info/warn/error(text)` writes a message tagged with the Lua
script ID.

The teaching example `notify-editor-save` is installed under
`PREFIX/share/coole/lua/examples/editor/`; coole never loads scripts from
there.  Copy an example into `~/.local/share/coole/lua/scripts/editor/` to try
it, and copy an installed script before adapting it, so system updates do not
overwrite local changes.

### Services

A plugin of the editor can offer a service under a name, and a script
calls it: the viewer plugin, for one, offers `viewer`, windows that show
the text a script gives them.

```lua
local viewer = mc.service("viewer")

local function show(text)
    local r, err = viewer:call("open", { title = "Notes", text = text, place = "right" })
    if r == nil then
        mc.ui.message("Notes", err)   -- "not_found" without the plugin
        return
    end
    viewer:on("closed", function(args) if args.id == r.id then --[[ gone ]] end end)
end
```

`mc.service(name)` gives the object whether the service is there yet or
not: the scripts load before the editor opens its plugins, and a call
says `not_found` while the service is missing.  `service:call(method,
args)` takes a table of strings, numbers, booleans and tables and gives
the answer as a table; a string that is not UTF-8 goes as the bytes it
is.  `service:on(signal, fn)` calls `fn(args, signal)` when the service
tells of something, `"*"` for any signal; `service:off(id)` stops it.
The methods a service has are listed where it is described; those of the
viewer are in `src/editor-plugins/viewer/viewer.c` and in `doc/PLUGINS`.

## Trust boundary

Lua scripts run with the permissions of the current coole process.  Install
only scripts you trust.  coole rejects a script or loaded Lua module if its directory
tree is symbolic-linked, owned by neither the current user nor root, or is
group/world writable.  A Lua callback error is isolated to that callback;
after three errors in one session the callback is disabled.
