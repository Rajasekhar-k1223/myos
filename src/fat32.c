#include "fat32.h"
#include "ata.h"
#include "string.h"

static fat32_bpb_t bpb;
static uint32_t fat_start;     /* first sector of FAT1 */
static uint32_t data_start;    /* first data sector (cluster 2) */
static uint32_t root_cluster;  /* cluster number of root directory */
static int      fat32_ready = 0;

/* Separate static I/O buffers */
static uint8_t fat_io[512];
static uint8_t dir_io[512];
static uint8_t dat_io[512];

/* ─── Geometry helpers ──────────────────────────────────────────────── */

static uint32_t cluster_to_lba(uint32_t c) {
    return data_start + (c - 2) * bpb.sectors_per_cluster;
}

/* ─── FAT32 access ──────────────────────────────────────────────────── */

static uint32_t fat_get(uint32_t c) {
    uint32_t byte_off = c * 4;
    uint32_t sec = fat_start + byte_off / 512;
    uint32_t off = byte_off % 512;
    ata_read_sector(sec, fat_io);
    return *(uint32_t*)(fat_io + off) & 0x0FFFFFFF;
}

static void fat_set(uint32_t c, uint32_t val) {
    uint32_t byte_off = c * 4;
    for (int f = 0; f < bpb.num_fats; f++) {
        uint32_t sec = fat_start + (uint32_t)f * bpb.fat_size_32 + byte_off / 512;
        uint32_t off = byte_off % 512;
        ata_read_sector(sec, fat_io);
        uint32_t cur = *(uint32_t*)(fat_io + off);
        /* Preserve top 4 bits */
        *(uint32_t*)(fat_io + off) = (cur & 0xF0000000) | (val & 0x0FFFFFFF);
        ata_write_sector(sec, fat_io);
    }
}

/* Find a free cluster and mark it as end-of-chain. Returns 0 on failure. */
static uint32_t fat_alloc(void) {
    uint32_t total_sec = bpb.total_sectors_32;
    uint32_t data_secs = total_sec - data_start;
    uint32_t total_clusters = data_secs / bpb.sectors_per_cluster;
    if (total_clusters > 0x0FFFFFF6) total_clusters = 0x0FFFFFF6;

    for (uint32_t c = 2; c < total_clusters + 2; c++) {
        if (fat_get(c) == 0x00000000) {
            fat_set(c, 0x0FFFFFFF);
            return c;
        }
    }
    return 0;
}

static void fat_free_chain(uint32_t c) {
    while (c >= 2 && c < 0x0FFFFFF8) {
        uint32_t next = fat_get(c);
        fat_set(c, 0x00000000);
        c = next;
    }
}

/* ─── 8.3 name conversion ───────────────────────────────────────────── */

static void to_83(const char* in, char out11[11]) {
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

/* ─── Directory cluster iteration ───────────────────────────────────── */

/* Callback: (dirent, user_data) -> 1 to stop, 0 to continue */
typedef int (*dir_cb_t)(fat32_dirent_t*, void*);

static void iter_dir(uint32_t cluster, dir_cb_t cb, void* ud) {
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            ata_read_sector(lba + sec, dir_io);
            fat32_dirent_t* dir = (fat32_dirent_t*)dir_io;
            int per_sec = 512 / (int)sizeof(fat32_dirent_t);
            for (int i = 0; i < per_sec; i++) {
                uint8_t first = (uint8_t)dir[i].name[0];
                if (first == 0x00) return;
                if (first == 0xE5) continue;
                if (dir[i].attr == FAT32_ATTR_LFN) continue;
                if (cb(&dir[i], ud)) return;
            }
        }
        cluster = fat_get(cluster);
    }
}

/* ─── public API ────────────────────────────────────────────────────── */

int fat32_init(void) {
    uint8_t boot[512];
    ata_read_sector(0, boot);

    if (boot[510] != 0x55 || boot[511] != 0xAA) return 0;

    memcpy(&bpb, boot, sizeof(bpb));

    if (bpb.boot_sig != 0x29)         return 0;
    if (bpb.bytes_per_sector != 512)  return 0;
    if (bpb.sectors_per_cluster == 0) return 0;
    if (bpb.fat_size_16 != 0)         return 0; /* must be FAT32 */
    if (bpb.fat_size_32 == 0)         return 0;

    fat_start   = bpb.reserved_sectors;
    data_start  = fat_start + (uint32_t)bpb.num_fats * bpb.fat_size_32;
    root_cluster = bpb.root_cluster;
    fat32_ready = 1;
    return 1;
}

