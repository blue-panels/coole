-- XML read into nodes that keep what a view needs: the order of the
-- elements and their attributes, the prefixes of the names as they are
-- written, and the line every node starts on.
--
-- A node is one of
--   { t = "element", name = "a:b", line = n, close = n,
--     attrs = { { name = "x", value = "text", line = n }... },
--     children = { node... } }
--   { t = "text", text = "words", line = n }  white space run together
--   { t = "comment" | "cdata" | "pi" | "doctype", raw = "<!-- ... -->", line = n }
-- The value of an attribute and a text have their entities replaced:
-- &amp; &lt; &gt; &quot; &apos; and &#...; ; another stays as it is.
-- A text of white space only is left out.

local M = {}

local ENTITIES = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'" }

local function char_of(code)
    if code == nil or code > 0x10FFFF then
        return nil
    end
    return utf8.char(code)
end

local function decode(s)
    if s:find("&", 1, true) == nil then
        return s
    end
    s = s:gsub("&#[xX](%x+);", function(hex)
        return char_of(tonumber(hex, 16))
    end)
    s = s:gsub("&#(%d+);", function(dec)
        return char_of(tonumber(dec))
    end)
    return (s:gsub("&(%a+);", ENTITIES))
end

-- What went wrong, and where: the line and the column, from 1.
local function fail(state, message, pos)
    pos = pos or state.pos
    local line, i = 1, 1

    while true do
        local nl = state.text:find("\n", i, true)
        if nl == nil or nl >= pos then
            break
        end
        line = line + 1
        i = nl + 1
    end
    error({ xml_error = true, line = line, column = pos - i + 1, message = message }, 0)
end

-- The line of the byte at pos; pos does not go back.
local function line_of(state, pos)
    while state.line_pos < pos do
        local nl = state.text:find("\n", state.line_pos, true)
        if nl == nil or nl >= pos then
            state.line_pos = pos
            break
        end
        state.line = state.line + 1
        state.line_pos = nl + 1
    end
    return state.line
end

local NAME = "^[%a_:\128-\255][%w_:%.%-\128-\255]*"

