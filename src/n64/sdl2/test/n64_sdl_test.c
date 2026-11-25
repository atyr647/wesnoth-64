/*
 * n64_sdl_test.c -- check the N64 SDL port on hardware or an emulator.
 *
 * Exercises what the Wesnoth engine relies on: window + software renderer,
 * target textures, alpha blending, color/alpha modulation, scaled copies,
 * fill rects, PNG decoding through SDL_image, colour cursors, timers and
 * controller input. Prints progress through the debug channel (IS-Viewer).
 */
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <libdragon.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>

extern const unsigned char test_cursor_png[], test_unit_png[], test_tile_png[];
extern const unsigned test_cursor_png_len, test_unit_png_len, test_tile_png_len;

static SDL_Surface* load(const unsigned char* p, unsigned n, const char* what)
{
    SDL_Surface* s = IMG_Load_RW(SDL_RWFromConstMem(p, (int)n), 1);
    fprintf(stderr, "[sdltest] %s: %s %dx%d\n", what, s ? "ok" : IMG_GetError(), s ? s->w : 0, s ? s->h : 0);
    return s;
}

static Uint32 tick(Uint32 interval, void* param)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_USEREVENT;
    SDL_PushEvent(&e);
    (void)param;
    return interval;
}

#ifdef PERSIST_TEST
/*
 * Persistence under texture pressure: Wesnoth's game display draws the map
 * once into an off-screen target (display::front_) and then only copies
 * regions of it to the screen buffer (video's render_texture_) each frame,
 * while three screen-sized targets and many terrain textures compete for the
 * renderer's RDRAM texture budget. Physical RAM is reserved first so the
 * budget is as small as in the game.
 */
static int persist_test(SDL_Renderer* ren, SDL_Surface* tile, SDL_Surface* unit)
{
    enum { NT = 120 };
    SDL_Texture* tiles[NT];
    for (int i = 0; i < NT; ++i) {
        SDL_Surface* s = SDL_ConvertSurfaceFormat(tile, SDL_PIXELFORMAT_ARGB8888, 0);
        /* tint each copy differently so every texture is distinct */
        Uint32* px = (Uint32*)s->pixels;
        for (int k = 0; k < s->w * s->h; ++k)
            if (px[k] >> 24) px[k] = (px[k] & 0xFF00FFFFu) | ((Uint32)((i * 2) & 255) << 16);
        tiles[i] = SDL_CreateTextureFromSurface(ren, s);
        SDL_FreeSurface(s);
    }
    SDL_Texture* tunit = SDL_CreateTextureFromSurface(ren, unit);
    SDL_Texture* screen = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 640, 480);
    SDL_Texture* front = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 640, 480);
    SDL_Texture* back = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 640, 480);

    /* back buffer: cleared once (as display's create_buffers does) */
    SDL_SetRenderTarget(ren, back);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    /* draw the "map" once into front */
    SDL_SetRenderTarget(ren, front);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    int n = 0;
    for (int y = -36; y < 480; y += 36)
        for (int x = -54; x < 640; x += 54) {
            SDL_Rect d = { x, y + ((x / 54) & 1) * 18, 72, 72 };
            SDL_RenderCopy(ren, tiles[n++ % NT], NULL, &d);
        }
    fprintf(stderr, "[sdltest] persist: map drawn with %d copies of %d textures\n", n, NT);

    for (int frame = 0;; ++frame) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
        /* expose: copy the map area from front to the screen buffer */
        SDL_SetRenderTarget(ren, screen);
        SDL_Rect area = { 0, 0, 480, 480 };
        SDL_RenderCopy(ren, front, &area, &area);
        /* sidebar and a moving unit drawn directly to the screen buffer */
        SDL_SetRenderDrawColor(ren, 90, 60, 30, 255);
        SDL_Rect side = { 480, 0, 160, 480 };
        SDL_RenderFillRect(ren, &side);
        SDL_Rect u = { 100 + (frame * 4) % 300, 200, 72, 72 };
        SDL_RenderCopy(ren, tunit, NULL, &u);
        /* some frames redraw a few map hexes into front, like invalidated hexes */
        if (frame % 3 == 0) {
            SDL_SetRenderTarget(ren, front);
            for (int k = 0; k < 6; ++k) {
                SDL_Rect d = { (k * 97 + frame * 13) % 420, (k * 61 + frame * 7) % 420, 72, 72 };
                SDL_RenderCopy(ren, tiles[(frame + k * 17) % NT], NULL, &d);
            }
        }
        /* present: screen buffer to the window */
        SDL_SetRenderTarget(ren, NULL);
        SDL_RenderCopy(ren, screen, NULL, NULL);
        SDL_RenderPresent(ren);
        if (frame < 3 || frame % 30 == 0) fprintf(stderr, "[sdltest] persist frame %d\n", frame);
    }
    return 0;
}
#endif

