/**
 * main.cpp — Wesnoth N64 Asset Pipeline
 *
 * Usage:
 *   wesnoth-n64-pack [options] <wesnoth_data_dir> <output.wnrf>
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
#include <unordered_set>

namespace fs = std::filesystem;
using namespace n64pack;

// -----------------------------------------------------------------------
// Help text
// -----------------------------------------------------------------------
static void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options] <wesnoth_data_dir> <output.wnrf>\n"
        "\n"
        "Options:\n"
        "  --campaign <name>   Include only this campaign\n"
        "  --all-campaigns     Include all campaigns\n"
        "  --scale <n>         Texture downscale factor (default: 4)\n"
        "  --no-music          Skip music (this is the default)\n"
        "  --music-budget-mb <n>  Pack music, smallest tracks first, up to n MB\n"
        "  --sfx-max-ms <n>    Truncate sound effects longer than n ms (default: 1500)\n"
        "  --english-only      Skip non-English translations\n"
        "  --verbose           Verbose output\n"
        "  --help              Show this help\n"
        "\n"
        "Examples:\n"
        "  %s --campaign Heir_To_The_Throne data/ wesnoth.wnrf\n"
        "  %s --all-campaigns --no-music data/ wesnoth_full.wnrf\n",
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

// The N64 CPU is big-endian; pixel and PCM words must be stored that way.
static void append_be16(std::vector<uint8_t>& out, const uint16_t* px, size_t count) {
    out.resize(count * 2);
    for (size_t i = 0; i < count; ++i) {
        out[i * 2]     = uint8_t(px[i] >> 8);
        out[i * 2 + 1] = uint8_t(px[i]);
    }
}
static void swap_pairs(std::vector<uint8_t>& d) {   // LE s16 PCM -> BE
    for (size_t i = 0; i + 1 < d.size(); i += 2) std::swap(d[i], d[i + 1]);
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
    opts.convert_music   = false;
    opts.music_budget_mb = 0;
    opts.sfx_max_ms      = 1500;
    opts.english_only    = false;
    opts.verbose         = false;
    opts.all_campaigns   = false;

    fs::path input_rom_path;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0)        { print_usage(argv[0]); return 0; }
        else if (std::strcmp(argv[i], "--verbose") == 0) { opts.verbose = true; }
        else if (std::strcmp(argv[i], "--no-music") == 0) { opts.convert_music = false; opts.music_budget_mb = 0; }
        else if (std::strcmp(argv[i], "--music-budget-mb") == 0 && i + 1 < argc) {
            opts.music_budget_mb = std::max(0, std::atoi(argv[++i]));
            opts.convert_music = opts.music_budget_mb > 0;
        }
        else if (std::strcmp(argv[i], "--sfx-max-ms") == 0 && i + 1 < argc) {
            opts.sfx_max_ms = std::max(0, std::atoi(argv[++i]));
        }
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

    std::printf("=== Wesnoth N64 Asset Pipeline ===\n");
    std::printf("Data dir  : %s\n", opts.wesnoth_data_dir.c_str());
    std::printf("Output    : %s\n", input_rom_path.c_str());
    std::printf("Campaign  : %s\n", opts.campaign.empty() ? "(all)" : opts.campaign.c_str());
    std::printf("Scale     : 1/%d\n", opts.texture_scale);
    if (opts.convert_music) std::printf("Music     : ADPCM, budget %d MB\n", opts.music_budget_mb);
    else                    std::printf("Music     : skipped\n");
    std::printf("SFX       : 22050 Hz mono s16, max %d ms\n", opts.sfx_max_ms);
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
        append_be16(e.data, glyphs.pixels.data(), glyphs.pixels.size());
        assets.push_back(std::move(e));
        std::printf("       glyph sheet %ux%u (%zu bytes)\n",
                    glyphs.width, glyphs.height, assets.back().data.size());
    }

    // --- Step 2: Walk the data directory ---
    size_t img_count = 0, sfx_count = 0, mus_count = 0, wml_count = 0, lua_count = 0;
    std::vector<std::pair<fs::path, std::string>> music_files;

    // Work list: data/ (campaign-filtered) plus the repo's top-level images/
    // and sounds/ trees, which hold the UI art and UI sounds ("select-unit.wav").
    // Names already taken by an earlier tree win.
    std::vector<std::pair<fs::path, std::string>> work;
    {
        std::unordered_set<std::string> taken;
        auto add = [&](const fs::path& p, const std::string& name) {
            if (taken.insert(name).second) work.emplace_back(p, name);
        };
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
            add(p, romfs_name(p, opts.wesnoth_data_dir));
        }
        const fs::path repo_root = fs::absolute(opts.wesnoth_data_dir).parent_path();
        for (const char* tree : {"images", "sounds"}) {
            fs::path root = repo_root / tree;
            if (!fs::is_directory(root)) continue;
            for (auto& de : fs::recursive_directory_iterator(root,
                               fs::directory_options::skip_permission_denied)) {
                if (!de.is_regular_file()) continue;
                add(de.path(), std::string(tree) + "/" +
                               fs::relative(de.path(), root).generic_string());
            }
        }
    }

    for (const auto& [p, rom_name] : work) {
        AssetEntry entry{};
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
            append_be16(entry.data, tex.pixels.data(), tex.pixels.size());
            if (opts.verbose)
                std::printf("[img ] %s (%ux%u, %zu B)\n", rom_name.c_str(),
                            tex.width, tex.height, entry.data.size());
            ++img_count;

        } else if (is_sfx(p)) {
            entry.type = ASSET_SFX;
            entry.data = convert_sfx(p, opts.sfx_max_ms);
            if (entry.data.empty()) continue;
            swap_pairs(entry.data);
            if (opts.verbose)
                std::printf("[sfx ] %s (%zu B)\n", rom_name.c_str(), entry.data.size());
            ++sfx_count;

        } else if (is_music(p)) {
            // Converted after the walk so the size budget can pick tracks.
            if (opts.convert_music) music_files.push_back({p, rom_name});
            continue;

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

    // --- Music: smallest source files first, until the budget is spent ---
    if (!music_files.empty()) {
        std::sort(music_files.begin(), music_files.end(),
                  [](const auto& a, const auto& b) {
                      return fs::file_size(a.first) < fs::file_size(b.first);
                  });
        const uint64_t budget = (uint64_t)opts.music_budget_mb * 1024 * 1024;
        uint64_t used = 0;
        for (const auto& [mp, mname] : music_files) {
            std::vector<uint8_t> d = convert_music(mp);
            if (d.empty()) continue;
            if (used + d.size() > budget) {
                if (opts.verbose) std::printf("[mus ] over budget, skipping %s\n", mname.c_str());
                continue;
            }
            used += d.size();
            AssetEntry me;
            me.name = mname; me.type = ASSET_MUSIC; me.data = std::move(d);
            me.width = me.height = 0; me.rom_offset = 0;
            if (opts.verbose) std::printf("[mus ] %s (%zu B)\n", mname.c_str(), me.data.size());
            assets.push_back(std::move(me));
            ++mus_count;
        }
    }

    std::printf("\nAssets collected:\n");
    std::printf("  Textures : %zu\n", img_count);
    std::printf("  SFX      : %zu\n", sfx_count);
    std::printf("  Music    : %zu\n", mus_count);
    std::printf("  WML      : %zu\n", wml_count);
    std::printf("  Lua      : %zu\n", lua_count);
    std::printf("  Total    : %zu entries\n", assets.size());

    // --- Step 3: Build the ROMFS blob ---
    std::printf("\nBuilding ROM filesystem...\n");
    std::vector<uint8_t> blob;
    if (!build_romfs(assets, blob)) {
        std::fprintf(stderr, "ROMFS build failed.\n");
        return 1;
    }
    if (!write_rom(opts.output_rom, blob)) return 1;
    std::printf("\nDone. Wrote %s (%.2f MB)\n"
                "Append it to the ROM with: n64tool ... --align 256 %s\n",
                opts.output_rom.c_str(), blob.size() / 1048576.0,
                opts.output_rom.c_str());
    return 0;
}
