.section .text
.global context_switch
.type context_switch, @function
context_switch:
    /* Save old context (caller-saved not needed, callee-saved are in tcb) */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* rdi = new tcb */
    movq %rsp, (%rdi)
    /* Switch stack */
    movq (%rdi), %rsp
    /* Restore new context */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret