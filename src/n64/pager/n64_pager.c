/**
 * n64_pager.c -- demand paging of code/rodata from ROM (see n64_pager.h).
 *
 * MUST stay resident: compiled without LTO and placed in kseg0 by
 * cmake/n64_paged.ld. It may only call resident code (libdragon, libc).
 *
 * Mechanism
 *   - The paged image is linked at PG_VBASE (0xC0000000, kseg2) and stored in
 *     ROM as "<target>.pgc". Each TLB entry maps an even/odd pair of 16 KiB
 *     pages; with 16 KiB pages the caches' index bits lie inside the page
 *     offset, so virtual and physical indices agree and hit-invalidating a
 *     frame through its kseg0 address is enough to keep I/D caches coherent.
 *   - Any TLB miss in the paged range (refill or invalid entry) reaches the
 *     libdragon exception handler, which calls pager_exception(). That either
 *     re-installs the TLB entry of a resident page (a "refill") or loads the
 *     page from ROM into a frame (a "fault"), evicting with a clock policy.
 *   - libdragon clears EXL before calling handlers, so faults are re-entrant:
 *     paged code may run in interrupt callbacks and in the crash inspector.
 *   - Pages are mapped read-only (D=0): a stray write into code or rodata
 *     raises a TLB-modification exception and is reported as a crash.
 */

#include "n64_pager.h"

#include <libdragon.h>
#include <cop0.h>
#include <dma.h>
#include <exception.h>
#include <n64sys.h>
#include <malloc.h>
#include <string.h>

extern uint32_t rompak_search_ext(const char* ext);       // libdragon rompak.c

extern char __paged_start[];
extern char __paged_end[];

// Frame pool size. A target may define its own (strong) value.
__attribute__((weak)) uint32_t n64_pager_frames_requested = 160;   // 2.5 MiB

#define MAX_PAGES 4096u          // 64 MiB of paged image
#define NO_FRAME  0xFFFFu

static uint16_t s_pte[MAX_PAGES];            // page -> frame (NO_FRAME if absent)
typedef struct { int32_t page; uint8_t ref; } frame_t;
static frame_t*  s_frames;
static uint8_t*  s_pool;                     // kseg0 address of frame 0
static uint32_t  s_nframes, s_npages, s_rom, s_hand;
static n64_pager_stats_t s_stats;
static exception_handler_t s_prev_handler;
static int s_ready;

// ---------------------------------------------------------------------------
// TLB helpers
// ---------------------------------------------------------------------------
static inline void tlb_write_pagemask(uint32_t m) { asm volatile("mtc0 %0,$5; nop; nop" :: "r"(m)); }
// libdragon runs the CPU with 64-bit kernel addressing (SR.KX=1): EntryHi is a
// 64-bit register and kseg2 is the sign-extended 0xFFFFFFFFCxxxxxxx region.
static inline void tlb_write_hi(uint32_t v)       { asm volatile("dmtc0 %0,$10; nop; nop" :: "r"((int64_t)(int32_t)v)); }
static inline void tlb_write_lo0(uint32_t v)      { asm volatile("mtc0 %0,$2; nop; nop" :: "r"(v)); }
static inline void tlb_write_lo1(uint32_t v)      { asm volatile("mtc0 %0,$3; nop; nop" :: "r"(v)); }
static inline void tlb_write_index(uint32_t v)    { asm volatile("mtc0 %0,$0; nop; nop" :: "r"(v)); }
static inline void tlb_write_wired(uint32_t v)    { asm volatile("mtc0 %0,$6; nop; nop" :: "r"(v)); }
static inline uint32_t tlb_read_index(void)       { uint32_t v; asm volatile("mfc0 %0,$0; nop" : "=r"(v)); return v; }
static inline void tlb_probe(void)                { asm volatile("tlbp; nop; nop; nop" ::: "memory"); }
static inline void tlb_wi(void)                   { asm volatile("tlbwi; nop; nop; nop" ::: "memory"); }
static inline void tlb_wr(void)                   { asm volatile("tlbwr; nop; nop; nop" ::: "memory"); }

#define PAGEMASK_16K 0x00006000u

static inline uint32_t entry_lo(uint32_t page) {
    if (page >= s_npages || s_pte[page] == NO_FRAME) return 1u;     // invalid, global
    uint32_t phys = PhysicalAddr(s_pool + (uint32_t)s_pte[page] * PG_SIZE);
    // PFN | C=3 (cacheable, non-coherent) | D=0 (read-only) | V | G
    return ((phys >> 12) << 6) | (3u << 3) | 2u | 1u;
}

// Install (or refresh) the TLB entry for the pair containing `page`.
// If `insert` is false, only an existing entry is updated.
static void tlb_set_pair(uint32_t page, int insert) {
    uint32_t even = page & ~1u;
    uint32_t hi = PG_VBASE + even * PG_SIZE;                         // ASID 0
    uint32_t sr = C0_STATUS();
    C0_WRITE_STATUS(sr & ~1u);                                       // IE off while using TLB regs
    tlb_write_pagemask(PAGEMASK_16K);
    tlb_write_hi(hi);
    tlb_probe();
    uint32_t idx = tlb_read_index();
    if ((idx & 0x80000000u) && !insert) { C0_WRITE_STATUS(sr); return; }
    tlb_write_lo0(entry_lo(even));
    tlb_write_lo1(entry_lo(even + 1));
    tlb_write_hi(hi);
    if (idx & 0x80000000u) tlb_wr(); else tlb_wi();
    C0_WRITE_STATUS(sr);
}

