/*
 * n64_throwlog.cpp -- log every C++ throw on the debug channel (resident).
 *
 * Wraps __cxa_throw (linked with --wrap=__cxa_throw): prints the thrown type
 * and, for std::exception-derived objects, what(). The engine catches and
 * reports most of its exceptions itself; this makes the ones that end the
 * program, or that the engine swallows, visible on the IS-Viewer log.
 */
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
    __real___cxa_throw(obj, tinfo, dest);
}
