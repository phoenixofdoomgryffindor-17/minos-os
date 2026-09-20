#ifndef MINOS_VMM_H
#define MINOS_VMM_H
#include "../boot/bootinfo.h"
#define MINOS_HIGHER_HALF 0xFFFF800000000000ULL
#define MINOS_HHDM_BASE MINOS_HIGHER_HALF
void vmm_init(const MinOS_BootInfo *info);
#endif
