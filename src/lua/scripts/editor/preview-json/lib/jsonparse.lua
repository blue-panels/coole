-- JSON read into nodes that keep what a view needs and a table of Lua
-- loses: the order of the keys, the text of every value as it is written,
-- and the line every value starts on.
--
-- It takes what files read as JSON hold in practice: comments, // to the
-- end of the line and /* */, a comma before a closing bracket, and more
-- than one value, one after the other, as in JSON Lines.
--
-- A node is one of
--   { t = "object", line = n, members = { { key = node, value = node }... },
--     close = n }
--   { t = "array", line = n, items = { node... }, close = n }
--   { t = "string" | "number" | "literal", line = n, raw = "text" }
-- The key of a member is a node of type "string".

local M = {}

-- What went wrong, and where: the line and the column, from 1.
local function fail(state, message, pos)
    pos = pos or state.pos
    local line, col = 1, 1
    local i = 1

    while true do
        local nl = state.text:find("\n", i, true)
        if nl == nil or nl >= pos then
            col = pos - i + 1
            break
        end
        line = line + 1
        i = nl + 1
    end
    error({ json_error = true, line = line, column = col, message = message }, 0)
end

-- The line of the byte at pos, counted on from the last one asked for.
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

-- Over white space and comments.
local function skip(state)
    local text = state.text

    while true do
        local pos = text:find("[^ \t\r\n]", state.pos)
        if pos == nil then
            state.pos = #text + 1
            return
        end
        state.pos = pos
        local two = text:sub(pos, pos + 1)
        if two == "//" then
            local nl = text:find("\n", pos, true)
            state.pos = nl ~= nil and nl + 1 or #text + 1
        elseif two == "/*" then
            local stop = text:find("*/", pos + 2, true)
            if stop == nil then
                fail(state, "the comment does not end", pos)
            end
            state.pos = stop + 2
        else
            return
        end
    end
end

local parse_value

local function parse_string(state)
    local text = state.text
    local start = state.pos
    local pos = start + 1

    while true do
        local c = text:find('["\\\n]', pos)
        if c == nil or text:sub(c, c) == "\n" then
            fail(state, "the string does not end", start)
        end
        if text:sub(c, c) == "\\" then
            pos = c + 2
        else
            state.pos = c + 1
            return { t = "string", line = line_of(state, start), raw = text:sub(start, c) }
        end
    end
end

local function parse_number(state)
    local text = state.text
    local start = state.pos
    local raw = text:match("^-?%d+%.?%d*[eE]?[-+]?%d*", start)

    if raw == nil or raw == "" or raw == "-" then
        fail(state, "a value is expected")
    end
    state.pos = start + #raw
    return { t = "number", line = line_of(state, start), raw = raw }
end

local function parse_container(state, open, close)
    local start = state.pos
    local node = { line = line_of(state, start) }
    local list = {}

    if open == "{" then
        node.t = "object"
        node.members = list
    else
        node.t = "array"
        node.items = list
    end

    state.pos = state.pos + 1
    state.depth = state.depth + 1
    if state.depth > 512 then
        fail(state, "the values are nested too deep", start)
    end

    while true do
        skip(state)
        local c = state.text:sub(state.pos, state.pos)

        if c == close then
            node.close = line_of(state, state.pos)
            state.pos = state.pos + 1
            state.depth = state.depth - 1
            return node
        end
        if c == "" then
            fail(state, "the " .. (open == "{" and "object" or "array") .. " does not end", start)
        end

        if open == "{" then
            if c ~= '"' then
                fail(state, "a key in quotes is expected")
            end
            local key = parse_string(state)
            skip(state)
            if state.text:sub(state.pos, state.pos) ~= ":" then
                fail(state, "a colon is expected after the key")
            end
            state.pos = state.pos + 1
            skip(state)
            list[#list + 1] = { key = key, value = parse_value(state) }
        else
            list[#list + 1] = parse_value(state)
        end

        skip(state)
        c = state.text:sub(state.pos, state.pos)
        if c == "," then
            state.pos = state.pos + 1
        elseif c ~= close then
            fail(state, "a comma or " .. close .. " is expected")
        end
    end
end

parse_value = function(state)
    local text = state.text
    local c = text:sub(state.pos, state.pos)

    if c == "{" then
        return parse_container(state, "{", "}")
    elseif c == "[" then
        return parse_container(state, "[", "]")
    elseif c == '"' then
        return parse_string(state)
    elseif c == "-" or c:match("%d") then
        return parse_number(state)
    end

    for _, word in ipairs({ "true", "false", "null" }) do
        if text:sub(state.pos, state.pos + #word - 1) == word then
            local start = state.pos
            state.pos = state.pos + #word
            return { t = "literal", line = line_of(state, start), raw = word }
        end
    end
    fail(state, "a value is expected")
end

-- The values of text, one after the other: the nodes, or nil and the
-- error, { line, column, message }.
function M.parse(text)
    local state = { text = text, pos = 1, line = 1, line_pos = 1, depth = 0 }
    local roots = {}

    local ok, err = pcall(function()
        skip(state)
        while state.pos <= #text do
            roots[#roots + 1] = parse_value(state)
            skip(state)
            if text:sub(state.pos, state.pos) == "," then
                state.pos = state.pos + 1
                skip(state)
            end
        end
    end)

    if not ok then
        if type(err) == "table" and err.json_error then
            return nil, err
        end
        return nil, { line = 1, column = 1, message = tostring(err) }
    end
    return roots
end

return M
