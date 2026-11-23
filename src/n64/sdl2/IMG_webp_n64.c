/*
 * IMG_webp_n64.c -- SDL_image's WebP loader for the Nintendo 64.
 *
 * Replaces SDL_image's IMG_webp.c (same API, same libwebp decoder) with two
 * differences that matter on a console with ~29 MiB of (swapped) heap:
 *
 *  - Surfaces are decoded straight into SDL_PIXELFORMAT_ARGB8888, the format
 *    Wesnoth keeps all images in, so no full-size converted copy is made.
 *  - Images above k_max_pixels (2 MP) are decoded at a reduced scale by
 *    libwebp itself (Wesnoth's title screen background is 4096x2160, 35 MB
 *    decoded). The original size is recorded in the surface's userdata
 *    (bit 31 set, width << 16 | height) and the engine's texture keeps that
 *    as its draw size (src/sdl/texture.cpp), so layout -- including story
 *    map markers placed in image coordinates -- is unchanged; the screen is
 *    640x240 anyway.
 *
 * Animated WebP is not supported (Wesnoth does not use it).
 */
#include "SDL_image.h"

#ifdef LOAD_WEBP

#include <webp/decode.h>

static const int k_max_pixels = 2 * 1024 * 1024;

int IMG_InitWEBP(void) { return 0; }
void IMG_QuitWEBP(void) {}

int IMG_isWEBP(SDL_RWops *src)
{
    Sint64 start;
    Uint8 magic[20];
    int is_webp = 0;
    if (!src) return 0;
    start = SDL_RWtell(src);
    if (SDL_RWread(src, magic, 1, sizeof(magic)) == sizeof(magic)) {
        if (SDL_memcmp(magic, "RIFF", 4) == 0 && SDL_memcmp(magic + 8, "WEBP", 4) == 0 &&
            SDL_memcmp(magic + 12, "VP8", 3) == 0)
            is_webp = 1;
    }
    SDL_RWseek(src, start, RW_SEEK_SET);
    return is_webp;
}

SDL_Surface *IMG_LoadWEBP_RW(SDL_RWops *src)
{
    Sint64 start, size;
    Uint8 *data = NULL;
    SDL_Surface *surface = NULL;
    WebPDecoderConfig config;
    int w, h, k = 1;

    if (!src) return NULL;
    start = SDL_RWtell(src);
    size = SDL_RWsize(src) - start;
    if (size <= 12) { IMG_SetError("Invalid WEBP"); goto fail; }
    data = (Uint8 *)SDL_malloc((size_t)size);
    if (!data) { SDL_OutOfMemory(); goto fail; }
    if (SDL_RWread(src, data, 1, (size_t)size) != (size_t)size) { IMG_SetError("Failed to read WEBP"); goto fail; }

    if (!WebPInitDecoderConfig(&config) ||
        WebPGetFeatures(data, (size_t)size, &config.input) != VP8_STATUS_OK) {
        IMG_SetError("WebPGetFeatures has failed");
        goto fail;
    }
    w = config.input.width;
    h = config.input.height;
    while ((long)((w + k - 1) / k) * ((h + k - 1) / k) > k_max_pixels) ++k;

    surface = SDL_CreateRGBSurfaceWithFormat(0, (w + k - 1) / k, (h + k - 1) / k, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) goto fail;
    if (k > 1) {
        config.options.use_scaling = 1;
        config.options.scaled_width = surface->w;
        config.options.scaled_height = surface->h;
        surface->userdata = (void *)(uintptr_t)(0x80000000u | ((Uint32)(w & 0x7FFF) << 16) | (Uint32)(h & 0xFFFF));
    }
    /* ARGB8888 in memory on a big-endian CPU is A,R,G,B bytes */
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    config.output.colorspace = MODE_ARGB;
#else
    config.output.colorspace = MODE_BGRA;
#endif
    config.output.is_external_memory = 1;
    config.output.u.RGBA.rgba = (uint8_t *)surface->pixels;
    config.output.u.RGBA.stride = surface->pitch;
    config.output.u.RGBA.size = (size_t)surface->pitch * surface->h;
    if (WebPDecode(data, (size_t)size, &config) != VP8_STATUS_OK) {
        IMG_SetError("Failed to decode WEBP");
        goto fail;
    }
    WebPFreeDecBuffer(&config.output);
    SDL_free(data);
    return surface;

fail:
    SDL_FreeSurface(surface);
    SDL_free(data);
    SDL_RWseek(src, start, RW_SEEK_SET);
    return NULL;
}

IMG_Animation *IMG_LoadWEBPAnimation_RW(SDL_RWops *src)
{
    (void)src;
    IMG_SetError("Animated WEBP is not supported");
    return NULL;
}

#endif /* LOAD_WEBP */
