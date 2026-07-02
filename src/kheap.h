#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

void kheap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* krealloc(void* ptr, size_t newsize);
void  kheap_stats(uint32_t* used_out, uint32_t* free_out, uint32_t* blocks_out);

#endif
