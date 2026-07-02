#include "ext2.h"
#include "ahci.h"
#include "kernel.h"
#include "kheap.h"
#include "string.h"

static ext2_superblock_t* sb        = 0;
static ext2_bgd_t*        bgd_table = 0;
static uint32_t           block_size = 0;
static uint32_t           bgd_count  = 0;
static int                mounted    = 0;

/* Read `count` 512-byte sectors starting at `lba` into `buf` */
static int ext2_read_lba(uint32_t lba, uint32_t count, void* buf) {
    return ahci_read(lba, count, buf);
}

/* Write `count` 512-byte sectors starting at `lba` from `buf` */
static int ext2_write_lba(uint32_t lba, uint32_t count, void* buf) {
    return ahci_write(lba, count, buf);
}

/* Read one filesystem block (block_size bytes) */
static int ext2_read_block(uint32_t block_no, void* buf) {
    uint32_t sects = block_size / 512;
    return ext2_read_lba(block_no * sects, sects, buf);
}

/* Write one filesystem block (block_size bytes) */
static int ext2_write_block(uint32_t block_no, void* buf) {
    uint32_t sects = block_size / 512;
    return ext2_write_lba(block_no * sects, sects, buf);
}

void ext2_init(void) {
    mounted = 0;
    sb = (ext2_superblock_t*)kmalloc(1024);
    if (!sb) return;

    /* Superblock starts at byte 1024 = LBA 2 (512-byte sectors) */
    if (!ext2_read_lba(2, 2, sb)) {
        terminal_printf("[EXT2] AHCI read failed.\n");
        return;
    }

    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        terminal_printf("[EXT2] Not an EXT2 fs (magic 0x%x).\n", sb->s_magic);
        return;
    }

    block_size = 1024u << sb->s_log_block_size;
    bgd_count  = (sb->s_blocks_count + sb->s_blocks_per_group - 1)
                  / sb->s_blocks_per_group;
    uint32_t bgd_by_inodes = (sb->s_inodes_count + sb->s_inodes_per_group - 1)
                              / sb->s_inodes_per_group;
    if (bgd_by_inodes > bgd_count) bgd_count = bgd_by_inodes;

    bgd_table = (ext2_bgd_t*)kmalloc(block_size);
    if (!bgd_table) return;

    uint32_t bgd_block = (block_size == 1024) ? 2 : 1;
    if (!ext2_read_block(bgd_block, bgd_table)) {
        terminal_printf("[EXT2] Failed to read BGD table.\n");
        return;
    }

    mounted = 1;
    terminal_printf("[EXT2] Mounted — block_size=%u inodes=%u blocks=%u groups=%u\n",
                    block_size, sb->s_inodes_count, sb->s_blocks_count, bgd_count);

    /* Initialize journal state and replay any uncommitted transactions */
    ext2_journal_init();
    ext2_journal_replay();
}

int ext2_is_mounted(void) { return mounted; }

/* Forward declarations for internal helpers used by journal code */
static int ext2_read_inode(uint32_t ino, ext2_inode_t* out);

/* ── JBD2 definitions ──────────────────────────────────────────────────── */
#define JBD2_MAGIC          0xC03B3998u
#define JBD2_DESCRIPTOR_BLOCK  1
#define JBD2_COMMIT_BLOCK      2
#define JBD2_SUPERBLOCK_V1     3
#define JBD2_SUPERBLOCK_V2     4
#define JBD2_REVOKE_BLOCK      5

/* JBD2 tag flag: last entry in descriptor block */
#define JBD2_FLAG_LAST_TAG  0x8

typedef struct {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
} __attribute__((packed)) jbd2_header_t;

typedef struct {
    jbd2_header_t s_header;
    uint32_t s_blocksize;
    uint32_t s_maxlen;
    uint32_t s_first;
    uint32_t s_sequence;
    uint32_t s_start;
} __attribute__((packed)) jbd2_sb_t;

typedef struct {
    uint32_t t_blocknr;
    uint32_t t_flags;
} __attribute__((packed)) jbd2_tag_t;

