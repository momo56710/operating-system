--@ title: Fibonacci
--@ about: numbers, exact and fast
--@ args: How many? (blank = 40)
local n = tonumber(arg[1]) or 40
local a, b = 0, 1
for i = 1, n do io.write(a, i < n and ", " or "\n"); a, b = b, a + b end
local memo = {}
local function fib(k) if k < 2 then return k end if memo[k] then return memo[k] end local v = fib(k - 1) + fib(k - 2); memo[k] = v; return v end
print("fib(80) =", fib(80))
local t = os.clock()
local function slow(k) return k < 2 and k or slow(k - 1) + slow(k - 2) end
print("slow recursive fib(27) =", slow(27), ("(%.2f s)"):format(os.clock() - t))
