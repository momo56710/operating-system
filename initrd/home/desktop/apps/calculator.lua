--@ title: Calculator
--@ about: click the keys or type; powers, brackets, math functions
local ui = ...
local C, text, tw, region = ui.C, ui.text, ui.tw, ui.region

local KEYS = {
  { "C", "(", ")", "<" }, { "7", "8", "9", "/" }, { "4", "5", "6", "*" },
  { "1", "2", "3", "-" }, { "0", ".", "^", "+" },
}
local env = setmetatable({}, { __index = function(_, k) if math[k] ~= nil then return math[k] end end })

local function press(win, k)
  if k == "C" then win.expr, win.result = "", nil
  elseif k == "<" then win.expr = win.expr:sub(1, -2)
  elseif k == "=" then
    local f = load("return " .. win.expr, "=calc", "t", env)
    local ok, v = false, nil
    if f then ok, v = pcall(f) end
    if ok and type(v) == "number" then
      win.result = (v == math.floor(v) and math.abs(v) < 1e15) and string.format("%d", v) or string.format("%.10g", v)
      win.hist[#win.hist + 1] = win.expr .. " = " .. win.result
      if #win.hist > 4 then table.remove(win.hist, 1) end
      win.expr = win.result
    else win.result = "error" end
  else win.expr = win.expr .. k; win.result = nil end
end

return {
  title = "calculator", w = 340, h = 470,
  init = function(win) win.expr, win.result, win.hist = "", nil, {} end,
  draw = function(win, x, y, w, h)
    gfx.frame(x + 12, y + 12, w - 24, 76, C.line, 10)
    local shown = win.expr
    text(x + 24, y + 22, shown:sub(-((w - 48) // 8)), C.fg)
    text(x + 24, y + 54, win.result and ("= " .. win.result) or "", ui.accent(), 1)
    local bw, bh = (w - 24 - 3 * 8) // 4, 48
    for r, row in ipairs(KEYS) do
      for c, k in ipairs(row) do
        local bx, by = x + 12 + (c - 1) * (bw + 8), y + 100 + (r - 1) * (bh + 8)
        local key = "calc" .. k
        gfx.frame(bx, by, bw, bh, C.line, 10)
        text(bx + bw // 2 - 4, by + bh // 2 - 8, k, tonumber(k) and C.fg or ui.accent())
        region(bx, by, bw, bh, function() press(win, k) end, key)
      end
    end
    local by = y + 100 + 5 * (bh + 8)
    gfx.round(x + 12, by, w - 24, bh, 10, ui.accent())
    text(x + w // 2 - 4, by + bh // 2 - 8, "=", C.bg)
    region(x + 12, by, w - 24, bh, function() press(win, "=") end, "calc=")
  end,
  key = function(win, ev)
    if not ev.down then return end
    if ev.code == 0x1C then press(win, "=")
    elseif ev.code == 0x0E then press(win, "<")
    elseif ev.ch and ev.ch:match("[%d%.%+%-%*/%^%(%)%%a-z, ]") then
      if ev.ch == "c" and win.expr == "" then return end
      win.expr = win.expr .. ev.ch; win.result = nil
    end
  end,
}
