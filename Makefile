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
QEMUFLAGS  := -cdrom os.iso -serial stdio -m 512M -display gtk
# debug and trace freeze on a reset instead of rebooting, so a triple fault stays visible
CRASHFLAGS := -no-reboot -no-shutdown

CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -fno-lto -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=kernel -m64 -Ikernel/include -MMD -MP
CFLAGS += $(EXTRA)
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker.ld

SRCS    := $(shell find kernel arch -name '*.c')
ASMSRCS := $(shell find arch -name '*.asm')
OBJS    := $(SRCS:%.c=build/%.o) $(ASMSRCS:%.asm=build/%.asm.o)
-include $(OBJS:.o=.d)

# ---- user programs: separate ELF files, no SSE (the kernel does not save FPU/SSE state yet) ----
USER_PROGS := hello args crash forktest umem
USER_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
               -mno-sse -mno-sse2 -mno-mmx -mno-red-zone -m64 -Iuser/include -MMD -MP
USER_LIB_SRCS := $(wildcard user/lib/*.c)
USER_LIB_OBJS := $(USER_LIB_SRCS:%.c=build/%.o) build/user/lib/crt0.o
USER_ELFS := $(USER_PROGS:%=build/user/%.elf)

build/user/lib/crt0.o: user/lib/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%.o: user/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%.elf: build/user/progs/%.o $(USER_LIB_OBJS) user/user.ld
	$(LD) -nostdlib -static -z max-page-size=0x1000 -T user/user.ld $(USER_LIB_OBJS) $< -o $@

-include $(wildcard build/user/*/*.d)

.PHONY: all iso run debug trace clean programs
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
os.iso: kernel.elf limine.conf assets/splash.png $(USER_ELFS)
	rm -rf iso_root && mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp kernel.elf iso_root/boot/kernel.elf
	cp limine.conf assets/splash.png iso_root/boot/limine/
	mkdir -p iso_root/boot/user && cp $(USER_ELFS) iso_root/boot/user/
	$(foreach p,$(USER_PROGS),echo '    module_path: boot():/boot/user/$(p).elf' >> iso_root/boot/limine/limine.conf;)
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    iso_root -o os.iso
	limine bios-install os.iso

run: os.iso
	$(QEMU) $(QEMUFLAGS)

debug: os.iso
	$(QEMU) $(QEMUFLAGS) $(CRASHFLAGS) -s -S &
	gdb kernel.elf -ex 'target remote :1234'

trace: os.iso
	$(QEMU) $(QEMUFLAGS) $(CRASHFLAGS) -d int,cpu_reset -D qemu.log

clean:
	rm -rf build iso_root kernel.elf os.iso qemu.log
