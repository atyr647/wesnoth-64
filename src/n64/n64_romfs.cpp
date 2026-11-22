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
static u32 s_strings_addr  = 0;   // absolute cart address of the name table

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
    s_strings_addr = s_base + s_dma.hdr.strings_off;

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
// Compare `name` with the stored (NUL-terminated) name at string-table offset.
static bool name_matches(u32 name_off, const char* name) {
#ifdef N64_BUILD
    static char buf[64] __attribute__((aligned(16)));
    u32 addr = s_strings_addr + name_off;
    size_t len = strlen(name) + 1;                     // include the terminator
    for (size_t done = 0; done < len; ) {
        // DMA from an even address, 32 bytes at a time (PI DMA needs 2-byte phase match).
        u32 a = (addr + done) & ~1u;
        u32 skew = (addr + done) - a;
        data_cache_hit_writeback_invalidate(buf, sizeof(buf));
        dma_read(buf, a, 48);
        data_cache_hit_invalidate(buf, sizeof(buf));
        size_t chunk = 48 - skew;
        if (chunk > len - done) chunk = len - done;
        if (memcmp(buf + skew, name + done, chunk) != 0) return false;
        done += chunk;
    }
    return true;
#else
    (void)name_off; (void)name;
    return false;
#endif
}

static void fill_result(RomFsEntry& out) {
    out.name_handle = 0;
    out.rom_offset  = s_base + s_dma.ent.offset;       // absolute cart addr
    out.byte_length = s_dma.ent.length;
    out.type        = s_dma.ent.type;
    out.width       = s_dma.ent.width;
    out.height      = s_dma.ent.height;
    out.raw_length  = s_dma.ent.reserved[0];           // 0 = not compressed
}

// -----------------------------------------------------------------------
// n64_romfs_find — binary search of the sorted on-ROM directory, then a
// name check over the run of entries sharing the hash.
// -----------------------------------------------------------------------
const RomFsEntry* n64_romfs_find(const char* filename) {
#ifdef N64_BUILD
    if (!s_entry_count || !filename) return nullptr;
    const u32 want = n64_romfs_hash(filename);

    u32 lo = 0, hi = s_entry_count;
    while (lo < hi) {                                  // lower bound of `want`
        u32 mid = lo + (hi - lo) / 2;
        rom_read32(s_entries_addr + mid * (u32)sizeof(RomFsWireEntry));
        if (s_dma.ent.hash < want) lo = mid + 1; else hi = mid;
    }
    for (u32 i = lo; i < s_entry_count; ++i) {
        rom_read32(s_entries_addr + i * (u32)sizeof(RomFsWireEntry));
        if (s_dma.ent.hash != want) break;
        RomFsWireEntry e = s_dma.ent;
        if (!name_matches(e.name_off, filename)) continue;
        s_dma.ent = e;
        RomFsEntry& out = s_slots[s_next_slot];
        s_next_slot = (s_next_slot + 1) % SLOT_COUNT;
        fill_result(out);
        return &out;
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
    // Paths from the engine's own filesystem code: "/rom/<exact ROMFS name>".
    if (std::strncmp(wesnoth_path, "/rom/", 5) == 0)
        return n64_romfs_find(wesnoth_path + 5);
    // Exact ROMFS names (packer --vfs stores the repository layout).
    if (const RomFsEntry* e = n64_romfs_find(wesnoth_path))
        return e;
    // Legacy layout (packer without --vfs): data/core/ and data/ stripped.
    const char* prefixes[] = { "data/core/", "data/", nullptr };
    for (const char** p = prefixes; *p; ++p) {
        size_t plen = std::strlen(*p);
        if (std::strncmp(wesnoth_path, *p, plen) == 0)
            return n64_romfs_find(wesnoth_path + plen);
    }
    if (const RomFsEntry* e = n64_romfs_find(wesnoth_path))
        return e;

    // The sound code passes bare names ("select-unit.wav", "sword-1.ogg");
    // the files live under sounds/ in the ROMFS.
    if (!std::strchr(wesnoth_path, '/')) {
        char buf[96];
        if (std::strlen(wesnoth_path) + sizeof("sounds/") <= sizeof(buf)) {
            std::strcpy(buf, "sounds/");
            std::strcat(buf, wesnoth_path);
            return n64_romfs_find(buf);
        }
    }
    return nullptr;
}

u32 n64_romfs_read_capacity(const RomFsEntry* e) {
    if (!e) return 0;
    u32 need = e->raw_length ? e->raw_length + (e->byte_length >> 8) + 32 + 16 : e->byte_length;
    return (need + 15u) & ~15u;
}

#ifdef N64_BUILD
extern "C" int decompress_lz4_full_inplace(const uint8_t* src, size_t src_size,
                                           uint8_t* dst, size_t dst_size);  // libdragon
#endif

u32 n64_romfs_read(const RomFsEntry* e, void* dst, u32 capacity) {
    if (!e || !dst) return 0;
    if (n64_romfs_read_capacity(e) > capacity) return 0;
#ifdef N64_BUILD
    const u32 span = (e->byte_length + 15u) & ~15u;
    if (!e->raw_length) {
        data_cache_hit_writeback_invalidate(dst, span);
        dma_read(dst, e->rom_offset, span);
        data_cache_hit_invalidate(dst, span);
        return e->byte_length;
    }
    // LZ4: DMA the compressed block to the (16-byte aligned) tail of the
    // buffer, then decompress in place towards the front.
    u8* buf = static_cast<u8*>(dst);
    u32 src_at = (capacity - span) & ~15u;
    data_cache_hit_writeback_invalidate(buf + src_at, span);
    dma_read(buf + src_at, e->rom_offset, span);
    data_cache_hit_invalidate(buf + src_at, span);
    int n = decompress_lz4_full_inplace(buf + src_at, e->byte_length, buf, e->raw_length);
    return n == (int)e->raw_length ? e->raw_length : 0;
#else
    return 0;
#endif
}

u16 n64_romfs_entry_count() { return (u16)s_entry_count; }
