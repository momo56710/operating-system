--@ title: Sort race
--@ about: three sorting algorithms
--@ args: Items (blank = 2000)
local n = tonumber(arg[1]) or 2000
local function data() math.randomseed(42); local t = {} for i = 1, n do t[i] = math.random(1, 100000) end return t end
local function bubble(t) for i = 1, #t do for j = 1, #t - i do if t[j] > t[j + 1] then t[j], t[j + 1] = t[j + 1], t[j] end end end end
local function insertion(t) for i = 2, #t do local v, j = t[i], i - 1; while j > 0 and t[j] > v do t[j + 1] = t[j]; j = j - 1 end t[j + 1] = v end end
local function quick(t, lo, hi)
  lo, hi = lo or 1, hi or #t
  if lo >= hi then return end
  local p, i = t[(lo + hi) // 2], lo
  local j = hi
  while i <= j do
    while t[i] < p do i = i + 1 end
    while t[j] > p do j = j - 1 end
    if i <= j then t[i], t[j] = t[j], t[i]; i = i + 1; j = j - 1 end
  end
  quick(t, lo, j); quick(t, i, hi)
end
for _, a in ipairs { { "bubble", bubble }, { "insertion", insertion }, { "quicksort", quick }, { "table.sort", function(t) table.sort(t) end } } do
  local t = data()
  local t0 = os.clock()
  a[2](t)
  local ok = true
  for i = 2, #t do if t[i - 1] > t[i] then ok = false end end
  print(("%-10s %7.3f s  %s"):format(a[1], os.clock() - t0, ok and "sorted" or "WRONG"))
end
