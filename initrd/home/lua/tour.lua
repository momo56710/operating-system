--@ title: Lua tour
--@ about: a quick tour of the language
-- A quick tour of Lua running on LeOS.  Try:  lua /home/lua/tour.lua
print(_VERSION .. " on LeOS, up " .. string.format("%.1f", os.clock()) .. " s")

-- numbers: 64-bit integers and doubles
print(7 // 2, 7 / 2, 7 % 3, 2 ^ 10, 1 << 40, 0xFF & 0x0F, math.maxinteger, math.mininteger)
print(0.1 + 0.2, 1e15, 2^53, math.pi, -0.0, 1/0, -1/0, math.tointeger(3.0))
print(string.format("%5.2f | %-8s| %05d | %x | %g | %q", math.pi, "left", 42, 255, 1e20, "a\"b"))

-- strings and patterns
local text = "the quick brown fox jumps over the lazy dog"
print(text:upper(), #text, text:find("quick"), text:rep(2, " / "):sub(1, 30))
for word in text:gmatch("%a+") do io.write(word:sub(1, 1):upper(), word:sub(2), " ") end
print()
print(("x=%d, y=%d"):format(3, 4), ("hello"):reverse(), ("  trim me  "):match("^%s*(.-)%s*$"))

-- tables, closures, metatables
local function counter() local n = 0 return function() n = n + 1 return n end end
local c = counter(); c(); c()
print("counter:", c())
local Vec = {}
Vec.__index = Vec
Vec.__add = function(a, b) return setmetatable({ x = a.x + b.x, y = a.y + b.y }, Vec) end
Vec.__tostring = function(v) return "(" .. v.x .. ", " .. v.y .. ")" end
print(setmetatable({ x = 1, y = 2 }, Vec) + setmetatable({ x = 10, y = 20 }, Vec))

local t = { 5, 2, 8, 1, 9, 3 }
table.sort(t, function(a, b) return a > b end)
print(table.concat(t, ", "), #t, select("#", table.unpack(t)))

-- coroutines
local gen = coroutine.wrap(function() for i = 1, 5 do coroutine.yield(i * i) end end)
io.write("squares:")
for v in gen do io.write(" ", v) end
print()

-- errors
print(pcall(function() error({ code = 42 }) end))
print(select(2, pcall(function() return nil + 1 end)))
print(xpcall(function() error("boom") end, function(m) return "handled: " .. m end))