/* Byte-swap helpers (JBD2 is big-endian on disk) */
static uint32_t be32(uint32_t x) {
    return ((x & 0xFF000000u) >> 24) |
           ((x & 0x00FF0000u) >>  8) |
           ((x & 0x0000FF00u) <<  8) |
           ((x & 0x000000FFu) << 24);
}

/* ── Journal state ─────────────────────────────────────────────────────── */
static int      has_journal  = 0;
/* Journal inode block pointers (direct blocks only for now: up to 12) */
static uint32_t j_blocks[12];
static uint32_t j_nblocks = 0;

void ext2_journal_init(void) {
    if (!mounted) return;
    if (sb->s_feature_compat & EXT3_FEATURE_COMPAT_HAS_JOURNAL) {
        has_journal = 1;
        terminal_printf("[EXT3] Journal found. Ext3 mode enabled.\n");
    } else {
        terminal_printf("[EXT2] No journal found. Ext2 mode.\n");
    }
}

/* ── Journal replay (JBD2) ─────────────────────────────────────────────── */
void ext2_journal_replay(void) {
    if (!mounted || !has_journal) return;

    uint32_t jino = sb->s_journal_inum;
    if (jino == 0) jino = 8; /* EXT3 default journal inode */

    ext2_inode_t jinode;
    if (!ext2_read_inode(jino, &jinode)) {
        terminal_printf("[EXT2] Could not read journal inode %u.\n", jino);
        return;
    }

    /* Collect journal block numbers from direct block pointers */
    j_nblocks = 0;
    for (int i = 0; i < 12; i++) {
        if (jinode.i_block[i] == 0) break;
        j_blocks[j_nblocks++] = jinode.i_block[i];
    }
    if (j_nblocks == 0) {
        terminal_printf("[EXT2] Journal inode has no blocks.\n");
        return;
    }

    /* Read journal block 0 to find the JBD2 superblock */
    uint8_t* jblk = (uint8_t*)kmalloc(block_size);
    if (!jblk) return;

    if (!ext2_read_block(j_blocks[0], jblk)) {
        terminal_printf("[EXT2] Could not read journal block 0.\n");
        kfree(jblk);
        return;
    }

    jbd2_sb_t* jsb = (jbd2_sb_t*)jblk;
    if (be32(jsb->s_header.h_magic) != JBD2_MAGIC) {
        terminal_printf("[EXT2] Journal clean, no replay needed.\n");
        kfree(jblk);
        return;
    }

    uint32_t j_start    = be32(jsb->s_start);
    uint32_t j_maxlen   = be32(jsb->s_maxlen);
    uint32_t j_seq      = be32(jsb->s_sequence);

    terminal_printf("[EXT2] JBD2 journal found, replaying... (start=%u maxlen=%u seq=%u)\n",
                    j_start, j_maxlen, j_seq);

    if (j_start == 0) {
        /* Journal is clean: s_start == 0 means no outstanding transactions */
        terminal_printf("[EXT2] Journal clean, no replay needed.\n");
        kfree(jblk);
        return;
    }

    int recovered = 0;
    uint8_t* dblk = (uint8_t*)kmalloc(block_size);
    if (!dblk) { kfree(jblk); return; }

    /* Walk journal blocks starting at j_start */
    uint32_t pos = j_start;
    uint32_t cur_seq = j_seq;
    int max_iter = (int)j_maxlen + 1; /* safety cap */

    while (max_iter-- > 0) {
        if (pos >= j_nblocks) break; /* beyond our direct blocks */

        if (!ext2_read_block(j_blocks[pos], jblk)) break;

        jbd2_header_t* hdr = (jbd2_header_t*)jblk;
        if (be32(hdr->h_magic) != JBD2_MAGIC) break; /* end of valid journal data */

        uint32_t btype = be32(hdr->h_blocktype);
        uint32_t bseq  = be32(hdr->h_sequence);

        if (bseq != cur_seq) break; /* sequence mismatch — stop */

        if (btype == JBD2_DESCRIPTOR_BLOCK) {
            /* Descriptor block: parse tags and apply data blocks that follow */
            uint32_t tag_off = sizeof(jbd2_header_t);
            uint32_t data_pos = pos + 1;
            int done = 0;

            while (!done && tag_off + sizeof(jbd2_tag_t) <= block_size) {
                jbd2_tag_t* tag = (jbd2_tag_t*)(jblk + tag_off);
                uint32_t fs_block = be32(tag->t_blocknr);
                uint32_t flags    = be32(tag->t_flags);

                /* Read the data block and write it to the real filesystem */
                if (data_pos < j_nblocks) {
                    if (ext2_read_block(j_blocks[data_pos], dblk)) {
                        ext2_write_block(fs_block, dblk);
                        recovered++;
                    }
                    data_pos++;
                }

                if (flags & JBD2_FLAG_LAST_TAG) done = 1;
                tag_off += sizeof(jbd2_tag_t);
            }

            pos = data_pos; /* advance past all data blocks */

        } else if (btype == JBD2_COMMIT_BLOCK) {
            /* Committed — advance sequence and continue */
            cur_seq++;
            pos++;

        } else if (btype == JBD2_REVOKE_BLOCK) {
            /* Revoke block — skip (blocks listed here should NOT be replayed) */
            pos++;

        } else {
            /* Unknown block type — stop */
            break;
        }
    }

    kfree(dblk);
    kfree(jblk);
    terminal_printf("[EXT2] Journal replay complete: %d blocks recovered.\n", recovered);
}

