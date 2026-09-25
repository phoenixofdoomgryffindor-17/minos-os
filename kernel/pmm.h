#ifndef MINOS_PMM_H
#define MINOS_PMM_H
#include <stdint.h>
#include "../boot/bootinfo.h"

#define PMM_FRAME_SIZE 4096ULL
#define PMM_MAX_PHYSICAL_BYTES (64ULL * 1024ULL * 1024ULL * 1024ULL)
#define PMM_MAX_FRAMES (PMM_MAX_PHYSICAL_BYTES / PMM_FRAME_SIZE)
#define PMM_BITMAP_WORDS ((PMM_MAX_FRAMES + 63ULL) / 64ULL)

void pmm_init(const MinOS_BootInfo *info);
uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_frames(uint32_t count);
void pmm_free_frame(uint64_t frame);
int pmm_reserve(uint64_t start, uint64_t size);
int pmm_is_available(uint64_t frame);
uint64_t pmm_free_frames(void);
uint64_t pmm_total_bytes(void);
uint64_t pmm_usable_bytes(void);
uint64_t pmm_reserved_bytes(void);
#endif
