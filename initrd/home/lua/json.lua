--@ title: JSON
--@ about: encode and decode in Lua
local function encode(v, indent)
  local t = type(v)
  if t == "nil" then return "null" elseif t == "boolean" or t == "number" then return tostring(v)
  elseif t == "string" then return '"' .. v:gsub('[%c"\\]', function(c) return ("\\u%04x"):format(c:byte()) end) .. '"'
  elseif t == "table" then
    local parts = {}
    if #v > 0 or next(v) == nil then for _, x in ipairs(v) do parts[#parts + 1] = encode(x) end return "[" .. table.concat(parts, ",") .. "]" end
    local keys = {} for k in pairs(v) do keys[#keys + 1] = k end table.sort(keys)
    for _, k in ipairs(keys) do parts[#parts + 1] = encode(tostring(k)) .. ":" .. encode(v[k]) end
    return "{" .. table.concat(parts, ",") .. "}"
  end
end
local function decode(s)
  local pos = 1
  local function ws() pos = s:find("%S", pos) or #s + 1 end
  local value
  local function str() local e = s:find('"', pos + 1, true); local r = s:sub(pos + 1, e - 1); pos = e + 1; return r end
  function value()
    ws()
    local c = s:sub(pos, pos)
    if c == "{" then
      local t = {}; pos = pos + 1; ws()
      if s:sub(pos, pos) == "}" then pos = pos + 1 return t end
      repeat ws(); local k = str(); ws(); pos = pos + 1; t[k] = value(); ws(); local d = s:sub(pos, pos); pos = pos + 1 until d == "}"
      return t
    elseif c == "[" then
      local t = {}; pos = pos + 1; ws()
      if s:sub(pos, pos) == "]" then pos = pos + 1 return t end
      repeat t[#t + 1] = value(); ws(); local d = s:sub(pos, pos); pos = pos + 1 until d == "]"
      return t
    elseif c == '"' then return str()
    elseif s:sub(pos, pos + 3) == "true" then pos = pos + 4 return true
    elseif s:sub(pos, pos + 4) == "false" then pos = pos + 5 return false
    elseif s:sub(pos, pos + 3) == "null" then pos = pos + 4 return nil
    else local num = s:match("^-?%d+%.?%d*[eE]?[+-]?%d*", pos); pos = pos + #num; return tonumber(num) end
  end
  return value()
end
local doc = { name = "LeOS", version = 1.2, tags = { "os", "lua", "gui" }, cpu = { arch = "x86_64", bits = 64 }, ok = true }
local text = encode(doc)
print(text)
local back = decode(text)
print(back.name, back.version, #back.tags, back.cpu.arch, back.ok)
print(encode(decode('[1, 2, {"a": [true, false]}, "x"]')))
