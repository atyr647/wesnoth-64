/**
 * wml_compiler.cpp
 *
 * Compiles Wesnoth WML (.cfg) files to a compact binary format
 * for streaming from ROM on N64.
 *
 * Binary WML format:
 *   All strings are stored in a per-file string table at the top of the blob.
 *   WML tags and keys are encoded as:
 *
 *   [TAG_OPEN  u8=0x01] [name_idx u16]
 *   [KEY_VALUE u8=0x02] [key_idx u16] [val_idx u16]
 *   [TAG_CLOSE u8=0x03]
 *   [TAG_MERGE u8=0x04] [name_idx u16]  (for [+TAG])
 *   [END       u8=0x00]
 *
 *   Followed by the string table:
 *   [u16 count] [u16 offset0] [u16 offset1] ... [char data...]
 *
 * The N64 binary WML reader (src/serialization/binary_wml.cpp) reads
 * this format into Wesnoth's config tree.
 *
 * This is a simplified implementation — it reads WML line-by-line
 * rather than fully parsing nested structures, because the full Wesnoth
 * WML parser depends on Boost and is complex. For correctness, a future
 * implementation should link against libwesnoth's parser.
 */

#include "pipeline.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace n64pack {

// -----------------------------------------------------------------------
// Binary WML opcodes
// -----------------------------------------------------------------------
enum BwmlOp : uint8_t {
    BWML_END       = 0x00,
    BWML_TAG_OPEN  = 0x01,
    BWML_KEY_VALUE = 0x02,
    BWML_TAG_CLOSE = 0x03,
    BWML_TAG_MERGE = 0x04,
};

// -----------------------------------------------------------------------
// String table builder
// -----------------------------------------------------------------------
struct StringTable {
    std::unordered_map<std::string, uint16_t> map;
    std::vector<std::string> strings;

    uint16_t intern(const std::string& s) {
        auto it = map.find(s);
        if (it != map.end()) return it->second;
        uint16_t idx = (uint16_t)strings.size();
        map[s] = idx;
        strings.push_back(s);
        return idx;
    }

    std::vector<uint8_t> serialise() const {
        // [u16 count] [u16 offset0...] [char data...]
        std::vector<uint8_t> out;
        uint16_t count = (uint16_t)strings.size();
        out.push_back(count & 0xFF);
        out.push_back(count >> 8);

        // Calculate offsets
        uint16_t data_start = 2 + count * 2;
        uint16_t cursor = 0;
        for (const auto& s : strings) {
            uint16_t off = data_start + cursor;
            out.push_back(off & 0xFF);
            out.push_back(off >> 8);
            cursor += (uint16_t)(s.size() + 1);
        }
        // String data
        for (const auto& s : strings) {
            for (char c : s) out.push_back((uint8_t)c);
            out.push_back(0); // null terminator
        }
        return out;
    }
};

// -----------------------------------------------------------------------
// Simple WML line parser
//
// Handles:
//   [tag_name]         → TAG_OPEN
//   [+tag_name]        → TAG_MERGE
//   [/tag_name]        → TAG_CLOSE
//   key=value          → KEY_VALUE
//   # comment          → ignored
//   << heredoc >>      → KEY_VALUE with multi-line value
// -----------------------------------------------------------------------
static void trim_str(std::string& s) {
    auto l = s.find_first_not_of(" \t\r\n");
    auto r = s.find_last_not_of(" \t\r\n");
    s = (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
}

std::vector<uint8_t> compile_wml(const std::filesystem::path& cfg_path,
                                  const std::filesystem::path& /*data_dir*/) {
    std::ifstream f(cfg_path);
    if (!f) return {};

    std::vector<uint8_t> ops;
    StringTable strtab;
    bool in_heredoc = false;
    std::string heredoc_key;
    std::string heredoc_buf;

    auto emit_u8  = [&](uint8_t v)  { ops.push_back(v); };
    auto emit_u16 = [&](uint16_t v) {
        ops.push_back(v & 0xFF);
        ops.push_back(v >> 8);
    };

    std::string line;
    while (std::getline(f, line)) {
        trim_str(line);

        // Comment
        if (line.empty() || line[0] == '#') continue;

        // Heredoc continuation
        if (in_heredoc) {
            if (line == ">>") {
                trim_str(heredoc_buf);
                uint16_t k = strtab.intern(heredoc_key);
                uint16_t v = strtab.intern(heredoc_buf);
                emit_u8(BWML_KEY_VALUE);
                emit_u16(k);
                emit_u16(v);
                in_heredoc  = false;
                heredoc_key = "";
                heredoc_buf = "";
            } else {
                heredoc_buf += line + "\n";
            }
            continue;
        }

        // Tag open: [tag] or [+tag]
        if (line.front() == '[' && line.back() == ']') {
            std::string tag_inner = line.substr(1, line.size() - 2);
            if (!tag_inner.empty() && tag_inner.front() == '/') {
                // Close tag [/tag]
                emit_u8(BWML_TAG_CLOSE);
            } else if (!tag_inner.empty() && tag_inner.front() == '+') {
                // Merge tag [+tag]
                std::string name = tag_inner.substr(1);
                trim_str(name);
                emit_u8(BWML_TAG_MERGE);
                emit_u16(strtab.intern(name));
            } else {
                emit_u8(BWML_TAG_OPEN);
                emit_u16(strtab.intern(tag_inner));
            }
            continue;
        }

        // Key=value
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            trim_str(key);
            trim_str(val);

            // Remove surrounding quotes if present
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);

            // Check for heredoc start (value is <<)
            if (val == "<<") {
                in_heredoc  = true;
                heredoc_key = key;
                heredoc_buf = "";
                continue;
            }

            // Remove trailing # comment from value
            auto comment = val.find('#');
            if (comment != std::string::npos) {
                val = val.substr(0, comment);
                trim_str(val);
            }

            emit_u8(BWML_KEY_VALUE);
            emit_u16(strtab.intern(key));
            emit_u16(strtab.intern(val));
            continue;
        }

        // Preprocessor directives (#ifdef, #define, etc.) — skip
        // The N64 build only includes pre-preprocessed WML
    }

    emit_u8(BWML_END);

    // Append string table after ops
    auto str_data = strtab.serialise();

    // Final output: [u32 ops_size] [ops...] [string_table...]
    std::vector<uint8_t> out;
    uint32_t ops_size = (uint32_t)ops.size();
    out.push_back(ops_size & 0xFF);
    out.push_back((ops_size >> 8)  & 0xFF);
    out.push_back((ops_size >> 16) & 0xFF);
    out.push_back((ops_size >> 24) & 0xFF);
    out.insert(out.end(), ops.begin(), ops.end());
    out.insert(out.end(), str_data.begin(), str_data.end());
    return out;
}

} // namespace n64pack