/* ── Transaction log buffer ────────────────────────────────────────────── */
#define JOURNAL_MAX_DIRTY 16
static uint32_t j_dirty_blocks[JOURNAL_MAX_DIRTY];
static uint8_t* j_dirty_data[JOURNAL_MAX_DIRTY];
static int      j_dirty_count   = 0;
static int      j_in_transaction = 0;

int ext2_journal_start_transaction(void) {
    if (!has_journal) return 0;
    /* Free any leftover buffers from a previous abandoned transaction */
    for (int i = 0; i < j_dirty_count; i++) {
        if (j_dirty_data[i]) { kfree(j_dirty_data[i]); j_dirty_data[i] = 0; }
    }
    j_dirty_count = 0;
    j_in_transaction = 1;
    return 1;
}

int ext2_journal_commit_transaction(void) {
    if (!has_journal || !j_in_transaction) return 0;
    for (int i = 0; i < j_dirty_count; i++) {
        if (j_dirty_data[i]) {
            ext2_write_block(j_dirty_blocks[i], j_dirty_data[i]);
            kfree(j_dirty_data[i]);
            j_dirty_data[i] = 0;
        }
    }
    j_dirty_count = 0;
    j_in_transaction = 0;
    return 1;
}

void ext2_journal_log_block(uint32_t block_no, const uint8_t* data) {
    if (!j_in_transaction || j_dirty_count >= JOURNAL_MAX_DIRTY) return;
    uint32_t bsz = block_size ? block_size : 1024;
    uint8_t* copy = (uint8_t*)kmalloc(bsz);
    if (!copy) return;
    memcpy(copy, data, bsz);
    j_dirty_blocks[j_dirty_count] = block_no;
    j_dirty_data[j_dirty_count]   = copy;
    j_dirty_count++;
}
/* Read inode number `ino` (1-based) into `out` */
static int ext2_read_inode(uint32_t ino, ext2_inode_t* out) {
    if (!mounted || ino == 0) return 0;
    uint32_t group  = (ino - 1) / sb->s_inodes_per_group;
    uint32_t idx    = (ino - 1) % sb->s_inodes_per_group;
    uint32_t isize  = (sb->s_rev_level >= 1) ? sb->s_inode_size : 128;
    uint32_t table_block = bgd_table[group].bg_inode_table;
    uint32_t byte_off    = idx * isize;
    uint32_t block_off   = byte_off / block_size;
    uint32_t inner_off   = byte_off % block_size;
    uint8_t* tmp = (uint8_t*)kmalloc(block_size);
    if (!tmp) return 0;
    if (!ext2_read_block(table_block + block_off, tmp)) { kfree(tmp); return 0; }
    memcpy(out, tmp + inner_off, sizeof(ext2_inode_t));
    kfree(tmp);
    return 1;
}

