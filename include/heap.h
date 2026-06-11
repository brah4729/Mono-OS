#ifndef HEAP_H
#define HEAP_H

#include "types.h"

// Start heap at 2MB — safely past kernel, PMM bitmap, and VMM page tables
// With 16MB identity map, heap at 0x200000 is fully within mapped memory
#define HEAP_START        0x200000    // 2 MiB — safe fixed address
#define HEAP_INITIAL_SIZE 0x800000    // 8 MiB — enough for backbuffer (3MB) + windows

void  heap_init(void);
void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size);
void* kmalloc_physical(size_t size, uint32_t* phys);
void  kfree(void* ptr);

#endif