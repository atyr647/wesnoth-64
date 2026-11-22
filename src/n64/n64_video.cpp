/**
 * n64_video.cpp
 *
 * N64 rendering implementation for Wesnoth.
 *
 * Uses libdragon's display (VI) and graphics (RDP via rdpq) subsystems.
 * All draw calls are layered; layers are flushed in order each frame.
 *
 * Texture cache: up to 512 textures, total ~1 MB RAM budget.
 * TMEM (4 KB on-chip) is loaded per draw call via rdpq_tex_blit().
 */

#include "n64_video.hpp"
#include "n64_memory.hpp"
#include "n64_romfs.hpp"

#ifdef N64_BUILD
#  include <display.h>
#  include <rdpq.h>
#  include <rdpq_tex.h>
#  include <rdpq_mode.h>
// Redefine RGBA32 to use libdragon_color_t instead of color_t
// (the original macro expands to (color_t){...} which conflicts with Wesnoth's color_t)
#  undef RGBA32
#  define RGBA32(rx,gx,bx,ax) ((libdragon_color_t){.r=(rx), .g=(gx), .b=(bx), .a=(ax)})
#endif

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------
static constexpr int    GLYPH_W      = 8;    // pixels per glyph cell
static constexpr int    GLYPH_H      = 8;
static constexpr int    GLYPHS_PER_ROW = 16; // glyph sheet layout
static constexpr size_t MAX_TEXTURES  = 512;

// Hex stagger: odd columns are shifted down by half a tile height
static constexpr s16 HEX_COL_STRIDE = N64_HEX_W * 3 / 4;  // ~13px
static constexpr s16 HEX_ROW_STRIDE = N64_HEX_H;           // 18px
static constexpr s16 HEX_STAGGER    = N64_HEX_H / 2;       //  9px

// -----------------------------------------------------------------------
// Draw-call record (queued per layer, flushed in order)
// -----------------------------------------------------------------------
enum DrawCmdType : u8 { CMD_TEX, CMD_TEX_REGION, CMD_RECT, CMD_HLINE, CMD_TEXT };

struct DrawCmd {
    DrawCmdType  type;
    N64Layer     layer;
    union {
        struct { N64TexHandle tex; s16 x, y; }              tex;
        struct { N64TexHandle atlas; N64Rect src; s16 x, y; } tex_region;
        struct { N64Rect r; N64Color c; }                    rect;
        struct { s16 x1, x2, y; N64Color c; }               hline;
        struct { s16 x, y; u16 text_off; N64Color c; }       text;
    };
};

// Text is copied into a per-frame buffer (reset by n64_video_flip) instead of
// the permanent string-intern table: dynamic text (counters, coordinates)
// would otherwise grow that table until it overflowed.
static constexpr size_t TEXT_BUF_CAP = 8192;
static char  s_frame_text[TEXT_BUF_CAP];
static u16   s_frame_text_used = 0;

// Per-layer command queues (max 256 commands per layer per frame)
static constexpr size_t CMD_QUEUE_CAP = 256;
static DrawCmd s_cmds[LAYER_COUNT][CMD_QUEUE_CAP];
static u32     s_cmd_count[LAYER_COUNT];

// -----------------------------------------------------------------------
// Texture cache
// -----------------------------------------------------------------------
struct TexCacheEntry {
    u32      rom_offset;
    N64Texture tex;
    bool     in_use;
};

static TexCacheEntry s_tex_cache[MAX_TEXTURES];
static u16           s_tex_count = 0;

// Backing RAM for texture data (fits inside N64_POOL_TEXTURE_CACHE = 1 MB)
static u8 s_tex_data[N64_POOL_TEXTURE_CACHE] __attribute__((aligned(16)));   // DMA + cache ops need 16
static size_t s_tex_data_used = 0;

// Glyph sheet texture handle (loaded once at init)
static N64TexHandle s_glyph_sheet = N64_TEX_INVALID;

// ROM offset for the glyph sheet baked by the asset pipeline
// (Defined in n64_romfs.hpp, resolved at link time)
extern u32 ROMFS_GLYPH_SHEET_OFFSET;
extern u16 ROMFS_GLYPH_SHEET_W;
extern u16 ROMFS_GLYPH_SHEET_H;

// -----------------------------------------------------------------------
// Framebuffer
// -----------------------------------------------------------------------
#ifdef N64_BUILD
static bool s_attached = false;   // rdpq is currently attached to a framebuffer

// Attach the next display buffer to the RDP if a frame is not already open.
static void ensure_attached() {
    if (!s_attached) {
        rdpq_attach(display_get(), nullptr);
        s_attached = true;
    }
}

