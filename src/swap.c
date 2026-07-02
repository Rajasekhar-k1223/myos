#include "swap.h"
#include "paging.h"
#include "pmm.h"
#include "fat16.h"
#include "kernel.h"
#include "string.h"
#include <stdint.h>

/* ── Internal state ─────────────────────────────────────────────────────── */
static int      swap_available  = 0;
static uint8_t  slot_used[SWAP_MAX_PAGES]; /* 1 = slot occupied */

/*
 * Swap file layout:
 *   offset = slot * 4096
 *   each slot holds exactly one 4096-byte page
 *
 * We keep a single in-memory image of the swap file and write it back to
 * FAT16 on each swap_out / after swap_in frees a slot.  This is intentionally
 * simple for a hobby OS — a production implementation would use ATA sectors
 * directly (no FAT overhead per-access).
 */
static uint8_t swap_image[SWAP_MAX_PAGES * 4096];

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Find a free swap slot; returns -1 if all slots are full. */
static int alloc_slot(void) {
    for (int i = 0; i < SWAP_MAX_PAGES; i++)
        if (!slot_used[i]) return i;
    return -1;
}

/* Write the in-memory swap image back to the FAT16 file. */
static int flush_swap_file(void) {
    int rc = fat16_write_file(SWAP_FILE, swap_image, sizeof(swap_image));
    if (rc < 0) {
        terminal_printf("[SWAP] Warning: failed to flush swap file to disk\n");
        return -1;
    }
    return 0;
}

/* Load the swap image from FAT16 into RAM (called once at init). */
static void load_swap_file(void) {
    int bytes = fat16_read_file(SWAP_FILE, swap_image, sizeof(swap_image));
    if (bytes < 0)
        memset(swap_image, 0, sizeof(swap_image)); /* treat as empty */
}

/* ── swap_init ───────────────────────────────────────────────────────────── */
void swap_init(void) {
    memset(slot_used, 0, sizeof(slot_used));
    memset(swap_image, 0, sizeof(swap_image));

    /* Check FAT16 is mounted by attempting a read (returns -1 if not ready) */
    uint8_t probe[1];
    int rc = fat16_read_file(SWAP_FILE, probe, 1);
    if (rc < 0) {
        /* File doesn't exist yet — create an empty one */
        rc = fat16_write_file(SWAP_FILE, swap_image, sizeof(swap_image));
        if (rc < 0) {
            terminal_printf("[SWAP] FAT16 unavailable — swap disabled\n");
            swap_available = 0;
            return;
        }
    } else {
        load_swap_file();
    }

    swap_available = 1;
    terminal_printf("[SWAP] Swap initialised: %u slots x 4096 bytes (%s)\n",
                    SWAP_MAX_PAGES, SWAP_FILE);
}

/* ── swap_is_available ───────────────────────────────────────────────────── */
int swap_is_available(void) {
    return swap_available;
}

/* ── swap_out ────────────────────────────────────────────────────────────── */
/*
 * Evict the resident page at virt_addr (backed by phys_addr) to the swap
 * file, then free the physical frame and mark the PTE as swapped-out.
 *
 * PTE encoding after swap-out:
 *   bits 31:12 = slot index
 *   bit  9     = 1  (software "in swap" flag)
 *   bit  0     = 0  (not present)
 */
int swap_out(uint32_t virt_addr, uint32_t phys_addr) {
    if (!swap_available) return -1;

    int slot = alloc_slot();
    if (slot < 0) {
        terminal_printf("[SWAP] No free swap slots\n");
        return -1;
    }

    /* Copy page contents into the in-memory image */
    memcpy(swap_image + slot * 4096, (void*)phys_addr, 4096);
    slot_used[slot] = 1;

    /* Persist to FAT16 */
    if (flush_swap_file() < 0) {
        slot_used[slot] = 0;
        return -1;
    }

    /* Update PTE: mark not-present, set bit9, encode slot in bits 31:12 */
    extern uint32_t* paging_get_current_dir();
    uint32_t pde_idx = virt_addr >> 22;
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF;
    uint32_t* pd = paging_get_current_dir();

    if (!(pd[pde_idx] & 1)) return -1;
    uint32_t* pt = (uint32_t*)(pd[pde_idx] & ~0xFFF);

    pt[pte_idx] = ((uint32_t)slot << 12) | (1u << 9); /* not-present, in-swap */

    /* Flush TLB for this page */
    asm volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");

    /* Free the physical frame */
    pmm_free_frame((void*)phys_addr);

    terminal_printf("[SWAP] Swapped out virt=0x%x to slot %d\n", virt_addr, slot);
    return slot;
}

/* ── swap_in ─────────────────────────────────────────────────────────────── */
/*
 * Bring the swapped page for virt_addr back into a new physical frame.
 * Reads slot index from the PTE (bits 31:12), loads the data, updates PTE.
 */
int swap_in(uint32_t virt_addr) {
    if (!swap_available) return -1;

    extern uint32_t* paging_get_current_dir();
    uint32_t pde_idx = virt_addr >> 22;
    uint32_t pte_idx = (virt_addr >> 12) & 0x3FF;
    uint32_t* pd = paging_get_current_dir();

    if (!(pd[pde_idx] & 1)) {
        terminal_printf("[SWAP] swap_in: PDE not present for 0x%x\n", virt_addr);
        return -1;
    }

    uint32_t* pt = (uint32_t*)(pd[pde_idx] & ~0xFFF);
    uint32_t pte = pt[pte_idx];

    /* Verify this is actually a swapped-out page */
    if ((pte & 1) || !(pte & (1u << 9))) {
        terminal_printf("[SWAP] swap_in: PTE at 0x%x is not a swap PTE (0x%x)\n",
                        virt_addr, pte);
        return -1;
    }

    int slot = (int)(pte >> 12);
    if (slot < 0 || slot >= SWAP_MAX_PAGES || !slot_used[slot]) {
        terminal_printf("[SWAP] swap_in: invalid slot %d for 0x%x\n", slot, virt_addr);
        return -1;
    }

    /* Allocate a new physical frame */
    void* new_frame = pmm_alloc_frame();
    if (!new_frame) {
        terminal_printf("[SWAP] swap_in: out of physical memory\n");
        return -1;
    }

    /* Copy page data from swap image */
    memcpy(new_frame, swap_image + slot * 4096, 4096);

    /* Release the swap slot */
    slot_used[slot] = 0;

    /* Map the new frame: present, writable, user */
    pt[pte_idx] = (uint32_t)(uintptr_t)new_frame | 7; /* Present, R/W, User */

    /* Flush TLB */
    asm volatile("invlpg (%0)" :: "r"(virt_addr) : "memory");

    terminal_printf("[SWAP] Swapped in virt=0x%x from slot %d\n", virt_addr, slot);
    return 0;
}

/* ── swap_handle_fault ───────────────────────────────────────────────────── */
/*
 * Called from paging_page_fault_handler() when present=0 and bit9=1 in PTE.
 * If swap_in fails, kill the faulting task.
 */
void swap_handle_fault(uint32_t fault_addr) {
    uint32_t page_addr = fault_addr & ~0xFFFu;
    if (swap_in(page_addr) < 0) {
        terminal_printf("[SWAP] Fatal: swap_in failed for 0x%x — killing task\n",
                        fault_addr);
        extern void task_exit(void);
        task_exit();
    }
}
