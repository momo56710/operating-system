--@ title: Snake
--@ about: arrow keys or WASD, eat the dots
local ui = ...
local C, text = ui.C, ui.text
local GW, GH, CELL = 24, 16, 20

local function reset(win)
  win.snake = { { 12, 8 }, { 11, 8 }, { 10, 8 } }
  win.dir, win.next = { 1, 0 }, { 1, 0 }
  win.score, win.dead, win.last, win.waiting = 0, false, 0, true
  win.food = nil
end

local function place_food(win)
  while true do
    local fx, fy = math.random(1, GW), math.random(1, GH)
    local free = true
    for _, s in ipairs(win.snake) do if s[1] == fx and s[2] == fy then free = false break end end
    if free then win.food = { fx, fy } return end
  end
end

return {
  title = "snake", w = GW * CELL + 24, h = GH * CELL + 90, speed = 110,
  init = function(win) math.randomseed(os.time() + ui.now()); reset(win); place_food(win) end,
  tick = function(win, t)
    if not win.focused then win.last = t return false end          -- paused while another window is in front
    if win.waiting or win.dead or t - win.last < 110 then return false end
    win.last = t
    win.dir = win.next
    local head = win.snake[1]
    local nx, ny = head[1] + win.dir[1], head[2] + win.dir[2]
    if nx < 1 or nx > GW or ny < 1 or ny > GH then win.dead = true return true end
    for i = 1, #win.snake - 1 do local s = win.snake[i]; if s[1] == nx and s[2] == ny then win.dead = true return true end end
    table.insert(win.snake, 1, { nx, ny })
    if win.food and nx == win.food[1] and ny == win.food[2] then win.score = win.score + 1; place_food(win)
    else table.remove(win.snake) end
    return true
  end,
  draw = function(win, x, y, w, h)
    text(x + 12, y + 10, "score " .. win.score, C.fg)
    text(x + w - 12 - 8 * 26, y + 10, win.dead and "game over: press Enter" or win.waiting and "press an arrow key" or "arrows / WASD", win.dead and ui.accent() or C.mute)
    local ox, oy = x + 12, y + 40
    gfx.frame(ox - 2, oy - 2, GW * CELL + 4, GH * CELL + 4, C.line, 4)
    for i, s in ipairs(win.snake) do
      gfx.round(ox + (s[1] - 1) * CELL + 1, oy + (s[2] - 1) * CELL + 1, CELL - 2, CELL - 2, 5, i == 1 and ui.accent() or C.fg)
    end
    if win.food then ui.circle(ox + (win.food[1] - 1) * CELL + CELL // 2, oy + (win.food[2] - 1) * CELL + CELL // 2, 6, C.dim, true) end
  end,
  key = function(win, ev)
    if not ev.down then return end
    local c, ch = ev.code, ev.ch
    if win.waiting and (c == 0x148 or c == 0x150 or c == 0x14B or c == 0x14D or ch == "w" or ch == "a" or ch == "s" or ch == "d") then win.waiting = false end
    local d = win.dir
    if (c == 0x148 or ch == "w") and d[2] == 0 then win.next = { 0, -1 }
    elseif (c == 0x150 or ch == "s") and d[2] == 0 then win.next = { 0, 1 }
    elseif (c == 0x14B or ch == "a") and d[1] == 0 then win.next = { -1, 0 }
    elseif (c == 0x14D or ch == "d") and d[1] == 0 then win.next = { 1, 0 }
    elseif (c == 0x1C or ch == " ") and win.dead then reset(win); place_food(win) end
  end,
}
