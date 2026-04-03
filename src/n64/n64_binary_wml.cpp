/**
 * n64_binary_wml.cpp
 *
 * Runtime binary WML reader for Wesnoth N64.
 *
 * Streams binary WML opcodes into Wesnoth's config tree.
 * The config API (src/config.hpp) is identical to the PC build.
 */

#include "n64_binary_wml.hpp"

#ifdef N64_BUILD
#  include <dma.h>
#endif

// -----------------------------------------------------------------------
// String table (in-parse context, allocated from g_arena_wml)
// -----------------------------------------------------------------------
struct BwmlStringTable {
    u16         count;
    const char* strings[4096]; // pointers into the string data blob
};

static bool build_string_table(const u8* str_section, u32 str_size,
                                BwmlStringTable& out) {
    if (str_size < 2) return false;
    out.count = (u16)(str_section[0] | (str_section[1] << 8));
    if (out.count > 4096) return false;

    const u8* offsets = str_section + 2;
    for (u16 i = 0; i < out.count; ++i) {
        u16 off = (u16)(offsets[i * 2] | (offsets[i * 2 + 1] << 8));
        out.strings[i] = reinterpret_cast<const char*>(str_section + off);
    }
    return true;
}

// -----------------------------------------------------------------------
// n64_bwml_parse — core parser
// -----------------------------------------------------------------------
bool n64_bwml_parse(const u8* data, u32 byte_length, config& out) {
    if (!data || byte_length < 4) return false;

    // Read ops_size
    u32 ops_size = (u32)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24));
    if (4 + ops_size > byte_length) return false;

    const u8* ops     = data + 4;
    const u8* str_sec = ops + ops_size;
    u32       str_sz  = byte_length - 4 - ops_size;

    // Build string table
    BwmlStringTable strtab;
    if (!build_string_table(str_sec, str_sz, strtab)) return false;

    // Helper: get string by index
    auto get_str = [&](u16 idx) -> const char* {
        if (idx >= strtab.count) return "";
        return strtab.strings[idx];
    };

    // Helper: read u16 from ops stream
    u32 ip = 0;
    auto read_u16 = [&]() -> u16 {
        if (ip + 1 >= ops_size) return 0;
        u16 v = (u16)(ops[ip] | (ops[ip + 1] << 8));
        ip += 2;
        return v;
    };

    // Walk the opcode stream, building the config tree
    // Use a pointer stack to track the current config node
    config* stack[64];
    int     depth = 0;
    stack[0] = &out;

    while (ip < ops_size) {
        BwmlOp op = (BwmlOp)ops[ip++];
        switch (op) {
        case BWML_END:
            goto done;

        case BWML_TAG_OPEN: {
            u16 name_idx = read_u16();
            const char* name = get_str(name_idx);
            config& parent = *stack[depth];
            config& child  = parent.add_child(name);
            N64_ASSERT(depth + 1 < 64);
            stack[++depth] = &child;
            break;
        }
        case BWML_TAG_MERGE: {
            u16 name_idx = read_u16();
            const char* name = get_str(name_idx);
            config& parent = *stack[depth];
            // [+TAG] merges into an existing child or creates a new one
            config::child_itors range = parent.child_range(name);
            config* target = nullptr;
            if (range.begin() != range.end())
                target = &*range.begin();
            else {
                config& nc = parent.add_child(name);
                target = &nc;
            }
            N64_ASSERT(depth + 1 < 64);
            stack[++depth] = target;
            break;
        }
        case BWML_TAG_CLOSE:
            if (depth > 0) --depth;
            break;

        case BWML_KEY_VALUE: {
            u16 key_idx = read_u16();
            u16 val_idx = read_u16();
            const char* key = get_str(key_idx);
            const char* val = get_str(val_idx);
            (*stack[depth])[key] = val;
            break;
        }
        default:
            N64_LOG("BWML: unknown opcode 0x%02X at ip=%u", op, ip - 1);
            return false;
        }
    }
done:
    return true;
}

// -----------------------------------------------------------------------
// n64_bwml_load — DMA from ROM then parse
// -----------------------------------------------------------------------
bool n64_bwml_load(u32 rom_offset, u32 byte_length, config& out) {
    // Allocate from WML arena (reset after scenario load)
    N64_ASSERT(byte_length <= g_arena_wml.capacity() - g_arena_wml.used());
    u8* buf = static_cast<u8*>(g_arena_wml.alloc(byte_length, 8));

#ifdef N64_BUILD
    data_cache_hit_writeback_invalidate(buf, byte_length);
    dma_read(buf, rom_offset, byte_length);
#else
    // Host stub: buffer stays zeroed (empty config)
    (void)rom_offset;
    std::memset(buf, 0, byte_length);
    return true;
#endif

    return n64_bwml_parse(buf, byte_length, out);
}
