#include "paging.h"
#include "pmm.h"
#include "string.h"

// 1024 entries * 4 bytes = 4096 bytes (1 frame)
uint32_t* kernel_page_directory;
uint32_t* current_page_directory;


void paging_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdindex = virt >> 22;
    uint32_t ptindex = virt >> 12 & 0x03FF;

    if (!(current_page_directory[pdindex] & 1)) {
        // Page table not present, allocate one
        uint32_t* pt = (uint32_t*)pmm_alloc_frame();
        memset(pt, 0, 4096);
        current_page_directory[pdindex] = (uint32_t)pt | 7; // Present, Read/Write, User
    }

    uint32_t* pt = (uint32_t*)(current_page_directory[pdindex] & ~0xFFF);
    pt[ptindex] = phys | (flags & 0xFFF) | 1; // Present
}

void paging_init(void) {
    kernel_page_directory = (uint32_t*)pmm_alloc_frame();
    memset(kernel_page_directory, 0, 4096);
    current_page_directory = kernel_page_directory;

    // Identity map the first 16MB of memory (Kernel space)
    for (uint32_t i = 0; i < 0x1000000; i += 4096) {
        paging_map_page(i, i, 3); // 3 = Present + Read/Write + Supervisor Only!
    }

    // Identity map the VESA Framebuffer
    extern uint32_t vesa_get_fb_addr(void);
    extern uint32_t vesa_get_fb_size(void);
    uint32_t fb_addr = vesa_get_fb_addr() & ~0xFFF;
    uint32_t fb_size = vesa_get_fb_size();
    if (fb_addr != 0) {
        for (uint32_t i = 0; i < fb_size + 4096; i += 4096) {
            paging_map_page(fb_addr + i, fb_addr + i, 3); // Supervisor, R/W
        }
    }

    // Assembly function to set CR3 and enable CR0 paging bit
    asm volatile("mov %0, %%cr3" :: "r"(current_page_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Enable paging!
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
}

uint32_t* paging_clone_directory(void) {
    uint32_t* dir = (uint32_t*)pmm_alloc_frame();
    memset(dir, 0, 4096);
    
    // Copy the kernel's page tables (0 - 16MB and VESA framebuffers)
    // The kernel mappings must be kept identical across all processes!
    for (int i = 0; i < 1024; i++) {
        // Just link to the kernel's page tables directly
        // Because they are supervisor only, user apps can't access them anyway
        if (kernel_page_directory[i] & 1) {
            dir[i] = kernel_page_directory[i];
        }
    }
    return dir;
}

void paging_switch_directory(uint32_t* dir) {
    current_page_directory = dir;
    asm volatile("mov %0, %%cr3" :: "r"(dir));
}
