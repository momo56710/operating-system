--@ title: Ping sweep
--@ about: ping the neighbours
--@ args: Hosts (blank = gateway, DNS, example.com)
local hosts = #arg > 0 and arg or { "10.0.2.2", "10.0.2.3", "example.com", "1.1.1.1" }
for _, h in ipairs(hosts) do
  local ms = leos.ping(h, 1500)
  print(("%-16s %s"):format(h, ms and (ms .. " ms") or "no reply"))
end
