/*
 * SDL_n64video.c -- SDL video driver for the Nintendo 64.
 *
 * One display mode: 640x480, 32-bit. Programs draw with SDL's software
 * renderer into the window framebuffer; presenting converts it to the video
 * interface's 640x240 progressive 16-bit output by averaging each pair of
 * rows (the console's highest non-interlaced mode keeps the full horizontal
 * resolution and halves the vertical one).
 *
 * Input: controller 1 drives the mouse and a few keys (see pump_controller).
 * The mouse cursor is composited onto the output, as a hardware cursor would
 * be, so it never touches the program's framebuffer.
 *
 * Registered as DUMMY_bootstrap (SDL_VIDEO_DRIVER_DUMMY in SDL_config.h) so
 * SDL itself needs no changes.
 */
#include "SDL_internal.h"

#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_hints.h"
#include "video/SDL_sysvideo.h"
#include "video/SDL_pixels_c.h"
#include "events/SDL_events_c.h"
#include "events/SDL_mouse_c.h"
#include "events/SDL_keyboard_c.h"

#include <libdragon.h>

#define N64_W        640
#define N64_H        480            /* window / logical height */
#define N64_OUT_H    240            /* video output lines */
#define FB_DATA      "_SDL_N64Surface"

void SDL_N64_RunTimers(void);
void SDL_N64_AudioPump(void);

/* ------------------------------------------------------------- output */

static uint16_t *s_out;             /* converted frame, 640x240 RGBA5551 */
static SDL_bool s_display_ready;

static void output_init(void)
{
    if (s_display_ready) {
        return;
    }
    /* The platform start-up code may already have set up the display; take
       it over with our mode. */
    display_close();
    display_init(RESOLUTION_640x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    s_out = (uint16_t *)SDL_calloc(N64_W * N64_OUT_H, sizeof(uint16_t));
    s_display_ready = SDL_TRUE;
}

static inline uint16_t avg_pack(Uint32 a, Uint32 b)
{
    /* average two XRGB8888 pixels, pack as RGBA5551 */
    const Uint32 r = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF)) >> 1;
    const Uint32 g = (((a >> 8) & 0xFF) + ((b >> 8) & 0xFF)) >> 1;
    const Uint32 bl = ((a & 0xFF) + (b & 0xFF)) >> 1;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((bl >> 3) << 1) | 1);
}

/* Convert window rows [y0, y1) into the output frame. */
static void convert_rows(const SDL_Surface *s, int x0, int x1, int y0, int y1)
{
    int y, x;
    y0 &= ~1;
    if (y1 > s->h) {
        y1 = s->h;
    }
    if (x1 > N64_W) {
        x1 = N64_W;
    }
    for (y = y0; y < y1; y += 2) {
        const Uint32 *r0 = (const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch);
        const Uint32 *r1 = (y + 1 < s->h) ? (const Uint32 *)((const Uint8 *)r0 + s->pitch) : r0;
        uint16_t *o = s_out + (y >> 1) * N64_W;
        for (x = x0; x < x1; ++x) {
            o[x] = avg_pack(r0[x], r1[x]);
        }
    }
}

/* ------------------------------------------------------------- cursor */

typedef struct
{
    SDL_Surface *image;             /* ARGB8888 copy */
    int hot_x, hot_y;
} N64Cursor;

static int s_mouse_x = N64_W / 2, s_mouse_y = N64_H / 2;

static SDL_Cursor *N64_CreateCursor(SDL_Surface *surface, int hot_x, int hot_y)
{
    SDL_Cursor *cursor = (SDL_Cursor *)SDL_calloc(1, sizeof(*cursor));
    N64Cursor *data = (N64Cursor *)SDL_calloc(1, sizeof(*data));
    if (!cursor || !data) {
        SDL_free(cursor);
        SDL_free(data);
        SDL_OutOfMemory();
        return NULL;
    }
    data->image = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    data->hot_x = hot_x;
    data->hot_y = hot_y;
    cursor->driverdata = data;
    return cursor;
}

static SDL_Cursor *N64_CreateSystemCursor(SDL_SystemCursor id)
{
    /* Standard arrow: 12x19, 1 = black outline, 2 = white fill */
    static const char *const arrow[19] = {
        "1           ", "11          ", "121         ", "1221        ",
        "12221       ", "122221      ", "1222221     ", "12222221    ",
        "122222221   ", "1222222221  ", "12222222221 ", "122222111111",
        "1221221     ", "121 1221    ", "11  1221    ", "1    1221   ",
        "     1221   ", "      1221  ", "      11    ",
    };
    SDL_Surface *s;
    SDL_Cursor *c;
    int x, y;
    (void)id;
    s = SDL_CreateRGBSurfaceWithFormat(0, 12, 19, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        return NULL;
    }
    for (y = 0; y < 19; ++y) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (x = 0; x < 12; ++x) {
            row[x] = arrow[y][x] == '1' ? 0xFF000000u : arrow[y][x] == '2' ? 0xFFFFFFFFu : 0;
        }
    }
    c = N64_CreateCursor(s, 0, 0);
    SDL_FreeSurface(s);
    return c;
}

