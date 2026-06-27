#include "tss.h"
#include "gdt.h"
#include "string.h"

#include "acpi.h"

static struct tss_entry_struct tss_entries[MAX_CORES];

extern void gdt_set_entry_external(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

void tss_init(uint32_t core_idx, uint32_t gdt_num, uint32_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entries[core_idx];
    uint32_t limit = sizeof(struct tss_entry_struct) - 1;

    gdt_set_entry_external(gdt_num, base, limit, 0xE9, 0x00);

    memset(&tss_entries[core_idx], 0, sizeof(struct tss_entry_struct));

    tss_entries[core_idx].ss0 = ss0;
    tss_entries[core_idx].esp0 = esp0;
    
    // Here we set the cs, ss, ds, es, fs and gs entries in the TSS.
    // They specify what segments should be loaded when the processor switches to kernel mode.
    // 0x08 = kernel code, 0x10 = kernel data. We set everything to kernel data except CS.
    tss_entries[core_idx].cs = 0x0b; 
    tss_entries[core_idx].ss = tss_entries[core_idx].ds = tss_entries[core_idx].es = tss_entries[core_idx].fs = tss_entries[core_idx].gs = 0x13; 
    tss_entries[core_idx].iomap_base = sizeof(struct tss_entry_struct);
}

void tss_set_stack(uint32_t core_idx, uint32_t ss0, uint32_t esp0) {
    tss_entries[core_idx].ss0 = ss0;
    tss_entries[core_idx].esp0 = esp0;
}
