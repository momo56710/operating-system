--@ title: Todo list
--@ about: a tiny task manager
local path = (leos.exists("/disk/bin") and "/disk" or "/tmp") .. "/todo.txt"
local items = {}
for line in io.lines(path) do items[#items + 1] = line end
local function save() local f = assert(io.open(path, "w")) for _, l in ipairs(items) do f:write(l, "\n") end f:close() end
print("commands: add TEXT | done N | list | quit      (saved in " .. path .. ")")
local function list() if #items == 0 then print("(nothing to do)") end for i, l in ipairs(items) do print(i .. ". " .. l) end end
list()
while true do
  io.write("todo> "); io.flush()
  local line = io.read("l")
  if not line or line == "quit" then break end
  local cmd, rest = line:match("^(%S+)%s*(.*)$")
  if cmd == "add" and rest ~= "" then items[#items + 1] = rest; save(); list()
  elseif cmd == "done" and items[tonumber(rest)] then table.remove(items, tonumber(rest)); save(); list()
  elseif cmd == "list" then list()
  else print("?") end
end
