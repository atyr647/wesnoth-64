/**
 * main.cpp — Wesnoth N64 Asset Pipeline
 *
 * Usage:
 *   wesnoth-n64-pack [options] <wesnoth_data_dir> <output.n64>
 *
 * Options:
 *   --campaign <name>    Include only this campaign (e.g. "Heir_To_The_Throne")
 *   --all-campaigns      Include all campaigns (large ROM; for 64drive/SD)
 *   --scale <n>          Texture downscale factor (default: 4 for 320x240)
 *   --no-music           Skip music conversion (smaller ROM for testing)
 *   --english-only       Skip all non-English translations
 *   --verbose            Print every asset being processed
 *
 * The tool:
 *   1. Scans data/ for all images, sounds, music, WML cfg, Lua scripts
 *   2. Converts each asset to N64 format
 *   3. Builds the ROM filesystem directory
 *   4. Patches the directory + asset blob into the provided .n64 ROM image
 *   5. Writes src/n64/n64_romfs_offsets.cpp with resolved ROM addresses
 *
 * The input .n64 ROM must already contain the compiled game code (ELF → N64).
 * The code segment is preserved; assets are appended after it.
 */

#include "pipeline.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;
using namespace n64pack;

// -----------------------------------------------------------------------
// Help text
// -----------------------------------------------------------------------
static void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options] <wesnoth_data_dir> <output.n64>\n"
        "\n"
        "Options:\n"
        "  --campaign <name>   Include only this campaign\n"
        "  --all-campaigns     Include all campaigns\n"
        "  --scale <n>         Texture downscale factor (default: 4)\n"
        "  --no-music          Skip music conversion\n"
        "  --english-only      Skip non-English translations\n"
        "  --verbose           Verbose output\n"
        "  --help              Show this help\n"
        "\n"
        "Examples:\n"
        "  %s --campaign Heir_To_The_Throne data/ wesnoth.n64\n"
        "  %s --all-campaigns --no-music data/ wesnoth_full.n64\n",
        prog, prog, prog
    );
}

// -----------------------------------------------------------------------
// File extension helpers
// -----------------------------------------------------------------------
static bool has_ext(const fs::path& p, const char* ext) {
    return p.extension() == ext;
}

static bool is_image(const fs::path& p) {
    return has_ext(p, ".png") || has_ext(p, ".webp");
}
static bool is_sfx(const fs::path& p) {
    return (has_ext(p, ".ogg") || has_ext(p, ".wav")) &&
           p.string().find("/music/") == std::string::npos;
}
static bool is_music(const fs::path& p) {
    return (has_ext(p, ".ogg") || has_ext(p, ".wav")) &&
           p.string().find("/music/") != std::string::npos;
}
static bool is_wml(const fs::path& p) { return has_ext(p, ".cfg"); }
static bool is_lua(const fs::path& p) { return has_ext(p, ".lua"); }

// -----------------------------------------------------------------------
// Derive the ROM filesystem name from a data/ path
// e.g. data/core/images/terrain/grass/green.png
//   → images/terrain/grass/green.png
// -----------------------------------------------------------------------
static std::string romfs_name(const fs::path& full_path,
                               const fs::path& data_dir) {
    auto rel = fs::relative(full_path, data_dir);
    // Strip leading "core/" if present
    auto rel_str = rel.generic_string();
    const char* prefix = "core/";
    if (rel_str.rfind(prefix, 0) == 0)
        rel_str = rel_str.substr(std::strlen(prefix));
    return rel_str;
}

// -----------------------------------------------------------------------
// Load a ROM image from disk
// -----------------------------------------------------------------------
static std::vector<uint8_t> load_rom(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "Cannot open ROM: %s\n", path.c_str()); return {}; }
    auto size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// -----------------------------------------------------------------------