int main(void)
{
    debug_init_isviewer();
#ifdef PERSIST_TEST
    /* the texture budget Wesnoth's game gets (2.4 MiB of RAM left) */
    SDL_SetHint("N64_TEXTURE_BUDGET_KB", "1916");
#endif
    fprintf(stderr, "[sdltest] start\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[sdltest] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("test", 0, 0, 640, 480, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    SDL_RendererInfo info;
    SDL_GetRendererInfo(ren, &info);
    fprintf(stderr, "[sdltest] window %p renderer %s (%s)\n", (void*)win, info.name, SDL_GetCurrentVideoDriver());

    SDL_Surface* cur = load(test_cursor_png, test_cursor_png_len, "cursor");
    SDL_Surface* unit = load(test_unit_png, test_unit_png_len, "unit");
    SDL_Surface* tile = load(test_tile_png, test_tile_png_len, "tile");
    if (cur) SDL_SetCursor(SDL_CreateColorCursor(cur, 1, 1));
#ifdef PERSIST_TEST
    return persist_test(ren, tile, unit);
#endif
    SDL_Texture* tunit = unit ? SDL_CreateTextureFromSurface(ren, unit) : NULL;
    SDL_Texture* ttile = tile ? SDL_CreateTextureFromSurface(ren, tile) : NULL;
    SDL_Texture* target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 640, 480);
    /* A window-style buffer: transparent target with blended content,
       composited over the scene (what Wesnoth's GUI windows do). */
    SDL_Texture* winbuf = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 240, 160);
    SDL_SetTextureBlendMode(winbuf, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);
    SDL_AddTimer(500, tick, NULL);

    int frames = 0, ticks = 0, clicks = 0, keys = 0;
    Uint32 t0 = SDL_GetTicks();
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_USEREVENT) ++ticks;
            if (e.type == SDL_MOUSEBUTTONDOWN) ++clicks;
            if (e.type == SDL_KEYDOWN) ++keys;
        }
        SDL_SetRenderTarget(ren, target);
        SDL_SetRenderDrawColor(ren, 20, 30, 60, 255);
        SDL_RenderClear(ren);
        for (int y = 0; y < 480; y += 54)
            for (int x = 0; x < 640; x += 72) {
                SDL_Rect d = { x + ((y / 54) & 1) * 36, y, 72, 72 };
                if (ttile) SDL_RenderCopy(ren, ttile, NULL, &d);
            }
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 128);
        SDL_Rect panel = { 40, 300, 560, 140 };
        SDL_RenderFillRect(ren, &panel);
        if (tunit) {
            SDL_Rect d = { 100 + (frames % 200), 100, 72, 72 };
            SDL_RenderCopy(ren, tunit, NULL, &d);
            SDL_Rect big = { 300, 60, 144, 144 };
            SDL_SetTextureColorMod(tunit, 255, 128, 128);
            SDL_RenderCopy(ren, tunit, NULL, &big);
            SDL_SetTextureColorMod(tunit, 255, 255, 255);
            SDL_SetTextureAlphaMod(tunit, 96);
            SDL_Rect ghost = { 460, 100, 72, 72 };
            SDL_RenderCopyEx(ren, tunit, NULL, &ghost, 0.0, NULL, SDL_FLIP_HORIZONTAL);
            SDL_SetTextureAlphaMod(tunit, 255);
        }
        /* window buffer: clear transparent, opaque box, half-transparent box, sprite */
        SDL_SetRenderTarget(ren, winbuf);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        { SDL_Rect a = { 10, 10, 100, 60 }, b = { 80, 50, 140, 90 };
          SDL_SetRenderDrawColor(ren, 20, 40, 120, 255); SDL_RenderFillRect(ren, &a);
          SDL_SetRenderDrawColor(ren, 200, 160, 40, 128); SDL_RenderFillRect(ren, &b); }
        if (tunit) { SDL_Rect d = { 150, 10, 72, 72 }; SDL_RenderCopy(ren, tunit, NULL, &d); }
        SDL_SetRenderTarget(ren, target);
        { SDL_Rect w = { 360, 250, 240, 160 }; SDL_RenderCopy(ren, winbuf, NULL, &w); }
        SDL_SetRenderTarget(ren, NULL);
        SDL_RenderCopy(ren, target, NULL, NULL);
        SDL_RenderPresent(ren);
        ++frames;
        if (frames <= 5 || frames % 60 == 0) {
            Uint32 t = SDL_GetTicks() - t0;
            fprintf(stderr, "[sdltest] %d frames in %lu ms, timer ticks %d, clicks %d, keys %d\n",
                    frames, (unsigned long)t, ticks, clicks, keys);
        }
    }
}

/* The engine provides these (swappable heap); the test uses the C heap. */
#include <stdlib.h>
void* n64_vmalloc(size_t n) { return malloc(n); }
void* n64_vcalloc(size_t n, size_t s) { return calloc(n, s); }
void* n64_vrealloc(void* p, size_t n) { return realloc(p, n); }
void n64_vfree(void* p) { free(p); }
