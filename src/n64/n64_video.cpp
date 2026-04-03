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

#ifdef N64_BUILD
#  include <display.h>
#  include <rdpq.h>
#  include <rdpq_tex.h>
#  include <rdpq_mode.h>
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
        struct { s16 x, y; u16 str_handle; N64Color c; }    text;
    };
};

// Per-layer command queues (max 1024 commands per layer per frame)
static constexpr size_t CMD_QUEUE_CAP = 1024;
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
static u8 s_tex_data[N64_POOL_TEXTURE_CACHE] __attribute__((aligned(8)));
static size_t s_tex_data_used = 0;

// Glyph sheet texture handle (loaded once at init)
static N64TexHandle s_glyph_sheet = N64_TEX_INVALID;

// ROM offset for the glyph sheet baked by the asset pipeline
// (Defined in n64_romfs.hpp, resolved at link time)
extern const u32 ROMFS_GLYPH_SHEET_OFFSET;
extern const u16 ROMFS_GLYPH_SHEET_W;
extern const u16 ROMFS_GLYPH_SHEET_H;

// -----------------------------------------------------------------------
// Framebuffer
// -----------------------------------------------------------------------
#ifdef N64_BUILD
static display_context_t s_disp = 0;
#endif

// -----------------------------------------------------------------------
// n64_video_init
// -----------------------------------------------------------------------
void n64_video_init() {
#ifdef N64_BUILD
    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE);
    rdpq_init();
    rdpq_debug_start();
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

    N64_LOG("Video init: 320x240 16-bit double-buffered, %u textures max", MAX_TEXTURES);
}

// -----------------------------------------------------------------------
// n64_video_flip
// -----------------------------------------------------------------------
void n64_video_flip() {
#ifdef N64_BUILD
    display_show(s_disp);
    s_disp = display_get();
#endif
    // Reset command queues for next frame
    for (int i = 0; i < LAYER_COUNT; ++i)
        s_cmd_count[i] = 0;
}

// -----------------------------------------------------------------------
// n64_video_clear
// -----------------------------------------------------------------------
void n64_video_clear(N64Color c) {
#ifdef N64_BUILD
    if (!s_disp) s_disp = display_get();
    rdpq_attach_clear(s_disp, nullptr, c.v);
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
    N64_ASSERT(s_tex_data_used + bytes <= sizeof(s_tex_data));

    u16* dest = reinterpret_cast<u16*>(s_tex_data + s_tex_data_used);
    s_tex_data_used += bytes;

#ifdef N64_BUILD
    // DMA from ROM cartridge address space into dest
    // rom_offset is an absolute ROM address provided by the asset pipeline
    data_cache_hit_writeback_invalidate(dest, bytes);
    dma_read(dest, rom_offset, bytes);
#else
    // Host stub: zero the texture
    std::memset(dest, 0, bytes);
#endif

    u16 handle = s_tex_count++;
    s_tex_cache[handle] = { rom_offset, { dest, w, h, has_alpha }, true };
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
    N64_ASSERT(s_cmd_count[layer] < CMD_QUEUE_CAP);
    DrawCmd* cmd = &s_cmds[layer][s_cmd_count[layer]++];
    cmd->layer = layer;
    return cmd;
}

// -----------------------------------------------------------------------
// Public draw API
// -----------------------------------------------------------------------
void n64_draw_texture(N64Layer layer, N64TexHandle tex, s16 x, s16 y) {
    DrawCmd* cmd = alloc_cmd(layer);
    cmd->type    = CMD_TEX;
    cmd->tex     = { tex, x, y };
}

void n64_draw_texture_region(N64Layer layer, N64TexHandle atlas,
                              N64Rect src, s16 dst_x, s16 dst_y) {
    DrawCmd* cmd       = alloc_cmd(layer);
    cmd->type          = CMD_TEX_REGION;
    cmd->tex_region    = { atlas, src, dst_x, dst_y };
}

void n64_draw_rect(N64Layer layer, N64Rect r, N64Color c) {
    DrawCmd* cmd  = alloc_cmd(layer);
    cmd->type     = CMD_RECT;
    cmd->rect     = { r, c };
}

void n64_draw_hline(N64Layer layer, s16 x1, s16 x2, s16 y, N64Color c) {
    DrawCmd* cmd  = alloc_cmd(layer);
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
    // Intern and queue — actual blitting happens in n64_flush_layer
    u16 handle = n64_intern(text);
    DrawCmd* cmd = alloc_cmd(LAYER_TEXT);
    cmd->type    = CMD_TEXT;
    cmd->text    = { x, y, handle, colour };
    (void)colour;
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
    rdpq_mode_begin();
    if (layer == LAYER_TERRAIN || layer == LAYER_UNITS) {
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    } else {
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(0);
    }
    rdpq_mode_end();
#endif

    for (u32 i = 0; i < s_cmd_count[layer]; ++i) {
        const DrawCmd& cmd = s_cmds[layer][i];
        switch (cmd.type) {
        case CMD_TEX: {
            const N64Texture& t = s_tex_cache[cmd.tex.tex].tex;
#ifdef N64_BUILD
            rdpq_tex_blit((surface_t*)t.data, cmd.tex.x, cmd.tex.y, nullptr);
#endif
            break;
        }
        case CMD_TEX_REGION: {
            const N64Texture& t = s_tex_cache[cmd.tex_region.atlas].tex;
#ifdef N64_BUILD
            rdpq_blitparms_t bp = {};
            bp.s0 = cmd.tex_region.src.x;
            bp.t0 = cmd.tex_region.src.y;
            bp.width  = cmd.tex_region.src.w;
            bp.height = cmd.tex_region.src.h;
            rdpq_tex_blit((surface_t*)t.data, cmd.tex_region.x, cmd.tex_region.y, &bp);
#endif
            break;
        }
        case CMD_RECT:
#ifdef N64_BUILD
            rdpq_set_prim_color(RGBA32(
                (cmd.rect.c.v >> 11) & 0x1F,
                (cmd.rect.c.v >>  6) & 0x1F,
                (cmd.rect.c.v >>  1) & 0x1F, 255));
            rdpq_fill_rectangle(cmd.rect.r.x, cmd.rect.r.y,
                                cmd.rect.r.x + cmd.rect.r.w,
                                cmd.rect.r.y + cmd.rect.r.h);
#endif
            break;
        case CMD_HLINE:
#ifdef N64_BUILD
            rdpq_set_prim_color(RGBA32(0,0,0,255));
            rdpq_fill_rectangle(cmd.hline.x1, cmd.hline.y,
                                cmd.hline.x2 + 1, cmd.hline.y + 1);
#endif
            break;
        case CMD_TEXT: {
            // Blit glyph by glyph from the glyph sheet
            const char* s = n64_str(cmd.text.str_handle);
            const N64Texture& sheet = s_tex_cache[s_glyph_sheet].tex;
            s16 cx = cmd.text.x;
            while (*s) {
                unsigned char c = (unsigned char)*s++;
                s16 gx = (c % GLYPHS_PER_ROW) * GLYPH_W;
                s16 gy = (c / GLYPHS_PER_ROW) * GLYPH_H;
#ifdef N64_BUILD
                rdpq_blitparms_t bp = {};
                bp.s0 = gx; bp.t0 = gy;
                bp.width = GLYPH_W; bp.height = GLYPH_H;
                rdpq_tex_blit((surface_t*)sheet.data, cx, cmd.text.y, &bp);
#endif
                cx += GLYPH_W;
            }
            break;
        }
        } // switch
    }
}
