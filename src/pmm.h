#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "multiboot.h"

#define PMM_FRAME_SIZE 4096
#define PMM_MAX_FRAMES 1048576

void pmm_init(struct multiboot_info* mbi);
void* pmm_alloc_frame(void);
void pmm_free_frame(void* addr);
uint32_t pmm_get_used_frames(void);
uint32_t pmm_get_max_frames(void);

#endif