/* Write inode number `ino` (1-based) back to disk */
static int ext2_write_inode(uint32_t ino, ext2_inode_t* in) {
    if (!mounted || ino == 0) return 0;
    uint32_t group  = (ino - 1) / sb->s_inodes_per_group;
    uint32_t idx    = (ino - 1) % sb->s_inodes_per_group;
    uint32_t isize  = (sb->s_rev_level >= 1) ? sb->s_inode_size : 128;
    uint32_t table_block = bgd_table[group].bg_inode_table;
    uint32_t byte_off    = idx * isize;
    uint32_t block_off   = byte_off / block_size;
    uint32_t inner_off   = byte_off % block_size;
    uint8_t* tmp = (uint8_t*)kmalloc(block_size);
    if (!tmp) return 0;
    if (!ext2_read_block(table_block + block_off, tmp)) { kfree(tmp); return 0; }
    memcpy(tmp + inner_off, in, sizeof(ext2_inode_t));
    int ok = ext2_write_block(table_block + block_off, tmp);
    kfree(tmp);
    return ok;
}

/* Allocate a free block from block bitmap in group 0; returns block number or 0 */
static uint32_t ext2_alloc_block(void) {
    uint8_t* bitmap = (uint8_t*)kmalloc(block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < bgd_count; g++) {
        if (bgd_table[g].bg_free_blocks_count == 0) continue;
        if (!ext2_read_block(bgd_table[g].bg_block_bitmap, bitmap)) continue;

        uint32_t blocks_in_group = sb->s_blocks_per_group;
        for (uint32_t i = 0; i < blocks_in_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit  = i % 8;
            if (!(bitmap[byte] & (1u << bit))) {
                /* Mark as used */
                bitmap[byte] |= (1u << bit);
                ext2_write_block(bgd_table[g].bg_block_bitmap, bitmap);
                bgd_table[g].bg_free_blocks_count--;
                sb->s_free_blocks_count--;
                kfree(bitmap);
                return sb->s_first_data_block + g * sb->s_blocks_per_group + i;
            }
        }
    }
    kfree(bitmap);
    return 0;
}

/* Allocate a free inode from inode bitmap in group 0; returns inode number or 0 */
static uint32_t ext2_alloc_inode(void) {
    uint8_t* bitmap = (uint8_t*)kmalloc(block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < bgd_count; g++) {
        if (bgd_table[g].bg_free_inodes_count == 0) continue;
        if (!ext2_read_block(bgd_table[g].bg_inode_bitmap, bitmap)) continue;

        uint32_t inodes_in_group = sb->s_inodes_per_group;
        for (uint32_t i = 0; i < inodes_in_group; i++) {
            uint32_t byte = i / 8;
            uint32_t bit  = i % 8;
            if (!(bitmap[byte] & (1u << bit))) {
                /* Mark as used */
                bitmap[byte] |= (1u << bit);
                ext2_write_block(bgd_table[g].bg_inode_bitmap, bitmap);
                bgd_table[g].bg_free_inodes_count--;
                sb->s_free_inodes_count--;
                kfree(bitmap);
                return g * sb->s_inodes_per_group + i + 1; /* inode numbers are 1-based */
            }
        }
    }
    kfree(bitmap);
    return 0;
}

/* Write BGD table back to disk */
static int ext2_write_bgd(void) {
    uint32_t bgd_block = (block_size == 1024) ? 2 : 1;
    return ext2_write_block(bgd_block, bgd_table);
}

/* List root-directory entries; returns count stored in names[][13] */
int ext2_ls(char names[][13], int max_entries) {
    if (!mounted) return 0;
    ext2_inode_t root;
    if (!ext2_read_inode(2, &root)) return 0; /* inode 2 = root dir */

    uint8_t* blk = (uint8_t*)kmalloc(block_size);
    if (!blk) return 0;

    int count = 0;
    /* Walk up to 12 direct blocks */
    for (int b = 0; b < 12 && count < max_entries; b++) {
        uint32_t bno = root.i_block[b];
        if (bno == 0) break;
        if (!ext2_read_block(bno, blk)) break;
        uint32_t off = 0;
        while (off < block_size && count < max_entries) {
            ext2_dir_entry_t* de = (ext2_dir_entry_t*)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                int nl = de->name_len < 12 ? de->name_len : 12;
                memcpy(names[count], de->name, nl);
                names[count][nl] = '\0';
                count++;
            }
            off += de->rec_len;
        }
    }
    kfree(blk);
    return count;
}

