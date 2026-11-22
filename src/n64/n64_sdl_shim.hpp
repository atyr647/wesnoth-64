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

// Block all SDL2 headers from being included — this shim replaces them all
#define SDL_h_
#define SDL_H_
#define SDL_main_h_
#define SDL_stdinc_h_
#define SDL_audio_h_
#define SDL_clipboard_h_
#define SDL_error_h_
#define SDL_events_h_
#define SDL_gamecontroller_h_
#define SDL_haptic_h_
#define SDL_hints_h_
#define SDL_joystick_h_
#define SDL_keyboard_h_
#define SDL_keycode_h_
#define SDL_log_h_
#define SDL_mouse_h_
#define SDL_mutex_h_
#define SDL_pixels_h_
#define SDL_platform_h_
#define SDL_power_h_
#define SDL_rect_h_
#define SDL_render_h_
#define SDL_rwops_h_
#define SDL_scancode_h_
#define SDL_surface_h_
#define SDL_system_h_
#define SDL_syswm_h_
#define SDL_thread_h_
#define SDL_timer_h_
#define SDL_touch_h_
#define SDL_version_h_
#define SDL_video_h_
#define SDL_vulkan_h_
#define SDL_image_h_
#define _SDL_IMAGE_H
#define SDL_ttf_h_
#define _SDL_TTF_H
#define SDL_mixer_h_   // also blocked by n64_sdl_mixer_shim.hpp
#define _SDL_NET_H

// Block curl and pango/cairo headers — stubs provided below
#define CURLINC_CURL_H
#define __CURL_CURL_H
#define __PANGOCAIRO_H__

#include "n64_platform.hpp"
#include "n64_video.hpp"
#include "n64_audio.hpp"
#include "n64_input.hpp"
#include <cstdlib>

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
typedef struct SDL_FRect { float x, y, w, h; } SDL_FRect;
typedef struct SDL_FPoint { float x, y; } SDL_FPoint;

// SDL_Rect utility functions
inline int SDL_RectEquals(const SDL_Rect* a, const SDL_Rect* b) {
    return a && b && a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}
inline int SDL_RectEmpty(const SDL_Rect* r) {
    return (!r || r->w <= 0 || r->h <= 0);
}
inline int SDL_PointInRect(const SDL_Point* p, const SDL_Rect* r) {
    return p && r && p->x >= r->x && p->x < r->x + r->w && p->y >= r->y && p->y < r->y + r->h;
}
inline int SDL_HasIntersection(const SDL_Rect* a, const SDL_Rect* b) {
    if (!a || !b) return 0;
    if (a->x + a->w <= b->x || b->x + b->w <= a->x) return 0;
    if (a->y + a->h <= b->y || b->y + b->h <= a->y) return 0;
    return 1;
}
inline void SDL_UnionRect(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* result) {
    if (!a || !b || !result) return;
    int x1 = a->x < b->x ? a->x : b->x;
    int y1 = a->y < b->y ? a->y : b->y;
    int x2a = a->x + a->w, x2b = b->x + b->w;
    int y2a = a->y + a->h, y2b = b->y + b->h;
    result->x = x1; result->y = y1;
    result->w = (x2a > x2b ? x2a : x2b) - x1;
    result->h = (y2a > y2b ? y2a : y2b) - y1;
}
inline int SDL_IntersectRect(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* result) {
    if (!a || !b || !result) return 0;
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2a = a->x + a->w, x2b = b->x + b->w;
    int y2a = a->y + a->h, y2b = b->y + b->h;
    int x2 = x2a < x2b ? x2a : x2b;
    int y2 = y2a < y2b ? y2a : y2b;
    if (x2 <= x1 || y2 <= y1) return 0;
    result->x = x1; result->y = y1; result->w = x2 - x1; result->h = y2 - y1;
    return 1;
}
inline void SDL_free(void* p) { free(p); }

// -----------------------------------------------------------------------
// SDL_Color / SDL_Colour
// -----------------------------------------------------------------------
typedef struct SDL_Color { u8 r, g, b, a; } SDL_Color;
typedef SDL_Color SDL_Colour;

#define SDL_ALPHA_OPAQUE 255
#define SDL_ALPHA_TRANSPARENT 0

struct SDL_Vertex { SDL_FPoint position; SDL_Color color; SDL_FPoint tex_coord; };

// -----------------------------------------------------------------------
// SDL_Surface — backed by our texture cache
// -----------------------------------------------------------------------
struct SDL_PixelFormat {
    u32 format;
    void* palette;
    u8 BitsPerPixel;
    u8 BytesPerPixel;
    u32 Rmask, Gmask, Bmask, Amask;
};

struct SDL_Surface {
    int    w, h;
    int    pitch;       // bytes per row
    void*  pixels;      // pointer into texture cache RAM
    SDL_PixelFormat* format;  // pixel format descriptor
    SDL_Rect clip_rect;
    int    refcount;
};

