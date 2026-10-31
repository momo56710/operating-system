-- A small terminal emulator: turns the bytes a program prints (text, \r, \b, \t, a few ANSI escape
-- sequences) into lines of text the desktop can draw. Used by the terminal and by the script runner.
local emu = {}
emu.__index = emu

local MAXLINES = 800

function emu.new(cols)
  return setmetatable({ cols = cols or 80, lines = { "" }, row = 1, col = 1, top = 1, state = 0, params = "", wrapped = false }, emu)
end

function emu:clear()
  self.lines = { "" }; self.row, self.col, self.top, self.state, self.params = 1, 1, 1, 0, ""
end

local function ensure(self, row) while #self.lines < row do self.lines[#self.lines + 1] = "" end end

function emu:newline()
  self.row = self.row + 1
  ensure(self, self.row)
  self.col = 1
  if #self.lines > MAXLINES then
    table.remove(self.lines, 1)
    self.row = self.row - 1
    self.top = math.max(1, self.top - 1)
  end
end

function emu:put(ch)
  if self.col > self.cols then self:newline() end
  local line = self.lines[self.row]
  if #line < self.col - 1 then line = line .. (" "):rep(self.col - 1 - #line) end
  self.lines[self.row] = line:sub(1, self.col - 1) .. ch .. line:sub(self.col + 1)
  self.col = self.col + 1
end

local function csi(self, final)
  local p = {}
  for n in self.params:gmatch("%d+") do p[#p + 1] = tonumber(n) end
  local a, b = p[1], p[2]
  if final == "H" or final == "f" then
    self.row = self.top + (a or 1) - 1
    ensure(self, self.row)
    self.col = b or 1
  elseif final == "A" then self.row = math.max(self.top, self.row - (a or 1))
  elseif final == "B" then self.row = self.row + (a or 1); ensure(self, self.row)
  elseif final == "C" then self.col = self.col + (a or 1)
  elseif final == "D" then self.col = math.max(1, self.col - (a or 1))
  elseif final == "G" then self.col = a or 1
  elseif final == "J" then
    if (a or 0) == 2 or a == 3 then
      -- the old screen scrolls into history; the next screenful starts on a fresh line
      if self.lines[#self.lines] ~= "" then self.lines[#self.lines + 1] = "" end
      self.top = #self.lines; self.row = self.top; self.col = 1
    elseif (a or 0) == 0 then
      self.lines[self.row] = self.lines[self.row]:sub(1, self.col - 1)
      for r = self.row + 1, #self.lines do self.lines[r] = "" end
    end
  elseif final == "K" then
    local k = a or 0
    local l = self.lines[self.row]
    if k == 0 then self.lines[self.row] = l:sub(1, self.col - 1)
    elseif k == 2 then self.lines[self.row] = "" end
  end
  -- colours (m), cursor visibility (?25l/h) and everything else are ignored
end

function emu:feed(s)
  for i = 1, #s do
    local c = s:sub(i, i)
    local b = c:byte()
    if self.state == 1 then
      if c == "[" then self.state = 2; self.params = "" else self.state = 0 end
    elseif self.state == 2 then
      if b >= 0x40 and b <= 0x7e then self.state = 0; csi(self, c) else self.params = self.params .. c end
    elseif c == "\n" then self:newline()
    elseif c == "\r" then self.col = 1
    elseif c == "\b" then self.col = math.max(1, self.col - 1)
    elseif c == "\t" then self.col = ((self.col - 1) // 8 + 1) * 8 + 1
    elseif c == "\27" then self.state = 1
    elseif b == 7 or b == 0 then -- bell, nul: ignore
    else self:put(c) end
  end
end

return emu
