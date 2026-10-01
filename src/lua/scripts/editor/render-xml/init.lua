-- The renderer of XML for the Preview of the viewer plugin: an element to
-- a line, indented and colored the way the editor colors XML; a short text
-- on the line of its element, a long one wrapped under it; the number of
-- the elements an element holds beside it; a long run of elements of one
-- name cut short, and a run of elements of attributes and plain text drawn
-- as a table.  The view follows the cursor line for line, for it is known
-- where every line of it comes from.
--
-- A file that does not read as XML, as it does not halfway through a
-- change, is shown with the error on top and the last view that read under
-- it.

local parse = require("xmlparse").parse
local xmlview = require("xmlview")

local viewer = mc.service("viewer")

-- What a string of base64 holds, shown beside it: a module shared by the
-- renderers.  Without it the strings are shown as they are.
local blob_ok, base64text = pcall(require, "base64text")
local blob = blob_ok and base64text or nil

-- Larger files are shown as their text: reading them would hold the editor.
local MAX_SIZE = 16 * 1024 * 1024

-- The files of XML, by the ends of their names and how their texts start.
local SUFFIXES = {
    ".xml", ".xsd", ".xsl", ".xslt", ".dtd", ".svg", ".plist", ".rss", ".atom",
    ".wsdl", ".kml", ".gpx", ".xaml", ".csproj", ".vbproj", ".fsproj", ".vcxproj",
    ".props", ".targets", ".nuspec", ".resx", ".ui", ".glade", ".xib", ".storyboard",
    ".fodt", ".fods", ".fodp", ".docbook", ".xlf", ".xliff", ".qrc",
    ".policy", ".metainfo.xml", ".appdata.xml",
}
local STARTS = { "<?xml" }

-- The colors when the editor has no rules for XML: those of xml.syntax.
local DEFAULT_COLORS = {
    tag = "\27[97m", attr = "\27[93m", value = "\27[96m", text = nil,
    comment = "\27[92m", decl = "\27[93m", error = "\27[1;91m",
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

-- The colors the editor gives the kinds of an XML text, found by coloring
-- a sample of each: what the user set for XML is what the preview shows.
local colors = nil

local function xml_colors()
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

    local sample = '<?xml version="1.0"?><!DOCTYPE d><a b="c">t<!-- n --></a>'
    local scan = mc.syntax.scan(sample, { filename = "sample.xml" })
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
        tag = sample:find("<a", 1, true) + 1,
        attr = sample:find(" b=", 1, true) + 1,
        value = sample:find('"c"', 1, true),
        comment = sample:find("<!--", 1, true),
        decl = sample:find("<!DOCTYPE", 1, true),
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

-- What is shown in each window of the viewer: the last view that read.
local shown = {}

-- The windows that show an error now.
local failed = {}

local function lines_text(lines)
    return table.concat(lines, "\n") .. "\n"
end

-- The viewer asks what files are whose: these are XML.
viewer:on("types", function()
    viewer:call("add_type", { type = "xml", suffixes = SUFFIXES, starts = STARTS })
end)

viewer:on("render", function(args)
    if args.type ~= "xml" then
        return
    end

    if #args.text > MAX_SIZE then
        shown[args.id] = nil
        viewer:call("set_text", { id = args.id, text = args.text })
        return
    end

    local c = xml_colors()
    local nodes, err = parse(args.text)

    if nodes ~= nil then
        failed[args.id] = nil
        local view = xmlview.view(nodes, {
            width = args.width, colors = c, text_width = text_width, blob = blob,
        })
        shown[args.id] = view
        viewer:call("set_text", { id = args.id, text = lines_text(view.lines) })
        return
    end

    -- it does not read: where, and the last view that did
    local head = string.format("%sXML: line %d, column %d: %s\27[0m",
        c.error, err.line, err.column, err.message)
    local last = shown[args.id]
    local body

    if last ~= nil then
        body = (c.comment or "") .. "<!-- the last view that read -->\27[0m\n" .. lines_text(last.lines)
    else
        body = args.text
    end
    failed[args.id] = true
    viewer:call("set_text", { id = args.id, text = head .. "\n\n" .. body })
    viewer:call("scroll_to", { id = args.id, line = 0 })
end)

viewer:on("follow", function(args)
    local view = shown[args.id]
    if args.type ~= "xml" or view == nil then
        return
    end
    -- the error stays in sight: the lines under it are not where the view says
    if failed[args.id] then
        viewer:call("scroll_to", { id = args.id, line = 0 })
        return
    end

    local info = viewer:call("info", { id = args.id })
    local height = info ~= nil and info.lines or 0

    viewer:call("scroll_to", {
        id = args.id,
        line = math.max(0, xmlview.find(view, args.line) - math.floor(height / 3)),
    })
end)

viewer:on("closed", function(args)
    shown[args.id] = nil
    failed[args.id] = nil
end)
