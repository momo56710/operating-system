-- Sieve of Eratosthenes.  lua /home/lua/primes.lua [limit]
local limit = tonumber(arg[1]) or 100000
local t0 = os.clock()
local composite, count, last = {}, 0, 0
for i = 2, limit do
  if not composite[i] then
    count, last = count + 1, i
    for j = i * i, limit, i do composite[j] = true end
  end
end
print(string.format("%d primes up to %d (largest %d), %.2f s", count, limit, last, os.clock() - t0))
