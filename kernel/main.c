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

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

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

    if (hhdm_request.response)
        kprintf("HHDM offset: %p\n", (void *)hhdm_request.response->offset);
    if (memmap_request.response)
        kprintf("Memory map entries: %lu\n", memmap_request.response->entry_count);

    gdt_init();
    kprintf("GDT + TSS loaded\n");
    idt_init();
    kprintf("IDT loaded (256 vectors)\n");
    pic_init();
    keyboard_init();
    kprintf("PIC remapped to vectors 0x20-0x2F, keyboard on IRQ1\n");

    if (!hhdm_request.response || !memmap_request.response || !exec_request.response)
        panic("bootloader did not provide the HHDM, memory map or executable address");

    pmm_init(memmap_request.response, hhdm_request.response->offset);
    pmm_print_stats();
    vmm_init(memmap_request.response, exec_request.response, hhdm_request.response->offset);
    kprintf("VMM: kernel page tables loaded (CR3 switched)\n");
    heap_init();
    kprintf("Memory self-test:\n");
    int mm_fail = mm_selftest_quick();
#ifdef MM_TEST
    kprintf("Full memory test (stress + 100 MiB):\n");
    mm_fail += mm_selftest_full();
#endif
    kprintf("Memory self-test: %s\n", mm_fail ? "FAILED" : "PASSED");
    pmm_print_stats();

#ifdef EXC_TEST
    /* 1 divide error, 2 invalid opcode, 3 #GP, 4 null deref (#PF), 5 double fault,
     * 6 access past the heap end, 7 write to read-only kernel code */
    kprintf("triggering test exception %d\n", EXC_TEST);
    switch (EXC_TEST) {
    case 1: { volatile int a = 1, b = 0; kprintf("%d\n", a / b); break; }
    case 2: __asm__ volatile ("ud2"); break;
    case 3: __asm__ volatile ("mov %0, %%ds" : : "r"((uint16_t)0x38)); break;
    case 4: kprintf("%d\n", *(volatile int *)0); break;
    case 6: kprintf("%d\n", *(volatile int *)(0xFFFFC00000000000ULL + (512ULL << 20))); break;   /* heap window, past the end */
    case 7: { extern char __text_start[]; *(volatile char *)__text_start = 0; break; }              /* write to kernel code */
    case 5: idt_set_present(14, 0); kprintf("%d\n", *(volatile int *)0); break;
    }
#endif

#ifdef SCROLL_TEST
    for (int i = 0; i < 70; i++) kprintf("scroll test line %d\n", i);
#endif

#ifdef PANIC_TEST
    panic("test panic, value=%d", 1234);
#endif

    __asm__ volatile ("sti");
    kprintf("Interrupts on. Type something:\n> ");
    for (;;) __asm__ volatile ("hlt");
}
