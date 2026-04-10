/*
 * n64_lua_stubs.c
 *
 * On N64, loslib.c (luaopen_os) and loadlib.c (luaopen_package) are
 * excluded from the build because they pull in fopen/dlopen/system
 * which are not available. Lua's linit.c however still references
 * them by name. Provide empty module stubs so the linker is happy.
 *
 * Each stub pushes an empty table and returns 1, behaving as a Lua
 * module that exports nothing.
 */

#define LUA_LIB
#include "lua.h"
#include "lauxlib.h"

LUAMOD_API int luaopen_os(lua_State* L) {
    lua_createtable(L, 0, 0);
    return 1;
}

LUAMOD_API int luaopen_package(lua_State* L) {
    lua_createtable(L, 0, 0);
    return 1;
}
