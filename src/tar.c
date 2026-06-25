#include "tar.h"
#include "kernel.h"
#include "string.h"

// Standard TAR header (512 bytes)
typedef struct {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
} tar_header_t;

static uint32_t tar_address = 0;

static uint32_t parse_octal(const char* ptr, int size) {
    uint32_t value = 0;
    for (int i = 0; i < size && ptr[i] != '\0' && ptr[i] != ' '; i++) {
        if (ptr[i] >= '0' && ptr[i] <= '7') {
            value = (value * 8) + (ptr[i] - '0');
        }
    }
    return value;
}

void tar_init(uint32_t address) {
    tar_address = address;
}

void tar_ls(void) {
    if (!tar_address) {
        terminal_writestring("No virtual pendrive (RAM Disk) found.\n");
        return;
    }

    uint32_t current_address = tar_address;
    
    while (1) {
        tar_header_t* header = (tar_header_t*)current_address;
        
        // If name is empty, we reached the end of the archive
        if (header->name[0] == '\0') {
            break;
        }

        uint32_t size = parse_octal(header->size, 11);
        
        // typeflag '0' or '\0' is a normal file
        // '5' is a directory
        if (header->typeflag == '0' || header->typeflag == '\0') {
            terminal_writestring(header->name);
            terminal_writestring("\n");
        }
        
        // Advance to the next header
        // Data blocks are padded to 512 bytes
        uint32_t data_blocks = (size + 511) / 512;
        current_address += 512 + (data_blocks * 512);
    }
}

void tar_cat(const char* filename) {
    if (!tar_address) {
        terminal_writestring("No virtual pendrive (RAM Disk) found.\n");
        return;
    }

    uint32_t current_address = tar_address;
    
    while (1) {
        tar_header_t* header = (tar_header_t*)current_address;
        
        if (header->name[0] == '\0') {
            break;
        }

        uint32_t size = parse_octal(header->size, 11);
        
        if ((header->typeflag == '0' || header->typeflag == '\0') && 
            strcmp(header->name, filename) == 0) {
            
            // Print file contents
            char* data = (char*)(current_address + 512);
            for (uint32_t i = 0; i < size; i++) {
                terminal_putchar(data[i]);
            }
            terminal_writestring("\n");
            return;
        }
        
        uint32_t data_blocks = (size + 511) / 512;
        current_address += 512 + (data_blocks * 512);
    }

    terminal_writestring("File not found: ");
    terminal_writestring(filename);
    terminal_writestring("\n");
}

void* tar_get_file(const char* filename, size_t* out_size) {
    if (!tar_address) return NULL;

    uint32_t current_address = tar_address;
    
    while (1) {
        tar_header_t* header = (tar_header_t*)current_address;
        if (header->name[0] == '\0') break;

        uint32_t size = parse_octal(header->size, 11);
        
        if ((header->typeflag == '0' || header->typeflag == '\0') && 
            strcmp(header->name, filename) == 0) {
            
            if (out_size) *out_size = size;
            return (void*)(current_address + 512);
        }
        
        uint32_t data_blocks = (size + 511) / 512;
        current_address += 512 + (data_blocks * 512);
    }

    return NULL;
}

int tar_get_file_at_index(int index, char* out_name) {
    if (!tar_address) return 0;

    uint32_t current_address = tar_address;
    int current_idx = 0;
    
    while (1) {
        tar_header_t* header = (tar_header_t*)current_address;
        if (header->name[0] == '\0') break;

        uint32_t size = parse_octal(header->size, 11);
        
        if (header->typeflag == '0' || header->typeflag == '\0') {
            if (current_idx == index) {
                if (out_name) {
                    strncpy(out_name, header->name, 99);
                    out_name[99] = '\0';
                }
                return 1;
            }
            current_idx++;
        }
        
        uint32_t data_blocks = (size + 511) / 512;
        current_address += 512 + (data_blocks * 512);
    }

    return 0;
}
