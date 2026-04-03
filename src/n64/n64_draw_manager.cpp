/**
 * n64_draw_manager.cpp
 *
 * Drop-in replacement for src/draw_manager.cpp on N64.
 *
 * The draw_manager coordinates all top-level drawables (TLDs) and calls
 * their layout(), expose(), and render() methods each frame.
 *
 * On N64, the rendering pipeline is simplified:
 *   1. Call layout() on all registered drawables
 *   2. Call expose() (draw) on all registered drawables
 *   3. Flush N64 draw layers in order
 *   4. Flip framebuffers (n64_video_flip)
 *
 * Frame timing targets 30 FPS (33ms per frame). Since Wesnoth is turn-based,
 * occasional frame drops are acceptable.
 */

#ifdef N64_BUILD

#include "../draw_manager.hpp"
#include "n64_video.hpp"
#include "n64_audio.hpp"
#include "n64_input.hpp"
#include "n64_platform.hpp"

#include <vector>
#include <algorithm>

namespace {

// Registered top-level drawables
struct TldEntry {
    gui2::top_level_drawable* tld;
    int priority;
};

std::vector<TldEntry> s_tlds;
bool s_invalidated = true;
bool s_extra_pass  = false;

// Frame timing
static constexpr u32 TARGET_FRAME_MS = 33; // ~30 FPS
u32 s_last_frame_time = 0;
u32 s_frame_length    = TARGET_FRAME_MS;

} // anonymous namespace

namespace draw_manager {

void register_drawable(gui2::top_level_drawable* tld, int priority) {
    s_tlds.push_back({tld, priority});
    std::sort(s_tlds.begin(), s_tlds.end(),
              [](const TldEntry& a, const TldEntry& b) { return a.priority < b.priority; });
}

void deregister_drawable(gui2::top_level_drawable* tld) {
    s_tlds.erase(
        std::remove_if(s_tlds.begin(), s_tlds.end(),
                        [tld](const TldEntry& e) { return e.tld == tld; }),
        s_tlds.end());
}

void raise_drawable(gui2::top_level_drawable*) {
    // Priority-based; no re-ordering needed beyond initial sort
}

void invalidate_region(const rect&) {
    s_invalidated = true;
}

void invalidate_all() {
    s_invalidated = true;
}

void request_extra_render_pass() {
    s_extra_pass = true;
}

int get_frame_length() {
    return (int)s_frame_length;
}

/**
 * sparkle() — the main rendering entry point, called every frame.
 *
 * On PC this is a complex multi-pass system. On N64 we simplify to:
 *   layout → expose → flush layers → flip
 */
void sparkle() {
    u32 frame_start = SDL_GetTicks();

    // Update audio (keeps music streaming)
    n64_audio_update();

    // Poll input
    n64_input_poll();

    // Clear backbuffer
    n64_video_clear(N64Color::black());

    // Layout pass
    for (auto& e : s_tlds) {
        e.tld->layout();
    }

    // Draw pass
    for (auto& e : s_tlds) {
        rect r = {0, 0, N64_SCREEN_W, N64_SCREEN_H};
        e.tld->expose(r);
    }

    // Extra render pass if requested
    if (s_extra_pass) {
        s_extra_pass = false;
        for (auto& e : s_tlds) {
            rect r = {0, 0, N64_SCREEN_W, N64_SCREEN_H};
            e.tld->render();
        }
    }

    // Flush all N64 drawing layers to the backbuffer
    for (int l = 0; l < LAYER_COUNT; ++l) {
        n64_flush_layer(static_cast<N64Layer>(l));
    }

    // Swap framebuffers
    n64_video_flip();

    s_invalidated = false;

    // Frame timing
    u32 frame_end = SDL_GetTicks();
    s_frame_length = frame_end - frame_start;
    if (s_frame_length < TARGET_FRAME_MS) {
        SDL_Delay(TARGET_FRAME_MS - s_frame_length);
        s_frame_length = TARGET_FRAME_MS;
    }
    s_last_frame_time = frame_start;
}

} // namespace draw_manager

#endif // N64_BUILD
