/**
 * vfs_pack.cpp -- pack the game data as a read-only filesystem for the engine.
 *
 * The N64 port runs Wesnoth's own filesystem code (src/filesystem.cpp) over a
 * ROM-backed virtual filesystem (src/n64/n64_vfs.cpp). For that the ROMFS must
 * look like the data tree on disk:
 *
 *   - every file is stored under its exact repository-relative path
 *     ("data/core/units.cfg", "images/misc/logo.png", "sounds/button.wav");
 *   - WML, Lua, maps and other text are stored verbatim, so Wesnoth's own
 *     preprocessor and parser read them;
 *   - PNGs become RGBA5551 textures and sounds 22 kHz mono PCM (the port's
 *     image and sound layers consume those);
 *   - every directory gets a listing entry "vfs:<dir>" whose lines are
 *     "d <name>" or "f <name>", so directory iteration needs no RAM table;
 *   - entries are LZ4-compressed (block format) where that saves space; the
 *     uncompressed size is recorded in the directory entry.
 */

#include "pipeline.hpp"

#include <lz4hc.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace n64pack {
namespace fs = std::filesystem;

static std::vector<uint8_t> read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::vector<uint8_t> d((size_t)f.tellg());
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), (std::streamsize)d.size());
    return d;
}

static bool ext_in(const fs::path& p, std::initializer_list<const char*> exts) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    for (const char* x : exts) if (e == x) return true;
    return false;
}

size_t pack_vfs(const fs::path& repo_root, const PipelineOptions& opts, std::vector<AssetEntry>& out) {
    std::map<std::string, std::set<std::string>> dir_files, dir_dirs;
    size_t text = 0, tex = 0, snd = 0, skipped = 0;
    std::map<std::string, size_t> skipped_ext;

    auto add_to_tree = [&](const std::string& rel) {
        std::string path = rel;
        size_t slash = path.rfind('/');
        dir_files[slash == std::string::npos ? "" : path.substr(0, slash)]
            .insert(slash == std::string::npos ? path : path.substr(slash + 1));
        while ((slash = path.rfind('/')) != std::string::npos) {
            std::string parent = path.substr(0, slash);
            size_t ps = parent.rfind('/');
            dir_dirs[ps == std::string::npos ? "" : parent.substr(0, ps)]
                .insert(ps == std::string::npos ? parent : parent.substr(ps + 1));
            path = parent;
        }
    };

    for (const char* top : { "data", "images", "sounds" }) {
        const fs::path root = repo_root / top;
        if (!fs::is_directory(root)) continue;
        for (auto& de : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            if (!de.is_regular_file()) continue;
            const fs::path& p = de.path();
            std::string rel = fs::relative(p, repo_root).generic_string();
            if (rel.find("/.") != std::string::npos) continue;          // dotfiles
            // English only: translation catalogues are not part of the data tree.
            if (opts.english_only && ext_in(p, { ".po", ".mo" })) continue;
            // Campaign filter (optional): other campaigns keep their _main.cfg
            // so the campaign list is complete, but not their content.
            if (!opts.campaign.empty() && !opts.all_campaigns &&
                rel.rfind("data/campaigns/", 0) == 0) {
                std::string sub = rel.substr(std::strlen("data/campaigns/"));
                std::string camp = sub.substr(0, sub.find('/'));
                if (camp != opts.campaign && sub != camp + "/_main.cfg") continue;
            }

            AssetEntry e{};
            e.name = rel;
            if (ext_in(p, { ".cfg", ".lua", ".map", ".mask", ".txt", ".pbl", ".fluent", ".xml" })) {
                e.type = ASSET_WML;
                e.data = read_all(p);
                ++text;
            } else if (ext_in(p, { ".png" })) {
                Texture16 t = convert_image(p, 0, 0);
                if (t.pixels.empty()) { ++skipped; continue; }
                e.type = ASSET_TEXTURE;
                e.width = t.width;
                e.height = t.height;
                e.data.resize(t.pixels.size() * 2);
                for (size_t k = 0; k < t.pixels.size(); ++k) {
                    e.data[k * 2] = uint8_t(t.pixels[k] >> 8);
                    e.data[k * 2 + 1] = uint8_t(t.pixels[k]);
                }
                ++tex;
            } else if (ext_in(p, { ".ogg", ".wav" }) && rel.find("/music/") == std::string::npos) {
                if (opts.sfx_max_ms < 0) { ++skipped; continue; }
                e.type = ASSET_SFX;
                e.data = convert_sfx(p, opts.sfx_max_ms);
                if (e.data.empty()) { ++skipped; continue; }
                for (size_t k = 0; k + 1 < e.data.size(); k += 2) std::swap(e.data[k], e.data[k + 1]);
                ++snd;
            } else {
                ++skipped;
                ++skipped_ext[p.extension().string()];
                continue;
            }
            add_to_tree(rel);
            out.push_back(std::move(e));
        }
    }

    // Directory listings.
    std::set<std::string> all_dirs;
    for (auto& kv : dir_files) all_dirs.insert(kv.first);
    for (auto& kv : dir_dirs) { all_dirs.insert(kv.first); for (auto& d : kv.second) all_dirs.insert(kv.first.empty() ? d : kv.first + "/" + d); }
    for (const std::string& d : all_dirs) {
        std::string listing;
        for (auto& sub : dir_dirs[d]) listing += "d " + sub + "\n";
        for (auto& f : dir_files[d]) listing += "f " + f + "\n";
        AssetEntry e{};
        e.name = "vfs:" + d;
        e.type = ASSET_DATA;
        e.data.assign(listing.begin(), listing.end());
        out.push_back(std::move(e));
    }

    std::printf("[vfs ] text %zu, textures %zu, sounds %zu, directories %zu, skipped %zu\n",
                text, tex, snd, all_dirs.size(), skipped);
    for (auto& kv : skipped_ext)
        if (opts.verbose) std::printf("       skipped %-8s x%zu\n", kv.first.c_str(), kv.second);
    return out.size();
}

// LZ4 (block format, as libdragon's decompress_lz4_full_inplace expects).
// Compressed only where it saves at least 1/8; raw_size records the original.
void compress_entries(std::vector<AssetEntry>& assets, bool textures_too) {
    size_t before = 0, after = 0;
    for (auto& a : assets) {
        before += a.data.size();
        bool ok_type = a.type == ASSET_WML || a.type == ASSET_LUA || a.type == ASSET_DATA ||
                       (textures_too && (a.type == ASSET_TEXTURE || a.type == ASSET_SFX));
        if (ok_type && a.data.size() >= 64) {
            std::vector<uint8_t> c((size_t)LZ4_compressBound((int)a.data.size()));
            int n = LZ4_compress_HC(reinterpret_cast<const char*>(a.data.data()),
                                    reinterpret_cast<char*>(c.data()),
                                    (int)a.data.size(), (int)c.size(), LZ4HC_CLEVEL_MAX);
            if (n > 0 && (size_t)n <= a.data.size() - a.data.size() / 8) {
                a.raw_size = (uint32_t)a.data.size();
                c.resize((size_t)n);
                a.data = std::move(c);
            }
        }
        after += a.data.size();
    }
    std::printf("[lz4 ] %.2f MB -> %.2f MB\n", before / 1048576.0, after / 1048576.0);
}

} // namespace n64pack
