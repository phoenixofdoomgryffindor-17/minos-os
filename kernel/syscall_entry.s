.section .text
.global syscall_entry
syscall_entry:
    swapgs
    movq %rsp, %gs:0
    movq %gs:8, %rsp
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
    pushq %rcx
    pushq %r11
    movq %rdi, %rsi
    movq %rsi, %rdx
    movq %rdx, %rcx
    movq %r10, %r8
    movq %r8, %r9
    movq %r9, %rax
    call syscall_dispatch
    popq %r11
    popq %rcx
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
    movq %gs:0, %rsp
    swapgs
    sysretq