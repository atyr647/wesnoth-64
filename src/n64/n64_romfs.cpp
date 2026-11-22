/**
 * n64_romfs.cpp
 *
 * ROM filesystem runtime. The directory lives in ROM (see n64_romfs.hpp for
 * the wire format); lookups binary-search it by FNV-1a name hash using small
 * DMA reads, so there is no RAM-resident table and no entry-count limit.
 */

#include "n64_romfs.hpp"
#include "n64_memory.hpp"

#ifdef N64_BUILD
#  include <n64sys.h>
#  include <dma.h>
extern "C" uint32_t rompak_search_ext(const char* ext); // libdragon rompak.c
#endif

// -----------------------------------------------------------------------
// State
// -----------------------------------------------------------------------
static u32 s_base          = 0;   // absolute cart address of the .wnrf blob
static u32 s_entries_addr  = 0;   // absolute cart address of entry[0]
static u32 s_entry_count   = 0;

// Resolved at init from font/glyph_sheet.tex (absolute cart address).
u32 ROMFS_GLYPH_SHEET_OFFSET = 0;
u16 ROMFS_GLYPH_SHEET_W      = 128;
u16 ROMFS_GLYPH_SHEET_H      = 128;

// Results are handed out from a small rotating pool, so a caller can hold a
// few entries at once (e.g. a texture and its atlas) without a RAM table.
static constexpr unsigned SLOT_COUNT = 8;
static RomFsEntry s_slots[SLOT_COUNT];
static unsigned   s_next_slot = 0;

#ifdef N64_BUILD
// One cache-line-aligned bounce buffer for directory DMA.
static union {
    RomFsHeader    hdr;
    RomFsWireEntry ent;
    u8             raw[32];
} s_dma __attribute__((aligned(16)));

static void rom_read32(u32 cart_addr) {
    data_cache_hit_writeback_invalidate(&s_dma, sizeof(s_dma));
    dma_read(&s_dma, cart_addr, sizeof(s_dma));
    data_cache_hit_invalidate(&s_dma, sizeof(s_dma));
}
#endif

// -----------------------------------------------------------------------
// n64_romfs_init
// -----------------------------------------------------------------------
bool n64_romfs_init() {
#ifdef N64_BUILD
    s_base = rompak_search_ext(".wnrf");
    if (!s_base) {
        N64_LOG("ROMFS: no .wnrf file in ROM table of contents");
        return false;
    }

    rom_read32(s_base);
    if (s_dma.hdr.magic != ROMFS_MAGIC) {
        N64_LOG("ROMFS: bad magic %08lX at %08lX (expected %08lX)",
                (unsigned long)s_dma.hdr.magic, (unsigned long)s_base,
                (unsigned long)ROMFS_MAGIC);
        return false;
    }
    s_entry_count  = s_dma.hdr.entry_count;
    s_entries_addr = s_base + s_dma.hdr.entries_off;

    const RomFsEntry* glyphs = n64_romfs_find("font/glyph_sheet.tex");
    if (glyphs) {
        ROMFS_GLYPH_SHEET_OFFSET = glyphs->rom_offset;
        ROMFS_GLYPH_SHEET_W      = glyphs->width;
        ROMFS_GLYPH_SHEET_H      = glyphs->height;
    } else {
        N64_LOG("ROMFS: font/glyph_sheet.tex missing");
        return false;
    }

    N64_LOG("ROMFS: %u entries at cart %08lX", (unsigned)s_entry_count,
            (unsigned long)s_base);
#else
    s_entry_count = 0;
    N64_LOG("ROMFS: host stub (no ROM), 0 entries");
#endif
    return true;
}

// -----------------------------------------------------------------------
// n64_romfs_find — binary search of the sorted on-ROM directory
// -----------------------------------------------------------------------
const RomFsEntry* n64_romfs_find(const char* filename) {
#ifdef N64_BUILD
    if (!s_entry_count || !filename) return nullptr;
    const u32 want = n64_romfs_hash(filename);

    u32 lo = 0, hi = s_entry_count;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        rom_read32(s_entries_addr + mid * (u32)sizeof(RomFsWireEntry));
        const u32 h = s_dma.ent.hash;
        if (h == want) {
            RomFsEntry& out = s_slots[s_next_slot];
            s_next_slot = (s_next_slot + 1) % SLOT_COUNT;
            out.name_handle = 0;
            out.rom_offset  = s_base + s_dma.ent.offset; // absolute cart addr
            out.byte_length = s_dma.ent.length;
            out.type        = s_dma.ent.type;
            out.width       = s_dma.ent.width;
            out.height      = s_dma.ent.height;
            return &out;
        }
        if (h < want) lo = mid + 1; else hi = mid;
    }
#else
    (void)filename;
#endif
    return nullptr;
}

// -----------------------------------------------------------------------
// n64_romfs_find_wesnoth
// Wesnoth paths start with "data/" on PC; in the ROM we strip that prefix.
// e.g. "data/core/images/terrain/..." → "images/terrain/..."
// -----------------------------------------------------------------------
const RomFsEntry* n64_romfs_find_wesnoth(const char* wesnoth_path) {
    const char* prefixes[] = { "data/core/", "data/", nullptr };
    for (const char** p = prefixes; *p; ++p) {
        size_t plen = std::strlen(*p);
        if (std::strncmp(wesnoth_path, *p, plen) == 0)
            return n64_romfs_find(wesnoth_path + plen);
    }
    return n64_romfs_find(wesnoth_path);
}

u16 n64_romfs_entry_count() { return (u16)s_entry_count; }
