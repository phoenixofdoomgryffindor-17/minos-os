#include "pmm.h"
#include "serial.h"

#define MAX_RANGES 256
typedef struct { uint64_t start, end; } frame_range;
static frame_range free_ranges[MAX_RANGES];
static uint32_t range_count;
static uint64_t free_count;
static uint64_t usable_bytes;
static uint64_t reserved_bytes;

static void reserve(uint64_t start, uint64_t size) {
    uint64_t end = start + size;
    if (end < start) end = UINT64_MAX;
    for (uint32_t i = 0; i < range_count; ) {
        frame_range r = free_ranges[i];
        if (end <= r.start || start >= r.end) { ++i; continue; }
        if (start <= r.start && end >= r.end) {
            free_ranges[i] = free_ranges[--range_count]; continue;
        }
        if (start <= r.start) { free_ranges[i].start = end; ++i; continue; }
        if (end >= r.end) { free_ranges[i].end = start; ++i; continue; }
        if (range_count < MAX_RANGES) {
            free_ranges[range_count++] = (frame_range){ end, r.end };
            free_ranges[i].end = start;
        }
        ++i;
    }
}

void pmm_init(const MinOS_BootInfo *info) {
    range_count = 0; free_count = 0; usable_bytes = 0; reserved_bytes = 0;
    if (!info || !info->memory_map || !info->descriptor_size) return;
    uint8_t *p = (uint8_t *)info->memory_map;
    uint64_t entries = info->memory_map_size / info->descriptor_size;
    for (uint64_t i = 0; i < entries; ++i) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(p + i * info->descriptor_size);
        uint64_t bytes = d->NumberOfPages * 4096;
        if (d->Type != EfiConventionalMemory) {
            reserved_bytes += bytes;
            continue;
        }
        if (!d->NumberOfPages) continue;
        usable_bytes += bytes;
        if (range_count < MAX_RANGES) {
            uint64_t s = (d->PhysicalStart + 4095) & ~4095ULL;
            uint64_t e = d->PhysicalStart + d->NumberOfPages * 4096;
            free_ranges[range_count++] = (frame_range){s, e};
            free_count += (e - s) / 4096;
        }
    }
    /* Keep objects that the kernel still references out of the allocator. */
    reserve((uint64_t)(uintptr_t)info, sizeof(*info));
    reserve((uint64_t)(uintptr_t)info->memory_map, info->memory_map_size);
    reserve((uint64_t)(uintptr_t)info->fb_base, info->fb_size);
    reserve(info->kernel_image_base, info->kernel_image_size);
    reserve(info->bootstrap_stack_base, info->bootstrap_stack_size);
    free_count = 0;
    for (uint32_t i = 0; i < range_count; ++i) free_count += (free_ranges[i].end - free_ranges[i].start) / 4096;
    serial_printf("[MinOS PMM] Conventional: %u MiB, reserved: %u MiB\n",
                  usable_bytes / (1024 * 1024), reserved_bytes / (1024 * 1024));
    serial_printf("[MinOS PMM] Conventional 4 KiB frames: %u ranges, %u free\n", range_count, free_count);
}

uint64_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < range_count; ++i)
        if (free_ranges[i].start < free_ranges[i].end) {
            uint64_t frame = free_ranges[i].start;
            free_ranges[i].start += 4096; --free_count; return frame;
        }
    return 0;
}

void pmm_free_frame(uint64_t frame) {
    if ((frame & 4095) != 0 || !frame) return;
    if (range_count >= MAX_RANGES) return;
    uint32_t i = 0;
    while (i < range_count && free_ranges[i].start < frame) ++i;
    if (i > 0 && free_ranges[i - 1].end == frame) {
        free_ranges[i - 1].end += 4096;
        if (i < range_count && free_ranges[i - 1].end == free_ranges[i].start) {
            free_ranges[i - 1].end = free_ranges[i].end;
            free_ranges[i] = free_ranges[--range_count];
        }
    } else if (i < range_count && frame + 4096 == free_ranges[i].start) {
        free_ranges[i].start = frame;
    } else {
        for (uint32_t j = range_count; j > i; --j) free_ranges[j] = free_ranges[j - 1];
        free_ranges[i] = (frame_range){ frame, frame + 4096 };
        ++range_count;
    }
    ++free_count;
}

uint64_t pmm_free_frames(void) { return free_count; }
uint64_t pmm_usable_bytes(void) { return usable_bytes; }
uint64_t pmm_reserved_bytes(void) { return reserved_bytes; }
