/**
 * n64_events.cpp
 *
 * N64 replacement for src/events.cpp.
 *
 * Wesnoth's event system manages the SDL event loop, handler registration,
 * and per-frame pumping. On N64, events come from the controller (via
 * n64_input) and are synthesised into SDL-like events by n64_sdl_shim.hpp.
 *
 * This file provides the N64 implementation of the events namespace.
 */

#ifdef N64_BUILD

#include "../events.hpp"
#include "n64_input.hpp"
#include "n64_audio.hpp"
#include "n64_platform.hpp"

#include <vector>
#include <algorithm>

namespace events {

// -----------------------------------------------------------------------
// Handler management
// -----------------------------------------------------------------------
static std::vector<sdl_handler*> s_handlers;

sdl_handler::sdl_handler(bool) {
    s_handlers.push_back(this);
}

sdl_handler::~sdl_handler() {
    s_handlers.erase(
        std::remove(s_handlers.begin(), s_handlers.end(), this),
        s_handlers.end());
}

void sdl_handler::join() {
    if (std::find(s_handlers.begin(), s_handlers.end(), this) == s_handlers.end())
        s_handlers.push_back(this);
}

void sdl_handler::join_same(sdl_handler*) {
    join();
}

void sdl_handler::leave() {
    s_handlers.erase(
        std::remove(s_handlers.begin(), s_handlers.end(), this),
        s_handlers.end());
}

// -----------------------------------------------------------------------
// Event pump — called every frame by the game loop
// -----------------------------------------------------------------------
void pump() {
    // Poll controller input
    n64_input_poll();

    // Update audio (music streaming)
    n64_audio_update();

    // Synthesise and dispatch SDL-like events
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        // Dispatch to all registered handlers
        for (auto* h : s_handlers) {
            switch (ev.type) {
            case SDL_KEYDOWN:
                h->handle_event(ev);
                break;
            case SDL_KEYUP:
                h->handle_event(ev);
                break;
            case SDL_MOUSEMOTION:
                h->handle_event(ev);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                h->handle_event(ev);
                break;
            case SDL_USEREVENT:
                h->handle_event(ev);
                break;
            case SDL_QUIT:
                // Handle quit
                break;
            }
        }
    }
}

void pump_and_draw() {
    pump();
    // draw_manager::sparkle() is called by the game loop
}

void raise_process_event() {
    for (auto* h : s_handlers) {
        h->process_event();
    }
}

void raise_draw_event() {
    for (auto* h : s_handlers) {
        h->draw();
    }
}

// -----------------------------------------------------------------------
// Focus/visibility (always focused on N64)
// -----------------------------------------------------------------------
bool has_focus() { return true; }
int pump_info::ticks() { return (int)SDL_GetTicks(); }

// -----------------------------------------------------------------------
// Context management
// -----------------------------------------------------------------------
event_context::event_context() {}
event_context::~event_context() {}

// -----------------------------------------------------------------------
// Discard input (used during AI turns)
// -----------------------------------------------------------------------
void discard_input() {
    n64_input_poll(); // consume and ignore
}

// -----------------------------------------------------------------------
// Call frequency limiter
// -----------------------------------------------------------------------
call_in_main_thread::call_in_main_thread(std::function<void()> f) {
    if (f) f(); // Execute immediately on N64 (single-threaded)
}

} // namespace events

#endif // N64_BUILD
