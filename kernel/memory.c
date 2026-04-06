// memory.c
// Simple Memory Management
//
// Implements a bump allocator for kernel memory allocation.
// Bump allocators are simple but wasteful (can't free memory).
// Good for learning; real OSes use more sophisticated allocators.

#include <stddef.h>
#include "memory.h"

// ===== HEAP STATE =====
// A bump allocator just tracks a "next free address" pointer.
// All allocations progress forward; no freeing is possible.
static uint32_t heap_start;  // Start of heap region
static uint32_t heap_end;    // End of heap region
static uint32_t next_alloc;  // Current position (next address to allocate)

void memory_init(uint32_t start, uint32_t end) {
    // Initialize the memory allocator.
    // This is called at kernel startup with the heap memory range.
    //
    // Parameters:
    //   start: first address available for allocation
    //   end: last address available (exclusive)
    
    heap_start = start;
    heap_end = end;
    next_alloc = start;
}

void* alloc_page(void) {
    // Allocate one 4KB page.
    // Returns pointer to page, or NULL if no memory available.
    
    // Check if allocation would exceed heap
    if (next_alloc + PAGE_SIZE > heap_end) {
        return NULL;  // Out of memory
    }
    
    // Get current position (our new page)
    void* page = (void*)next_alloc;
    
    // Advance pointer for next allocation
    next_alloc += PAGE_SIZE;
    
    return page;
}

void free_page(void* page) {
    // Stub function (does nothing).
    // Bump allocators don't support freeing.
    // Included for API compatibility.
    //
    // In a real OS, this would return the page to the free list.
}