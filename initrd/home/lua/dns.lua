--@ title: DNS lookup
--@ about: look host names up
--@ args: Host names (blank for the demo)
-- Look names up.  lua /home/lua/dns.lua name [name ...]      (set a server first: ifconfig dns 1.1.1.1)
for _, name in ipairs(#arg > 0 and arg or { "example.com", "lua.org" }) do
  print(name, leos.resolve(name) or "not found")
end
