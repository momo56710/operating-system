--@ title: Statistics
--@ about: mean, median, deviation
--@ args: Numbers (blank = a demo set)
local xs = {}
for i = 1, #arg do xs[#xs + 1] = tonumber(arg[i]) end
if #xs == 0 then xs = { 4, 8, 15, 16, 23, 42, 7, 7, 9 } end
table.sort(xs)
local sum = 0 for _, x in ipairs(xs) do sum = sum + x end
local mean = sum / #xs
local var = 0 for _, x in ipairs(xs) do var = var + (x - mean) ^ 2 end
local n = #xs
local median = n % 2 == 1 and xs[(n + 1) // 2] or (xs[n // 2] + xs[n // 2 + 1]) / 2
print("sorted:", table.concat(xs, " "))
print(("count %d  min %g  max %g  mean %.3f  median %g  stddev %.3f"):format(n, xs[1], xs[n], mean, median, math.sqrt(var / n)))
