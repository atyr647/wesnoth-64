/**
 * romfs_builder.cpp
 *
 * Builds the standalone ROMFS blob (wesnoth.wnrf) that n64tool appends to the
 * ROM. Wire format is documented in src/n64/n64_romfs.hpp; everything is
 * big-endian (the N64's native order) and offsets are relative to the start
 * of the blob, so the blob can sit anywhere in the cart.
 */

#include "pipeline.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace n64pack {

static constexpr uint32_t ROMFS_MAGIC = 0x574E5246; // "WNRF"
static constexpr uint32_t HEADER_SIZE = 32;
static constexpr uint32_t ENTRY_SIZE  = 32;

uint32_t romfs_hash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    v[at + 0] = uint8_t(x >> 24); v[at + 1] = uint8_t(x >> 16);
    v[at + 2] = uint8_t(x >> 8);  v[at + 3] = uint8_t(x);
}
static void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) {
    v[at + 0] = uint8_t(x >> 8); v[at + 1] = uint8_t(x);
}
static uint32_t align16(uint32_t v) { return (v + 15u) & ~15u; }

bool build_romfs(const std::vector<AssetEntry>& assets,
                 std::vector<uint8_t>& blob) {
    const size_t n = assets.size();

    // Sanity: no single asset can usefully exceed a few MB on a 64 MB cart.
    for (const auto& a : assets) {
        if (a.data.size() > 16u * 1024 * 1024) {
            std::fprintf(stderr, "ROMFS: asset '%s' is %zu bytes (type %d) -- refusing\n",
                         a.name.c_str(), a.data.size(), (int)a.type);
            return false;
        }
    }

    // Sort by hash; reject collisions (a hash must identify one file).
    std::vector<uint32_t> hashes(n);
    for (size_t i = 0; i < n; ++i) hashes[i] = romfs_hash(assets[i].name);
    std::vector<size_t> order(n);
    std::iota(order.begin(), order.end(), size_t(0));
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return hashes[a] < hashes[b]; });
    for (size_t i = 1; i < n; ++i) {
        if (hashes[order[i]] == hashes[order[i - 1]]) {
            std::fprintf(stderr, "ROMFS: hash collision between '%s' and '%s'\n",
                         assets[order[i - 1]].name.c_str(),
                         assets[order[i]].name.c_str());
            return false;
        }
    }

    // String table (debug names).
    std::vector<uint8_t> strings;
    std::vector<uint32_t> name_off(n);
    for (size_t i = 0; i < n; ++i) {
        name_off[i] = (uint32_t)strings.size();
        strings.insert(strings.end(), assets[i].name.begin(), assets[i].name.end());
        strings.push_back(0);
    }

    const uint32_t entries_off = HEADER_SIZE;
    const uint32_t strings_off = entries_off + uint32_t(n) * ENTRY_SIZE;
    const uint32_t data_off    = align16(strings_off + (uint32_t)strings.size());

    std::vector<uint32_t> off(n);
    uint64_t cursor = data_off;
    for (size_t i = 0; i < n; ++i) {
        off[i] = (uint32_t)cursor;
        cursor = align16((uint32_t)(cursor + assets[i].data.size()));
        if (cursor > 0xF0000000ull) { std::fprintf(stderr, "ROMFS: too large\n"); return false; }
    }
    const uint32_t total = (uint32_t)cursor;

    blob.assign(total, 0);
    put32(blob, 0,  ROMFS_MAGIC);
    put32(blob, 4,  (uint32_t)n);
    put32(blob, 8,  entries_off);
    put32(blob, 12, strings_off);
    put32(blob, 16, (uint32_t)strings.size());
    put32(blob, 20, data_off);
    put32(blob, 24, total);

    for (size_t k = 0; k < n; ++k) {
        const size_t i = order[k];
        const size_t at = entries_off + k * ENTRY_SIZE;
        put32(blob, at + 0,  hashes[i]);
        put32(blob, at + 4,  off[i]);
        put32(blob, at + 8,  (uint32_t)assets[i].data.size());
        put16(blob, at + 12, assets[i].width);
        put16(blob, at + 14, assets[i].height);
        blob[at + 16] = (uint8_t)assets[i].type;
        put32(blob, at + 20, name_off[i]);
    }
    std::memcpy(blob.data() + strings_off, strings.data(), strings.size());
    for (size_t i = 0; i < n; ++i)
        if (!assets[i].data.empty())
            std::memcpy(blob.data() + off[i], assets[i].data.data(), assets[i].data.size());

    std::printf("  entries %zu, directory %u B, strings %zu B, total %.2f MB\n",
                n, uint32_t(n) * ENTRY_SIZE, strings.size(), total / 1048576.0);
    return true;
}

} // namespace n64pack
