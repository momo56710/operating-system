--@ title: Word count
--@ about: count lines, words, bytes
--@ args: File (blank = /etc/motd)
local path = arg[1] or "/etc/motd"
local f = assert(io.open(path, "r"))
local text = f:read("a"); f:close()
local lines = select(2, text:gsub("\n", "\n"))
local words = select(2, text:gsub("%S+", ""))
print(("%d lines, %d words, %d bytes  %s"):format(lines, words, #text, path))
local freq = {}
for w in text:lower():gmatch("%a+") do freq[w] = (freq[w] or 0) + 1 end
local list = {}
for w, c in pairs(freq) do list[#list + 1] = { w, c } end
table.sort(list, function(a, b) return a[2] > b[2] or (a[2] == b[2] and a[1] < b[1]) end)
for i = 1, math.min(5, #list) do print(("  %-12s %d"):format(list[i][1], list[i][2])) end
