/**
 * n64_main.cpp
 *
 * Nintendo 64 entry point for Wesnoth.
 *
 * Initialises the N64 platform layer (game data on the SD card or ROM, SDL
 * memory and drivers; Expansion Pak check), then calls Wesnoth's own main() from
 * src/wesnoth.cpp, which runs the unmodified start-up and game loop.
 */

#include "n64_platform.hpp"
#include "n64_romfs.hpp"
#include "n64_vfs.hpp"
#  ifdef N64_PAGED
#    include "pager/n64_pager.h"
#  endif
#include "netlib_adapter.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef N64_BUILD
#  include <libdragon.h>
#  include <n64sys.h>

// Wesnoth core systems (same headers as PC build)
#  include "game_config.hpp"
#  include "game_config_manager.hpp"
#  include "game_launcher.hpp"
#  include "game_classification.hpp"
#  include "commandline_options.hpp"
#  include "log.hpp"
#  include "preferences/preferences.hpp"
#  include "serialization/schema_validator.hpp"

// Frame pool for the demand pager (src/n64/pager): 16 KiB frames shared by
// paged code and the swappable heap. 208 frames = 3.25 MiB of RAM; the rest
// of physical memory goes to the RDP renderer's texture cache.
extern "C" uint32_t n64_pager_frames_requested = 208;

#  ifdef N64_PAGED
extern "C" void* n64_vmalloc(size_t);
extern "C" void* n64_vrealloc(void*, size_t);
extern "C" void  n64_vfree(void*);
extern "C" void* n64_vcalloc(size_t, size_t);
extern "C" void n64_vheap_mark(const char* what, int stage);
#  endif

// C++ exception self-test: a throw through a frame with a destructor (cleanup
// landing pad + _Unwind_Resume) into a catch clause. Logs how many exception
// pointers reached the runtime zero-extended (see pager/n64_eh_sext.S).
extern "C" unsigned n64_eh_zero_extended;
namespace {
struct eh_probe { int* n; ~eh_probe() { ++*n; } };
[[gnu::noinline]] void eh_throw(int v) { if (v >= 0) throw std::runtime_error("probe"); }
[[gnu::noinline]] void eh_cleanup(int* n, int v) { eh_probe p{ n }; eh_throw(v); }
void eh_selftest() {
    int dtors = 0;
    bool caught = false;
    try { eh_cleanup(&dtors, 1); } catch (const std::exception& e) { caught = std::strcmp(e.what(), "probe") == 0; }
    N64_LOG("C++ exceptions: %s (destructors run %d, zero-extended pointers fixed %u)",
            caught && dtors == 1 ? "ok" : "BROKEN", dtors, n64_eh_zero_extended);
}
} // namespace

// std::this_thread::sleep_for (n64_threading_shim.hpp).
extern "C" void n64_sleep_us(unsigned long long us) {
    SDL_Delay(static_cast<Uint32>((us + 999) / 1000));
}

// std::chrono::steady_clock::now(), linked with
// --wrap=_ZNSt6chrono3_V212steady_clock3nowEv. libstdc++ builds it on
// gettimeofday(), whose epoch is 1970 (n64_libgcc.c): nanoseconds since then
// are about 1.8e18, and converting such a difference to double (cvt.d.l)
// raises an "unimplemented operation" exception on the VR4300, which only
// converts integers below 2^53 in hardware. The steady clock's epoch is
// unspecified by the standard; here it is power-on.
extern "C" uint64_t get_ticks_us(void);
std::chrono::steady_clock::time_point n64_steady_clock_now()
    asm("__wrap__ZNSt6chrono3_V212steady_clock3nowEv");
std::chrono::steady_clock::time_point n64_steady_clock_now() {
    return std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(static_cast<long long>(get_ticks_us()) * 1000));
}

// Wesnoth's real entry point (src/wesnoth.cpp, renamed for N64).
int wesnoth_main(int argc, char** argv);

// -----------------------------------------------------------------------
// Expansion Pak check — Wesnoth N64 requires 8 MB
// -----------------------------------------------------------------------
static void check_expansion_pak() {
    // get_memory_size() returns either 0x400000 (4 MB) or 0x800000 (8 MB)
    int mem_size = get_memory_size();
    if (mem_size < 0x800000) {
        // Draw an error screen and halt
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, 1, GAMMA_NONE, ANTIALIAS_OFF);
        display_context_t dc = display_get();
        graphics_draw_text(dc, 40, 100, "Wesnoth N64 requires");
        graphics_draw_text(dc, 40, 116, "the Expansion Pak!");
        graphics_draw_text(dc, 40, 140, "Please insert the");
        graphics_draw_text(dc, 40, 156, "Expansion Pak and");
        graphics_draw_text(dc, 40, 172, "restart the console.");
        display_show(dc);
        while (1) {} // halt
    }
}

