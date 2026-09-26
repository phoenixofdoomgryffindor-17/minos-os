#ifndef MINOS_HEAP_H
#define MINOS_HEAP_H
#include <stdint.h>
#include <stddef.h>

void heap_init(void);
void *kmalloc(uint32_t size);
void kfree(void *ptr);

#endif