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

// ---------------------------------------------------------------------------
// Regions. Page indices are global: code pages [0, s_ncode), heap pages
// [s_ncode, s_ncode + s_nheap).
//   code  PG_VBASE (0xC0000000)  read-only, backed by the ROM (.pgc)
//   heap  VH_BASE  (0xD0000000)  read/write, zero on first touch, backed by
//                               SummerCart64 SDRAM (swap) beyond the ROM
// ---------------------------------------------------------------------------
#define MAX_PAGES 8192u          // 128 MiB of virtual space
#define NO_FRAME  0xFFFFu
#define F_DIRTY   1u             // heap page modified since loaded
#define F_SWAPPED 2u             // heap page has a copy in swap

// SummerCart64 (registers as in libdragon's usb.c)
#define SC64_REG_SR_CMD     0x1FFF0000u
#define SC64_REG_DATA_0     0x1FFF0004u
#define SC64_REG_DATA_1     0x1FFF0008u
#define SC64_REG_IDENTIFIER 0x1FFF000Cu
#define SC64_REG_KEY        0x1FFF0010u
#define SC64_V2_IDENTIFIER  0x53437632u
#define SC64_CMD_CONFIG_SET 'C'
#define SC64_CFG_ROM_WRITE_ENABLE 1u
// Swap: cartridge SDRAM from the end of the ROM image up to just below the
// IS-Viewer window. The ROM's last TOC entry is an end marker ("rom.end",
// added by n64_create_paged_rom), so the swap never overlaps ROM data.
#define SWAP_CART_END       0x13FC0000u
static uint32_t s_swap_base;

static uint16_t s_pte[MAX_PAGES];            // page -> frame (NO_FRAME if absent)
static uint8_t  s_flags[MAX_PAGES];
typedef struct { int32_t page; uint8_t ref; } frame_t;
static frame_t*  s_frames;
static uint8_t*  s_pool;                     // kseg0 address of frame 0
static uint32_t  s_nframes, s_ncode, s_nheap, s_rom, s_hand;
static n64_pager_stats_t s_stats;
static exception_handler_t s_prev_handler;
static int s_ready;

static inline int is_heap(uint32_t page) { return page >= s_ncode; }
static inline uint32_t page_va(uint32_t page) {
    return is_heap(page) ? VH_BASE + (page - s_ncode) * PG_SIZE : PG_VBASE + page * PG_SIZE;
}
// Global page index of a virtual address, or -1.
static inline int32_t va_page(uint32_t va) {
    if (va >= PG_VBASE && va < PG_VBASE + s_ncode * PG_SIZE) return (int32_t)((va - PG_VBASE) >> PG_SHIFT);
    if (va >= VH_BASE && va < VH_BASE + s_nheap * PG_SIZE) return (int32_t)(s_ncode + ((va - VH_BASE) >> PG_SHIFT));
    return -1;
}

// Heap pages beyond what the cartridge SDRAM holds swap to a file on the SD
// card (attached after the card is mounted, see n64_pager_attach_sd_swap).
#define SD_SWAP_MAX_PAGES   4096u            /* 64 MiB */
#define SD_UNIT             4096u            /* swap file mapped in 4 KiB units */
static uint32_t  s_nsdram;                   // heap pages backed by SDRAM
static uint32_t* s_sd_lba;                   // first sector of each 4 KiB unit of the swap file
static uint32_t  s_sd_pages;                 // pages the attached swap file holds
static inline int on_sd(uint32_t page) { return page >= s_ncode + s_nsdram; }
extern int cart_card_rd_dram(void* dram, uint32_t lba, uint32_t count);      // libdragon libcart
extern int cart_card_wr_dram(const void* dram, uint32_t lba, uint32_t count);

