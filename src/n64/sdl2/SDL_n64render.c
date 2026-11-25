/*
 * SDL_n64render.c -- SDL render driver for the Nintendo 64 RDP (libdragon rdpq).
 *
 * The window is 640x480 to the program; the video interface shows 640x240
 * progressive. Everything is therefore stored and drawn at half height:
 *
 *  - render targets (and the screen) are w x ceil(h/2) RGBA32 (screen RGBA16)
 *    surfaces; every y coordinate is halved when drawing;
 *  - other textures are box-filtered to half height (premultiplied, so edges
 *    of transparent sprites stay clean) when their pixels are set -- each
 *    output row is the average of the two rows it shows, exactly what the
 *    software path's row-pair averaging produces -- and larger-than-screen
 *    images are reduced to at most 640x240.
 *
 * Texture pixels live in the swappable heap; an RDRAM copy is made when a
 * texture is drawn (LRU cache within a memory budget; render targets are
 * copied back when evicted).
 *
 * The RDP does not compute destination alpha (a 32-bit framebuffer's fourth
 * byte receives coverage bits). Render targets that are cleared to a
 * non-opaque colour (Wesnoth's GUI window buffers) keep a companion RGBA16
 * "alpha" surface: every draw is repeated into it with the blender computing
 * a_out = a_src + a_dst * (1 - a_src) -- SDL_BLENDMODE_BLEND's alpha rule --
 * and the result is merged into the texture's alpha bytes before it is drawn.
 *
 * Registered in SDL's render driver table as PS2_RenderDriver
 * (SDL_VIDEO_RENDER_PS2 in SDL_config.h), so SDL itself is unmodified.
 */
#include "SDL_internal.h"

#include "SDL_hints.h"
#include "render/SDL_sysrender.h"
#include "video/SDL_sysvideo.h"

#include <libdragon.h>
#include <malloc.h>
#include <math.h>

#define N64R_OUT_W 640
#define N64R_OUT_H 480
#define N64R_PHYS_H 240

void SDL_N64_GetCursor(SDL_Surface **image, int *hot_x, int *hot_y, int *x, int *y);
void SDL_N64_TakeDisplay(void);

/* ------------------------------------------------------------ textures */

typedef struct N64_Texture
{
    int w, h;                 /* SDL size */
    int sw, sh;               /* stored size */
    float fx, fy;             /* stored / SDL size */
    SDL_bool target;
    SDL_bool track_alpha;
    Uint8 *cpu;               /* stored pixels, RGBA32 (R,G,B,A bytes), sw*sh*4 */
    Uint8 *staging;           /* SDL-size pixels while locked */
    SDL_Rect locked;
    surface_t gpu;            /* RDRAM copy (gpu.buffer NULL when not resident) */
    surface_t alpha;          /* RDRAM RGBA16, R = alpha (track_alpha targets) */
    SDL_bool cpu_stale;       /* GPU copy is newer (target was drawn into) */
    SDL_bool gpu_stale;       /* CPU copy is newer */
    SDL_bool alpha_dirty;     /* alpha surface not yet merged into gpu */
    Uint32 last_used;
    struct N64_Texture *prev, *next;
} N64_Texture;

typedef struct
{
    surface_t *display;       /* current frame's display buffer (NULL until drawn) */
    N64_Texture *target;      /* current render target (NULL = screen) */
    SDL_Rect viewport;
    SDL_bool clip_enabled;
    SDL_Rect clip;
    SDL_Color draw_color;
    Uint32 frame;
    size_t budget, used;      /* RDRAM texture memory */
    N64_Texture *lru_head, *lru_tail;  /* head = most recently used */
    surface_t cursor_tex;
    SDL_Surface *cursor_src;
    SDL_bool attached;
} N64_RenderData;

/* diagnostics: work done since the last log line */
static struct { unsigned copies, skipped, skip_room, skip_self, skip_tiny, fills, clears, queues, uploads; uint64_t last_log; } s_diag;

static size_t tex_bytes(const N64_Texture *t)
{
    return (size_t)t->sw * t->sh * 4 + (t->track_alpha ? (size_t)t->sw * t->sh * 2 : 0);
}

static void lru_unlink(N64_RenderData *d, N64_Texture *t)
{
    if (t->prev) t->prev->next = t->next; else if (d->lru_head == t) d->lru_head = t->next;
    if (t->next) t->next->prev = t->prev; else if (d->lru_tail == t) d->lru_tail = t->prev;
    t->prev = t->next = NULL;
}

static void lru_push_front(N64_RenderData *d, N64_Texture *t)
{
    t->prev = NULL;
    t->next = d->lru_head;
    if (d->lru_head) d->lru_head->prev = t;
    d->lru_head = t;
    if (!d->lru_tail) d->lru_tail = t;
}

static void detach(N64_RenderData *d)
{
    if (d->attached) {
        rdpq_detach();
        d->attached = SDL_FALSE;
    }
}

/* Copy a target's RDRAM contents back to its CPU copy (alpha merged). */
static void merge_alpha(N64_Texture *t);
static void download(N64_Texture *t)
{
    int y;
    if (!t->gpu.buffer || !t->cpu_stale) return;
    if (!t->cpu) t->cpu = (Uint8 *)SDL_malloc((size_t)t->sw * t->sh * 4);
    if (!t->cpu) return;
    merge_alpha(t);
    for (y = 0; y < t->sh; ++y)
        SDL_memcpy(t->cpu + y * t->sw * 4, (Uint8 *)t->gpu.buffer + y * t->gpu.stride, t->sw * 4);
    t->cpu_stale = SDL_FALSE;
}

