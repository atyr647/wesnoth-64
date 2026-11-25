/*
 * n64_throwlog.cpp -- log every C++ throw on the debug channel (resident).
 *
 * Wraps __cxa_throw (linked with --wrap=__cxa_throw): prints the thrown type
 * and, for std::exception-derived objects, what(). The engine catches and
 * reports most of its exceptions itself; this makes the ones that end the
 * program, or that the engine swallows, visible on the IS-Viewer log.
 */
#include <cstdint>
#include <cstdio>
#include <exception>
#include <typeinfo>

extern "C" void __real___cxa_throw(void* obj, std::type_info* tinfo, void (*dest)(void*));

extern "C" void __wrap___cxa_throw(void* obj, std::type_info* tinfo, void (*dest)(void*))
{
    const char* what = nullptr;
    void* adjusted = obj;
    // Upcast the thrown object to std::exception if its type derives from it
    // (the same check a catch (const std::exception&) clause performs).
    if (tinfo && typeid(std::exception).__do_catch(tinfo, &adjusted, 1)) {
        what = static_cast<std::exception*>(adjusted)->what();
    }
    std::fprintf(stderr, "[n64] C++ throw: %s%s%s\n", tinfo ? tinfo->name() : "?",
                 what ? ": " : "", what ? what : "");
    // Where from: libdragon's backtrace cannot walk the paged code, so list
    // the words on the stack that look like return addresses into it
    // (symbolize with addr2line; the first ones are the innermost callers).
    {
        const uint32_t* sp = static_cast<const uint32_t*>(__builtin_frame_address(0));
        char line[200];
        int n = std::snprintf(line, sizeof line, "[n64]   stack:");
        int found = 0;
        for (int i = 0; i < 256 && found < 8; ++i) {
            const uint32_t v = sp[i * 2 + 1];      // o64 saves registers as 64-bit words
            if (v >= 0xC0000000u && v < 0xC4000000u && (v & 3) == 0) {
                n += std::snprintf(line + n, sizeof line - n, " %08lx", (unsigned long)v);
                ++found;
            }
        }
        std::fprintf(stderr, "%s\n", line);
    }
    __real___cxa_throw(obj, tinfo, dest);
}
