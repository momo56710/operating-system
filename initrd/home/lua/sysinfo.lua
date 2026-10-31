-- What is the machine doing?  lua /home/lua/sysinfo.lua
local m = leos.meminfo()
print(("memory: %d MiB total, %d MiB free, kernel heap %d KiB, up %.1f s"):format(m.total_kb // 1024, m.free_kb // 1024, m.heap_used // 1024, m.uptime_ms / 1000))
local n = leos.netinfo()
if n then print(("network: %s  %s gw %s dns %s  (rx %d frames, tx %d)"):format(n.mac, n.ip, n.gateway, n.dns, n.rx_frames, n.tx_frames)) end
print("processes:")
for _, p in ipairs(leos.ps()) do print(("  %3d  ppid %-3d %-9s %-6s %s"):format(p.pid, p.ppid, p.state, p.kind, p.name)) end
print("cwd " .. leos.getcwd() .. ", contents of /:")
for _, e in ipairs(leos.dir("/")) do io.write(e.name, e.type == "dir" and "/" or "", "  ") end
print()
print("ping gateway: " .. tostring(leos.ping(n and n.gateway or "10.0.2.2")) .. " ms")
