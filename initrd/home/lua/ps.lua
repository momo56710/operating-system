--@ title: Processes
--@ about: who is running
print("PID PPID STATE    KIND   NAME")
for _, t in ipairs(leos.ps()) do print(("%3d %4d %-8s %-6s %s"):format(t.pid, t.ppid, t.state, t.kind, t.name)) end
