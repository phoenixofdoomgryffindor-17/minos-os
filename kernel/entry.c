#include "kernel.h"

/* A firmware stack must not be retained after ExitBootServices. */
static uint8_t kernel_stack[32768] __attribute__((aligned(16)));

MINOS_SYSV_ABI void kernel_enter(MinOS_BootInfo *boot_info)
    __attribute__((naked, noreturn, noinline));

MINOS_SYSV_ABI void kernel_enter(MinOS_BootInfo *boot_info) {
    (void)boot_info;
    __asm__ volatile (
        "cli\n"
        "movq %rdi, %r12\n"
        "leaq kernel_stack+32768(%rip), %rsp\n"
        "andq $-16, %rsp\n"
        "movq %r12, %rdi\n"
        "call kernel_main\n"
        "cli\n"
        "hlt\n"
        "jmp .-2\n");
}

uint64_t kernel_stack_base(void) { return (uint64_t)(uintptr_t)kernel_stack; }
uint64_t kernel_stack_size(void) { return sizeof(kernel_stack); }
