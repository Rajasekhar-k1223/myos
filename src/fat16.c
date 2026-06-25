#include "fat16.h"
#include "ata.h"
#include "string.h"

static fat16_bpb_t bpb;
static uint32_t fat1_start;  /* first sector of FAT1 */
static uint32_t root_start;  /* first sector of root directory */
static uint32_t data_start;  /* first data sector (cluster 2) */
static uint32_t root_secs;   /* sectors in root directory region */
static int      fat16_ready = 0;

/* Separate static I/O buffers so callers don't clobber each other */
static uint8_t fat_io[512];   /* used only by fat_get / fat_set */
static uint8_t dir_io[512];   /* used only by directory scanners */
static uint8_t dat_io[512];   /* used only by data read / write */

/* ─── geometry helpers ──────────────────────────────────────────────── */

static uint32_t cluster_to_lba(uint16_t c) {
    return data_start + (uint32_t)(c - 2) * bpb.sectors_per_cluster;
}

/* ─── FAT access ────────────────────────────────────────────────────── */

static uint16_t fat_get(uint16_t c) {
    uint32_t byte_off = (uint32_t)c * 2;
    uint32_t sec  = fat1_start + byte_off / 512;
    uint32_t off  = byte_off % 512;
    ata_read_sector(sec, fat_io);
    return *(uint16_t*)(fat_io + off);
}

static void fat_set(uint16_t c, uint16_t val) {
    uint32_t byte_off = (uint32_t)c * 2;
    for (int f = 0; f < bpb.num_fats; f++) {
        uint32_t sec = fat1_start + (uint32_t)f * bpb.fat_size_16 + byte_off / 512;
        uint32_t off = byte_off % 512;
        ata_read_sector(sec, fat_io);
        *(uint16_t*)(fat_io + off) = val;
        ata_write_sector(sec, fat_io);
    }
}

