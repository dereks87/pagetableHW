# Multi-Level Page Table

## How to customize `config.h` (with guidance)
#define LEVELS  1   /* number of levels in the page table */
#define POBITS  12  /* number of bits in the page offset */
Pages are size 2^POBITS bytes; each page table occupies one page and has 2^(POBITS-3) 8-byte entries.

Guidance:
Typical testing uses LEVELS=1, POBITS=12 (4 KiB pages, 512 entries/table).
Increasing LEVELS increases tree depth (larger virtual spaces).
Increasing POBITS increases page size and the number of entries per table.




De-allocation interface

Function prototypes:
int deallocate_page(size_t start_va);
size_t deallocate_range(size_t start_va, size_t n_pages);
void destroy_all(void);


Argument meanings:
start_va: virtual address at the start of a page (must be page-aligned).
n_pages: number of consecutive pages beginning at start_va.


What each function deallocates:
deallocate_page(start_va):
Frees the data page mapped at start_va (if present) and clears the leaf PTE.
If any page table becomes empty as a result, that table is freed and its parent PTE cleared.
If the root becomes empty, it is freed and ptbr is set to 0.
Returns -1 if start_va is not page-aligned, 0 if already unmapped, 1 if deallocated.

deallocate_range(start_va, n_pages):
Applies deallocate_page to each page in the range [start_va, start_va + n_pages*page_size).
Returns the count of pages actually deallocated.

destroy_all():
Frees all data pages and all page tables in the current tree; after return, ptbr == 0.