// Part 1: ptbr + translate 

#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "mlpt.h"

size_t ptbr = 0;


static inline size_t page_size(void) {
    return 1ULL << POBITS;
}

static inline size_t idx_bits(void) {
    return POBITS - 3;
}

static inline size_t offset_mask(void) {
    return page_size() - 1ULL;
}

static inline size_t base_mask(void) {
    return ~offset_mask();
}

static inline size_t va_index(size_t va, int level) {
    size_t bits_per_level = idx_bits();
    size_t shift = POBITS + (size_t)(LEVELS - 1 - level) * bits_per_level;
    size_t mask  = (1ULL << bits_per_level) - 1ULL;
    return (va >> shift) & mask;
}

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

    return ~(size_t)0;
}