// -----------------------------------------------------------------------
// N64 main — execution begins here
// -----------------------------------------------------------------------
#ifdef N64_PAGED
extern "C" int n64_paged_main() {   // entered from the resident pager's main()
#else
int main() {
#endif
    // --- Phase 0: Hardware init ---
    debug_init_isviewer();       // USB debug output (ISViewer/64drive)
#ifndef N64_NO_USB_LOG
    // Uses the top 8 MiB of cartridge SDRAM (0x13800000); the pager's swap
    // stops below it (n64_pager.c SWAP_CART_END).
    debug_init_usblog();         // USB log buffer
#endif

    N64_LOG("Wesnoth N64 starting up...");
    check_expansion_pak();

    // --- Phase 2: game data ---
    // The SummerCart64's SD card holds the unmodified data tree (directory
    // "wesnoth") and the user data ("wesnoth-userdata"); without a card the
    // cartridge ROM's data tree is used and user data lives in RAM.
    const bool rom_ok = n64_romfs_init();
    if (n64vfs::init()) {
        N64_LOG("Game data: SD card (sd:/wesnoth), user data: sd:/wesnoth-userdata");
#  if defined(N64_PAGED) && defined(N64_SD_SWAP)
        // Optional (off by default): let the heap spill into a swap file on
        // the SD card when it outgrows the cartridge SDRAM.
        if (!n64_pager_attach_sd_swap("/wesnoth-userdata/swap.bin"))
            N64_LOG("No SD swap file: the heap is limited to the cartridge SDRAM");
#  endif
    } else if (rom_ok) {
        N64_LOG("Game data: cartridge ROM (no SD card with a 'wesnoth' directory)");
    } else {
        N64_LOG("FATAL: no game data (no SD card 'wesnoth' directory, no ROM filesystem)");
        while(1) {}
    }

    // --- Phase 3: SDL ---
    // Video and controller input belong to SDL's N64 driver (src/n64/sdl2),
    // set up when the engine initialises SDL. SDL's own allocations
    // (surfaces, textures, decoded images) go to the swappable heap.
#  ifdef N64_PAGED
    SDL_SetMemoryFunctions(n64_vmalloc, n64_vcalloc, n64_vrealloc, n64_vfree);
#  endif

    // --- Phase 6: hand over to Wesnoth's own main() (src/wesnoth.cpp) ---
    // Everything from here on is the unmodified engine start-up path:
    // command line, game_launcher, fonts, language, video, gui2::init, the
    // loading screen with game_config_manager::init_game_config, the plugin
    // Lua kernel and the title-screen loop. wesnoth.cpp is compiled with
    // -Dmain=wesnoth_main for N64 (see CMakeLists.txt).
    // The data tree is the cartridge (/rom) and user data lives in /save (see
    // n64_vfs.hpp). --nocache: the config cache stores gzip'd copies of the
    // parsed WML, for which the console has neither space nor a codec.
    eh_selftest();
    // Extra command-line options (e.g. --log-debug=...) can be put in
    // args.txt in the user data directory, one per line.
    static std::vector<std::string> args = { "wesnoth", "--data-dir", "/rom",
                                             "--userdata-dir", "/save", "--nocache" };
    {
        std::string extra;
        if (n64vfs::read("/save/args.txt", extra)) {
            size_t i = 0;
            while (i < extra.size()) {
                size_t j = extra.find('\n', i);
                if (j == std::string::npos) j = extra.size();
                std::string a = extra.substr(i, j - i);
                while (!a.empty() && (a.back() == '\r' || a.back() == ' ')) a.pop_back();
                if (a == "--n64-watchdog") {
#  ifdef N64_PAGED
                    n64_pager_start_watchdog();
#  endif
                } else if (!a.empty() && a[0] != '#') {
                    N64_LOG("extra argument: %s", a.c_str());
                    args.push_back(a);
                }
                i = j + 1;
            }
        }
    }
    static std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    n64_vheap_mark("before wesnoth_main", 0);
    N64_LOG("Entering Wesnoth main()");
    const int rc = wesnoth_main((int)args.size(), argv.data());

    // --- Shutdown ---
    N64_LOG("Wesnoth N64 exiting (%d).", rc);
    return rc;
}

#else // !N64_BUILD — host build entry point for testing

int main(int argc, char** argv) {
    N64_LOG("Wesnoth N64 host stub (not a real N64 build)");
    N64_LOG("Use cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/n64-libdragon.cmake to build for N64");
    (void)argc; (void)argv;
    return 0;
}

#endif // N64_BUILD
