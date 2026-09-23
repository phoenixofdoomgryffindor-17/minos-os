#ifndef MINOS_KERNEL_H
#define MINOS_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include "../boot/bootinfo.h"

/* x86 I/O Port operations */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}

static inline void halt_loop(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* Kernel Entry Point */
#if defined(__GNUC__)
#define MINOS_SYSV_ABI __attribute__((sysv_abi))
#else
#define MINOS_SYSV_ABI
#endif

void kernel_enter(MinOS_BootInfo *boot_info) MINOS_SYSV_ABI;
void kernel_main(MinOS_BootInfo *boot_info) MINOS_SYSV_ABI;


#endif /* MINOS_KERNEL_H */
