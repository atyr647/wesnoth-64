#pragma once

/**
 * n64_sdl_shim.hpp
 *
 * Minimal SDL2 API shim for N64.
 *
 * Wesnoth calls SDL2 functions throughout src/sdl/, src/video.cpp,
 * src/draw.cpp, src/sound.cpp, etc. Rather than touching every call site
 * (which would make the diff enormous), we provide a thin shim that:
 *
 *   1. Defines the subset of SDL types/constants used by Wesnoth
 *   2. Routes SDL calls to our n64_video / n64_audio / n64_input layers
 *   3. Stubs out calls that have no N64 equivalent (clipboard, cursor, etc.)
 *
 * Include this INSTEAD of <SDL2/SDL.h> when N64_BUILD is defined.
 * The build system injects -include src/n64/n64_sdl_shim.hpp for N64 builds.
 *
 * Coverage: only the SDL surface/texture/renderer calls actually used
 * by Wesnoth. Unused SDL API (joystick HAT, touch events, etc.) is omitted.
 */

#ifdef N64_BUILD  // entire file is N64-only

#include "n64_platform.hpp"
#include "n64_video.hpp"
#include "n64_audio.hpp"
#include "n64_input.hpp"

// -----------------------------------------------------------------------
// SDL_bool
// -----------------------------------------------------------------------
typedef int SDL_bool;
#define SDL_TRUE  1
#define SDL_FALSE 0

// -----------------------------------------------------------------------
// SDL_Rect
// -----------------------------------------------------------------------
typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Point { int x, y; } SDL_Point;

// -----------------------------------------------------------------------
// SDL_Color / SDL_Colour
// -----------------------------------------------------------------------
typedef struct SDL_Color { u8 r, g, b, a; } SDL_Color;
typedef SDL_Color SDL_Colour;

// -----------------------------------------------------------------------
// SDL_Surface — backed by our texture cache
// -----------------------------------------------------------------------
struct SDL_Surface {
    int    w, h;
    int    pitch;       // bytes per row
    void*  pixels;      // pointer into texture cache RAM
    int    format;      // ignored on N64 (always RGBA5551)
    int    refcount;
};

// -----------------------------------------------------------------------
// SDL_Texture — alias for our N64TexHandle
// -----------------------------------------------------------------------
struct SDL_Texture {
    N64TexHandle handle;
    int w, h;
    bool has_alpha;
};

// -----------------------------------------------------------------------
// SDL_Renderer — singleton, routes to n64_video
// -----------------------------------------------------------------------
struct SDL_Renderer { int dummy; };
struct SDL_Window   { int dummy; };

// Wesnoth holds one global renderer; we expose a stub singleton
static SDL_Renderer g_n64_renderer = {};
static SDL_Window   g_n64_window   = {};

// -----------------------------------------------------------------------
// SDL event types used by Wesnoth
// -----------------------------------------------------------------------
#define SDL_QUIT            0x100
#define SDL_KEYDOWN         0x300
#define SDL_KEYUP           0x301
#define SDL_MOUSEMOTION     0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP   0x402
#define SDL_MOUSEWHEEL      0x403
#define SDL_USEREVENT       0x8000

// SDL_Keycode / SDL_Scancode — minimal set used by Wesnoth's hotkey system
typedef int SDL_Keycode;
typedef int SDL_Scancode;
#define SDLK_UNKNOWN  0
#define SDLK_RETURN   '\r'
#define SDLK_ESCAPE   '\033'
#define SDLK_SPACE    ' '

struct SDL_KeyboardEvent {
    u32 type;
    struct { SDL_Scancode scancode; SDL_Keycode sym; u16 mod; } keysym;
    u8 repeat;
};
struct SDL_MouseMotionEvent  { u32 type; s32 x, y, xrel, yrel; };
struct SDL_MouseButtonEvent  { u32 type; u8 button; u8 state; s32 x, y; };
struct SDL_MouseWheelEvent   { u32 type; s32 x, y; };
struct SDL_UserEvent         { u32 type; int code; void* data1; void* data2; };
struct SDL_QuitEvent         { u32 type; };

typedef union SDL_Event {
    u32                   type;
    SDL_KeyboardEvent     key;
    SDL_MouseMotionEvent  motion;
    SDL_MouseButtonEvent  button;
    SDL_MouseWheelEvent   wheel;
    SDL_UserEvent         user;
    SDL_QuitEvent         quit;
} SDL_Event;

