# Lua API reference

This file is generated from annotations in `src/plugins/lua/mc-lua.c`.
Do not edit it manually; run `python3 maint/generate-lua-api.py`.

## Workspace `editor`

| Lua method | Description | Capability | Mutation |
|---|---|---|---|
| `editor:cursor() -> Position\|nil, error?` | Return the current cursor position. | `editor` | no |
| `editor:edit(spec) -> EditResult\|nil, error?` | Apply replacements atomically as one undo operation. | `editor` | yes |
| `editor:get_text() -> string\|nil, error?` | Read the complete buffer using the compatibility API. | `editor` | no |
| `editor:info() -> DocumentInfo\|nil, error?` | Return document metadata and the current revision. | `editor` | no |
| `editor:insert(text) -> boolean\|nil, error?` | Insert text at the cursor using the compatibility API. | `editor` | yes |
| `editor:is_readonly() -> boolean\|nil, error?` | Report whether the document is read-only. | `editor` | no |
| `editor:overwrite() -> boolean\|nil, error?` | Report whether typing overwrites instead of inserting. | `editor` | no |
| `editor:path() -> string\|nil, error?` | Return the document path. | `editor` | no |
| `editor:replace(range, text) -> EditResult\|nil, error?` | Replace a byte range in the editor buffer. | `editor` | yes |
| `editor:replace_selection(text, options?) -> EditResult\|nil, error?` | Replace the selection, or insert when it is empty. | `editor` | yes |
| `editor:save() -> boolean\|nil, error?` | Save the document with the native editor operation. | `editor` | yes |
| `editor:selected_text() -> string\|nil, error?` | Read selected text using the compatibility API. | `editor` | no |
| `editor:selection() -> Selection\|nil, error?` | Return the current selection snapshot. | `editor` | no |
| `editor:set_cursor(position) -> boolean\|nil, error?` | Move the cursor to a validated position. | `editor` | yes |
| `editor:set_overwrite(flag) -> boolean\|nil, error?` | Switch typing between overwrite and insert. | `editor` | yes |
| `editor:tab_width() -> integer\|nil, error?` | Return the configured tab width. | `editor` | no |
| `editor:text(range?) -> string\|nil, error?` | Read the complete buffer or a byte range. | `editor` | no |
| `mc.editor.current() -> editor\|nil, error?` | Return the editor associated with the active callback. | `editor` | no |
| `mc.macro(spec) -> boolean\|nil, error?` | Register an editor action with optional key and menu placement. | `events` | yes |

## Workspace `any`

