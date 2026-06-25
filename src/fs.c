#include "fs.h"
#include "ata.h"
#include "string.h"

#define FS_MAGIC "MYFS"
#define MAX_FILES 20

typedef struct {
    char name[16];
    uint32_t start_lba;
    uint32_t size;
} fs_entry_t;

typedef struct {
    char magic[4];
    uint32_t num_files;
    fs_entry_t entries[MAX_FILES];
} fs_superblock_t;

static fs_superblock_t sb;

void fs_format(void) {
    memcpy(sb.magic, FS_MAGIC, 4);
    sb.num_files = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        sb.entries[i].name[0] = '\0';
        sb.entries[i].start_lba = 0;
        sb.entries[i].size = 0;
    }
    ata_write_sector(1, (uint8_t*)&sb);
}

int fs_init(void) {
    ata_read_sector(1, (uint8_t*)&sb);
    if (strncmp(sb.magic, FS_MAGIC, 4) != 0) {
        fs_format();
        return 0; // Formatted fresh
    }
    return 1; // Loaded existing
}

int fs_write_file(const char* name, const char* data) {
    int idx = -1;
    for (uint32_t i = 0; i < sb.num_files; i++) {
        if (strcmp(sb.entries[i].name, name) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx == -1) {
        if (sb.num_files >= MAX_FILES) return -1; // Disk full
        idx = sb.num_files;
        sb.num_files++;
    }
    
    uint32_t next_lba = 2;
    for (uint32_t i = 0; i < sb.num_files; i++) {
        if (i != (uint32_t)idx && sb.entries[i].start_lba >= next_lba) {
            next_lba = sb.entries[i].start_lba + 1;
        }
    }
    
    strncpy(sb.entries[idx].name, name, 15);
    sb.entries[idx].name[15] = '\0';
    sb.entries[idx].start_lba = next_lba;
    sb.entries[idx].size = strlen(data);
    
    uint8_t buf[512] = {0};
    strncpy((char*)buf, data, 511);
    ata_write_sector(next_lba, buf);
    
    ata_write_sector(1, (uint8_t*)&sb); // Update directory
    return 0;
}

int fs_read_file(const char* name, char* out_data) {
    for (uint32_t i = 0; i < sb.num_files; i++) {
        if (strcmp(sb.entries[i].name, name) == 0) {
            uint8_t buf[512];
            ata_read_sector(sb.entries[i].start_lba, buf);
            buf[511] = '\0';
            strcpy(out_data, (char*)buf);
            return 0;
        }
    }
    return -1; // Not found
}

int fs_list_files(fs_file_info_t* files) {
    for (uint32_t i = 0; i < sb.num_files; i++) {
        strcpy(files[i].name, sb.entries[i].name);
        files[i].size = sb.entries[i].size;
    }
    return sb.num_files;
}
