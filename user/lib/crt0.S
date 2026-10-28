/* Program entry. The kernel leaves rsp pointing at argc, then argv[], a NULL, and envp. */
    .text
    .globl _start
_start:
    xorl %ebp, %ebp
    movq (%rsp), %rdi          /* argc */
    leaq 8(%rsp), %rsi         /* argv */
    andq $-16, %rsp            /* the ABI wants rsp aligned before a call */
    call main
    movl %eax, %edi
    call exit
    ud2
