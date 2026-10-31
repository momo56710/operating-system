# Uses a real cross compiler when installed, otherwise the host gcc in
# freestanding mode (fine for a 64-bit Linux host).
ifneq ($(shell command -v x86_64-elf-gcc 2>/dev/null),)
CC := x86_64-elf-gcc
LD := x86_64-elf-ld
else
CC := gcc
LD := ld
endif

LIMINE_DIR ?= /usr/share/limine
QEMU       := qemu-system-x86_64
comma := ,
# QEMU's built-in user-mode network. To reach a LeOS server from this machine add port forwards, e.g.
#   make run QEMU_NET_FWD=,hostfwd=tcp::5555-:7777
QEMU_NET_FWD :=
# a disk image, if there is one, is attached as a virtio disk
QEMUFLAGS  := -cpu max -cdrom os.iso -boot order=d -serial stdio -m 512M -display gtk -netdev user,id=net0$(QEMU_NET_FWD) -device virtio-net-pci,netdev=net0 $(if $(wildcard disk.img),-drive file=disk.img$(comma)if=virtio$(comma)format=raw)
# debug and trace freeze on a reset instead of rebooting, so a triple fault stays visible
CRASHFLAGS := -no-reboot -no-shutdown

CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -fno-lto -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=kernel -m64 -Ikernel/include -Iabi -MMD -MP
CFLAGS += $(EXTRA)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker.ld

SRCS    := $(shell find kernel arch -name '*.c')
ASMSRCS := $(shell find arch -name '*.asm')
OBJS    := $(SRCS:%.c=build/%.o) $(ASMSRCS:%.asm=build/%.asm.o)
-include $(OBJS:.o=.d)

# ---- user programs: separate ELF files (the kernel saves their FPU/SSE state on every context switch) ----
USER_PROGS := sh nano mv lua fpdemo libctest ifconfig ping nslookup wget nc echoserv cksum ls cat echo cp rm mkdir touch wc grep head ps mem sleep uptime \
              hello args crash forktest umem count primes fib sortdemo tree life mandel bench gfxtest
USER_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
               -mno-red-zone -m64 -Iuser/include -Iabi -MMD -MP
USER_LIB_SRCS := $(wildcard user/lib/*.c)
USER_LIB_ASM  := $(wildcard user/lib/*.S)
USER_LIB_OBJS := $(USER_LIB_SRCS:%.c=build/%.o) $(USER_LIB_ASM:%.S=build/%.o)
# Programs that live only on the disk image (/disk/bin), not on the ramdisk
DISK_PROGS := greet
USER_ELFS := $(USER_PROGS:%=build/user/%.elf) $(DISK_PROGS:%=build/user/%.elf)

# The ramdisk: a tar of /bin (the programs) plus whatever is under initrd/, loaded by Limine as a module.
INITRD_FILES := $(shell find initrd -type f)
build/initrd.tar: $(USER_ELFS) $(INITRD_FILES)
	rm -rf build/initrd && mkdir -p build/initrd/bin build/initrd/tmp build/initrd/dev build/initrd/disk
	cp -r initrd/. build/initrd/
	$(foreach p,$(USER_PROGS),cp build/user/$(p).elf build/initrd/bin/$(p);)
	tar --format=ustar --owner=0 --group=0 --numeric-owner -cf $@ -C build/initrd .

# the C library must not have its loops turned back into calls to itself
build/user/lib/%.o: USER_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns

build/user/lib/%.o: user/lib/%.S
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%.elf: build/user/progs/%.o $(USER_LIB_OBJS) user/user.ld
	$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld $(USER_LIB_OBJS) $< -o $@

# ---- Lua: third_party/lua, compiled unmodified against the LeOS C library ----
LUA_DIR    := third_party/lua/src
# our own linit.c (same libraries plus "leos") replaces Lua's; leoslib.c is the LeOS library
LUA_SRCS   := $(filter-out $(LUA_DIR)/luac.c $(LUA_DIR)/linit.c,$(wildcard $(LUA_DIR)/*.c))
LUA_OBJS   := $(LUA_SRCS:%.c=build/%.o) build/user/lua/linit.o build/user/lua/leoslib.o build/user/lua/gfxlib.o build/user/lua/font8x16.o
LUA_CFLAGS := -std=gnu99 -O2 -w -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -m64 \
              -Iuser/include -Iabi -I$(LUA_DIR) -DLUA_ROOT='"/disk/"'

build/user/lua/%.o: user/lua/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUA_CFLAGS) -c $< -o $@

build/user/lua/font8x16.o: kernel/drivers/font8x16.c
	@mkdir -p $(dir $@)
	$(CC) $(LUA_CFLAGS) -c $< -o $@

build/third_party/lua/src/%.o: third_party/lua/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LUA_CFLAGS) -c $< -o $@

build/user/lua.elf: $(LUA_OBJS) $(USER_LIB_OBJS) user/user.ld
	$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld $(USER_LIB_OBJS) $(LUA_OBJS) -o $@

-include $(wildcard build/user/*/*.d)

.PHONY: all iso run debug trace clean programs disk disk-reset
programs: $(USER_ELFS)
all: kernel.elf

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	nasm -f elf64 -g -F dwarf $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

iso: os.iso
os.iso: kernel.elf limine.conf assets/splash.png build/initrd.tar
	rm -rf iso_root && mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp kernel.elf iso_root/boot/kernel.elf
	cp limine.conf assets/splash.png iso_root/boot/limine/
	cp build/initrd.tar iso_root/boot/initrd.tar
	echo '    module_path: boot():/boot/initrd.tar' >> iso_root/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    iso_root -o os.iso
	limine bios-install os.iso

# disk.img is built only if missing, so what you save on it survives rebuilds; `make disk-reset` starts over
DISK_FILES := $(shell find diskfiles -type f)
disk.img:
	rm -rf build/diskroot && mkdir -p build/diskroot/bin && cp -r diskfiles/. build/diskroot/
	$(foreach p,$(DISK_PROGS),cp build/user/$(p).elf build/diskroot/bin/$(p);)
	python3 tools/mkfat32.py disk.img 64 build/diskroot

disk: $(USER_ELFS) disk.img

disk-reset: $(USER_ELFS)
	rm -f disk.img
	$(MAKE) disk.img

run: os.iso disk.img
	$(QEMU) $(QEMUFLAGS)

debug: os.iso
	$(QEMU) $(QEMUFLAGS) $(CRASHFLAGS) -s -S &
	gdb kernel.elf -ex 'target remote :1234'

trace: os.iso
	$(QEMU) $(QEMUFLAGS) $(CRASHFLAGS) -d int,cpu_reset -D qemu.log

clean:
	rm -rf build iso_root kernel.elf os.iso qemu.log