static void evict(N64_RenderData *d, N64_Texture *t)
{
    if (!t->gpu.buffer) return;
    if (d->target == t) return;
    detach(d);
    rspq_wait();
    download(t);
    surface_free(&t->gpu);
    t->gpu.buffer = NULL;
    if (t->alpha.buffer) { surface_free(&t->alpha); t->alpha.buffer = NULL; }
    d->used -= tex_bytes(t);
    lru_unlink(d, t);
}

static SDL_bool make_room(N64_RenderData *d, size_t need, N64_Texture *keep)
{
    N64_Texture *t = d->lru_tail;
    while (d->used + need > d->budget && t) {
        N64_Texture *prev = t->prev;
        if (t != keep && t != d->target) evict(d, t);
        t = prev;
    }
    return d->used + need <= d->budget;
}

/* Make sure t has an RDRAM copy that is up to date. */
static SDL_bool resident(N64_RenderData *d, N64_Texture *t)
{
    int y;
    t->last_used = d->frame;
    if (t->gpu.buffer) {
        lru_unlink(d, t);
        lru_push_front(d, t);
        if (t->gpu_stale && t->cpu) {
            rspq_wait();
            for (y = 0; y < t->sh; ++y)
                SDL_memcpy((Uint8 *)t->gpu.buffer + y * t->gpu.stride, t->cpu + y * t->sw * 4, t->sw * 4);
            t->gpu_stale = SDL_FALSE;
        }
        return SDL_TRUE;
    }
    if (!make_room(d, tex_bytes(t), t)) {
        static int logged;
        if (logged < 20) {
            ++logged;
            debugf("[n64render] no room for %dx%d texture (%lu KiB; %lu of %lu KiB in use)\n", t->sw, t->sh,
                   (unsigned long)(tex_bytes(t) >> 10), (unsigned long)(d->used >> 10), (unsigned long)(d->budget >> 10));
        }
        SDL_SetError("n64 renderer: out of texture memory");
        return SDL_FALSE;
    }
    t->gpu = surface_alloc(FMT_RGBA32, t->sw, t->sh);
    if (!t->gpu.buffer) {
        SDL_OutOfMemory();
        return SDL_FALSE;
    }
    if (t->track_alpha) t->alpha = surface_alloc(FMT_RGBA16, t->sw, t->sh);
    d->used += tex_bytes(t);
    lru_push_front(d, t);
    if (t->cpu) {
        for (y = 0; y < t->sh; ++y)
            SDL_memcpy((Uint8 *)t->gpu.buffer + y * t->gpu.stride, t->cpu + y * t->sw * 4, t->sw * 4);
        if (t->alpha.buffer) {
            /* rebuild the alpha surface from the stored alpha bytes */
            int x;
            for (y = 0; y < t->sh; ++y) {
                const Uint8 *s = t->cpu + y * t->sw * 4;
                uint16_t *a = (uint16_t *)((Uint8 *)t->alpha.buffer + y * t->alpha.stride);
                for (x = 0; x < t->sw; ++x) {
                    const unsigned v = s[x * 4 + 3] >> 3;
                    a[x] = (uint16_t)((v << 11) | (v << 6) | (v << 1) | 1);
                }
            }
        }
    } else {
        for (y = 0; y < t->sh; ++y) SDL_memset((Uint8 *)t->gpu.buffer + y * t->gpu.stride, 0, t->sw * 4);
        if (t->alpha.buffer)
            for (y = 0; y < t->sh; ++y) SDL_memset((Uint8 *)t->alpha.buffer + y * t->alpha.stride, 0, t->sw * 2);
    }
    t->gpu_stale = SDL_FALSE;
    t->alpha_dirty = SDL_FALSE;
    return SDL_TRUE;
}

/* Alpha surface (5-bit, in R) -> alpha bytes of the RGBA32 copy. */
static void merge_alpha(N64_Texture *t)
{
    int x, y;
    if (!t->track_alpha || !t->alpha_dirty || !t->alpha.buffer) return;
    rspq_wait();
    for (y = 0; y < t->sh; ++y) {
        Uint8 *p = (Uint8 *)t->gpu.buffer + y * t->gpu.stride;
        const uint16_t *a = (const uint16_t *)((Uint8 *)t->alpha.buffer + y * t->alpha.stride);
        for (x = 0; x < t->sw; ++x) {
            const unsigned v = (a[x] >> 11) & 31;
            p[x * 4 + 3] = (Uint8)((v << 3) | (v >> 2));
        }
    }
    t->alpha_dirty = SDL_FALSE;
}

/* Box-filter SDL-size pixels (any 32-bit format with known masks) into the
   stored RGBA32 buffer, region rect (SDL coordinates). Premultiplied average. */
static void store_pixels(N64_Texture *t, const SDL_Rect *rect, const void *pixels, int pitch, Uint32 format)
{
    SDL_PixelFormat *fmt = SDL_AllocFormat(format);
    int X0, X1, Y0, Y1, X, Y;
    if (!fmt) return;
    X0 = (int)floorf(rect->x * t->fx);
    X1 = (int)ceilf((rect->x + rect->w) * t->fx);
    Y0 = (int)floorf(rect->y * t->fy);
    Y1 = (int)ceilf((rect->y + rect->h) * t->fy);
    if (X1 > t->sw) X1 = t->sw;
    if (Y1 > t->sh) Y1 = t->sh;
    for (Y = Y0; Y < Y1; ++Y) {
        int y0 = (int)(Y / t->fy), y1 = (int)((Y + 1) / t->fy);
        if (y1 <= y0) y1 = y0 + 1;
        if (y0 < rect->y) y0 = rect->y;
        if (y1 > rect->y + rect->h) y1 = rect->y + rect->h;
        for (X = X0; X < X1; ++X) {
            int x0 = (int)(X / t->fx), x1 = (int)((X + 1) / t->fx), x, y;
            Uint32 sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
            Uint8 *out = t->cpu + (Y * t->sw + X) * 4;
            if (x1 <= x0) x1 = x0 + 1;
            if (x0 < rect->x) x0 = rect->x;
            if (x1 > rect->x + rect->w) x1 = rect->x + rect->w;
            for (y = y0; y < y1; ++y) {
                const Uint32 *row = (const Uint32 *)((const Uint8 *)pixels + (y - rect->y) * pitch);
                for (x = x0; x < x1; ++x) {
                    Uint8 r, g, b, a;
                    SDL_GetRGBA(row[x - rect->x], fmt, &r, &g, &b, &a);
                    sr += r * a; sg += g * a; sb += b * a; sa += a;
                    ++n;
                }
            }
            if (!n) continue;
            if (sa) {
                out[0] = (Uint8)(sr / sa);
                out[1] = (Uint8)(sg / sa);
                out[2] = (Uint8)(sb / sa);
            } else {
                out[0] = out[1] = out[2] = 0;
            }
            out[3] = (Uint8)(sa / n);
        }
    }
    SDL_FreeFormat(fmt);
}

