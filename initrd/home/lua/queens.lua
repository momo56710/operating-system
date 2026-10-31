--@ title: N-Queens
--@ about: place N queens on a board
--@ args: Board size (blank = 8)
local N = tonumber(arg[1]) or 8
local cols, count, first = {}, 0, nil
local function ok(r, c) for i = 1, r - 1 do local d = cols[i]; if d == c or math.abs(d - c) == r - i then return false end end return true end
local function place(r)
  if r > N then count = count + 1; if not first then first = { table.unpack(cols) } end return end
  for c = 1, N do if ok(r, c) then cols[r] = c; place(r + 1) end end
end
place(1)
print(N .. " queens: " .. count .. " solutions. The first one:")
for r = 1, N do local row = {} for c = 1, N do row[c] = first[r] == c and "Q" or "." end print(table.concat(row, " ")) end
