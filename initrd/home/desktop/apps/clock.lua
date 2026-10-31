--@ title: Clock
--@ about: analog clock and a stopwatch
local ui = ...
local C, text, region = ui.C, ui.text, ui.region

return {
  title = "clock", w = 360, h = 520,
  init = function(win) win.sw, win.running, win.start, win.last = 0, false, 0, -1 end,
  tick = function(win, t)
    local s = os.time()
    local changed = s ~= win.last or win.running
    win.last = s
    if win.running then win.sw = t - win.start end
    return changed
  end,
  draw = function(win, x, y, w, h)
    local cx, cy, r = x + w // 2, y + 140, 110
    ui.circle(cx, cy, r, C.line, false)
    ui.circle(cx, cy, r - 2, C.faint, false)
    for i = 0, 59 do
      local a = i * math.pi / 30
      local len = i % 5 == 0 and 10 or 4
      gfx.line(cx + math.floor(math.sin(a) * (r - 6) + .5), cy - math.floor(math.cos(a) * (r - 6) + .5),
               cx + math.floor(math.sin(a) * (r - 6 - len) + .5), cy - math.floor(math.cos(a) * (r - 6 - len) + .5), i % 5 == 0 and C.fg or C.mute)
    end
    local t = os.time() + math.floor(0)
    local tz = tonumber(ui.clock():match("^(%d+)")) or 0
    local hh, mm = ui.clock():match("(%d+):(%d+)")
    hh, mm = tonumber(hh) % 12, tonumber(mm)
    local ss = t % 60
    local function hand(frac, len, col)
      local a = frac * 2 * math.pi
      gfx.line(cx, cy, cx + math.floor(math.sin(a) * len + .5), cy - math.floor(math.cos(a) * len + .5), col)
    end
    hand((hh + mm / 60) / 12, 55, C.fg); hand(mm / 60, 80, C.fg); hand(ss / 60, 95, ui.accent())
    ui.circle(cx, cy, 4, ui.accent(), true)
    local digital = ui.clock()
    text(cx - ui.tw(digital, 3) // 2, y + 276, digital, C.fg, 3)
    text(cx - 8 * 12, y + 326, os.date("!%A %d %B %Y", t + 3600), C.dim)
    gfx.rect(x + 20, y + 360, w - 40, 1, C.faint)
    local ms = win.sw
    local s = string.format("%02d:%02d.%d", ms // 60000, ms // 1000 % 60, ms // 100 % 10)
    text(cx - ui.tw(s, 2) // 2, y + 380, s, win.running and ui.accent() or C.fg, 2)
    local bx = cx - 100
    for i, b in ipairs({ { win.running and "stop" or "start", function() if win.running then win.running = false else win.running = true; win.start = ui.now() - win.sw end end },
                         { "reset", function() win.sw = 0; win.start = ui.now() end } }) do
      gfx.frame(bx + (i - 1) * 108, y + 424, 92, 30, C.line, 10)
      text(bx + (i - 1) * 108 + 46 - ui.tw(b[1]) // 2, y + 430, b[1], C.fg)
      region(bx + (i - 1) * 108, y + 424, 92, 30, b[2], "sw" .. i)
    end
  end,
}
