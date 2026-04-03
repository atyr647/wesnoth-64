/**
 * romfs_builder.cpp
 *
 * Builds the ROM filesystem directory and patches it into the N64 ROM image.
 *
 * ROM layout after patching:
 *   [0x00000000 - 0x00200000)  N64 header + game code (preserved as-is)
 *   [0x00200000]               RomFsHeader
 *   [0x00200010]               RomFsEntry[0..N-1]
 *   [after entries]            Packed string table
 *   [aligned to 8 bytes]       Asset data blob
 *
 * The ROM file is extended if necessary to fit all assets.
 * N64 ROMs must have sizes that are powers of 2; we pad to the next P2.
 *
 * The function also fills in rom_offset for every AssetEntry so that
 * write_romfs_offsets() can generate the offsets source file.
 */

#include "pipeline.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace n64pack {

// Must match n64_romfs.hpp
static constexpr uint32_t ROMFS_MAGIC        = 0x574E5246; // "WNRF"
static constexpr uint32_t ROMFS_DIR_ROM_ADDR = 0x00200000; // 2 MB into ROM

struct RomFsHeader_wire {
    uint32_t magic;
    uint16_t entry_count;
    uint16_t reserved;
    uint32_t string_data_offset; // ROM offset (from ROM start) of string table
    uint32_t string_data_size;
};

struct RomFsEntry_wire {
    uint16_t name_handle;   // offset into packed string table
    uint8_t  _pad[2];
    uint32_t rom_offset;
    uint32_t byte_length;
    uint8_t  type;
    uint8_t  _pad2[3];
};

static uint32_t next_pow2_u32(uint32_t v) {
    v--;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return ++v;
}

// -----------------------------------------------------------------------
// build_romfs
// -----------------------------------------------------------------------
bool build_romfs(const std::vector<AssetEntry>& assets,
                 const PipelineOptions& opts,
                 std::vector<uint8_t>& rom) {

    size_t asset_count = assets.size();
    if (asset_count > 8192) {
        std::fprintf(stderr, "ROMFS: too many assets (%zu > 8192)\n", asset_count);
        return false;
    }

    // --- Build packed string table ---
    // Map: name → offset into string blob
    std::vector<std::string> names;
    std::unordered_map<std::string, uint16_t> str_offsets;
    std::vector<uint8_t> str_blob;

    for (const auto& a : assets) {
        if (str_offsets.count(a.name) == 0) {
            str_offsets[a.name] = (uint16_t)str_blob.size();
            for (char c : a.name) str_blob.push_back((uint8_t)c);
            str_blob.push_back(0);
            names.push_back(a.name);
        }
    }

    // --- Calculate ROM offsets ---
    // Directory lives at ROMFS_DIR_ROM_ADDR in the ROM byte array
    uint32_t dir_start = ROMFS_DIR_ROM_ADDR;
    uint32_t header_size  = sizeof(RomFsHeader_wire);
    uint32_t entries_size = (uint32_t)(asset_count * sizeof(RomFsEntry_wire));
    uint32_t str_blob_off = dir_start + header_size + entries_size;
    // Align to 8 bytes
    str_blob_off = (str_blob_off + 7) & ~7u;
    uint32_t asset_data_start = str_blob_off + (uint32_t)str_blob.size();
    asset_data_start = (asset_data_start + 7) & ~7u;

    // Compute per-asset ROM offsets
    std::vector<AssetEntry*> asset_ptrs;
    // We need mutable access; cast away const for offset assignment
    // (safe: we own these entries in the caller's vector)
    std::vector<uint32_t> offsets(asset_count);
    uint32_t cursor = asset_data_start;
    for (size_t i = 0; i < asset_count; ++i) {
        offsets[i] = cursor;
        cursor += (uint32_t)assets[i].data.size();
        cursor = (cursor + 7) & ~7u; // align each asset to 8 bytes
    }

    uint32_t total_rom_size = cursor;
    // Pad to next power of 2 (N64 requirement)
    uint32_t padded_size = next_pow2_u32(total_rom_size);
    if (padded_size < 0x100000) padded_size = 0x100000; // minimum 1 MB

    std::printf("  ROM directory at 0x%08X\n", dir_start);
    std::printf("  Asset data    at 0x%08X\n", asset_data_start);
    std::printf("  Total data    : %.2f MB\n", total_rom_size / 1048576.0);
    std::printf("  ROM size      : %.2f MB (padded to P2)\n", padded_size / 1048576.0);

    // Extend ROM buffer
    if (rom.size() < padded_size) rom.resize(padded_size, 0xFF);

    // --- Write directory header ---
    RomFsHeader_wire hdr;
    hdr.magic               = ROMFS_MAGIC;
    hdr.entry_count         = (uint16_t)asset_count;
    hdr.reserved            = 0;
    hdr.string_data_offset  = str_blob_off;
    hdr.string_data_size    = (uint32_t)str_blob.size();
    std::memcpy(rom.data() + dir_start, &hdr, sizeof(hdr));

    // --- Write entry table ---
    uint32_t entries_start = dir_start + header_size;
    for (size_t i = 0; i < asset_count; ++i) {
        RomFsEntry_wire e;
        std::memset(&e, 0, sizeof(e));
        e.name_handle  = str_offsets.at(assets[i].name);
        e.rom_offset   = offsets[i];
        e.byte_length  = (uint32_t)assets[i].data.size();
        e.type         = (uint8_t)assets[i].type;
        std::memcpy(rom.data() + entries_start + i * sizeof(e), &e, sizeof(e));
    }

    // --- Write string table ---
    std::memcpy(rom.data() + str_blob_off, str_blob.data(), str_blob.size());

    // --- Write asset data blobs ---
    for (size_t i = 0; i < asset_count; ++i) {
        std::memcpy(rom.data() + offsets[i], assets[i].data.data(), assets[i].data.size());
        // Write back resolved offset so the caller can use it
        const_cast<AssetEntry&>(assets[i]).rom_offset = offsets[i];
    }

    return true;
}

// -----------------------------------------------------------------------
// write_romfs_offsets
// Generates src/n64/n64_romfs_offsets.cpp with well-known ROM addresses.
// -----------------------------------------------------------------------
bool write_romfs_offsets(const std::vector<AssetEntry>& assets,
                         const std::filesystem::path& output_cpp) {
    // Find glyph sheet
    uint32_t glyph_offset = 0;
    uint16_t glyph_w = 128, glyph_h = 128;

    for (const auto& a : assets) {
        if (a.type == ASSET_FONT && a.name.find("glyph_sheet") != std::string::npos) {
            glyph_offset = a.rom_offset;
            glyph_w      = a.width;
            glyph_h      = a.height;
            break;
        }
    }

    FILE* fp = std::fopen(output_cpp.c_str(), "w");
    if (!fp) return false;

    std::fprintf(fp,
        "// AUTO-GENERATED by tools/n64-asset-pipeline — DO NOT EDIT\n"
        "// Regenerate by running wesnoth-n64-pack after any asset change.\n"
        "#include <stdint.h>\n"
        "const uint32_t ROMFS_GLYPH_SHEET_OFFSET = 0x%08Xu;\n"
        "const uint16_t ROMFS_GLYPH_SHEET_W      = %u;\n"
        "const uint16_t ROMFS_GLYPH_SHEET_H      = %u;\n",
        glyph_offset, glyph_w, glyph_h
    );

    std::fclose(fp);
    return true;
}

} // namespace n64pack