static int N64R_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    N64_Texture *t = (N64_Texture *)SDL_calloc(1, sizeof(*t));
    (void)renderer;
    if (!t) return SDL_OutOfMemory();
    t->w = texture->w;
    t->h = texture->h;
    t->target = texture->access == SDL_TEXTUREACCESS_TARGET;
    t->fx = 1.0f;
    t->fy = 0.5f;
    if (!t->target) {
        if (t->w > N64R_OUT_W) t->fx = (float)N64R_OUT_W / t->w;
        if (t->h * t->fy > N64R_PHYS_H) t->fy = (float)N64R_PHYS_H / t->h;
    }
    t->sw = (int)ceilf(t->w * t->fx);
    t->sh = (int)ceilf(t->h * t->fy);
    if (t->sw < 1) t->sw = 1;
    if (t->sh < 1) t->sh = 1;
    t->track_alpha = SDL_FALSE;      /* enabled by the first transparent clear */
    if (!t->target) {
        t->cpu = (Uint8 *)SDL_calloc((size_t)t->sw * t->sh, 4);
        if (!t->cpu) { SDL_free(t); return SDL_OutOfMemory(); }
    }
    texture->driverdata = t;
    return 0;
}

static int N64R_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect,
                              const void *pixels, int pitch)
{
    N64_Texture *t = (N64_Texture *)texture->driverdata;
    (void)renderer;
    if (!t->cpu && t->gpu.buffer) {
        /* resident target without a CPU copy: fetch its current contents */
        rspq_wait();
        download(t);
    }
    if (!t->cpu) {
        t->cpu = (Uint8 *)SDL_calloc((size_t)t->sw * t->sh, 4);
        if (!t->cpu) return SDL_OutOfMemory();
    }
    store_pixels(t, rect, pixels, pitch, texture->format);
    t->gpu_stale = SDL_TRUE;
    t->cpu_stale = SDL_FALSE;
    return 0;
}

static int N64R_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect,
                            void **pixels, int *pitch)
{
    N64_Texture *t = (N64_Texture *)texture->driverdata;
    (void)renderer;
    t->staging = (Uint8 *)SDL_calloc((size_t)rect->w * rect->h, 4);
    if (!t->staging) return SDL_OutOfMemory();
    t->locked = *rect;
    *pixels = t->staging;
    *pitch = rect->w * 4;
    return 0;
}

static void N64R_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    N64_Texture *t = (N64_Texture *)texture->driverdata;
    if (!t->staging) return;
    N64R_UpdateTexture(renderer, texture, &t->locked, t->staging, t->locked.w * 4);
    SDL_free(t->staging);
    t->staging = NULL;
}

static void N64R_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    (void)renderer; (void)texture; (void)scaleMode;   /* read from the texture at draw time */
}

static int N64R_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer; (void)texture;                    /* applied when the queue runs */
    return 0;
}

static void N64R_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    N64_RenderData *d = (N64_RenderData *)renderer->driverdata;
    N64_Texture *t = (N64_Texture *)texture->driverdata;
    if (!t) return;
    if (d->target == t) { detach(d); d->target = NULL; }
    if (t->gpu.buffer) {
        rspq_wait();
        surface_free(&t->gpu);
        if (t->alpha.buffer) surface_free(&t->alpha);
        d->used -= tex_bytes(t);
        lru_unlink(d, t);
    }
    SDL_free(t->cpu);
    SDL_free(t->staging);
    SDL_free(t);
    texture->driverdata = NULL;
}

/* ------------------------------------------------------- command queue */

typedef struct { float sx, sy, sw, sh, dx, dy, dw, dh, angle, cx, cy; int flip; } N64_CopyCmd;

static int N64R_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd) { (void)renderer; (void)cmd; return 0; }
static int N64R_QueueSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd) { (void)renderer; (void)cmd; return 0; }

static int N64R_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FRect *rects, int count)
{
    SDL_FRect *v = (SDL_FRect *)SDL_AllocateRenderVertices(renderer, count * sizeof(SDL_FRect), 0, &cmd->data.draw.first);
    int i;
    if (!v) return -1;
    cmd->data.draw.count = count;
    for (i = 0; i < count; ++i) v[i] = rects[i];
    return 0;
}

static int N64R_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    SDL_FRect *v = (SDL_FRect *)SDL_AllocateRenderVertices(renderer, count * sizeof(SDL_FRect), 0, &cmd->data.draw.first);
    int i;
    if (!v) return -1;
    cmd->data.draw.count = count;
    for (i = 0; i < count; ++i) { v[i].x = points[i].x; v[i].y = points[i].y; v[i].w = 1; v[i].h = 1; }
    return 0;
}