// RGBA5551 -> 8-bit-per-channel colour (expand 5 bits to 8 by replication).
static libdragon_color_t to_color(N64Color c) {
    auto ex = [](u16 v) -> u8 { return (u8)((v << 3) | (v >> 2)); };
    return RGBA32(ex((c.v >> 11) & 0x1F), ex((c.v >> 6) & 0x1F),
                  ex((c.v >> 1) & 0x1F), (c.v & 1) ? 255 : 0);
}

// Texture memory is a plain RGBA5551 block in the texture pool; wrap it in a
// surface descriptor for the RDP (no copy).
static surface_t tex_surface(const N64Texture& t) {
    return surface_make(t.data, FMT_RGBA16, t.width, t.height, t.width * 2);
}

// Alpha-keyed (1-bit alpha) textured draw, optionally tinted.
static void begin_textured(bool tinted, N64Color tint) {
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    if (tinted) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_set_prim_color(to_color(tint));
    } else {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    }
}
#endif

// -----------------------------------------------------------------------
// n64_video_init
// -----------------------------------------------------------------------
void n64_video_init() {
#ifdef N64_BUILD
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    rdpq_init();
#endif

    // Clear command queues
    for (int i = 0; i < LAYER_COUNT; ++i)
        s_cmd_count[i] = 0;

    // Load glyph sheet
    s_glyph_sheet = n64_tex_load(ROMFS_GLYPH_SHEET_OFFSET,
                                  ROMFS_GLYPH_SHEET_W,
                                  ROMFS_GLYPH_SHEET_H,
                                  /*has_alpha=*/true);
    N64_ASSERT(s_glyph_sheet != N64_TEX_INVALID);

    N64_LOG("Video init: 320x240 16-bit double-buffered, %u textures max", (unsigned)MAX_TEXTURES);
}

// -----------------------------------------------------------------------
// n64_video_flip — finish the frame (RDP flush + show on next vblank)
// -----------------------------------------------------------------------
void n64_video_flip() {
#ifdef N64_BUILD
    if (s_attached) {
        rdpq_detach_show();
        s_attached = false;
    }
#endif
    // Reset command queues for next frame
    for (int i = 0; i < LAYER_COUNT; ++i)
        s_cmd_count[i] = 0;
    s_frame_text_used = 0;
}

// -----------------------------------------------------------------------
// n64_video_clear — start a frame and clear it to colour c
// -----------------------------------------------------------------------
void n64_video_clear(N64Color c) {
#ifdef N64_BUILD
    ensure_attached();
    rdpq_clear(to_color(c));
#else
    (void)c;
#endif
}

// -----------------------------------------------------------------------
// n64_tex_load
// -----------------------------------------------------------------------
N64TexHandle n64_tex_load(u32 rom_offset, u16 w, u16 h, bool has_alpha) {
    // Check if already cached
    for (u16 i = 0; i < s_tex_count; ++i) {
        if (s_tex_cache[i].in_use && s_tex_cache[i].rom_offset == rom_offset)
            return i;
    }

    N64_ASSERT(s_tex_count < MAX_TEXTURES);
    size_t bytes = (size_t)w * h * 2; // 16-bit RGBA5551
    // Cache-line (16 B) granularity: DMA targets are invalidated by line, so a
    // texture must not share a line with its neighbour. ROMFS data is 16-byte
    // aligned and padded, so reading the padded span is safe.
    size_t span = (bytes + 15) & ~(size_t)15;
    N64_ASSERT(s_tex_data_used + span <= sizeof(s_tex_data));

    u16* dest = reinterpret_cast<u16*>(s_tex_data + s_tex_data_used);
    s_tex_data_used += span;

#ifdef N64_BUILD
    // DMA from ROM cartridge address space into dest
    // rom_offset is an absolute ROM address provided by the asset pipeline
    data_cache_hit_writeback_invalidate(dest, span);
    dma_read(dest, rom_offset, span);
    data_cache_hit_invalidate(dest, span);
#else
    // Host stub: zero the texture
    std::memset(dest, 0, bytes);
#endif

    u16 handle = s_tex_count++;
    s_tex_cache[handle] = { rom_offset, { dest, w, h, has_alpha }, true };
    return handle;
}

