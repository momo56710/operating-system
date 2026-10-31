--@ title: Lua calc
--@ about: type Lua expressions, one per line
-- A calculator: type Lua expressions, one per line (Ctrl-D to stop).  lua /home/lua/calc.lua
print("Type an expression, e.g.  2^10 + math.sqrt(16)   or   string.rep('ab', 3)")
local env = setmetatable({}, { __index = function(_, k) if math[k] ~= nil then return math[k] end return _G[k] end })   -- math.* first, then everything else
for line in io.lines() do
  local chunk, err = load("return " .. line, "=input", "t", env)
  if not chunk then chunk, err = load(line, "=input", "t", env) end
  if chunk then
    local ok, res = pcall(chunk)
    print(ok and tostring(res) or ("error: " .. tostring(res)))
  else
    print("syntax error: " .. err)
  end
  io.write("> "); io.flush()
end
