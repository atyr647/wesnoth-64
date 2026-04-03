/**
 * n64_memory.cpp
 *
 * Global memory pool definitions for the N64 port.
 * All memory is statically partitioned at startup to avoid fragmentation.
 */

#include "n64_memory.hpp"

// -----------------------------------------------------------------------
// Static backing buffers (placed in BSS — zeroed at startup by CRT)
// -----------------------------------------------------------------------
static u8 s_game_buf [N64_POOL_GAME_STATE]   __attribute__((aligned(8)));
static u8 s_wml_buf  [N64_POOL_WML_PARSE]    __attribute__((aligned(8)));
static char s_string_data[128 * 1024]        __attribute__((aligned(4)));

// -----------------------------------------------------------------------
// Global allocator instances
// -----------------------------------------------------------------------
LinearAllocator g_arena_game;
LinearAllocator g_arena_wml;
StringTable     g_strings;

// -----------------------------------------------------------------------
// n64_memory_init — call once from main() before any game systems
// -----------------------------------------------------------------------
void n64_memory_init() {
    g_arena_game = LinearAllocator(s_game_buf, sizeof(s_game_buf));
    g_arena_wml  = LinearAllocator(s_wml_buf,  sizeof(s_wml_buf));
    // StringTable constructor already ran (global ctor), nothing extra needed
    N64_LOG("Memory pools initialised: game=%uKB wml=%uKB strings=128KB",
            (unsigned)(sizeof(s_game_buf) / 1024),
            (unsigned)(sizeof(s_wml_buf)  / 1024));
}

// Exposed declaration so n64_main.cpp can call it
extern "C" void n64_memory_init();
