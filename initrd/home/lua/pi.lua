--@ title: Pi digits
--@ about: compute digits of pi
--@ args: Digits (blank = 100)
local n = tonumber(arg[1]) or 100
local len = n * 10 // 3 + 1
local a = {}
for i = 1, len do a[i] = 2 end
local out, nines, pre = {}, 0, 0
for j = 1, n do
  local q = 0
  for i = len, 1, -1 do
    local x = 10 * a[i] + q * i
    a[i] = x % (2 * i - 1)
    q = x // (2 * i - 1)
  end
  a[1] = q % 10; q = q // 10
  if q == 9 then nines = nines + 1
  elseif q == 10 then out[#out + 1] = pre + 1; for _ = 1, nines do out[#out + 1] = 0 end pre, nines = 0, 0
  else if j > 1 then out[#out + 1] = pre end pre = q; for _ = 1, nines do out[#out + 1] = 9 end nines = 0 end
end
out[#out + 1] = pre
local s = table.concat(out)
print(s:sub(1, 1) .. "." .. s:sub(2, n))
