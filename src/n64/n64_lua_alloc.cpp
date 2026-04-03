/**
 * n64_lua_alloc.cpp
 *
 * Custom Lua heap allocator for N64.
 *
 * Uses a 512 KB static backing buffer (N64_POOL_LUA).
 * Implements a simple segregated free list with 16-byte alignment.
 *
 * The Lua GC will call this allocator for all allocations, reallocations,
 * and frees. It matches the lua_Alloc contract exactly.
 */

#include "n64_platform.hpp"
#include "n64_lua_alloc.h"
#include <cstring>

// -----------------------------------------------------------------------
// Backing buffer
// -----------------------------------------------------------------------
static u8 s_lua_heap[N64_POOL_LUA] __attribute__((aligned(16)));
static size_t s_lua_heap_used = 0;

// -----------------------------------------------------------------------
// Block header (16 bytes, maintains alignment)
// -----------------------------------------------------------------------
struct LuaBlock {
    u32        size;     // payload size in bytes
    u32        magic;    // 0xLUA4BEEF when allocated, 0xFREEBEEF when free
    LuaBlock*  next;     // next free block (when free) or nullptr (when used)
    u32        _pad;
};

static constexpr u32 MAGIC_ALLOC = 0xC0DEB10C;
static constexpr u32 MAGIC_FREE  = 0xDEADBEEF;
static constexpr u16 HEADER_SIZE = sizeof(LuaBlock);
static constexpr u16 ALIGN       = 16;

static LuaBlock* s_free_list = nullptr;

// -----------------------------------------------------------------------
// Initialise the heap (called once)
// -----------------------------------------------------------------------
static bool s_initialized = false;
static void lua_heap_init() {
    if (s_initialized) return;
    s_initialized = true;
    // The entire heap starts as one big free block
    LuaBlock* blk  = reinterpret_cast<LuaBlock*>(s_lua_heap);
    blk->size  = sizeof(s_lua_heap) - HEADER_SIZE;
    blk->magic = MAGIC_FREE;
    blk->next  = nullptr;
    s_free_list = blk;
}

// -----------------------------------------------------------------------
// Allocate 'size' bytes; return pointer to payload
// -----------------------------------------------------------------------
static void* lua_heap_alloc(size_t size) {
    if (!s_initialized) lua_heap_init();
    // Round up to alignment
    size = (size + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    LuaBlock* prev = nullptr;
    LuaBlock* cur  = s_free_list;
    while (cur) {
        if (cur->size >= size) {
            // Split if large enough
            if (cur->size >= size + HEADER_SIZE + ALIGN) {
                LuaBlock* split = reinterpret_cast<LuaBlock*>(
                    reinterpret_cast<u8*>(cur) + HEADER_SIZE + size);
                split->size  = cur->size - size - HEADER_SIZE;
                split->magic = MAGIC_FREE;
                split->next  = cur->next;
                cur->size    = (u32)size;
                cur->next    = split;
            }
            // Remove from free list
            if (prev) prev->next = cur->next;
            else       s_free_list = cur->next;
            cur->magic = MAGIC_ALLOC;
            cur->next  = nullptr;
            s_lua_heap_used += cur->size + HEADER_SIZE;
            return reinterpret_cast<u8*>(cur) + HEADER_SIZE;
        }
        prev = cur;
        cur  = cur->next;
    }
    // Out of Lua heap
    N64_LOG("Lua heap OOM: requested %zu, used %zu / %zu",
            size, s_lua_heap_used, sizeof(s_lua_heap));
    return nullptr;
}

// -----------------------------------------------------------------------
// Free a block
// -----------------------------------------------------------------------
static void lua_heap_free(void* ptr) {
    if (!ptr) return;
    LuaBlock* blk = reinterpret_cast<LuaBlock*>(
        reinterpret_cast<u8*>(ptr) - HEADER_SIZE);
    N64_ASSERT(blk->magic == MAGIC_ALLOC);
    blk->magic = MAGIC_FREE;
    s_lua_heap_used -= blk->size + HEADER_SIZE;
    // Insert at front of free list (LIFO — simple, tends to reuse hot memory)
    blk->next  = s_free_list;
    s_free_list = blk;
    // Note: no coalescing; acceptable for Lua's allocation patterns
}

// -----------------------------------------------------------------------
// n64_lua_alloc — matches lua_Alloc contract
// -----------------------------------------------------------------------
extern "C" void* n64_lua_alloc(void* /*ud*/, void* ptr, size_t osize, size_t nsize) {
    (void)osize;
    if (nsize == 0) {
        lua_heap_free(ptr);
        return nullptr;
    }
    if (!ptr) {
        return lua_heap_alloc(nsize);
    }
    // Realloc: alloc new, copy, free old
    void* newptr = lua_heap_alloc(nsize);
    if (!newptr) return nullptr;
    std::memcpy(newptr, ptr, osize < nsize ? osize : nsize);
    lua_heap_free(ptr);
    return newptr;
}
