#include "limine.h"
#include "serial.h"
#include "fb.h"
#include "kprintf.h"
#include "panic.h"

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

#ifdef SCROLL_TEST
    for (int i = 0; i < 70; i++) kprintf("scroll test line %d\n", i);
#endif

#ifdef PANIC_TEST
    panic("test panic, value=%d", 1234);
#endif

    kprintf("Boot complete, halting.\n");
    for (;;) __asm__ volatile ("hlt");
}
