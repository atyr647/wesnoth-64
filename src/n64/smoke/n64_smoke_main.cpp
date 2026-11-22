/**
 * n64_smoke_main.cpp
 *
 * Platform-layer smoke test. Boots with the same linker script, ROMFS blob and
 * runtime code as the full game (n64_romfs.cpp, n64_memory.cpp, n64_video.cpp)
 * but none of the Wesnoth engine, so it fits in RAM and can be screenshotted
 * on an emulator:
 *
 *   - ROMFS located through the ROM table of contents and its hashed
 *     directory searched by name;
 *   - textures DMA'd out of the ROMFS and drawn by the RDP;
 *   - the pre-rasterised glyph sheet used for text.
 *
 * Screen colour on failure: RED = ROMFS did not initialise.
 */

#include "../n64_platform.hpp"
#include "../n64_memory.hpp"
#include "../n64_romfs.hpp"
#include "../n64_video.hpp"

#include <stdio.h>

static const char* const kTiles[] = {
    "images/terrain/grass/green.png",
    "images/terrain/grass/green2.png",
    "images/terrain/grass/green3.png",
    "images/terrain/hills/regular.png",
    "images/terrain/water/ocean-tile.png",
    "images/terrain/grass/green4.png",
};
static constexpr int kTileCount = sizeof(kTiles) / sizeof(kTiles[0]);

static void fail_screen() {
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    for (;;) {
        surface_t* d = display_get();
        graphics_fill_screen(d, 0xF800F800u);   // red
        display_show(d);
    }
}

int main() {
    debug_init_isviewer();
    debug_init_usblog();

    n64_memory_init();
    if (!n64_romfs_init())
        fail_screen();

    n64_video_init();

    // Load the tiles once.
    N64TexHandle tiles[kTileCount];
    int loaded = 0;
    for (int i = 0; i < kTileCount; ++i) {
        const RomFsEntry* e = n64_romfs_find(kTiles[i]);
        if (e) {
            tiles[i] = n64_tex_load(e->rom_offset, e->width, e->height, true);
            ++loaded;
        } else {
            tiles[i] = N64_TEX_INVALID;
        }
    }
    N64_LOG("smoke: %d/%d tiles found, %u ROMFS entries", loaded, kTileCount,
            (unsigned)n64_romfs_entry_count());

    char line[64];
    for (unsigned frame = 0;; ++frame) {
        n64_video_clear(N64Color::from_rgb(20, 24, 48));

        // A staggered field of real terrain textures.
        for (int row = 0; row < 5; ++row) {
            for (int col = 0; col < 11; ++col) {
                N64TexHandle t = tiles[(row * 3 + col * 2) % kTileCount];
                if (t == N64_TEX_INVALID) continue;
                s16 x = 8 + col * 27;
                s16 y = 78 + row * 28 + (col & 1) * 14;
                n64_draw_texture(LAYER_TERRAIN, t, x, y);
            }
        }

        n64_draw_rect(LAYER_UI, {6, 6, 308, 62}, N64Color::from_rgb(0, 0, 0));
        n64_draw_rect_outline(LAYER_UI, {6, 6, 308, 62}, N64Color::from_rgb(220, 190, 80));

        n64_draw_text(16, 14, "BATTLE FOR WESNOTH", N64Color::from_rgb(255, 224, 96));
        n64_draw_text(16, 26, "N64 PORT - PLATFORM SMOKE TEST", N64Color::white());
        snprintf(line, sizeof(line), "ROMFS ENTRIES: %u", (unsigned)n64_romfs_entry_count());
        n64_draw_text(16, 40, line, N64Color::from_rgb(160, 255, 160));
        snprintf(line, sizeof(line), "TILES LOADED: %d/%d  FRAME: %u", loaded, kTileCount, frame);
        n64_draw_text(16, 52, line, N64Color::from_rgb(160, 200, 255));

        // Moving marker so a screenshot proves the frame loop is live.
        n64_draw_rect(LAYER_EFFECTS, {(s16)(10 + (frame * 3) % 290), 226, 12, 6},
                      N64Color::from_rgb(255, 80, 80));

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer((N64Layer)l);
        n64_video_flip();
    }
}
