#include "smp.h"
#include "acpi.h"
#include "apic.h"
#include "kernel.h"
#include "string.h"
#include "paging.h"
#include "idt.h"
#include "pit.h"

#include "trampoline.h" // Generated from trampoline.asm
#include "task.h"

volatile int ap_count = 0;
volatile int tasking_ready = 0; /* Set by BSP after tasking_init() completes */

extern uint32_t* kernel_page_directory;

void ap_main(void) {
    // Boot order: Paging -> GDT -> IDT -> APIC -> Timer
    // The LAPIC (0xFEE00000) is pre-mapped in kernel_page_directory by paging_init().

    // 1. Enable paging — LAPIC is already in kernel_page_directory
    asm volatile("mov %0, %%cr3" : : "r"((uint32_t)kernel_page_directory));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    // 2. Load kernel GDT + TSS for this AP (apic_get_id() now safe, LAPIC is mapped)
    extern void gdt_init_ap(void);
    gdt_init_ap();

    // 3. Load shared IDT
    extern void idt_load_ap(void);
    idt_load_ap();

    // 4. Init this core's Local APIC (enable, set spurious vector)
    apic_init();

    // 5. Start APIC timer for scheduling
    apic_timer_init();
    
    // 6. Register this AP with the scheduler (current_tasks[id] = -1 before sti)
    tasking_init_ap();

    // 7. Wait for BSP to finish setting up the task table before enabling interrupts
    while (!tasking_ready) {
        asm volatile("pause");
    }

    // Increment the AP count
    __sync_fetch_and_add(&ap_count, 1);
    
    uint8_t id = apic_get_id();
    terminal_printf("[SMP] AP %d booted successfully!\n", id);
    
    // Enable interrupts — APIC timer will fire and call schedule()
    while (1) {
        asm volatile("sti; hlt");
    }
}

void smp_init(void) {
    if (num_cores <= 1) {
        terminal_printf("[SMP] No APs to boot.\n");
        return;
    }

    // The trampoline code needs to be at 0x8000
    uint8_t* trampoline_target = (uint8_t*)0x8000;
    
    // Copy the trampoline binary to 0x8000
    memcpy(trampoline_target, src_trampoline_bin, src_trampoline_bin_len);
    
    // Pointers to the variables inside the trampoline code
    // In our ASM, ap_stack is at offset: src_trampoline_bin_len - 8
    // ap_main_ptr is at offset: src_trampoline_bin_len - 4
    uint32_t* ap_stack = (uint32_t*)(0x8000 + src_trampoline_bin_len - 8);
    uint32_t* ap_main_ptr = (uint32_t*)(0x8000 + src_trampoline_bin_len - 4);
    
    *ap_main_ptr = (uint32_t)&ap_main;
    
    terminal_printf("[SMP] Booting %d APs...\n", num_cores - 1);
    
    for (int i = 0; i < num_cores; i++) {
        if (apic_ids[i] == bsp_apic_id) continue;
        
        // Allocate a new stack for the AP
        uint32_t stack = (uint32_t)pmm_alloc_frame();
        *ap_stack = stack + 4096; // Stack grows downwards
        
        // Send INIT
        apic_send_init(apic_ids[i]);
        
        // Send SIPI (Vector 0x08 -> 0x8000)
        apic_send_sipi(apic_ids[i], 0x08);
        
        terminal_printf("[SMP] Sent SIPI to AP %d, waiting for 20 ticks...\n", apic_ids[i]);
        // Short delay using a spinloop instead of PIT ticks
        // since LAPIC initialization might temporarily disrupt PIC interrupts
        for (volatile int delay = 0; delay < 2000000; delay++) {
            asm volatile("pause");
        }
        terminal_printf("\n[SMP] Finished waiting for AP %d.\n", apic_ids[i]);
    }
    
    terminal_printf("[SMP] %d APs active.\n", ap_count);
}