// -----------------------------------------------------------------------
// SDL_TextureAccess
// -----------------------------------------------------------------------
enum SDL_TextureAccess { SDL_TEXTUREACCESS_STATIC = 0, SDL_TEXTUREACCESS_STREAMING = 1, SDL_TEXTUREACCESS_TARGET = 2 };

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
#define SDL_TEXTEDITING     0x302
#define SDL_TEXTINPUT       0x303
#define SDL_MOUSEMOTION     0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP   0x402
#define SDL_MOUSEWHEEL      0x403
#define SDL_JOYAXISMOTION   0x600
#define SDL_JOYBUTTONDOWN   0x603
#define SDL_JOYBUTTONUP     0x604
#define SDL_JOYHATMOTION    0x602
#define SDL_WINDOWEVENT     0x200
#define SDL_SYSWMEVENT      0x201
#define SDL_MULTIGESTURE    0x800
#define SDL_USEREVENT       0x8000

// SDL_WindowEvent sub-types
#define SDL_WINDOWEVENT_SHOWN          1
#define SDL_WINDOWEVENT_HIDDEN         2
#define SDL_WINDOWEVENT_EXPOSED        3
#define SDL_WINDOWEVENT_MOVED          4
#define SDL_WINDOWEVENT_RESIZED        5
#define SDL_WINDOWEVENT_SIZE_CHANGED   6
#define SDL_WINDOWEVENT_MINIMIZED      7
#define SDL_WINDOWEVENT_MAXIMIZED      8
#define SDL_WINDOWEVENT_RESTORED       9
#define SDL_WINDOWEVENT_ENTER          10
#define SDL_WINDOWEVENT_LEAVE          11
#define SDL_WINDOWEVENT_FOCUS_GAINED   12
#define SDL_WINDOWEVENT_FOCUS_LOST     13
#define SDL_WINDOWEVENT_CLOSE          14

// SDL init subsystem flags
#define SDL_INIT_TIMER  0x00000001

// SDL_Keycode / SDL_Scancode — minimal set used by Wesnoth's hotkey system
typedef int SDL_Keycode;
typedef int SDL_Scancode;

typedef int SDL_Keymod;
#define KMOD_NONE 0x0000
#define KMOD_LSHIFT 0x0001
#define KMOD_RSHIFT 0x0002
#define KMOD_LCTRL 0x0040
#define KMOD_RCTRL 0x0080
#define KMOD_LALT 0x0100
#define KMOD_RALT 0x0200
#define KMOD_CTRL (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_ALT (KMOD_LALT | KMOD_RALT)
#define KMOD_GUI 0x0400

#define SDL_FINGERDOWN 0x700
#define SDL_FINGERUP 0x701
#define SDL_FINGERMOTION 0x702

#define SDLK_UNKNOWN  0
#define SDLK_RETURN   '\r'
#define SDLK_ESCAPE   '\033'
#define SDLK_SPACE    ' '
#define SDLK_TAB      '\t'
#define SDLK_UP       0x40000052
#define SDLK_DOWN     0x40000051
#define SDLK_KP_ENTER 0x400000D
#define SDLK_AC_BACK  0x40000119
#define SDLK_LEFT     0x40000050
#define SDLK_RIGHT    0x4000004F
#define SDLK_LSHIFT   0x400000E1
#define SDLK_RSHIFT   0x400000E5
#define SDLK_LCTRL    0x400000E0
#define SDLK_RCTRL    0x400000E4
#define SDLK_LALT     0x400000E2
#define SDLK_RALT     0x400000E6
#define SDLK_DELETE    0x4000004C
#define SDLK_HOME      0x4000004A
#define SDLK_END       0x4000004D
#define SDLK_PAGEUP    0x4000004B
#define SDLK_PAGEDOWN  0x4000004E
#define SDLK_INSERT    0x40000049
#define SDLK_BACKSPACE 0x00000008
#define SDLK_F1        0x4000003A
#define SDLK_F2        0x4000003B
#define SDLK_F3        0x4000003C
#define SDLK_F4        0x4000003D
#define SDLK_F5        0x4000003E
#define SDLK_F6        0x4000003F
#define SDLK_F7        0x40000040
#define SDLK_F8        0x40000041
#define SDLK_F9        0x40000042
#define SDLK_F10       0x40000043
#define SDLK_F11       0x40000044
#define SDLK_F12       0x40000045
#define SDLK_F13       0x40000068
#define SDLK_F14       0x40000069
#define SDLK_F15       0x4000006A
#define SDLK_F16       0x4000006B
#define SDLK_F17       0x4000006C
#define SDLK_F18       0x4000006D
#define SDLK_F19       0x4000006E
#define SDLK_F20       0x4000006F
#define SDLK_F21       0x40000070
#define SDLK_F22       0x40000071
#define SDLK_F23       0x40000072
#define SDLK_F24       0x40000073

