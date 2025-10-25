#pragma once
#include <stddef.h>

/**
 * Page table base register.
 * Declared here so tester code can look at it; because it is extern
 * you'll need to define it (without extern) in exactly one .c file.
 */
extern size_t ptbr;

/**
 * Given a virtual address, return the physical address.
 * Return a value consisting of all 1 bits (~0)
 * if this virtual address does not have a physical address.
 */
size_t translate(size_t va);

/**
 * Allocates and maps the virtual page which starts at virtual address `start_va`
 * (if it is not allocated already).
 *
 * Returns:
 *  -1 if `start_va` is not the address at the start of a page
 *   0 if the page is already allocated (no change)
 *   1 if a new mapping was created
 *
 * Any missing page tables and the data page are allocated with posix_memalign.
 */
int allocate_page(size_t start_va);

/* ======================  Part C: Deallocation API  ====================== */

/**
 * Deallocates the mapping for the virtual page that starts at `start_va`.
 * If this makes any page table empty, that table is freed and its parent PTE
 * cleared; if the root becomes empty, it is freed and `ptbr` is set to 0.
 *
 * Returns:
 *  -1 if `start_va` is not page-aligned
 *   0 if the page was already unmapped (no change)
 *   1 if the mapping existed and was removed
 */
int deallocate_page(size_t start_va);

/**
 * Deallocates `n_pages` consecutive pages starting at `start_va`
 * (pages processed independently via deallocate_page).
 *
 * Returns: number of pages actually deallocated (i.e., count of returns == 1).
 * If `start_va` is misaligned, returns 0 and makes no changes.
 */
size_t deallocate_range(size_t start_va, size_t n_pages);

/**
 * Frees the entire page-table tree and all mapped data pages.
 * After return, `ptbr == 0`.
 */
void destroy_all(void);
