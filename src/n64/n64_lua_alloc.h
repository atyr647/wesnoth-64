/*
 * n64_lua_alloc.h
 *
 * Custom Lua allocator for N64.
 *
 * Routes all Lua memory allocation through a dedicated fixed-size heap
 * backed by N64_POOL_LUA (512 KB). Uses a simple first-fit free list.
 *
 * This header is included by luaconf.h via LUA_USER_H.
 * The actual implementation is in n64_lua_alloc.cpp.
 */

#ifndef N64_LUA_ALLOC_H
#define N64_LUA_ALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Allocator function matching lua_Alloc signature */
void* n64_lua_alloc(void* ud, void* ptr, size_t osize, size_t nsize);

/* Install this allocator when creating a Lua state:
 *   lua_State* L = lua_newstate(n64_lua_alloc, nullptr);
 */

/* Override lua_newstate to always use our allocator */
#ifdef lua_newstate
#undef lua_newstate
#endif
/* Do NOT redefine lua_newstate here — it's done in game_lua_kernel.cpp
 * where the Lua state is created. */

#ifdef __cplusplus
}
#endif

#endif /* N64_LUA_ALLOC_H */
