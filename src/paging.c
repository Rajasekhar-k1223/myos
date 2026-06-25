#include "paging.h"
#include "pmm.h"
#include "string.h"

// 1024 entries * 4 bytes = 4096 bytes (1 frame)
static uint32_t* page_directory;


void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdindex = virt >> 22;
    uint32_t ptindex = virt >> 12 & 0x03FF;

    if (!(page_directory[pdindex] & 1)) {
        // Page table not present, allocate one
        uint32_t* pt = (uint32_t*)pmm_alloc_frame();
        memset(pt, 0, 4096);
        page_directory[pdindex] = (uint32_t)pt | 3; // Present, Read/Write
    }

    uint32_t* pt = (uint32_t*)(page_directory[pdindex] & ~0xFFF);
    pt[ptindex] = phys | (flags & 0xFFF) | 1; // Present
}

void paging_init(void) {
    page_directory = (uint32_t*)pmm_alloc_frame();
    memset(page_directory, 0, 4096);

    // Identity map the first 16MB of memory
    for (uint32_t i = 0; i < 0x1000000; i += 4096) {
        paging_map_page(i, i, 3); // 3 = Present + Read/Write
    }

    // Assembly function to set CR3 and enable CR0 paging bit
    // We will define this inline here for simplicity
    asm volatile("mov %0, %%cr3" :: "r"(page_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}
