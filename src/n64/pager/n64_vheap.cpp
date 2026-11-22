/**
 * n64_vheap.cpp -- allocator for the swappable heap (see n64_pager.h).
 *
 * The engine's C++ allocations (global operator new/delete) and Lua come from
 * the virtual heap at VH_BASE, which the pager backs with RAM frames and
 * SummerCart64 SDRAM, so the engine can use far more memory than the
 * console's 8 MiB. libdragon and anything DMA/RSP/RDP-visible keep using the
 * physical malloc heap. Without an SC64 (no virtual heap) everything falls
 * back to malloc.
 *
 * MUST stay resident (listed in cmake/n64_paged.ld; compiled without LTO):
 * resident libstdc++ calls operator new with direct jumps.
 *
 * Layout
 *   small (<= 512 bytes): 64 size classes (8-byte steps); each slab is one
 *     16 KiB page holding objects of one class, free lists threaded through
 *     the free objects. No per-object header -- the class is looked up by
 *     page. This is where config trees live (map nodes, strings).
 *   large (> 512 bytes): boundary-tag blocks inside arenas of whole pages,
 *     segregated free lists by power of two, coalescing with neighbours.
 */

#include "n64_pager.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>

void n64_vheap_progress(uint32_t pages);

namespace {

constexpr uint32_t kPage      = PG_SIZE;
constexpr uint32_t kSmallMax  = 512;
constexpr uint32_t kClasses   = kSmallMax / 8;      // 64
constexpr uint32_t kMaxPages  = 4096;               // 64 MiB of heap at most
constexpr uint8_t  kLarge     = 0xFF;
constexpr uint8_t  kUnused    = 0;

uint32_t s_size = 0;              // heap bytes (0 = no virtual heap)
uint32_t s_top  = 0;              // next unassigned page (index)
bool     s_init = false;
uint8_t  s_page_kind[kMaxPages];  // 0 unused, 1..64 small class+1, 0xFF large
void*    s_small_free[kClasses];

void init() {
    if (s_init) return;
    s_init = true;
    s_size = n64_pager_heap_size();
    if (s_size > kMaxPages * kPage) s_size = kMaxPages * kPage;
}

inline bool in_heap(const void* p) {
    uintptr_t a = reinterpret_cast<uintptr_t>(p) & 0xFFFFFFFFu;
    return s_size && a >= VH_BASE && a < VH_BASE + s_size;
}
inline uint32_t page_of(const void* p) {
    return ((reinterpret_cast<uintptr_t>(p) & 0xFFFFFFFFu) - VH_BASE) / kPage;
}
inline uint8_t* page_addr(uint32_t page) {
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(VH_BASE + page * kPage));
}

// Take `n` fresh contiguous pages from the top, or nullptr.
uint8_t* take_pages(uint32_t n, uint8_t kind) {
    if ((s_top + n) * kPage > s_size) return nullptr;
    uint32_t first = s_top;
    s_top += n;
    if ((first * kPage) / (2u << 20) != (s_top * kPage) / (2u << 20)) {
        ::n64_vheap_progress(s_top);
    }
    for (uint32_t i = 0; i < n; ++i) s_page_kind[first + i] = kind;
    return page_addr(first);
}

// ---------------------------------------------------------------- small
void* small_alloc(uint32_t cls) {
    void* p = s_small_free[cls];
    if (!p) {
        const uint32_t size = (cls + 1) * 8;
        uint8_t* slab = take_pages(1, static_cast<uint8_t>(cls + 1));
        if (!slab) return nullptr;
        // Thread the slab's objects onto the free list (in address order).
        const uint32_t count = kPage / size;
        for (uint32_t i = 0; i < count; ++i) {
            void* obj = slab + i * size;
            *static_cast<void**>(obj) = (i + 1 < count) ? slab + (i + 1) * size : nullptr;
        }
        p = slab;
    }
    s_small_free[cls] = *static_cast<void**>(p);
    return p;
}
void small_free(void* p, uint32_t cls) {
    *static_cast<void**>(p) = s_small_free[cls];
    s_small_free[cls] = p;
}

// ---------------------------------------------------------------- large
// Block: [u32 prev_size][u32 size | flags] payload ... ; size includes header.
// prev_size is valid only when the previous block is free.
constexpr uint32_t kHdr      = 8;
constexpr uint32_t kInUse    = 1;
constexpr uint32_t kPrevUse  = 2;
constexpr uint32_t kMinBlock = 32;             // header + two free-list links + room
constexpr int      kBins     = 26;

