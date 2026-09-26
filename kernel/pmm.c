#include "pmm.h"
#include "serial.h"
#include <stddef.h>

#define PMM_MAX_FRAMES (64ULL * 1024ULL * 1024ULL * 1024ULL / 4096ULL)
#define PMM_BITMAP_WORDS ((PMM_MAX_FRAMES + 63ULL) / 64ULL)

static uint64_t bitmap[PMM_BITMAP_WORDS];
static uint64_t total_frames;
static uint64_t free_frames;
static uint64_t reserved_bytes_total;

static inline void bitmap_set(uint64_t frame) {
    bitmap[frame >> 6] |= (1ULL << (frame & 63ULL));
}
static inline void bitmap_clear(uint64_t frame) {
    bitmap[frame >> 6] &= ~(1ULL << (frame & 63ULL));
}
static inline int bitmap_test(uint64_t frame) {
    return (bitmap[frame >> 6] >> (frame & 63ULL)) & 1ULL;
}

static void reserve_range(uint64_t start, uint64_t end) {
    for (uint64_t f = start; f < end; ++f) {
        if (!bitmap_test(f)) { bitmap_set(f); --free_frames; }
    }
    reserved_bytes_total += (end - start) * 4096ULL;
}

void pmm_init(const MinOS_BootInfo *info) {
    for (uint32_t i = 0; i < PMM_BITMAP_WORDS; ++i) bitmap[i] = 0;
    total_frames = 0; free_frames = 0; reserved_bytes_total = 0;
    if (!info || !info->memory_map || !info->descriptor_size) return;
    uint8_t *p = (uint8_t *)info->memory_map;
    uint64_t entries = info->memory_map_size / info->descriptor_size;
    for (uint64_t i = 0; i < entries; ++i) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(p + i * info->descriptor_size);
        uint64_t bytes = d->NumberOfPages * 4096ULL;
        if (d->Type != EfiConventionalMemory) {
            reserved_bytes_total += bytes;
            continue;
        }
        if (!d->NumberOfPages) continue;
        uint64_t s = (d->PhysicalStart + 4095ULL) & ~4095ULL;
        uint64_t e = d->PhysicalStart + d->NumberOfPages * 4096ULL;
        if (e > PMM_MAX_FRAMES * 4096ULL) e = PMM_MAX_FRAMES * 4096ULL;
        for (uint64_t f = s / 4096ULL; f < e / 4096ULL; ++f) {
            if (!bitmap_test(f)) { bitmap_clear(f); ++free_frames; }
        }
        total_frames += (e - s) / 4096ULL;
    }
    /* Kernel-reserved regions */
    if (info->initrd_size)
        reserve_range(info->initrd_base / 4096ULL, (info->initrd_base + info->initrd_size + 4095ULL) / 4096ULL);
    reserve_range((uint64_t)(uintptr_t)info / 4096ULL, ((uint64_t)(uintptr_t)info + sizeof(*info) + 4095ULL) / 4096ULL);
    reserve_range((uint64_t)(uintptr_t)info->memory_map / 4096ULL, ((uint64_t)(uintptr_t)info->memory_map + info->memory_map_size + 4095ULL) / 4096ULL);
    reserve_range((uint64_t)info->fb_base / 4096ULL, ((uint64_t)info->fb_base + info->fb_size + 4095ULL) / 4096ULL);
    reserve_range(info->kernel_image_base / 4096ULL, (info->kernel_image_base + info->kernel_image_size + 4095ULL) / 4096ULL);
    reserve_range(info->bootstrap_stack_base / 4096ULL, (info->bootstrap_stack_base + info->bootstrap_stack_size + 4095ULL) / 4096ULL);
    serial_printf("[MinOS PMM] Conventional: %u MiB, reserved: %u MiB\n",
                  (uint32_t)(total_frames * 4096ULL / (1024ULL * 1024ULL)),
                  (uint32_t)(reserved_bytes_total / (1024ULL * 1024ULL)));
    serial_printf("[MinOS PMM] 4 KiB frames: %u total, %u free\n", (uint32_t)total_frames, (uint32_t)free_frames);
}

uint64_t pmm_alloc_frame(void) {
    for (uint64_t f = 1; f < total_frames; ++f)
        if (!bitmap_test(f)) { bitmap_set(f); --free_frames; return f * 4096ULL; }
    return 0;
}

uint64_t pmm_alloc_frames(uint32_t count) {
    if (!count) return 0;
    for (uint64_t f = 1; f + count <= total_frames; ++f) {
        int ok = 1;
        for (uint32_t j = 0; j < count; ++j)
            if (bitmap_test(f + j)) { ok = 0; break; }
        if (ok) {
            for (uint32_t j = 0; j < count; ++j) bitmap_set(f + j);
            free_frames -= count;
            return f * 4096ULL;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t frame) {
    if (!frame || (frame & 4095ULL)) return;
    uint64_t f = frame / 4096ULL;
    if (f < total_frames && bitmap_test(f)) { bitmap_clear(f); ++free_frames; }
}

int pmm_reserve(uint64_t start, uint64_t size) {
    if (!size) return -1;
    uint64_t s = (start + 4095ULL) & ~4095ULL;
    uint64_t e = (start + size + 4095ULL) & ~4095ULL;
    if (e > PMM_MAX_FRAMES * 4096ULL) return -1;
    reserve_range(s / 4096ULL, e / 4096ULL);
    return 0;
}

int pmm_is_available(uint64_t frame) {
    if (!frame || (frame & 4095ULL)) return 0;
    uint64_t f = frame / 4096ULL;
    return f < total_frames && !bitmap_test(f);
}

uint64_t pmm_free_frames(void) { return free_frames; }
uint64_t pmm_total_bytes(void) { return total_frames * 4096ULL; }
uint64_t pmm_usable_bytes(void) { return free_frames * 4096ULL; }
uint64_t pmm_reserved_bytes(void) { return reserved_bytes_total; }