static void tlb_reset(void) {
    tlb_write_wired(0);
    tlb_write_pagemask(PAGEMASK_16K);
    for (uint32_t i = 0; i < 32; ++i) {
        // Unique, never-matching kseg0 addresses (kseg0 is not TLB-mapped).
        tlb_write_hi(0x80000000u + i * 0x8000u);
        tlb_write_lo0(0);
        tlb_write_lo1(0);
        tlb_write_index(i);
        tlb_wi();
    }
}

// ---------------------------------------------------------------------------
// Paging
// ---------------------------------------------------------------------------
static uint32_t pick_frame(void) {
    for (;;) {
        frame_t* f = &s_frames[s_hand];
        uint32_t i = s_hand;
        s_hand = (s_hand + 1) % s_nframes;
        if (f->page < 0) return i;
        if (f->ref) { f->ref = 0; continue; }
        return i;
    }
}

static void load_page(uint32_t page) {
    uint32_t fi = pick_frame();
    frame_t* f = &s_frames[fi];
    if (f->page >= 0) {
        uint32_t old = (uint32_t)f->page;
        s_pte[old] = NO_FRAME;
        tlb_set_pair(old, 0);                    // drop the old mapping if cached
        ++s_stats.evictions;
        --s_stats.resident;
    }
    uint8_t* dst = s_pool + fi * PG_SIZE;
    data_cache_hit_writeback_invalidate(dst, PG_SIZE);
    dma_read(dst, s_rom + page * PG_SIZE, PG_SIZE);
    data_cache_hit_invalidate(dst, PG_SIZE);
    inst_cache_hit_invalidate(dst, PG_SIZE);
    f->page = (int32_t)page;
    f->ref = 1;
    s_pte[page] = (uint16_t)fi;
    ++s_stats.faults;
    ++s_stats.resident;
}

static void pager_exception(exception_t* ex) {
    uint32_t code = C0_GET_CAUSE_EXC_CODE(ex->regs->cr);
    if (code == EXCEPTION_CODE_TLB_LOAD_I_MISS || code == EXCEPTION_CODE_TLB_STORE_MISS) {
        uint32_t va = C0_BADVADDR();
        if (va >= PG_VBASE && va < PG_VBASE + s_npages * PG_SIZE) {
            uint32_t page = (va - PG_VBASE) >> PG_SHIFT;
            if (s_pte[page] == NO_FRAME) load_page(page);
            else { s_frames[s_pte[page]].ref = 1; ++s_stats.refills; }
            tlb_set_pair(page, 1);
            return;                              // retry the faulting instruction
        }
    }
    if (s_prev_handler) s_prev_handler(ex);
}

static void pager_init(void) {
    if (s_ready) return;
    s_ready = 1;

    s_npages = (uint32_t)(__paged_end - __paged_start) / PG_SIZE;
    if (s_npages == 0) return;                    // nothing is paged in this build
    assertf(s_npages <= MAX_PAGES, "paged image too large: %lu pages", (unsigned long)s_npages);

    s_rom = rompak_search_ext(".pgc");
    assertf(s_rom != 0, "pager: paged image (.pgc) missing from the ROM");

    s_nframes = n64_pager_frames_requested;
    if (s_nframes > s_npages) s_nframes = s_npages;
    s_pool = memalign(PG_SIZE, s_nframes * PG_SIZE);
    assertf(s_pool, "pager: cannot allocate %lu frames", (unsigned long)s_nframes);
    s_pool = (uint8_t*)CachedAddr(s_pool);
    s_frames = malloc(s_nframes * sizeof(frame_t));
    for (uint32_t i = 0; i < s_nframes; ++i) { s_frames[i].page = -1; s_frames[i].ref = 0; }
    for (uint32_t i = 0; i < MAX_PAGES; ++i) s_pte[i] = NO_FRAME;

    s_stats.pages = s_npages;
    s_stats.frames = s_nframes;

    tlb_reset();
    s_prev_handler = register_exception_handler(pager_exception);
}

void n64_pager_get_stats(n64_pager_stats_t* out) { *out = s_stats; }

// libdragon's entry point calls __do_global_ctors, which the link redirects
// (--wrap) to this function. It replaces libdragon's own wrapper so the pager
// is installed before anything paged runs: registering the exception frames
// already reads the (paged) .eh_frame, and every constructor is paged code.
// The rest mirrors libdragon's src/do_ctors.c.
typedef void (*func_ptr)(void);
extern func_ptr __CTOR_LIST__[];
extern func_ptr __CTOR_END__[];
extern char __EH_FRAME_BEGIN__[];
// libgcc's registration entry point. (__register_frame_info itself must not be
// named here: libdragon's archive defines a weak copy in do_ctors.o, and
// referencing it would pull that object -- and its __wrap___do_global_ctors --
// into the link.) __register_frame_info(b, o) == ..._bases(b, o, 0, 0).
extern void __register_frame_info_bases(const void* begin, void* object, void* tbase, void* dbase);
static uint32_t s_eh_frame_object[6];

void __wrap___do_global_ctors(void) {
    pager_init();
    __register_frame_info_bases(__EH_FRAME_BEGIN__, s_eh_frame_object, 0, 0);
    func_ptr* ctor = __CTOR_END__ - 2;          // g++ places a trailing 0
    func_ptr* sentinel = __CTOR_LIST__;
    assertf((uint32_t)sentinel[0] == 0xFFFFFFFFu &&
            (uint32_t)sentinel[(__CTOR_END__ - __CTOR_LIST__) - 1] == 0,
            "Invalid constructor sentinel, ensure you link via g++");
    while (ctor > sentinel) {
        (*ctor)();
        --ctor;
    }
}

// libdragon's entry point calls main() with a direct jump, which cannot reach
// kseg2. The paged program's entry point is n64_paged_main().
extern int n64_paged_main(void);
int main(void) {
    pager_init();
    int (*volatile entry)(void) = n64_paged_main;
    return entry();
}