// Keypad keys
#define SDLK_KP_0             0x40000062
#define SDLK_KP_1             0x40000059
#define SDLK_KP_2             0x4000005A
#define SDLK_KP_3             0x4000005B
#define SDLK_KP_4             0x4000005C
#define SDLK_KP_5             0x4000005D
#define SDLK_KP_6             0x4000005E
#define SDLK_KP_7             0x4000005F
#define SDLK_KP_8             0x40000060
#define SDLK_KP_9             0x40000061
#define SDLK_KP_00            0x400000B0
#define SDLK_KP_000           0x400000B1
#define SDLK_KP_A             0x400000BC
#define SDLK_KP_B             0x400000BD
#define SDLK_KP_C             0x400000BE
#define SDLK_KP_D             0x400000BF
#define SDLK_KP_E             0x400000C0
#define SDLK_KP_F             0x400000C1
#define SDLK_KP_AMPERSAND     0x400000C7
#define SDLK_KP_AT            0x400000CE
#define SDLK_KP_BACKSPACE     0x400000BB
#define SDLK_KP_BINARY        0x400000DA
#define SDLK_KP_CLEAR         0x400000D8
#define SDLK_KP_CLEARENTRY    0x400000D9
#define SDLK_KP_COLON         0x400000CB
#define SDLK_KP_COMMA         0x400000C5
#define SDLK_KP_DBLAMPERSAND  0x400000C8
#define SDLK_KP_DBLVERTICALBAR 0x400000CA
#define SDLK_KP_DECIMAL       0x400000DC
#define SDLK_KP_DIVIDE        0x40000054
#define SDLK_KP_EQUALS        0x40000067
#define SDLK_KP_EQUALSAS400   0x400000C6
#define SDLK_KP_EXCLAM        0x400000CF
#define SDLK_KP_GREATER       0x400000CC
#define SDLK_KP_HASH          0x400000CD
#define SDLK_KP_HEXADECIMAL   0x400000DD
#define SDLK_KP_LEFTBRACE     0x400000B8
#define SDLK_KP_LEFTPAREN     0x400000B6
#define SDLK_KP_LESS          0x400000C2
#define SDLK_KP_MEMADD        0x400000D3
#define SDLK_KP_MEMCLEAR      0x400000D2
#define SDLK_KP_MEMDIVIDE     0x400000D6
#define SDLK_KP_MEMMULTIPLY   0x400000D5
#define SDLK_KP_MEMRECALL     0x400000D1
#define SDLK_KP_MEMSTORE      0x400000D0
#define SDLK_KP_MEMSUBTRACT   0x400000D4
#define SDLK_KP_MINUS         0x40000056
#define SDLK_KP_MULTIPLY      0x40000055
#define SDLK_KP_OCTAL         0x400000DB
#define SDLK_KP_PERCENT       0x400000C4
#define SDLK_KP_PERIOD        0x40000063
#define SDLK_KP_PLUS          0x40000057
#define SDLK_KP_PLUSMINUS      0x400000D7
#define SDLK_KP_POWER         0x400000C3
#define SDLK_KP_RIGHTBRACE    0x400000B9
#define SDLK_KP_RIGHTPAREN    0x400000B7
#define SDLK_KP_SPACE         0x400000BA
#define SDLK_KP_TAB           0x400000B2
#define SDLK_KP_VERTICALBAR   0x400000C9
#define SDLK_KP_XOR           0x400000DE

// Letter keys (SDL2 uses lowercase ASCII values)
#define SDLK_a 'a'
#define SDLK_b 'b'
#define SDLK_c 'c'
#define SDLK_d 'd'
#define SDLK_e 'e'
#define SDLK_f 'f'
#define SDLK_g 'g'
#define SDLK_h 'h'
#define SDLK_i 'i'
#define SDLK_j 'j'
#define SDLK_k 'k'
#define SDLK_l 'l'
#define SDLK_m 'm'
#define SDLK_n 'n'
#define SDLK_o 'o'
#define SDLK_p 'p'
#define SDLK_q 'q'
#define SDLK_r 'r'
#define SDLK_s 's'
#define SDLK_t 't'
#define SDLK_u 'u'
#define SDLK_v 'v'
#define SDLK_w 'w'
#define SDLK_x 'x'
#define SDLK_y 'y'
#define SDLK_z 'z'
#define SDLK_0 '0'
#define SDLK_1 '1'
#define SDLK_2 '2'
#define SDLK_3 '3'
#define SDLK_4 '4'
#define SDLK_5 '5'
#define SDLK_6 '6'
#define SDLK_7 '7'
#define SDLK_8 '8'
#define SDLK_9 '9'
#define SDLK_PERIOD '.'
#define SDLK_MINUS '-'
#define SDLK_PLUS '+'
#define SDLK_EQUALS '='
#define SDLK_SEMICOLON ';'
#define SDLK_SLASH '/'
#define SDLK_BACKSLASH '\\'
#define SDLK_LEFTBRACKET '['
#define SDLK_RIGHTBRACKET ']'
#define SDLK_COMMA ','
#define SDLK_QUOTE '\''
#define SDLK_BACKQUOTE '`'

struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    u16 mod;
};
// Event structures with SDL 2's exact layouts (SDL_events.h), so the engine's
// own events.cpp and GUI code compile and behave unchanged.
struct SDL_CommonEvent       { u32 type; u32 timestamp; };
struct SDL_KeyboardEvent     { u32 type; u32 timestamp; u32 windowID; u8 state; u8 repeat; u8 padding2, padding3; SDL_Keysym keysym; };
struct SDL_MouseMotionEvent  { u32 type; u32 timestamp; u32 windowID; u32 which; u32 state; s32 x, y, xrel, yrel; };
struct SDL_MouseButtonEvent  { u32 type; u32 timestamp; u32 windowID; u32 which; u8 button; u8 state; u8 clicks; u8 padding1; s32 x, y; };
struct SDL_MouseWheelEvent   { u32 type; u32 timestamp; u32 windowID; u32 which; s32 x, y; u32 direction; float preciseX, preciseY; s32 mouseX, mouseY; };
struct SDL_UserEvent         { u32 type; u32 timestamp; u32 windowID; s32 code; void* data1; void* data2; };
struct SDL_QuitEvent         { u32 type; u32 timestamp; };
struct SDL_TouchFingerEvent  { u32 type; u32 timestamp; s64 touchId; s64 fingerId; float x, y, dx, dy; float pressure; u32 windowID; };
struct SDL_TextInputEvent    { u32 type; u32 timestamp; u32 windowID; char text[32]; };
struct SDL_TextEditingEvent  { u32 type; u32 timestamp; u32 windowID; char text[32]; s32 start; s32 length; };
struct SDL_WindowEvent       { u32 type; u32 timestamp; u32 windowID; u8 event; u8 padding1, padding2, padding3; s32 data1; s32 data2; };
struct SDL_MultiGestureEvent { u32 type; u32 timestamp; s64 touchId; float dTheta, dDist, x, y; u16 numFingers; u16 padding; };
struct SDL_JoyAxisEvent      { u32 type; u32 timestamp; s32 which; u8 axis; u8 padding1, padding2, padding3; s16 value; u16 padding4; };
struct SDL_JoyHatEvent       { u32 type; u32 timestamp; s32 which; u8 hat; u8 value; u8 padding1, padding2; };
struct SDL_JoyButtonEvent    { u32 type; u32 timestamp; s32 which; u8 button; u8 state; u8 padding1, padding2; };
struct SDL_SysWMEvent        { u32 type; u32 timestamp; void* msg; };
#define SDL_TOUCH_MOUSEID ((u32)-1)
#define SDL_MOUSE_TOUCHID ((s64)-1)

typedef union SDL_Event {
    u32                    type;
    SDL_CommonEvent        common;
    SDL_KeyboardEvent      key;
    SDL_MouseMotionEvent   motion;
    SDL_MouseButtonEvent   button;
    SDL_MouseWheelEvent    wheel;
    SDL_UserEvent          user;
    SDL_QuitEvent          quit;
    SDL_TouchFingerEvent   tfinger;
    SDL_TextInputEvent     text;
    SDL_TextEditingEvent   edit;
    SDL_WindowEvent        window;
    SDL_MultiGestureEvent  mgesture;
    SDL_JoyAxisEvent       jaxis;
    SDL_JoyHatEvent        jhat;
    SDL_JoyButtonEvent     jbutton;
    SDL_SysWMEvent         syswm;
    u8                     padding[56];
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
// SDL_RendererFlip
// -----------------------------------------------------------------------
enum SDL_RendererFlip { SDL_FLIP_NONE = 0, SDL_FLIP_HORIZONTAL = 1, SDL_FLIP_VERTICAL = 2 };

#define SDL_RENDERER_ACCELERATED 2
#define SDL_RENDERER_PRESENTVSYNC 4
#define SDL_WINDOW_SHOWN 0x00000004
#define SDL_WINDOW_RESIZABLE 0x00000020
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001
#define SDL_WINDOW_INPUT_FOCUS 0x00000200
#define SDL_INIT_EVERYTHING 0x0000FFFF

// -----------------------------------------------------------------------
// SDL_PixelFormat constants (only used for format queries)
// -----------------------------------------------------------------------
#define SDL_PIXELFORMAT_UNKNOWN  0
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
inline u32  SDL_WasInit(u32) { return 0; }
inline int  SDL_InitSubSystem(u32) { return 0; }
inline void SDL_Quit() {}
inline const char* SDL_GetError() { return ""; }
inline int SDL_SetHint(const char*, const char*) { return 1; }
#define SDL_HINT_NO_SIGNAL_HANDLERS "SDL_NO_SIGNAL_HANDLERS"
// newlib keeps a process environment; route SDL's setenv to it.
inline int SDL_setenv(const char* name, const char* value, int overwrite) { return ::setenv(name, value, overwrite); }
// No OS dialogs on the console: message boxes go to the debug log.
#define SDL_MESSAGEBOX_ERROR       0x00000010
#define SDL_MESSAGEBOX_WARNING     0x00000020
#define SDL_MESSAGEBOX_INFORMATION 0x00000040
inline int SDL_ShowSimpleMessageBox(u32, const char* title, const char* message, struct SDL_Window*) {
    N64_LOG("%s: %s", title ? title : "", message ? message : "");
    return 0;
}
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
inline int  SDL_GetTextureAlphaMod(SDL_Texture*, u8* alpha) { if (alpha) *alpha = 255; return 0; }
inline int  SDL_GetTextureColorMod(SDL_Texture*, u8* r, u8* g, u8* b) { if (r) *r = 255; if (g) *g = 255; if (b) *b = 255; return 0; }
inline int  SDL_GetTextureBlendMode(SDL_Texture*, SDL_BlendMode* m) { if (m) *m = 0; return 0; }
inline SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer*, SDL_Surface*) { return nullptr; }
inline int  SDL_QueryTexture(SDL_Texture* t, u32*, int*, int* w, int* h) {
    *w = t->w; *h = t->h; return 0;
}

