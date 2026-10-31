--@ title: Minesweeper
--@ about: click to dig, toggle the flag mode to mark mines
local ui = ...
local C, text, region = ui.C, ui.text, ui.region
local CW, CH, CELL, MINES = 14, 10, 32, 18

local function reset(win)
  win.cells, win.over, win.won, win.flag, win.placed = {}, false, false, false, false
  for i = 1, CW * CH do win.cells[i] = { mine = false, open = false, flag = false, n = 0 } end
end

local function neighbours(i)
  local x, y = (i - 1) % CW, (i - 1) // CW
  local out = {}
  for dy = -1, 1 do for dx = -1, 1 do
    if (dx ~= 0 or dy ~= 0) and x + dx >= 0 and x + dx < CW and y + dy >= 0 and y + dy < CH then out[#out + 1] = (y + dy) * CW + x + dx + 1 end
  end end
  return out
end

local function place(win, safe)
  local n = 0
  while n < MINES do
    local i = math.random(1, CW * CH)
    if i ~= safe and not win.cells[i].mine then win.cells[i].mine = true; n = n + 1 end
  end
  for i = 1, CW * CH do
    local c = 0
    for _, j in ipairs(neighbours(i)) do if win.cells[j].mine then c = c + 1 end end
    win.cells[i].n = c
  end
  win.placed = true
end

local function dig(win, i)
  local c = win.cells[i]
  if c.open or c.flag then return end
  c.open = true
  if c.mine then win.over = true return end
  if c.n == 0 then for _, j in ipairs(neighbours(i)) do dig(win, j) end end
end

local function check(win)
  local closed = 0
  for _, c in ipairs(win.cells) do if not c.open then closed = closed + 1 end end
  if closed == MINES and not win.over then win.won = true end
end

return {
  title = "minesweeper", w = CW * CELL + 24, h = CH * CELL + 130,
  init = function(win) math.randomseed(os.time() + ui.now()); reset(win) end,
  draw = function(win, x, y, w, h)
    local flags = 0
    for _, c in ipairs(win.cells) do if c.flag then flags = flags + 1 end end
    text(x + 12, y + 14, "mines left " .. (MINES - flags), C.fg)
    local msg = win.over and "boom!" or win.won and "cleared!" or ""
    text(x + w // 2 - ui.tw(msg) // 2, y + 14, msg, ui.accent())
    local labels = { win.flag and "flag: on" or "flag: off", "new" }
    local bx = x + w - 12
    for i = 2, 1, -1 do
      local bw = ui.tw(labels[i]) + 20
      bx = bx - bw
      gfx.frame(bx, y + 10, bw, 24, (i == 1 and win.flag) and ui.accent() or C.line, 8)
      text(bx + 10, y + 14, labels[i], C.fg)
      region(bx, y + 10, bw, 24, function() if i == 1 then win.flag = not win.flag else reset(win) end end, "mine" .. i)
      bx = bx - 8
    end
    local ox, oy = x + 12, y + 48
    for i, c in ipairs(win.cells) do
      local cx, cy = ox + ((i - 1) % CW) * CELL, oy + ((i - 1) // CW) * CELL
      if c.open then
        gfx.frame(cx + 1, cy + 1, CELL - 2, CELL - 2, C.faint, 4)
        if c.mine then ui.circle(cx + CELL // 2, cy + CELL // 2, 7, ui.accent(), true)
        elseif c.n > 0 then text(cx + CELL // 2 - 4, cy + CELL // 2 - 8, tostring(c.n), c.n >= 3 and ui.accent() or C.fg) end
      else
        gfx.frame(cx + 1, cy + 1, CELL - 2, CELL - 2, C.line, 4)
        if c.flag then text(cx + CELL // 2 - 4, cy + CELL // 2 - 8, "P", ui.accent())
        elseif win.over and c.mine then ui.circle(cx + CELL // 2, cy + CELL // 2, 5, C.mute, true) end
      end
    end
    win.ox, win.oy = 12, 48
  end,
  click = function(win, rx, ry)
    if win.over or win.won then return end
    local cx, cy = (rx - 12) // CELL, (ry - 48) // CELL
    if cx < 0 or cx >= CW or cy < 0 or cy >= CH then return end
    local i = cy * CW + cx + 1
    if win.flag then if not win.cells[i].open then win.cells[i].flag = not win.cells[i].flag end return end
    if not win.placed then place(win, i) end
    dig(win, i)
    check(win)
  end,
}
