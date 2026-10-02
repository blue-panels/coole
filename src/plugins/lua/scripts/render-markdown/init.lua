-- The renderer of markdown for the Preview of the viewer plugin.  The
-- viewer tells the type of the file and asks for its view with the signal
-- "render"; this script answers for markdown with the text the way it is
-- meant to read, and to "follow" with where the cursor is in it.  Any
-- other type is somebody else's, or the viewer shows the text as it is.
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
    return dir .. "/render-markdown.ini"
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

-- What was rendered for each window of the viewer: the lines of the text,
-- the width, and the line of the view where a block starts, by the line of
-- the text it starts on.
local shown = {}

------------------------------------------------------------------------

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

local function render(text, width, screen)
    local ok, out = pcall(md.render, text, { width = width, screen = screen })
    if ok then
        return out
    end
    return "The document could not be rendered:\n" .. tostring(out) .. "\n"
end

-- The line of the view where the block of @line (from 1) starts: the text
-- before it rendered, and its lines counted.  The renderer does not say
-- where a line comes from.
local function view_line(state, line)
    local lines = state.lines
    local start = math.min(line, #lines)

    while start > 1 and lines[start - 1]:match("%S") ~= nil do
        start = start - 1
    end
    if start <= 1 then
        return 0
    end

    local cached = state.blocks[start]
    if cached == nil then
        cached = count_lines(render(table.concat(lines, "\n", 1, start - 1), state.width, state.screen))
        state.blocks[start] = cached
    end
    return cached
end

------------------------------------------------------------------------

-- The viewer asks what files are whose: these are markdown.
viewer:on("types", function()
    viewer:call("add_type", { type = "markdown", suffixes = { ".md", ".markdown", ".mkd" } })
end)

viewer:on("render", function(args)
    if args.type ~= "markdown" then
        return
    end

    local screen = math.max(20, args.width or cfg.DEFAULT_WIDTH)
    local width = math.min(screen, cfg.MAX_WIDTH)

    shown[args.id] = { lines = split_lines(args.text), width = width, screen = screen, blocks = {} }
    viewer:call("set_text", { id = args.id, text = render(args.text, width, screen) })
end)

viewer:on("follow", function(args)
    local state = shown[args.id]
    if args.type ~= "markdown" or state == nil then
        return
    end

    local info = viewer:call("info", { id = args.id })
    local height = info ~= nil and info.lines or 0

    viewer:call("scroll_to", {
        id = args.id,
        line = math.max(0, view_line(state, args.line) - math.floor(height / 3)),
    })
end)

viewer:on("closed", function(args)
    shown[args.id] = nil
end)

-- The settings of the rendering, from Manage Plugins.  They are seen the
-- next time the text is rendered.
if mc.settings ~= nil then
    mc.settings(function()
        settings.dialog()
    end)
end
