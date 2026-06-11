/* kernel/heap.c — Kernel heap allocator (linked-list first-fit) */

#include "../include/heap.h"
#include "../include/pmm.h"
#include "../include/vmm.h"
#include "../include/string.h"
#include "../include/serial.h"

typedef struct heap_block {
    uint32_t size;
    bool     free;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* heap_start = 0;
static uint32_t      heap_end_addr = 0;

/* Simple placement allocator used before the heap is set up */

void heap_init(void) {
    /* Initialize the heap with a single large free block */
    heap_start = (heap_block_t*)HEAP_START;
    heap_end_addr = HEAP_START + HEAP_INITIAL_SIZE;

    heap_start->size = HEAP_INITIAL_SIZE - sizeof(heap_block_t);
    heap_start->free = true;
    heap_start->next = NULL;

    serial_puts("[HEAP] Initialized heap at 0x");
    serial_put_hex(HEAP_START);
    serial_puts(", size=");
    char buf[16];
    utoa(HEAP_INITIAL_SIZE / 1024, buf, 10);
    serial_puts(buf);
    serial_puts(" KiB\n");
}

/* Split a block if it's larger than needed */
static void split_block(heap_block_t* block, uint32_t size) {
    if (block->size >= size + sizeof(heap_block_t) + 16) {
        heap_block_t* new_block = (heap_block_t*)((uint8_t*)block + sizeof(heap_block_t) + size);
        new_block->size = block->size - size - sizeof(heap_block_t);
        new_block->free = true;
        new_block->next = block->next;

        block->size = size;
        block->next = new_block;
    }
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align size to 4 bytes */
    size = (size + 3) & ~3;

    heap_block_t* current = heap_start;
    while (current) {
        if (current->free && current->size >= size) {
            split_block(current, size);
            current->free = false;
            return (void*)((uint8_t*)current + sizeof(heap_block_t));
        }
        current = current->next;
    }

    /* Out of heap memory */
    serial_puts("[HEAP] WARNING: Out of memory!\n");
    return NULL;
}

void* kmalloc_aligned(size_t size) {
    /* For page-aligned allocations */
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    return kmalloc(size);
}

void* kmalloc_physical(size_t size, uint32_t* phys) {
    void* addr = kmalloc(size);
    if (addr && phys) {
        *phys = (uint32_t)addr; /* In identity-mapped region, virt == phys */
    }
    return addr;
}

/* Merge adjacent free blocks */
static void coalesce(void) {
    heap_block_t* current = heap_start;
    while (current && current->next) {
        if (current->free && current->next->free) {
            current->size += sizeof(heap_block_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

void kfree(void* ptr) {
    if (!ptr) return;

    heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
    block->free = true;
    coalesce();
}
