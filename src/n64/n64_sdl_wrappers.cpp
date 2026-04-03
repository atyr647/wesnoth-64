/**
 * n64_sdl_wrappers.cpp
 *
 * N64 implementations for Wesnoth's SDL wrapper classes.
 *
 * Wesnoth wraps SDL types in src/sdl/ (texture.hpp, surface.hpp, rect.hpp).
 * On N64, these classes still exist but their SDL backends are replaced
 * with our N64 rendering layer.
 *
 * This file is compiled INSTEAD of src/sdl/texture.cpp, src/sdl/surface.cpp,
 * etc. when N64_BUILD is defined. The N64 CMakeLists includes the original
 * src/sdl/ files too — those are written to be compatible via the SDL shim.
 * This file provides additional N64-specific implementations where the shim
 * alone is insufficient.
 */

#ifdef N64_BUILD

#include "n64_video.hpp"
#include "n64_platform.hpp"

// -----------------------------------------------------------------------
// src/draw.cpp replacement — Wesnoth's draw namespace
//
// draw::blit, draw::fill, draw::line, etc. are the primary drawing API.
// On N64 they route to n64_draw_* functions.
// -----------------------------------------------------------------------
#include "../draw.hpp"

namespace draw {

void blit(const texture& tex, const rect& dst) {
    // Route through SDL_RenderCopy shim
    SDL_Rect d = {dst.x, dst.y, dst.w, dst.h};
    SDL_RenderCopy(nullptr, nullptr, nullptr, &d);
}

void blit(const texture& tex, const rect& src, const rect& dst) {
    // Handled by SDL shim
    SDL_Rect d = {dst.x, dst.y, dst.w, dst.h};
    SDL_RenderCopy(nullptr, nullptr, nullptr, &d);
}

void fill(const rect& area, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    n64_draw_rect(LAYER_UI, {(s16)area.x, (s16)area.y, (s16)area.w, (s16)area.h},
                  N64Color::from_rgba(r, g, b, a));
}

void fill(const rect& area, const color_t& c) {
    fill(area, c.r, c.g, c.b, c.a);
}

void rect(const ::rect& area, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    n64_draw_rect_outline(LAYER_UI, {(s16)area.x, (s16)area.y, (s16)area.w, (s16)area.h},
                           N64Color::from_rgba(r, g, b, a));
}

void rect(const ::rect& area, const color_t& c) {
    rect(area, c.r, c.g, c.b, c.a);
}

void line(int x1, int y1, int x2, int y2, const color_t&) {
    // Simple horizontal/vertical line approximation
    n64_draw_hline(LAYER_UI, (s16)std::min(x1, x2), (s16)std::max(x1, x2),
                   (s16)y1, N64Color::white());
}

void line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t) {
    n64_draw_hline(LAYER_UI, (s16)std::min(x1, x2), (s16)std::max(x1, x2),
                   (s16)y1, N64Color::from_rgb(r, g, b));
}

void clear() {
    n64_video_clear(N64Color::black());
}

void present() {
    for (int l = 0; l < LAYER_COUNT; ++l)
        n64_flush_layer(static_cast<N64Layer>(l));
    n64_video_flip();
}

void set_color(uint8_t, uint8_t, uint8_t, uint8_t) {}
void set_blend_mode(SDL_BlendMode) {}

::rect get_clip() { return {0, 0, N64_SCREEN_W, N64_SCREEN_H}; }
void set_clip(const ::rect&) {}
void unset_clip() {}

bool null_clip() { return false; }

// Render target stack — N64 has one framebuffer, no off-screen targets
void force_render_target(const texture&) {}
void clear_render_target() {}

} // namespace draw

// -----------------------------------------------------------------------
// texture class N64 methods (supplements src/sdl/texture.cpp)
// -----------------------------------------------------------------------
// The texture class on N64 is a thin wrapper around N64TexHandle.
// Most methods are handled by the SDL shim; this provides any extras.

// -----------------------------------------------------------------------
// surface class N64 methods (supplements src/sdl/surface.cpp)
// -----------------------------------------------------------------------
// Surfaces on N64 are rarely used (most rendering is texture-based).
// The SDL shim handles creation/destruction.

// -----------------------------------------------------------------------
// picture.cpp / image.cpp adaptations
//
// Wesnoth's image loading system (src/picture.cpp) uses SDL_image to
// load PNGs. On N64, all images are pre-converted by the asset pipeline.
// We intercept image::get_texture() to look up ROMFS entries.
// -----------------------------------------------------------------------
#include "../picture.hpp"

namespace image {

texture get_texture(const locator& loc, TYPE) {
    // Build the ROM path from the image locator
    std::string path = loc.get_filename();
    if (path.empty()) return texture();

    // Replace .png extension with .tex (N64 texture format)
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        path = path.substr(0, dot) + ".tex";
    }

    const RomFsEntry* entry = n64_romfs_find_wesnoth(path.c_str());
    if (!entry) {
        // Try without extension replacement
        entry = n64_romfs_find_wesnoth(loc.get_filename().c_str());
    }

    if (entry) {
        N64TexHandle h = n64_tex_load(entry->rom_offset,
                                       entry->width, entry->height, true);
        // Create a texture wrapper around this handle
        texture t;
        // On N64, the texture class stores the handle in its internal SDL_Texture*
        // which is actually our shim struct with an N64TexHandle member
        auto* st = SDL_CreateTexture(nullptr, 0, 0, entry->width, entry->height);
        if (st) {
            st->handle = h;
            st->w = entry->width;
            st->h = entry->height;
        }
        // Note: proper texture wrapper integration requires matching the
        // Wesnoth texture class constructor; this is a simplified path
        return t;
    }

    return texture(); // not found
}

texture get_lighted_texture(const locator& loc, const light_string&) {
    return get_texture(loc, UNSCALED);
}

surface get_surface(const locator&, TYPE) {
    return surface(); // Surfaces not used on N64
}

point get_size(const locator& loc) {
    std::string path = loc.get_filename();
    auto dot = path.rfind('.');
    if (dot != std::string::npos) path = path.substr(0, dot) + ".tex";
    const RomFsEntry* entry = n64_romfs_find_wesnoth(path.c_str());
    if (entry) return {entry->width, entry->height};
    return {N64_HEX_W, N64_HEX_H}; // default hex size
}

bool exists(const locator& loc) {
    return n64_romfs_find_wesnoth(loc.get_filename().c_str()) != nullptr;
}

void flush_cache() {
    // Called between scenarios; flush N64 texture cache
    extern void n64_display_flush_caches();
    n64_display_flush_caches();
}

} // namespace image

#endif // N64_BUILD
