#include <stddef.h>
#include "memory.h"

static uint32_t heap_start;
static uint32_t heap_end;
static uint32_t next_alloc;

void memory_init(uint32_t start, uint32_t end) {
    heap_start = start;
    heap_end = end;
    next_alloc = start;
}

void* alloc_page(void) {
    if (next_alloc + PAGE_SIZE > heap_end) return NULL;
    void* page = (void*)next_alloc;
    next_alloc += PAGE_SIZE;
    return page;
}

void free_page(void* page) {
    // Simple bump allocator, no free
}