--@ title: Spiral
--@ about: a number spiral in text
--@ args: Size (blank = 7)
local n = tonumber(arg[1]) or 7
local g = {} for i = 1, n do g[i] = {} end
local x, y, dx, dy = 1, 1, 1, 0
for v = 1, n * n do
  g[y][x] = v
  local nx, ny = x + dx, y + dy
  if nx < 1 or nx > n or ny < 1 or ny > n or g[ny][nx] then dx, dy = -dy, dx; nx, ny = x + dx, y + dy end
  x, y = nx, ny
end
for i = 1, n do local r = {} for j = 1, n do r[j] = ("%4d"):format(g[i][j]) end print(table.concat(r)) end
