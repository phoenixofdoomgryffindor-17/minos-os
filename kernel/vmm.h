#ifndef MINOS_VMM_H
#define MINOS_VMM_H
#include <stdint.h>
#include "../boot/bootinfo.h"

#define MINOS_HIGHER_HALF 0xFFFF800000000000ULL
#define MINOS_HHDM_BASE MINOS_HIGHER_HALF
/* Keep the established MinOS direct-map selector stable across bootstrap revisions. */
#define MINOS_HHDM_PML4_INDEX 256ULL
#define MINOS_VMM_BOOTSTRAP_PAGE_SIZE (2ULL * 1024ULL * 1024ULL)
#define MINOS_VMM_BOOTSTRAP_MAP_LIMIT (64ULL * 1024ULL * 1024ULL * 1024ULL)
#define MINOS_VMM_BOOTSTRAP_MAX_PAGES \
    (MINOS_VMM_BOOTSTRAP_MAP_LIMIT / MINOS_VMM_BOOTSTRAP_PAGE_SIZE)
#define MINOS_VMM_PD_COUNT 262144ULL
#define MINOS_PAGE_PRESENT 1ULL
#define MINOS_PAGE_WRITE 2ULL
#define MINOS_PAGE_USER 4ULL
#define MINOS_PAGE_HUGE 0x80ULL

void vmm_init(const MinOS_BootInfo *info);
int vmm_map_user_page(uint64_t physical, uint64_t virtual_address);
int vmm_unmap_user_page(uint64_t virtual_address);
uint64_t vmm_physical_to_virtual(uint64_t physical);
uint64_t vmm_mapped_bytes(void);
#endif