/* Read a file by name from root directory; returns bytes read or -1 */
int ext2_read_file(const char* name, uint8_t* buf, uint32_t max_len) {
    if (!mounted) return -1;
    ext2_inode_t root;
    if (!ext2_read_inode(2, &root)) return -1;

    uint8_t* blk = (uint8_t*)kmalloc(block_size);
    if (!blk) return -1;

    /* Number of block pointers per indirect block */
    uint32_t ptrs_per_block = block_size / 4;

    uint32_t target_ino = 0;
    for (int b = 0; b < 12 && !target_ino; b++) {
        uint32_t bno = root.i_block[b];
        if (bno == 0) break;
        if (!ext2_read_block(bno, blk)) break;
        uint32_t off = 0;
        while (off < block_size) {
            ext2_dir_entry_t* de = (ext2_dir_entry_t*)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                char nm[13]; int nl = de->name_len < 12 ? de->name_len : 12;
                memcpy(nm, de->name, nl); nm[nl] = '\0';
                if (strcmp(nm, name) == 0) { target_ino = de->inode; break; }
            }
            off += de->rec_len;
        }
    }
    if (!target_ino) { kfree(blk); return -1; }

    ext2_inode_t fi;
    if (!ext2_read_inode(target_ino, &fi)) { kfree(blk); return -1; }

    uint32_t total = fi.i_size < max_len ? fi.i_size : max_len;
    uint32_t read  = 0;

    /* Direct blocks (i_block[0..11]) */
    for (int b = 0; b < 12 && read < total; b++) {
        uint32_t bno = fi.i_block[b];
        if (bno == 0) break;
        if (!ext2_read_block(bno, blk)) break;
        uint32_t chunk = block_size;
        if (read + chunk > total) chunk = total - read;
        memcpy(buf + read, blk, chunk);
        read += chunk;
    }

    /* Single indirect block (i_block[12]) */
    if (read < total && fi.i_block[12] != 0) {
        uint32_t* ind_blk = (uint32_t*)kmalloc(block_size);
        if (ind_blk) {
            if (ext2_read_block(fi.i_block[12], ind_blk)) {
                for (uint32_t i = 0; i < ptrs_per_block && read < total; i++) {
                    uint32_t bno = ind_blk[i];
                    if (bno == 0) break;
                    if (!ext2_read_block(bno, blk)) break;
                    uint32_t chunk = block_size;
                    if (read + chunk > total) chunk = total - read;
                    memcpy(buf + read, blk, chunk);
                    read += chunk;
                }
            }
            kfree(ind_blk);
        }
    }

    /* Double indirect block (i_block[13]) */
    if (read < total && fi.i_block[13] != 0) {
        uint32_t* dind_blk = (uint32_t*)kmalloc(block_size);
        uint32_t* sind_blk = (uint32_t*)kmalloc(block_size);
        if (dind_blk && sind_blk) {
            if (ext2_read_block(fi.i_block[13], dind_blk)) {
                for (uint32_t di = 0; di < ptrs_per_block && read < total; di++) {
                    uint32_t sind_bno = dind_blk[di];
                    if (sind_bno == 0) break;
                    if (!ext2_read_block(sind_bno, sind_blk)) break;
                    for (uint32_t si = 0; si < ptrs_per_block && read < total; si++) {
                        uint32_t bno = sind_blk[si];
                        if (bno == 0) break;
                        if (!ext2_read_block(bno, blk)) break;
                        uint32_t chunk = block_size;
                        if (read + chunk > total) chunk = total - read;
                        memcpy(buf + read, blk, chunk);
                        read += chunk;
                    }
                }
            }
        }
        if (dind_blk) kfree(dind_blk);
        if (sind_blk) kfree(sind_blk);
    }

    kfree(blk);
    return (int)read;
}