// -----------------------------------------------------------------------
// n64_tex_load_entry -- as n64_tex_load, for a ROMFS entry that may be
// LZ4-compressed (decompressed in place into the texture pool).
// -----------------------------------------------------------------------
N64TexHandle n64_tex_load_entry(const RomFsEntry* e, bool has_alpha) {
    if (!e) return N64_TEX_INVALID;
    if (!e->raw_length) return n64_tex_load(e->rom_offset, e->width, e->height, has_alpha);
    for (u16 i = 0; i < s_tex_count; ++i)
        if (s_tex_cache[i].in_use && s_tex_cache[i].rom_offset == e->rom_offset) return i;
    if (s_tex_count >= MAX_TEXTURES) return N64_TEX_INVALID;
    const u32 cap = n64_romfs_read_capacity(e);
    if (s_tex_data_used + cap > sizeof(s_tex_data)) return N64_TEX_INVALID;
    u8* dest = s_tex_data + s_tex_data_used;
    if (n64_romfs_read(e, dest, cap) != (u32)e->width * e->height * 2) return N64_TEX_INVALID;
    s_tex_data_used += ((u32)e->width * e->height * 2 + 15u) & ~15u;
    u16 handle = s_tex_count++;
    s_tex_cache[handle] = { e->rom_offset, { reinterpret_cast<u16*>(dest), e->width, e->height, has_alpha }, true };
    return handle;
}

// -----------------------------------------------------------------------
// n64_texcache_flush
// -----------------------------------------------------------------------
void n64_texcache_flush() {
    for (u16 i = 0; i < s_tex_count; ++i)
        s_tex_cache[i].in_use = false;
    s_tex_count = 0;
    s_tex_data_used = 0;
    // Glyph sheet must be reloaded after a flush
    s_glyph_sheet = n64_tex_load(ROMFS_GLYPH_SHEET_OFFSET,
                                  ROMFS_GLYPH_SHEET_W,
                                  ROMFS_GLYPH_SHEET_H,
                                  true);
}

// -----------------------------------------------------------------------
// Queue helpers
// -----------------------------------------------------------------------
static DrawCmd* alloc_cmd(N64Layer layer) {
    // A full queue drops the draw rather than hanging the console.
    if (s_cmd_count[layer] >= CMD_QUEUE_CAP) return nullptr;
    DrawCmd* cmd = &s_cmds[layer][s_cmd_count[layer]++];
    cmd->layer = layer;
    return cmd;
}

// -----------------------------------------------------------------------
// Public draw API
// -----------------------------------------------------------------------
void n64_draw_texture(N64Layer layer, N64TexHandle tex, s16 x, s16 y) {
    DrawCmd* cmd = alloc_cmd(layer);
    if (!cmd) return;
    cmd->type    = CMD_TEX;
    cmd->tex     = { tex, x, y };
}

void n64_draw_texture_region(N64Layer layer, N64TexHandle atlas,
                              N64Rect src, s16 dst_x, s16 dst_y) {
    DrawCmd* cmd       = alloc_cmd(layer);
    if (!cmd) return;
    cmd->type          = CMD_TEX_REGION;
    cmd->tex_region    = { atlas, src, dst_x, dst_y };
}

void n64_draw_rect(N64Layer layer, N64Rect r, N64Color c) {
    DrawCmd* cmd  = alloc_cmd(layer);
    if (!cmd) return;
    cmd->type     = CMD_RECT;
    cmd->rect     = { r, c };
}

void n64_draw_hline(N64Layer layer, s16 x1, s16 x2, s16 y, N64Color c) {
    DrawCmd* cmd  = alloc_cmd(layer);
    if (!cmd) return;
    cmd->type     = CMD_HLINE;
    cmd->hline    = { x1, x2, y, c };
}

void n64_draw_rect_outline(N64Layer layer, N64Rect r, N64Color c) {
    n64_draw_hline(layer, r.x, r.x + r.w - 1, r.y,           c);
    n64_draw_hline(layer, r.x, r.x + r.w - 1, r.y + r.h - 1, c);
    // sides
    for (s16 dy = 1; dy < r.h - 1; ++dy) {
        n64_draw_hline(layer, r.x,           r.x,           r.y + dy, c);
        n64_draw_hline(layer, r.x + r.w - 1, r.x + r.w - 1, r.y + dy, c);
    }
}

// -----------------------------------------------------------------------
// Hex coordinate helpers
// -----------------------------------------------------------------------
N64Point n64_hex_to_screen(s16 hx, s16 hy, s16 scroll_x, s16 scroll_y) {
    // Wesnoth uses "offset" hex grid: odd columns staggered down
    s16 px = hx * HEX_COL_STRIDE - scroll_x;
    s16 py = hy * HEX_ROW_STRIDE + (hx & 1) * HEX_STAGGER - scroll_y;
    return { px, py };
}

