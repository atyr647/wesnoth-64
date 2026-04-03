#pragma once

/**
 * pipeline.hpp — shared types for the N64 asset pipeline tool
 *
 * This header is used ONLY by the host-side asset pipeline tool.
 * It is NOT included in the N64 ROM build.
 */

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace n64pack {

// -----------------------------------------------------------------------
// Asset types (mirrors RomFsType in n64_romfs.hpp)
// -----------------------------------------------------------------------
enum AssetType : uint8_t {
    ASSET_TEXTURE  = 0,
    ASSET_MUSIC    = 1,
    ASSET_SFX      = 2,
    ASSET_WML      = 3,
    ASSET_LUA      = 4,
    ASSET_FONT     = 5,
};

// -----------------------------------------------------------------------
// An asset entry before it gets a ROM offset assigned
// -----------------------------------------------------------------------
struct AssetEntry {
    std::string          name;        // ROM filesystem path (relative to data/)
    AssetType            type;
    std::vector<uint8_t> data;        // converted binary data
    uint32_t             rom_offset;  // filled in by romfs_builder
    uint16_t             width;       // for textures: pixel width
    uint16_t             height;      // for textures: pixel height
};

// -----------------------------------------------------------------------
// Texture conversion result
// -----------------------------------------------------------------------
struct Texture16 {
    std::vector<uint16_t> pixels;  // RGBA5551 row-major
    uint16_t width;
    uint16_t height;
};

// -----------------------------------------------------------------------
// Pipeline options
// -----------------------------------------------------------------------
struct PipelineOptions {
    std::filesystem::path wesnoth_data_dir;  // input: Wesnoth data/
    std::filesystem::path output_rom;         // output: .n64 file to patch
    std::string           campaign;           // which campaign to include
    bool                  all_campaigns;      // include all campaigns
    bool                  verbose;
    int                   texture_scale;      // 1=full, 4=quarter (N64 default)
    bool                  convert_music;      // true = ADPCM, false = skip music
    bool                  english_only;       // skip non-English translations
};

// -----------------------------------------------------------------------
// Pipeline stages
// -----------------------------------------------------------------------

// image_converter.cpp
// Read a PNG from disk, downscale to target_w x target_h,
// quantise to RGBA5551. Returns nullptr on failure.
Texture16 convert_image(const std::filesystem::path& png_path,
                        int target_w, int target_h);

// Compute N64 texture dimensions for a given original size and scale factor
inline int n64_tex_size(int original, int scale) {
    int scaled = original / scale;
    // Round up to next power of 2 (N64 TMEM requirement)
    int p = 1;
    while (p < scaled) p <<= 1;
    return p;
}

// audio_converter.cpp
// Convert OGG/WAV to raw signed-16-bit PCM mono at 22050 Hz.
// For music tracks, returns ADPCM-compressed data.
std::vector<uint8_t> convert_sfx  (const std::filesystem::path& path);
std::vector<uint8_t> convert_music(const std::filesystem::path& path);

// wml_compiler.cpp
// Parse a Wesnoth WML .cfg file (or pre-parsed config tree) and emit
// compact binary WML suitable for streaming from ROM.
std::vector<uint8_t> compile_wml(const std::filesystem::path& cfg_path,
                                  const std::filesystem::path& data_dir);

// glyph_sheet.cpp
// Rasterise a bitmap font into a 128x128 RGBA5551 glyph sheet.
// Each glyph is 8x8 pixels; sheet holds 256 glyphs (16 per row).
Texture16 build_glyph_sheet(const std::filesystem::path& ttf_path = {});

// lua_precompiler.cpp
// Compile a Lua 5.4 source file to bytecode using the embedded luac.
std::vector<uint8_t> compile_lua(const std::filesystem::path& lua_path);

// romfs_builder.cpp
// Given a list of assets, build:
//   1. The packed string table
//   2. The RomFsHeader + RomFsEntry array
//   3. A contiguous blob of all asset data
// And patch them into the ROM image at ROMFS_DIR_ROM_ADDR.
bool build_romfs(const std::vector<AssetEntry>& assets,
                 const PipelineOptions& opts,
                 std::vector<uint8_t>& rom_image_inout);

// n64_romfs_offsets generator
// Writes src/n64/n64_romfs_offsets.cpp with the glyph sheet ROM offset
// and other well-known offsets resolved after romfs_builder runs.
bool write_romfs_offsets(const std::vector<AssetEntry>& assets,
                         const std::filesystem::path& output_cpp);

} // namespace n64pack
