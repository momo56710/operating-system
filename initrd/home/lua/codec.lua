--@ title: Encoder
--@ about: rot13, base64, hex, crc32
local b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local function base64(s)
  local out = {}
  for i = 1, #s, 3 do
    local a, b, c = s:byte(i, i + 2)
    local n = a * 65536 + (b or 0) * 256 + (c or 0)
    out[#out + 1] = b64:sub((n >> 18 & 63) + 1, (n >> 18 & 63) + 1)
    out[#out + 1] = b64:sub((n >> 12 & 63) + 1, (n >> 12 & 63) + 1)
    out[#out + 1] = b and b64:sub((n >> 6 & 63) + 1, (n >> 6 & 63) + 1) or "="
    out[#out + 1] = c and b64:sub((n & 63) + 1, (n & 63) + 1) or "="
  end
  return table.concat(out)
end
local function crc32(s)
  local crc = 0xFFFFFFFF
  for i = 1, #s do
    crc = crc ~ s:byte(i)
    for _ = 1, 8 do crc = (crc >> 1) ~ (0xEDB88320 & -(crc & 1)) end
  end
  return ~crc & 0xFFFFFFFF
end
local function rot13(s) return (s:gsub("%a", function(c) local base = c:match("%l") and 97 or 65; return string.char((c:byte() - base + 13) % 26 + base) end)) end
print("type text, one line at a time")
for line in io.lines() do
  print("rot13   " .. rot13(line))
  print("base64  " .. base64(line))
  print("hex     " .. line:gsub(".", function(c) return ("%02x"):format(c:byte()) end))
  print(("crc32   %08x"):format(crc32(line)))
  io.write("\n"); io.flush()
end