void n64_draw_hex_tile(N64Layer layer, N64TexHandle tex, s16 hx, s16 hy) {
    // scroll is managed by the caller via the display object
    // Here we draw at the canonical position; display.cpp subtracts scroll
    n64_draw_texture(layer, tex, hx, hy); // positions replaced in flush
}

// -----------------------------------------------------------------------
// Text drawing
// -----------------------------------------------------------------------
void n64_draw_text(s16 x, s16 y, const char* text, N64Color colour) {
    if (!text || !*text) return;
    // Copy into the per-frame buffer and queue — blitting happens in
    // n64_flush_layer. Text that does not fit this frame is dropped.
    size_t len = strlen(text) + 1;
    if (s_frame_text_used + len > TEXT_BUF_CAP || s_cmd_count[LAYER_TEXT] >= CMD_QUEUE_CAP)
        return;
    memcpy(s_frame_text + s_frame_text_used, text, len);
    DrawCmd* cmd = alloc_cmd(LAYER_TEXT);
    if (!cmd) return;
    cmd->type    = CMD_TEXT;
    cmd->text    = { x, y, s_frame_text_used, colour };
    s_frame_text_used += (u16)len;
}

void n64_draw_text_shadowed(s16 x, s16 y, const char* text, N64Color colour) {
    n64_draw_text(x + 1, y + 1, text, N64Color::black());
    n64_draw_text(x,     y,     text, colour);
}

s16 n64_text_width(const char* text) {
    if (!text) return 0;
    s16 len = 0;
    while (*text++) ++len;
    return len * GLYPH_W;
}

// -----------------------------------------------------------------------
// n64_flush_layer — execute queued draw commands via RDP
// -----------------------------------------------------------------------
void n64_flush_layer(N64Layer layer) {
#ifdef N64_BUILD
    ensure_attached();
#endif

    for (u32 i = 0; i < s_cmd_count[layer]; ++i) {
        const DrawCmd& cmd = s_cmds[layer][i];
        switch (cmd.type) {
        case CMD_TEX: {
#ifdef N64_BUILD
            const N64Texture& t = s_tex_cache[cmd.tex.tex].tex;
            surface_t surf = tex_surface(t);
            begin_textured(false, N64Color::white());
            rdpq_tex_blit(&surf, cmd.tex.x, cmd.tex.y, nullptr);
#endif
            break;
        }
        case CMD_TEX_REGION: {
#ifdef N64_BUILD
            const N64Texture& t = s_tex_cache[cmd.tex_region.atlas].tex;
            surface_t surf = tex_surface(t);
            rdpq_blitparms_t bp = {};
            bp.s0 = cmd.tex_region.src.x;
            bp.t0 = cmd.tex_region.src.y;
            bp.width  = cmd.tex_region.src.w;
            bp.height = cmd.tex_region.src.h;
            begin_textured(false, N64Color::white());
            rdpq_tex_blit(&surf, cmd.tex_region.x, cmd.tex_region.y, &bp);
#endif
            break;
        }
        case CMD_RECT:
#ifdef N64_BUILD
            rdpq_set_mode_fill(to_color(cmd.rect.c));
            rdpq_fill_rectangle(cmd.rect.r.x, cmd.rect.r.y,
                                cmd.rect.r.x + cmd.rect.r.w,
                                cmd.rect.r.y + cmd.rect.r.h);
#endif
            break;
        case CMD_HLINE:
#ifdef N64_BUILD
            rdpq_set_mode_fill(to_color(cmd.hline.c));
            rdpq_fill_rectangle(cmd.hline.x1, cmd.hline.y,
                                cmd.hline.x2 + 1, cmd.hline.y + 1);
#endif
            break;
        case CMD_TEXT: {
            // Blit glyph by glyph from the glyph sheet, tinted by the text colour
            const char* s = s_frame_text + cmd.text.text_off;
            s16 cx = cmd.text.x;
#ifdef N64_BUILD
            surface_t sheet = tex_surface(s_tex_cache[s_glyph_sheet].tex);
            begin_textured(true, cmd.text.c);
#endif
            while (*s) {
                unsigned char c = (unsigned char)*s++;
#ifdef N64_BUILD
                rdpq_blitparms_t bp = {};
                bp.s0 = (c % GLYPHS_PER_ROW) * GLYPH_W;
                bp.t0 = (c / GLYPHS_PER_ROW) * GLYPH_H;
                bp.width = GLYPH_W; bp.height = GLYPH_H;
                rdpq_tex_blit(&sheet, cx, cmd.text.y, &bp);
#endif
                cx += GLYPH_W;
            }
            break;
        }
        } // switch
    }
}
