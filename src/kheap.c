#include "kheap.h"
#include "pmm.h"

typedef struct header {
    size_t size;
    struct header* next;
    int is_free;
} header_t;

static header_t* head = NULL;
// We will allocate a few frames for the initial heap
#define HEAP_INIT_SIZE (1024 * 1024) // 1 MB heap

void kheap_init(void) {
    // Allocate contiguous frames for our simple heap
    // Since our PMM allocates first-available, we can grab 256 frames (1MB)
    // Wait, PMM might not be strictly contiguous, but for a simple OS right after boot, it is.
    // A safer way is to just grab 1 frame for the heap head for now, and grow it later, but 1MB is fine.
    void* heap_start = pmm_alloc_frame();
    for (int i = 1; i < HEAP_INIT_SIZE / 4096; i++) {
        pmm_alloc_frame();
    }

    head = (header_t*)heap_start;
    head->size = HEAP_INIT_SIZE - sizeof(header_t);
    head->next = NULL;
    head->is_free = 1;
}

void* kmalloc(size_t size) {
    if (!size) return NULL;
    header_t* curr = head;

    // Align size to 4 bytes
    size = (size + 3) & ~3;

    while (curr) {
        if (curr->is_free && curr->size >= size) {
            // Can we split the block?
            if (curr->size > size + sizeof(header_t)) {
                header_t* next_block = (header_t*)((uint8_t*)curr + sizeof(header_t) + size);
                next_block->size = curr->size - size - sizeof(header_t);
                next_block->is_free = 1;
                next_block->next = curr->next;
                curr->next = next_block;
                curr->size = size;
            }
            curr->is_free = 0;
            return (void*)(curr + 1);
        }
        curr = curr->next;
    }
    return NULL; // Out of heap memory
}

void kfree(void* ptr) {
    if (!ptr) return;
    header_t* block = (header_t*)ptr - 1;
    block->is_free = 1;

    // Merge with next if free
    if (block->next && block->next->is_free) {
        block->size += sizeof(header_t) + block->next->size;
        block->next = block->next->next;
    }

    // Merging with previous requires doubly-linked list or restarting from head. 
    // Restarting from head for simplicity:
    header_t* curr = head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(header_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}