// Surface management
inline SDL_Surface* SDL_CreateRGBSurface(u32, int w, int h, int bpp, u32 rm, u32 gm, u32 bm, u32 am) {
    auto* s = new SDL_Surface;
    s->w = w; s->h = h; s->pitch = w * (bpp > 0 ? bpp / 8 : 2);
    s->pixels = nullptr;
    auto* fmt = new SDL_PixelFormat;
    fmt->format = 0; fmt->BitsPerPixel = bpp > 0 ? bpp : 16;
    fmt->BytesPerPixel = fmt->BitsPerPixel / 8;
    fmt->Rmask = rm; fmt->Gmask = gm; fmt->Bmask = bm; fmt->Amask = am;
    s->format = fmt;
    s->refcount = 1;
    return s;
}
inline void SDL_FreeSurface(SDL_Surface* s) { if (s) { delete s->format; delete s; } }
inline int  SDL_LockSurface(SDL_Surface*) { return 0; }
inline void SDL_UnlockSurface(SDL_Surface*) {}
#define SDL_MUSTLOCK(s) (0)

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

// SDL_PeepEvents action values
#define SDL_ADDEVENT  0
#define SDL_PEEKEVENT 1
#define SDL_GETEVENT  2

inline int SDL_PeepEvents(SDL_Event*, int, int, u32, u32) { return 0; }
inline void SDL_StartTextInput() {}
inline void SDL_StopTextInput() {}
inline void SDL_SetTextInputRect(const SDL_Rect*) {}

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
inline const char* SDL_GetCurrentVideoDriver() { return "n64"; }
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

// -----------------------------------------------------------------------
// SDL version info (used by build_info.cpp, log output)
// Must be defined before SDL_image/SDL_mixer version macros.
// -----------------------------------------------------------------------
struct SDL_version {
    u8 major, minor, patch;
};

#define SDL_VERSION(x) do { (x)->major = 2; (x)->minor = 0; (x)->patch = 0; } while(0)

inline void SDL_GetVersion(SDL_version* v) {
    if (v) { v->major = 2; v->minor = 0; v->patch = 0; }
}
inline const char* SDL_GetRevision() { return "N64-shim"; }

// -----------------------------------------------------------------------
// SDL_image stubs — Wesnoth uses IMG_Load, IMG_Init, etc.
// On N64, all images are pre-converted by the asset pipeline.
// -----------------------------------------------------------------------
#define IMG_INIT_PNG 0x02

inline int  IMG_Init(int) { return IMG_INIT_PNG; }
inline void IMG_Quit() {}
inline SDL_Surface* IMG_Load(const char*) { return nullptr; }
inline SDL_Surface* IMG_Load_RW(void*, int) { return nullptr; }
inline SDL_Texture* IMG_LoadTexture(SDL_Renderer*, const char*) { return nullptr; }
inline const char*  IMG_GetError() { return "N64: SDL_image not available"; }

#define SDL_IMAGE_VERSION(x) do { (x)->major = 0; (x)->minor = 0; (x)->patch = 0; } while(0)
inline const SDL_version* IMG_Linked_Version() { static SDL_version v = {0,0,0}; return &v; }

// -----------------------------------------------------------------------
// SDL_Timer stubs — used by AI timing and some UI elements
// -----------------------------------------------------------------------
typedef u32 SDL_TimerID;
typedef u32 (*SDL_TimerCallback)(u32, void*);
inline SDL_TimerID SDL_AddTimer(u32, SDL_TimerCallback, void*) { return 0; }
inline int SDL_RemoveTimer(SDL_TimerID) { return 1; }