/* Write data to an existing file's blocks; allocate new blocks as needed.
 * Returns bytes written or -1 on error. */
int ext2_write_file(const char* name, const uint8_t* buf, uint32_t len) {
    if (!mounted) return -1;

    /* Find the file in root directory */
    ext2_inode_t root;
    if (!ext2_read_inode(2, &root)) return -1;

    uint8_t* blk = (uint8_t*)kmalloc(block_size);
    if (!blk) return -1;

    uint32_t target_ino = 0;
    for (int b = 0; b < 12 && !target_ino; b++) {
        uint32_t bno = root.i_block[b];
        if (bno == 0) break;
        if (!ext2_read_block(bno, blk)) break;
        uint32_t off = 0;
        while (off < block_size) {
            ext2_dir_entry_t* de = (ext2_dir_entry_t*)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                char nm[13]; int nl = de->name_len < 12 ? de->name_len : 12;
                memcpy(nm, de->name, nl); nm[nl] = '\0';
                if (strcmp(nm, name) == 0) { target_ino = de->inode; break; }
            }
            off += de->rec_len;
        }
    }
    if (!target_ino) { kfree(blk); return -1; }

    ext2_inode_t fi;
    if (!ext2_read_inode(target_ino, &fi)) { kfree(blk); return -1; }

    uint32_t written = 0;
    uint32_t blocks_needed = (len + block_size - 1) / block_size;

    /* Write to direct blocks (up to 12) */
    for (uint32_t b = 0; b < 12 && written < len; b++) {
        if (b >= blocks_needed) break;
        if (fi.i_block[b] == 0) {
            fi.i_block[b] = ext2_alloc_block();
            if (fi.i_block[b] == 0) { kfree(blk); return -1; }
        }
        memset(blk, 0, block_size);
        uint32_t chunk = block_size;
        if (written + chunk > len) chunk = len - written;
        memcpy(blk, buf + written, chunk);
        ext2_write_block(fi.i_block[b], blk);
        written += chunk;
    }

    /* Update inode size and write back */
    fi.i_size = len;
    fi.i_blocks = blocks_needed * (block_size / 512);
    ext2_write_inode(target_ino, &fi);
    ext2_write_bgd();

    kfree(blk);
    return (int)written;
}

/* Create a new file in the root directory and write data to it.
 * Returns bytes written or -1 on error. */
