-- The view of JSON read by jsonparse: one value to a line, indented by
-- depth, colored by kind; the size of an object or an array beside it, and
-- a long array cut short; an array of objects of the same kind drawn as a
-- table.  Every line of the view knows the line of the file it comes from,
-- so that the view follows the cursor.

local M = {}

M.INDENT = 2
M.MAX_ITEMS = 100    -- an array is cut after this many values
M.MAX_PACKED = 1000  -- an array of plain values, packed to the width, after this many
M.TABLE_COLUMNS = 16 -- an array of objects with more keys is no table
M.COUNT_ARRAY = 2    -- an array of at least this many values says how many
M.COUNT_OBJECT = 5   -- an object of at least this many keys says how many
M.INLINE_OBJECT = 3  -- an object of no more keys of plain values is one line when it fits

local RESET = "\27[0m"

-- The frame of a table.
local FRAME = {
    top = { "┌", "┬", "┐" }, mid = { "├", "┼", "┤" }, bottom = { "└", "┴", "┘" },
    h = "─", v = "│",
}

------------------------------------------------------------------------

local function new_view(opts)
    return {
        lines = {},
        src = {},
        width = opts.width or 80,
        colors = opts.colors or {},
        text_width = opts.text_width or function(s) return utf8.len(s) or #s end,
    }
end

-- text in the color of a kind: key, string, number, literal, punct, comment
local function paint(view, kind, text)
    local sgr = view.colors[kind]
    if sgr == nil or sgr == "" then
        return text
    end
    return sgr .. text .. RESET
end

