-- A web client in 15 lines of Lua.  lua /home/lua/http.lua http://example.com/
local url = arg[1] or "http://example.com/"
local host, port, path = url:match("^http://([^:/]+):?(%d*)(/?.*)$")
if not host then print("usage: http.lua http://host[:port]/path") os.exit(1) end
port = tonumber(port) or 80
if path == "" then path = "/" end

local ip, err = leos.resolve(host)
if not ip then print("cannot resolve " .. host .. ": " .. tostring(err)) os.exit(1) end
print(("connecting to %s (%s):%d"):format(host, ip, port))
local s = assert(leos.tcp(ip, port))
s:send(("GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n"):format(path, host))
local reply = s:recvall()
s:close()

local head, body = reply:match("^(.-)\r\n\r\n(.*)$")
print(head:match("^[^\r\n]+"), #body .. " bytes")
print(body:sub(1, 600))