int ext2_create_file(const char* name, const uint8_t* buf, uint32_t len) {
    if (!mounted) return -1;

    /* Allocate a new inode */
    uint32_t new_ino = ext2_alloc_inode();
    if (new_ino == 0) return -1;

    /* Initialize the inode */
    ext2_inode_t fi;
    memset(&fi, 0, sizeof(fi));
    fi.i_mode = EXT2_S_IFREG | 0644;
    fi.i_links_count = 1;
    fi.i_size = len;

    /* Allocate blocks and write data */
    uint8_t* blk = (uint8_t*)kmalloc(block_size);
    if (!blk) return -1;

    uint32_t written = 0;
    uint32_t blocks_needed = (len + block_size - 1) / block_size;
    if (blocks_needed == 0) blocks_needed = 1;

    for (uint32_t b = 0; b < 12 && written < len && b < blocks_needed; b++) {
        fi.i_block[b] = ext2_alloc_block();
        if (fi.i_block[b] == 0) { kfree(blk); return -1; }
        memset(blk, 0, block_size);
        uint32_t chunk = block_size;
        if (written + chunk > len) chunk = len - written;
        if (chunk > 0) memcpy(blk, buf + written, chunk);
        ext2_write_block(fi.i_block[b], blk);
        written += chunk;
    }

    fi.i_blocks = blocks_needed * (block_size / 512);
    ext2_write_inode(new_ino, &fi);

    /* Add directory entry to root directory (inode 2) */
    ext2_inode_t root;
    if (!ext2_read_inode(2, &root)) { kfree(blk); return -1; }

    uint8_t name_len = (uint8_t)strlen(name);
    /* Entry size must be 4-byte aligned */
    uint16_t entry_size = (uint16_t)(8 + name_len);
    if (entry_size % 4) entry_size += 4 - (entry_size % 4);

    int added = 0;
    for (int b = 0; b < 12 && !added; b++) {
        uint32_t bno = root.i_block[b];
        if (bno == 0) {
            /* Allocate a new directory block */
            bno = ext2_alloc_block();
            if (bno == 0) { kfree(blk); return -1; }
            root.i_block[b] = bno;
            memset(blk, 0, block_size);
        } else {
            if (!ext2_read_block(bno, blk)) continue;
        }

        /* Scan for space in this directory block */
        uint32_t off = 0;
        while (off < block_size) {
            ext2_dir_entry_t* de = (ext2_dir_entry_t*)(blk + off);

            /* Empty space at end of block */
            if (de->inode == 0 && de->rec_len == 0) {
                /* Place entry here spanning remaining block */
                de->inode    = new_ino;
                de->rec_len  = (uint16_t)(block_size - off);
                de->name_len = name_len;
                de->file_type = EXT2_FT_REG_FILE;
                memcpy(de->name, name, name_len);
                ext2_write_block(bno, blk);
                added = 1;
                break;
            }

            uint16_t actual = (uint16_t)(8 + de->name_len);
            if (actual % 4) actual += 4 - (actual % 4);
            uint16_t slack = de->rec_len - actual;

            if (de->rec_len == 0) break; /* safety */

            if (slack >= entry_size) {
                /* Shrink this entry and insert new one after it */
                uint16_t old_rec = de->rec_len;
                de->rec_len = actual;
                ext2_dir_entry_t* ne = (ext2_dir_entry_t*)(blk + off + actual);
                ne->inode     = new_ino;
                ne->rec_len   = old_rec - actual;
                ne->name_len  = name_len;
                ne->file_type = EXT2_FT_REG_FILE;
                memcpy(ne->name, name, name_len);
                ext2_write_block(bno, blk);
                added = 1;
                break;
            }
            off += de->rec_len;
        }
    }

    /* Update root inode if we allocated a new block */
    ext2_write_inode(2, &root);
    ext2_write_bgd();

    kfree(blk);
    return added ? (int)written : -1;
}

static uint32_t ext2_find_ino(const char* name) {
    if (!mounted) return 0;
    ext2_inode_t root;
    if (!ext2_read_inode(2, &root)) return 0;
    uint8_t* blk = (uint8_t*)kmalloc(block_size);
    if (!blk) return 0;
    uint32_t target_ino = 0;
    for (int b = 0; b < 12 && !target_ino; b++) {
        uint32_t bno = root.i_block[b];
        if (bno == 0) break;
        if (!ext2_read_block(bno, blk)) break;
        uint32_t off = 0;
        while (off < block_size) {
            ext2_dir_entry_t* de = (ext2_dir_entry_t*)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len > 0) {
                char nm[13]; int nl = de->name_len < 12 ? de->name_len : 12;
                memcpy(nm, de->name, nl); nm[nl] = '\0';
                if (strcmp(nm, name) == 0) { target_ino = de->inode; break; }
            }
            off += de->rec_len;
        }
    }
    kfree(blk);
    return target_ino;
}

int ext2_chmod(const char* name, uint16_t mode) {
    uint32_t ino = ext2_find_ino(name);
    if (!ino) return -1;
    ext2_inode_t fi;
    if (!ext2_read_inode(ino, &fi)) return -1;
    fi.i_mode = (fi.i_mode & 0xF000) | (mode & 0x0FFF);
    return ext2_write_inode(ino, &fi) ? 0 : -1;
}

int ext2_chown(const char* name, uint16_t uid, uint16_t gid) {
    uint32_t ino = ext2_find_ino(name);
    if (!ino) return -1;
    ext2_inode_t fi;
    if (!ext2_read_inode(ino, &fi)) return -1;
    fi.i_uid = uid;
    fi.i_gid = gid;
    return ext2_write_inode(ino, &fi) ? 0 : -1;
}
