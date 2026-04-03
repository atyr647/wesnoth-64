#pragma once

/**
 * n64_platform.hpp
 *
 * Top-level platform abstraction header for the Nintendo 64 port.
 * Include this instead of SDL2, Boost, OS headers on N64.
 *
 * Also included by host-side tools (with N64_HOST_TOOL defined) to share
 * type definitions without pulling in libdragon.
 */

#ifdef N64_BUILD
#  include <libdragon.h>
#  include <n64sys.h>
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

// -----------------------------------------------------------------------
// Basic integer types (mirrors what SDL2 / Boost provided)
// -----------------------------------------------------------------------
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

// -----------------------------------------------------------------------
// Screen constants
// -----------------------------------------------------------------------
static constexpr int N64_SCREEN_W = 320;
static constexpr int N64_SCREEN_H = 240;

// Wesnoth hex tile size on PC is 72px. On N64 we use 18px (1/4 scale).
static constexpr int N64_HEX_SIZE  = 18;
static constexpr int N64_HEX_W     = N64_HEX_SIZE;
static constexpr int N64_HEX_H     = N64_HEX_SIZE;

// -----------------------------------------------------------------------
// Memory budget constants
// With Expansion Pak: 8 MB total RDRAM
// -----------------------------------------------------------------------
static constexpr size_t N64_RAM_TOTAL        = 8 * 1024 * 1024;
static constexpr size_t N64_POOL_GAME_STATE  = 1536 * 1024;  // 1.5 MB
static constexpr size_t N64_POOL_LUA         =  512 * 1024;  //   512 KB
static constexpr size_t N64_POOL_TEXTURE_CACHE = 1024 * 1024; //   1 MB
static constexpr size_t N64_POOL_AUDIO       =  512 * 1024;  //   512 KB
static constexpr size_t N64_POOL_WML_PARSE   = 1024 * 1024;  //   1 MB
static constexpr size_t N64_POOL_FRAMEBUF    =  307200;      // 320*240*2*2 (double-buffered 16-bit)

// -----------------------------------------------------------------------
// Debug output (maps to libdragon debug_printf on N64, printf on host)
// -----------------------------------------------------------------------
#ifdef N64_BUILD
#  define N64_LOG(fmt, ...)  debugf("[wesnoth] " fmt "\n", ##__VA_ARGS__)
#  define N64_ASSERT(cond)   do { if (!(cond)) { debugf("ASSERT FAILED: %s:%d\n", __FILE__, __LINE__); while(1){} } } while(0)
#else
#  include <cstdio>
#  define N64_LOG(fmt, ...)  std::printf("[wesnoth] " fmt "\n", ##__VA_ARGS__)
#  define N64_ASSERT(cond)   do { if (!(cond)) { std::fprintf(stderr, "ASSERT FAILED: %s:%d\n", __FILE__, __LINE__); std::abort(); } } while(0)
#endif

// -----------------------------------------------------------------------
// Compile-time guard: ensure N64-incompatible headers are not included
// -----------------------------------------------------------------------
#ifdef N64_BUILD
#  define SDL_h_                    // block SDL.h
#  define BOOST_FILESYSTEM_HPP      // block boost/filesystem.hpp
#endif
