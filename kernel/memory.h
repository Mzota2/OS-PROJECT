#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define PAGE_SIZE 4096

void memory_init(uint32_t start, uint32_t end);

void* alloc_page(void);

void free_page(void* page);

#endif