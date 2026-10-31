# AHCI disk and Intel e1000 network drivers

Date: 2026-10-31
Status: **complete in QEMU; first VirtualBox boot confirmed by the user** (before these drivers: shell, Lua and the ramdisk worked; no disk, no network).

## Why
LeOS first only understood QEMU's *virtio* devices. VirtualBox and VMware present a **SATA (AHCI) disk** and an **Intel e1000 network card** instead, and so do most real PCs. Without drivers for those, a VirtualBox boot had no `/disk` and no network.

## What I did
- `kernel/drivers/ahci.c`: AHCI SATA driver. Finds the controller by PCI class, enables AHCI mode, finds the first attached SATA disk, sets up the command list, received-FIS area and command table, issues ATA IDENTIFY (LBA48 capacity), then READ/WRITE DMA EXT in requests of up to 8 sectors through a bounce buffer. One command slot, polled.
- `kernel/drivers/e1000.c`: Intel 8254x driver (a list of about 30 device IDs: 82540EM, the QEMU/VirtualBox default, and relatives). Resets the card, takes the MAC from the receive-address registers (or the EEPROM), sets up a 32-entry receive ring and an 8-entry transmit ring in RAM, polled.
- `blkdev.c` and `nic.c`: thin layers that pick whichever driver finds hardware (virtio first, then AHCI / e1000). The rest of the kernel (block cache, FAT32, network stack) only sees `blkdev_*` and `nic_*`.
- PCI now scans every bus, records the programming interface, finds devices by class, and enables bus mastering; `vmm_map_mmio` is a shared helper to map device registers.
- `tools/leos_drive.py` takes `LEOS_DISK=ahci` and `LEOS_NIC=e1000` to test the new hardware.

## Results (QEMU, scripted)
- **AHCI + e1000, no virtio at all:** read and write the disk (programs run from it, files saved), `ping`, `wget`.
- **Transfers verified by checksum:** a 300 KB download over e1000, the same file uploaded to a host server, copied onto the AHCI disk, then read back after a **reboot**: all CRC-32 `fa3ffd6a`, identical to the host's. A 28-file directory and a 43 KB program copied to the disk survived the reboot, and the program still ran.
- **Offline filesystem check** of the image the AHCI driver wrote: consistent (no cross-linked or lost clusters).
- All four combinations of {virtio, AHCI} disk and {virtio, e1000} network boot and work, including on the `q35` chipset.

## VirtualBox setup that matches
- Type "Other / Unknown (64-bit)", 512 MB or more, **Enable I/O APIC** (System).
- Network: NAT, adapter type **Intel PRO/1000 MT Desktop**. VirtualBox's NAT uses 10.0.2.0/24 with the gateway at 10.0.2.2, which is exactly what the kernel assumes.
- Storage: ISO in the optical drive, boot order optical first; for a disk, add a **SATA controller** and attach a VDI made from the raw image: `VBoxManage convertfromraw disk.img disk.vdi --format VDI`.

## Limits
- Tested against QEMU's emulation of these devices. VirtualBox and real hardware have their own quirks (extra SATA ports, real link-up delays, controllers in non-AHCI mode).
- AHCI: first SATA disk only, no port multipliers, no ATAPI (CD) drives, no NCQ, no interrupts. e1000: no interrupts, no offloads, a 2 KB frame limit; newer "e1000e" chips (82574 and later) are not supported.
- Other hardware is still untested: NVMe and USB disks, Wi-Fi, other network cards.

## Mistake worth recording
While testing UEFI boot earlier I attached the real `disk.img` instead of a copy, and the UEFI firmware wrote a settings file (`NvVars`) onto it. It was the only change (the filesystem stayed consistent); I deleted exactly that file with LeOS's own `rm` and re-verified the image.