/* Lines as runs of 1x1 rectangles (horizontal/vertical ones as single rects). */
static int N64R_QueueDrawLines(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    int i, n = 0, cap = 0;
    SDL_FRect *tmp = NULL, *v;
    for (i = 0; i + 1 < count; ++i) {
        int x0 = (int)points[i].x, y0 = (int)points[i].y, x1 = (int)points[i + 1].x, y1 = (int)points[i + 1].y;
        int dx = SDL_abs(x1 - x0), dy = -SDL_abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
        if (x0 == x1 || y0 == y1) {
            if (n + 1 > cap) { cap = cap * 2 + 16; tmp = (SDL_FRect *)SDL_realloc(tmp, cap * sizeof(SDL_FRect)); }
            tmp[n].x = (float)SDL_min(x0, x1); tmp[n].y = (float)SDL_min(y0, y1);
            tmp[n].w = (float)(dx + 1); tmp[n].h = (float)(-dy + 1);
            ++n;
            continue;
        }
        for (;;) {
            if (n + 1 > cap) { cap = cap * 2 + 64; tmp = (SDL_FRect *)SDL_realloc(tmp, cap * sizeof(SDL_FRect)); }
            tmp[n].x = (float)x0; tmp[n].y = (float)y0; tmp[n].w = 1; tmp[n].h = 1;
            ++n;
            if (x0 == x1 && y0 == y1) break;
            {
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        }
    }
    v = (SDL_FRect *)SDL_AllocateRenderVertices(renderer, (n ? n : 1) * sizeof(SDL_FRect), 0, &cmd->data.draw.first);
    if (!v) { SDL_free(tmp); return -1; }
    cmd->data.draw.count = n;
    if (n) SDL_memcpy(v, tmp, n * sizeof(SDL_FRect));
    SDL_free(tmp);
    return 0;
}

static int N64R_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                          const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    N64_CopyCmd *c = (N64_CopyCmd *)SDL_AllocateRenderVertices(renderer, sizeof(*c), 0, &cmd->data.draw.first);
    (void)texture;
    if (!c) return -1;
    cmd->data.draw.count = 1;
    c->sx = (float)srcrect->x; c->sy = (float)srcrect->y; c->sw = (float)srcrect->w; c->sh = (float)srcrect->h;
    c->dx = dstrect->x; c->dy = dstrect->y; c->dw = dstrect->w; c->dh = dstrect->h;
    c->angle = 0; c->cx = c->cy = 0; c->flip = 0;
    return 0;
}

static int N64R_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                            const SDL_Rect *srcrect, const SDL_FRect *dstrect, const double angle,
                            const SDL_FPoint *center, const SDL_RendererFlip flip, float scale_x, float scale_y)
{
    N64_CopyCmd *c = (N64_CopyCmd *)SDL_AllocateRenderVertices(renderer, sizeof(*c), 0, &cmd->data.draw.first);
    (void)texture;
    if (!c) return -1;
    cmd->data.draw.count = 1;
    c->sx = (float)srcrect->x; c->sy = (float)srcrect->y; c->sw = (float)srcrect->w; c->sh = (float)srcrect->h;
    c->dx = dstrect->x * scale_x; c->dy = dstrect->y * scale_y;
    c->dw = dstrect->w * scale_x; c->dh = dstrect->h * scale_y;
    c->angle = (float)angle;
    c->cx = center ? center->x * scale_x : c->dw / 2;
    c->cy = center ? center->y * scale_y : c->dh / 2;
    c->flip = (int)flip;
    return 0;
}

/* Axis-aligned textured quads (Wesnoth's draw::smooth_shaded) as per-row
   copies with the interpolated vertex colour. Stored: rect + 4 colours + uv. */
typedef struct { float x0, y0, x1, y1, u0, v0, u1, v1; SDL_Color c[4]; } N64_QuadCmd;

static int N64R_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                              const float *xy, int xy_stride, const SDL_Color *color, int color_stride,
                              const float *uv, int uv_stride, int num_vertices, const void *indices,
                              int num_indices, int size_indices, float scale_x, float scale_y)
{
    /* only the 4-vertex rectangle Wesnoth uses */
    N64_QuadCmd *q;
    int i;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    (void)indices; (void)num_indices; (void)size_indices;
    if (num_vertices != 4 || !texture) return SDL_Unsupported();
    q = (N64_QuadCmd *)SDL_AllocateRenderVertices(renderer, sizeof(*q), 0, &cmd->data.draw.first);
    if (!q) return -1;
    cmd->data.draw.count = 1;
    for (i = 0; i < 4; ++i) {
        const float *p = (const float *)((const char *)xy + i * xy_stride);
        const float *t = (const float *)((const char *)uv + i * uv_stride);
        const SDL_Color *col = (const SDL_Color *)((const char *)color + i * color_stride);
        const float x = p[0] * scale_x, y = p[1] * scale_y;
        /* corner index: 0 TL, 1 TR, 2 BL, 3 BR (by position) */
        minx = SDL_min(minx, x); maxx = SDL_max(maxx, x);
        miny = SDL_min(miny, y); maxy = SDL_max(maxy, y);
        (void)t; (void)col;
    }
    q->x0 = minx; q->y0 = miny; q->x1 = maxx; q->y1 = maxy;
    q->u0 = q->v0 = 1e9f; q->u1 = q->v1 = -1e9f;
    for (i = 0; i < 4; ++i) {
        const float *p = (const float *)((const char *)xy + i * xy_stride);
        const float *t = (const float *)((const char *)uv + i * uv_stride);
        const SDL_Color *col = (const SDL_Color *)((const char *)color + i * color_stride);
        const int right = p[0] * scale_x > (minx + maxx) / 2, bottom = p[1] * scale_y > (miny + maxy) / 2;
        q->c[bottom * 2 + right] = *col;
        q->u0 = SDL_min(q->u0, t[0]); q->u1 = SDL_max(q->u1, t[0]);
        q->v0 = SDL_min(q->v0, t[1]); q->v1 = SDL_max(q->v1, t[1]);
    }
    return 0;
}

