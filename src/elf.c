#include "elf.h"
#include "tar.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "kernel.h"

void elf_load_and_run(const char* filename) {
    size_t file_size;
    const uint8_t* elf_data = (const uint8_t*)tar_get_file(filename, &file_size);
    
    if (!elf_data) {
        terminal_printf("  [ELF] File not found: %s\n", filename);
        return;
    }
    
    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    
    if (header->e_ident_mag != ELF_MAGIC) {
        terminal_printf("  [ELF] Invalid magic signature.\n");
        return;
    }
    
    if (header->e_machine != 3) { // EM_386
        terminal_printf("  [ELF] Unsupported architecture (not x86).\n");
        return;
    }

    extern uint32_t* current_page_directory;
    extern uint32_t* paging_clone_directory(void);
    extern void paging_switch_directory(uint32_t* dir);
    
    uint32_t* old_dir = current_page_directory;
    uint32_t* new_dir = paging_clone_directory();
    paging_switch_directory(new_dir);
    
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
                    paging_switch_directory(old_dir);
                    return;
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
    
    // Allocate User Stack (8KB at 0xB0000000)
    uint32_t user_stack_top = 0xB0000000;
    for (uint32_t p = user_stack_top - 8192; p < user_stack_top; p += 4096) {
        void* frame = pmm_alloc_frame();
        memset(frame, 0, 4096);
        paging_map_page(p, (uint32_t)frame, 7); // User, R/W
    }

    // Switch back to kernel directory so we don't crash before schedule
    paging_switch_directory(old_dir);

    extern int task_create_user(const char* name, uint32_t entry, uint32_t user_stack_top, uint32_t* page_directory);
    task_create_user(filename, header->e_entry, user_stack_top, new_dir);
    terminal_printf("  [ELF] Spawning Ring 3 Task for %s...\n", filename);
}
