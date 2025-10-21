#define _XOPEN_SOURCE 700  /* ensure posix_memalign prototype on Linux */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "mlpt.h"

/* Page table base register */
size_t ptbr = 0;

static inline size_t page_size(void) {
    return 1ULL << POBITS;
}

static inline size_t idx_bits(void) {
    /* 8-byte PTEs -> entries = 2^(POBITS-3) -> index consumes POBITS-3 bits */
    return POBITS - 3;
}

static inline size_t offset_mask(void) {
    return page_size() - 1ULL;
}

static inline size_t base_mask(void) {
    /* Zero the low POBITS bits to get the page base (and drop flags). */
    return ~offset_mask();
}

/* Extract the index for a given level from a virtual address.
 * Level 0 = root, Level (LEVELS-1) = leaf.
 */
static inline size_t va_index(size_t va, int level) {
    size_t bits_per_level = idx_bits();
    size_t shift = POBITS + (size_t)(LEVELS - 1 - level) * bits_per_level;
    size_t mask  = (1ULL << bits_per_level) - 1ULL;
    return (va >> shift) & mask;
}

/* Allocate one zeroed, page-aligned page. Aborts on failure. */
static void *alloc_page_zeroed(void) {
    void *p = NULL;
    int rc = posix_memalign(&p, page_size(), page_size());
    if (rc != 0 || p == NULL) {
        abort();
    }
    memset(p, 0, page_size());
    return p;
}

/* Translate a virtual address to a physical address using the current page table.
 * Returns all-ones (~0) if unmapped.
 */
size_t translate(size_t va) {
    if (ptbr == 0) {
        return ~(size_t)0;
    }

    size_t *table = (size_t *) ptbr;

    for (int level = 0; level < LEVELS; ++level) {
        size_t idx = va_index(va, level);
        size_t pte = table[idx];

        if ((pte & 1ULL) == 0ULL) {
            return ~(size_t)0;
        }

        size_t base = pte & base_mask();

        if (level < LEVELS - 1) {
            table = (size_t *) base;  
        }
        else {
            return base | (va & offset_mask());
        }
    }

    /* Should be unreachable for LEVELS >= 1. */
    return ~(size_t)0;
}

/* Allocates and maps the virtual page that begins at start_va, if needed.
 * Returns:
 *   -1 if start_va is not page-aligned,
 *    0 if the page was already mapped,
 *    1 if a new mapping was created.
 */
int allocate_page(size_t start_va) {
    /* Alignment check */
    if ((start_va & offset_mask()) != 0ULL) {
        return -1;
    }

    /* Ensure root page table exists. */
    if (ptbr == 0) {
        ptbr = (size_t) alloc_page_zeroed();
    }

    size_t *table = (size_t *) ptbr;

    for (int level = 0; level < LEVELS; ++level) {
        size_t idx = va_index(start_va, level);
        size_t pte = table[idx];

        if (level < LEVELS - 1) {
            /* Ensure next-level table exists. */
            if ((pte & 1ULL) == 0ULL) {
                void *child_tbl = alloc_page_zeroed();
                table[idx] = ((size_t) child_tbl & base_mask()) | 1ULL;
                pte = table[idx];
            }
            table = (size_t *) (pte & base_mask());
        }
        else {
            /* Ensure data page exists or detect already mapped. */
            if ((pte & 1ULL) != 0ULL) {
                return 0;
            }
            void *data_page = alloc_page_zeroed();
            table[idx] = ((size_t) data_page & base_mask()) | 1ULL;
            return 1;
        }
    }

    /* Unreachable. */
    return 0;
}