// Write final ROM to disk
// -----------------------------------------------------------------------
static bool write_rom(const fs::path& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "Cannot write ROM: %s\n", path.c_str()); return false; }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    return f.good();
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main(int argc, char** argv) {
    PipelineOptions opts{};
    opts.texture_scale   = 4;
    opts.convert_music   = true;
    opts.english_only    = false;
    opts.verbose         = false;
    opts.all_campaigns   = false;

    fs::path input_rom_path;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0)        { print_usage(argv[0]); return 0; }
        else if (std::strcmp(argv[i], "--verbose") == 0) { opts.verbose = true; }
        else if (std::strcmp(argv[i], "--no-music") == 0) { opts.convert_music = false; }
        else if (std::strcmp(argv[i], "--english-only") == 0) { opts.english_only = true; }
        else if (std::strcmp(argv[i], "--all-campaigns") == 0) { opts.all_campaigns = true; }
        else if (std::strcmp(argv[i], "--campaign") == 0 && i + 1 < argc) {
            opts.campaign = argv[++i];
        }
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            opts.texture_scale = std::atoi(argv[++i]);
            if (opts.texture_scale < 1) opts.texture_scale = 1;
        }
        else if (opts.wesnoth_data_dir.empty()) {
            opts.wesnoth_data_dir = argv[i];
        }
        else if (input_rom_path.empty()) {
            input_rom_path = argv[i];
            opts.output_rom = argv[i]; // overwrite in-place
        }
    }

    if (opts.wesnoth_data_dir.empty() || input_rom_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }
    if (!fs::exists(opts.wesnoth_data_dir)) {
        std::fprintf(stderr, "Data directory not found: %s\n", opts.wesnoth_data_dir.c_str());
        return 1;
    }
    if (!fs::exists(input_rom_path)) {
        std::fprintf(stderr, "ROM file not found: %s\n", input_rom_path.c_str());
        return 1;
    }

    std::printf("=== Wesnoth N64 Asset Pipeline ===\n");
    std::printf("Data dir  : %s\n", opts.wesnoth_data_dir.c_str());
    std::printf("ROM       : %s\n", input_rom_path.c_str());
    std::printf("Campaign  : %s\n", opts.campaign.empty() ? "(all)" : opts.campaign.c_str());
    std::printf("Scale     : 1/%d\n", opts.texture_scale);
    std::printf("Music     : %s\n", opts.convert_music ? "ADPCM" : "skipped");
    std::printf("\n");

    std::vector<AssetEntry> assets;

    // --- Step 1: Glyph sheet (always included first) ---
    {
        std::printf("[font] Building glyph sheet...\n");
        Texture16 glyphs = build_glyph_sheet();
        AssetEntry e;
        e.name   = "font/glyph_sheet.tex";
        e.type   = ASSET_FONT;
        e.width  = glyphs.width;
        e.height = glyphs.height;
        e.data.resize(glyphs.pixels.size() * 2);
        std::memcpy(e.data.data(), glyphs.pixels.data(), e.data.size());
        assets.push_back(std::move(e));
        std::printf("       glyph sheet %ux%u (%zu bytes)\n",
                    glyphs.width, glyphs.height, assets.back().data.size());
    }

    // --- Step 2: Walk the data directory ---
    size_t img_count = 0, sfx_count = 0, mus_count = 0, wml_count = 0, lua_count = 0;

    for (auto& de : fs::recursive_directory_iterator(opts.wesnoth_data_dir,
                       fs::directory_options::skip_permission_denied)) {
        if (!de.is_regular_file()) continue;
        const fs::path& p = de.path();

        // Filter by campaign if specified
        if (!opts.campaign.empty() && !opts.all_campaigns) {
            auto ps = p.generic_string();
            bool in_core     = ps.find("/core/")     != std::string::npos;
            bool in_campaign = ps.find("/" + opts.campaign + "/") != std::string::npos;
            bool in_shared   = ps.find("/themes/") != std::string::npos ||
                               ps.find("/gui/")    != std::string::npos ||
                               ps.find("/ai/")     != std::string::npos ||
                               ps.find("/fonts/")  != std::string::npos;
            if (!in_core && !in_campaign && !in_shared) continue;
        }

        std::string rom_name = romfs_name(p, opts.wesnoth_data_dir);
        AssetEntry entry;
        entry.name = rom_name;

        if (is_image(p)) {
            // Derive target texture size
            // We'll let convert_image infer source dimensions and downscale
            Texture16 tex = convert_image(p, 0, 0); // 0,0 = auto-scale by opts.texture_scale
            if (tex.pixels.empty()) {
                if (opts.verbose) std::printf("[skip] %s\n", rom_name.c_str());
                continue;
            }
            entry.type   = ASSET_TEXTURE;
            entry.width  = tex.width;
            entry.height = tex.height;
            entry.data.resize(tex.pixels.size() * 2);
            std::memcpy(entry.data.data(), tex.pixels.data(), entry.data.size());
            if (opts.verbose)
                std::printf("[img ] %s (%ux%u, %zu B)\n", rom_name.c_str(),
                            tex.width, tex.height, entry.data.size());
            ++img_count;

        } else if (is_sfx(p)) {
            entry.type = ASSET_SFX;
            entry.data = convert_sfx(p);
            if (entry.data.empty()) continue;
            if (opts.verbose)
                std::printf("[sfx ] %s (%zu B)\n", rom_name.c_str(), entry.data.size());
            ++sfx_count;

        } else if (is_music(p)) {
            if (!opts.convert_music) continue;
            entry.type = ASSET_MUSIC;
            entry.data = convert_music(p);
            if (entry.data.empty()) continue;
            if (opts.verbose)
                std::printf("[mus ] %s (%zu B)\n", rom_name.c_str(), entry.data.size());
            ++mus_count;

        } else if (is_wml(p)) {
            entry.type = ASSET_WML;
            entry.data = compile_wml(p, opts.wesnoth_data_dir);
            if (entry.data.empty()) continue;
            if (opts.verbose)
                std::printf("[wml ] %s (%zu B)\n", rom_name.c_str(), entry.data.size());
            ++wml_count;

        } else if (is_lua(p)) {
            entry.type = ASSET_LUA;
            entry.data = compile_lua(p);
            if (entry.data.empty()) continue;
            if (opts.verbose)
                std::printf("[lua ] %s (%zu B)\n", rom_name.c_str(), entry.data.size());
            ++lua_count;

        } else {
            continue; // skip unknown types
        }

        assets.push_back(std::move(entry));
    }

    std::printf("\nAssets collected:\n");
    std::printf("  Textures : %zu\n", img_count);
    std::printf("  SFX      : %zu\n", sfx_count);
    std::printf("  Music    : %zu\n", mus_count);
    std::printf("  WML      : %zu\n", wml_count);
    std::printf("  Lua      : %zu\n", lua_count);
    std::printf("  Total    : %zu entries\n", assets.size());

    // --- Step 3: Load ROM, build ROMFS, patch ---
    std::printf("\nLoading ROM image...\n");
    std::vector<uint8_t> rom = load_rom(input_rom_path);
    if (rom.empty()) return 1;

    std::printf("Building ROM filesystem...\n");
    if (!build_romfs(assets, opts, rom)) {
        std::fprintf(stderr, "ROMFS build failed.\n");
        return 1;
    }

    std::printf("Writing ROM: %s (%zu MB)\n",
                opts.output_rom.c_str(), rom.size() / 1024 / 1024);
    if (!write_rom(opts.output_rom, rom)) return 1;

    // --- Step 4: Write resolved ROM offsets source file ---
    fs::path offsets_cpp = fs::path("src/n64/n64_romfs_offsets.cpp");
    std::printf("Writing %s...\n", offsets_cpp.c_str());
    if (!write_romfs_offsets(assets, offsets_cpp)) {
        std::fprintf(stderr, "Warning: could not write ROM offsets file.\n");
    }

    std::printf("\nDone. ROM ready: %s\n", opts.output_rom.c_str());
    return 0;
}