/* --------------------------------------------------------------- drawing */

static void attach_target(N64_RenderData *d, N64_Texture *t)
{
    if (d->attached) return;
    if (t) {
        rdpq_attach(&t->gpu, NULL);
    } else {
        if (!d->display) {
            SDL_N64_TakeDisplay();
            d->display = display_get();
        }
        rdpq_attach(d->display, NULL);
    }
    d->attached = SDL_TRUE;
}

static void set_scissor(N64_RenderData *d)
{
    int tw = d->target ? d->target->w : N64R_OUT_W, th = d->target ? d->target->h : N64R_OUT_H;
    int x0 = d->viewport.x, y0 = d->viewport.y;
    int x1 = d->viewport.x + (d->viewport.w > 0 ? d->viewport.w : tw), y1 = d->viewport.y + (d->viewport.h > 0 ? d->viewport.h : th);
    if (d->clip_enabled) {
        x0 = SDL_max(x0, d->viewport.x + d->clip.x);
        y0 = SDL_max(y0, d->viewport.y + d->clip.y);
        x1 = SDL_min(x1, d->viewport.x + d->clip.x + d->clip.w);
        y1 = SDL_min(y1, d->viewport.y + d->clip.y + d->clip.h);
    }
    x0 = SDL_max(x0, 0); y0 = SDL_max(y0, 0);
    x1 = SDL_min(x1, tw); y1 = SDL_min(y1, th);
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    rdpq_set_scissor(x0, y0 / 2, x1, (y1 + 1) / 2);
}

static void blend_mode(SDL_BlendMode b)
{
    switch (b) {
    case SDL_BLENDMODE_BLEND: rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY); break;
    case SDL_BLENDMODE_ADD: rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE); break;
    default: rdpq_mode_blender(0); break;
    }
}

/* Draw the companion alpha pass? (only BLEND and NONE change SDL dst alpha) */
static SDL_bool alpha_pass(N64_RenderData *d, SDL_BlendMode b)
{
    return d->target && d->target->track_alpha && (b == SDL_BLENDMODE_BLEND || b == SDL_BLENDMODE_NONE);
}

static void fill_rects(N64_RenderData *d, const SDL_FRect *r, int n, SDL_Color c, SDL_BlendMode b)
{
    int pass, i;
    for (pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (!alpha_pass(d, b)) break;
            detach(d);
            rdpq_attach(&d->target->alpha, NULL);
            set_scissor(d);
            rdpq_set_mode_standard();
            /* RGB = 1, alpha = colour alpha; blend: 1*a + dst*(1-a)  (NONE: a) */
            rdpq_mode_combiner(b == SDL_BLENDMODE_NONE ? RDPQ_COMBINER1((ONE, 0, PRIM_ALPHA, 0), (0, 0, 0, ONE))
                                                       : RDPQ_COMBINER1((0, 0, 0, ONE), (0, 0, 0, PRIM)));
            blend_mode(b);
            rdpq_set_prim_color(RGBA32(255, 255, 255, c.a));
            d->target->alpha_dirty = SDL_TRUE;
        } else {
            attach_target(d, d->target);
            set_scissor(d);
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            blend_mode(b);
            rdpq_set_prim_color(RGBA32(c.r, c.g, c.b, c.a));
        }
        for (i = 0; i < n; ++i) {
            const float x0 = d->viewport.x + r[i].x, y0 = d->viewport.y + r[i].y;
            rdpq_fill_rectangle(x0, y0 * 0.5f, x0 + r[i].w, (y0 + r[i].h) * 0.5f);
        }
        if (pass == 1) {
            rdpq_detach();
            d->attached = SDL_FALSE;
        }
    }
}

static void clear(N64_RenderData *d, SDL_Color c)
{
    /* A target cleared to a non-opaque colour is one whose alpha matters when
       it is composited later (GUI window buffers): start tracking it. The
       main screen buffer is only ever cleared opaque and needs none. */
    if (d->target && !d->target->track_alpha && c.a < 255) {
        N64_Texture *t = d->target;
        const size_t extra = (size_t)t->sw * t->sh * 2;
        detach(d);
        make_room(d, extra, t);
        t->alpha = surface_alloc(FMT_RGBA16, t->sw, t->sh);
        if (t->alpha.buffer) {
            d->used += extra;
            t->track_alpha = SDL_TRUE;
        }
    }
    attach_target(d, d->target);
    rdpq_set_scissor(0, 0, d->target ? d->target->sw : N64R_OUT_W, d->target ? d->target->sh : N64R_PHYS_H);
    rdpq_set_mode_fill(RGBA32(c.r, c.g, c.b, c.a));
    rdpq_fill_rectangle(0, 0, d->target ? d->target->sw : N64R_OUT_W, d->target ? d->target->sh : N64R_PHYS_H);
    if (d->target && d->target->track_alpha) {
        detach(d);
        rdpq_attach(&d->target->alpha, NULL);
        rdpq_set_mode_fill(RGBA32(c.a, c.a, c.a, 255));
        rdpq_fill_rectangle(0, 0, d->target->sw, d->target->sh);
        rdpq_detach();
        d->attached = SDL_FALSE;
        d->target->alpha_dirty = SDL_TRUE;
    }
    if (d->target) d->target->cpu_stale = SDL_TRUE;
}

