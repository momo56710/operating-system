--@ title: Hex dump
--@ about: look inside a file
--@ args: File (blank = /etc/motd)
local f = assert(io.open(arg[1] or "/etc/motd", "rb"))
local off = 0
while true do
  local chunk = f:read(16)
  if not chunk then break end
  local hex, asc = {}, {}
  for i = 1, 16 do local b = chunk:byte(i); hex[i] = b and ("%02x"):format(b) or "  "; asc[i] = b and (b >= 32 and b < 127 and string.char(b) or ".") or " " end
  print(("%06x  %s  %s"):format(off, table.concat(hex, " "), table.concat(asc)))
  off = off + 16
  if off >= 512 then print("...") break end
end
f:close()