-- A node that runs from pos to the first stop after it, taken as it is.
local function raw_node(state, t, stop, what)
    local start = state.pos
    local e = state.text:find(stop, start, true)
    if e == nil then
        fail(state, "the " .. what .. " does not end", start)
    end
    state.pos = e + #stop
    return { t = t, line = line_of(state, start), raw = state.text:sub(start, e + #stop - 1) }
end

-- <!DOCTYPE ...>, which may hold [ declarations ] with > in them.
local function doctype_node(state)
    local text = state.text
    local start = state.pos
    local pos = start
    local depth = 0

    while pos <= #text do
        local c = text:sub(pos, pos)
        if c == "[" then
            depth = depth + 1
        elseif c == "]" then
            depth = depth - 1
        elseif c == ">" and depth <= 0 then
            state.pos = pos + 1
            return { t = "doctype", line = line_of(state, start), raw = text:sub(start, pos) }
        elseif c == '"' or c == "'" then
            local e = text:find(c, pos + 1, true)
            if e == nil then
                break
            end
            pos = e
        end
        pos = pos + 1
    end
    fail(state, "the declaration does not end", start)
end

local function parse_attrs(state, element)
    local text = state.text

    while true do
        local ws = text:match("^%s*", state.pos)
        state.pos = state.pos + #ws
        local c = text:sub(state.pos, state.pos)

        if c == ">" or c == "/" or c == "" then
            return
        end

        local name = text:match(NAME, state.pos)
        if name == nil then
            fail(state, "a name of an attribute is expected")
        end
        if ws == "" then
            fail(state, "a space is expected before the attribute")
        end
        local line = line_of(state, state.pos)
        state.pos = state.pos + #name

        local eq = text:match("^%s*=%s*", state.pos)
        if eq == nil then
            fail(state, "= is expected after " .. name)
        end
        state.pos = state.pos + #eq

        local quote = text:sub(state.pos, state.pos)
        if quote ~= '"' and quote ~= "'" then
            fail(state, "the value of " .. name .. " is expected in quotes")
        end
        local e = text:find(quote, state.pos + 1, true)
        if e == nil then
            fail(state, "the value of " .. name .. " does not end")
        end
        element.attrs[#element.attrs + 1] = {
            name = name, line = line, value = decode(text:sub(state.pos + 1, e - 1)),
        }
        state.pos = e + 1
    end
end

-- The nodes up to the end tag of parent, or to the end of the text when
-- parent is nil, into list.
local function parse_nodes(state, list, parent)
    local text = state.text

    while true do
        local lt = text:find("<", state.pos, true)
        local stop = lt or (#text + 1)

        -- the text before it
        if stop > state.pos then
            local chunk = text:sub(state.pos, stop - 1)
            local first = chunk:find("%S")
            if first ~= nil then
                local words = decode(chunk):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", "")
                list[#list + 1] = { t = "text", text = words, line = line_of(state, state.pos + first - 1) }
            end
            state.pos = stop
        end

        if lt == nil then
            if parent ~= nil then
                fail(state, "<" .. parent.name .. "> is not closed", parent.pos)
            end
            return
        end

        if text:sub(lt, lt + 3) == "<!--" then
            list[#list + 1] = raw_node(state, "comment", "-->", "comment")
        elseif text:sub(lt, lt + 8) == "<![CDATA[" then
            list[#list + 1] = raw_node(state, "cdata", "]]>", "CDATA section")
        elseif text:sub(lt, lt + 1) == "<?" then
            list[#list + 1] = raw_node(state, "pi", "?>", "processing instruction")
        elseif text:sub(lt, lt + 1) == "<!" then
            list[#list + 1] = doctype_node(state)
        elseif text:sub(lt, lt + 1) == "</" then
            local name = text:match(NAME, lt + 2)
            if parent == nil then
                fail(state, "</" .. (name or "") .. "> closes nothing")
            end
            if name ~= parent.name then
                fail(state, "</" .. (name or "") .. "> closes <" .. parent.name .. ">")
            end
            local e = text:find(">", lt, true)
            if e == nil or text:sub(lt + 2 + #name, e - 1):find("%S") then
                fail(state, "> is expected after </" .. name)
            end
            parent.close = line_of(state, lt)
            state.pos = e + 1
            return
        else
            local name = text:match(NAME, lt + 1)
            if name == nil then
                fail(state, "a name of an element is expected after <")
            end
            local element = {
                t = "element", name = name, line = line_of(state, lt),
                attrs = {}, children = {}, pos = lt,
            }
            state.pos = lt + 1 + #name
            parse_attrs(state, element)
            list[#list + 1] = element

            if text:sub(state.pos, state.pos + 1) == "/>" then
                state.pos = state.pos + 2
                element.close = element.line
            elseif text:sub(state.pos, state.pos) == ">" then
                state.pos = state.pos + 1
                state.depth = state.depth + 1
                if state.depth > 512 then
                    fail(state, "the elements are nested too deep", lt)
                end
                parse_nodes(state, element.children, element)
                state.depth = state.depth - 1
            else
                fail(state, "> is expected to end <" .. name)
            end
            element.pos = nil
        end
    end
end

-- The nodes of text, or nil and the error, { line, column, message }.
function M.parse(text)
    local state = { text = text, pos = 1, line = 1, line_pos = 1, depth = 0 }
    local nodes = {}

    -- a byte order mark is no text
    if text:sub(1, 3) == "\239\187\191" then
        state.pos = 4
    end

    local ok, err = pcall(parse_nodes, state, nodes, nil)
    if not ok then
        if type(err) == "table" and err.xml_error then
            return nil, err
        end
        return nil, { line = 1, column = 1, message = tostring(err) }
    end
    return nodes
end

return M
