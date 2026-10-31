--@ title: Factors
--@ about: split a number into primes
--@ args: Number (blank = 360360)
local n = math.tointeger(tonumber(arg[1]) or 360360)
local orig, out, p = n, {}, 2
while p * p <= n do
  while n % p == 0 do out[#out + 1] = p; n = n // p end
  p = p + (p == 2 and 1 or 2)
end
if n > 1 then out[#out + 1] = n end
print(orig .. " = " .. table.concat(out, " x "))
