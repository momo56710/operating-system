--@ title: Uptime
--@ about: how long has LeOS been up
local ms = leos.uptime()
print(("up %d min %d s (%d ms)"):format(ms // 60000, ms // 1000 % 60, ms))
local n = leos.netinfo()
print(n and ("network %s via %s"):format(n.ip, n.gateway) or "no network")
