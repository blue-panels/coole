-- Markdown preview: the markdown file of the editor, rendered the way it is
-- meant to read, in a window of the viewer plugin beside it.  The window is
-- rendered again when the text changes and follows the cursor.
--
-- The rendering is lua-markdown of mc: lib/ is taken from mc as it is, so
-- that a fix there comes over by a diff.  What coole needs of it otherwise
-- is done here.

local cfg = require("config")
local md = require("document")
local settings = require("settings")

-- The settings are kept with the configuration of coole, not of mc.
settings.path = function()
    local xdg = os.getenv("XDG_CONFIG_HOME")
    local dir

    if xdg ~= nil and xdg ~= "" then
        dir = xdg .. "/coole"
    else
        local home = os.getenv("HOME")
        if home == nil or home == "" then
            return nil
        end
        dir = home .. "/.config/coole"
    end
    return dir .. "/markdown-preview.ini"
end

-- The shade of a code block is taken from the colors of the viewer of mc.
-- The preview is drawn in the colors of the editor, beside a file.
if mc.tty ~= nil and mc.tty.info ~= nil then
    local tty_info = mc.tty.info
    mc.tty.info = function(section)
        if section == "Viewer" then
            section = "editor"
        end
        return tty_info(section)
    end
end

settings.load()

local viewer = mc.service("viewer")

local TITLE = "Markdown preview"

local preview = {
    id = nil,        -- the window of the viewer, nil while there is none
    shown = false,
    path = nil,      -- the file shown
    revision = nil,  -- the revision of its text rendered, and the width
    width = nil,
    source = nil,    -- the lines of the text rendered
    follow = {},     -- the line of the output where a block starts, by the
                     -- line of the text it starts on
}

------------------------------------------------------------------------

local function is_markdown(path)
    if path == nil then
        return false
    end
    local lower = path:lower()
    return lower:match("%.md$") ~= nil or lower:match("%.markdown$") ~= nil
        or lower:match("%.mkd$") ~= nil
end

local function basename(path)
    return path:match("([^/]+)$") or path
end

local function count_lines(text)
    local _, n = text:gsub("\n", "")
    return n
end

local function split_lines(text)
    local lines = {}
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        lines[#lines + 1] = line
    end
    return lines
end

-- The columns the text has in the window, no more than the rendering takes.
local function window_width()
    local info = viewer:call("info", { id = preview.id })
    if info == nil then
        return nil
    end
    return math.max(20, math.min(info.cols, cfg.MAX_WIDTH)), info.lines
end

local function render(text, width)
    local ok, out = pcall(md.render, text, { width = width })
    if ok then
        return out
    end
    return "The document could not be rendered:\n" .. tostring(out) .. "\n"
end

------------------------------------------------------------------------

-- Render the file of @editor into the window, unless it is rendered at this
-- revision and width already.
local function update(editor, path, revision)
    local width = window_width()
    if width == nil then
        return
    end
    if path == preview.path and revision == preview.revision and width == preview.width then
        return
    end

    local text = editor:text()
    if text == nil then
        return
    end

    preview.path = path
    preview.revision = revision
    preview.width = width
    preview.source = split_lines(text)
    preview.follow = {}
    viewer:call("set_title", { id = preview.id, title = "Preview: " .. basename(path) })
    viewer:call("set_text", { id = preview.id, text = render(text, width) })
end

-- The line of the output where the block of @line (from 1) starts: the text
-- before it rendered, and its lines counted.  The renderer does not say
-- where a line comes from.
local function output_line(line)
    local lines = preview.source
    if lines == nil then
        return 0
    end

    local start = math.min(line, #lines)
    while start > 1 and lines[start - 1]:match("%S") ~= nil do
        start = start - 1
    end
    if start <= 1 then
        return 0
    end

    local cached = preview.follow[start]
    if cached == nil then
        cached = count_lines(render(table.concat(lines, "\n", 1, start - 1), preview.width))
        preview.follow[start] = cached
    end
    return cached
end

-- Scroll the window so that the block the cursor is in stands a third of the
-- way down.
local function follow(line)
    local _, height = window_width()
    local top = output_line(line)

    viewer:call("scroll_to", { id = preview.id, line = math.max(0, top - math.floor((height or 0) / 3)) })
end

------------------------------------------------------------------------

local function toggle(ev)
    local editor = ev.editor
    local path = editor ~= nil and editor:path() or nil

    if preview.id ~= nil and preview.shown then
        viewer:call("hide", { id = preview.id })
        preview.shown = false
        return mc.CONSUME
    end

    if not is_markdown(path) and preview.id == nil then
        mc.ui.status(TITLE .. ": not a markdown file")
        return mc.CONSUME
    end

    if preview.id == nil then
        local r, err = viewer:call("open", {
            title = "Preview: " .. basename(path),
            text = "",
            place = "right",
            focus = false,
        })
        if r == nil then
            if err == "not_found" then
                err = "The preview needs the viewer plugin, viewer.so."
            end
            mc.ui.message(TITLE, err)
            return mc.CONSUME
        end
        preview.id = r.id
        preview.path = nil
    else
        viewer:call("show", { id = preview.id, focus = false })
    end
    preview.shown = true

    if is_markdown(path) then
        local info = editor:info()
        update(editor, path, info ~= nil and info.revision or nil)
        local line = editor:cursor()
        if line ~= nil then
            follow(line)
        end
    end
    return mc.CONSUME
end

mc.macro {
    id = "toggle",
    area = "editor",
    key = "Ctrl-Alt-P",
    description = "Show or hide the preview of a markdown file",
    menu = { path = "Plugins", label = "Markdown preview", position = 100 },
    action = toggle,
}

mc.on("editor.change", function(ev)
    if preview.id ~= nil and preview.shown and is_markdown(ev.path) then
        update(ev.editor, ev.path, ev.revision)
    end
end)

mc.on("editor.cursor", function(ev)
    if preview.id == nil or not preview.shown or ev.path ~= preview.path then
        return
    end
    -- the window may have been resized: the text is laid out again
    local width = window_width()
    if width ~= nil and width ~= preview.width then
        local info = ev.editor:info()
        update(ev.editor, ev.path, info ~= nil and info.revision or nil)
    end
    follow(ev.line)
end)

viewer:on("closed", function(args)
    if args.id == preview.id then
        preview.id = nil
        preview.shown = false
        preview.path = nil
    end
end)

-- The settings of the rendering, from Manage Plugins.  A change is seen the
-- next time the text is rendered.
if mc.settings ~= nil then
    mc.settings(function()
        if settings.dialog() then
            preview.revision = nil
        end
    end)
end
