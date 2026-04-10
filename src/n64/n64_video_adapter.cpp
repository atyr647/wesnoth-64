/**
 * n64_video_adapter.cpp
 *
 * Drop-in replacement for src/video.cpp on N64.
 *
 * Wesnoth's video namespace provides window/renderer management and
 * coordinate space conversions. On N64 we hard-code 320x240 and route
 * all SDL renderer calls through n64_video.
 *
 * This file is compiled INSTEAD of src/video.cpp when N64_BUILD is defined.
 * It implements every function declared in src/video.hpp.
 */

#ifdef N64_BUILD

#include "../video.hpp"
#include "../sdl/surface.hpp"
#include "../sdl/texture.hpp"
#include "n64_video.hpp"
#include "n64_platform.hpp"

namespace video {

// -----------------------------------------------------------------------
// State
// -----------------------------------------------------------------------
static bool s_initialized = false;
static bool s_headless = false;

// Coordinate spaces: on N64, all three are the same (320x240, no scaling)
static point s_resolution = {N64_SCREEN_W, N64_SCREEN_H};

// -----------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------
void init(fake f) {
    s_headless = (f != fake::none);
    if (!s_headless) {
        n64_video_init();
    }
    s_initialized = true;
}

void deinit() {
    s_initialized = false;
}

void update_buffers(bool) {
    // No-op on N64 (single fixed resolution)
}

// -----------------------------------------------------------------------
// State queries
// -----------------------------------------------------------------------
bool headless()           { return s_headless; }
bool testing()            { return false; }
bool has_window()         { return !s_headless; }
bool is_fullscreen()      { return true; } // N64 is always fullscreen
bool window_is_visible()  { return true; }
bool window_has_focus()   { return true; }
bool window_has_mouse_focus() { return true; }

void set_fullscreen(bool) {} // no-op
void toggle_fullscreen()  {} // no-op

bool set_resolution(point) { return false; } // fixed resolution
point current_resolution() { return s_resolution; }

std::vector<point> get_available_resolutions() {
    return { s_resolution };
}

void set_window_title(const std::string&) {}
void set_window_icon(surface&) {}

// -----------------------------------------------------------------------
// Coordinate spaces — all identical on N64
// -----------------------------------------------------------------------
rect game_canvas()      { return {0, 0, N64_SCREEN_W, N64_SCREEN_H}; }
point game_canvas_size() { return s_resolution; }
point draw_size()        { return s_resolution; }
rect draw_area()         { return {0, 0, N64_SCREEN_W, N64_SCREEN_H}; }
point output_size()      { return s_resolution; }
rect output_area()       { return {0, 0, N64_SCREEN_W, N64_SCREEN_H}; }
point window_size()      { return s_resolution; }
rect input_area()        { return {0, 0, N64_SCREEN_W, N64_SCREEN_H}; }
int get_pixel_scale()    { return 1; }
int get_max_pixel_scale() { return 1; }
point to_output(const point& p) { return p; }

// -----------------------------------------------------------------------
// Screen capture
// -----------------------------------------------------------------------
surface read_pixels(const rect*) { return surface(); }
surface read_pixels_low_res(const rect*) { return surface(); }

// -----------------------------------------------------------------------
// Render targets
// -----------------------------------------------------------------------
void force_render_target(const texture&) {}
void clear_render_target()  {}
void reset_render_target()  {}
texture get_render_target()  { return texture(); }

// -----------------------------------------------------------------------
// Renderer access — return our SDL shim singletons
// -----------------------------------------------------------------------
SDL_Renderer* get_renderer() { return &g_n64_renderer; }
SDL_Window*   get_window()   { return &g_n64_window; }

} // namespace video

#endif // N64_BUILD