| Lua method | Description | Capability | Mutation |
|---|---|---|---|
| `mc.log.debug(message) -> nil` | Write a debug message to the Lua runtime log. | `—` | no |
| `mc.log.error(message) -> nil` | Write an error to the Lua runtime log. | `—` | no |
| `mc.log.info(message) -> nil` | Write an informational message to the Lua runtime log. | `—` | no |
| `mc.log.warn(message) -> nil` | Write a warning to the Lua runtime log. | `—` | no |
| `mc.off(subscription) -> boolean` | Remove an event subscription owned by the package. | `events` | yes |
| `mc.on(event, callback, options?) -> integer\|nil, error?` | Subscribe the package to a named event. | `events` | yes |
| `mc.process.run(spec) -> ProcessResult\|nil, error?` | Run a shell command and capture its bounded output. | `process` | yes |
| `mc.service(name) -> service` | A service a plugin offers, by its name ("viewer").  The object is there whether the service is yet or not: a call says "not_found" while it is not. | `services` | no |
| `mc.settings(handler) -> true\|nil, error?` | Register the dialog this package shows when its settings are asked for in Manage Plugins.  The handler takes no argument and returns nothing; it owns the dialog and whatever it keeps. | `—` | yes |
| `mc.syntax.scan(text, options?) -> table\|nil, error?` | Color text with the syntax rules of the editor.  options.type names the rule set the way the Syntax file does ("C Program"), options.filename picks it by name; without both, the first line of the text decides.  Returns { type = "C Program", colors = { { fg = "yellow", bg = nil, attrs = "bold" } }, runs = { { offset = 1, length = 6, color = 1 } } }, offsets counting bytes from one and color indexing colors. | `syntax` | no |
| `mc.tty.info(section?) -> table\|nil, error?` | What the terminal shows and what the skin paints a section with.  The section is named the way the skin names it ("editor", "dialog"); the default is the core.  Returns { colors = 256, fg = "white", bg = "black" }, colors being 16, 256 or 16777216 for true color, and fg and bg the color names of the skin, nil when it names none. | `tty` | no |
| `mc.ui.dialog(spec) -> DialogResult\|nil, error?` | Show a declarative native modal dialog.  spec.help = {file, node} is what F1 opens over it; a relative file is taken from the script's directory, and a spec without a node has no help. | `ui` | yes |
| `mc.ui.indicator(spec) -> boolean\|nil, error?` | Set or replace a package-owned persistent UI indicator. | `ui` | yes |
| `mc.ui.indicator_clear(id, area?) -> boolean\|nil, error?` | Remove a package-owned UI indicator. | `ui` | yes |
| `mc.ui.message(title, text) -> boolean\|nil, error?` | Show a native informational message box. | `ui` | yes |
| `mc.ui.screen(spec) -> screen\|nil, error?` | Describe a full-screen grid of widgets: a title, a status line on top, keys for the button bar at the bottom, and between them spec.layout, rows of cells; a row is height = n lines or weight = n, a cell is width = n columns or weight = n and holds one control: label, status, text, separator, input, checkbox, or a table with columns and rows(first, count).  help = {file, node}; the callbacks are on_key, on_enter, on_action, on_check, on_row, on_resize, on_close.  palette = "editor" gives the whole screen the editor skin colors; the default is the dialog palette.  A text control is read-only: it scrolls both ways and does not wrap. screen:run() shows it and returns when it is closed. | `ui` | yes |
| `mc.ui.status(text) -> boolean\|nil, error?` | Display transient text in the status line of the editor, in front of the indicators. | `ui` | yes |
| `mc.ui.text_width(text) -> integer\|nil, error?` | Measure UTF-8 text using terminal display columns. | `ui` | no |
| `screen:close() -> boolean\|nil, error?` | Close a running screen; screen:run() then returns. | `ui` | yes |
| `screen:run() -> boolean\|nil, error?` | Show the screen and return when it is closed: by F10 or Esc, by the "close" action, by screen:close(), or by a callback returning { close = true }. | `ui` | yes |
| `screen:status(text) -> boolean\|nil, error?` | Change the status line of a running screen. | `ui` | yes |
| `screen:update(control, patch) -> boolean\|nil, error?` | Change a running screen's control: patch.text for a label, status or text cell, patch.value for an input or checkbox, and for a table patch.rows (a new rows function), patch.row_count, patch.invalidate (drop the rows fetched so far) and patch.row (the current row, from 0). | `ui` | yes |
| `service:call(method, args?) -> table\|nil, error?` | Call a method of the service.  args is a table of strings, numbers, booleans and tables; the answer is a table the same way.  The error is the one the service gives, or "not_found" when there is no such service. | `services` | yes |
| `service:off(id) -> boolean` | Stop listening: id is what service:on() returned. | `services` | yes |
| `service:on(signal, callback) -> integer\|nil, error?` | Call callback(args, signal) when the service tells of signal ("closed"), or of any signal for "*".  It listens whether the service is there yet or not.  The id is what service:off() takes. | `services` | yes |

## Callback contracts

| Callback | Workspace | Capability |
|---|---|---|
| `action(event) -> mc.PASS\|mc.CONSUME` | `editor` | `events` |
| `event(snapshot) -> nil` | `any` | `events` |
| `signal(args, name) -> nil` | `any` | `services` |
