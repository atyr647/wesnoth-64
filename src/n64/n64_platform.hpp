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
#  define color_t libdragon_color_t
#  include <libdragon.h>
#  include <n64sys.h>
#  undef color_t
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#ifdef __cplusplus
#  include <cstring>   // std::memset etc. (the engine build gets these via the shims)
#  include <cstdlib>
#endif

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
// With Expansion Pak: 8 MB total RDRAM. Pools are kept tight to make
// room for the (very large) Wesnoth code section.
// -----------------------------------------------------------------------
static constexpr size_t N64_RAM_TOTAL        = 8 * 1024 * 1024;
static constexpr size_t N64_POOL_GAME_STATE  =  256 * 1024;  // 256 KB
static constexpr size_t N64_POOL_LUA         =  128 * 1024;  // 128 KB
static constexpr size_t N64_POOL_TEXTURE_CACHE = 256 * 1024; // 256 KB
static constexpr size_t N64_POOL_AUDIO       =  128 * 1024;  // 128 KB
static constexpr size_t N64_POOL_WML_PARSE   =  256 * 1024;  // 256 KB
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
// POSIX stubs not available on N64
// -----------------------------------------------------------------------
#ifdef N64_BUILD
#include <unistd.h>
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#endif

// -----------------------------------------------------------------------
// Compile-time guard: ensure N64-incompatible headers are not included
// -----------------------------------------------------------------------
#ifdef N64_BUILD
#  define SDL_h_                    // block SDL.h
#  define BOOST_FILESYSTEM_HPP      // block boost/filesystem.hpp
#endif
