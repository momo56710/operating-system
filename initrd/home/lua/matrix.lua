--@ title: Matrices
--@ about: multiply and invert
local function mul(a, b) local n, m, p = #a, #b, #b[1]; local c = {} for i = 1, n do c[i] = {} for j = 1, p do local s = 0 for k = 1, m do s = s + a[i][k] * b[k][j] end c[i][j] = s end end return c end
local function show(m) for _, r in ipairs(m) do local t = {} for j, v in ipairs(r) do t[j] = ("%8.3f"):format(v) end print(table.concat(t)) end end
local function det(m) local n = #m if n == 1 then return m[1][1] end local d = 0 for j = 1, n do local sub = {} for i = 2, n do local r = {} for k = 1, n do if k ~= j then r[#r + 1] = m[i][k] end end sub[#sub + 1] = r end d = d + (j % 2 == 1 and 1 or -1) * m[1][j] * det(sub) end return d end
local A = { { 2, 1, 0 }, { 1, 3, 1 }, { 0, 1, 4 } }
local B = { { 1, 0, 2 }, { 0, 1, 0 }, { 3, 0, 1 } }
print("A x B"); show(mul(A, B))
print("det(A) =", det(A))
local n = 60
local big = {} for i = 1, n do big[i] = {} for j = 1, n do big[i][j] = (i * j) % 7 end end
local t = os.clock(); mul(big, big)
print(("a %dx%d product took %.3f s"):format(n, n, os.clock() - t))
