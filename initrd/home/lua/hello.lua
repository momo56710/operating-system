--@ title: Hello
--@ about: say hello and show the system
--@ args: Your name (optional)
local name = arg[1] or "world"
print("Hello, " .. name .. "!")
print(_VERSION .. " on LeOS, process " .. leos.getpid())
local m = leos.meminfo()
print(("%d MiB of memory, up %.1f seconds"):format(m.total_kb // 1024, m.uptime_ms / 1000))
print("today is " .. os.date("!%A %d %B %Y, %H:%M UTC"))
