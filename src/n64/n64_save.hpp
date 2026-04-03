#pragma once

/**
 * n64_save.hpp
 *
 * Savegame system for N64, writing to cartridge SRAM or Flash.
 *
 * N64 save storage options:
 *   - SRAM:           32 KB (256 Kbit) — common on flashcarts
 *   - Flash:          128 KB (1 Mbit) — supported by EverDrive
 *   - Controller Pak: 256 KB across 123 pages
 *
 * Wesnoth saves are typically 50KB-500KB uncompressed. With zlib compression
 * they shrink to ~20-100KB. We target SRAM (32KB) for quick-save with
 * aggressive compression, and Flash (128KB) for full saves.
 *
 * Save format on N64:
 *   [4 bytes]  Magic "WN64"
 *   [4 bytes]  Save slot (0-3)
 *   [4 bytes]  Compressed data size
 *   [4 bytes]  Uncompressed data size
 *   [4 bytes]  CRC32 checksum
 *   [N bytes]  zlib-compressed binary WML
 *
 * Up to 4 save slots. The save index occupies the first 256 bytes of SRAM.
 *
 * On flashcarts (EverDrive, 64drive) the full 128KB Flash is available.
 * On real cartridges, only 32KB SRAM is available (1 save slot).
 */

#include "n64_platform.hpp"
#include <string>

// -----------------------------------------------------------------------
// Save slot metadata
// -----------------------------------------------------------------------
struct N64SaveSlot {
    bool        occupied;
    u32         compressed_size;
    u32         uncompressed_size;
    u32         crc32;
    char        label[32];       // scenario name or "Turn X"
    u8          campaign_idx;
    u8          scenario_idx;
    u16         turn_number;
};

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------
static constexpr int N64_SAVE_SLOTS       = 4;
static constexpr u32 N64_SAVE_MAGIC       = 0x574E3634; // "WN64"
static constexpr u32 SRAM_SIZE            = 32 * 1024;
static constexpr u32 FLASH_SIZE           = 128 * 1024;
static constexpr u32 SAVE_INDEX_SIZE      = 256; // first 256 bytes = index
static constexpr u32 SAVE_HEADER_SIZE     = 20;  // per-slot header

// -----------------------------------------------------------------------
// Save system API
// -----------------------------------------------------------------------

// Initialise save hardware (detect SRAM vs Flash). Call once at boot.
void n64_save_init();

// Returns the total save storage available in bytes.
u32  n64_save_capacity();

// Returns true if Flash (128KB) is available; false if only SRAM (32KB).
bool n64_save_has_flash();

// --- Save slots ---

// Read the save index from SRAM/Flash. Call once at startup.
void n64_save_read_index();

// Get metadata for a slot (0-3).
const N64SaveSlot& n64_save_slot_info(int slot);

// --- Save game ---

// Save the current game state to the given slot.
// 'wml_data' is the serialised WML text of the saved_game config.
// Returns true on success.
bool n64_save_write(int slot, const std::string& wml_data,
                    const char* label, u8 campaign_idx,
                    u8 scenario_idx, u16 turn_number);

// --- Load game ---

// Load a save from the given slot into 'wml_data_out'.
// Returns true on success.
bool n64_save_read(int slot, std::string& wml_data_out);

// --- Delete ---

// Mark a slot as empty.
void n64_save_delete(int slot);
