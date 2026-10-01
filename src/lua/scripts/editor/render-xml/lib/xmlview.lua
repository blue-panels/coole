-- The view of XML read by xmlparse: an element to a line, indented by
-- depth; a short text on the line of its element, a long one wrapped below
-- it; the attributes on the line of the tag, or one to a line when they do
-- not fit.  An element of many children says how many; a long run of
-- elements of one name is cut short; a run of elements of one name that
-- hold only attributes and elements of plain text is drawn as a table.
-- Every line of the view knows the line of the file it comes from, so
-- that the view follows the cursor.

local M = {}

M.INDENT = 2
M.MAX_ITEMS = 100    -- a run of elements of one name is cut after this many
M.TABLE_COLUMNS = 16 -- a run of elements with more fields is no table
M.COUNT_CHILDREN = 5 -- an element of at least this many elements says how many

local RESET = "\27[0m"

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
        blob = opts.blob,
    }
end

-- text in the color of a kind: tag, attr, value, text, entity, comment, decl
local function paint(view, kind, text)
    local sgr = view.colors[kind]
    if sgr == nil or sgr == "" or text == "" then
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

-- The words of text in lines no wider than width.
local function wrap(view, text, width)
    local lines, line = {}, ""

    width = math.max(width, 10)
    for word in text:gmatch("%S+") do
        if line == "" then
            line = word
        elseif view.text_width(line) + 1 + view.text_width(word) <= width then
            line = line .. " " .. word
        else
            lines[#lines + 1] = line
            line = word
        end
    end
    if line ~= "" then
        lines[#lines + 1] = line
    end
    return lines
end

------------------------------------------------------------------------
-- Base64: what a text or a value holds, when the view is given a reader of
-- it (opts.blob: inspect and note, as base64text has them).  Kept on the
-- thing it is read for, a node of text or an attribute.

local function blob_of(view, holder, text)
    if view.blob == nil or #text < 20 then
        return nil
    end
    if holder.blob == nil then
        holder.blob = view.blob.inspect(text, view.width) or false
    end
    return holder.blob or nil
end

local function blob_short(info)
    return info.scheme .. info.prefix .. "…"
end

-- What it holds, in parentheses, at most width wide.
local function blob_note(view, info, width)
    return "(" .. view.blob.note(info, width - 2, view.text_width) .. ")"
end

------------------------------------------------------------------------
-- What an element is made of.

local function elements_of(node)
    local list = {}
    for _, child in ipairs(node.children) do
        if child.t == "element" then
            list[#list + 1] = child
        end
    end
    return list
end

-- The text of an element that holds a text and nothing else, "" for an
-- empty one, nil for any other.
local function plain_text(node)
    if #node.children == 0 then
        return ""
    end
    if #node.children == 1 and node.children[1].t == "text" then
        return node.children[1].text
    end
    return nil
end

-- The value of an attribute as the view shows it: base64 cut short.
local function attr_value(view, attr)
    local info = blob_of(view, attr, attr.value)
    if info ~= nil then
        return blob_short(info), info
    end
    return (attr.value:gsub('"', "&quot;"))
end

local function attr_plain(view, attr)
    local value, info = attr_value(view, attr)
    local text = attr.name .. '="' .. value .. '"'
    if info ~= nil then
        text = text .. " " .. blob_note(view, info, 40)
    end
    return text
end

local function attr_text(view, attr)
    local value, info = attr_value(view, attr)
    local text = paint(view, "attr", attr.name) .. paint(view, "tag", "=")
        .. paint(view, "value", '"' .. value .. '"')
    if info ~= nil then
        text = text .. " " .. paint(view, "comment", blob_note(view, info, 40))
    end
    return text
end

------------------------------------------------------------------------
-- A table: a run of at least two elements of one name, each of them of
-- attributes, of elements of plain text and no attributes, or of a text;
-- two columns at least.

local TEXT_COLUMN = "#text"

-- The text of a cell: base64 cut short, with what it holds.
local function cell_value(view, holder, text)
    local info = blob_of(view, holder, text)
    if info ~= nil then
        return blob_short(info) .. " " .. blob_note(view, info, 30)
    end
    return text
end

local function row_fields(view, node)
    local fields = {}

    for _, a in ipairs(node.attrs) do
        fields[#fields + 1] = {
            key = "@" .. a.name, title = a.name, value = cell_value(view, a, a.value), attr = true,
        }
    end
    local text = plain_text(node)
    if text ~= nil then
        if text ~= "" then
            fields[#fields + 1] = {
                key = TEXT_COLUMN, title = TEXT_COLUMN, value = cell_value(view, node.children[1], text),
            }
        end
        return fields
    end
    for _, child in ipairs(node.children) do
        if child.t ~= "element" or #child.attrs > 0 then
            return nil
        end
        local value = plain_text(child)
        if value == nil then
            return nil
        end
        if value ~= "" then
            value = cell_value(view, child.children[1], value)
        end
        fields[#fields + 1] = { key = child.name, title = child.name, value = value }
    end
    return fields
end

-- The columns of the elements first .. last, and the fields of each, or nil.
local function table_of(view, run)
    local columns, seen, rows = {}, {}, {}
    local titles = {}

    for r, node in ipairs(run) do
        local fields = row_fields(view, node)
        if fields == nil then
            return nil
        end
        local row = {}
        for _, f in ipairs(fields) do
            if row[f.key] ~= nil then
                return nil -- the same field twice is a list, not a cell
            end
            row[f.key] = f.value
            if not seen[f.key] then
                seen[f.key] = true
                columns[#columns + 1] = f.key
                titles[f.key] = f.title
                if #columns > M.TABLE_COLUMNS then
                    return nil
                end
            end
        end
        rows[r] = row
    end
    -- one column is a list: its lines say as much without a frame
    if #columns < 2 then
        return nil
    end

    -- an attribute and an element of one name: the attribute takes its @
    local names = {}
    for _, key in ipairs(columns) do
        names[titles[key]] = (names[titles[key]] or 0) + 1
    end
    for _, key in ipairs(columns) do
        if names[titles[key]] > 1 and key:sub(1, 1) == "@" then
            titles[key] = key
        end
    end
    return columns, titles, rows
end

local function frame_line(view, indent, widths, parts)
    local pieces = {}
    for i, w in ipairs(widths) do
        pieces[i] = FRAME.h:rep(w + 2)
    end
    return indent .. paint(view, "tag", parts[1] .. table.concat(pieces, parts[2]) .. parts[3])
end

local function pad(view, text, width, right)
    local space = (" "):rep(width - view.text_width(text))
    return right and (space .. text) or (text .. space)
end

local function is_number(s)
    return s:match("^%s*[-+]?%d[%d.,]*%s*$") ~= nil
end

-- The run as a table under a line that names it, or FALSE when it is
-- wider than the view.
local function emit_table(view, run, total, indent)
    local columns, titles, rows = table_of(view, run)
    if columns == nil then
        return false
    end

    local widths = {}
    for i, key in ipairs(columns) do
        widths[i] = view.text_width(titles[key])
        for _, row in ipairs(rows) do
            if row[key] ~= nil then
                widths[i] = math.max(widths[i], view.text_width(row[key]))
            end
        end
    end
    local full = #indent + 1
    for _, w in ipairs(widths) do
        full = full + w + 3
    end
    if full > view.width then
        return false
    end

    local v = paint(view, "tag", FRAME.v)
    local name = run[1].name
    emit(view, indent .. paint(view, "comment", "<!-- " .. total .. " × <" .. name .. "> -->"), run[1].line)
    emit(view, frame_line(view, indent, widths, FRAME.top), run[1].line)
    local head = {}
    for i, key in ipairs(columns) do
        local kind = key:sub(1, 1) == "@" and "attr" or "tag"
        head[i] = " " .. paint(view, kind, pad(view, titles[key], widths[i])) .. " "
    end
    emit(view, indent .. v .. table.concat(head, v) .. v, run[1].line)
    emit(view, frame_line(view, indent, widths, FRAME.mid), run[1].line)

    for r, row in ipairs(rows) do
        local cells = {}
        for i, key in ipairs(columns) do
            local value = row[key] or ""
            local kind = key:sub(1, 1) == "@" and "value" or "text"
            cells[i] = " " .. paint(view, kind, pad(view, value, widths[i], is_number(value))) .. " "
        end
        emit(view, indent .. v .. table.concat(cells, v) .. v, run[r].line)
    end
    emit(view, frame_line(view, indent, widths, FRAME.bottom), run[#run].close or run[#run].line)
    return true
end

------------------------------------------------------------------------

local emit_node

-- The width of the tag that opens node, its attributes on its line.
local function open_width(view, node, ending)
    local plain = { "<" .. node.name }
    for i, a in ipairs(node.attrs) do
        plain[i + 1] = attr_plain(view, a)
    end
    return view.text_width(table.concat(plain, " ") .. ending)
end

-- The tag that opens node, with its attributes on its line when they fit
-- and one to a line under it when they do not; ending gives what ends it.
-- The last line of it is given back to be emitted with what follows, and
-- the line of the file it comes from when that is not the line of node.
local function emit_open(view, node, indent, ending)
    local parts = { paint(view, "tag", "<" .. node.name) }
    for i, a in ipairs(node.attrs) do
        parts[i + 1] = attr_text(view, a)
    end

    if #node.attrs <= 1 or #indent + open_width(view, node, ending) <= view.width then
        return indent .. table.concat(parts, " ") .. paint(view, "tag", ending)
    end

    emit(view, indent .. parts[1], node.line)
    local inner = indent .. (" "):rep(M.INDENT * 2)
    for i, a in ipairs(node.attrs) do
        local text = inner .. parts[i + 1]
        if i == #node.attrs then
            return text .. paint(view, "tag", ending), a.line
        end
        emit(view, text, a.line)
    end
end

-- The children of node; runs of elements of one name cut after MAX_ITEMS
-- or drawn as tables.
local function emit_children(view, list, indent)
    local i = 1

    while i <= #list do
        local node = list[i]
        local j = i

        if node.t == "element" then
            while j < #list and list[j + 1].t == "element" and list[j + 1].name == node.name do
                j = j + 1
            end
        end

        local count = j - i + 1
        if count >= 2 then
            local shown = math.min(count, M.MAX_ITEMS)
            local run = {}
            for k = 1, shown do
                run[k] = list[i + k - 1]
            end
            if not emit_table(view, run, count, indent) then
                for k = 1, shown do
                    emit_node(view, run[k], indent)
                end
            end
            if shown < count then
                emit(view, indent .. paint(view, "comment", "<!-- … "
                    .. plural(count - shown, "more <" .. node.name .. ">", "more <" .. node.name .. ">")
                    .. " -->"), list[i + shown].line)
            end
        else
            emit_node(view, node, indent)
        end
        i = j + 1
    end
end

emit_node = function(view, node, indent)
    if node.t == "text" then
        local info = blob_of(view, node, node.text)
        if info ~= nil then
            local short = blob_short(info)
            local used = #indent + view.text_width(short) + 2
            emit(view, indent .. paint(view, "text", short) .. "  "
                .. paint(view, "comment", blob_note(view, info, view.width - used)), node.line)
            return
        end
        for _, line in ipairs(wrap(view, node.text, view.width - #indent)) do
            emit(view, indent .. paint(view, "text", line), node.line)
        end
        return
    end
    if node.t ~= "element" then
        local kind = (node.t == "comment") and "comment" or "decl"
        local n = node.line
        for line in (node.raw .. "\n"):gmatch("(.-)\n") do
            emit(view, indent .. paint(view, kind, line), n)
            n = n + 1
        end
        return
    end

    local close = paint(view, "tag", "</" .. node.name .. ">")
    local text = plain_text(node)

    if text == "" then
        local line, last = emit_open(view, node, indent, "/>")
        emit(view, line, last or node.line)
        return
    end

    local info = text ~= nil and text ~= "" and blob_of(view, node.children[1], text) or nil
    if info ~= nil then
        -- base64: its first letters in the element, what it holds after it
        local open, last = emit_open(view, node, indent, ">")
        local short = blob_short(info)
        local plain_open = open_width(view, node, ">")
        local used = #indent + plain_open + view.text_width(short)
            + view.text_width("</" .. node.name .. ">") + 2
        if last == nil then
            emit(view, open .. paint(view, "text", short) .. close .. "  "
                .. paint(view, "comment", blob_note(view, info, view.width - used)), node.line)
        else
            emit(view, open, last)
            local inner = indent .. (" "):rep(M.INDENT)
            used = #inner + view.text_width(short) + 2
            emit(view, inner .. paint(view, "text", short) .. "  "
                .. paint(view, "comment", blob_note(view, info, view.width - used)), node.children[1].line)
            emit(view, indent .. close, node.close or node.line)
        end
        return
    end

    if text ~= nil then
        local whole = #indent + open_width(view, node, ">") + view.text_width(text)
            + view.text_width("</" .. node.name .. ">")
        if whole <= view.width then
            local open = emit_open(view, node, indent, ">")
            emit(view, open .. paint(view, "text", text) .. close, node.line)
            return
        end
        local open, last = emit_open(view, node, indent, ">")
        emit(view, open, last or node.line)
        local inner = indent .. (" "):rep(M.INDENT)
        for _, line in ipairs(wrap(view, text, view.width - #inner)) do
            emit(view, inner .. paint(view, "text", line), node.children[1].line)
        end
        emit(view, indent .. close, node.close or node.line)
        return
    end

    local elements = #elements_of(node)
    local count = ""
    if elements >= M.COUNT_CHILDREN then
        count = "  " .. paint(view, "comment", "<!-- " .. plural(elements, "element", "elements") .. " -->")
    end
    local open, last = emit_open(view, node, indent, ">")
    emit(view, open .. count, last or node.line)
    emit_children(view, node.children, indent .. (" "):rep(M.INDENT))
    emit(view, indent .. close, node.close or node.line)
end

------------------------------------------------------------------------

-- The view of the nodes, opts = { width, colors, text_width }:
-- { lines = { text... }, src = { line of the file... } }.
function M.view(nodes, opts)
    local view = new_view(opts or {})
    emit_children(view, nodes, "")
    return view
end

-- The line of the view, from 0, that shows the line of the file line: the
-- first of those that come from the last line at or before it.
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
    while found > 1 and view.src[found - 1] == view.src[found] do
        found = found - 1
    end
    return math.max(0, found - 1)
end

return M
