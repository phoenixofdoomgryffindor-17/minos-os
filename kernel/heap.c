#include "heap.h"
#include "pmm.h"
#include "vmm.h"
#include "serial.h"
#include <stddef.h>

#define HEAP_MAGIC 0x4D4F5348ULL

typedef struct slab_header {
    struct slab_header *next;
    uint64_t magic;
    uint32_t size;
    uint8_t free;
    uint8_t pad[3];
} slab_header;

static uint64_t heap_base;
static uint64_t heap_top;
static uint64_t heap_limit;
static slab_header *free_list;

void heap_init(void) {
    heap_base = 0xFFFF8000FFC00000ULL;
    heap_limit = 0xFFFF8000FFFF0000ULL;
    heap_top = heap_base;
    free_list = NULL;
    serial_puts("[MinOS Heap] Initialized (16 MiB at HHDM+252 MiB)\n");
}

static slab_header *slab_alloc(uint32_t size) {
    uint64_t page = pmm_alloc_frame();
    if (!page) return NULL;
    if (vmm_map_user_page(page, heap_top) != 0) { pmm_free_frame(page); return NULL; }
    slab_header *h = (slab_header *)(heap_top + MINOS_HHDM_BASE);
    h->magic = HEAP_MAGIC;
    h->size = size;
    h->free = 1;
    h->next = free_list;
    free_list = h;
    heap_top += 4096;
    return h;
}

void *kmalloc(uint32_t size) {
    if (!size) return NULL;
    size = (size + 15) & ~15U;
    for (slab_header *h = free_list; h; h = h->next)
        if (h->free && h->size >= size) {
            h->free = 0;
            return (void *)(h + 1);
        }
    slab_header *h = slab_alloc(size);
    if (!h) return NULL;
    h->free = 0;
    return (void *)(h + 1);
}

void kfree(void *ptr) {
    if (!ptr) return;
    slab_header *h = (slab_header *)ptr - 1;
    if (h->magic != HEAP_MAGIC) return;
    h->free = 1;
}