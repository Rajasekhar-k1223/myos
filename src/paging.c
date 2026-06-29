#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "kernel.h"
#include "apic.h"
#include "idt.h"

// 1024 entries * 4 bytes = 4096 bytes (1 frame)
uint32_t* kernel_page_directory;
uint32_t* current_page_directory;

/* COW reference counts — indexed by physical frame number (phys >> 12) */
static uint8_t cow_refs[1024 * 1024];

void cow_inc(uint32_t phys_addr) {
    uint32_t frame = phys_addr >> 12;
    if (frame < 1024 * 1024 && cow_refs[frame] < 255) cow_refs[frame]++;
}
void cow_dec(uint32_t phys_addr) {
    uint32_t frame = phys_addr >> 12;
    if (frame >= 1024 * 1024) return;
    if (cow_refs[frame] == 0) return;
    if (--cow_refs[frame] == 0) pmm_free_frame((void*)phys_addr);
}
int cow_count(uint32_t phys_addr) {
    uint32_t frame = phys_addr >> 12;
    return (frame < 1024 * 1024) ? (int)cow_refs[frame] : 0;
}


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

    /* Identity map the first 256 MB of memory (kernel + DMA buffers) */
    for (uint32_t i = 0; i < 0x10000000; i += 4096) {
        paging_map_page(i, i, 3); /* Present + Read/Write + Supervisor */
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

    // Identity map the LAPIC MMIO region (0xFEE00000, 1 page)
    // This must be done here so APs can call apic_get_id() after loading kernel CR3
    paging_map_page(0xFEE00000, 0xFEE00000, 3);

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

    for (int i = 0; i < 1024; i++) {
        if (kernel_page_directory[i] & 1) {
            dir[i] = kernel_page_directory[i]; // share kernel pages
        } else if (current_page_directory[i] & 1) {
            // User page table — COW: share pages read-only
            uint32_t* pt_child = (uint32_t*)pmm_alloc_frame();
            memset(pt_child, 0, 4096);
            dir[i] = (uint32_t)pt_child | (current_page_directory[i] & 0xFFF);

            uint32_t* src_pt = (uint32_t*)(current_page_directory[i] & ~0xFFF);
            for (int j = 0; j < 1024; j++) {
                if (src_pt[j] & 1) {
                    uint32_t pte  = src_pt[j];
                    uint32_t phys = pte & ~0xFFF;
                    src_pt[j]   = pte & ~2u;  // parent: read-only
                    pt_child[j] = pte & ~2u;  // child:  read-only
                    cow_inc(phys);
                }
            }
            // Flush TLB for parent (pages are now read-only)
            asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");
        }
    }
    /* Shootdown TLB on all other SMP cores — they may have cached the
     * now-read-only parent PTEs as writable, bypassing COW protection. */
    apic_send_tlb_shootdown();
    return dir;
}

/* IPI handler for TLB shootdown (vector 0x3E) — flushes local TLB and ACKs. */
static void tlb_shootdown_handler(struct registers* r) {
    (void)r;
    asm volatile("mov %%cr3, %%eax; mov %%eax, %%cr3" ::: "eax", "memory");
    apic_eoi();
}

void paging_register_tlb_handler(void) {
    register_interrupt_handler(0x3E, tlb_shootdown_handler);
}

void paging_switch_directory(uint32_t* dir) {
    current_page_directory = dir;
    asm volatile("mov %0, %%cr3" :: "r"(dir));
}

/* Page fault handler — hooked from ISR 14 */
void paging_page_fault_handler(uint32_t fault_addr, uint32_t error_code) {
    extern void task_exit(void);

    /* Check for swap-backed page (present=0, bit9=1) */
    {
        uint32_t pde_idx = fault_addr >> 22;
        uint32_t pte_idx = (fault_addr >> 12) & 0x3FF;
        uint32_t* pd = current_page_directory;
        if ((pd[pde_idx] & 1) && !(error_code & 1)) {
            uint32_t* pt = (uint32_t*)(pd[pde_idx] & ~0xFFF);
            uint32_t pte = pt[pte_idx];
            if (!(pte & 1) && (pte & (1 << 9))) {
                extern void swap_handle_fault(uint32_t);
                swap_handle_fault(fault_addr);
                return;
            }
        }
    }

    if (!(error_code & 2)) {
        terminal_printf("[PAGING] Read fault at 0x%x (err=0x%x) — killing task\n",
                        fault_addr, error_code);
        task_exit(); return;
    }

    /* Write fault — COW handling */
    uint32_t pde_idx = fault_addr >> 22;
    uint32_t pte_idx = (fault_addr >> 12) & 0x3FF;
    uint32_t* pd = current_page_directory;

    if (!(pd[pde_idx] & 1)) { task_exit(); return; }
    uint32_t* pt = (uint32_t*)(pd[pde_idx] & ~0xFFF);
    uint32_t pte  = pt[pte_idx];
    if (!(pte & 1))           { task_exit(); return; }

    uint32_t phys = pte & ~0xFFF;
    uint32_t frame = phys >> 12;
    if (frame >= 1024u * 1024u) { task_exit(); return; }

    if (cow_refs[frame] > 1) {
        uint32_t new_phys = (uint32_t)pmm_alloc_frame();
        if (!new_phys) { task_exit(); return; }
        uint32_t new_frame = new_phys >> 12;
        if (new_frame >= 1024u * 1024u) { task_exit(); return; }
        memcpy((void*)new_phys, (void*)phys, 4096);
        cow_dec(phys);
        pt[pte_idx] = new_phys | (pte & 0xFFF) | 2;
        cow_refs[new_frame] = 1;
        asm volatile("invlpg (%0)" :: "r"(fault_addr) : "memory");
    } else {
        pt[pte_idx] = pte | 2;
        asm volatile("invlpg (%0)" :: "r"(fault_addr) : "memory");
    }
}
