#include "vmm.h"
#include "pmm.h"
#include "serial.h"

#define PRESENT 1ULL
#define WRITE 2ULL
#define HUGE 0x80ULL

static uint64_t *kernel_pml4;
static uint64_t mapped_2mb_pages;

static uint64_t *table_alloc(void) {
    uint64_t phys = pmm_alloc_frame();
    if (!phys) return NULL;
    return (uint64_t *)(phys + MINOS_HHDM_BASE);
}

static void table_free(uint64_t *table) {
    if (!table) return;
    pmm_free_frame((uint64_t)(uintptr_t)table - MINOS_HHDM_BASE);
}

static uint64_t *walk_pml4(uint64_t *pml4, uint64_t vaddr, int create, uint64_t *out_pd) {
    uint32_t pml4_idx = (vaddr >> 39) & 0x1FFULL;
    uint32_t pdpt_idx = (vaddr >> 30) & 0x1FFULL;
    uint32_t pd_idx   = (vaddr >> 21) & 0x1FFULL;
    uint64_t *pdpt_table = (uint64_t *)((pml4[pml4_idx] & ~0xFFFULL) + MINOS_HHDM_BASE);
    if (!pdpt_table) {
        if (!create) return NULL;
        pdpt_table = table_alloc();
        if (!pdpt_table) return NULL;
        pml4[pml4_idx] = ((uint64_t)(uintptr_t)pdpt_table - MINOS_HHDM_BASE) | PRESENT | WRITE;
    }
    uint64_t *pd_table = (uint64_t *)((pdpt_table[pdpt_idx] & ~0xFFFULL) + MINOS_HHDM_BASE);
    if (!pd_table) {
        if (!create) return NULL;
        pd_table = table_alloc();
        if (!pd_table) return NULL;
        pdpt_table[pdpt_idx] = ((uint64_t)(uintptr_t)pd_table - MINOS_HHDM_BASE) | PRESENT | WRITE;
    }
    if (out_pd) *out_pd = (uint64_t)(uintptr_t)pd_table;
    return pd_table + pd_idx;
}

void vmm_init(const MinOS_BootInfo *info) {
    kernel_pml4 = table_alloc();
    if (!kernel_pml4) { serial_puts("[MinOS VMM] FATAL: PML4 allocation failed\n"); return; }
    for (uint32_t i = 0; i < 512; ++i) kernel_pml4[i] = 0;

    uint64_t *pdpt = table_alloc();
    if (!pdpt) { serial_puts("[MinOS VMM] FATAL: PDPT allocation failed\n"); return; }
    for (uint32_t i = 0; i < 512; ++i) pdpt[i] = 0;
    kernel_pml4[0] = ((uint64_t)(uintptr_t)pdpt - MINOS_HHDM_BASE) | PRESENT | WRITE;
    kernel_pml4[MINOS_HHDM_PML4_INDEX] = ((uint64_t)(uintptr_t)pdpt - MINOS_HHDM_BASE) | PRESENT | WRITE;

    uint64_t max_phys = info ? info->total_memory_bytes : 0;
    uint64_t map_bytes = max_phys;
    if (!map_bytes) map_bytes = 2ULL * 1024ULL * 1024ULL;
    if (map_bytes > MINOS_VMM_BOOTSTRAP_MAP_LIMIT) map_bytes = MINOS_VMM_BOOTSTRAP_MAP_LIMIT;

    /* Also ensure kernel image (16 MiB at kernel_image_base) and framebuffer are mapped. */
    uint64_t kernel_end = 0, fb_end = 0;
    if (info) {
        kernel_end = info->kernel_image_base + info->kernel_image_size;
        fb_end = (uint64_t)info->fb_base + info->fb_size;
    }
    if (kernel_end > map_bytes) map_bytes = kernel_end;
    if (fb_end > map_bytes) map_bytes = fb_end;
    if (map_bytes > MINOS_VMM_BOOTSTRAP_MAP_LIMIT) map_bytes = MINOS_VMM_BOOTSTRAP_MAP_LIMIT;

    mapped_2mb_pages = (map_bytes + MINOS_VMM_BOOTSTRAP_PAGE_SIZE - 1ULL) / MINOS_VMM_BOOTSTRAP_PAGE_SIZE;
    if (mapped_2mb_pages > MINOS_VMM_BOOTSTRAP_MAX_PAGES) mapped_2mb_pages = MINOS_VMM_BOOTSTRAP_MAX_PAGES;

    for (uint64_t i = 0; i < mapped_2mb_pages; ++i) {
        uint64_t vaddr = i * MINOS_VMM_BOOTSTRAP_PAGE_SIZE;
        uint64_t *pd = walk_pml4(kernel_pml4, vaddr, 1, NULL);
        if (!pd) continue;
        pd[(vaddr >> 21) & 0x1FFULL] = vaddr | PRESENT | WRITE | HUGE;
        pd[(vaddr >> 21) & 0x1FFULL] = vaddr | PRESENT | WRITE | HUGE;
        uint64_t hhdm_vaddr = MINOS_HHDM_BASE + vaddr;
        pd = walk_pml4(kernel_pml4, hhdm_vaddr, 1, NULL);
        if (!pd) continue;
        pd[(hhdm_vaddr >> 21) & 0x1FFULL] = vaddr | PRESENT | WRITE | HUGE;
    }

    __asm__ volatile ("movq %0, %%cr3" : : "r"((uint64_t)(uintptr_t)kernel_pml4 - MINOS_HHDM_BASE) : "memory");
    serial_printf("[MinOS VMM] CR3=%p, HHDM=%p, 2 MiB pages=%u (%u GiB)\n",
                  (uint64_t)(uintptr_t)kernel_pml4 - MINOS_HHDM_BASE, MINOS_HHDM_BASE,
                  (uint32_t)mapped_2mb_pages, (uint32_t)(mapped_2mb_pages * 2 / 1024));
}

int vmm_map_user_page(uint64_t physical, uint64_t virtual_address) {
    if (!physical || (physical & 4095ULL)) return -1;
    if (!virtual_address || (virtual_address & 4095ULL)) return -1;
    if (virtual_address >= MINOS_HHDM_BASE) return -1;
    uint64_t *pte = walk_pml4(kernel_pml4, virtual_address, 1, NULL);
    if (!pte) return -1;
    *pte = physical | PRESENT | WRITE | 4ULL;
    return 0;
}

int vmm_unmap_user_page(uint64_t virtual_address) {
    if (!virtual_address || (virtual_address & 4095ULL)) return -1;
    if (virtual_address >= MINOS_HHDM_BASE) return -1;
    uint64_t *pte = walk_pml4(kernel_pml4, virtual_address, 0, NULL);
    if (!pte) return -1;
    *pte = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
    return 0;
}

uint64_t vmm_physical_to_virtual(uint64_t physical) {
    return MINOS_HHDM_BASE + physical;
}

uint64_t vmm_mapped_bytes(void) {
    return mapped_2mb_pages * MINOS_VMM_BOOTSTRAP_PAGE_SIZE;
}
