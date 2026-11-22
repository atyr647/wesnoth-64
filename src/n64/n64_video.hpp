#pragma once

/**
 * n64_video.hpp
 *
 * N64 rendering abstraction for Wesnoth.
 *
 * Replaces src/sdl/window.hpp, src/sdl/texture.hpp, src/draw.cpp, and
 * the top-level src/display.cpp SDL rendering calls.
 *
 * Architecture:
 *   - Double-buffered 320x240 16-bit (RGBA5551) framebuffer via libdragon VI
 *   - Textures live in a RAM texture cache (N64_POOL_TEXTURE_CACHE = 1 MB)
 *     and are swapped into TMEM (4 KB on-chip) for drawing
 *   - All hex tile drawing goes through n64_draw_tile()
 *   - UI text rendered via pre-rasterised bitmap glyph sheet
 *   - Drawing layers collapsed from 21 (PC) to 6 (N64):
 *       0  LAYER_BG         terrain background
 *       1  LAYER_TERRAIN    terrain foreground
 *       2  LAYER_UNITS      units (back to front by y-position)
 *       3  LAYER_EFFECTS    animations, fog, shroud, arrows
 *       4  LAYER_UI         HUD panels, minimap
 *       5  LAYER_TEXT       all text (drawn last, always on top)
 */

#include "n64_platform.hpp"

// -----------------------------------------------------------------------
// Colour helpers (RGBA5551, the N64's native 16-bit format)
// -----------------------------------------------------------------------
struct N64Color {
    u16 v; // packed RGBA5551

    static constexpr N64Color from_rgb(u8 r, u8 g, u8 b) {
        return { static_cast<u16>(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1u) };
    }
    static constexpr N64Color from_rgba(u8 r, u8 g, u8 b, u8 a) {
        // Alpha: 0 = transparent, 255 = opaque (stored as 1-bit in RGBA5551)
        return { static_cast<u16>(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >= 128 ? 1u : 0u)) };
    }
    static constexpr N64Color transparent() { return { 0 }; }
    static constexpr N64Color white()       { return from_rgb(255, 255, 255); }
    static constexpr N64Color black()       { return from_rgb(0,   0,   0  ); }
};

// -----------------------------------------------------------------------
// Texture handle
//
// Textures are stored in the RAM texture cache as raw RGBA5551 data.
// A handle is just an index into the cache; 0xFFFF = invalid.
// -----------------------------------------------------------------------
using N64TexHandle = u16;
static constexpr N64TexHandle N64_TEX_INVALID = 0xFFFF;

struct N64Texture {
    u16*  data;       // pointer into texture cache RAM
    u16   width;
    u16   height;
    bool  has_alpha;  // whether any pixel has alpha=0
};

// -----------------------------------------------------------------------
// Drawing layers
// -----------------------------------------------------------------------
enum N64Layer : u8 {
    LAYER_BG      = 0,
    LAYER_TERRAIN = 1,
    LAYER_UNITS   = 2,
    LAYER_EFFECTS = 3,
    LAYER_UI      = 4,
    LAYER_TEXT    = 5,
    LAYER_COUNT   = 6,
};

// -----------------------------------------------------------------------
// Rectangle / point helpers
// -----------------------------------------------------------------------
struct N64Rect { s16 x, y, w, h; };
struct N64Point { s16 x, y; };

// -----------------------------------------------------------------------
// Video subsystem API
// -----------------------------------------------------------------------

// Initialise VI, allocate framebuffers, initialise texture cache.
// Call once from n64_main() after n64_memory_init().
void n64_video_init();

// Swap framebuffers and wait for vsync (call at end of each frame).
void n64_video_flip();

// Clear the current backbuffer to colour c.
void n64_video_clear(N64Color c);

// --- Texture cache management -------------------------------------------

// Load a texture from ROM (offset into the ROM filesystem).
// Returns a handle valid until n64_texcache_flush() is called.
N64TexHandle n64_tex_load(u32 rom_offset, u16 w, u16 h, bool has_alpha);

// Load a texture from a ROMFS entry, decompressing it if stored LZ4.
struct RomFsEntry;
N64TexHandle n64_tex_load_entry(const RomFsEntry* entry, bool has_alpha);

// Release all cached textures (call between scenarios).
void n64_texcache_flush();

// --- Primitive drawing --------------------------------------------------

// Draw a texture at (x, y) in screen space.
void n64_draw_texture(N64Layer layer, N64TexHandle tex, s16 x, s16 y);

// Draw a sub-region (src_rect) of a texture atlas at (dst_x, dst_y).
void n64_draw_texture_region(N64Layer layer, N64TexHandle atlas,
                             N64Rect src, s16 dst_x, s16 dst_y);

// Draw a solid colour rectangle.
void n64_draw_rect(N64Layer layer, N64Rect r, N64Color c);

// Draw a horizontal line (for grid overlay).
void n64_draw_hline(N64Layer layer, s16 x1, s16 x2, s16 y, N64Color c);

// Draw an outlined rectangle.
void n64_draw_rect_outline(N64Layer layer, N64Rect r, N64Color c);

// --- Hex tile drawing ---------------------------------------------------

// Draw one hex tile at hex grid position (hex_x, hex_y).
// 'tex' is the terrain/unit texture. Handles the offset stagger pattern.
void n64_draw_hex_tile(N64Layer layer, N64TexHandle tex, s16 hex_x, s16 hex_y);

// Convert hex grid coordinates to screen pixel coordinates (top-left of tile).
N64Point n64_hex_to_screen(s16 hex_x, s16 hex_y, s16 scroll_x, s16 scroll_y);

// --- Text drawing -------------------------------------------------------

// Draw null-terminated ASCII string at (x, y) in the given colour.
// Uses the pre-rasterised glyph sheet baked into ROM.
void n64_draw_text(s16 x, s16 y, const char* text, N64Color colour);

// Draw text with a 1-pixel black shadow (improves readability on tiles).
void n64_draw_text_shadowed(s16 x, s16 y, const char* text, N64Color colour);

// Measure string width in pixels (for centering).
s16  n64_text_width(const char* text);

// --- Layer flush --------------------------------------------------------

// Commit all queued draw calls for 'layer' to the backbuffer.
// Layers must be flushed in order 0..LAYER_COUNT-1.
void n64_flush_layer(N64Layer layer);
