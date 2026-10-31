# Phase 7 (stretch): Networking

Date: 2026-10-29
Status: **complete for the first milestone** (the roadmap's "virtio-net driver, Ethernet, ARP, IP, ICMP, UDP, then a small TCP and a socket API"). Tag to apply: `v1.1-network`.

## What I did
**Driver** (`kernel/drivers/virtio_net.c`): virtio-net over the legacy I/O-port interface (what QEMU's `-device virtio-net-pci` gives). Two queues: 64 receive buffers posted in advance, one transmit buffer. Reads the MAC address, accepts no optional features. Frames are polled, not interrupt-driven.

**Stack** (`kernel/net/`)
- `net.c`: Ethernet and the network thread. One kernel thread polls the card (sleeping 1 ms when idle), dispatches frames, and runs the TCP timers every 10 ms. Static configuration for QEMU's user-mode network: 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
- `arp.c`: a 16-entry cache that learns from every ARP packet and every on-link IP packet. Requests are retried 3 times. The receive thread never waits for ARP (it asks and lets retransmission cope).
- `ip.c`: IPv4 with header and checksum validation. Sends never fragment (DF set); received fragments are dropped. Off-link traffic goes to the gateway.
- `icmp.c`: answers echo requests; sends them for `ping`.
- `udp.c`: sockets with a bound-port table and a 32-datagram receive queue each; checksums verified and generated.
- `tcp.c`: the three-way handshake, sequence and acknowledgement numbers, a 16 KiB send buffer and 16 KiB receive buffer per connection, a window advertised from the free buffer space, retransmission with exponential backoff (400 ms up to 4 s, gives up after 8 tries; on a timeout everything unacknowledged is resent), orderly close through every FIN state, TIME_WAIT, reset handling in both directions, passive open (`listen` / `accept` with a backlog), `shutdown` for half-close, 32 connections, 8 listeners.
- `sock.c`: sockets are files. `read`, `write`, `close`, `dup2` and `fork` all work on them; the stack is only torn down when the last descriptor closes.

**System calls** (`abi/abi.h`): `socket bind connect listen accept sendto recvfrom shutdown netctl`. `sendto`/`recvfrom` take a small message structure, so a receive can carry a timeout and report the sender.

**User side** (`user/lib/net.c`, `user/progs/`)
- `dns_lookup` / `resolve`: a DNS client over UDP (A records, follows past CNAMEs, retries once).
- `ifconfig` (show settings; `ifconfig dns IP` picks the DNS server), `ping`, `nslookup name [server [port]]`, `wget [-O file] http://host[:port]/path`, `nc` (client, `-u` UDP, `-l` listen), `echoserv [-u] port [count]`, `cksum` (CRC-32 as zlib does it).
- `make run` attaches QEMU's built-in user-mode network. To reach a LeOS server from the host: `make run QEMU_NET_FWD=,hostfwd=tcp::5555-:7777`.

## Results
Host servers written in Python on this machine (reached by the guest at 10.0.2.2) checked every byte.

| Test | Result |
|---|---|
| `ping` the gateway (10.0.2.2) and DNS (10.0.2.3) | replies; an address that does not exist times out cleanly |
| UDP: `echo hello udp \| nc -u 10.0.2.2 17771` | echoed back |
| DNS against a test server: A record, a CNAME followed by an A record, a name that does not exist | resolved, resolved (CNAME skipped), clean failure after one retry |
| `wget` a small file and a 300 KB random file | 78 bytes, and 300 KB with a **matching CRC-32** |
| `wget` a missing page, and a port with no server | HTTP 404 delivered, and "cannot connect" (the reset is understood) |
| **Upload** 300 KB with `nc host port < file` | the host received exactly 307,200 bytes, same CRC; the reply came back (half-close works) |
| 40 connections in a row | all succeeded; free memory and kernel heap identical before and after (no leak) |
| **Server**, host connects in through a port forward: TCP echo of 13 bytes, then 200 KB of random data sent and received at once | identical |
| `nc -l` receiving a 300 KB file from the host | matching CRC |
| UDP echo server | both datagrams answered |
| **TCP under 5% random packet loss** in each direction (`make iso EXTRA=-DNET_LOSS_TEST`) | a 300 KB download and a 300 KB upload both intact |
| **The real internet** | `nslookup example.com 1.1.1.1` resolved it, `ping 1.1.1.1` got replies (13-16 ms), `wget http://example.com/` returned HTTP 200 |
| Regressions | memory stress test, sync self-test, kernel exception tests, files, pipes, nano, processes, and a boot with no network card all still pass |

## Notes and limits
- **DNS in this environment:** QEMU's own resolver (10.0.2.3) gave no answer because this machine's `/etc/resolv.conf` has no nameserver line, so it had nothing to forward to. That is why `ifconfig dns 1.1.1.1` exists. On a machine with a working resolver the default should work (not tested).
- **Static IP only.** No DHCP: the addresses match QEMU's user-mode network. On another network they would need changing in `net_init`.
- **TCP is deliberately simple:** no congestion control (the send window is capped at 6 segments), no selective acknowledgements, out-of-order segments are dropped and recovered by retransmission, no zero-window probes, no keep-alive, no urgent data. It recovers from loss, but slowly compared with a real stack.
- No IP fragmentation or reassembly, no IPv6, no ARP expiry, DNS has no caching and no TCP fallback, `wget` is plain HTTP/1.0 only (no HTTPS, no redirects).
- Receiving is polled once per millisecond, so every round trip carries up to 1 ms of added latency.
- One NIC, virtio only (legacy interface). A real machine would need another driver (e1000 or RTL8139 for older hardware).
- Verified under QEMU's user-mode networking only; not on real hardware, and not against a bridged or tap network.
- The send path holds `net_lock` with interrupts off while the card is polled for completion. Fine for one CPU; it would need rework for SMP.
- Kernel stacks are 16 KiB. The TCP send path nests several fixed-size packet buffers, so it uses around 6-7 KiB of it.

## Try it
    make run
    ifconfig
    ping 10.0.2.2
    ifconfig dns 1.1.1.1
    nslookup example.com
    wget http://example.com/
    echoserv 7777          (and from the host, with QEMU_NET_FWD=,hostfwd=tcp::5555-:7777: nc 127.0.0.1 5555)
