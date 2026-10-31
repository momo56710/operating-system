--@ title: Dice roller
--@ about: roll NdM dice
--@ args: Dice, e.g. 3d6 (blank = 2d6)
math.randomseed(os.time() + leos.uptime())
local spec = arg[1] or "2d6"
local n, m = spec:match("^(%d+)d(%d+)$")
n, m = tonumber(n), tonumber(m)
if not n then print("use NdM, for example 3d6") os.exit(1) end
local sum, rolls = 0, {}
for i = 1, n do local r = math.random(1, m); rolls[i] = r; sum = sum + r end
print(table.concat(rolls, " + ") .. " = " .. sum)