// -----------------------------------------------------------------------
// SDL_BlendMode
// -----------------------------------------------------------------------
typedef int SDL_BlendMode;
#define SDL_BLENDMODE_NONE  0
#define SDL_BLENDMODE_BLEND 1
#define SDL_BLENDMODE_ADD   2
#define SDL_BLENDMODE_MOD   3

// -----------------------------------------------------------------------
// SDL_PixelFormat constants (only used for format queries)
// -----------------------------------------------------------------------
#define SDL_PIXELFORMAT_RGBA5551 0x15441002
#define SDL_PIXELFORMAT_ARGB8888 0x16362004

// -----------------------------------------------------------------------
// SDL subsystem flags (ignored on N64)
// -----------------------------------------------------------------------
#define SDL_INIT_VIDEO  0x00000020
#define SDL_INIT_AUDIO  0x00000010
#define SDL_INIT_EVENTS 0x00004000

// -----------------------------------------------------------------------
// SDL function stubs
// All implementations are inline; no separate .cpp needed for the shim.
// -----------------------------------------------------------------------

// Lifecycle
inline int  SDL_Init(u32) { return 0; }
inline void SDL_Quit() {}
inline const char* SDL_GetError() { return ""; }
inline int SDL_SetHint(const char*, const char*) { return 1; }
inline u32 SDL_GetTicks() {
#ifdef N64_BUILD
    extern u64 get_ticks_ms();
    return (u32)get_ticks_ms();
#else
    return 0;
#endif
}

// Window / Renderer creation — return our singletons
inline SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, u32) {
    return &g_n64_window;
}
inline SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, u32) {
    return &g_n64_renderer;
}
inline void SDL_DestroyWindow(SDL_Window*) {}
inline void SDL_DestroyRenderer(SDL_Renderer*) {}
inline int  SDL_GetRendererOutputSize(SDL_Renderer*, int* w, int* h) {
    *w = N64_SCREEN_W; *h = N64_SCREEN_H; return 0;
}
inline int  SDL_RenderSetLogicalSize(SDL_Renderer*, int, int) { return 0; }
inline int  SDL_RenderSetIntegerScale(SDL_Renderer*, SDL_bool) { return 0; }
inline void SDL_RenderPresent(SDL_Renderer*) { n64_video_flip(); }
inline int  SDL_RenderClear(SDL_Renderer*) {
    n64_video_clear(N64Color::black());
    return 0;
}
inline int  SDL_SetRenderDrawColor(SDL_Renderer*, u8, u8, u8, u8) { return 0; }
inline int  SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode) { return 0; }

// Texture management
inline SDL_Texture* SDL_CreateTexture(SDL_Renderer*, u32, int, int w, int h) {
    auto* t = new SDL_Texture;
    t->handle = N64_TEX_INVALID;
    t->w = w; t->h = h;
    t->has_alpha = true;
    return t;
}
inline void SDL_DestroyTexture(SDL_Texture* t) { delete t; }
inline int  SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return 0; }
inline int  SDL_SetTextureAlphaMod(SDL_Texture*, u8)  { return 0; }
inline int  SDL_SetTextureColorMod(SDL_Texture*, u8, u8, u8) { return 0; }
inline int  SDL_QueryTexture(SDL_Texture* t, u32*, int*, int* w, int* h) {
    *w = t->w; *h = t->h; return 0;
}

// Surface management
inline SDL_Surface* SDL_CreateRGBSurface(u32, int w, int h, int, u32, u32, u32, u32) {
    auto* s = new SDL_Surface;
    s->w = w; s->h = h; s->pitch = w * 2;
    s->pixels = nullptr;
    s->refcount = 1;
    return s;
}
inline void SDL_FreeSurface(SDL_Surface* s) { delete s; }
inline int  SDL_LockSurface(SDL_Surface*) { return 0; }
inline void SDL_UnlockSurface(SDL_Surface*) {}

// Drawing
inline int SDL_RenderCopy(SDL_Renderer*, SDL_Texture* t, const SDL_Rect*, const SDL_Rect* dst) {
    if (t && t->handle != N64_TEX_INVALID && dst)
        n64_draw_texture(LAYER_TERRAIN, t->handle, dst->x, dst->y);
    return 0;
}
inline int SDL_RenderCopyEx(SDL_Renderer*, SDL_Texture* t, const SDL_Rect*, const SDL_Rect* dst,
                            double, const SDL_Point*, int) {
    return SDL_RenderCopy(nullptr, t, nullptr, dst);
}
inline int SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect* r) {
    if (r) n64_draw_rect(LAYER_UI, {(s16)r->x,(s16)r->y,(s16)r->w,(s16)r->h},
                          N64Color::black());
    return 0;
}
inline int SDL_RenderDrawRect(SDL_Renderer*, const SDL_Rect* r) {
    if (r) n64_draw_rect_outline(LAYER_UI, {(s16)r->x,(s16)r->y,(s16)r->w,(s16)r->h},
                                  N64Color::white());
    return 0;
}
inline int SDL_RenderDrawLine(SDL_Renderer*, int x1, int y1, int x2, int) {
    n64_draw_hline(LAYER_UI, x1, x2, y1, N64Color::white());
    return 0;
}

