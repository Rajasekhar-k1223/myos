#pragma once
#include <stdint.h>

/* FAT16 BIOS Parameter Block (first 62 bytes of boot sector) */
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;          /* 0x29 if following fields are valid */
    uint32_t vol_id;
    char     vol_label[11];
    char     fs_type[8];        /* "FAT16   " */
} fat16_bpb_t;

/* Directory entry (32 bytes) */
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
} fat16_dirent_t;

#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F   /* long filename marker */

typedef struct {
    char     name[13];   /* "FILENAME.EXT\0" */
    uint32_t size;
} fat16_file_info_t;

int fat16_init(void);
int fat16_list_files(fat16_file_info_t* files, int max_files);
int fat16_read_file(const char* name, uint8_t* buf, uint32_t max_size);
int fat16_write_file(const char* name, const uint8_t* data, uint32_t size);
int fat16_delete_file(const char* name);
