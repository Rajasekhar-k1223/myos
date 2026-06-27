#pragma once
#include <stdint.h>

/* FAT32 BIOS Parameter Block */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;  /* 0 for FAT32 */
    uint16_t total_sectors_16;  /* 0 for FAT32 */
    uint8_t  media_type;
    uint16_t fat_size_16;       /* 0 for FAT32 */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32 extended BPB */
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    char     vol_label[11];
    char     fs_type[8];  /* "FAT32   " */
} fat32_bpb_t;

/* Directory entry (32 bytes) — same as FAT16 */
typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  nt_res;
    uint8_t  ctime_cs;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t wtime;
    uint16_t wdate;
    uint16_t cluster_lo;
    uint32_t file_size;
} fat32_dirent_t;

#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LFN        0x0F

typedef struct {
    char     name[13];
    uint32_t size;
    int      is_dir;
} fat32_file_info_t;

int fat32_init(void);
int fat32_list_files(fat32_file_info_t* files, int max_files);
int fat32_read_file(const char* name, uint8_t* buf, uint32_t max_size);
int fat32_write_file(const char* name, const uint8_t* buf, uint32_t len);
int fat32_mkdir(const char* name);
int fat32_ls_dir(const char* path, char names[][13], int max);
