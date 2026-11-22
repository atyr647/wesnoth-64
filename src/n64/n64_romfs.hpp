#pragma once

/**
 * n64_romfs.hpp
 *
 * ROM filesystem for the N64 port.
 *
 * The asset pipeline tool (tools/n64-asset-pipeline) packs all converted
 * game assets into the ROM image with a directory table at a fixed offset.
 *
 * Layout of the ROM image (libdragon n64tool, see n64_create_rom()):
 *
 *   [IPL3 + TOC            ]
 *   [Game ELF (compressed) ]
 *   [wesnoth.wnrf          ]  ROMFS: sorted directory + asset data
 *   [symbol table          ]
 *
 * The directory stays in ROM and is binary-searched by name hash via DMA,
 * so it costs no RAM regardless of how many assets the ROM carries.
 *
 * File naming convention matches Wesnoth's data/ tree:
 *   images/terrain/grass/green.png     → terrain texture
 *   images/units/human-loyalists/...   → unit sprite sheet
 *   sounds/...                         → SFX
 *   music/...                          → ADPCM music
 *   data/campaigns/...                 → compiled binary WML
 *   data/lua/...                       → Lua bytecode
 */

#include "n64_platform.hpp"

// -----------------------------------------------------------------------
// ROMFS entry
// -----------------------------------------------------------------------
struct RomFsEntry {
    u16  name_handle;   // interned string ID (from g_strings)
    u32  rom_offset;    // absolute ROM cartridge address (PI_DOM1_ADDR2 + offset)
    u32  byte_length;   // size of the asset in bytes
    u8   type;          // RomFsType
    u16  width;         // pixel width (for textures/sprites)
    u16  height;        // pixel height (for textures/sprites)
    u32  raw_length;    // uncompressed size if LZ4-compressed, else 0
};

enum RomFsType : u8 {
    ROMFS_TEXTURE  = 0,  // RGBA5551 raw pixel data
    ROMFS_MUSIC    = 1,  // ADPCM stream
    ROMFS_SFX      = 2,  // signed 16-bit PCM mono
    ROMFS_WML      = 3,  // compiled binary WML
    ROMFS_LUA      = 4,  // Lua 5.4 bytecode
    ROMFS_FONT     = 5,  // glyph sheet (RGBA5551)
    ROMFS_DATA     = 6,  // raw bytes: maps, terrain-code table, manifest
};

// -----------------------------------------------------------------------
// On-ROM format (all fields big-endian, offsets relative to blob start)
//
// The pipeline emits one standalone file, wesnoth.wnrf, which n64tool
// appends to the ROM (libdragon --toc). At runtime it is located with
// rompak_search_ext(".wnrf"); nothing depends on a fixed ROM address.
//
//   RomFsHeader (32 B)
//   RomFsWireEntry[entry_count] (32 B each), sorted by name hash
//   string table (NUL-terminated names)
//   asset data (16-byte aligned; LZ4 block format where raw size != 0)
//
// The directory stays in ROM: lookups hash the name (FNV-1a, 32 bit),
// binary-search the table (sorted by hash, then name) with 32-byte DMA reads,
// and confirm the stored name, so colliding hashes are resolved correctly.
// -----------------------------------------------------------------------
static constexpr u32 ROMFS_MAGIC = 0x574E5246; // "WNRF"

struct RomFsHeader {
    u32 magic;
    u32 entry_count;
    u32 entries_off;
    u32 strings_off;
    u32 strings_size;
    u32 data_off;
    u32 total_size;
    u32 reserved;
};

struct RomFsWireEntry {
    u32 hash;
    u32 offset;        // from blob start
    u32 length;
    u16 width;
    u16 height;
    u8  type;
    u8  pad[3];
    u32 name_off;      // into the string table (verified on lookup)
    u32 reserved[2];   // [0]: uncompressed size if LZ4-compressed, else 0
};
static_assert(sizeof(RomFsHeader) == 32, "RomFsHeader wire size");
static_assert(sizeof(RomFsWireEntry) == 32, "RomFsWireEntry wire size");

// FNV-1a, shared with the host packer.
static inline u32 n64_romfs_hash(const char* s) {
    u32 h = 2166136261u;
    while (*s) { h ^= (u8)*s++; h *= 16777619u; }
    return h;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

// Load the ROM filesystem directory into RAM. Call once at startup.
// Returns false if the ROM header is missing or corrupt.
bool n64_romfs_init();

// Find an entry by filename. Returns nullptr if not found. The pointer is
// valid until 8 further lookups have been made (rotating slot pool);
// rom_offset is an absolute cart address ready for dma_read().
const RomFsEntry* n64_romfs_find(const char* filename);

// Convenience: resolve a Wesnoth data/ path to a ROM entry.
// e.g. n64_romfs_find_wesnoth("images/terrain/grass/green.png")
const RomFsEntry* n64_romfs_find_wesnoth(const char* wesnoth_path);

// Size of an entry once read (decompressed if it is stored compressed).
inline u32 n64_romfs_size(const RomFsEntry* e) { return e->raw_length ? e->raw_length : e->byte_length; }

// Buffer capacity n64_romfs_read needs for an entry: its size plus the LZ4
// in-place margin, rounded up to 16.
u32  n64_romfs_read_capacity(const RomFsEntry* e);

// Read an entry into dst (16-byte aligned), decompressing LZ4 in place.
// Returns the entry's (uncompressed) size, or 0 if it does not fit.
u32  n64_romfs_read(const RomFsEntry* e, void* dst, u32 capacity);

// Total number of entries in the directory.
u16  n64_romfs_entry_count();

// -----------------------------------------------------------------------
// Well-known ROM offsets (generated by asset pipeline, linked in)
// These are extern const values set by a generated n64_romfs_offsets.cpp
// -----------------------------------------------------------------------
// Resolved by n64_romfs_init() from "font/glyph_sheet.tex" in the ROMFS.
extern u32 ROMFS_GLYPH_SHEET_OFFSET;
extern u16 ROMFS_GLYPH_SHEET_W;
extern u16 ROMFS_GLYPH_SHEET_H;