struct Block {
    uint32_t prev_size;
    uint32_t size_flags;
    Block*   next;                             // free blocks only
    Block*   prev;
    uint32_t size() const { return size_flags & ~7u; }
    bool in_use() const { return size_flags & kInUse; }
    bool prev_in_use() const { return size_flags & kPrevUse; }
    Block* after() { return reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(this) + size()); }
    Block* before() { return reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(this) - prev_size); }
    void* payload() { return reinterpret_cast<uint8_t*>(this) + kHdr; }
};
inline Block* block_of(void* p) { return reinterpret_cast<Block*>(static_cast<uint8_t*>(p) - kHdr); }

Block* s_bins[kBins];

inline int bin_of(uint32_t size) {
    int b = 31 - __builtin_clz(size);           // floor(log2)
    return b < kBins ? b : kBins - 1;
}
void bin_insert(Block* b) {
    int i = bin_of(b->size());
    b->prev = nullptr;
    b->next = s_bins[i];
    if (s_bins[i]) s_bins[i]->prev = b;
    s_bins[i] = b;
}
void bin_remove(Block* b) {
    int i = bin_of(b->size());
    if (b->prev) b->prev->next = b->next; else s_bins[i] = b->next;
    if (b->next) b->next->prev = b->prev;
}
// Mark `b` free with size `size`, maintaining the next block's prev info.
void set_free(Block* b, uint32_t size, uint32_t prev_flag) {
    b->size_flags = size | prev_flag;
    Block* n = b->after();
    n->prev_size = size;
    n->size_flags &= ~kPrevUse;
}

void* large_alloc(uint32_t want) {
    uint32_t need = (want + kHdr + 7) & ~7u;
    if (need < kMinBlock) need = kMinBlock;
    for (int i = bin_of(need); i < kBins; ++i) {
        for (Block* b = s_bins[i]; b; b = b->next) {
            if (b->size() < need) continue;
            bin_remove(b);
            uint32_t rest = b->size() - need;
            if (rest >= kMinBlock) {
                b->size_flags = need | kInUse | (b->size_flags & kPrevUse);
                Block* r = b->after();
                set_free(r, rest, kPrevUse);
                bin_insert(r);
            } else {
                b->size_flags |= kInUse;
                b->after()->size_flags |= kPrevUse;
            }
            return b->payload();
        }
    }
    // New arena: whole pages, one free block plus an in-use end sentinel.
    uint32_t pages = (need + kHdr + 16 + kPage - 1) / kPage;
    if (pages < 4) pages = 4;                         // 64 KiB arenas
    uint8_t* arena = take_pages(pages, kLarge);
    if (!arena) return nullptr;
    uint32_t total = pages * kPage - kHdr;            // leave room for the sentinel header
    Block* b = reinterpret_cast<Block*>(arena);
    b->prev_size = 0;
    Block* sentinel = reinterpret_cast<Block*>(arena + total);
    sentinel->size_flags = kInUse;                    // size 0: never coalesced
    set_free(b, total, kPrevUse);                     // nothing before the arena
    bin_insert(b);
    return large_alloc(want);
}

void large_free(void* p) {
    Block* b = block_of(p);
    uint32_t size = b->size();
    uint32_t prev_flag = b->size_flags & kPrevUse;
    // Merge with the next block if free.
    Block* n = b->after();
    if (n->size() && !n->in_use()) { bin_remove(n); size += n->size(); }
    // Merge with the previous block if free.
    if (!prev_flag) {
        Block* pv = b->before();
        bin_remove(pv);
        size += pv->size();
        b = pv;
        prev_flag = b->size_flags & kPrevUse;
    }
    set_free(b, size, prev_flag);
    bin_insert(b);
}

size_t usable(void* p) {
    uint8_t kind = s_page_kind[page_of(p)];
    if (kind == kLarge) return block_of(p)->size() - kHdr;
    return kind * 8u;
}

} // namespace

// ---------------------------------------------------------------- public C API
extern "C" void* n64_vmalloc(size_t n) {
    init();
    if (!s_size) return std::malloc(n);
    if (n == 0) n = 1;
    void* p = (n <= kSmallMax) ? small_alloc(static_cast<uint32_t>((n + 7) / 8 - 1))
                               : large_alloc(static_cast<uint32_t>(n));
    return p;
}