static void N64_FreeCursor(SDL_Cursor *cursor)
{
    if (cursor) {
        N64Cursor *data = (N64Cursor *)cursor->driverdata;
        if (data) {
            SDL_FreeSurface(data->image);
            SDL_free(data);
        }
        SDL_free(cursor);
    }
}

static int N64_ShowCursor(SDL_Cursor *cursor)
{
    (void)cursor;
    return 0;
}

static void N64_WarpMouse(SDL_Window *window, int x, int y)
{
    s_mouse_x = x;
    s_mouse_y = y;
    SDL_SendMouseMotion(window, 0, 0, x, y);
}

/* Blend the current cursor onto the 16-bit output frame. */
static void draw_cursor(uint16_t *dst, int stride_px)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    N64Cursor *data;
    SDL_Surface *img;
    int x, y;

    if (!mouse->cursor_shown || !mouse->cur_cursor || !mouse->cur_cursor->driverdata) {
        return;
    }
    data = (N64Cursor *)mouse->cur_cursor->driverdata;
    img = data->image;
    if (!img) {
        return;
    }
    for (y = 0; y < img->h; y += 2) {
        const int wy = s_mouse_y - data->hot_y + y;
        const Uint32 *row;
        uint16_t *o;
        if (wy < 0 || wy >= N64_H) {
            continue;
        }
        row = (const Uint32 *)((const Uint8 *)img->pixels + y * img->pitch);
        o = dst + (wy >> 1) * stride_px;
        for (x = 0; x < img->w; ++x) {
            const int wx = s_mouse_x - data->hot_x + x;
            const Uint32 p = row[x];
            const Uint32 a = p >> 24;
            Uint32 r, g, b, d;
            if (wx < 0 || wx >= N64_W || a == 0) {
                continue;
            }
            r = (p >> 16) & 0xFF;
            g = (p >> 8) & 0xFF;
            b = p & 0xFF;
            if (a != 255) {
                d = o[wx];
                r = (r * a + (((d >> 11) & 31) << 3) * (255 - a)) / 255;
                g = (g * a + (((d >> 6) & 31) << 3) * (255 - a)) / 255;
                b = (b * a + (((d >> 1) & 31) << 3) * (255 - a)) / 255;
            }
            o[wx] = (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
        }
    }
}

/* For the RDP renderer (SDL_n64render.c): the cursor to draw on the frame. */
void SDL_N64_GetCursor(SDL_Surface **image, int *hot_x, int *hot_y, int *x, int *y)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    *image = NULL;
    if (mouse->cursor_shown && mouse->cur_cursor && mouse->cur_cursor->driverdata) {
        N64Cursor *data = (N64Cursor *)mouse->cur_cursor->driverdata;
        *image = data->image;
        *hot_x = data->hot_x;
        *hot_y = data->hot_y;
    }
    *x = s_mouse_x;
    *y = s_mouse_y;
}

void SDL_N64_TakeDisplay(void)
{
    output_init();
}

static void present(void)
{
    surface_t *disp = display_get();
    uint16_t *dst = (uint16_t *)disp->buffer;
    const int stride_px = disp->stride / 2;
    int y;
    for (y = 0; y < N64_OUT_H; ++y) {
        SDL_memcpy(dst + y * stride_px, s_out + y * N64_W, N64_W * sizeof(uint16_t));
    }
    draw_cursor(dst, stride_px);
    display_show(disp);
}

/* ------------------------------------------------------- framebuffer */

static int N64_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    const Uint32 surface_format = SDL_PIXELFORMAT_RGB888;
    SDL_Surface *surface;
    int w, h;

    SDL_Surface *old = (SDL_Surface *)SDL_SetWindowData(window, FB_DATA, NULL);
    SDL_FreeSurface(old);

    SDL_GetWindowSizeInPixels(window, &w, &h);
    surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 0, surface_format);
    if (!surface) {
        return -1;
    }
    SDL_SetWindowData(window, FB_DATA, surface);
    *format = surface_format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

static int N64_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_GetWindowData(window, FB_DATA);
    int i;
    if (!surface) {
        return SDL_SetError("Couldn't find framebuffer surface for window");
    }
    for (i = 0; i < numrects; ++i) {
        const SDL_Rect *r = &rects[i];
        convert_rows(surface, SDL_max(r->x, 0), r->x + r->w, SDL_max(r->y, 0), r->y + r->h);
    }
    present();
    return 0;
}

static void N64_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_Surface *surface = (SDL_Surface *)SDL_SetWindowData(window, FB_DATA, NULL);
    SDL_FreeSurface(surface);
}

/* --------------------------------------------------------------- input */

static joypad_buttons_t s_prev;

static void key(SDL_bool down, SDL_Scancode sc)
{
    SDL_SendKeyboardKey(down ? SDL_PRESSED : SDL_RELEASED, sc);
}

