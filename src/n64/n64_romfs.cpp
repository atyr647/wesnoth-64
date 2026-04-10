/**
 * n64_romfs.cpp
 *
 * ROM filesystem implementation.
 * Loads the directory from the ROM cartridge into RAM at startup,
 * then provides O(n) lookup by interned name handle.
 */

#include "n64_romfs.hpp"
#include "n64_memory.hpp"

#ifdef N64_BUILD
#  include <n64sys.h>
#  include <dma.h>
#endif

// -----------------------------------------------------------------------
// In-RAM directory
// -----------------------------------------------------------------------
static constexpr u16 MAX_ENTRIES = 1024; // max assets in one ROM

static RomFsEntry s_entries[MAX_ENTRIES];
static u16        s_entry_count = 0;

// Loaded string table (from ROM, interned into g_strings)
static u8         s_string_buf[16 * 1024] __attribute__((aligned(4)));

// Stub constants — overridden by generated n64_romfs_offsets.cpp
// from the asset pipeline. These are zero until a real ROM is built.
const u32 ROMFS_GLYPH_SHEET_OFFSET = 0;
const u16 ROMFS_GLYPH_SHEET_W      = 128;
const u16 ROMFS_GLYPH_SHEET_H      = 128;

// -----------------------------------------------------------------------
// n64_romfs_init
// -----------------------------------------------------------------------
bool n64_romfs_init() {
#ifdef N64_BUILD
    // DMA the ROMFS header
    RomFsHeader hdr;
    data_cache_hit_writeback_invalidate(&hdr, sizeof(hdr));
    dma_read(&hdr, ROMFS_DIR_ROM_ADDR, sizeof(hdr));

    if (hdr.magic != ROMFS_MAGIC) {
        N64_LOG("ROMFS: bad magic %08X (expected %08X)", hdr.magic, ROMFS_MAGIC);
        return false;
    }
    if (hdr.entry_count > MAX_ENTRIES) {
        N64_LOG("ROMFS: entry_count %u exceeds MAX_ENTRIES %u",
                hdr.entry_count, MAX_ENTRIES);
        return false;
    }

    // Load string table into s_string_buf, then intern all strings
    N64_ASSERT(hdr.string_data_size <= sizeof(s_string_buf));
    data_cache_hit_writeback_invalidate(s_string_buf, hdr.string_data_size);
    dma_read(s_string_buf, hdr.string_data_offset, hdr.string_data_size);

    // Load entry table (immediately after RomFsHeader in ROM)
    u32 entries_addr = ROMFS_DIR_ROM_ADDR + sizeof(RomFsHeader);
    size_t entries_bytes = hdr.entry_count * sizeof(RomFsEntry);
    data_cache_hit_writeback_invalidate(s_entries, entries_bytes);
    dma_read(s_entries, entries_addr, entries_bytes);

    s_entry_count = hdr.entry_count;

    // Re-intern string handles: the packed ROM string table uses offsets;
    // convert them to handles in our runtime g_strings table.
    const char* str_base = reinterpret_cast<const char*>(s_string_buf);
    for (u16 i = 0; i < s_entry_count; ++i) {
        // name_handle in ROM stores the byte offset into the packed string table
        u32 str_off = s_entries[i].name_handle; // temporarily holds offset
        const char* name = str_base + str_off;
        s_entries[i].name_handle = g_strings.intern(name);
    }

    N64_LOG("ROMFS: loaded %u entries", s_entry_count);
#else
    // Host stub: empty filesystem
    s_entry_count = 0;
    N64_LOG("ROMFS: host stub (no ROM), 0 entries");
#endif
    return true;
}

// -----------------------------------------------------------------------
// n64_romfs_find
// -----------------------------------------------------------------------
const RomFsEntry* n64_romfs_find(const char* filename) {
    u16 handle = g_strings.intern(filename);
    for (u16 i = 0; i < s_entry_count; ++i) {
        if (s_entries[i].name_handle == handle)
            return &s_entries[i];
    }
    return nullptr;
}

// -----------------------------------------------------------------------
// n64_romfs_find_wesnoth
// Wesnoth paths start with "data/" on PC; in the ROM we strip that prefix.
// e.g. "data/core/images/terrain/..." → "images/terrain/..."
// -----------------------------------------------------------------------
const RomFsEntry* n64_romfs_find_wesnoth(const char* wesnoth_path) {
    // Strip "data/core/" prefix if present
    const char* prefixes[] = { "data/core/", "data/", nullptr };
    for (const char** p = prefixes; *p; ++p) {
        size_t plen = std::strlen(*p);
        if (std::strncmp(wesnoth_path, *p, plen) == 0)
            return n64_romfs_find(wesnoth_path + plen);
    }
    return n64_romfs_find(wesnoth_path);
}

// -----------------------------------------------------------------------
// n64_romfs_entry_count
// -----------------------------------------------------------------------
u16 n64_romfs_entry_count() { return s_entry_count; }