// Read/write one page of the SD swap file, merging contiguous units.
static void sd_page_io(uint32_t page, uint8_t* ram, int write) {
    const uint32_t idx = page - s_ncode - s_nsdram;
    assertf(s_sd_lba && idx < s_sd_pages,
            "out of swap: the heap outgrew the cartridge SDRAM and no SD swap file is attached (page %lu)",
            (unsigned long)idx);
    const uint32_t* lba = s_sd_lba + idx * (PG_SIZE / SD_UNIT);
    const uint32_t units = PG_SIZE / SD_UNIT, spu = SD_UNIT / 512;
    for (uint32_t u = 0; u < units;) {
        uint32_t n = 1;
        while (u + n < units && lba[u + n] == lba[u] + n * spu) ++n;
        int err = write ? cart_card_wr_dram(ram + u * SD_UNIT, lba[u], n * spu)
                        : cart_card_rd_dram(ram + u * SD_UNIT, lba[u], n * spu);
        assertf(err == 0, "SD swap %s failed (sector %lu)", write ? "write" : "read", (unsigned long)lba[u]);
        u += n;
    }
}

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
    if (page >= s_ncode + s_nheap || s_pte[page] == NO_FRAME) return 1u;   // invalid, global
    uint32_t phys = PhysicalAddr(s_pool + (uint32_t)s_pte[page] * PG_SIZE);
    // PFN | C=3 (cacheable, non-coherent) | D (writable: dirty heap pages only) | V | G
    uint32_t d = (is_heap(page) && (s_flags[page] & F_DIRTY)) ? 4u : 0u;
    return ((phys >> 12) << 6) | (3u << 3) | d | 2u | 1u;
}