typedef struct { fat32_file_info_t* files; int max; int count; } list_ctx_t;

static int list_cb(fat32_dirent_t* d, void* ud) {
    list_ctx_t* ctx = (list_ctx_t*)ud;
    if (ctx->count >= ctx->max) return 1;
    if (d->attr & (FAT32_ATTR_VOLUME_ID)) return 0;
    from_83(d->name, ctx->files[ctx->count].name);
    ctx->files[ctx->count].size   = d->file_size;
    ctx->files[ctx->count].is_dir = (d->attr & FAT32_ATTR_DIRECTORY) ? 1 : 0;
    ctx->count++;
    return 0;
}

int fat32_list_files(fat32_file_info_t* files, int max_files) {
    if (!fat32_ready) return -1;
    list_ctx_t ctx = { files, max_files, 0 };
    iter_dir(root_cluster, list_cb, &ctx);
    return ctx.count;
}

typedef struct { const char* fat83; uint32_t start_cluster; uint32_t size; uint8_t attr; } find_ctx_t;

static int find_cb(fat32_dirent_t* d, void* ud) {
    find_ctx_t* ctx = (find_ctx_t*)ud;
    char name11[11];
    memcpy(name11, d->name, 8);
    memcpy(name11 + 8, d->ext, 3);
    if (memcmp(name11, ctx->fat83, 11) == 0) {
        ctx->start_cluster = ((uint32_t)d->cluster_hi << 16) | d->cluster_lo;
        ctx->size = d->file_size;
        ctx->attr = d->attr;
        return 1; /* stop */
    }
    return 0;
}

int fat32_read_file(const char* name, uint8_t* buf, uint32_t max_size) {
    if (!fat32_ready || max_size == 0) return -1;
    char fat83[11];
    to_83(name, fat83);

    find_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fat83 = fat83;
    iter_dir(root_cluster, find_cb, &ctx);
    if (ctx.start_cluster < 2) return -1;

    uint32_t fsize = ctx.size;
    if (fsize > max_size - 1) fsize = max_size - 1;
    uint32_t pos = 0;
    uint32_t cluster = ctx.start_cluster;

    while (cluster >= 2 && cluster < 0x0FFFFFF8 && pos < fsize) {
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

/* ─── Write file ─────────────────────────────────────────────────────── */

/* Context for finding a dir entry slot for write */
typedef struct {
    char     fat83[11];
    int      found;          /* 1 = existing entry found */
    uint32_t old_cluster;
    uint32_t entry_cluster;  /* cluster of sector containing the entry */
    uint32_t entry_sector;   /* sector within that cluster */
    int      entry_idx;      /* index within that sector */
    int      free_found;
    uint32_t free_cluster;
    uint32_t free_sector;
    int      free_idx;
} write_dir_ctx_t;

static int write_dir_scan(uint32_t dir_cluster, write_dir_ctx_t* ctx) {
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            ata_read_sector(lba + sec, dir_io);
            fat32_dirent_t* dir = (fat32_dirent_t*)dir_io;
            int per_sec = 512 / (int)sizeof(fat32_dirent_t);
            for (int i = 0; i < per_sec; i++) {
                uint8_t first = (uint8_t)dir[i].name[0];
                if (first == 0x00 || first == 0xE5) {
                    if (!ctx->free_found) {
                        ctx->free_found   = 1;
                        ctx->free_cluster = cluster;
                        ctx->free_sector  = sec;
                        ctx->free_idx     = i;
                    }
                    if (first == 0x00) return 0; /* no more entries */
                } else if (dir[i].attr != FAT32_ATTR_LFN) {
                    char name11[11];
                    memcpy(name11, dir[i].name, 8);
                    memcpy(name11 + 8, dir[i].ext, 3);
                    if (memcmp(name11, ctx->fat83, 11) == 0) {
                        ctx->found        = 1;
                        ctx->old_cluster  = ((uint32_t)dir[i].cluster_hi << 16) | dir[i].cluster_lo;
                        ctx->entry_cluster= cluster;
                        ctx->entry_sector = sec;
                        ctx->entry_idx    = i;
                        return 1;
                    }
                }
            }
        }
        cluster = fat_get(cluster);
    }
    return 0;
}

