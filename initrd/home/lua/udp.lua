--@ title: UDP echo
--@ about: send a datagram, wait for the reply
--@ args: Host port text (blank for defaults)
-- UDP: send a datagram to an echo server and wait for the answer.  lua /home/lua/udp.lua host port [text]
local host, port, text = arg[1] or "10.0.2.2", tonumber(arg[2]) or 17771, arg[3] or "hello from lua"
local s = assert(leos.udp())
s:sendto(host, port, text)
local reply, ip, p = s:recvfrom(2000)
print(reply and ("reply from %s:%d: %s"):format(ip, p, reply) or "no reply")
