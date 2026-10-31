--@ title: Mandelbrot
--@ about: the set, drawn in text
local w, h = 78, 28
for py = 0, h - 1 do
  local row = {}
  for px = 0, w - 1 do
    local x0, y0 = px / w * 3.5 - 2.5, py / h * 2 - 1
    local x, y, i = 0, 0, 0
    while x * x + y * y < 4 and i < 60 do x, y = x * x - y * y + x0, 2 * x * y + y0; i = i + 1 end
    row[#row + 1] = (" .:-=+*#%@"):sub(i // 6 + 1, i // 6 + 1)
  end
  print(table.concat(row))
end
