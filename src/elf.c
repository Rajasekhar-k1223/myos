#include "elf.h"
#include "tar.h"
#include "paging.h"
#include "pmm.h"
#include "string.h"
#include "kernel.h"
#include "task.h"

// ─── Dynamic Linking Helpers ───────────────────────────────────────────────────
static void load_shared_library(const char* libname, uint32_t load_base) {
    size_t sz;
    const uint8_t* lib_data = (const uint8_t*)tar_get_file(libname, &sz);
    if (!lib_data) return;
    Elf32_Ehdr* hdr = (Elf32_Ehdr*)lib_data;
    if (hdr->e_ident_mag != ELF_MAGIC) return;
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(lib_data + hdr->e_phoff);
    for (int i=0; i<hdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint32_t vaddr = phdrs[i].p_vaddr + load_base;
            uint32_t memsz = phdrs[i].p_memsz;
            uint32_t filesz = phdrs[i].p_filesz;
            uint32_t offset = phdrs[i].p_offset;
            uint32_t start_page = vaddr & ~0xFFF;
            uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
            for (uint32_t page = start_page; page < end_page; page += 4096) {
                void* frame = pmm_alloc_frame();
                memset(frame, 0, 4096);
                paging_map_page(page, (uint32_t)frame, 7);
            }
            if (filesz > 0 && offset + filesz <= sz && filesz <= memsz)
                memcpy((void*)vaddr, lib_data + offset, filesz);
        }
    }
}

static uint32_t find_sym_in_lib(const char* libname, const char* sym_name, uint32_t load_base) {
    size_t sz;
    const uint8_t* lib_data = (const uint8_t*)tar_get_file(libname, &sz);
    if (!lib_data) return 0;
    Elf32_Ehdr* hdr = (Elf32_Ehdr*)lib_data;
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(lib_data + hdr->e_phoff);
    Elf32_Dyn* dyn = 0;
    for (int i=0; i<hdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn = (Elf32_Dyn*)(lib_data + phdrs[i].p_offset);
            break;
        }
    }
    if (!dyn) return 0;
    Elf32_Sym* symtab = 0;
    const char* strtab = 0;
    for (; dyn->d_tag != DT_NULL; dyn++) {
        if (dyn->d_tag == DT_SYMTAB) symtab = (Elf32_Sym*)(lib_data + dyn->d_un.d_ptr);
        if (dyn->d_tag == DT_STRTAB) strtab = (const char*)(lib_data + dyn->d_un.d_ptr);
    }
    if (!symtab || !strtab) return 0;
    for (int i=1; i<500; i++) { // Simple bounded linear search
        if (symtab[i].st_name) {
            if (strcmp(strtab + symtab[i].st_name, sym_name) == 0) {
                return load_base + symtab[i].st_value;
            }
        }
    }
    return 0;
}

static void resolve_dynamic_links(const uint8_t* elf_data) {
    Elf32_Ehdr* hdr = (Elf32_Ehdr*)elf_data;
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(elf_data + hdr->e_phoff);
    Elf32_Dyn* dyn = 0;
    for (int i=0; i<hdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            // For non-PIE executable, p_offset points to the data in the file
            dyn = (Elf32_Dyn*)(elf_data + phdrs[i].p_offset);
            break;
        }
    }
    if (!dyn) return;

    // We found a dynamic section! Let's load our shared library.
    uint32_t libc_base = 0x20000000;
    load_shared_library("libc.so", libc_base);

    Elf32_Sym* symtab = 0;
    const char* strtab = 0;
    Elf32_Rel* jmprel = 0;
    uint32_t pltrelsz = 0;

    for (; dyn->d_tag != DT_NULL; dyn++) {
        // Since we already mapped the executable into memory, d_ptr is a virtual address
        if (dyn->d_tag == DT_SYMTAB) symtab = (Elf32_Sym*)dyn->d_un.d_ptr;
        if (dyn->d_tag == DT_STRTAB) strtab = (const char*)dyn->d_un.d_ptr;
        if (dyn->d_tag == DT_JMPREL) jmprel = (Elf32_Rel*)dyn->d_un.d_ptr;
        if (dyn->d_tag == DT_PLTRELSZ) pltrelsz = dyn->d_un.d_val;
    }

    if (jmprel && symtab && strtab) {
        int count = pltrelsz / sizeof(Elf32_Rel);
        for (int i=0; i<count; i++) {
            uint32_t sym_idx = ELF32_R_SYM(jmprel[i].r_info);
            const char* name = strtab + symtab[sym_idx].st_name;
            uint32_t addr = find_sym_in_lib("libc.so", name, libc_base);
            if (addr) {
                uint32_t* target = (uint32_t*)jmprel[i].r_offset; // Virtual address
                *target = addr;
            }
        }
    }
}


