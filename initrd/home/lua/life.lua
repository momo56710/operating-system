--@ title: Life
--@ about: Conway cellular automaton
--@ args: Generations (blank = 10)
-- Conway's Game of Life.  lua /home/lua/life.lua [generations]
local W, H, gens = 40, 14, tonumber(arg[1]) or 10
local grid = {}
for y = 1, H do grid[y] = {} for x = 1, W do grid[y][x] = 0 end end
for _, p in ipairs { {2,1},{3,2},{1,3},{2,3},{3,3}, {20,6},{21,6},{22,6}, {30,10},{31,10},{30,11},{31,11} } do grid[p[2]][p[1]] = 1 end
local function show(g, n)
  local rows = { ("generation %d"):format(n) }
  for y = 1, H do
    local r = {}
    for x = 1, W do r[x] = g[y][x] == 1 and "#" or "." end
    rows[#rows + 1] = table.concat(r)
  end
  print(table.concat(rows, "\n"))
end
for n = 0, gens do
  show(grid, n)
  local nxt = {}
  for y = 1, H do
    nxt[y] = {}
    for x = 1, W do
      local c = 0
      for dy = -1, 1 do for dx = -1, 1 do
        if dx ~= 0 or dy ~= 0 then c = c + grid[(y + dy - 1) % H + 1][(x + dx - 1) % W + 1] end
      end end
      nxt[y][x] = (c == 3 or (c == 2 and grid[y][x] == 1)) and 1 or 0
    end
  end
  grid = nxt
end
