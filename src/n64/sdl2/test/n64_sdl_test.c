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

int main(void)
{
    debug_init_isviewer();
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
