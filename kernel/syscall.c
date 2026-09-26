#include "syscall.h"
#include "scheduler.h"
#include "console.h"
#include "vfs.h"
#include "serial.h"

static uint64_t (*syscall_table[256])(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t sys_write(uint64_t fd, uint64_t buf, uint64_t count, uint64_t, uint64_t) {
    if (fd == 1 || fd == 2) {
        const char *s = (const char *)buf;
        for (uint64_t i = 0; i < count; ++i) console_putc(s[i]);
        return count;
    }
    return -1;
}
static uint64_t sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t, uint64_t) { return 0; }
static uint64_t sys_open(uint64_t path, uint64_t, uint64_t, uint64_t, uint64_t) { return vfs_open((const char *)path, 0); }
static uint64_t sys_close(uint64_t fd, uint64_t, uint64_t, uint64_t, uint64_t) { return vfs_close((int)fd); }
static uint64_t sys_exit(uint64_t code, uint64_t, uint64_t, uint64_t, uint64_t) {
    thread_exit(); return code;
}
static uint64_t sys_yield(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    thread_yield(); return 0;
}

void syscall_init(void) {
    for (int i = 0; i < 256; ++i) syscall_table[i] = NULL;
    syscall_table[SYSCALL_WRITE] = sys_write;
    syscall_table[SYSCALL_READ]  = sys_read;
    syscall_table[SYSCALL_OPEN]  = sys_open;
    syscall_table[SYSCALL_CLOSE] = sys_close;
    syscall_table[SYSCALL_EXIT]  = sys_exit;
    syscall_table[SYSCALL_YIELD] = sys_yield;

    /* IA32_EFER.SCE = 1 */
    uint64_t efer = 0;
    __asm__ volatile ("rdmsr" : "=A"(efer) : "c"(0xC0000080));
    efer |= 1ULL;
    __asm__ volatile ("wrmsr" : : "c"(0xC0000080), "A"(efer));

    /* IA32_STAR = kernel_cs<<32 | kernel_ss<<48 | user_cs<<16 | user_ss<<0 */
    uint64_t star = ((uint64_t)0x08ULL << 32) | ((uint64_t)0x10ULL << 48) | ((uint64_t)0x1BULL << 16) | ((uint64_t)0x23ULL << 0);
    __asm__ volatile ("wrmsr" : : "c"(0xC0000081), "A"(star));

    /* IA32_LSTAR = syscall_entry point */
    extern void syscall_entry(void);
    __asm__ volatile ("wrmsr" : : "c"(0xC0000082), "A"((uint64_t)(uintptr_t)syscall_entry));

    /* IA32_FMASK = mask IF (0x200) */
    __asm__ volatile ("wrmsr" : : "c"(0xC0000084), "A"(0x200ULL));
    serial_puts("[MinOS Syscall] MSRs configured (SYSCALL/SYSRET ready)\n");
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    if (num >= 256 || !syscall_table[num]) return (uint64_t)-1;
    return syscall_table[num](a1, a2, a3, a4, a5);
}