static uint16_t fat_alloc(void) {
    uint32_t total_sec = bpb.total_sectors_16 ? bpb.total_sectors_16 : bpb.total_sectors_32;
    uint32_t data_clusters = (total_sec - data_start) / bpb.sectors_per_cluster;
    if (data_clusters > 65524) data_clusters = 65524; /* FAT16 max */
    for (uint16_t c = 2; (uint32_t)(c - 2) < data_clusters; c++) {
        if (fat_get(c) == 0x0000) {
            fat_set(c, 0xFFFF);
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(uint16_t c) {
    while (c >= 2 && c < 0xFFF8) {
        uint16_t next = fat_get(c);
        fat_set(c, 0x0000);
        c = next;
    }
}

/* ─── 8.3 name conversion ───────────────────────────────────────────── */

static void to_83(const char* in, char* out11) {
    memset(out11, ' ', 11);
    int i = 0;
    while (*in && *in != '.' && i < 8) {
        char ch = *in++;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        out11[i++] = ch;
    }
    if (*in == '.') {
        in++;
        int j = 8;
        while (*in && j < 11) {
            char ch = *in++;
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            out11[j++] = ch;
        }
    }
}

static void from_83(const char* in11, char* out) {
    int i = 0;
    for (int j = 0; j < 8; j++) {
        if (in11[j] == ' ') break;
        char ch = in11[j];
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        out[i++] = ch;
    }
    if (in11[8] != ' ') {
        out[i++] = '.';
        for (int j = 8; j < 11; j++) {
            if (in11[j] == ' ') break;
            char ch = in11[j];
            if (ch >= 'A' && ch <= 'Z') ch += 32;
            out[i++] = ch;
        }
    }
    out[i] = '\0';
}

/* ─── public API ────────────────────────────────────────────────────── */

int fat16_init(void) {
    uint8_t boot[512];
    ata_read_sector(0, boot);

    /* Boot sector signature */
    if (boot[510] != 0x55 || boot[511] != 0xAA) return 0;

    memcpy(&bpb, boot, sizeof(bpb));

    if (bpb.boot_sig != 0x29)          return 0;
    if (bpb.bytes_per_sector != 512)   return 0;
    if (bpb.sectors_per_cluster == 0)  return 0;

    fat1_start = bpb.reserved_sectors;
    root_secs  = ((uint32_t)bpb.root_entry_count * 32u + 511u) / 512u;
    root_start = fat1_start + (uint32_t)bpb.num_fats * bpb.fat_size_16;
    data_start = root_start + root_secs;
    fat16_ready = 1;
    return 1;
}

int fat16_list_files(fat16_file_info_t* files, int max_files) {
    if (!fat16_ready) return 0;
    int count = 0;
    for (uint32_t s = 0; s < root_secs && count < max_files; s++) {
        ata_read_sector(root_start + s, dir_io);
        fat16_dirent_t* dir = (fat16_dirent_t*)dir_io;
        int per_sec = 512 / (int)sizeof(fat16_dirent_t);
        for (int i = 0; i < per_sec && count < max_files; i++) {
            uint8_t first = (uint8_t)dir[i].name[0];
            if (first == 0x00) return count;
            if (first == 0xE5) continue;
            if (dir[i].attr == FAT_ATTR_LFN) continue;
            if (dir[i].attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) continue;
            from_83(dir[i].name, files[count].name);
            files[count].size = dir[i].file_size;
            count++;
        }
    }
    return count;
}

int fat16_read_file(const char* name, uint8_t* buf, uint32_t max_size) {
    if (!fat16_ready || max_size == 0) return -1;
    char fat83[11];
    to_83(name, fat83);

    /* Scan root directory */
    for (uint32_t s = 0; s < root_secs; s++) {
        ata_read_sector(root_start + s, dir_io);
        fat16_dirent_t* dir = (fat16_dirent_t*)dir_io;
        int per_sec = 512 / (int)sizeof(fat16_dirent_t);
        for (int i = 0; i < per_sec; i++) {
            uint8_t first = (uint8_t)dir[i].name[0];
            if (first == 0x00) return -1;
            if (first == 0xE5) continue;
            if (dir[i].attr == FAT_ATTR_LFN) continue;
            if (dir[i].attr & (FAT_ATTR_VOLUME_ID | FAT_ATTR_DIRECTORY)) continue;
            if (memcmp(dir[i].name, fat83, 11) != 0) continue;

            uint32_t fsize = dir[i].file_size;
            uint16_t cluster = dir[i].cluster_lo;
            /* dir_io will be overwritten after this — we've saved what we need */

            if (fsize > max_size - 1) fsize = max_size - 1;
            uint32_t pos = 0;

            while (cluster >= 2 && cluster < 0xFFF8 && pos < fsize) {
                uint32_t lba = cluster_to_lba(cluster);
                for (uint8_t sec = 0; sec < bpb.sectors_per_cluster && pos < fsize; sec++) {
                    ata_read_sector(lba + sec, dat_io);
                    uint32_t n = 512;
                    if (pos + n > fsize) n = fsize - pos;
                    memcpy(buf + pos, dat_io, n);
                    pos += n;
                }
                cluster = fat_get(cluster);
            }
            buf[pos] = '\0';
            return (int)pos;
        }
    }
    return -1;
}

int fat16_write_file(const char* name, const uint8_t* data, uint32_t size) {
    if (!fat16_ready) return -1;
    char fat83[11];
    to_83(name, fat83);

    /* Scan root directory: find existing entry or first free slot */
    int dir_s = -1, dir_i = -1;
    int free_s = -1, free_i = -1;
    uint16_t old_cluster = 0;

    for (uint32_t s = 0; s < root_secs; s++) {
        ata_read_sector(root_start + s, dir_io);
        fat16_dirent_t* dir = (fat16_dirent_t*)dir_io;
        int per_sec = 512 / (int)sizeof(fat16_dirent_t);
        int done = 0;
        for (int i = 0; i < per_sec; i++) {
            uint8_t first = (uint8_t)dir[i].name[0];
            if (first == 0x00 || first == 0xE5) {
                if (free_s < 0) { free_s = (int)s; free_i = i; }
                if (first == 0x00) { done = 1; break; }
            } else if (memcmp(dir[i].name, fat83, 11) == 0) {
                dir_s = (int)s;
                dir_i = i;
                old_cluster = dir[i].cluster_lo;
                done = 1;
                break;
            }
        }
        if (done) break;
    }

    /* Use existing slot or free slot */
    if (dir_s < 0) {
        if (free_s < 0) return -1; /* root directory full */
        dir_s = free_s;
        dir_i = free_i;
    }

    /* Release old cluster chain */
    if (old_cluster) fat_free_chain(old_cluster);

    /* Allocate clusters and write data */
    uint16_t first_cluster = 0, prev = 0;
    uint32_t written = 0;

    while (written < size) {
        uint16_t c = fat_alloc();
        if (c == 0) return -1; /* disk full */

        if (prev == 0) first_cluster = c;
        else fat_set(prev, c);

        uint32_t lba = cluster_to_lba(c);
        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            memset(dat_io, 0, 512);
            uint32_t n = 512;
            if (written + n > size) n = size - written;
            if (n > 0) { memcpy(dat_io, data + written, n); written += n; }
            ata_write_sector(lba + sec, dat_io);
            if (written >= size) break;
        }
        prev = c;
    }
    if (first_cluster == 0) { first_cluster = fat_alloc(); } /* empty file */

    /* Write directory entry */
    ata_read_sector(root_start + (uint32_t)dir_s, dir_io);
    fat16_dirent_t* dir = (fat16_dirent_t*)dir_io;
    memcpy(dir[dir_i].name, fat83, 11);
    dir[dir_i].attr        = FAT_ATTR_ARCHIVE;
    dir[dir_i].nt_res      = 0;
    dir[dir_i].ctime_cs    = 0;
    dir[dir_i].ctime       = 0;
    dir[dir_i].cdate       = 0;
    dir[dir_i].adate       = 0;
    dir[dir_i].cluster_hi  = 0;
    dir[dir_i].wtime       = 0;
    dir[dir_i].wdate       = 0;
    dir[dir_i].cluster_lo  = first_cluster;
    dir[dir_i].file_size   = size;
    ata_write_sector(root_start + (uint32_t)dir_s, dir_io);

    return (int)written;
}

int fat16_delete_file(const char* name) {
    if (!fat16_ready) return -1;
    char fat83[11];
    to_83(name, fat83);

    for (uint32_t s = 0; s < root_secs; s++) {
        ata_read_sector(root_start + s, dir_io);
        fat16_dirent_t* dir = (fat16_dirent_t*)dir_io;
        int per_sec = 512 / (int)sizeof(fat16_dirent_t);
        for (int i = 0; i < per_sec; i++) {
            uint8_t first = (uint8_t)dir[i].name[0];
            if (first == 0x00) return -1;
            if (first == 0xE5) continue;
            if (memcmp(dir[i].name, fat83, 11) != 0) continue;
            fat_free_chain(dir[i].cluster_lo);
            dir[i].name[0] = (char)0xE5;
            ata_write_sector(root_start + s, dir_io);
            return 0;
        }
    }
    return -1;
}