// Events — translate N64 controller input to SDL events
inline int SDL_PollEvent(SDL_Event* ev) {
    // Wesnoth's event loop calls SDL_PollEvent; we synthesise events
    // from the controller state polled by n64_input_poll().
    // On N64 the caller must call n64_input_poll() before the event loop.
    static u32 last_cursor_x = N64_SCREEN_W / 2;
    static u32 last_cursor_y = N64_SCREEN_H / 2;

    const N64InputState& inp = n64_input_get(0);

    // Cursor movement → synthesise SDL_MOUSEMOTION
    if (inp.cursor_dx || inp.cursor_dy) {
        last_cursor_x = (u32)((s32)last_cursor_x + inp.cursor_dx * N64_HEX_W);
        last_cursor_y = (u32)((s32)last_cursor_y + inp.cursor_dy * N64_HEX_H);
        last_cursor_x = last_cursor_x < 0 ? 0 : (last_cursor_x > N64_SCREEN_W ? N64_SCREEN_W : last_cursor_x);
        last_cursor_y = last_cursor_y < 0 ? 0 : (last_cursor_y > N64_SCREEN_H ? N64_SCREEN_H : last_cursor_y);
        ev->type = SDL_MOUSEMOTION;
        ev->motion.x = last_cursor_x;
        ev->motion.y = last_cursor_y;
        ev->motion.xrel = inp.cursor_dx * N64_HEX_W;
        ev->motion.yrel = inp.cursor_dy * N64_HEX_H;
        return 1;
    }

    // A button → SDL_MOUSEBUTTONDOWN / UP
    if (inp.just_pressed[N64_ACTION_SELECT]) {
        ev->type = SDL_MOUSEBUTTONDOWN;
        ev->button.button = 1; // SDL_BUTTON_LEFT
        ev->button.state  = 1;
        ev->button.x = last_cursor_x;
        ev->button.y = last_cursor_y;
        return 1;
    }

    // Other actions → SDL_USEREVENT with hotkey code
    for (int a = N64_ACTION_CANCEL; a < N64_ACTION_COUNT; ++a) {
        if (inp.just_pressed[a]) {
            ev->type = SDL_USEREVENT;
            ev->user.code  = n64_action_to_hotkey(static_cast<N64Action>(a));
            ev->user.data1 = nullptr;
            ev->user.data2 = nullptr;
            return 1;
        }
    }

    return 0; // no event
}

inline void SDL_PumpEvents() { n64_input_poll(); }
inline int  SDL_PushEvent(SDL_Event*) { return 1; }
inline void SDL_FlushEvents(u32, u32) {}

// Audio stubs — Wesnoth calls SDL_mixer directly; those calls are
// intercepted by n64_sdl_mixer_shim.hpp
inline int SDL_OpenAudio(void*, void*) { return 0; }
inline void SDL_CloseAudio() {}
inline void SDL_PauseAudio(int) {}

// Miscellaneous stubs
inline int  SDL_GetDisplayBounds(int, SDL_Rect* r) {
    r->x = 0; r->y = 0; r->w = N64_SCREEN_W; r->h = N64_SCREEN_H;
    return 0;
}
inline int  SDL_ShowCursor(int) { return 0; }
inline void SDL_WarpMouseInWindow(SDL_Window*, int, int) {}
inline void SDL_SetWindowFullscreen(SDL_Window*, u32) {}
inline void SDL_SetWindowTitle(SDL_Window*, const char*) {}
inline void SDL_GetWindowSize(SDL_Window*, int* w, int* h) {
    *w = N64_SCREEN_W; *h = N64_SCREEN_H;
}
inline char* SDL_GetClipboardText() { return (char*)""; }
inline int   SDL_SetClipboardText(const char*) { return 0; }
inline int   SDL_HasClipboardText() { return 0; }
inline u64   SDL_GetPerformanceCounter()  { return SDL_GetTicks(); }
inline u64   SDL_GetPerformanceFrequency() { return 1000; }
inline void  SDL_Delay(u32 ms) {
    // Busy-wait; should rarely be called in the game loop
    u32 end = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < end) {}
}

#endif // N64_BUILD