static void copy(N64_RenderData *d, SDL_Texture *texture, const N64_CopyCmd *c, SDL_Color mod,
                 SDL_BlendMode b, float row_from, float row_to)
{
    N64_Texture *t = (N64_Texture *)texture->driverdata;
    rdpq_blitparms_t p;
    int pass;
    const SDL_bool modulate = mod.r != 255 || mod.g != 255 || mod.b != 255 || mod.a != 255;
    float s0, t0, sw, sh, dx, dy, dw, dh;
    if (!t || !resident(d, t)) { ++s_diag.skipped; ++s_diag.skip_room; return; }
    if (t == d->target) { ++s_diag.skipped; ++s_diag.skip_self; return; }   /* drawing a target into itself */
    ++s_diag.copies;
    merge_alpha(t);

    s0 = c->sx * t->fx; t0 = c->sy * t->fy;
    sw = c->sw * t->fx; sh = c->sh * t->fy;
    dx = d->viewport.x + c->dx; dy = (d->viewport.y + c->dy) * 0.5f;
    dw = c->dw; dh = c->dh * 0.5f;
    if (row_to > row_from) {                  /* a horizontal band of the copy */
        t0 += sh * row_from;
        dy += dh * row_from;
        sh *= (row_to - row_from);
        dh *= (row_to - row_from);
    }
    if (sw < 0.5f || sh < 0.5f || dw <= 0 || dh <= 0) { ++s_diag.skipped; ++s_diag.skip_tiny; return; }

    SDL_zero(p);
    p.s0 = (int)s0; p.t0 = (int)t0;
    p.width = (int)(sw + 0.5f); p.height = (int)(sh + 0.5f);
    if (p.width < 1) p.width = 1;
    if (p.height < 1) p.height = 1;
    if (p.s0 + p.width > t->sw) p.width = t->sw - p.s0;
    if (p.t0 + p.height > t->sh) p.height = t->sh - p.t0;
    if (p.width <= 0 || p.height <= 0) return;
    p.scale_x = dw / p.width;
    p.scale_y = dh / p.height;
    p.flip_x = (c->flip & SDL_FLIP_HORIZONTAL) != 0;
    p.flip_y = (c->flip & SDL_FLIP_VERTICAL) != 0;
    if (c->angle != 0) {
        p.theta = -c->angle * (float)M_PI / 180.0f;
        p.cx = (int)(c->cx / p.scale_x);
        p.cy = (int)(c->cy * 0.5f / p.scale_y);
        dx += c->cx;
        dy += c->cy * 0.5f;
    }
    p.filtering = texture->scaleMode != SDL_ScaleModeNearest &&
                  (fabsf(p.scale_x - 1.0f) > 0.01f || fabsf(p.scale_y - 1.0f) > 0.01f || c->angle != 0);

    for (pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            if (!alpha_pass(d, b)) break;
            detach(d);
            rdpq_attach(&d->target->alpha, NULL);
            set_scissor(d);
            rdpq_set_mode_standard();
            rdpq_mode_combiner(b == SDL_BLENDMODE_NONE
                ? RDPQ_COMBINER1((ONE, 0, TEX0_ALPHA, 0), (0, 0, 0, ONE))
                : RDPQ_COMBINER1((0, 0, 0, ONE), (TEX0, 0, PRIM, 0)));
            rdpq_set_prim_color(RGBA32(255, 255, 255, mod.a));
            d->target->alpha_dirty = SDL_TRUE;
        } else {
            attach_target(d, d->target);
            set_scissor(d);
            rdpq_set_mode_standard();
            if (modulate) {
                rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, PRIM, 0), (TEX0, 0, PRIM, 0)));
                rdpq_set_prim_color(RGBA32(mod.r, mod.g, mod.b, mod.a));
            } else {
                rdpq_mode_combiner(RDPQ_COMBINER_TEX);
            }
        }
        blend_mode(b);
        rdpq_mode_filter(p.filtering ? FILTER_BILINEAR : FILTER_POINT);
        rdpq_tex_blit(&t->gpu, dx, dy, &p);
        if (pass == 1) {
            rdpq_detach();
            d->attached = SDL_FALSE;
        }
    }
    if (d->target) d->target->cpu_stale = SDL_TRUE;
}

static int N64R_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    N64_RenderData *d = (N64_RenderData *)renderer->driverdata;
    N64_Texture *want = renderer->target ? (N64_Texture *)renderer->target->driverdata : NULL;
    (void)vertsize;
    ++s_diag.queues;

    if (want != d->target || !d->attached) {
        detach(d);
        d->target = want;
        if (want && !resident(d, want)) return -1;
        attach_target(d, d->target);
    }

    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETDRAWCOLOR:
            d->draw_color.r = cmd->data.color.r; d->draw_color.g = cmd->data.color.g;
            d->draw_color.b = cmd->data.color.b; d->draw_color.a = cmd->data.color.a;
            break;
        case SDL_RENDERCMD_SETVIEWPORT:
            d->viewport = cmd->data.viewport.rect;
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            d->clip_enabled = cmd->data.cliprect.enabled;
            d->clip = cmd->data.cliprect.rect;
            break;
        case SDL_RENDERCMD_CLEAR: {
            SDL_Color c = { cmd->data.color.r, cmd->data.color.g, cmd->data.color.b, cmd->data.color.a };
            clear(d, c);
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
        case SDL_RENDERCMD_DRAW_LINES:
        case SDL_RENDERCMD_FILL_RECTS: {
            SDL_Color c = { cmd->data.draw.r, cmd->data.draw.g, cmd->data.draw.b, cmd->data.draw.a };
            fill_rects(d, (const SDL_FRect *)((Uint8 *)vertices + cmd->data.draw.first), (int)cmd->data.draw.count,
                       c, cmd->data.draw.blend);
            if (d->target) d->target->cpu_stale = SDL_TRUE;
            break;
        }
        case SDL_RENDERCMD_COPY:
        case SDL_RENDERCMD_COPY_EX: {
            SDL_Color c = { cmd->data.draw.r, cmd->data.draw.g, cmd->data.draw.b, cmd->data.draw.a };
            copy(d, cmd->data.draw.texture, (const N64_CopyCmd *)((Uint8 *)vertices + cmd->data.draw.first),
                 c, cmd->data.draw.blend, 0, 0);
            break;
        }
        case SDL_RENDERCMD_GEOMETRY: {
            const N64_QuadCmd *q = (const N64_QuadCmd *)((Uint8 *)vertices + cmd->data.draw.first);
            SDL_Texture *tex = cmd->data.draw.texture;
            N64_CopyCmd c;
            const int rows = SDL_max(1, (int)((q->y1 - q->y0) * 0.5f + 0.5f));
            int r;
            c.sx = q->u0 * tex->w; c.sy = q->v0 * tex->h;
            c.sw = (q->u1 - q->u0) * tex->w; c.sh = (q->v1 - q->v0) * tex->h;
            c.dx = q->x0; c.dy = q->y0; c.dw = q->x1 - q->x0; c.dh = q->y1 - q->y0;
            c.angle = 0; c.cx = c.cy = 0; c.flip = 0;
            for (r = 0; r < rows; ++r) {
                /* vertical interpolation of the left/right averages */
                const float f = (r + 0.5f) / rows;
                SDL_Color m;
                m.r = (Uint8)(((q->c[0].r + q->c[1].r) * (1 - f) + (q->c[2].r + q->c[3].r) * f) / 2);
                m.g = (Uint8)(((q->c[0].g + q->c[1].g) * (1 - f) + (q->c[2].g + q->c[3].g) * f) / 2);
                m.b = (Uint8)(((q->c[0].b + q->c[1].b) * (1 - f) + (q->c[2].b + q->c[3].b) * f) / 2);
                m.a = (Uint8)(((q->c[0].a + q->c[1].a) * (1 - f) + (q->c[2].a + q->c[3].a) * f) / 2);
                copy(d, tex, &c, m, cmd->data.draw.blend, (float)r / rows, (float)(r + 1) / rows);
            }
            break;
        }
        default:
            break;
        }
        cmd = cmd->next;
    }
    return 0;
}

