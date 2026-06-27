#include "pmm.h"
#include "multiboot.h"
#include "string.h"

static uint32_t pmm_bitmap[PMM_MAX_FRAMES / 32];
static uint32_t pmm_used_frames = 0;
static uint32_t pmm_max_frames  = 0;

static inline void pmm_set(uint32_t frame)  { pmm_bitmap[frame/32] |=  (1u << (frame%32)); }
static inline void pmm_clear(uint32_t frame) { pmm_bitmap[frame/32] &= ~(1u << (frame%32)); }
static inline int  pmm_test(uint32_t frame)  { return pmm_bitmap[frame/32] &  (1u << (frame%32)); }

/*
 * Initialise from Multiboot2 mmap tag (type 6).
 * mmap_tag_addr  = physical address of struct mb2_tag_mmap
 * entry_size     = bytes per mb2_mmap_entry (usually 24)
 * total_bytes    = tag->size - 16  (total entry data bytes)
 */
void pmm_init(uint32_t mmap_tag_addr, uint32_t entry_size, uint32_t total_bytes) {
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap));
    pmm_max_frames  = 0;
    pmm_used_frames = 0;

    uint32_t offset = 0;
    while (offset + entry_size <= total_bytes) {
        struct mb2_mmap_entry* e =
            (struct mb2_mmap_entry*)(mmap_tag_addr + offset);

        uint32_t addr = (uint32_t)(e->addr & 0xFFFFFFFFu);
        uint32_t len  = (uint32_t)(e->len  & 0xFFFFFFFFu);

        if (e->type == MB2_MMAP_AVAILABLE) {
            if (addr + len > pmm_max_frames * PMM_FRAME_SIZE)
                pmm_max_frames = (addr + len) / PMM_FRAME_SIZE;
            for (uint32_t i = 0; i < len; i += PMM_FRAME_SIZE)
                pmm_clear((addr + i) / PMM_FRAME_SIZE);
        }

        offset += entry_size;
    }

    /* protect first 2 MB (BIOS data, kernel, stack) */
    for (uint32_t i = 0; i < 0x200000u / PMM_FRAME_SIZE; i++)
        pmm_set(i);
}

void pmm_mark_used(uint32_t start, uint32_t size) {
    uint32_t start_frame = start / PMM_FRAME_SIZE;
    uint32_t end_frame = (start + size + PMM_FRAME_SIZE - 1) / PMM_FRAME_SIZE;
    for (uint32_t i = start_frame; i < end_frame; i++) {
        if (!pmm_test(i)) {
            pmm_set(i);
            pmm_used_frames++;
        }
    }
}

void* pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < pmm_max_frames; i++) {
        if (!pmm_test(i)) {
            pmm_set(i);
            pmm_used_frames++;
            return (void*)(i * PMM_FRAME_SIZE);
        }
    }
    return 0;
}

void pmm_free_frame(void* addr) {
    uint32_t frame = (uint32_t)addr / PMM_FRAME_SIZE;
    pmm_clear(frame);
    if (pmm_used_frames) pmm_used_frames--;
}

uint32_t pmm_get_used_frames(void) { return pmm_used_frames; }
uint32_t pmm_get_max_frames(void)  { return pmm_max_frames;  }
