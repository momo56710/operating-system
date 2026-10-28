#include "syscall.h"
#include "io.h"

/* Kernel stack top for the task that is running; schedule() updates it on every switch. */
uint64_t syscall_kstack;

extern void syscall_entry(void);

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084

void syscall_init(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1);                    /* SCE: enable syscall/sysret */
    /* syscall loads CS=0x08, SS=0x10; sysret loads SS=0x18|3 and CS=0x20|3 from base 0x10 */
    wrmsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, 0x200 | 0x400 | 0x100 | 0x40000);      /* clear IF, DF, TF, AC on entry */
}
