#pragma once

/**
 * n64_binary_wml.hpp
 *
 * Runtime reader for the binary WML format produced by the asset pipeline.
 *
 * Binary WML format (see tools/n64-asset-pipeline/src/wml_compiler.cpp):
 *
 *   [u32 ops_size]
 *   [ops: sequence of BwmlOp bytes + u16 operands]
 *   [string table: u16 count, u16 offsets[], char data[]]
 *
 * The reader streams through the opcode sequence and populates Wesnoth's
 * config tree (src/config.hpp) identically to the text WML parser.
 *
 * Memory: the entire binary WML blob is DMA'd from ROM into g_arena_wml
 * and then parsed in-place. The config tree nodes are allocated from
 * g_arena_game. After the scenario starts, g_arena_wml is reset.
 */

#include "n64_platform.hpp"
#include "n64_memory.hpp"
#include "../config.hpp"

// -----------------------------------------------------------------------
// Binary WML opcodes (must match wml_compiler.cpp)
// -----------------------------------------------------------------------
enum BwmlOp : u8 {
    BWML_END       = 0x00,
    BWML_TAG_OPEN  = 0x01,
    BWML_KEY_VALUE = 0x02,
    BWML_TAG_CLOSE = 0x03,
    BWML_TAG_MERGE = 0x04,
};

// -----------------------------------------------------------------------
// API
// -----------------------------------------------------------------------

/**
 * Load binary WML from ROM and parse it into 'out'.
 *
 * @param rom_offset  Absolute ROM address of the binary WML blob
 * @param byte_length Size of the blob in bytes
 * @param out         Config object to populate (appended to, not cleared)
 * @return true on success
 */
bool n64_bwml_load(u32 rom_offset, u32 byte_length, config& out);

/**
 * Parse an already-DMA'd binary WML buffer.
 * Used when the caller has already loaded the data into RAM.
 *
 * @param data        Pointer to the binary WML blob in RAM
 * @param byte_length Size of the blob
 * @param out         Config object to populate
 * @return true on success
 */
bool n64_bwml_parse(const u8* data, u32 byte_length, config& out);
