#pragma once
/* callee-saved registers rbx, rbp, r12-r15, plus the stack pointer and the return address */
typedef long jmp_buf[8];
int  setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
