#include "kheap.h"
#include "pmm.h"
#include "string.h"

/*
 * Best-fit free list heap with forward+backward coalescing.
 * 8-byte aligned allocations; doubly-linked block list for O(1) coalesce.
 */
typedef struct header {
    size_t         size;    /* usable bytes (excludes this header) */
    struct header* prev;
    struct header* next;
    int            is_free;
    uint32_t       magic;   /* 0xDEADBEEF when alive */
} header_t;

#define MAGIC_ALIVE 0xDEADBEEF
#define ALIGN8(x)   (((x) + 7) & ~7u)

static header_t* head = NULL;
#define HEAP_INIT_SIZE (32 * 1024 * 1024)

void kheap_init(void) {
    void* heap_start = pmm_alloc_frame();
    for (int i = 1; i < HEAP_INIT_SIZE / 4096; i++)
        pmm_alloc_frame();

    head            = (header_t*)heap_start;
    head->size      = HEAP_INIT_SIZE - sizeof(header_t);
    head->prev      = NULL;
    head->next      = NULL;
    head->is_free   = 1;
    head->magic     = MAGIC_ALIVE;
}

void* kmalloc(size_t size) {
    if (!size) return NULL;
    size = ALIGN8(size);

    /* Best-fit: find the smallest free block that fits */
    header_t* best = NULL;
    for (header_t* b = head; b; b = b->next) {
        if (b->magic != MAGIC_ALIVE) break; /* heap corrupt */
        if (b->is_free && b->size >= size) {
            if (!best || b->size < best->size)
                best = b;
            if (best->size == size) break; /* perfect fit, stop early */
        }
    }
    if (!best) return NULL;

    /* Split if enough room for a new header + at least 8 bytes */
    if (best->size > size + sizeof(header_t) + 8) {
        header_t* split = (header_t*)((uint8_t*)best + sizeof(header_t) + size);
        split->size    = best->size - size - sizeof(header_t);
        split->is_free = 1;
        split->magic   = MAGIC_ALIVE;
        split->prev    = best;
        split->next    = best->next;
        if (best->next) best->next->prev = split;
        best->next = split;
        best->size = size;
    }
    best->is_free = 0;
    return (void*)(best + 1);
}

void kfree(void* ptr) {
    if (!ptr) return;
    header_t* b = (header_t*)ptr - 1;
    if (b->magic != MAGIC_ALIVE) return; /* double-free / corrupt guard */
    b->is_free = 1;

    /* Coalesce with next */
    if (b->next && b->next->is_free && b->next->magic == MAGIC_ALIVE) {
        header_t* nx = b->next;
        b->size += sizeof(header_t) + nx->size;
        b->next  = nx->next;
        if (nx->next) nx->next->prev = b;
        nx->magic = 0; /* invalidate merged block */
    }

    /* Coalesce with previous */
    if (b->prev && b->prev->is_free && b->prev->magic == MAGIC_ALIVE) {
        header_t* pv = b->prev;
        pv->size += sizeof(header_t) + b->size;
        pv->next  = b->next;
        if (b->next) b->next->prev = pv;
        b->magic = 0;
    }
}

void* krealloc(void* ptr, size_t newsize) {
    if (!ptr) return kmalloc(newsize);
    if (!newsize) { kfree(ptr); return NULL; }

    header_t* b = (header_t*)ptr - 1;
    newsize = ALIGN8(newsize);

    /* Already fits in current block */
    if (b->size >= newsize) return ptr;

    /* Try to absorb the next free block */
    if (b->next && b->next->is_free &&
        b->size + sizeof(header_t) + b->next->size >= newsize) {
        header_t* nx = b->next;
        b->size += sizeof(header_t) + nx->size;
        b->next  = nx->next;
        if (nx->next) nx->next->prev = b;
        nx->magic = 0;
        /* Re-split if oversized */
        if (b->size > newsize + sizeof(header_t) + 8) {
            header_t* split = (header_t*)((uint8_t*)b + sizeof(header_t) + newsize);
            split->size    = b->size - newsize - sizeof(header_t);
            split->is_free = 1;
            split->magic   = MAGIC_ALIVE;
            split->prev    = b;
            split->next    = b->next;
            if (b->next) b->next->prev = split;
            b->next = split;
            b->size = newsize;
        }
        return ptr;
    }

    void* n = kmalloc(newsize);
    if (!n) return NULL;
    memcpy(n, ptr, b->size);
    kfree(ptr);
    return n;
}

void kheap_stats(uint32_t* used_out, uint32_t* free_out, uint32_t* blocks_out) {
    uint32_t used = 0, free = 0, blks = 0;
    for (header_t* b = head; b && b->magic == MAGIC_ALIVE; b = b->next) {
        blks++;
        if (b->is_free) free += (uint32_t)b->size;
        else            used += (uint32_t)b->size;
    }
    if (used_out)   *used_out   = used;
    if (free_out)   *free_out   = free;
    if (blocks_out) *blocks_out = blks;
}
