--@ title: Maze
--@ about: generate and solve a maze
math.randomseed(os.time())
local W, H = 19, 9
local cell = {}
for y = 1, 2 * H + 1 do cell[y] = {} for x = 1, 2 * W + 1 do cell[y][x] = "#" end end
local function carve(x, y)
  cell[2 * y][2 * x] = " "
  local dirs = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }
  for i = #dirs, 2, -1 do local j = math.random(i); dirs[i], dirs[j] = dirs[j], dirs[i] end
  for _, d in ipairs(dirs) do
    local nx, ny = x + d[1], y + d[2]
    if nx >= 1 and nx <= W and ny >= 1 and ny <= H and cell[2 * ny][2 * nx] == "#" then cell[2 * y + d[2]][2 * x + d[1]] = " "; carve(nx, ny) end
  end
end
carve(1, 1)
local function solve(x, y, px, py)
  if x == 2 * W and y == 2 * H then cell[y][x] = "."; return true end
  for _, d in ipairs { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } } do
    local nx, ny = x + d[1], y + d[2]
    if cell[ny] and cell[ny][nx] == " " then
      cell[y][x] = "."
      if solve(nx, ny) then return true end
      cell[y][x] = " "
    end
  end
  cell[y][x] = " "
  return false
end
cell[2][2] = " "
solve(2, 2)
cell[2][2] = "S"
for y = 1, 2 * H + 1 do print(table.concat(cell[y])) end
