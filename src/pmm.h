#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_FRAME_SIZE  4096
#define PMM_MAX_FRAMES  1048576

/* Initialise from a Multiboot2 mmap tag */
void  pmm_init(uint32_t mmap_tag_addr, uint32_t entry_size, uint32_t total_bytes);
void* pmm_alloc_frame(void);
void  pmm_free_frame(void* addr);
uint32_t pmm_get_used_frames(void);
uint32_t pmm_get_max_frames(void);

#endif