// Install (or refresh) the TLB entry for the pair containing `page`.
// If `insert` is false, only an existing entry is updated. Pairs never straddle
// regions: both regions start on a 32 KiB boundary and the code region has an
// even page count.
static void tlb_set_pair(uint32_t page, int insert) {
    uint32_t even = is_heap(page) ? s_ncode + ((page - s_ncode) & ~1u) : (page & ~1u);
    uint32_t hi = page_va(even);                                     // ASID 0
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
// SummerCart64: unlock and enable writes to the cartridge SDRAM.
// ---------------------------------------------------------------------------
static void pi_wait(void) { while (*(volatile uint32_t*)0xA4600010 & 3) {} }
static void sc64_write(uint32_t reg, uint32_t v) { pi_wait(); io_write(reg, v); pi_wait(); }
static uint32_t sc64_read(uint32_t reg) { pi_wait(); return io_read(reg); }

static int sc64_enable_sdram_writes(void) {
    sc64_write(SC64_REG_KEY, 0);
    sc64_write(SC64_REG_KEY, 0x5F554E4Cu);
    sc64_write(SC64_REG_KEY, 0x4F434B5Fu);
    if (sc64_read(SC64_REG_IDENTIFIER) != SC64_V2_IDENTIFIER) return 0;
    sc64_write(SC64_REG_DATA_0, SC64_CFG_ROM_WRITE_ENABLE);
    sc64_write(SC64_REG_DATA_1, 1);
    sc64_write(SC64_REG_SR_CMD, SC64_CMD_CONFIG_SET);
    uint32_t sr;
    do { sr = sc64_read(SC64_REG_SR_CMD); } while (sr & (1u << 31));
    return !(sr & (1u << 30));
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

// ---------------------------------------------------------------------------
// Compressed swap. Heap pages written back to the cartridge SDRAM are stored
// LZ4-compressed in slots of whole 1 KiB units (all-zero pages take none), so
// the virtual heap can be well over twice the SDRAM it uses. Slots are kept
// in per-size free lists and carved from the top of the swap area.
// ---------------------------------------------------------------------------
#define LZ4_STATIC_LINKING_ONLY
#include "lz4/lz4.h"
#define CS_UNIT        1024u
#define CS_CLASSES     (PG_SIZE / CS_UNIT)       // a page takes 1..16 units
#define CS_RAW         0xFFFFu                   // clen: stored uncompressed
#define CS_MAX_UNITS   65536u
extern int decompress_lz4_full_fast(const void* inbuf, int insize, void* outbuf);   // libdragon
static LZ4_stream_t s_lz4_state;
static uint8_t s_cbuf[LZ4_COMPRESSBOUND(PG_SIZE) + 16] __attribute__((aligned(16)));
static uint8_t s_dbuf[PG_SIZE + 16] __attribute__((aligned(16)));   // decoder may write 8 bytes past the end
static uint16_t s_slot[MAX_PAGES];             // first unit of the page's slot (0 = none)
static uint8_t  s_sunits[MAX_PAGES];           // units the slot spans
static uint16_t s_clen[MAX_PAGES];             // compressed bytes (CS_RAW: uncompressed)
static uint32_t s_cs_map[CS_MAX_UNITS / 32];   // allocation bitmap, 1 = used
static uint32_t s_cs_units, s_cs_used, s_cs_rover = 1;
static uint64_t s_cz_in, s_cz_out, s_cz_ticks;
static uint32_t s_cz_zero;

static inline int  cs_bit(uint32_t u) { return (s_cs_map[u >> 5] >> (u & 31)) & 1; }
static inline void cs_set(uint32_t u, uint32_t n, int v) {
    for (uint32_t i = u; i < u + n; ++i) {
        if (v) s_cs_map[i >> 5] |= 1u << (i & 31); else s_cs_map[i >> 5] &= ~(1u << (i & 31));
    }
    if (v) s_cs_used += n; else s_cs_used -= n;
}
static inline uint32_t cs_units_for(uint32_t page) {
    return s_clen[page] == CS_RAW ? CS_CLASSES : (s_clen[page] + CS_UNIT - 1) / CS_UNIT;
}
static void cs_release(uint32_t page) {
    if (!s_slot[page]) return;
    cs_set(s_slot[page], s_sunits[page], 0);
    s_slot[page] = 0;
    s_sunits[page] = 0;
}
static void cs_compact(void);
static uint32_t s_cs_compactions;

// Next-fit search for a run of free units (unit 0 is never used).
static int cs_find(uint32_t units, uint16_t* out) {
    for (int pass = 0; pass < 2; ++pass) {
        uint32_t u = pass ? 1 : s_cs_rover, run = 0, start = u;
        const uint32_t end = pass ? s_cs_rover + units : s_cs_units;
        for (; u < end && u < s_cs_units; ++u) {
            if ((u & 31) == 0 && run == 0 && s_cs_map[u >> 5] == 0xFFFFFFFFu) {
                u += 31;                           // whole word in use: skip it
                start = u + 1;
                continue;
            }
            if (cs_bit(u)) { run = 0; start = u + 1; continue; }
            if (++run == units) {
                cs_set(start, units, 1);
                s_cs_rover = start + units;
                *out = (uint16_t)start;
                return 1;
            }
        }
    }
    return 0;
}

// When the free space is enough but too fragmented, compact and retry.
static uint16_t cs_alloc(uint32_t units) {
    uint16_t u;
    if (cs_find(units, &u)) return u;
    if (s_cs_used + units + 1 <= s_cs_units) {
        cs_compact();
        s_cs_rover = 1;
        if (cs_find(units, &u)) return u;
    }
    assertf(0, "out of swap: the heap does not fit in the cartridge SDRAM even compressed "
               "(%lu of %lu KiB in use)", (unsigned long)s_cs_used, (unsigned long)s_cs_units);
    return 0;
}
static inline uint32_t cs_addr(uint16_t unit) { return s_swap_base + (uint32_t)unit * CS_UNIT; }

// Slide every stored page down to close the gaps (SDRAM -> RAM -> SDRAM).
static void cs_compact(void) {
    static uint16_t order[MAX_PAGES];
    uint32_t n = 0;
    for (uint32_t p = s_ncode; p < s_ncode + s_nheap; ++p)
        if (s_slot[p]) order[n++] = (uint16_t)p;
    // sort by slot position (insertion sort on a nearly sorted list is fine; n is a few thousand)
    for (uint32_t i = 1; i < n; ++i) {
        uint16_t v = order[i];
        uint32_t j = i;
        while (j > 0 && s_slot[order[j - 1]] > s_slot[v]) { order[j] = order[j - 1]; --j; }
        order[j] = v;
    }
    uint32_t next = 1;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t p = order[i], len = s_sunits[p];
        if (s_slot[p] != next) {
            const uint32_t bytes = len * CS_UNIT;
            data_cache_hit_writeback_invalidate(s_dbuf, bytes);
            dma_read(s_dbuf, cs_addr(s_slot[p]), bytes);
            dma_write(s_dbuf, cs_addr((uint16_t)next), bytes);
            s_slot[p] = (uint16_t)next;
        }
        next += len;
    }
    memset(s_cs_map, 0, sizeof s_cs_map);
    s_cs_used = 0;
    cs_set(0, next, 1);                              // unit 0 (reserved) and the packed slots
    s_cs_used -= 1;
    ++s_cs_compactions;
    debugf("[pager] compressed swap compacted: %lu KiB in %lu pages\n",
           (unsigned long)((next - 1) * CS_UNIT >> 10), (unsigned long)n);
}


// Write a heap page (frame contents) to compressed swap.
static void cs_store(uint32_t page, const uint8_t* src) {
    const uint64_t t0 = get_ticks();
    const uint64_t* w = (const uint64_t*)src;
    uint32_t i = 0;
    while (i < PG_SIZE / 8 && w[i] == 0) ++i;
    s_cz_in += PG_SIZE;
    if (i == PG_SIZE / 8) { cs_release(page); s_clen[page] = 0; ++s_cz_zero; return; }   // zero page
    int n = LZ4_compress_fast_extState(&s_lz4_state, (const char*)src, (char*)s_cbuf, PG_SIZE,
                                       (int)sizeof s_cbuf - 16, 1);
    const uint8_t* data = s_cbuf;
    if (n <= 0 || n > (int)(PG_SIZE - CS_UNIT)) { n = PG_SIZE; s_clen[page] = CS_RAW; data = src; }
    else s_clen[page] = (uint16_t)n;
    const uint32_t units = cs_units_for(page);
    if (s_slot[page] && units <= s_sunits[page]) {
        // fits in the old slot: keep it, give back the tail
        cs_set(s_slot[page] + units, s_sunits[page] - units, 0);
    } else {
        cs_release(page);
        s_slot[page] = cs_alloc(units);
    }
    s_sunits[page] = (uint8_t)units;
    if (data == s_cbuf) data_cache_hit_writeback(s_cbuf, (n + 15) & ~15);
    dma_write(data, cs_addr(s_slot[page]), (n + 1) & ~1);
    s_cz_out += (uint64_t)units * CS_UNIT;
    s_cz_ticks += get_ticks() - t0;
}

// Read a heap page back from compressed swap into a frame.
static void cs_load(uint32_t page, uint8_t* dst) {
    if (!s_slot[page]) { memset(dst, 0, PG_SIZE); return; }            // zero page
    if (s_clen[page] == CS_RAW) {
        data_cache_hit_writeback_invalidate(dst, PG_SIZE);
        dma_read(dst, cs_addr(s_slot[page]), PG_SIZE);
        data_cache_hit_invalidate(dst, PG_SIZE);
        return;
    }
    const uint32_t n = s_clen[page];
    data_cache_hit_writeback_invalidate(s_cbuf, (n + 15) & ~15u);
    dma_read(s_cbuf, cs_addr(s_slot[page]), (n + 1) & ~1u);
    data_cache_hit_invalidate(s_cbuf, (n + 15) & ~15u);
    const int got = decompress_lz4_full_fast(s_cbuf, (int)n, s_dbuf);
    assertf(got == (int)PG_SIZE, "swap: corrupt compressed page %lu (%d bytes)", (unsigned long)page, got);
    memcpy(dst, s_dbuf, PG_SIZE);
}

static void evict(uint32_t fi) {
    frame_t* f = &s_frames[fi];
    if (f->page < 0) return;
    uint32_t old = (uint32_t)f->page;
    s_pte[old] = NO_FRAME;
    tlb_set_pair(old, 0);                        // drop the mapping if cached
    if (is_heap(old) && (s_flags[old] & F_DIRTY)) {
        uint8_t* src = s_pool + fi * PG_SIZE;
        data_cache_hit_writeback(src, PG_SIZE);   // CPU writes -> RAM, then RAM -> SDRAM / SD
        if (on_sd(old)) { sd_page_io(old, src, 1); ++s_stats.sd_writes; }
        else cs_store(old, src);
        s_flags[old] = (uint8_t)((s_flags[old] & ~F_DIRTY) | F_SWAPPED);
        ++s_stats.writebacks;
    }
    f->page = -1;
    ++s_stats.evictions;
    --s_stats.resident;
}

static void load_page(uint32_t page) {
    uint32_t fi = pick_frame();
    evict(fi);
    frame_t* f = &s_frames[fi];
    uint8_t* dst = s_pool + fi * PG_SIZE;
    if (!is_heap(page)) {
        data_cache_hit_writeback_invalidate(dst, PG_SIZE);
        dma_read(dst, s_rom + page * PG_SIZE, PG_SIZE);
        data_cache_hit_invalidate(dst, PG_SIZE);
        inst_cache_hit_invalidate(dst, PG_SIZE);
    } else if (s_flags[page] & F_SWAPPED) {
        if (on_sd(page)) {
            data_cache_hit_writeback_invalidate(dst, PG_SIZE);
            sd_page_io(page, dst, 0);
            data_cache_hit_invalidate(dst, PG_SIZE);
            ++s_stats.sd_reads;
        } else {
            cs_load(page, dst);
        }
        ++s_stats.swapins;
    } else {
        memset(dst, 0, PG_SIZE);                 // fresh heap page
    }
    f->page = (int32_t)page;
    f->ref = 1;
    s_pte[page] = (uint16_t)fi;
    ++s_stats.faults;
    ++s_stats.resident;
}

// ---------------------------------------------------------------------------
// Thread-local storage. GCC accesses TLS with rdhwr $29 (MIPS32r2), which the
// VR4300 lacks; like the Linux kernel, emulate it in the Reserved Instruction
// handler. One thread: a single TLS block, thread pointer = block + 0x7000
// (the MIPS TLS ABI's TP offset).
// ---------------------------------------------------------------------------
extern char __tdata_start[], __tdata_end[], __tbss_start[], __tbss_end[];
static uint32_t s_thread_pointer;

static void tls_init(void) {
    uint32_t tdata = (uint32_t)(__tdata_end - __tdata_start);
    uint32_t total = (uint32_t)(__tbss_end - __tdata_start);
    if (__tbss_end < __tdata_end) total = tdata;
    uint8_t* block = memalign(16, total ? total : 16);
    assertf(block, "cannot allocate the TLS block");
    memcpy(block, __tdata_start, tdata);
    memset(block + tdata, 0, total - tdata);
    s_thread_pointer = (uint32_t)block + 0x7000u;
}

static int emulate_rdhwr(exception_t* ex) {
    if (ex->regs->cr & (1u << 31)) return 0;           // in a branch delay slot: not expected
    uint32_t insn = *(volatile uint32_t*)ex->regs->epc;
    if ((insn & 0xFFE0FFFFu) != 0x7C00E83Bu) return 0;  // rdhwr rt, $29
    uint32_t rt = (insn >> 16) & 31;
    if (rt) ex->regs->gpr[rt] = (uint64_t)(int64_t)(int32_t)s_thread_pointer;
    ex->regs->epc += 4;
    return 1;
}

// A kernel address held zero-extended in a 64-bit register (0x00000000_8xxxxxxx
// instead of 0xFFFFFFFF_8xxxxxxx) is an xuseg address under SR.KX=1 and
// faults. GCC-generated o64 code never does this, but a few paths in the
// runtime libraries can (see tools/patch-n64-libgcc.sh). Repair the base
// register of the faulting load/store and retry; log where it happened.
static int fix_zero_extended_base(exception_t* ex) {
    uint64_t bad;
    asm volatile("dmfc0 %0,$8" : "=r"(bad));
    if ((bad >> 32) != 0 || (uint32_t)bad < 0x80000000u) return 0;
    uint32_t pc = ex->regs->epc + ((ex->regs->cr & (1u << 31)) ? 4 : 0);
    if (pc < 0x80000000u || pc >= 0xC0000000u + s_ncode * PG_SIZE) return 0;
    uint32_t insn = *(volatile uint32_t*)pc;
    uint32_t op = insn >> 26;
    if (!(op >= 0x20 || op == 0x1A || op == 0x1B)) return 0;      // loads/stores
    uint32_t base = (insn >> 21) & 31;
    uint64_t v = ex->regs->gpr[base];
    if ((v >> 32) != 0 || !(v & 0x80000000u)) return 0;
    ex->regs->gpr[base] = (uint64_t)(int64_t)(int32_t)(uint32_t)v;
    static int logged;
    if (logged < 16) {
        ++logged;
        debugf("[pager] zero-extended pointer in $%lu at PC %08lx (ra %08lx): repaired\n",
               (unsigned long)base, (unsigned long)pc, (unsigned long)(uint32_t)ex->regs->gpr[31]);
    }
    return 1;
}

static void pager_exception(exception_t* ex) {
    uint32_t code = C0_GET_CAUSE_EXC_CODE(ex->regs->cr);
    if (code == EXCEPTION_CODE_RESERVED_INSTRUCTION && emulate_rdhwr(ex)) return;
    if (code == EXCEPTION_CODE_TLB_LOAD_I_MISS || code == EXCEPTION_CODE_TLB_STORE_MISS ||
        code == EXCEPTION_CODE_TLB_MODIFICATION) {
        int32_t page = va_page(C0_BADVADDR());
        if (page >= 0) {
            if (code == EXCEPTION_CODE_TLB_MODIFICATION) {
                // First write to a heap page since it was loaded: mark it dirty
                // and remap it writable. (Writes to code/rodata are bugs.)
                if (!is_heap((uint32_t)page) || s_pte[page] == NO_FRAME) goto unhandled;
                s_flags[page] |= F_DIRTY;
            } else if (s_pte[page] == NO_FRAME) {
                load_page((uint32_t)page);
            } else {
                s_frames[s_pte[page]].ref = 1;
                ++s_stats.refills;
            }
            tlb_set_pair((uint32_t)page, 1);
            return;                              // retry the faulting instruction
        }
        if (code != EXCEPTION_CODE_TLB_MODIFICATION && fix_zero_extended_base(ex)) return;
    }
unhandled:
    if (s_prev_handler) s_prev_handler(ex);
}

static void pager_init(void) {
    if (s_ready) return;
    s_ready = 1;

    s_ncode = (uint32_t)(__paged_end - __paged_start) / PG_SIZE;
    s_ncode = (s_ncode + 1) & ~1u;                // keep TLB pairs within the region
    if (s_ncode) {
        s_rom = rompak_search_ext(".pgc");
        assertf(s_rom != 0, "pager: paged image (.pgc) missing from the ROM");
    }

    // The swappable heap needs a SummerCart64 with SDRAM writes enabled.
    uint32_t rom_end = rompak_search_ext(".end");
    s_swap_base = rom_end ? ((rom_end + 64 + 0xFFFFu) & ~0xFFFFu) : 0;
    if (s_swap_base && s_swap_base < SWAP_CART_END && sc64_enable_sdram_writes()) {
        // Compressed swap: the SDRAM area holds the heap at ~2.7:1 (measured on
        // Wesnoth's heap); offer 2.5x its size as virtual heap.
        uint32_t swap_end = SWAP_CART_END;
#ifdef N64_SWAP_AREA_LIMIT_MB   /* testing: a smaller swap area, as with a bigger ROM image */
        if (swap_end - s_swap_base > N64_SWAP_AREA_LIMIT_MB * 1048576u) swap_end = s_swap_base + N64_SWAP_AREA_LIMIT_MB * 1048576u;
#endif
        s_cs_units = (swap_end - s_swap_base) / CS_UNIT;
        if (s_cs_units > CS_MAX_UNITS) s_cs_units = CS_MAX_UNITS;
        s_nsdram = ((uint32_t)((uint64_t)(swap_end - s_swap_base) * 5 / 2 / PG_SIZE)) & ~1u;
        if (s_ncode + s_nsdram > MAX_PAGES) s_nsdram = (MAX_PAGES - s_ncode) & ~1u;
#ifdef N64_SDRAM_SWAP_LIMIT_MB   /* testing: force most of the heap onto the SD swap file */
        if (s_nsdram > N64_SDRAM_SWAP_LIMIT_MB * 64u) s_nsdram = N64_SDRAM_SWAP_LIMIT_MB * 64u;
#endif
        // Virtual heap = SDRAM part + SD part (usable once the swap file is attached).
#ifdef N64_SD_SWAP
        s_nheap = s_nsdram + SD_SWAP_MAX_PAGES;
#else
        s_nheap = s_nsdram;                      // SD swap is opt-in (-DN64_SD_SWAP)
#endif
        if (s_ncode + s_nheap > MAX_PAGES) s_nheap = MAX_PAGES - s_ncode;
        s_nheap &= ~1u;
    }
    tls_init();
    if (s_ncode + s_nheap == 0) { s_prev_handler = register_exception_handler(pager_exception); return; }
    assertf(s_ncode <= MAX_PAGES, "paged image too large: %lu pages", (unsigned long)s_ncode);

    s_nframes = n64_pager_frames_requested;
    s_pool = memalign(PG_SIZE, s_nframes * PG_SIZE);
    assertf(s_pool, "pager: cannot allocate %lu frames", (unsigned long)s_nframes);
    s_pool = (uint8_t*)CachedAddr(s_pool);
    s_frames = malloc(s_nframes * sizeof(frame_t));
    for (uint32_t i = 0; i < s_nframes; ++i) { s_frames[i].page = -1; s_frames[i].ref = 0; }
    for (uint32_t i = 0; i < MAX_PAGES; ++i) { s_pte[i] = NO_FRAME; s_flags[i] = 0; }

    s_stats.pages = s_ncode;
    s_stats.heap_pages = s_nheap;
    s_stats.frames = s_nframes;

    tlb_reset();
    s_prev_handler = register_exception_handler(pager_exception);
}

uint32_t n64_pager_heap_size(void) { pager_init(); return s_nheap * PG_SIZE; }

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

// ---------------------------------------------------------------------------
// Watchdog: every 15 s (timer interrupt) log the interrupted program counter
// and the pager's counters -- a crude sampling profiler that tells a busy
// loop from paging thrash or an idle wait, without a debugger. Started by
// the --n64-watchdog option (src/n64/n64_main.cpp).
// ---------------------------------------------------------------------------
#include <timer.h>
static n64_pager_stats_t s_wd_last;
extern unsigned n64_vheap_live_kib(void);
extern void n64_vheap_profile_dump(const char* why) __attribute__((weak));
static void watchdog_cb(int ovfl)
{
    (void)ovfl;
    uint32_t epc;
    asm volatile("mfc0 %0,$14" : "=r"(epc));
    debugf("[watchdog] heap %u KiB  pc %08lx  faults +%lu  refills +%lu  evictions +%lu  writebacks +%lu  swapins +%lu  sd w+%lu r+%lu\n",
           n64_vheap_live_kib(), (unsigned long)epc,
           (unsigned long)(s_stats.faults - s_wd_last.faults),
           (unsigned long)(s_stats.refills - s_wd_last.refills),
           (unsigned long)(s_stats.evictions - s_wd_last.evictions),
           (unsigned long)(s_stats.writebacks - s_wd_last.writebacks),
           (unsigned long)(s_stats.swapins - s_wd_last.swapins),
           (unsigned long)(s_stats.sd_writes - s_wd_last.sd_writes),
           (unsigned long)(s_stats.sd_reads - s_wd_last.sd_reads));
    s_wd_last = s_stats;
    static unsigned ticks;
    if (n64_vheap_profile_dump && ++ticks % 8 == 0) n64_vheap_profile_dump("periodic");
    if (s_cz_in)
        debugf("[watchdog] compressed swap (virtual heap %lu MiB): %lu KiB of %lu KiB used; written %lu KiB as %lu KiB (%lu%%), %lu us per page\n",
               (unsigned long)((s_nsdram * PG_SIZE) >> 20),
               (unsigned long)((s_cs_used * CS_UNIT) >> 10), (unsigned long)((s_cs_units * CS_UNIT) >> 10),
               (unsigned long)(s_cz_in >> 10), (unsigned long)(s_cz_out >> 10),
               (unsigned long)(s_cz_out * 100 / s_cz_in),
               (unsigned long)(TICKS_TO_US(s_cz_ticks) / (s_cz_in / PG_SIZE - s_cz_zero + 1)));
}

void n64_pager_start_watchdog(void)
{
    static int started;
    if (started) return;
    started = 1;
    timer_init();
    new_timer(TIMER_TICKS(15 * 1000000), TF_CONTINUOUS, watchdog_cb);
}

// ---------------------------------------------------------------------------
// SD swap file. Created on first use (the size of the heap's SD part), then
// mapped once: the sector of every 4 KiB unit is recorded, so the pager does
// raw sector I/O from its exception handler without going through FatFs (and
// the file need not be contiguous).
// ---------------------------------------------------------------------------
#include <fatfs/ff.h>
int n64_pager_attach_sd_swap(const char* path)
{
    static FIL fil;
    if (s_sd_lba || s_nheap <= s_nsdram) return s_sd_lba != NULL;
    const uint32_t pages = s_nheap - s_nsdram;
    const FSIZE_t bytes = (FSIZE_t)pages * PG_SIZE;
    if (f_open(&fil, path, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) return 0;
    if (f_size(&fil) < bytes) {
        // Extending by seeking allocates the clusters without writing data.
        if (f_lseek(&fil, bytes) != FR_OK || f_tell(&fil) != bytes || f_sync(&fil) != FR_OK) {
            debugf("[pager] cannot allocate a %lu MiB swap file on the SD card\n", (unsigned long)(bytes >> 20));
            f_close(&fil);
            return 0;
        }
    }
    FATFS* fs = fil.obj.fs;
    if ((fs->csize * 512u) % SD_UNIT != 0) {
        debugf("[pager] SD swap needs a cluster size that is a multiple of 4 KiB\n");
        f_close(&fil);
        return 0;
    }
    const uint32_t units = pages * (PG_SIZE / SD_UNIT);
    uint32_t* map = malloc(units * sizeof(uint32_t));
    if (!map) { f_close(&fil); return 0; }
    for (uint32_t u = 0; u < units; ++u) {
        const FSIZE_t off = (FSIZE_t)u * SD_UNIT;
        if (f_lseek(&fil, off) != FR_OK || fil.clust < 2) { free(map); f_close(&fil); return 0; }
        map[u] = (uint32_t)(fs->database + (LBA_t)(fil.clust - 2) * fs->csize + (off / 512) % fs->csize);
    }
    // The file stays open (never written through FatFs) so it cannot be removed.
    s_sd_pages = pages;
    s_sd_lba = map;
    debugf("[pager] SD swap attached: %lu MiB at %s (heap: %lu MiB SDRAM + %lu MiB SD)\n",
           (unsigned long)(bytes >> 20), path, (unsigned long)(s_nsdram * PG_SIZE >> 20),
           (unsigned long)(pages * PG_SIZE >> 20));
    return 1;
}
