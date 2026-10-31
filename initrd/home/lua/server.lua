--@ title: Web server
--@ about: a tiny HTTP server in Lua
--@ args: Port and requests (blank = 8080 3)
-- A tiny web server written in Lua.  lua /home/lua/server.lua [port] [requests]
-- From the host, with QEMU_NET_FWD=,hostfwd=tcp::8080-:8080 :  curl http://127.0.0.1:8080/
local port, limit = tonumber(arg[1]) or 8080, tonumber(arg[2]) or 3
local server = assert(leos.listen(port))
print("listening on port " .. port .. " for " .. limit .. " request(s)")
local hits = 0
for _ = 1, limit do
  local c, ip, p = server:accept()
  local request = c:recv(2048, 3000) or ""
  hits = hits + 1
  local path = request:match("^%u+ (%S+)") or "/"
  local m = leos.meminfo()
  local body = ("<h1>Hello from Lua on LeOS</h1>\n<p>You asked for <b>%s</b> from %s:%d.</p>\n<p>Request #%d. Up %.1f s, %d KiB free.</p>\n")
      :format(path, ip, p, hits, m.uptime_ms / 1000, m.free_kb)
  c:send("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nContent-Length: " .. #body .. "\r\nConnection: close\r\n\r\n" .. body)
  c:close()
  print(("served %s to %s:%d"):format(path, ip, p))
end
server:close()
