-- The renderer of JSON for the Preview of the viewer plugin: one value to a
-- line, indented and colored the way the editor colors JSON, the size of
-- every object and array beside it, a long array cut short, and an array of
-- objects of one kind drawn as a table.  The view follows the cursor line
-- for line, for it is known where every line of it comes from.
--
-- A file that does not read as JSON, as it does not halfway through a
-- change, is shown with the error on top and the last view that read under
-- it.

local parse = require("jsonparse").parse
local jsonview = require("jsonview")

local viewer = mc.service("viewer")

-- Larger files are shown as their text: reading them would hold the editor.
local MAX_SIZE = 16 * 1024 * 1024

-- The colors when the editor has no rules for JSON: those of json.syntax.
local DEFAULT_COLORS = {
    key = "\27[93m", string = "\27[32m", number = "\27[95m",
    literal = "\27[91m", punct = "\27[96m", comment = "\27[33m",
    error = "\27[1;91m",
}

local SGR_NAMES = {
    black = 30, red = 31, green = 32, brown = 33, blue = 34, magenta = 35,
    cyan = 36, lightgray = 37, gray = 90, brightred = 91, brightgreen = 92,
    yellow = 93, brightblue = 94, brightmagenta = 95, brightcyan = 96, white = 97,
}
local SGR_ATTRS = { bold = "1", italic = "3", underline = "4", reverse = "7" }

-- The SGR of a color of the syntax rules, nil for none.
local function sgr_of(color)
    if color == nil then
        return nil
    end
    local codes = {}
    if color.attrs ~= nil then
        for attr in color.attrs:gmatch("[^+]+") do
            codes[#codes + 1] = SGR_ATTRS[attr]
        end
    end
    local fg = color.fg
    if fg ~= nil then
        if SGR_NAMES[fg] ~= nil then
            codes[#codes + 1] = tostring(SGR_NAMES[fg])
        elseif fg:match("^color%d+$") then
            codes[#codes + 1] = "38;5;" .. fg:sub(6)
        end
    end
    if #codes == 0 then
        return nil
    end
    return "\27[" .. table.concat(codes, ";") .. "m"
end

-- The colors the editor gives the kinds of a JSON text, found by coloring a
-- sample of each: what the user set for JSON is what the preview shows.
local colors = nil

local function json_colors()
    if colors ~= nil then
        return colors
    end
    colors = {}
    for kind, sgr in pairs(DEFAULT_COLORS) do
        colors[kind] = sgr
    end
    if mc.syntax == nil or mc.syntax.scan == nil then
        return colors
    end

    local sample = '{"key": "text", "n": 12, "b": true} /* note */'
    local scan = mc.syntax.scan(sample, { filename = "sample.json" })
    if scan == nil or scan.runs == nil then
        return colors
    end

    local function color_at(pos)
        for _, run in ipairs(scan.runs) do
            if pos >= run.offset and pos < run.offset + run.length then
                return sgr_of(scan.colors[run.color])
            end
        end
        return nil
    end

    local where = {
        key = sample:find('"key"', 1, true),
        string = sample:find('"text"', 1, true),
        number = sample:find("12", 1, true),
        literal = sample:find("true", 1, true),
        punct = sample:find("{", 1, true),
        comment = sample:find("/*", 1, true),
    }
    for kind, pos in pairs(where) do
        local sgr = color_at(pos)
        if sgr ~= nil then
            colors[kind] = sgr
        end
    end
    return colors
end

------------------------------------------------------------------------

local function text_width(s)
    local w = mc.ui ~= nil and mc.ui.text_width ~= nil and mc.ui.text_width(s) or nil
    return w or utf8.len(s) or #s
end

-- What is shown in each window of the viewer: the view, and the text of
-- the last view that read, for a file that does not read now.
local shown = {}

local function lines_text(lines)
    return table.concat(lines, "\n") .. "\n"
end

-- The viewer asks what files are whose: these are JSON.
viewer:on("types", function()
    viewer:call("add_type", {
        type = "json",
        suffixes = { ".json", ".jsonl", ".ndjson", ".jsonc", ".geojson", ".webmanifest" },
    })
end)

viewer:on("render", function(args)
    if args.type ~= "json" then
        return
    end

    if #args.text > MAX_SIZE then
        shown[args.id] = nil
        viewer:call("set_text", { id = args.id, text = args.text })
        return
    end

    local c = json_colors()
    local roots, err = parse(args.text)

    if roots ~= nil then
        local view = jsonview.view(roots, { width = args.width, colors = c, text_width = text_width })
        shown[args.id] = view
        viewer:call("set_text", { id = args.id, text = lines_text(view.lines) })
        return
    end

    -- it does not read: where, and the last view that did
    local head = string.format("%sJSON: line %d, column %d: %s\27[0m",
        c.error, err.line, err.column, err.message)
    local last = shown[args.id]
    local body

    if last ~= nil then
        body = c.comment .. "/* the last view that read */\27[0m\n" .. lines_text(last.lines)
    else
        body = args.text
    end
    viewer:call("set_text", { id = args.id, text = head .. "\n\n" .. body })
end)

viewer:on("follow", function(args)
    local view = shown[args.id]
    if args.type ~= "json" or view == nil then
        return
    end

    local info = viewer:call("info", { id = args.id })
    local height = info ~= nil and info.lines or 0

    viewer:call("scroll_to", {
        id = args.id,
        line = math.max(0, jsonview.find(view, args.line) - math.floor(height / 3)),
    })
end)

viewer:on("closed", function(args)
    shown[args.id] = nil
end)
