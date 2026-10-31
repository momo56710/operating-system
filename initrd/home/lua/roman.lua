--@ title: Roman
--@ about: convert numbers both ways
local map = { { 1000, "M" }, { 900, "CM" }, { 500, "D" }, { 400, "CD" }, { 100, "C" }, { 90, "XC" }, { 50, "L" }, { 40, "XL" }, { 10, "X" }, { 9, "IX" }, { 5, "V" }, { 4, "IV" }, { 1, "I" } }
local function toroman(n) local s = "" for _, p in ipairs(map) do while n >= p[1] do s = s .. p[2]; n = n - p[1] end end return s end
local function fromroman(s) local n = 0 for _, p in ipairs(map) do while s:sub(1, #p[2]) == p[2] do n = n + p[1]; s = s:sub(#p[2] + 1) end end return n end
print("type a number (1-3999) or a roman numeral")
for line in io.lines() do
  local n = tonumber(line)
  if n and n >= 1 and n <= 3999 then print(toroman(math.floor(n))) elseif line:match("^[IVXLCDM]+$") then print(fromroman(line)) else print("?") end
  io.write(""); io.flush()
end
