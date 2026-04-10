/**
 * n64_save.cpp
 *
 * N64 cartridge SRAM/Flash save implementation.
 *
 * Uses libdragon's eeprom/sram/flashram APIs.
 * On EverDrive/flashcart: Flash (128KB) is typically available.
 * On real cartridge: SRAM (32KB) only.
 *
 * zlib is used for compression — it's small, portable, and already
 * required by Wesnoth's PC build for save file compression.
 */

#ifdef N64_BUILD

#include "n64_save.hpp"
#include "n64_platform.hpp"

#include <cstring>
#include <cstdlib>

// Minimal zlib subset for compression/decompression
// On N64 we include a lightweight zlib implementation (already in libdragon)
extern "C" {
#include <zlib.h>
}

// PI address for SRAM / FlashRAM on the cartridge bus
#define SRAM_PI_BASE  0x08000000

// -----------------------------------------------------------------------
// Hardware abstraction
// -----------------------------------------------------------------------
static bool s_has_flash = false;
static u32  s_capacity  = SRAM_SIZE;

// Save index cached in RAM
static N64SaveSlot s_slots[N64_SAVE_SLOTS];

// -----------------------------------------------------------------------
// Low-level SRAM/Flash I/O
// -----------------------------------------------------------------------
static void save_hw_read(u32 offset, void* dst, u32 len) {
#ifdef N64_BUILD
    // libdragon accesses SRAM/FlashRAM via PI DMA at 0x08000000
    if (s_has_flash) {
        // Flash reads are 128-byte aligned
        u8 buf[128] __attribute__((aligned(16)));
        u32 pos = 0;
        while (pos < len) {
            u32 chunk = (len - pos > 128) ? 128 : (len - pos);
            u32 aligned_off = (offset + pos) & ~127u;
            dma_read(buf, SRAM_PI_BASE + aligned_off, 128);
            u32 skip = (offset + pos) - aligned_off;
            u32 copy = (chunk > 128 - skip) ? 128 - skip : chunk;
            std::memcpy((u8*)dst + pos, buf + skip, copy);
            pos += copy;
        }
    } else {
        dma_read(dst, SRAM_PI_BASE + offset, len);
    }
#endif
}

static void save_hw_write(u32 offset, const void* src, u32 len) {
#ifdef N64_BUILD
    if (s_has_flash) {
        // Flash writes require sector erase + write
        // PI DMA write to FlashRAM address space
        dma_write(src, SRAM_PI_BASE + offset, len);
    } else {
        dma_write(src, SRAM_PI_BASE + offset, len);
    }
#endif
}