// -----------------------------------------------------------------------
// SDL_RWops stubs — Wesnoth uses these for file I/O abstraction
// -----------------------------------------------------------------------
// SDL_RWops with SDL 2's layout. The engine's filesystem_sdl.cpp builds its
// own RWops over filesystem streams; memory RWops are implemented here.
#define SDLCALL
#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2
#define SDL_RWOPS_UNKNOWN   0U
#define SDL_RWOPS_MEMORY    4U
#define SDL_RWOPS_MEMORY_RO 5U
struct SDL_RWops {
    s64 (*size)(SDL_RWops* context);
    s64 (*seek)(SDL_RWops* context, s64 offset, int whence);
    size_t (*read)(SDL_RWops* context, void* ptr, size_t size, size_t maxnum);
    size_t (*write)(SDL_RWops* context, const void* ptr, size_t size, size_t num);
    int (*close)(SDL_RWops* context);
    u32 type;
    union {
        struct { u8* base; u8* here; u8* stop; } mem;
        struct { void* data1; void* data2; } unknown;
    } hidden;
};
inline SDL_RWops* SDL_AllocRW() { return static_cast<SDL_RWops*>(::calloc(1, sizeof(SDL_RWops))); }
inline void SDL_FreeRW(SDL_RWops* rw) { ::free(rw); }
inline s64 SDL_RWsize(SDL_RWops* c)                         { return c->size(c); }
inline s64 SDL_RWseek(SDL_RWops* c, s64 off, int whence)    { return c->seek(c, off, whence); }
inline s64 SDL_RWtell(SDL_RWops* c)                         { return c->seek(c, 0, RW_SEEK_CUR); }
inline size_t SDL_RWread(SDL_RWops* c, void* p, size_t sz, size_t n)        { return c->read(c, p, sz, n); }
inline size_t SDL_RWwrite(SDL_RWops* c, const void* p, size_t sz, size_t n) { return c->write(c, p, sz, n); }
inline int SDL_RWclose(SDL_RWops* c)                        { return c->close(c); }
namespace n64_sdl_detail {
inline s64 mem_size(SDL_RWops* c) { return c->hidden.mem.stop - c->hidden.mem.base; }
inline s64 mem_seek(SDL_RWops* c, s64 off, int whence) {
    u8* p = whence == RW_SEEK_SET ? c->hidden.mem.base + off
          : whence == RW_SEEK_CUR ? c->hidden.mem.here + off : c->hidden.mem.stop + off;
    if (p < c->hidden.mem.base) p = c->hidden.mem.base;
    if (p > c->hidden.mem.stop) p = c->hidden.mem.stop;
    c->hidden.mem.here = p;
    return p - c->hidden.mem.base;
}
inline size_t mem_read(SDL_RWops* c, void* ptr, size_t size, size_t maxnum) {
    if (!size) return 0;
    size_t avail = (size_t)(c->hidden.mem.stop - c->hidden.mem.here) / size;
    size_t n = maxnum < avail ? maxnum : avail;
    ::memcpy(ptr, c->hidden.mem.here, n * size);
    c->hidden.mem.here += n * size;
    return n;
}
inline size_t mem_write(SDL_RWops* c, const void* ptr, size_t size, size_t num) {
    if (c->type == SDL_RWOPS_MEMORY_RO || !size) return 0;
    size_t avail = (size_t)(c->hidden.mem.stop - c->hidden.mem.here) / size;
    size_t n = num < avail ? num : avail;
    ::memcpy(c->hidden.mem.here, ptr, n * size);
    c->hidden.mem.here += n * size;
    return n;
}
inline int mem_close(SDL_RWops* c) { SDL_FreeRW(c); return 0; }
} // namespace n64_sdl_detail
inline SDL_RWops* SDL_RWFromConstMem(const void* mem, int size) {
    SDL_RWops* c = SDL_AllocRW();
    if (!c) return nullptr;
    c->size = n64_sdl_detail::mem_size; c->seek = n64_sdl_detail::mem_seek;
    c->read = n64_sdl_detail::mem_read; c->write = n64_sdl_detail::mem_write;
    c->close = n64_sdl_detail::mem_close;
    c->type = SDL_RWOPS_MEMORY_RO;
    c->hidden.mem.base = c->hidden.mem.here = (u8*)mem;
    c->hidden.mem.stop = (u8*)mem + size;
    return c;
}
inline SDL_RWops* SDL_RWFromMem(void* mem, int size) {
    SDL_RWops* c = SDL_RWFromConstMem(mem, size);
    if (c) c->type = SDL_RWOPS_MEMORY;
    return c;
}
// Files are opened through the engine's filesystem (filesystem::make_read_RWops).
inline SDL_RWops* SDL_RWFromFile(const char*, const char*) { return nullptr; }

