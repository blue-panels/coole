-- What a string of base64 holds, told short enough for a line of a view:
-- the start of the text it holds, or the kind of file when the bytes are
-- of one that is known, and the size.  Only the start is decoded, as much
-- as a line can show, so a value of megabytes costs no more than a pass of
-- a pattern over it.
--
-- A string is taken for base64 when it is long enough, its length without
-- white space is a multiple of four, it holds nothing but the letters of
-- base64, and what it holds reads as text or starts like a known kind of
-- file: a hash or an identifier that happens to be of those letters
-- decodes to bytes of no meaning, and is left alone.  A data: URI with
-- ;base64, is taken by its prefix.

local M = {}

M.MIN_LENGTH = 20
-- A longer string is looked at by its start and its end, this much of each:
-- a pattern goes over a megabyte in tens of milliseconds, and the view is
-- made again after every change of the file.
M.SAMPLE = 8192

local ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local VALUE = {}
for i = 1, #ALPHABET do
    VALUE[ALPHABET:byte(i)] = i - 1
end

-- The kinds of file known by their first bytes.
local MAGIC = {
    { "\137PNG\r\n\26\n", "PNG image" },
    { "\255\216\255", "JPEG image" },
    { "GIF87a", "GIF image" },
    { "GIF89a", "GIF image" },
    { "%PDF-", "PDF" },
    { "PK\3\4", "ZIP archive" },
    { "\31\139", "gzip" },
    { "\0asm", "WebAssembly" },
    { "\127ELF", "ELF" },
}

-- The bytes of b64, which is whole quads of the letters, padding last.
local function decode(b64)
    local out = {}
    for i = 1, #b64 - 3, 4 do
        local a, b, c, d = b64:byte(i, i + 3)
        local va, vb, vc, vd = VALUE[a], VALUE[b], VALUE[c], VALUE[d]
        if va == nil or vb == nil then
            break
        end
        local n = (va << 18) | (vb << 12) | ((vc or 0) << 6) | (vd or 0)
        out[#out + 1] = string.char((n >> 16) & 255)
        if vc ~= nil then
            out[#out + 1] = string.char((n >> 8) & 255)
        end
        if vd ~= nil then
            out[#out + 1] = string.char(n & 255)
        end
    end
    return table.concat(out)
end

local function size_text(n)
    if n < 1024 then
        return string.format("%d B", n)
    elseif n < 1024 * 1024 then
        return string.format("%.1f KB", n / 1024)
    end
    return string.format("%.1f MB", n / (1024 * 1024))
end

-- The start of bytes as text on one line, or nil when they are no text.
local function as_text(bytes)
    -- a character cut at the end of what was decoded is no fault of the text
    for _ = 1, 3 do
        if utf8.len(bytes) ~= nil then
            break
        end
        bytes = bytes:sub(1, -2)
    end
    if utf8.len(bytes) == nil or bytes:find("[%z\1-\8\11\12\14-\31\127]") then
        return nil
    end
    local line = bytes:gsub("%s+", " "):gsub("^ ", "")
    if line == "" then
        return nil
    end
    return line
end

-- What s holds when it is base64: { scheme = "data:...;base64," or "",
-- prefix = the first letters of it,
-- what = "base64" or the kind of file or the type of a data: URI,
-- size = "2.1 KB", text = the start of the text it holds or nil,
-- more = whether the text goes on }; nil when it is not taken for base64.
-- want is how many characters of the text are wanted at most.
function M.inspect(s, want)
    if type(s) ~= "string" or #s < M.MIN_LENGTH then
        return nil
    end

    local what = "base64"
    local payload = s
    local mime, rest = s:match("^data:([%w%.%+%-/]*)[^,]-;base64,()")
    if rest ~= nil then
        payload = s:sub(rest)
        if mime ~= "" then
            what = mime:gsub("^[%a]+/", "")
        end
    end

    -- the letters of base64, white space between them, padding last
    local whole = #payload <= 2 * M.SAMPLE
    local sample = whole and payload or (payload:sub(1, M.SAMPLE) .. payload:sub(-M.SAMPLE))
    if sample:find("[^%w%+/=%s]") then
        return nil
    end
    local eq = sample:find("=", 1, true)
    if eq ~= nil and sample:find("[^=%s]", eq) then
        return nil
    end

    local spaced = sample:find(" ", 1, true) or sample:find("\n", 1, true)
        or sample:find("\r", 1, true) or sample:find("\t", 1, true)
    local letters = #payload
    local about = ""
    if spaced then
        local _, spaces = sample:gsub("%s", "")
        if whole then
            letters = letters - spaces
        else
            -- wrapped lines: as many letters to a byte as in the sample
            letters = math.floor(#payload * (1 - spaces / #sample) / 4 + 0.5) * 4
            about = "≈"
        end
    end
    if letters < M.MIN_LENGTH - (rest ~= nil and 16 or 0) or letters % 4 ~= 0 then
        return nil
    end
    local pad = #(payload:sub(-8):match("(=*)%s*$"))
    local size = letters // 4 * 3 - pad

    -- the start of it: as many quads as the text wanted takes
    want = math.max(want or 60, 16)
    local quads = (want + 4) // 3 + 1
    local head = payload:sub(1, quads * 4 * 2):gsub("%s", ""):sub(1, quads * 4)
    head = head:sub(1, #head - #head % 4)
    local bytes = decode(head)
    local cut = #bytes < size

    local info = {
        scheme = rest ~= nil and s:sub(1, rest - 1) or "",
        prefix = head:sub(1, 12),
        what = what,
        size = about .. size_text(size),
    }
    for _, m in ipairs(MAGIC) do
        if bytes:sub(1, #m[1]) == m[1] then
            info.what = m[2]
            return info
        end
    end

    local text = as_text(bytes:sub(1, want + 3))
    if text == nil then
        -- bytes of no known kind: a data: URI says what it is, anything else is no base64 of ours
        if rest ~= nil then
            return info
        end
        return nil
    end
    info.text, info.more = text, cut or #bytes > want
    return info
end

-- The text of info for a view, at most width wide: "what, size: text…".
function M.note(info, width, text_width)
    text_width = text_width or function(t) return utf8.len(t) or #t end
    local head = info.what .. ", " .. info.size
    if info.text == nil then
        return head
    end

    local room = width - text_width(head) - 2
    if room < 4 then
        return head
    end
    local text = info.text
    local more = info.more
    if text_width(text) > room - (more and 1 or 0) then
        local n = 0
        local out = {}
        for _, code in utf8.codes(text) do
            local ch = utf8.char(code)
            n = n + text_width(ch)
            if n > room - 1 then
                break
            end
            out[#out + 1] = ch
        end
        text = table.concat(out)
        more = true
    end
    return head .. ": " .. text .. (more and "…" or "")
end

return M
