.section .text
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    /* Save user RSP/RIP for SYSRET */
    swapgs
    movq %rsp, %gs:0
    movq %gs:8, %rsp

    /* Push full frame (callee-saved + volatile) */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax
    pushq %rcx      /* saved user RIP */
    pushq %r11      /* saved user RFLAGS */

    /* Syscall number in RAX, args in RDI, RSI, RDX, R10, R8, R9 */
    movq %rdi, %rsi
    movq %rsi, %rdx
    movq %rdx, %rcx
    movq %r10, %r8
    movq %r8, %r9
    movq %r9, %rax
    /* RDI still holds syscall number */

    call syscall_dispatch

    /* Return value in RAX */
    popq %r11      /* user RFLAGS */
    popq %rcx      /* user RIP */
    popq %rax
    popq %rdx
    popq %rbx
    popq %rbp
    popq %rsi
    popq %rdi
    popq %r9
    popq %r8
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    /* Restore user RSP */
    movq %gs:0, %rsp
    swapgs
    sysretq