extern "C" void n64_vfree(void* p) {
    if (!p) return;
    if (!in_heap(p)) { std::free(p); return; }
    uint8_t kind = s_page_kind[page_of(p)];
    if (kind == kLarge) large_free(p);
    else if (kind != kUnused) small_free(p, kind - 1u);
}

extern "C" void* n64_vrealloc(void* p, size_t n) {
    if (!p) return n64_vmalloc(n);
    if (n == 0) { n64_vfree(p); return nullptr; }
    if (!in_heap(p)) return std::realloc(p, n);
    size_t have = usable(p);
    if (n <= have && (n > kSmallMax || have <= kSmallMax)) return p;
    void* q = n64_vmalloc(n);
    if (!q) return nullptr;
    std::memcpy(q, p, have < n ? have : n);
    n64_vfree(p);
    return q;
}

// Over-aligned allocations (rare): a large block with the payload moved up to
// the alignment; the gap in front becomes its own free block.
static void* vmalloc_aligned(size_t n, size_t align) {
    if (align <= 8) return n64_vmalloc(n);
    init();
    if (!s_size) return memalign(align, n);
    void* raw = large_alloc(static_cast<uint32_t>(n + align + kMinBlock));
    if (!raw) return nullptr;
    uintptr_t a = reinterpret_cast<uintptr_t>(raw);
    uintptr_t aligned = (a + align - 1) & ~(uintptr_t)(align - 1);
    if (aligned == a) return raw;
    while (aligned - a < kMinBlock) aligned += align;
    Block* b = block_of(raw);
    Block* nb = block_of(reinterpret_cast<void*>(aligned));
    uint32_t lead = static_cast<uint32_t>(aligned - a);
    nb->size_flags = (b->size() - lead) | kInUse;     // previous (lead) is free
    set_free(b, lead, b->size_flags & kPrevUse);
    nb->size_flags &= ~kPrevUse;
    bin_insert(b);
    return reinterpret_cast<void*>(aligned);
}

// ---------------------------------------------------------------- diagnostics
#include <cstdio>
extern "C" void n64_vheap_report(size_t failed_request) {
    uint32_t small = 0, large = 0;
    for (uint32_t i = 0; i < s_top; ++i) (s_page_kind[i] == kLarge ? large : small)++;
    uint64_t free_large = 0;
    uint32_t free_blocks = 0;
    for (int i = 0; i < kBins; ++i)
        for (Block* b = s_bins[i]; b; b = b->next) { free_large += b->size(); ++free_blocks; }
    uint64_t free_small = 0;
    for (uint32_t c = 0; c < kClasses; ++c)
        for (void* p = s_small_free[c]; p; p = *static_cast<void**>(p)) free_small += (c + 1) * 8;
    std::fprintf(stderr, "[vheap] request %u failed: %u/%u pages used (small %u, large %u); "
                 "free in large blocks %u KiB (%u blocks), in small slabs %u KiB\n",
                 (unsigned)failed_request, (unsigned)s_top, (unsigned)(s_size / kPage),
                 (unsigned)small, (unsigned)large, (unsigned)(free_large / 1024),
                 (unsigned)free_blocks, (unsigned)(free_small / 1024));
}

void n64_vheap_progress(uint32_t pages) {
    std::fprintf(stderr, "[vheap] %u MiB of virtual heap in use\n", (unsigned)(pages * kPage >> 20));
}

// ---------------------------------------------------------------- operator new
void* operator new(size_t n) {
    void* p = n64_vmalloc(n);
    if (!p) { n64_vheap_report(n); throw std::bad_alloc(); }
    return p;
}
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return n64_vmalloc(n); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return n64_vmalloc(n); }
void* operator new(size_t n, std::align_val_t a) {
    void* p = vmalloc_aligned(n, static_cast<size_t>(a));
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n, std::align_val_t a) { return operator new(n, a); }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return vmalloc_aligned(n, static_cast<size_t>(a)); }
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return vmalloc_aligned(n, static_cast<size_t>(a)); }

void operator delete(void* p) noexcept { n64_vfree(p); }
void operator delete[](void* p) noexcept { n64_vfree(p); }
void operator delete(void* p, size_t) noexcept { n64_vfree(p); }
void operator delete[](void* p, size_t) noexcept { n64_vfree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { n64_vfree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { n64_vfree(p); }
void operator delete(void* p, std::align_val_t) noexcept { n64_vfree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { n64_vfree(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { n64_vfree(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { n64_vfree(p); }
