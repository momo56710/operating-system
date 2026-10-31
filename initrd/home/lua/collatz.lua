--@ title: Collatz
--@ about: longest 3n+1 chain below N
--@ args: Limit (blank = 30000)
local limit = tonumber(arg[1]) or 30000
local best, bestn, cache = 0, 1, { [1] = 1 }
for i = 2, limit do
  local n, steps = i, 0
  while not cache[n] do n = n % 2 == 0 and n // 2 or 3 * n + 1; steps = steps + 1 end
  cache[i] = steps + cache[n]
  if cache[i] > best then best, bestn = cache[i], i end
end
print(("longest chain below %d: starts at %d, %d steps"):format(limit, bestn, best))
