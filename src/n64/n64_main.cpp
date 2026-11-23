/**
 * n64_main.cpp
 *
 * Nintendo 64 entry point for Wesnoth.
 *
 * Initialises the N64 platform layer (memory pools, ROM filesystem, video,
 * audio, input; Expansion Pak check), then calls Wesnoth's own main() from
 * src/wesnoth.cpp, which runs the unmodified start-up and game loop.
 */

#include "n64_platform.hpp"
#include "n64_memory.hpp"
#include "n64_audio.hpp"
#include "n64_romfs.hpp"
#include "n64_vfs.hpp"
#include "netlib_adapter.hpp"

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
// paged code and the swappable heap. 240 frames = 3.75 MiB of RAM.
extern "C" uint32_t n64_pager_frames_requested = 240;

#  ifdef N64_PAGED
extern "C" void* n64_vmalloc(size_t);
extern "C" void* n64_vrealloc(void*, size_t);
extern "C" void  n64_vfree(void*);
static void* n64_vcalloc(size_t n, size_t size) {
    const size_t bytes = n * size;
    if (size && bytes / size != n) return nullptr;
    void* p = n64_vmalloc(bytes);
    if (p) std::memset(p, 0, bytes);
    return p;
}
#  endif

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
    debug_init_usblog();         // USB log buffer

    N64_LOG("Wesnoth N64 starting up...");
    check_expansion_pak();

    // --- Phase 1: Memory pools ---
    n64_memory_init();

    // --- Phase 2: game data ---
    // The SummerCart64's SD card holds the unmodified data tree (directory
    // "wesnoth") and the user data ("wesnoth-userdata"); without a card the
    // cartridge ROM's data tree is used and user data lives in RAM.
    const bool rom_ok = n64_romfs_init();
    if (n64vfs::init()) {
        N64_LOG("Game data: SD card (sd:/wesnoth), user data: sd:/wesnoth-userdata");
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
    // Until the RDP renderer lands, SDL's software renderer is the only one.
    // The engine asks for an accelerated renderer; name the driver instead.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    // --- Phase 4: Audio ---
    n64_audio_init();

    // --- Phase 6: hand over to Wesnoth's own main() (src/wesnoth.cpp) ---
    // Everything from here on is the unmodified engine start-up path:
    // command line, game_launcher, fonts, language, video, gui2::init, the
    // loading screen with game_config_manager::init_game_config, the plugin
    // Lua kernel and the title-screen loop. wesnoth.cpp is compiled with
    // -Dmain=wesnoth_main for N64 (see CMakeLists.txt).
    // The data tree is the cartridge (/rom) and user data lives in /save (see
    // n64_vfs.hpp). --nocache: the config cache stores gzip'd copies of the
    // parsed WML, for which the console has neither space nor a codec.
    static char a0[] = "wesnoth", a1[] = "--data-dir", a2[] = "/rom",
                a3[] = "--userdata-dir", a4[] = "/save", a5[] = "--nocache";
    static char* argv[] = { a0, a1, a2, a3, a4, a5, nullptr };
    N64_LOG("Entering Wesnoth main()");
    const int rc = wesnoth_main(6, argv);

    // --- Shutdown ---
    n64_audio_shutdown();
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
