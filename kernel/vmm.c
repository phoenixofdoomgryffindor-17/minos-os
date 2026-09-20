#include "vmm.h"
#include "serial.h"

#define PRESENT 1ULL
#define WRITE 2ULL
#define HUGE 0x80ULL
static uint64_t pml4[512] __attribute__((aligned(4096)));
static uint64_t pdpt[512] __attribute__((aligned(4096)));
static uint64_t pd[2048] __attribute__((aligned(4096)));

void vmm_init(const MinOS_BootInfo *info) {
    for (uint32_t i = 0; i < 512; ++i) pml4[i] = pdpt[i] = 0;
    for (uint32_t i = 0; i < 2048; ++i) pd[i] = 0;
    pml4[0] = (uint64_t)(uintptr_t)pdpt | PRESENT | WRITE;
    /* The same page directory is visible both identity-mapped and through HHDM. */
    pml4[256] = (uint64_t)(uintptr_t)pdpt | PRESENT | WRITE;
    for (uint32_t i = 0; i < 4; ++i)
        pdpt[i] = (uint64_t)(uintptr_t)(pd + i * 512) | PRESENT | WRITE;
    uint64_t max_phys = info ? info->total_memory_bytes : 0;
    uint64_t map_bytes = max_phys;
    if (map_bytes > 4ULL * 1024 * 1024 * 1024) map_bytes = 4ULL * 1024 * 1024 * 1024;
    if (!map_bytes) map_bytes = 2ULL * 1024 * 1024;
    uint32_t pages = (uint32_t)((map_bytes + (2ULL * 1024 * 1024 - 1)) / (2ULL * 1024 * 1024));
    if (pages > 2048) pages = 2048;
    for (uint32_t i = 0; i < pages; ++i) pd[i] = ((uint64_t)i << 21) | PRESENT | WRITE | HUGE;
    __asm__ volatile ("movq %0, %%cr3" : : "r"((uint64_t)(uintptr_t)pml4) : "memory");
    serial_printf("[MinOS VMM] CR3=%p, identity + HHDM=%p, mapped=%u MiB\n",
                  (uint64_t)(uintptr_t)pml4, MINOS_HHDM_BASE, (uint64_t)pages * 2);
}
