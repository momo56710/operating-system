#include "limine.h"
#include "serial.h"
#include "fb.h"
#include "kprintf.h"
#include "panic.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "keyboard.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "memtest.h"
#include "acpi.h"
#include "apic.h"
#include "task.h"
#include "spinlock.h"
#include "shell.h"
#include "modules.h"
#include "syscall.h"
#include "vfs.h"
#include "process.h"
#include "blk.h"
#include "pci.h"
#include "net.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(5);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request fb_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request exec_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID, .revision = 0
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

/* ---- kernel threads ---- */

/* The first process: gives the console to stdin/stdout/stderr, then keeps a shell running. */
static void init_thread(void *arg) {
    (void)arg;
    struct task *me = current_task();
    for (int i = 0; i < 3; i++) {
        struct file *c = file_open("/dev/console", i == 0 ? O_RDONLY : O_WRONLY);
        if (!c) panic("init: cannot open /dev/console");
        me->fds[i] = c;
    }
    for (;;) {
        char *argv[] = { "sh", 0 };
        long pid = process_spawn("/bin/sh", argv);
        if (pid < 0) { kprintf("init: cannot start /bin/sh\n"); sleep_ms(2000); continue; }
        int status;
        process_wait(&status);
        kprintf("\ninit: the shell exited (status %d), starting it again\n", status);
    }
}


#ifdef THREAD_DEMO
struct letter { char ch; unsigned period_ms; };
static struct letter letters[3] = { {'A', 300}, {'B', 500}, {'C', 700} };
static volatile uint64_t letter_count[3], spin_count;

static void letter_thread(void *arg) {
    struct letter *l = arg;
    for (;;) {
        kputc(l->ch);
        letter_count[l->ch - 'A']++;
        sleep_ms(l->period_ms);
    }
}

/* Never sleeps or yields: if the other threads keep printing while this one spins,
 * the timer really is preempting it. */
static void busy_thread(void *arg) {
    (void)arg;
    for (;;) {
        uint64_t until = uptime_ms() + 1000;
        while (uptime_ms() < until) spin_count++;
        kputc('*');
    }
}

static void monitor_thread(void *arg) {
    (void)arg;
    for (;;) {
        sleep_ms(30000);
        uint64_t span, used, fr;
        heap_stats(&span, &used, &fr);
        kprintf("\n[monitor] up %lus  A=%lu B=%lu C=%lu  spin=%lu  free frames=%lu  heap used=%lu B\n",
                uptime_ms() / 1000, letter_count[0], letter_count[1], letter_count[2],
                spin_count, pmm_free_frames(), used);
    }
}
#endif

void kmain(void) {
    serial_init();

    if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision))
        panic("bootloader does not support the requested protocol revision");

    if (fb_request.response && fb_request.response->framebuffer_count > 0)
        fb_init(fb_request.response->framebuffers[0]);
    else
        kprintf("(no framebuffer, serial only)\n");

    kprintf("LeOS booting...\n");
    kprintf("hello %d\n", 42);

    if (!hhdm_request.response || !memmap_request.response || !exec_request.response)
        panic("bootloader did not provide the HHDM, memory map or executable address");
    uint64_t hhdm = hhdm_request.response->offset;
    kprintf("HHDM offset: %p\n", (void *)hhdm);
    kprintf("Memory map entries: %lu\n", memmap_request.response->entry_count);

    gdt_init();
    kprintf("GDT + TSS loaded\n");
    idt_init();
    kprintf("IDT loaded (256 vectors)\n");
    pic_init();
    kprintf("Legacy PIC remapped and masked\n");
    syscall_init();
    modules_init(module_request.response);
    kprintf("syscall enabled, %lu boot module(s)\n", (unsigned long)module_count());

    pmm_init(memmap_request.response, hhdm);
    pmm_print_stats();
    vmm_init(memmap_request.response, exec_request.response, hhdm);
    kprintf("VMM: kernel page tables loaded (CR3 switched)\n");
    heap_init();
    kprintf("Memory self-test:\n");
    int mm_fail = mm_selftest_quick();
#ifdef MM_TEST
    kprintf("Full memory test (stress + 100 MiB):\n");
    mm_fail += mm_selftest_full();
#endif
    kprintf("Memory self-test: %s\n", mm_fail ? "FAILED" : "PASSED");

    if (rsdp_request.response && acpi_init(rsdp_request.response->address, hhdm) == 0)
        kprintf("ACPI: tables found\n");
    else
        kprintf("ACPI: not available, using default APIC addresses\n");
    apic_init();
    sched_init();

    size_t initrd_size;
    const void *initrd = module_find("initrd.tar", &initrd_size);
    if (!initrd) panic("no initrd.tar module: the boot image is incomplete");
    struct vnode *rootfs = memfs_new(1);
    tar_mount(rootfs, initrd, initrd_size);
    vfs_mount("/", rootfs);
    vfs_mount("/tmp", memfs_new(0));
    vfs_mount("/dev", devfs_new());
    kprintf("VFS: ramdisk on /, tmpfs on /tmp, devices on /dev\n");

    if (virtio_blk_init() == 0) {
        struct vnode *disk = fat32_mount();
        if (disk) { vfs_mount("/disk", disk); kprintf("VFS: FAT32 disk on /disk\n"); }
        else kprintf("disk: found, but it has no usable FAT32 volume\n");
    } else {
        kprintf("disk: none (add -drive file=disk.img,if=virtio,format=raw)\n");
    }
    net_init();
    keyboard_init();
    lapic_timer_start(1000);
    kprintf("Scheduler ready, timer at 1000 Hz\n");

#ifdef EXC_TEST
    /* 1 divide error, 2 invalid opcode, 3 #GP, 4 null deref (#PF), 5 double fault,
     * 6 access past the heap end, 7 write to read-only kernel code */
    kprintf("triggering test exception %d\n", EXC_TEST);
    switch (EXC_TEST) {
    case 1: { volatile int a = 1, b = 0; kprintf("%d\n", a / b); break; }
    case 2: __asm__ volatile ("ud2"); break;
    case 3: __asm__ volatile ("mov %0, %%ds" : : "r"((uint16_t)0x38)); break;
    case 4: kprintf("%d\n", *(volatile int *)0); break;
    case 6: kprintf("%d\n", *(volatile int *)(0xFFFFC00000000000ULL + (512ULL << 20))); break;
    case 7: { extern char __text_start[]; *(volatile char *)__text_start = 0; break; }
    case 5: idt_set_present(14, 0); kprintf("%d\n", *(volatile int *)0); break;
    }
#endif

#ifdef SCROLL_TEST
    for (int i = 0; i < 70; i++) kprintf("scroll test line %d\n", i);
#endif

#ifdef PANIC_TEST
    panic("test panic, value=%d", 1234);
#endif

#ifdef KSHELL
    task_create("kshell", shell_thread, 0);      /* the built-in kernel shell, for debugging */
#else
    task_create("init", init_thread, 0);
#endif
#ifdef THREAD_DEMO
    for (int i = 0; i < 3; i++) task_create("letter", letter_thread, &letters[i]);
    task_create("busy", busy_thread, 0);
    task_create("monitor", monitor_thread, 0);
#endif

#ifdef SYNC_TEST
    extern void sync_selftest(void *);
    task_create("synctest", sync_selftest, 0);
#endif

    __asm__ volatile ("sti");

    /* From here on this is the idle task: it runs only when no other task is ready. */
    for (;;) {
        reap_zombies();
        __asm__ volatile ("hlt");
    }
}
