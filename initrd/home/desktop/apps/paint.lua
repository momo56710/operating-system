--@ title: Paint
--@ about: pixel-art canvas with a palette
local ui = ...
local C, text, region = ui.C, ui.text, ui.region
local CW, CH, CELL = 40, 24, 14
local PALETTE = { 0x000000, 0xffffff, 0x9ca3af, 0xef4444, 0xfbbf24, 0x4ade80, 0x22d3ee, 0x3b82f6, 0xf472b6, 0x8b5cf6 }

return {
  title = "paint", w = CW * CELL + 24, h = CH * CELL + 130,
  init = function(win) win.px, win.color, win.brush = {}, 2, 1 end,
  draw = function(win, x, y, w, h)
    for i, col in ipairs(PALETTE) do
      local bx = x + 12 + (i - 1) * 34
      gfx.round(bx, y + 10, 28, 28, 6, col)
      gfx.frame(bx, y + 10, 28, 28, win.color == i and C.fg or C.mute, 6)
      if win.color == i then gfx.frame(bx - 3, y + 7, 34, 34, C.fg, 8) end
      region(bx, y + 10, 28, 28, function() win.color = i end, "pal" .. i)
    end
    local bx = x + 12 + #PALETTE * 34 + 10
    for _, b in ipairs({ { "brush " .. win.brush, function() win.brush = win.brush % 3 + 1 end }, { "clear", function() win.px = {} end } }) do
      local bw = ui.tw(b[1]) + 20
      gfx.frame(bx, y + 12, bw, 24, C.line, 8)
      text(bx + 10, y + 16, b[1], C.fg)
      region(bx, y + 12, bw, 24, b[2], "pb" .. b[1]:sub(1, 3))
      bx = bx + bw + 8
    end
    local ox, oy = x + 12, y + 52
    gfx.rect(ox, oy, CW * CELL, CH * CELL, 0x000000)
    for idx, col in pairs(win.px) do
      local cx, cy = (idx - 1) % CW, (idx - 1) // CW
      gfx.rect(ox + cx * CELL, oy + cy * CELL, CELL, CELL, PALETTE[col])
    end
    gfx.frame(ox - 1, oy - 1, CW * CELL + 2, CH * CELL + 2, C.line, 0)
    win.ox, win.oy = 12, 52
  end,
  mouse = function(win, rx, ry, down)
    if not down then win.last = nil return end
    local cx, cy = (rx - 12) // CELL, (ry - 52) // CELL
    local fx, fy = win.last and win.last[1] or cx, win.last and win.last[2] or cy
    local steps = math.max(math.abs(cx - fx), math.abs(cy - fy), 1)
    for i = 0, steps do                                      -- join this point to the previous one
      local px0, py0 = fx + (cx - fx) * i // steps, fy + (cy - fy) * i // steps
      for dy = 0, win.brush - 1 do for dx = 0, win.brush - 1 do
        local px, py = px0 + dx, py0 + dy
        if px >= 0 and px < CW and py >= 0 and py < CH then win.px[py * CW + px + 1] = win.color ~= 1 and win.color or nil end
      end end
    end
    win.last = { cx, cy }
  end,
}