// -----------------------------------------------------------------------
// CRC32 (standard polynomial)
// -----------------------------------------------------------------------
static u32 crc32_calc(const u8* data, u32 len) {
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

// -----------------------------------------------------------------------
// n64_save_init
// -----------------------------------------------------------------------
void n64_save_init() {
#ifdef N64_BUILD
    // Try to detect Flash vs SRAM
    // On EverDrive, Flash is always available
    // Simple probe: try reading Flash ID register
    s_has_flash = false; // Default to SRAM
    // Attempt Flash detection
    // TODO: Use libdragon flash detection API when available
    s_capacity = s_has_flash ? FLASH_SIZE : SRAM_SIZE;
#endif

    // Read save index
    n64_save_read_index();

    N64_LOG("Save init: %s, %uKB, %d slots",
            s_has_flash ? "Flash" : "SRAM",
            s_capacity / 1024, N64_SAVE_SLOTS);
}

u32  n64_save_capacity()   { return s_capacity; }
bool n64_save_has_flash()  { return s_has_flash; }

// -----------------------------------------------------------------------
// n64_save_read_index
// -----------------------------------------------------------------------
void n64_save_read_index() {
    u8 index_buf[SAVE_INDEX_SIZE];
    save_hw_read(0, index_buf, SAVE_INDEX_SIZE);

    // Check magic at offset 0
    u32 magic;
    std::memcpy(&magic, index_buf, 4);
    if (magic != N64_SAVE_MAGIC) {
        // Virgin SRAM — initialise empty
        N64_LOG("Save: no save data found, initialising");
        for (int i = 0; i < N64_SAVE_SLOTS; ++i) {
            std::memset(&s_slots[i], 0, sizeof(N64SaveSlot));
        }
        // Write empty index
        std::memset(index_buf, 0, SAVE_INDEX_SIZE);
        std::memcpy(index_buf, &magic, 4); // write magic
        magic = N64_SAVE_MAGIC;
        std::memcpy(index_buf, &magic, 4);
        save_hw_write(0, index_buf, SAVE_INDEX_SIZE);
        return;
    }

    // Read slot metadata (each 56 bytes, starting at offset 16)
    for (int i = 0; i < N64_SAVE_SLOTS; ++i) {
        u32 base = 16 + i * 56;
        if (base + 56 > SAVE_INDEX_SIZE) break;

        u8 occupied = index_buf[base];
        s_slots[i].occupied = (occupied != 0);
        if (!s_slots[i].occupied) continue;

        std::memcpy(&s_slots[i].compressed_size,   index_buf + base + 1, 4);
        std::memcpy(&s_slots[i].uncompressed_size, index_buf + base + 5, 4);
        std::memcpy(&s_slots[i].crc32,             index_buf + base + 9, 4);
        std::memcpy(s_slots[i].label,              index_buf + base + 13, 32);
        s_slots[i].label[31] = '\0';
        s_slots[i].campaign_idx  = index_buf[base + 45];
        s_slots[i].scenario_idx  = index_buf[base + 46];
        std::memcpy(&s_slots[i].turn_number, index_buf + base + 47, 2);
    }
}

const N64SaveSlot& n64_save_slot_info(int slot) {
    N64_ASSERT(slot >= 0 && slot < N64_SAVE_SLOTS);
    return s_slots[slot];
}

// -----------------------------------------------------------------------
// n64_save_write
// -----------------------------------------------------------------------
bool n64_save_write(int slot, const std::string& wml_data,
                    const char* label, u8 campaign_idx,
                    u8 scenario_idx, u16 turn_number) {
    N64_ASSERT(slot >= 0 && slot < N64_SAVE_SLOTS);

    // Compress with zlib
    uLong src_len = (uLong)wml_data.size();
    uLong bound   = compressBound(src_len);
    u8* compressed = (u8*)std::malloc(bound);
    if (!compressed) {
        N64_LOG("Save: malloc failed for compression buffer");
        return false;
    }

    uLong compressed_len = bound;
    int zret = compress2(compressed, &compressed_len,
                         (const Bytef*)wml_data.data(), src_len, Z_BEST_COMPRESSION);
    if (zret != Z_OK) {
        N64_LOG("Save: zlib compress failed: %d", zret);
        std::free(compressed);
        return false;
    }

    // Check if it fits
    u32 data_offset = SAVE_INDEX_SIZE + slot * (s_capacity / N64_SAVE_SLOTS);
    u32 max_slot_size = s_capacity / N64_SAVE_SLOTS - SAVE_HEADER_SIZE;
    if (compressed_len > max_slot_size) {
        N64_LOG("Save: compressed size %lu exceeds slot capacity %u",
                compressed_len, max_slot_size);
        std::free(compressed);
        return false;
    }

    // CRC32 of compressed data
    u32 crc = crc32_calc(compressed, (u32)compressed_len);

    // Write save data header + payload
    u8 header[SAVE_HEADER_SIZE];
    u32 magic = N64_SAVE_MAGIC;
    std::memcpy(header + 0, &magic, 4);
    u32 sl = (u32)slot;
    std::memcpy(header + 4, &sl, 4);
    u32 clen = (u32)compressed_len;
    std::memcpy(header + 8,  &clen, 4);
    u32 ulen = (u32)src_len;
    std::memcpy(header + 12, &ulen, 4);
    std::memcpy(header + 16, &crc, 4);

    save_hw_write(data_offset, header, SAVE_HEADER_SIZE);
    save_hw_write(data_offset + SAVE_HEADER_SIZE, compressed, (u32)compressed_len);
    std::free(compressed);

    // Update index
    s_slots[slot].occupied         = true;
    s_slots[slot].compressed_size  = (u32)compressed_len;
    s_slots[slot].uncompressed_size = (u32)src_len;
    s_slots[slot].crc32            = crc;
    std::strncpy(s_slots[slot].label, label, 31);
    s_slots[slot].label[31]        = '\0';
    s_slots[slot].campaign_idx     = campaign_idx;
    s_slots[slot].scenario_idx     = scenario_idx;
    s_slots[slot].turn_number      = turn_number;

    // Write updated index to SRAM
    u8 index_buf[SAVE_INDEX_SIZE];
    save_hw_read(0, index_buf, SAVE_INDEX_SIZE);
    u32 base = 16 + slot * 56;
    index_buf[base] = 1; // occupied
    std::memcpy(index_buf + base + 1,  &s_slots[slot].compressed_size, 4);
    std::memcpy(index_buf + base + 5,  &s_slots[slot].uncompressed_size, 4);
    std::memcpy(index_buf + base + 9,  &s_slots[slot].crc32, 4);
    std::memcpy(index_buf + base + 13, s_slots[slot].label, 32);
    index_buf[base + 45] = s_slots[slot].campaign_idx;
    index_buf[base + 46] = s_slots[slot].scenario_idx;
    std::memcpy(index_buf + base + 47, &s_slots[slot].turn_number, 2);
    save_hw_write(0, index_buf, SAVE_INDEX_SIZE);

    N64_LOG("Save: slot %d written (%u bytes compressed)", slot, (u32)compressed_len);
    return true;
}

// -----------------------------------------------------------------------
// n64_save_read
// -----------------------------------------------------------------------
bool n64_save_read(int slot, std::string& wml_data_out) {
    N64_ASSERT(slot >= 0 && slot < N64_SAVE_SLOTS);
    if (!s_slots[slot].occupied) return false;

    u32 data_offset = SAVE_INDEX_SIZE + slot * (s_capacity / N64_SAVE_SLOTS);

    // Read header
    u8 header[SAVE_HEADER_SIZE];
    save_hw_read(data_offset, header, SAVE_HEADER_SIZE);

    u32 magic;
    std::memcpy(&magic, header, 4);
    if (magic != N64_SAVE_MAGIC) {
        N64_LOG("Save: bad magic in slot %d", slot);
        return false;
    }

    u32 clen, ulen, crc;
    std::memcpy(&clen, header + 8, 4);
    std::memcpy(&ulen, header + 12, 4);
    std::memcpy(&crc,  header + 16, 4);

    // Read compressed data
    u8* compressed = (u8*)std::malloc(clen);
    if (!compressed) return false;
    save_hw_read(data_offset + SAVE_HEADER_SIZE, compressed, clen);

    // Verify CRC
    u32 actual_crc = crc32_calc(compressed, clen);
    if (actual_crc != crc) {
        N64_LOG("Save: CRC mismatch in slot %d (expected %08X, got %08X)",
                slot, crc, actual_crc);
        std::free(compressed);
        return false;
    }

    // Decompress
    u8* decompressed = (u8*)std::malloc(ulen);
    if (!decompressed) { std::free(compressed); return false; }

    uLong dest_len = ulen;
    int zret = uncompress(decompressed, &dest_len, compressed, clen);
    std::free(compressed);

    if (zret != Z_OK) {
        N64_LOG("Save: zlib uncompress failed: %d", zret);
        std::free(decompressed);
        return false;
    }

    wml_data_out.assign((char*)decompressed, dest_len);
    std::free(decompressed);

    N64_LOG("Save: slot %d loaded (%u bytes)", slot, (u32)dest_len);
    return true;
}

// -----------------------------------------------------------------------
// n64_save_delete
// -----------------------------------------------------------------------
void n64_save_delete(int slot) {
    N64_ASSERT(slot >= 0 && slot < N64_SAVE_SLOTS);
    s_slots[slot].occupied = false;

    u8 index_buf[SAVE_INDEX_SIZE];
    save_hw_read(0, index_buf, SAVE_INDEX_SIZE);
    u32 base = 16 + slot * 56;
    index_buf[base] = 0;
    save_hw_write(0, index_buf, SAVE_INDEX_SIZE);

    N64_LOG("Save: slot %d deleted", slot);
}

#endif // N64_BUILD
