#pragma once
#include <stdint.h>

typedef enum { VFS_FAT16=0, VFS_FAT32, VFS_EXT2, VFS_INITRD, VFS_TMPFS } vfs_type_t;

typedef struct {
    char       mount_point[32]; /* e.g. "/", "/mnt/fat", "/mnt/ext2" */
    vfs_type_t type;
    int        active;
    uint32_t   flags;  /* VFS_RDONLY=1 */
} vfs_mount_t;

#define VFS_MAX_MOUNTS 8
#define VFS_RDONLY     1

void vfs_init(void);
int  vfs_mount(const char* point, vfs_type_t type, uint32_t flags);
int  vfs_unmount(const char* point);
int  vfs_open(const char* path, int flags);   /* returns fd or -1 */
int  vfs_read(int fd, void* buf, uint32_t len);
int  vfs_write(int fd, const void* buf, uint32_t len);
int  vfs_close(int fd);
int  vfs_ls(const char* path, char names[][64], int max);
void vfs_print_mounts(void);
