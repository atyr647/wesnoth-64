/*
 * n64_lua_config.h
 *
 * Lua 5.4 configuration overrides for the N64 port.
 * Included via -include or directly by luaconf.h on N64.
 *
 * Key goals:
 *   - Reduce memory footprint (smaller stack, fewer initial tables)
 *   - Remove OS/file I/O dependencies (no stdio, no filesystem)
 *   - Use longjmp for error recovery (N64 has no C++ exceptions)
 *   - Fit the Lua heap within N64_POOL_LUA (512 KB)
 */

#ifndef N64_LUA_CONFIG_H
#define N64_LUA_CONFIG_H

/* Use a custom allocator backed by g_arena_game */
#define LUA_USER_H "n64_lua_alloc.h"

/* Maximum Lua stack size (default 15000; N64 uses 1024) */
#undef  LUAI_MAXSTACK
#define LUAI_MAXSTACK 1024

/* Maximum C stack depth (default 2000; N64 uses 200) */
#undef  LUAI_MAXCSTACK
#define LUAI_MAXCSTACK 200

/* No OS calls */
#undef  LUA_USE_POSIX
#undef  LUA_USE_DLOPEN

/* Use longjmp for error handling (avoids C++ exception overhead) */
#define LUA_USE_LONGJMP 1

/* Number type: use double (VR4300 has a hardware FPU) */
#define LUA_NUMBER  double
#define LUA_INTEGER long long

/* Disable dynamic library loading */
#define LUA_NOENV 1

/* Path and C path: unused on N64 (all Lua loaded from ROM) */
#undef  LUA_PATH_DEFAULT
#define LUA_PATH_DEFAULT ""
#undef  LUA_CPATH_DEFAULT
#define LUA_CPATH_DEFAULT ""

/* Reduce initial table sizes to save memory */
#undef  LUAI_NUMTYPES
/* Keep default */

/* Disable UTF-8 library (not needed for game scripting) */
/* utf8lib.c is still compiled but never registered if we skip it in linit.c */

#endif /* N64_LUA_CONFIG_H */
