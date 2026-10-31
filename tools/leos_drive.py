#!/usr/bin/env python3
"""Drive a LeOS virtual machine from a script: type lines through the QEMU monitor and capture the serial log.
usage (from the project root, after make iso): tools/leos_drive.py LOG SETTLE_SECONDS [--disk IMG] -- line1 line2 ...
Special lines: <CTRL-D>  <KEY:k1,k2>  <TYPE:text>  <WAIT:s>  <SHOT:file>  <UNTIL:text:secs>  <RUN:host command>  <MON:qemu monitor command>
Environment: LEOS_DISK (virtio|ahci), LEOS_NIC (virtio|e1000), NET_FWD (extra -netdev options), PCAP (packet capture file), QEMU_EXTRA (extra qemu arguments)."""
import subprocess, sys, time, os
args = sys.argv[1:]
log, settle = args[0], float(args[1]); args = args[2:]
disk = None
while args and args[0] != "--":
    if args[0] == "--disk": disk = args[1]; args = args[2:]
    else: break
lines = args[1:] if args and args[0] == "--" else args
KEYS = {" ": "spc", "/": "slash", ".": "dot", "-": "minus", "_": "shift-minus", "|": "shift-backslash",
        ">": "shift-dot", "<": "shift-comma", '"': "shift-apostrophe", "'": "apostrophe", ":": "shift-semicolon",
        ";": "semicolon", "$": "shift-4", "=": "equal", "+": "shift-equal", "*": "shift-8", ",": "comma",
        "!": "shift-1", "?": "shift-slash", "(": "shift-9", ")": "shift-0", "#": "shift-3", "%": "shift-5",
        "&": "shift-7", "\\": "backslash", "[": "bracket_left", "]": "bracket_right", "~": "shift-grave_accent",
        "@": "shift-2", "^": "shift-6", "`": "grave_accent"}
def key(c):
    if c in KEYS: return KEYS[c]
    if c.isupper(): return "shift-" + c.lower()
    return c
cmd = ["qemu-system-x86_64", "-cpu", "max", "-cdrom", "os.iso", "-serial", "file:" + log, "-display", "none", "-vga", "std",
       "-no-reboot", "-boot", "order=d", "-m", "512M", "-monitor", "stdio"]
disk_kind = os.environ.get("LEOS_DISK", "virtio")      # virtio, ahci
nic_kind = os.environ.get("LEOS_NIC", "virtio")        # virtio, e1000
if disk and disk_kind == "ahci": cmd += ["-drive", f"file={disk},if=none,id=ahd,format=raw", "-device", "ich9-ahci,id=ahci", "-device", "ide-hd,drive=ahd,bus=ahci.0"]
elif disk: cmd += ["-drive", f"file={disk},if=virtio,format=raw"]
nic_dev = {"virtio": "virtio-net-pci", "e1000": "e1000"}[nic_kind]
cmd += ["-netdev", "user,id=net0" + os.environ.get("NET_FWD", ""), "-device", nic_dev + ",netdev=net0"]
if os.environ.get("PCAP"): cmd += ["-object", "filter-dump,id=f1,netdev=net0,file=" + os.environ["PCAP"]]
if os.environ.get("QEMU_EXTRA"): cmd += os.environ["QEMU_EXTRA"].split()
if os.path.exists(log): os.remove(log)
p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True)
def send(s): p.stdin.write(s + "\n"); p.stdin.flush()
time.sleep(8)
for line in lines:
    if line.startswith("<WAIT:"): time.sleep(float(line[6:-1])); continue
    if line == "<CTRL-D>": send("sendkey ctrl-d"); time.sleep(settle); continue
    if line.startswith("<KEY:"):
        for k in line[5:-1].split(","): send("sendkey " + k); time.sleep(0.25)
        time.sleep(settle); continue
    if line.startswith("<TYPE:"):
        for ch in line[6:-1]: send("sendkey " + key(ch)); time.sleep(0.09)
        time.sleep(settle); continue
    if line.startswith("<UNTIL:"):
        pat, secs = line[7:-1].rsplit(":", 1); end = time.time() + float(secs); found = False
        while time.time() < end:
            if pat in open(log, errors="replace").read().replace(chr(13), ""): found = True; break
            time.sleep(0.3)
        print(("saw: " if found else "TIMED OUT waiting for: ") + pat, flush=True); continue
    if line.startswith("<RUN:"):
        r = subprocess.run(line[5:-1], shell=True, capture_output=True, text=True, timeout=120)
        print(r.stdout + r.stderr, end="", flush=True); continue
    if line.startswith("<MON:"): send(line[5:-1]); time.sleep(0.3); continue
    if line.startswith("<SHOT:"): send("screendump " + line[6:-1]); time.sleep(1.2); continue
    for ch in line: send("sendkey " + key(ch)); time.sleep(0.09)
    send("sendkey ret"); time.sleep(settle)
time.sleep(1)
send("quit")
try: p.wait(timeout=20)
except Exception: p.kill()
