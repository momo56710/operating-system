#pragma once
#include <stdint.h>
#include "interrupt.h"

#define PROC_FAULT_BASE 1000   /* exit status of a process killed by CPU exception N is 1000 + N */

long process_spawn(const char *name, char *const argv[]);   /* start a program from the boot modules; pid or -1 */
void process_exit(int code) __attribute__((noreturn));
long process_wait(int *status);                              /* reap a finished child; its pid, or -1 if none */
long process_fork(struct interrupt_frame *f);
long process_exec(const char *name, char *const argv[], struct interrupt_frame *f);
void process_fault(struct interrupt_frame *f, const char *what) __attribute__((noreturn));   /* user code hit an exception */
long process_brk(uint64_t addr);
long process_mmap(uint64_t len);
long process_munmap(uint64_t addr, uint64_t len);
int  process_grow_stack(uint64_t addr);    /* a fault in the stack region: add a page; 1 if handled */
