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
QEMUFLAGS  := -cdrom os.iso -serial stdio -no-reboot -no-shutdown -m 512M

CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -ffreestanding -fno-stack-protector \
          -fno-pic -fno-pie -fno-lto -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=kernel -m64
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T linker.ld

SRCS := $(shell find kernel -name '*.c')
OBJS := $(SRCS:%.c=build/%.o)

.PHONY: all iso run debug trace clean
all: kernel.elf

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

iso: os.iso
os.iso: kernel.elf limine.conf
	rm -rf iso_root && mkdir -p iso_root/boot/limine iso_root/EFI/BOOT
	cp kernel.elf iso_root/boot/kernel.elf
	cp limine.conf iso_root/boot/limine/limine.conf
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
	$(QEMU) $(QEMUFLAGS) -s -S &
	gdb kernel.elf -ex 'target remote :1234'

trace: os.iso
	$(QEMU) $(QEMUFLAGS) -d int,cpu_reset -D qemu.log

clean:
	rm -rf build iso_root kernel.elf os.iso qemu.log
