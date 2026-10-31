--@ title: Mem stress
--@ about: allocate and free in a loop
local function free() return leos.meminfo().free_kb end
local before = free()
print("free before:", before, "KiB")
local keep = {}
for i = 1, 2000 do keep[i] = ("x"):rep(1000 + i) end
print("holding 2000 strings, free:", free(), "KiB, Lua uses", math.floor(collectgarbage("count")), "KiB")
keep = nil
collectgarbage()
print("after collect, Lua uses", math.floor(collectgarbage("count")), "KiB")
local t = os.clock()
local n = 0
for i = 1, 200000 do local x = { i, i + 1 }; n = n + #x end
print(("200000 small tables in %.2f s"):format(os.clock() - t))
print("free after:", free(), "KiB")
