/**
 * n64_main.cpp
 *
 * Nintendo 64 entry point for Wesnoth.
 *
 * Replaces src/wesnoth.cpp's main() for N64 targets.
 * On N64 the CRT calls main() after hardware init; from there we:
 *
 *   1. Initialise all N64 subsystems (memory, video, audio, input, ROM FS)
 *   2. Detect Expansion Pak (require 8 MB)
 *   3. Load the ROM filesystem directory
 *   4. Initialise Wesnoth's own systems (config, game data, Lua kernel)
 *   5. Run the game loop (title screen → campaign select → gameplay)
 *
 * The multiplayer Client App connection is established lazily when the
 * player selects "Online Multiplayer" from the main menu.
 */

#include "n64_platform.hpp"
#include "n64_memory.hpp"
#include "n64_video.hpp"
#include "n64_audio.hpp"
#include "n64_input.hpp"
#include "n64_romfs.hpp"
#include "netlib_adapter.hpp"

#ifdef N64_BUILD
#  include <libdragon.h>
#  include <n64sys.h>

// Wesnoth core systems (same headers as PC build)
#  include "game_config.hpp"
#  include "game_config_manager.hpp"
#  include "game_launcher.hpp"
#  include "log.hpp"
#  include "preferences/preferences.hpp"
#  include "serialization/schema_validator.hpp"

// -----------------------------------------------------------------------
// Expansion Pak check — Wesnoth N64 requires 8 MB
// -----------------------------------------------------------------------
static void check_expansion_pak() {
    // osGetMemSize() returns either 0x400000 (4 MB) or 0x800000 (8 MB)
    u32 mem_size = osGetMemSize();
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
int main() {
    // --- Phase 0: Hardware init ---
    debug_init_isviewer();       // USB debug output (ISViewer/64drive)
    debug_init_usblog();         // USB log buffer

    N64_LOG("Wesnoth N64 starting up...");
    check_expansion_pak();

    // --- Phase 1: Memory pools ---
    n64_memory_init();

    // --- Phase 2: ROM filesystem ---
    if (!n64_romfs_init()) {
        N64_LOG("FATAL: ROM filesystem corrupt");
        while(1) {}
    }

    // --- Phase 3: Video ---
    n64_video_init();

    // --- Phase 4: Audio ---
    n64_audio_init();

    // --- Phase 5: Input ---
    n64_input_init();

    // --- Phase 6: Wesnoth config / prefs ---
    // On N64, preferences are read from cartridge SRAM (if available)
    // and game data is loaded from ROM instead of filesystem.
    // game_config_manager reads WML; on N64 this reads binary WML from ROM.
    lg::set_log_domain_severity("all", lg::err());  // suppress verbose logging

    // Load game data from ROM filesystem
    // The game_config_manager is initialised with a custom N64 filesystem
    // backend (src/n64/n64_filesystem_backend.cpp) instead of boost::filesystem
    game_config_manager::get()->load_game_config_for_game();

    // --- Phase 7: Title screen and main menu loop ---
    // game_launcher handles the entire title → campaign select → gameplay
    // loop. On N64 it is configured to skip the add-on manager, map editor,
    // and multiplayer server browser (unless USB NetLib is detected).
    game_launcher launcher;

    // Check for NetLib connection (optional — only for online MP)
    bool has_network = n64_netlib_init();
    N64_LOG("NetLib: %s", has_network ? "connected" : "not connected (single-player only)");

    // Main game loop
    while (launcher.play_game()) {
        // n64_audio_update() keeps the music streaming each frame
        // The game loop inside play_game() calls n64_input_poll() and
        // n64_video_flip() via the SDL shim.
    }

    // --- Shutdown ---
    n64_audio_shutdown();
    N64_LOG("Wesnoth N64 exiting.");
    return 0;
}

#else // !N64_BUILD — host build entry point for testing

int main(int argc, char** argv) {
    N64_LOG("Wesnoth N64 host stub (not a real N64 build)");
    N64_LOG("Use cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/n64-libdragon.cmake to build for N64");
    (void)argc; (void)argv;
    return 0;
}

#endif // N64_BUILD
