#ifndef MINOS_BOOT_BOOTINFO_H
#define MINOS_BOOT_BOOTINFO_H

#include <stdint.h>
#include "efi.h"

#define MINOS_BOOTINFO_MAGIC 0x4D696E4F533031ULL /* "MinOS01" */

typedef struct {
    uint64_t magic;
    
    /* Framebuffer Parameters */
    void    *fb_base;
    uint64_t fb_size;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
    uint32_t fb_is_bgr; /* 1 if BGR, 0 if RGB */

    /* CPU Detection */
    char     cpu_vendor[13];

    /* Memory Map Information */
    uint64_t total_memory_bytes;
    uint64_t usable_memory_bytes;
    uint64_t memory_map_size;
    uint64_t descriptor_size;
    uint32_t descriptor_version;
    void    *memory_map; /* Pointer to EFI_MEMORY_DESCRIPTOR array */
    uint64_t kernel_image_base;
    uint64_t kernel_image_size;
    uint64_t bootstrap_stack_base;
    uint64_t bootstrap_stack_size;
    uint64_t initrd_base;
    uint64_t initrd_size;
} MinOS_BootInfo;

#endif /* MINOS_BOOT_BOOTINFO_H */
