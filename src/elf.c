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

    /* Apply relocations to the library's own PLT GOT entries.
     * Since this is a shared object loaded at load_base (not 0),
     * all PLT stub addresses stored in .got.plt need +load_base. */
    Elf32_Dyn* dyn = 0;
    for (int i=0; i<hdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn = (Elf32_Dyn*)(lib_data + phdrs[i].p_offset);
            break;
        }
    }
    if (!dyn) return;

    uint32_t jmprel_off = 0, pltrelsz = 0;
    uint32_t pltgot_va = 0;
    for (Elf32_Dyn* d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_JMPREL)   jmprel_off = d->d_un.d_ptr;  /* file-relative VA */
        if (d->d_tag == DT_PLTRELSZ) pltrelsz   = d->d_un.d_val;
        if (d->d_tag == DT_PLTGOT)   pltgot_va  = d->d_un.d_ptr;  /* file-relative VA */
    }

    /* Patch .got.plt: GOT[0] = .dynamic VA+base, GOT[1]=0, GOT[2]=0,
     * GOT[3..] = initial PLT stub addresses (file-relative) + load_base */
    if (pltgot_va) {
        uint32_t* got = (uint32_t*)(load_base + pltgot_va);
        got[0] = load_base + got[0]; /* .dynamic VA */
        got[1] = 0;                   /* link_map (unused) */
        got[2] = 0;                   /* dl_resolve (unused) */
        /* Patch each PLT entry's initial GOT stub pointer */
        int count = (int)(pltrelsz / 8); /* each Elf32_Rel = 8 bytes */
        for (int i = 0; i < count; i++) {
            got[3 + i] = load_base + got[3 + i]; /* relocate stub addr */
        }
    }

    /* Now resolve the library's own PLT entries against itself (self-contained). */
    if (!jmprel_off || !pltrelsz) return;
    Elf32_Rel* rels = (Elf32_Rel*)(lib_data + jmprel_off);
    int count = (int)(pltrelsz / sizeof(Elf32_Rel));

    /* Build symtab/strtab pointers from the dynamic section */
    uint32_t symtab_off = 0, strtab_off = 0;
    for (Elf32_Dyn* d = dyn; d->d_tag != DT_NULL; d++) {
        if (d->d_tag == DT_SYMTAB) symtab_off = d->d_un.d_ptr;
        if (d->d_tag == DT_STRTAB) strtab_off = d->d_un.d_ptr;
    }
    if (!symtab_off || !strtab_off) return;

    Elf32_Sym* symtab = (Elf32_Sym*)(lib_data + symtab_off);
    const char* strtab = (const char*)(lib_data + strtab_off);

    for (int i = 0; i < count; i++) {
        uint32_t sym_idx = ELF32_R_SYM(rels[i].r_info);
        uint32_t sym_val = symtab[sym_idx].st_value;
        if (sym_val) {
            /* Symbol is defined within the library — patch to load_base + sym_val */
            uint32_t* target = (uint32_t*)(load_base + rels[i].r_offset);
            *target = load_base + sym_val;
            (void)strtab; /* strtab available for debugging if needed */
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
    extern void com1_print(const char*);
    extern void com1_print_hex(uint32_t);

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
    com1_print("[ELF] libc.so loaded at 0x20000000\n");

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

    com1_print("[ELF] symtab="); com1_print_hex((uint32_t)symtab);
    com1_print(" strtab="); com1_print_hex((uint32_t)strtab);
    com1_print(" jmprel="); com1_print_hex((uint32_t)jmprel);
    com1_print(" pltrelsz="); com1_print_hex(pltrelsz); com1_print("\n");

    if (jmprel && symtab && strtab) {
        int count = pltrelsz / sizeof(Elf32_Rel);
        for (int i=0; i<count; i++) {
            uint32_t sym_idx = ELF32_R_SYM(jmprel[i].r_info);
            const char* name = strtab + symtab[sym_idx].st_name;
            uint32_t addr = find_sym_in_lib("libc.so", name, libc_base);
            com1_print("[ELF] PLT["); com1_print_hex(i);
            com1_print("] offset="); com1_print_hex(jmprel[i].r_offset);
            com1_print(" sym="); com1_print(name ? name : "?");
            com1_print(" -> "); com1_print_hex(addr); com1_print("\n");
            if (addr) {
                uint32_t* target = (uint32_t*)jmprel[i].r_offset; // Virtual address
                *target = addr;
            }
        }
    }
}


int elf_load_and_run(const char* filename, const char* args_str) {
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

    extern uint32_t* paging_get_current_dir();
    extern uint32_t* paging_clone_directory(void);
    extern void paging_switch_directory(uint32_t* dir);
    
    uint32_t* old_dir = paging_get_current_dir();
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

    // --- Parse args and push to stack ---
    uint32_t sp = user_stack_top;
    uint32_t argv[32];
    int argc = 0;
    
    // Push filename
    sp -= strlen(filename) + 1;
    strcpy((char*)sp, filename);
    argv[argc++] = sp;
    
    if (args_str) {
        char buf[256];
        strncpy(buf, args_str, 255);
        buf[255] = '\0';
        char* p = buf;
        while (*p && argc < 31) {
            while (*p == ' ') p++;
            if (!*p) break;
            char* start = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
            sp -= strlen(start) + 1;
            strcpy((char*)sp, start);
            argv[argc++] = sp;
        }
    }
    argv[argc] = 0;
    sp &= ~3;
    sp -= (argc + 1) * 4;
    uint32_t argv_ptr = sp;
    memcpy((void*)sp, argv, (argc + 1) * 4);
    sp -= 4;
    *(uint32_t*)sp = argv_ptr;
    sp -= 4;
    *(uint32_t*)sp = argc;
    
    user_stack_top = sp; // Updated stack top for task_create_user
    
    // Switch back to kernel directory so we don't crash before schedule
    paging_switch_directory(old_dir);

    extern int task_create_user(const char* name, uint32_t entry, uint32_t user_stack_top, uint32_t* page_directory);
    int pid = task_create_user(filename, header->e_entry, user_stack_top, new_dir);
    terminal_printf("  [ELF] Spawning Ring 3 Task for %s (PID %d)...\n", filename, pid);
    return pid;
}

int task_exec(const char* filename, const char* args_str, struct registers* regs) {
    size_t file_size;
    const uint8_t* elf_data = (const uint8_t*)tar_get_file(filename, &file_size);
    if (!elf_data) return -1;
    
    Elf32_Ehdr* header = (Elf32_Ehdr*)elf_data;
    if (header->e_ident_mag != ELF_MAGIC || header->e_machine != 3) return -1;

    extern uint32_t* paging_get_current_dir();
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
    
    // --- Parse args and push to stack ---
    uint32_t sp = user_stack_top;
    uint32_t argv[32];
    int argc = 0;
    
    // Push filename
    sp -= strlen(filename) + 1;
    strcpy((char*)sp, filename);
    argv[argc++] = sp;
    
    if (args_str) {
        char buf[256];
        strncpy(buf, args_str, 255);
        buf[255] = '\0';
        char* p = buf;
        while (*p && argc < 31) {
            while (*p == ' ') p++;
            if (!*p) break;
            char* start = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
            sp -= strlen(start) + 1;
            strcpy((char*)sp, start);
            argv[argc++] = sp;
        }
    }
    argv[argc] = 0;
    
    // Align SP
    sp &= ~3;
    
    // Push argv array
    sp -= (argc + 1) * 4;
    uint32_t argv_ptr = sp;
    memcpy((void*)sp, argv, (argc + 1) * 4);
    
    // Push argv pointer
    sp -= 4;
    *(uint32_t*)sp = argv_ptr;
    
    // Push argc
    sp -= 4;
    *(uint32_t*)sp = argc;
    
    // Modify the registers pushed by the interrupt handler
    regs->eip = header->e_entry;
    regs->useresp = sp;
    
    return 0;
}