/*
 * Controller 1:
 *   analog stick  mouse pointer (speed grows with deflection)
 *   A / B         left / right mouse button
 *   Z             Return            Start   Escape
 *   D-pad         arrow keys (scroll the map)
 *   C-up/C-down   mouse wheel up/down (scroll lists, zoom with R)
 *   C-left        n (next unit)     C-right  space (end unit's move)
 *   L             Shift (held)      R        Ctrl (held)
 */
static void pump_controller(SDL_Window *window)
{
    joypad_inputs_t in;
    joypad_buttons_t b, chg;
    int dx, dy;

    joypad_poll();
    if (!joypad_is_connected(JOYPAD_PORT_1)) {
        return;
    }
    in = joypad_get_inputs(JOYPAD_PORT_1);
    b = in.btn;
    chg.raw = b.raw ^ s_prev.raw;

    /* pointer: dead zone 8, quadratic speed up to ~12 px/frame */
    dx = in.stick_x;
    dy = -in.stick_y;
    dx = (dx > 8) ? dx - 8 : (dx < -8) ? dx + 8 : 0;
    dy = (dy > 8) ? dy - 8 : (dy < -8) ? dy + 8 : 0;
    if (dx || dy) {
        const int sx = (dx * SDL_abs(dx)) / 300 + (dx > 0 ? 1 : dx < 0 ? -1 : 0);
        const int sy = (dy * SDL_abs(dy)) / 300 + (dy > 0 ? 1 : dy < 0 ? -1 : 0);
        s_mouse_x = SDL_clamp(s_mouse_x + sx, 0, N64_W - 1);
        s_mouse_y = SDL_clamp(s_mouse_y + sy, 0, N64_H - 1);
        SDL_SendMouseMotion(window, 0, 0, s_mouse_x, s_mouse_y);
    }

    if (chg.a) SDL_SendMouseButton(window, 0, b.a ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_LEFT);
    if (chg.b) SDL_SendMouseButton(window, 0, b.b ? SDL_PRESSED : SDL_RELEASED, SDL_BUTTON_RIGHT);
    if (chg.l) key(b.l, SDL_SCANCODE_LSHIFT);
    if (chg.r) key(b.r, SDL_SCANCODE_LCTRL);
    if (chg.z) key(b.z, SDL_SCANCODE_RETURN);
    if (chg.start) key(b.start, SDL_SCANCODE_ESCAPE);
    if (chg.d_up) key(b.d_up, SDL_SCANCODE_UP);
    if (chg.d_down) key(b.d_down, SDL_SCANCODE_DOWN);
    if (chg.d_left) key(b.d_left, SDL_SCANCODE_LEFT);
    if (chg.d_right) key(b.d_right, SDL_SCANCODE_RIGHT);
    if (chg.c_left) key(b.c_left, SDL_SCANCODE_N);
    if (chg.c_right) key(b.c_right, SDL_SCANCODE_SPACE);
    if (chg.c_up && b.c_up) SDL_SendMouseWheel(window, 0, 0.0f, 1.0f, SDL_MOUSEWHEEL_NORMAL);
    if (chg.c_down && b.c_down) SDL_SendMouseWheel(window, 0, 0.0f, -1.0f, SDL_MOUSEWHEEL_NORMAL);

    s_prev = b;
}

static void N64_PumpEvents(_THIS)
{
    SDL_Window *window = _this->windows;
    SDL_N64_RunTimers();
    SDL_N64_AudioPump();
    if (window) {
        if (SDL_GetMouseFocus() != window) {
            SDL_SetMouseFocus(window);
        }
        if (SDL_GetKeyboardFocus() != window) {
            SDL_SetKeyboardFocus(window);
        }
        pump_controller(window);
    }
}

/* -------------------------------------------------------------- device */

static int N64_VideoInit(_THIS)
{
    SDL_DisplayMode mode;
    SDL_Mouse *mouse;

    output_init();
    joypad_init();

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB888;
    mode.w = N64_W;
    mode.h = N64_H;
    mode.refresh_rate = 60;
    if (SDL_AddBasicVideoDisplay(&mode) < 0) {
        return -1;
    }
    SDL_AddDisplayMode(&_this->displays[0], &mode);

    mouse = SDL_GetMouse();
    mouse->CreateCursor = N64_CreateCursor;
    mouse->CreateSystemCursor = N64_CreateSystemCursor;
    mouse->ShowCursor = N64_ShowCursor;
    mouse->FreeCursor = N64_FreeCursor;
    mouse->WarpMouse = N64_WarpMouse;
    SDL_SetDefaultCursor(N64_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW));
    return 0;
}

static void N64_VideoQuit(_THIS)
{
    (void)_this;
}

static void N64_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *N64_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }
    device->VideoInit = N64_VideoInit;
    device->VideoQuit = N64_VideoQuit;
    device->PumpEvents = N64_PumpEvents;
    device->CreateWindowFramebuffer = N64_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = N64_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = N64_DestroyWindowFramebuffer;
    device->free = N64_DeleteDevice;
    return device;
}

VideoBootStrap DUMMY_bootstrap = {
    "n64", "Nintendo 64 video (640x480 shown at 640x240)",
    N64_CreateDevice,
    NULL
};