// (SDL version info moved above SDL_image section)

// -----------------------------------------------------------------------
// SDL Hints (used by video.cpp initialization)
// -----------------------------------------------------------------------
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_VIDEO_ALLOW_SCREENSAVER "SDL_VIDEO_ALLOW_SCREENSAVER"
#define SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH "SDL_MOUSE_FOCUS_CLICKTHROUGH"
#define SDL_HINT_MOUSE_DOUBLE_CLICK_TIME "SDL_MOUSE_DOUBLE_CLICK_TIME"
#define SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS "SDL_MOUSE_DOUBLE_CLICK_RADIUS"

inline const char* SDL_GetHint(const char*) { return ""; }

// -----------------------------------------------------------------------
// Additional missing stubs
// -----------------------------------------------------------------------
inline int SDL_SetSurfaceBlendMode(SDL_Surface*, SDL_BlendMode) { return 0; }
inline int SDL_GetSurfaceBlendMode(SDL_Surface*, SDL_BlendMode* m) { if (m) *m = SDL_BLENDMODE_NONE; return 0; }
inline int SDL_SetSurfaceAlphaMod(SDL_Surface*, u8) { return 0; }
inline int SDL_UpperBlit(SDL_Surface*, const SDL_Rect*, SDL_Surface*, SDL_Rect*) { return 0; }
#define SDL_BlitSurface SDL_UpperBlit
inline int SDL_UpperBlitScaled(SDL_Surface*, const SDL_Rect*, SDL_Surface*, SDL_Rect*) { return 0; }
#define SDL_BlitScaled SDL_UpperBlitScaled
inline SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface* src, u32, u32) { return src; }
inline SDL_Surface* SDL_CreateRGBSurfaceFrom(void*, int w, int h, int, int, u32, u32, u32, u32) {
    return SDL_CreateRGBSurface(0, w, h, 16, 0, 0, 0, 0);
}
inline SDL_Surface* SDL_CreateRGBSurfaceWithFormat(u32, int w, int h, int, u32) {
    return SDL_CreateRGBSurface(0, w, h, 16, 0, 0, 0, 0);
}
inline int SDL_FillRect(SDL_Surface*, const SDL_Rect*, u32) { return 0; }
inline u32 SDL_MapRGBA(const SDL_PixelFormat*, u8 r, u8 g, u8 b, u8 a) {
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | a;
}
inline u32 SDL_MapRGB(const SDL_PixelFormat*, u8 r, u8 g, u8 b) {
    return ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | 0xFF;
}

// SDL_PixelFormat — defined above near SDL_Surface

// SDL Log
inline void SDL_Log(const char*, ...) {}
inline void SDL_LogError(int, const char*, ...) {}
inline void SDL_LogWarn(int, const char*, ...) {}
inline void SDL_LogInfo(int, const char*, ...) {}

// SDL_SetError
inline int SDL_SetError(const char*, ...) { return -1; }
inline void SDL_ClearError() {}

inline int SDL_GetModState() { return KMOD_NONE; }
inline void SDL_SetModState(int) {}

// Mouse
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON(x)     (1u << ((x) - 1))
#define SDL_BUTTON_LMASK  SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK  SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK  SDL_BUTTON(SDL_BUTTON_RIGHT)
#define SDL_BUTTON_X1     4
#define SDL_BUTTON_X2     5
#define SDL_BUTTON_X1MASK SDL_BUTTON(SDL_BUTTON_X1)
#define SDL_BUTTON_X2MASK SDL_BUTTON(SDL_BUTTON_X2)
#define SDL_PRESSED  1
#define SDL_RELEASED 0

inline u32 SDL_GetMouseState(int* x, int* y) {
    if (x) *x = N64_SCREEN_W / 2;
    if (y) *y = N64_SCREEN_H / 2;
    return 0;
}

// Keyboard
inline const u8* SDL_GetKeyboardState(int* numkeys) {
    static u8 keys[512] = {};
    if (numkeys) *numkeys = 512;
    return keys;
}
inline const char* SDL_GetKeyName(SDL_Keycode) { return ""; }
inline SDL_Keycode SDL_GetKeyFromName(const char*) { return 0; }
inline SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode) { return 0; }

// -----------------------------------------------------------------------
// SDL semaphore stubs
// -----------------------------------------------------------------------
typedef struct SDL_sem SDL_sem;
inline SDL_sem* SDL_CreateSemaphore(u32) { return nullptr; }
inline void SDL_DestroySemaphore(SDL_sem*) {}
inline int SDL_SemWait(SDL_sem*) { return 0; }
inline int SDL_SemTryWait(SDL_sem*) { return 0; }
inline int SDL_SemPost(SDL_sem*) { return 0; }
inline u32 SDL_SemValue(SDL_sem*) { return 1; }