int fat32_write_file(const char* name, const uint8_t* buf, uint32_t len) {
    if (!fat32_ready) return -1;
    write_dir_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    to_83(name, ctx.fat83);

    write_dir_scan(root_cluster, &ctx);

    /* Choose entry slot */
    uint32_t use_cluster;
    uint32_t use_sector;
    int      use_idx;

    if (ctx.found) {
        fat_free_chain(ctx.old_cluster);
        use_cluster = ctx.entry_cluster;
        use_sector  = ctx.entry_sector;
        use_idx     = ctx.entry_idx;
    } else {
        if (!ctx.free_found) {
            /* Need to extend the directory cluster chain */
            uint32_t new_c = fat_alloc();
            if (!new_c) return -1;
            /* Link last cluster of root dir to new_c */
            uint32_t last = root_cluster;
            uint32_t nxt;
            while ((nxt = fat_get(last)) < 0x0FFFFFF8) last = nxt;
            fat_set(last, new_c);
            /* Zero out the new cluster */
            uint32_t lba = cluster_to_lba(new_c);
            memset(dat_io, 0, 512);
            for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++)
                ata_write_sector(lba + s, dat_io);
            ctx.free_found   = 1;
            ctx.free_cluster = new_c;
            ctx.free_sector  = 0;
            ctx.free_idx     = 0;
        }
        use_cluster = ctx.free_cluster;
        use_sector  = ctx.free_sector;
        use_idx     = ctx.free_idx;
    }

    /* Allocate cluster chain and write data */
    uint32_t first_cluster = 0, prev = 0;
    uint32_t written = 0;

    if (len == 0) {
        first_cluster = fat_alloc();
        if (!first_cluster) return -1;
    } else {
        while (written < len) {
            uint32_t c = fat_alloc();
            if (!c) return -1;
            if (!prev) first_cluster = c;
            else fat_set(prev, c);

            uint32_t lba = cluster_to_lba(c);
            for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
                memset(dat_io, 0, 512);
                uint32_t n = 512;
                if (written + n > len) n = len - written;
                if (n > 0) { memcpy(dat_io, buf + written, n); written += n; }
                ata_write_sector(lba + sec, dat_io);
                if (written >= len) break;
            }
            prev = c;
        }
    }

    /* Write/update directory entry */
    uint32_t lba = cluster_to_lba(use_cluster) + use_sector;
    ata_read_sector(lba, dir_io);
    fat32_dirent_t* dir = (fat32_dirent_t*)dir_io;
    fat32_dirent_t* ent = &dir[use_idx];
    memset(ent, 0, sizeof(*ent));
    memcpy(ent->name, ctx.fat83, 8);
    memcpy(ent->ext,  ctx.fat83 + 8, 3);
    ent->attr       = FAT32_ATTR_ARCHIVE;
    ent->cluster_hi = (uint16_t)(first_cluster >> 16);
    ent->cluster_lo = (uint16_t)(first_cluster & 0xFFFF);
    ent->file_size  = len;
    ata_write_sector(lba, dir_io);

    return (int)written;
}

/* ─── mkdir ─────────────────────────────────────────────────────────── */

