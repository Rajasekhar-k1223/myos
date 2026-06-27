#include "vfs.h"
#include "kernel.h"
#include "string.h"
#include "tar.h"
#include "fat16.h"
#include "ext2.h"
#include "kheap.h"

/* ── Mount table ──────────────────────────────────────────────────────────── */
static vfs_mount_t mounts[VFS_MAX_MOUNTS];

static const char* vfs_type_name(vfs_type_t t) {
    switch (t) {
    case VFS_FAT16:  return "fat16";
    case VFS_FAT32:  return "fat32";
    case VFS_EXT2:   return "ext2";
    case VFS_INITRD: return "initrd";
    case VFS_TMPFS:  return "tmpfs";
    default:         return "unknown";
    }
}

/* ── Open file table (simple, used by vfs_open/read/write/close) ─────────── */
#define VFS_MAX_FDS  16
#define VFS_FD_FREE  0
#define VFS_FD_USED  1

typedef struct {
    int       state;       /* VFS_FD_FREE or VFS_FD_USED */
    vfs_type_t fs_type;
    char      path[128];  /* path within the filesystem (prefix stripped) */
    uint32_t  pos;        /* read position */
    void*     data;       /* pointer into initrd data (for INITRD type) */
    uint32_t  size;       /* file size */
} vfs_fd_t;

static vfs_fd_t open_files[VFS_MAX_FDS];

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Find the mount that best matches path (longest prefix wins). */
static vfs_mount_t* vfs_find_mount(const char* path) {
    vfs_mount_t* best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) continue;
        size_t mplen = strlen(mounts[i].mount_point);
        if (strncmp(path, mounts[i].mount_point, mplen) == 0) {
            if (mplen > best_len) {
                best_len = mplen;
                best = &mounts[i];
            }
        }
    }
    return best;
}

/* Strip mount prefix from path, return pointer into path after prefix. */
static const char* vfs_strip_prefix(const char* path, const char* mount_point) {
    size_t mplen = strlen(mount_point);
    const char* rel = path + mplen;
    /* skip leading '/' */
    while (*rel == '/') rel++;
    return rel;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = 0;
    }
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        open_files[i].state = VFS_FD_FREE;
    }

    /* Auto-mount initrd at "/" */
    vfs_mount("/", VFS_INITRD, VFS_RDONLY);

    /* Auto-mount FAT16 at "/mnt/fat" if available */
    extern int fat16_init(void);
    if (fat16_init()) {
        vfs_mount("/mnt/fat", VFS_FAT16, 0);
        terminal_printf("[VFS] Mounted FAT16 at /mnt/fat\n");
    }

    /* Auto-mount EXT2 at "/mnt/ext2" if available */
    extern void ext2_init(void);
    extern int  ext2_is_mounted(void);
    ext2_init();
    if (ext2_is_mounted()) {
        vfs_mount("/mnt/ext2", VFS_EXT2, VFS_RDONLY);
        terminal_printf("[VFS] Mounted EXT2 at /mnt/ext2\n");
    }
}

int vfs_mount(const char* point, vfs_type_t type, uint32_t flags) {
    /* Check for duplicate */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && strcmp(mounts[i].mount_point, point) == 0)
            return -1; /* already mounted */
    }
    /* Find free slot */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            strncpy(mounts[i].mount_point, point, 31);
            mounts[i].mount_point[31] = '\0';
            mounts[i].type   = type;
            mounts[i].flags  = flags;
            mounts[i].active = 1;
            return 0;
        }
    }
    return -1; /* table full */
}

int vfs_unmount(const char* point) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && strcmp(mounts[i].mount_point, point) == 0) {
            mounts[i].active = 0;
            return 0;
        }
    }
    return -1;
}

int vfs_ls(const char* path, char names[][64], int max) {
    vfs_mount_t* mt = vfs_find_mount(path);
    if (!mt) return -1;

    int count = 0;

    switch (mt->type) {
    case VFS_INITRD: {
        /* List all files in the initrd tar archive */
        char name[100];
        int idx = 0;
        while (count < max && tar_get_file_at_index(idx, name)) {
            strncpy(names[count], name, 63);
            names[count][63] = '\0';
            count++;
            idx++;
        }
        break;
    }
    case VFS_FAT16: {
        fat16_file_info_t files[64];
        int n = fat16_list_files(files, max < 64 ? max : 64);
        if (n < 0) n = 0;
        for (int i = 0; i < n && count < max; i++) {
            strncpy(names[count], files[i].name, 63);
            names[count][63] = '\0';
            count++;
        }
        break;
    }
    case VFS_EXT2: {
        int cap = max < 128 ? max : 128;
        char e2names[128][13];
        int n = ext2_ls(e2names, cap);
        if (n < 0) n = 0;
        for (int i = 0; i < n && count < max; i++) {
            strncpy(names[count], e2names[i], 63);
            names[count][63] = '\0';
            count++;
        }
        break;
    }
    default:
        break;
    }

    return count;
}