// -----------------------------------------------------------------------
// SDL_mixer stubs
// -----------------------------------------------------------------------
#define SDL_MIXER_VERSION(x) do { (x)->major = 0; (x)->minor = 0; (x)->patch = 0; } while(0)
inline const SDL_version* Mix_Linked_Version() { static SDL_version v = {0,0,0}; return &v; }

// -----------------------------------------------------------------------
// Audio format constants (standard SDL2 values)
// -----------------------------------------------------------------------
#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_U16LSB 0x0010
#define AUDIO_S16LSB 0x8010
#define AUDIO_U16MSB 0x1010
#define AUDIO_S16MSB 0x9010
#define AUDIO_S32LSB 0x8020
#define AUDIO_S32MSB 0x9020
#define AUDIO_F32LSB 0x8120
#define AUDIO_F32MSB 0x9120

// -----------------------------------------------------------------------
// Boost version stub
// -----------------------------------------------------------------------
#ifndef BOOST_LIB_VERSION
#define BOOST_LIB_VERSION "1_85"
#endif

// -----------------------------------------------------------------------
// libcurl stubs
// -----------------------------------------------------------------------
#ifndef LIBCURL_VERSION_NUM
#define LIBCURL_VERSION_NUM 0x075500
#endif

typedef int CURLversion;
#define CURLVERSION_NOW 0
struct curl_version_info_data { const char* version; };
inline curl_version_info_data* curl_version_info(CURLversion) {
    static curl_version_info_data d = { "0.0.0" };
    return &d;
}

// -----------------------------------------------------------------------
// Cairo / Pango stubs
// -----------------------------------------------------------------------
#ifndef CAIRO_VERSION_STRING
#define CAIRO_VERSION_STRING "0.0.0"
#endif
inline const char* cairo_version_string() { return "0.0.0"; }

#ifndef PANGO_VERSION_STRING
#define PANGO_VERSION_STRING "0.0.0"
#endif
inline const char* pango_version_string() { return "0.0.0"; }

// -----------------------------------------------------------------------
// OpenSSL version stubs (build_info.cpp uses these)
// -----------------------------------------------------------------------
#ifndef OPENSSL_OPENSSLV_H
#define OPENSSL_OPENSSLV_H
#define OPENSSL_VERSION_NUMBER 0x1010100fL
#endif

#ifndef OPENSSL_CRYPTO_H
#define OPENSSL_CRYPTO_H
inline long SSLeay() { return 0x1010100fL; }
#endif

// OpenSSL stubs — preferences.cpp includes these conditionally
#ifndef OPENSSL_EVP_H
#define OPENSSL_EVP_H
#define OPENSSL_ERR_H
// Stub the functions used by preferences for password hashing
typedef void EVP_MD_CTX;
typedef void EVP_MD;
inline EVP_MD_CTX* EVP_MD_CTX_new() { return nullptr; }
inline void EVP_MD_CTX_free(EVP_MD_CTX*) {}
inline const EVP_MD* EVP_md5() { return nullptr; }
inline const EVP_MD* EVP_sha256() { return nullptr; }
inline int EVP_MD_size(const EVP_MD*) { return 16; }
inline int EVP_DigestInit_ex(EVP_MD_CTX*, const EVP_MD*, void*) { return 0; }
inline int EVP_DigestUpdate(EVP_MD_CTX*, const void*, size_t) { return 0; }
inline int EVP_DigestFinal_ex(EVP_MD_CTX*, unsigned char*, unsigned int*) { return 0; }
// Cipher API: used only to store multiplayer server passwords (online play).
// Every call fails, so preferences.cpp logs the error and stores nothing.
typedef struct n64_evp_cipher_ctx EVP_CIPHER_CTX;
typedef struct n64_evp_cipher EVP_CIPHER;
typedef struct n64_engine ENGINE;
inline EVP_CIPHER_CTX* EVP_CIPHER_CTX_new() { return nullptr; }
inline void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX*) {}
inline const EVP_CIPHER* EVP_aes_256_cbc() { return nullptr; }
inline int EVP_EncryptInit_ex(EVP_CIPHER_CTX*, const EVP_CIPHER*, ENGINE*, const unsigned char*, const unsigned char*) { return 0; }
inline int EVP_EncryptUpdate(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int) { return 0; }
inline int EVP_EncryptFinal_ex(EVP_CIPHER_CTX*, unsigned char*, int*) { return 0; }
inline int EVP_DecryptInit_ex(EVP_CIPHER_CTX*, const EVP_CIPHER*, ENGINE*, const unsigned char*, const unsigned char*) { return 0; }
inline int EVP_DecryptUpdate(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int) { return 0; }
inline int EVP_DecryptFinal_ex(EVP_CIPHER_CTX*, unsigned char*, int*) { return 0; }
inline unsigned long ERR_get_error() { return 0; }
inline char* ERR_error_string(unsigned long, char*) { static char msg[] = "no cryptography on the Nintendo 64"; return msg; }
#endif

#endif // N64_BUILD