int fat32_mkdir(const char* name) {
    if (!fat32_ready) return -1;

    /* Allocate a cluster for the new directory */
    uint32_t new_cluster = fat_alloc();
    if (!new_cluster) return -1;

    /* Zero out the cluster */
    uint32_t lba = cluster_to_lba(new_cluster);
    memset(dat_io, 0, 512);
    for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++)
        ata_write_sector(lba + s, dat_io);

    /* Write dot and dotdot entries */
    ata_read_sector(lba, dir_io);
    fat32_dirent_t* dir = (fat32_dirent_t*)dir_io;
    memset(dir, 0, 512);

    /* "." entry */
    memset(dir[0].name, ' ', 8); memset(dir[0].ext, ' ', 3);
    dir[0].name[0]    = '.';
    dir[0].attr       = FAT32_ATTR_DIRECTORY;
    dir[0].cluster_hi = (uint16_t)(new_cluster >> 16);
    dir[0].cluster_lo = (uint16_t)(new_cluster & 0xFFFF);

    /* ".." entry */
    memset(dir[1].name, ' ', 8); memset(dir[1].ext, ' ', 3);
    dir[1].name[0]    = '.';
    dir[1].name[1]    = '.';
    dir[1].attr       = FAT32_ATTR_DIRECTORY;
    dir[1].cluster_hi = (uint16_t)(root_cluster >> 16);
    dir[1].cluster_lo = (uint16_t)(root_cluster & 0xFFFF);

    ata_write_sector(lba, dir_io);

    /* Find slot in root directory and add entry */
    write_dir_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    to_83(name, ctx.fat83);
    write_dir_scan(root_cluster, &ctx);

    uint32_t use_cluster;
    uint32_t use_sector;
    int      use_idx;

    if (ctx.found) {
        /* Already exists — return error */
        fat_free_chain(new_cluster);
        return -1;
    } else {
        if (!ctx.free_found) {
            /* Extend root dir */
            uint32_t new_c = fat_alloc();
            if (!new_c) { fat_free_chain(new_cluster); return -1; }
            uint32_t last = root_cluster;
            uint32_t nxt;
            while ((nxt = fat_get(last)) < 0x0FFFFFF8) last = nxt;
            fat_set(last, new_c);
            uint32_t elba = cluster_to_lba(new_c);
            memset(dat_io, 0, 512);
            for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++)
                ata_write_sector(elba + s, dat_io);
            ctx.free_cluster = new_c;
            ctx.free_sector  = 0;
            ctx.free_idx     = 0;
        }
        use_cluster = ctx.free_cluster;
        use_sector  = ctx.free_sector;
        use_idx     = ctx.free_idx;
    }

    uint32_t elba = cluster_to_lba(use_cluster) + use_sector;
    ata_read_sector(elba, dir_io);
    fat32_dirent_t* edir = (fat32_dirent_t*)dir_io;
    fat32_dirent_t* ent  = &edir[use_idx];
    memset(ent, 0, sizeof(*ent));
    memcpy(ent->name, ctx.fat83, 8);
    memcpy(ent->ext,  ctx.fat83 + 8, 3);
    ent->attr       = FAT32_ATTR_DIRECTORY;
    ent->cluster_hi = (uint16_t)(new_cluster >> 16);
    ent->cluster_lo = (uint16_t)(new_cluster & 0xFFFF);
    ent->file_size  = 0;
    ata_write_sector(elba, dir_io);

    return 0;
}

/* ─── ls_dir ─────────────────────────────────────────────────────────── */

int fat32_ls_dir(const char* path, char names[][13], int max) {
    if (!fat32_ready) return -1;

    /* Find the subdirectory cluster in root directory */
    char fat83[11];
    to_83(path, fat83);

    find_ctx_t fctx;
    memset(&fctx, 0, sizeof(fctx));
    fctx.fat83 = fat83;
    iter_dir(root_cluster, find_cb, &fctx);

    uint32_t dir_cluster = fctx.start_cluster;
    if (dir_cluster < 2) {
        /* Path not found — try root itself */
        if (strcmp(path, "/") == 0 || strcmp(path, "") == 0)
            dir_cluster = root_cluster;
        else
            return -1;
    }
    if (!(fctx.attr & FAT32_ATTR_DIRECTORY))
        return -1;

    /* List entries in the found directory cluster */
    int count = 0;
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0x0FFFFFF8 && count < max) {
        uint32_t lba = cluster_to_lba(cluster);
        for (uint8_t sec = 0; sec < bpb.sectors_per_cluster; sec++) {
            ata_read_sector(lba + sec, dir_io);
            fat32_dirent_t* dir = (fat32_dirent_t*)dir_io;
            int per_sec = 512 / (int)sizeof(fat32_dirent_t);
            for (int i = 0; i < per_sec && count < max; i++) {
                uint8_t first = (uint8_t)dir[i].name[0];
                if (first == 0x00) goto done;
                if (first == 0xE5) continue;
                if (dir[i].attr == FAT32_ATTR_LFN) continue;
                if (dir[i].attr & FAT32_ATTR_VOLUME_ID) continue;
                from_83(dir[i].name, names[count]);
                names[count][12] = '\0';
                count++;
            }
        }
        cluster = fat_get(cluster);
    }
done:
    return count;
}
