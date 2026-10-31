-- Files and the clock.  lua /home/lua/files.lua [directory]    (default /tmp)
local dir = arg[1] or "/tmp"
local path = dir .. "/lua_demo.txt"
local f = assert(io.open(path, "w"))
for i = 1, 5 do f:write(string.format("line %d: %s\n", i, os.date("%H:%M:%S", os.time() + i))) end
f:close()

print("wrote " .. path)
for line in io.lines(path) do print("  " .. line) end

local f = assert(io.open(path, "r"))
local all = f:read("a")
print(#all .. " bytes, " .. select(2, all:gsub("\n", "")) .. " lines; seek to end: " .. f:seek("end"))
f:close()
f = io.open(path, "a"); f:write("appended\n"); f:close()
local n = 0
for _ in io.lines(path) do n = n + 1 end
print("now " .. n .. " lines")
print(os.remove(path), io.open(path))
print(os.date("%A %d %B %Y, %H:%M:%S UTC"))
