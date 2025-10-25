#define _XOPEN_SOURCE 700  /* ensure posix_memalign prototype on Linux */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "mlpt.h"

/* Page table base register */
size_t ptbr = 0;

/* -------------------- Common helpers (derived from config.h) -------------------- */

static inline size_t page_size(void) {
    return 1ULL << POBITS;
}

static inline size_t idx_bits(void) {
    /* 8-byte PTEs -> entries = 2^(POBITS-3) -> index consumes POBITS-3 bits */
    return POBITS - 3;
}

static inline size_t entries_per_table(void) {
    return 1ULL << idx_bits();
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
    size_t shift = POBITS + (size_t) (LEVELS - 1 - level) * bits_per_level;
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

/* ------------------------------ Translation API ------------------------------ */

size_t translate(size_t va) {
    if (ptbr == 0) {
        return ~ (size_t) 0;
    }

    size_t *table = (size_t *) ptbr;

    for (int level = 0; level < LEVELS; ++level) {
        size_t idx = va_index(va, level);
        size_t pte = table[idx];

        if ((pte & 1ULL) == 0ULL) {
            return ~ (size_t) 0;
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
    return ~ (size_t) 0;
}

/* ------------------------------ Allocation API ------------------------------ */

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

/* ------------------------------ Deallocation (Part C) ------------------------------ */

/* Returns 1 if every PTE in tbl is invalid (LSB==0), 0 otherwise. */
static int table_is_empty(const size_t *tbl) {
    size_t n = entries_per_table();
    for (size_t i = 0; i < n; ++i) {
        if (tbl[i] & 1ULL) {
            return 0;
        }
    }
    return 1;
}

/* Walk down to the leaf, filling arrays with (table pointers, indices).
 * On success (path reachable), returns 1 and sets *leaf_pte_out to the leaf PTE slot.
 * If any intermediate is invalid, returns 0 and does not modify outputs.
 *
 * Note: We consider "reachable leaf slot" even if the leaf PTE is invalid;
 * the caller will inspect the leaf PTE validity.
 */
static int find_leaf_slot(size_t va,
                          size_t **tables /* out: tables[0..LEVELS-1] */,
                          size_t  *idxs   /* out: idxs[0..LEVELS-1] */,
                          size_t **leaf_pte_out) {
    if (ptbr == 0) {
        return 0;
    }
    size_t *table = (size_t *) ptbr;

    for (int level = 0; level < LEVELS; ++level) {
        size_t idx = va_index(va, level);
        size_t *pte_slot = &table[idx];

        /* Save traversal state for the caller. */
        tables[level] = table;
        idxs[level]   = idx;

        size_t pte = *pte_slot;

        if (level == LEVELS - 1) {
            *leaf_pte_out = pte_slot;
            return 1;
        }

        if ((pte & 1ULL) == 0ULL) {
            /* Missing next-level table: cannot reach leaf. */
            return 0;
        }

        table = (size_t *) (pte & base_mask());
    }

    return 0; /* Unreachable for LEVELS >= 1. */
}

/* Recursively free the entire subtree rooted at `table` at level `level`.
 * Frees data pages under leaf tables and frees all page-table pages.
 * Caller clears parent PTE and resets ptbr when done.
 */
static void free_subtree(size_t *table, int level) {
    size_t n = entries_per_table();

    if (level < LEVELS - 1) {
        /* Non-leaf: iterate children. */
        for (size_t i = 0; i < n; ++i) {
            size_t pte = table[i];
            if ((pte & 1ULL) == 0ULL) {
                continue;
            }
            size_t base = pte & base_mask();

            if (level + 1 < LEVELS - 1) {
                /* Next level is still a page table. */
                free_subtree((size_t *) base, level + 1);
            }
            else {
                /* Next level is a leaf table: free all data pages then the leaf table. */
                size_t *leaf_tbl = (size_t *) base;
                size_t ln = entries_per_table();
                for (size_t j = 0; j < ln; ++j) {
                    size_t lpte = leaf_tbl[j];
                    if (lpte & 1ULL) {
                        void *data_page = (void *) (lpte & base_mask());
                        free(data_page);
                        leaf_tbl[j] = 0;
                    }
                }
                free(leaf_tbl);
            }

            table[i] = 0;
        }
    }
    else {
        /* If called on a leaf table, free all data pages it references. */
        for (size_t j = 0; j < n; ++j) {
            size_t lpte = table[j];
            if (lpte & 1ULL) {
                void *data_page = (void *) (lpte & base_mask());
                free(data_page);
                table[j] = 0;
            }
        }
    }

    free(table);
}

int deallocate_page(size_t start_va) {
    /* Alignment check */
    if ((start_va & offset_mask()) != 0ULL) {
        return -1;
    }

    if (ptbr == 0) {
        return 0; /* already unmapped */
    }

    size_t *tables[LEVELS];
    size_t  idxs[LEVELS];
    size_t *leaf_slot = NULL;

    if (!find_leaf_slot(start_va, tables, idxs, &leaf_slot)) {
        return 0; /* no path -> unmapped */
    }

    size_t lpte = *leaf_slot;
    if ((lpte & 1ULL) == 0ULL) {
        return 0; /* leaf invalid -> unmapped */
    }

    /* Free data page and clear leaf PTE. */
    void *data_page = (void *) (lpte & base_mask());
    free(data_page);
    *leaf_slot = 0;

    /* Prune upward: free empty tables; if root empty -> free and ptbr=0. */
    for (int level = LEVELS - 1; level >= 0; --level) {
        size_t *tbl = tables[level];

        if (!table_is_empty(tbl)) {
            break; /* this level still has mappings */
        }

        if (level == 0) {
            free(tbl);
            ptbr = 0;
            break;
        }

        size_t *parent_tbl = tables[level - 1];
        size_t   parent_ix = idxs[level - 1];
        free(tbl);
        parent_tbl[parent_ix] = 0;
    }

    return 1;
}

size_t deallocate_range(size_t start_va, size_t n_pages) {
    if ((start_va & offset_mask()) != 0ULL) {
        return 0;
    }

    size_t count = 0;
    size_t va = start_va;
    size_t step = (size_t) 1 << POBITS;

    for (size_t i = 0; i < n_pages; ++i, va += step) {
        int rc = deallocate_page(va);
        if (rc == 1) {
            count += 1;
        }
        else if (rc == -1) {
            break; /* should not happen due to stepping by page */
        }
    }

    return count;
}

void destroy_all(void) {
    if (ptbr == 0) {
        return;
    }

    if (LEVELS == 1) {
        /* Root is the leaf table. */
        size_t *leaf_tbl = (size_t *) ptbr;
        size_t n = entries_per_table();
        for (size_t j = 0; j < n; ++j) {
            size_t pte = leaf_tbl[j];
            if (pte & 1ULL) {
                void *data_page = (void *) (pte & base_mask());
                free(data_page);
                leaf_tbl[j] = 0;
            }
        }
        free(leaf_tbl);
        ptbr = 0;
        return;
    }

    /* LEVELS >= 2: free recursively from root. */
    size_t *root_tbl = (size_t *) ptbr;
    free_subtree(root_tbl, 0);
    ptbr = 0;
}
