#include "tss.h"
#include "gdt.h"
#include "string.h"

static struct tss_entry_struct tss_entry;

extern void gdt_set_entry_external(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

void tss_init(uint32_t num, uint32_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry) - 1;

    gdt_set_entry_external(num, base, limit, 0xE9, 0x00);

    memset(&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;
    
    // Here we set the cs, ss, ds, es, fs and gs entries in the TSS.
    // They specify what segments should be loaded when the processor switches to kernel mode.
    // 0x08 = kernel code, 0x10 = kernel data. We set everything to kernel data except CS.
    tss_entry.cs = 0x0b; 
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x13; 
    tss_entry.iomap_base = sizeof(tss_entry);
}

void tss_set_stack(uint32_t ss0, uint32_t esp0) {
    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;
}