static int N64R_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect, Uint32 format, void *pixels, int pitch)
{
    N64_RenderData *d = (N64_RenderData *)renderer->driverdata;
    SDL_PixelFormat *dst = SDL_AllocFormat(format);
    int x, y;
    if (!dst) return -1;
    detach(d);
    rspq_wait();
    /* The queue may not have run since the target changed, and the target may
       never have been drawn into (no RDRAM copy yet). */
    d->target = renderer->target ? (N64_Texture *)renderer->target->driverdata : NULL;
    if (d->target && !resident(d, d->target)) { SDL_FreeFormat(dst); return -1; }
    if (d->target) merge_alpha(d->target);
    for (y = 0; y < rect->h; ++y) {
        const int py = (rect->y + y) / 2;
        Uint32 *out = (Uint32 *)((Uint8 *)pixels + y * pitch);
        for (x = 0; x < rect->w; ++x) {
            const int px = rect->x + x;
            Uint8 r = 0, g = 0, b = 0, a = 255;
            if (d->target) {
                N64_Texture *t = d->target;
                if (px < t->sw && py < t->sh) {
                    const Uint8 *s = (const Uint8 *)t->gpu.buffer + py * t->gpu.stride + px * 4;
                    r = s[0]; g = s[1]; b = s[2]; a = s[3];
                }
            } else if (d->display && px < N64R_OUT_W && py < N64R_PHYS_H) {
                const uint16_t v = *(const uint16_t *)((const Uint8 *)d->display->buffer + py * d->display->stride + px * 2);
                r = (Uint8)(((v >> 11) & 31) << 3); g = (Uint8)(((v >> 6) & 31) << 3); b = (Uint8)(((v >> 1) & 31) << 3);
            }
            if (dst->BytesPerPixel == 4) out[x] = SDL_MapRGBA(dst, r, g, b, a);
        }
    }
    SDL_FreeFormat(dst);
    return 0;
}

static void draw_cursor(N64_RenderData *d)
{
    SDL_Surface *img;
    int hx, hy, mx, my;
    rdpq_blitparms_t p;
    SDL_N64_GetCursor(&img, &hx, &hy, &mx, &my);
    if (!img) return;
    if (img != d->cursor_src) {
        int x, y;
        if (d->cursor_tex.buffer) { rspq_wait(); surface_free(&d->cursor_tex); }
        d->cursor_tex = surface_alloc(FMT_RGBA32, img->w, (img->h + 1) / 2);
        for (y = 0; y < d->cursor_tex.height; ++y) {
            Uint8 *o = (Uint8 *)d->cursor_tex.buffer + y * d->cursor_tex.stride;
            for (x = 0; x < img->w; ++x) {
                /* half height: average the row pair (premultiplied) */
                Uint32 a0 = 0, a1 = 0, p0 = ((Uint32 *)((Uint8 *)img->pixels + (2 * y) * img->pitch))[x];
                Uint32 p1 = (2 * y + 1 < img->h) ? ((Uint32 *)((Uint8 *)img->pixels + (2 * y + 1) * img->pitch))[x] : p0;
                Uint32 sa;
                a0 = p0 >> 24; a1 = p1 >> 24;
                sa = a0 + a1;
                o[x * 4 + 0] = (Uint8)(sa ? (((p0 >> 16) & 255) * a0 + ((p1 >> 16) & 255) * a1) / sa : 0);
                o[x * 4 + 1] = (Uint8)(sa ? (((p0 >> 8) & 255) * a0 + ((p1 >> 8) & 255) * a1) / sa : 0);
                o[x * 4 + 2] = (Uint8)(sa ? ((p0 & 255) * a0 + (p1 & 255) * a1) / sa : 0);
                o[x * 4 + 3] = (Uint8)(sa / 2);
            }
        }
        d->cursor_src = img;
    }
    attach_target(d, NULL);
    rdpq_set_scissor(0, 0, N64R_OUT_W, N64R_PHYS_H);
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    SDL_zero(p);
    rdpq_tex_blit(&d->cursor_tex, (float)(mx - hx), (float)(my - hy) * 0.5f, &p);
}

