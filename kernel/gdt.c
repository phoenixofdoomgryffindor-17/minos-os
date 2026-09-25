#include "gdt.h"
#include "kernel.h"

struct gdt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
static uint64_t gdt[6] __attribute__((aligned(16))) = {
    0,
    0x00AF9A000000FFFFULL, /* kernel code, DPL 0 */
    0x00AF92000000FFFFULL, /* kernel data, DPL 0 */
    0x00AFFA000000FFFFULL, /* user code, DPL 3 */
    0x00AFF2000000FFFFULL  /* user data, DPL 3 */
};

void gdt_init(void) {
    struct gdt_ptr p = { (uint16_t)(5U * 8U - 1U), (uint64_t)(uintptr_t)gdt };
    __asm__ volatile ("lgdt %0\n"
                      "pushq $0x08\n"
                      "leaq 1f(%%rip), %%rax\n"
                      "pushq %%rax\n"
                      "lretq\n"
                      "1:\n"
                      "movw $0x10, %%ax\n"
                      "movw %%ax, %%ds\n"
                      "movw %%ax, %%es\n"
                      "movw %%ax, %%ss\n"
                      : : "m"(p) : "rax", "memory");
}