int vfs_open(const char* path, int flags) {
    (void)flags;
    vfs_mount_t* mt = vfs_find_mount(path);
    if (!mt) return -1;

    const char* rel = vfs_strip_prefix(path, mt->mount_point);

    /* Find free fd slot */
    int fd = -1;
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (open_files[i].state == VFS_FD_FREE) { fd = i; break; }
    }
    if (fd < 0) return -1;

    vfs_fd_t* f = &open_files[fd];
    f->fs_type = mt->type;
    strncpy(f->path, rel, 127);
    f->path[127] = '\0';
    f->pos  = 0;
    f->data = NULL;
    f->size = 0;

    switch (mt->type) {
    case VFS_INITRD: {
        size_t sz = 0;
        void* ptr = tar_get_file(rel, &sz);
        if (!ptr) return -1;
        f->data = ptr;
        f->size = (uint32_t)sz;
        break;
    }
    case VFS_FAT16:
        f->data = NULL;
        f->size = 0;
        break;
    case VFS_EXT2: {
        static uint8_t ext2_tmp[65536];
        int r = ext2_read_file(rel, ext2_tmp, sizeof(ext2_tmp));
        if (r < 0) return -1;
        void* ebuf = kmalloc((uint32_t)r);
        if (!ebuf) return -1;
        memcpy(ebuf, ext2_tmp, (uint32_t)r);
        f->data = ebuf;
        f->size = (uint32_t)r;
        break;
    }
    default:
        return -1;
    }

    f->state = VFS_FD_USED;
    return fd;
}

int vfs_read(int fd, void* buf, uint32_t len) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return -1;
    vfs_fd_t* f = &open_files[fd];
    if (f->state != VFS_FD_USED) return -1;

    switch (f->fs_type) {
    case VFS_INITRD: {
        if (!f->data) return -1;
        uint32_t avail = f->size - f->pos;
        if (len > avail) len = avail;
        memcpy(buf, (uint8_t*)f->data + f->pos, len);
        f->pos += len;
        return (int)len;
    }
    case VFS_FAT16: {
        int r = fat16_read_file(f->path, (uint8_t*)buf, len);
        if (r < 0) return -1;
        if (f->pos > 0) return 0;
        f->pos += (uint32_t)r;
        return r;
    }
    case VFS_EXT2: {
        if (!f->data) return -1;
        uint32_t avail = f->size - f->pos;
        if (len > avail) len = avail;
        memcpy(buf, (uint8_t*)f->data + f->pos, len);
        f->pos += len;
        return (int)len;
    }
    default:
        return -1;
    }
}

int vfs_write(int fd, const void* buf, uint32_t len) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return -1;
    vfs_fd_t* f = &open_files[fd];
    if (f->state != VFS_FD_USED) return -1;

    switch (f->fs_type) {
    case VFS_FAT16:
        return fat16_write_file(f->path, (const uint8_t*)buf, len);
    case VFS_INITRD:
        return -1; /* read-only */
    default:
        return -1;
    }
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FDS) return -1;
    vfs_fd_t* f = &open_files[fd];
    if (f->fs_type == VFS_EXT2 && f->data) {
        kfree(f->data);
        f->data = NULL;
    }
    f->state = VFS_FD_FREE;
    return 0;
}

void vfs_print_mounts(void) {
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    terminal_writestring("\n  Mount Table\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    terminal_writestring("  ──────────────────────────────────────────\n");
    terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    int found = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) continue;
        terminal_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        terminal_printf("  %-20s", mounts[i].mount_point);
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        terminal_printf("%-8s", vfs_type_name(mounts[i].type));
        terminal_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        terminal_printf("%s\n", (mounts[i].flags & VFS_RDONLY) ? "ro" : "rw");
        terminal_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        found++;
    }
    if (!found) terminal_writestring("  (no mounts)\n");
    terminal_putchar('\n');
}
