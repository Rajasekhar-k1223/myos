#include "elf.h"
#include "tar.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "kernel.h"

uint32_t elf_load(const char* filename) {
    size_t file_size;
    const uint8_t* elf_data = (const uint8_t*)tar_get_file(filename, &file_size);
    
    if (!elf_data) {
        terminal_printf("  [ELF] File not found: %s\n", filename);
        return 0;
    }
    
    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    
    if (header->e_ident_mag != ELF_MAGIC) {
        terminal_printf("  [ELF] Invalid magic signature.\n");
        return 0;
    }
    
    if (header->e_machine != 3) { // EM_386
        terminal_printf("  [ELF] Unsupported architecture (not x86).\n");
        return 0;
    }
    
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(elf_data + header->e_phoff);
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint32_t vaddr = phdrs[i].p_vaddr;
            uint32_t memsz = phdrs[i].p_memsz;
            uint32_t filesz = phdrs[i].p_filesz;
            uint32_t offset = phdrs[i].p_offset;
            
            // Map physical pages to cover this segment
            uint32_t start_page = vaddr & ~0xFFF;
            uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
            
            for (uint32_t page = start_page; page < end_page; page += 4096) {
                void* frame = pmm_alloc_frame();
                if (!frame) {
                    terminal_printf("  [ELF] Out of physical memory.\n");
                    return 0;
                }
                memset(frame, 0, 4096);
                paging_map_page(page, (uint32_t)frame, 7); // User, R/W, Present
            }
            
            // Copy data from file
            if (filesz > 0) {
                memcpy((void*)vaddr, elf_data + offset, filesz);
            }
            
            // The remainder is BSS (uninitialized data), which is automatically 0
            // because we memset the allocated frames to 0.
        }
    }
    
    return header->e_entry;
}
