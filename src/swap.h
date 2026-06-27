#pragma once
#include <stdint.h>

/* ── Swap Subsystem ──────────────────────────────────────────────────────────
 * Evicts LRU user pages to a FAT16 swap file ("pagefile.sys") and pages them
 * back in on demand via the page-fault handler.
 *
 * PTE encoding when a page is swapped out:
 *   bit  0 (Present)   = 0   — page not in RAM
 *   bit  9 (SW-avail)  = 1   — software "in swap" flag
 *   bits 31:12          = swap slot index (not a physical frame)
 *
 * The page-fault handler in paging.c checks: present=0 AND bit9=1, then
 * calls swap_handle_fault() to bring the page back.
 */

#define SWAP_FILE       "pagefile.sys"
#define SWAP_MAX_PAGES  256             /* maximum pages that can be swapped */

/* Initialise swap: open/create SWAP_FILE on FAT16, set swap_available flag */
void swap_init(void);

/*
 * swap_out — evict a resident page to the swap file.
 *   virt_addr : virtual address of the page (page-aligned)
 *   phys_addr : physical frame address
 * Writes 4096 bytes to swap slot, frees the frame, and marks the PTE with
 * the "in swap" encoding (present=0, bit9=1, slot in bits 31:12).
 * Returns the slot index on success, -1 on failure.
 */
int  swap_out(uint32_t virt_addr, uint32_t phys_addr);

/*
 * swap_in — read a swapped page back into a new physical frame.
 *   virt_addr : virtual address of the faulting page (page-aligned)
 * Allocates a new frame, reads 4096 bytes from the swap slot, updates the
 * PTE to present+writable, and releases the swap slot.
 * Returns 0 on success, -1 on failure.
 */
int  swap_in(uint32_t virt_addr);

/* Returns 1 if the swap subsystem is available (FAT16 + ATA present). */
int  swap_is_available(void);

/*
 * swap_handle_fault — called from paging_page_fault_handler when present=0
 * and bit9=1 in the PTE.  Delegates to swap_in().
 */
void swap_handle_fault(uint32_t fault_addr);
