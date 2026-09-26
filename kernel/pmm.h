#ifndef MINOS_PMM_H
#define MINOS_PMM_H
#include "../boot/bootinfo.h"
void pmm_init(const MinOS_BootInfo *info);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t frame);
uint64_t pmm_free_frames(void);
uint64_t pmm_usable_bytes(void);
uint64_t pmm_reserved_bytes(void);
#endif