static int N64R_RenderPresent(SDL_Renderer *renderer)
{
    N64_RenderData *d = (N64_RenderData *)renderer->driverdata;
    N64_Texture *keep = d->target;
    detach(d);
    d->target = NULL;
    if (!d->display) {
        SDL_N64_TakeDisplay();
        d->display = display_get();
        rdpq_attach_clear(d->display, NULL);
        d->attached = SDL_TRUE;
    }
    draw_cursor(d);
    detach(d);
    display_show(d->display);
    d->display = NULL;
    d->target = keep;
    ++d->frame;
    if (get_ticks() - s_diag.last_log > TICKS_PER_SECOND * 5ull) {
        int n = 0;
        N64_Texture *t;
        for (t = d->lru_head; t; t = t->next) ++n;
        debugf("[n64render] frame %lu at %lu s: %u queues, %u copies (%u skipped: %u no room, %u self, %u tiny), %d textures resident, %lu of %lu KiB\n",
               (unsigned long)d->frame, (unsigned long)(get_ticks_ms() / 1000), s_diag.queues, s_diag.copies,
               s_diag.skipped, s_diag.skip_room, s_diag.skip_self, s_diag.skip_tiny,
               n, (unsigned long)(d->used >> 10), (unsigned long)(d->budget >> 10));
        SDL_zero(s_diag);
        s_diag.last_log = get_ticks();
    }
    return 0;
}

static void N64R_DestroyRenderer(SDL_Renderer *renderer)
{
    N64_RenderData *d = (N64_RenderData *)renderer->driverdata;
    if (d) {
        detach(d);
        rspq_wait();
        if (d->cursor_tex.buffer) surface_free(&d->cursor_tex);
        SDL_free(d);
    }
    SDL_free(renderer);
}

static int N64R_GetOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
    (void)renderer;
    if (w) *w = N64R_OUT_W;
    if (h) *h = N64R_OUT_H;
    return 0;
}

static SDL_bool N64R_SupportsBlendMode(SDL_Renderer *renderer, SDL_BlendMode blendMode)
{
    (void)renderer;
    return blendMode == SDL_BLENDMODE_NONE || blendMode == SDL_BLENDMODE_BLEND || blendMode == SDL_BLENDMODE_ADD;
}

static SDL_Renderer *N64R_CreateRenderer(SDL_Window *window, Uint32 flags)
{
    SDL_Renderer *renderer = (SDL_Renderer *)SDL_calloc(1, sizeof(*renderer));
    N64_RenderData *d = (N64_RenderData *)SDL_calloc(1, sizeof(*d));
    heap_stats_t hs;
    (void)flags;
    if (!renderer || !d) {
        SDL_free(renderer);
        SDL_free(d);
        SDL_OutOfMemory();
        return NULL;
    }
    rdpq_init();
    /* Texture memory: what the physical heap has left, keeping 512 KiB for
       everything else that needs DMA-able memory. */
    sys_get_heap_stats(&hs);
    {
        const long free_bytes = (long)hs.total - (long)hs.used;
        const char* cap = SDL_GetHint("N64_TEXTURE_BUDGET_KB");   /* testing: a smaller budget */
        d->budget = free_bytes > (1536 << 10) ? (size_t)(free_bytes - (512 << 10)) : (1024 << 10);
        if (cap && SDL_atoi(cap) > 0 && (size_t)SDL_atoi(cap) << 10 < d->budget) d->budget = (size_t)SDL_atoi(cap) << 10;
        SDL_Log("n64 renderer: %ld KiB physical RAM free, %lu KiB for textures",
                free_bytes >> 10, (unsigned long)(d->budget >> 10));
    }

    renderer->GetOutputSize = N64R_GetOutputSize;
    renderer->SupportsBlendMode = N64R_SupportsBlendMode;
    renderer->CreateTexture = N64R_CreateTexture;
    renderer->UpdateTexture = N64R_UpdateTexture;
    renderer->LockTexture = N64R_LockTexture;
    renderer->UnlockTexture = N64R_UnlockTexture;
    renderer->SetTextureScaleMode = N64R_SetTextureScaleMode;
    renderer->SetRenderTarget = N64R_SetRenderTarget;
    renderer->QueueSetViewport = N64R_QueueSetViewport;
    renderer->QueueSetDrawColor = N64R_QueueSetDrawColor;
    renderer->QueueDrawPoints = N64R_QueueDrawPoints;
    renderer->QueueDrawLines = N64R_QueueDrawLines;
    renderer->QueueFillRects = N64R_QueueFillRects;
    renderer->QueueCopy = N64R_QueueCopy;
    renderer->QueueCopyEx = N64R_QueueCopyEx;
    renderer->QueueGeometry = N64R_QueueGeometry;
    renderer->RunCommandQueue = N64R_RunCommandQueue;
    renderer->RenderReadPixels = N64R_RenderReadPixels;
    renderer->RenderPresent = N64R_RenderPresent;
    renderer->DestroyTexture = N64R_DestroyTexture;
    renderer->DestroyRenderer = N64R_DestroyRenderer;
    renderer->info = PS2_RenderDriver.info;
    renderer->info.flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE;
    renderer->driverdata = d;
    renderer->window = window;
    return renderer;
}

SDL_RenderDriver PS2_RenderDriver = {
    N64R_CreateRenderer,
    {
        "n64",
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        4,
        /* Pixels are converted when stored, so these are all native: in
           particular the engine's ARGB8888 render targets must not get a
           proxy "native" texture (SDL would then report the proxy as the
           current target). */
        { SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_ABGR8888, SDL_PIXELFORMAT_BGRA8888 },
        2048,
        2048 }
};
