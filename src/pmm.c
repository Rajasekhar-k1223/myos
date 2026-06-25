#include "pmm.h"
#include "string.h"

static uint32_t pmm_bitmap[PMM_MAX_FRAMES / 32];
static uint32_t pmm_used_frames = 0;
static uint32_t pmm_max_frames = 0;

static inline void pmm_set(uint32_t frame) {
    pmm_bitmap[frame / 32] |= (1 << (frame % 32));
}

static inline void pmm_clear(uint32_t frame) {
    pmm_bitmap[frame / 32] &= ~(1 << (frame % 32));
}

static inline int pmm_test(uint32_t frame) {
    return pmm_bitmap[frame / 32] & (1 << (frame % 32));
}

void pmm_init(struct multiboot_info* mbi) {
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap)); // Mark all used by default

    // Read multiboot memory map to find available RAM
    if (mbi->flags & (1 << 6)) {
        struct multiboot_mmap_entry* mmap = (struct multiboot_mmap_entry*)mbi->mmap_addr;
        while ((uint32_t)mmap < mbi->mmap_addr + mbi->mmap_length) {
            if (mmap->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint32_t addr = mmap->addr_low;
                uint32_t len = mmap->len_low;
                uint32_t max_addr = addr + len;
                if (max_addr > pmm_max_frames * PMM_FRAME_SIZE) {
                    pmm_max_frames = max_addr / PMM_FRAME_SIZE;
                }
                for (uint32_t i = 0; i < len; i += PMM_FRAME_SIZE) {
                    pmm_clear((addr + i) / PMM_FRAME_SIZE);
                }
            }
            mmap = (struct multiboot_mmap_entry*)((uint32_t)mmap + mmap->size + sizeof(mmap->size));
        }
    }

    // Protect kernel memory (assuming kernel loads at 1MB and takes less than 1MB for now)
    for (uint32_t i = 0; i < 0x200000 / PMM_FRAME_SIZE; i++) {
        pmm_set(i);
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
    return NULL; // Out of memory
}

void pmm_free_frame(void* addr) {
    uint32_t frame = (uint32_t)addr / PMM_FRAME_SIZE;
    pmm_clear(frame);
    pmm_used_frames--;
}