int elf_load_and_run(const char* filename) {
    size_t file_size;
    const uint8_t* elf_data = (const uint8_t*)tar_get_file(filename, &file_size);
    
    if (!elf_data) {
        terminal_printf("  [ELF] File not found: %s\n", filename);
        return -1;
    }
    
    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    
    if (header->e_ident_mag != ELF_MAGIC) {
        terminal_printf("  [ELF] Invalid magic signature.\n");
        return -1;
    }
    
    if (header->e_machine != 3) { // EM_386
        terminal_printf("  [ELF] Unsupported architecture (not x86).\n");
        return -1;
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
            uint32_t start_page = vaddr & ~0xFFF;
            uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
            for (uint32_t page = start_page; page < end_page; page += 4096) {
                void* frame = pmm_alloc_frame();
                if (!frame) {
                    terminal_printf("  [ELF] Out of physical memory.\n");
                    paging_switch_directory(old_dir);
                    return -1;
                }
                memset(frame, 0, 4096);
                paging_map_page(page, (uint32_t)frame, 7);
            }
            if (filesz > 0) {
                if (offset + filesz > file_size || filesz > memsz) {
                    terminal_printf("  [ELF] Segment bounds invalid.\n");
                    paging_switch_directory(old_dir);
                    return -1;
                }
                memcpy((void*)vaddr, elf_data + offset, filesz);
            }
        } else if (phdrs[i].p_type == 7) { /* PT_TLS */
            uint32_t tls_size  = phdrs[i].p_memsz;
            uint32_t tls_fsz   = phdrs[i].p_filesz;
            uint32_t tls_off   = phdrs[i].p_offset;
            uint32_t tls_vaddr = 0xD0000000;
            for (uint32_t pg = 0; pg < tls_size; pg += 4096) {
                void* pf = pmm_alloc_frame();
                memset(pf, 0, 4096);
                paging_map_page(tls_vaddr + pg, (uint32_t)pf, 7);
            }
            if (tls_fsz > 0 && tls_off + tls_fsz <= file_size && tls_fsz <= tls_size)
                memcpy((void*)tls_vaddr, elf_data + tls_off, tls_fsz);
            extern task_t* task_current(void);
            task_t* cur = task_current();
            if (cur) cur->tls_base = tls_vaddr;
        }
    }
    
    // Allocate User Stack (8KB at 0xB0000000)
    uint32_t user_stack_top = 0xB0000000;
    for (uint32_t p = user_stack_top - 8192; p < user_stack_top; p += 4096) {
        void* frame = pmm_alloc_frame();
        memset(frame, 0, 4096);
        paging_map_page(p, (uint32_t)frame, 7); // User, R/W
    }

    // Now that the executable is mapped into the new page directory, resolve dynamic links
    resolve_dynamic_links(elf_data);

    // Switch back to kernel directory so we don't crash before schedule
    paging_switch_directory(old_dir);

    extern int task_create_user(const char* name, uint32_t entry, uint32_t user_stack_top, uint32_t* page_directory);
    int pid = task_create_user(filename, header->e_entry, user_stack_top, new_dir);
    terminal_printf("  [ELF] Spawning Ring 3 Task for %s (PID %d)...\n", filename, pid);
    return pid;
}

int task_exec(const char* filename, struct registers* regs) {
    size_t file_size;
    const uint8_t* elf_data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!elf_data) return -1;
    
    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    if (header->e_ident_mag != ELF_MAGIC || header->e_machine != 3) return -1;

    extern uint32_t* current_page_directory;
    extern uint32_t* paging_clone_directory(void);
    extern void paging_switch_directory(uint32_t* dir);
    
    uint32_t* new_dir = paging_clone_directory();
    paging_switch_directory(new_dir);
    
    Elf32_Phdr* phdrs = (Elf32_Phdr*)(elf_data + header->e_phoff);
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            uint32_t vaddr = phdrs[i].p_vaddr;
            uint32_t memsz = phdrs[i].p_memsz;
            uint32_t filesz = phdrs[i].p_filesz;
            uint32_t offset = phdrs[i].p_offset;
            
            uint32_t start_page = vaddr & ~0xFFF;
            uint32_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFF;
            for (uint32_t page = start_page; page < end_page; page += 4096) {
                void* frame = pmm_alloc_frame();
                memset(frame, 0, 4096);
                paging_map_page(page, (uint32_t)frame, 7);
            }
            if (filesz > 0 && offset + filesz <= file_size && filesz <= memsz)
                memcpy((void*)vaddr, elf_data + offset, filesz);
        }
    }

    uint32_t user_stack_top = 0xB0000000;
    for (uint32_t p = user_stack_top - 8192; p < user_stack_top; p += 4096) {
        void* frame = pmm_alloc_frame();
        memset(frame, 0, 4096);
        paging_map_page(p, (uint32_t)frame, 7);
    }

    // Resolve dynamic links in the exec environment
    resolve_dynamic_links(elf_data);

    task_t* cur = task_current();
    cur->page_directory = new_dir;
    
    // Modify the registers pushed by the interrupt handler
    // so when it returns, it jumps into the new ELF!
    regs->eip = header->e_entry;
    regs->useresp = user_stack_top;
    
    return 0;
}
