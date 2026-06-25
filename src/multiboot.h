#ifndef MULTIBOOT_H
#define MULTIBOOT_H

/* ── Multiboot2 ─────────────────────────────────────────────────────────────
 * Spec: https://www.gnu.org/software/grub/manual/multiboot2/
 * On UEFI:  GRUB uses GOP  → framebuffer tag carries GOP linear buffer.
 * On BIOS:  GRUB uses VBE  → framebuffer tag carries VBE linear buffer.
 * The kernel code is identical for both.
 * ───────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

#define MULTIBOOT2_MAGIC  0x36d76289u   /* value GRUB puts in EAX */

/* ── Tag types ── */
#define MB2_TAG_END          0
#define MB2_TAG_CMDLINE      1
#define MB2_TAG_LOADER_NAME  2
#define MB2_TAG_MODULE       3
#define MB2_TAG_BASIC_MEM    4
#define MB2_TAG_BOOTDEV      5
#define MB2_TAG_MMAP         6
#define MB2_TAG_VBE          7
#define MB2_TAG_FRAMEBUFFER  8
#define MB2_TAG_ELF_SECTIONS 9
#define MB2_TAG_APM          10

/* ── Memory map entry types ── */
#define MB2_MMAP_AVAILABLE   1
#define MB2_MMAP_RESERVED    2
#define MB2_MMAP_ACPI        3
#define MB2_MMAP_NVS         4
#define MB2_MMAP_BADRAM      5

/* ── Generic tag header (all tags start with these 8 bytes) ── */
struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

/* ── Tag 3: Module (initrd) ── */
struct mb2_tag_module {
    uint32_t type;          /* 3 */
    uint32_t size;
    uint32_t mod_start;     /* physical address of module data */
    uint32_t mod_end;
    char     cmdline[0];    /* null-terminated string */
} __attribute__((packed));

/* ── Tag 6: Memory map ── */
struct mb2_tag_mmap {
    uint32_t type;          /* 6 */
    uint32_t size;
    uint32_t entry_size;    /* size of each entry (usually 24) */
    uint32_t entry_version; /* 0 */
    /* entries follow immediately */
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;          /* 1 = available RAM */
    uint32_t zero;          /* reserved */
} __attribute__((packed));

/* ── Tag 8: Framebuffer (GOP on UEFI, VBE on BIOS) ── */
struct mb2_tag_framebuffer {
    uint32_t type;              /* 8 */
    uint32_t size;
    uint64_t framebuffer_addr;  /* linear framebuffer physical base address */
    uint32_t framebuffer_pitch; /* bytes per scanline */
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;   /* bits per pixel (32 for RGB) */
    uint8_t  framebuffer_type;  /* 1 = RGB direct color */
    uint16_t reserved;
} __attribute__((packed));

/* ── Multiboot2 info structure ── */
struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
    /* variable-length tag list follows — iterate with mb2_first_tag() */
} __attribute__((packed));

/* ── Tag iteration helpers ── */
static inline struct mb2_tag* mb2_first_tag(struct mb2_info* info) {
    return (struct mb2_tag*)((uint8_t*)info + 8);
}

static inline struct mb2_tag* mb2_next_tag(struct mb2_tag* tag) {
    /* each tag is 8-byte aligned */
    uint32_t addr = (uint32_t)tag + tag->size;
    addr = (addr + 7) & ~7u;
    return (struct mb2_tag*)addr;
}

#endif /* MULTIBOOT_H */
