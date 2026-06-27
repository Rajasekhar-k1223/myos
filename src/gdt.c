#include "gdt.h"

#include "acpi.h"
#include "apic.h"
#include "kernel.h"

#define GDT_ENTRIES (5 + MAX_CORES)

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gp;

extern void gdt_flush(uint32_t);
extern void tss_flush(void);
extern void tss_flush_ap(uint16_t sel);
extern void tss_init(uint32_t core_idx, uint32_t gdt_num, uint32_t ss0, uint32_t esp0);

void gdt_set_entry_external(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access      = access;
}

static void gdt_set_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_set_entry_external(num, base, limit, access, gran);
}

void gdt_init(void) {
    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint32_t)&gdt;

    gdt_set_entry(0, 0, 0,          0x00, 0x00); /* null segment */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* kernel code */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* kernel data */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* user code */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* user data */
    
    // Initialize TSS for BSP at segment 5
    tss_init(bsp_apic_id, 5 + bsp_apic_id, 0x10, 0x90000);

    gdt_flush((uint32_t)&gp);
    
    // We can only load TR after gdt_flush. But tss_flush hardcodes 0x28 in asm.
    // Let's call a new asm function or just inline it:
    asm volatile("ltr %%ax" : : "a"((5 + bsp_apic_id) * 8));
}

void gdt_init_ap(void) {
    uint8_t id = apic_get_id();
    
    // Initialize TSS for this AP
    tss_init(id, 5 + id, 0x10, 0x90000); // 0x90000 is dummy, will be updated per-task
    
    // Load the shared GDT
    gdt_flush((uint32_t)&gp);
    
    // Load TR for this core
    asm volatile("ltr %%ax" : : "a"((5 + id) * 8));
}