local function emit(view, text, line)
    view.lines[#view.lines + 1] = text
    view.src[#view.src + 1] = line
end

local function plural(n, one, many)
    return tostring(n) .. " " .. (n == 1 and one or many)
end

local function is_scalar(node)
    return node.t == "string" or node.t == "number" or node.t == "literal"
end

local function scalar_kind(node)
    return node.t == "string" and "string" or node.t
end

-- The text of a value in a cell: a string without its quotes.
local function cell_text(node)
    if node == nil then
        return ""
    end
    if node.t == "string" then
        return node.raw:sub(2, -2)
    end
    return node.raw
end

------------------------------------------------------------------------
-- A table: an array of at least two objects, all of them of scalar
-- values, with not too many keys among them.

local function table_columns(node)
    if node.t ~= "array" or #node.items < 2 then
        return nil
    end

    local columns, seen = {}, {}
    for _, item in ipairs(node.items) do
        if item.t ~= "object" or #item.members == 0 then
            return nil
        end
        for _, m in ipairs(item.members) do
            if not is_scalar(m.value) then
                return nil
            end
            local key = m.key.raw:sub(2, -2)
            if not seen[key] then
                seen[key] = true
                columns[#columns + 1] = key
                if #columns > M.TABLE_COLUMNS then
                    return nil
                end
            end
        end
    end
    return columns
end

local function frame_line(view, indent, widths, parts)
    local pieces = {}
    for i, w in ipairs(widths) do
        pieces[i] = FRAME.h:rep(w + 2)
    end
    return indent .. paint(view, "punct", parts[1] .. table.concat(pieces, parts[2]) .. parts[3])
end

local function pad(view, text, width, right)
    local space = (" "):rep(width - view.text_width(text))
    return right and (space .. text) or (text .. space)
end

-- The rows of the table, or FALSE when it is wider than the view.
local function emit_table(view, node, columns, indent, shown)
    local widths = {}
    local rows = {}

    for i, key in ipairs(columns) do
        widths[i] = view.text_width(key)
    end
    for r = 1, shown do
        local item = node.items[r]
        local by_key = {}
        for _, m in ipairs(item.members) do
            by_key[m.key.raw:sub(2, -2)] = m.value
        end
        local row = {}
        for i, key in ipairs(columns) do
            local value = by_key[key]
            row[i] = value
            widths[i] = math.max(widths[i], view.text_width(cell_text(value)))
        end
        rows[r] = row
    end

    local total = #indent + 1
    for _, w in ipairs(widths) do
        total = total + w + 3
    end
    if total > view.width then
        return false
    end

    local v = paint(view, "punct", FRAME.v)
    local line = node.line

    emit(view, frame_line(view, indent, widths, FRAME.top), line)
    local head = {}
    for i, key in ipairs(columns) do
        head[i] = " " .. paint(view, "key", pad(view, key, widths[i])) .. " "
    end
    emit(view, indent .. v .. table.concat(head, v) .. v, line)
    emit(view, frame_line(view, indent, widths, FRAME.mid), line)

    for r, row in ipairs(rows) do
        local cells = {}
        -- a row may lack keys: every column is gone through, not up to the first it lacks
        for i = 1, #columns do
            local value = row[i]
            local text = pad(view, cell_text(value), widths[i], value ~= nil and value.t == "number")
            if value ~= nil then
                text = paint(view, scalar_kind(value), text)
            end
            cells[i] = " " .. text .. " "
        end
        emit(view, indent .. v .. table.concat(cells, v) .. v, node.items[r].line)
    end
    emit(view, frame_line(view, indent, widths, FRAME.bottom), node.close or line)
    return true
end

------------------------------------------------------------------------

-- An array of values that are not containers, on one line when it fits.
local function inline_array(view, node, prefix_width)
    if #node.items > M.MAX_ITEMS then
        return nil
    end
    local parts, plain = {}, {}
    for i, item in ipairs(node.items) do
        if not is_scalar(item) then
            return nil
        end
        parts[i] = paint(view, scalar_kind(item), item.raw)
        plain[i] = item.raw
    end
    local width = prefix_width + view.text_width(table.concat(plain, ", ")) + 3
    if width > view.width then
        return nil
    end
    return paint(view, "punct", "[") .. table.concat(parts, paint(view, "punct", ", "))
        .. paint(view, "punct", "]")
end

-- An object of a few plain values, on one line when it fits.
local function inline_object(view, node, prefix_width)
    if #node.members > M.INLINE_OBJECT then
        return nil
    end
    local parts, plain = {}, {}
    for i, m in ipairs(node.members) do
        if not is_scalar(m.value) then
            return nil
        end
        parts[i] = paint(view, "key", m.key.raw) .. paint(view, "punct", ": ")
            .. paint(view, scalar_kind(m.value), m.value.raw)
        plain[i] = m.key.raw .. ": " .. m.value.raw
    end
    local width = prefix_width + view.text_width(table.concat(plain, ", ")) + 4
    if width > view.width then
        return nil
    end
    return paint(view, "punct", "{ ") .. table.concat(parts, paint(view, "punct", ", "))
        .. paint(view, "punct", " }")
end

-- An array of plain values too long for one line: as many to a line as fit.
local function emit_packed(view, node, indent)
    local inner = indent .. (" "):rep(M.INDENT)
    local shown = math.min(#node.items, M.MAX_PACKED)
    local parts, used, first = {}, #inner, nil

    local function flush()
        if #parts > 0 then
            emit(view, inner .. table.concat(parts, " "), first)
            parts, used, first = {}, #inner, nil
        end
    end

    for i = 1, shown do
        local item = node.items[i]
        local comma = i < #node.items and paint(view, "punct", ",") or ""
        local w = view.text_width(item.raw) + (comma ~= "" and 1 or 0)

        if #parts > 0 and used + 1 + w > view.width then
            flush()
        end
        parts[#parts + 1] = paint(view, scalar_kind(item), item.raw) .. comma
        used = used + (#parts > 1 and 1 or 0) + w
        first = first or item.line
    end
    flush()

    if shown < #node.items then
        emit(view, inner .. paint(view, "comment", "/* … "
            .. plural(#node.items - shown, "more value", "more values") .. " */"),
            node.items[shown + 1].line)
    end
end

local function all_scalar(list)
    for _, item in ipairs(list) do
        if not is_scalar(item) then
            return false
        end
    end
    return true
end

local emit_node

-- The values of a container; an array is cut after MAX_ITEMS, an object
-- shows every key.
local function emit_children(view, node, indent)
    local list = node.t == "object" and node.members or node.items
    local shown = node.t == "object" and #list or math.min(#list, M.MAX_ITEMS)
    local inner = indent .. (" "):rep(M.INDENT)

    for i = 1, shown do
        local comma = (i < #list) and paint(view, "punct", ",") or ""
        if node.t == "object" then
            emit_node(view, list[i].value, inner, list[i].key, comma)
        else
            emit_node(view, list[i], inner, nil, comma)
        end
    end
    if shown < #list then
        local rest = #list - shown
        emit(view, inner .. paint(view, "comment", "/* … "
            .. plural(rest, "more value", "more values") .. " */"), list[shown + 1].line)
    end
end

emit_node = function(view, node, indent, key, comma)
    local prefix = indent
    if key ~= nil then
        prefix = prefix .. paint(view, "key", key.raw) .. paint(view, "punct", ": ")
    end
    local prefix_width = #indent + (key ~= nil and view.text_width(key.raw) + 2 or 0)
    local line = key ~= nil and key.line or node.line

    if is_scalar(node) then
        emit(view, prefix .. paint(view, scalar_kind(node), node.raw) .. comma, line)
        return
    end

    local list = node.t == "object" and node.members or node.items
    local open = node.t == "object" and "{" or "["
    local close = node.t == "object" and "}" or "]"

    if #list == 0 then
        emit(view, prefix .. paint(view, "punct", open .. close) .. comma, line)
        return
    end

    local one_line
    if node.t == "array" then
        one_line = inline_array(view, node, prefix_width + (comma ~= "" and 1 or 0))
    else
        one_line = inline_object(view, node, prefix_width + (comma ~= "" and 1 or 0))
    end
    if one_line ~= nil then
        emit(view, prefix .. one_line .. comma, line)
        return
    end

    -- how big it is, beside the bracket that opens it
    local count = ""
    if node.t == "array" and #list >= M.COUNT_ARRAY then
        count = "  " .. paint(view, "comment", "/* " .. plural(#list, "value", "values") .. " */")
    elseif node.t == "object" and #list >= M.COUNT_OBJECT then
        count = "  " .. paint(view, "comment", "/* " .. plural(#list, "key", "keys") .. " */")
    end

    local columns = table_columns(node)
    if columns ~= nil then
        local shown = math.min(#list, M.MAX_ITEMS)
        local first = #view.lines
        emit(view, prefix .. paint(view, "punct", open) .. count, line)
        if emit_table(view, node, columns, indent .. (" "):rep(M.INDENT), shown) then
            if shown < #list then
                emit(view, indent .. (" "):rep(M.INDENT) .. paint(view, "comment", "/* … "
                    .. plural(#list - shown, "more row", "more rows") .. " */"),
                    list[shown + 1].line)
            end
            emit(view, indent .. paint(view, "punct", close) .. comma, node.close or line)
            return
        end
        -- too wide for a table: the lines of it go, the values are shown one by one
        for i = #view.lines, first + 1, -1 do
            view.lines[i] = nil
            view.src[i] = nil
        end
    end

    emit(view, prefix .. paint(view, "punct", open) .. count, line)
    if node.t == "array" and all_scalar(list) then
        emit_packed(view, node, indent)
    else
        emit_children(view, node, indent)
    end
    emit(view, indent .. paint(view, "punct", close) .. comma, node.close or line)
end

------------------------------------------------------------------------

-- The view of the values roots, opts = { width, colors, text_width }:
-- { lines = { text... }, src = { line of the file... } }.
function M.view(roots, opts)
    local view = new_view(opts or {})
    local shown = math.min(#roots, M.MAX_ITEMS)

    for i = 1, shown do
        emit_node(view, roots[i], "", nil, "")
    end
    if shown < #roots then
        emit(view, paint(view, "comment", "/* … "
            .. plural(#roots - shown, "more value", "more values") .. " */"), roots[shown + 1].line)
    end
    return view
end

-- The line of the view, from 0, that shows the line of the file line: the
-- last that comes from it or from a line before it.
function M.find(view, line)
    local lo, hi = 1, #view.src
    local found = 0

    while lo <= hi do
        local mid = (lo + hi) // 2
        if view.src[mid] <= line then
            found = mid
            lo = mid + 1
        else
            hi = mid - 1
        end
    end
    -- the first line of the view that comes from it, when several do
    while found > 1 and view.src[found - 1] == view.src[found] and view.src[found] == line do
        found = found - 1
    end
    return math.max(0, found - 1)
end

return M
