#pragma once
/**
 * n64_pager.h -- demand paging of code and read-only data from cartridge ROM.
 *
 * The N64 has 8 MiB of RAM; the Wesnoth engine's code alone is larger. Code,
 * read-only data and C++ exception tables are therefore linked at virtual
 * address PG_VBASE (kseg2, TLB-mapped) and stored in ROM as a separate blob
 * (".pgc" in the ROM table of contents). The pager maps 16 KiB pages of that
 * blob into a pool of RAM frames on first touch (TLB miss) and evicts with a
 * clock policy, so only the working set occupies RAM.
 *
 * Only the pager, libdragon and the C/C++ runtime libraries stay resident;
 * see cmake/n64_paged.ld. Paged code must be compiled with -mlong-calls
 * because kseg2 and kseg0 are different 256 MiB jump regions.
 *
 * Installed automatically before global constructors run: the pager provides
 * __wrap___do_global_ctors (libdragon's entry point calls it through --wrap).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PG_VBASE   0xC0000000u
#define PG_SHIFT   14u
#define PG_SIZE    (1u << PG_SHIFT)
#define VH_BASE    0xD0000000u      // swappable heap (SummerCart64 only)

typedef struct {
    uint32_t pages;          // pages in the paged image
    uint32_t frames;         // RAM frames in the pool
    uint32_t resident;       // frames currently holding a page
    uint32_t faults;         // page loads from ROM
    uint32_t refills;        // TLB refills of already-resident pages
    uint32_t evictions;
    uint32_t heap_pages;     // swappable heap size in pages (0 without SC64)
    uint32_t writebacks;     // dirty heap pages written to swap
    uint32_t swapins;        // heap pages read back from swap
} n64_pager_stats_t;

void n64_pager_get_stats(n64_pager_stats_t* out);

// Log the interrupted PC and pager counters every 15 s (diagnostics).
void n64_pager_start_watchdog(void);

// Size in bytes of the swappable heap at VH_BASE (0 if not available).
uint32_t n64_pager_heap_size(void);

#ifdef __cplusplus
}
#endif
