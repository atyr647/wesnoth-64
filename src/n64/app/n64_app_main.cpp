/**
 * n64_app_main.cpp
 *
 * Wesnoth N64 front-end: boots on the console, shows a title screen, lets you
 * browse every campaign and scenario packed in the ROM, and opens the real
 * Wesnoth map for that scenario in a scrollable hex viewer.
 *
 * Everything is driven from the ROMFS built by tools/n64-asset-pipeline:
 *   app/terrain_codes.txt  terrain code -> texture, generated from terrain.cfg
 *   app/manifest.txt       "<campaign>\t<map path>" per packed campaign map
 *   campaigns/<c>/maps/*.map   the maps themselves
 *   images/terrain/...     the real Wesnoth terrain art
 *
 * It uses none of the Wesnoth engine (which does not fit in the console's RAM
 * budget); see the project notes. Controls are listed on the in-game help.
 */

#include "../n64_platform.hpp"
#include "../n64_memory.hpp"
#include "../n64_romfs.hpp"
#include "../n64_video.hpp"
#include "../n64_audio.hpp"
#include "../n64_input.hpp"
#include "../n64_gui.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
// Static data loaded from the ROMFS
// -----------------------------------------------------------------------
struct TerrainDef { char code[12]; const char* path; };

static char       s_codes_txt[24 * 1024] __attribute__((aligned(16)));
static TerrainDef s_defs[400];
static int        s_def_count = 0;

static char s_manifest_txt[48 * 1024] __attribute__((aligned(16)));
struct MapRef { u8 campaign; const char* path; };
static MapRef s_maps[512];
static int    s_map_count = 0;
static const char* s_campaigns[48];
static int    s_campaign_count = 0;

static void load_tables() {
    const RomFsEntry* e = n64_romfs_find("app/terrain_codes.txt");
    u32 n = e ? n64_romfs_read(e, s_codes_txt, sizeof(s_codes_txt) - 1) : 0;
    s_codes_txt[n] = 0;
    for (char* p = s_codes_txt; *p && s_def_count < 400;) {
        char* tab = strchr(p, '\t');
        char* nl  = tab ? strchr(tab, '\n') : nullptr;
        if (!tab || !nl) break;
        *tab = 0; *nl = 0;
        TerrainDef& d = s_defs[s_def_count++];
        strncpy(d.code, p, sizeof(d.code) - 1);
        d.code[sizeof(d.code) - 1] = 0;
        d.path = tab + 1;
        p = nl + 1;
    }

    e = n64_romfs_find("app/manifest.txt");
    n = e ? n64_romfs_read(e, s_manifest_txt, sizeof(s_manifest_txt) - 1) : 0;
    s_manifest_txt[n] = 0;
    const char* last = "";
    for (char* p = s_manifest_txt; *p && s_map_count < 512;) {
        char* tab = strchr(p, '\t');
        char* nl  = tab ? strchr(tab, '\n') : nullptr;
        if (!tab || !nl) break;
        *tab = 0; *nl = 0;
        if (strcmp(p, last) != 0 && s_campaign_count < 48) {
            s_campaigns[s_campaign_count++] = p;
            last = p;
        }
        s_maps[s_map_count++] = { (u8)(s_campaign_count - 1), tab + 1 };
        p = nl + 1;
    }
    N64_LOG("app: %d terrain codes, %d campaigns, %d maps",
            s_def_count, s_campaign_count, s_map_count);
}

static const char* lookup_def(const char* code) {
    for (int i = 0; i < s_def_count; ++i)
        if (strcmp(s_defs[i].code, code) == 0) return s_defs[i].path;
    return nullptr;
}

// -----------------------------------------------------------------------
// Texture handling: on-demand loads with a byte budget; the whole cache is
// flushed at a frame boundary when the budget is spent.
// -----------------------------------------------------------------------
static constexpr u32 TEX_BUDGET = 200 * 1024;
static u32  s_tex_bytes = 0;
static bool s_tex_flush_pending = false;
static u16  s_flush_epoch = 1;          // bumped on every flush

struct CachedTex { N64TexHandle h; u16 epoch; };

static N64TexHandle get_tex(const char* path, CachedTex& slot) {
    if (slot.epoch == s_flush_epoch) return slot.h;
    slot.epoch = s_flush_epoch;
    slot.h = N64_TEX_INVALID;
    if (!path) return slot.h;
    const RomFsEntry* e = n64_romfs_find(path);
    if (!e) return slot.h;
    u32 bytes = (u32)e->width * e->height * 2;
    if (s_tex_bytes + bytes > TEX_BUDGET) { s_tex_flush_pending = true; return slot.h; }
    s_tex_bytes += bytes;
    slot.h = n64_tex_load(e->rom_offset, e->width, e->height, true);
    return slot.h;
}

static void begin_frame_textures() {
    if (s_tex_flush_pending) {
        n64_texcache_flush();
        s_tex_bytes = 128 * 128 * 2;     // glyph sheet is reloaded by the flush
        s_tex_flush_pending = false;
        ++s_flush_epoch;
        if (s_flush_epoch == 0) s_flush_epoch = 1;
    }
}

// -----------------------------------------------------------------------
// Sounds
// -----------------------------------------------------------------------
static N64SfxHandle load_sfx(const char* path) {
    const RomFsEntry* e = n64_romfs_find(path);
    return e ? n64_sfx_load(e->rom_offset, e->byte_length) : N64_SFX_INVALID;
}
static N64SfxHandle s_sfx_move = N64_SFX_INVALID, s_sfx_select = N64_SFX_INVALID,
                    s_sfx_back = N64_SFX_INVALID;
static void play(N64SfxHandle h) { if (h != N64_SFX_INVALID) n64_sfx_play(CHAN_UI0, h, 200, 0); }

// -----------------------------------------------------------------------
// Frame helper
// -----------------------------------------------------------------------
static void present() {
    for (int l = 0; l < LAYER_COUNT; ++l) n64_flush_layer((N64Layer)l);
    n64_video_flip();
    n64_audio_update();
    n64_input_poll();
}

// -----------------------------------------------------------------------
// Map model
// -----------------------------------------------------------------------
static constexpr int MAX_HEX   = 8192;
static constexpr int MAX_CODES = 256;
static constexpr int TILE      = 32;            // texture size on screen
static constexpr int COL_STEP  = 24;            // 3/4 of a hex width
static constexpr int ROW_STEP  = 32;
static constexpr int HALF_ROW  = 16;            // odd columns sit half a row lower

struct CodeInfo { char code[12]; const char* path; CachedTex tex; u16 color; };
static CodeInfo s_codeinfo[MAX_CODES];
static int      s_codeinfo_count = 0;

static u16 s_base[MAX_HEX];
static u16 s_over[MAX_HEX];
static u8  s_start[MAX_HEX];                     // player number (0 = none)
static int s_mw = 0, s_mh = 0;
static char s_map_buf[40 * 1024] __attribute__((aligned(16)));

static u16 fallback_colour(char c) {
    switch (c) {
    case 'G': return N64Color::from_rgb(70, 120, 40).v;
    case 'H': return N64Color::from_rgb(130, 100, 60).v;
    case 'M': return N64Color::from_rgb(120, 120, 120).v;
    case 'W': return N64Color::from_rgb(30, 70, 150).v;
    case 'S': return N64Color::from_rgb(60, 90, 70).v;
    case 'R': return N64Color::from_rgb(150, 120, 80).v;
    case 'D': case 'Q': return N64Color::from_rgb(200, 180, 110).v;
    case 'C': case 'K': return N64Color::from_rgb(140, 130, 130).v;
    case 'A': return N64Color::from_rgb(230, 240, 250).v;
    case 'U': return N64Color::from_rgb(90, 70, 70).v;
    default:  return N64Color::from_rgb(40, 40, 40).v;
    }
}

static u16 intern_code(const char* code) {
    if (!*code) return 0;                          // 0 = none
    for (int i = 1; i <= s_codeinfo_count; ++i)
        if (strcmp(s_codeinfo[i].code, code) == 0) return (u16)i;
    if (s_codeinfo_count + 1 >= MAX_CODES) return 0;
    CodeInfo& ci = s_codeinfo[++s_codeinfo_count];
    strncpy(ci.code, code, sizeof(ci.code) - 1);
    ci.code[sizeof(ci.code) - 1] = 0;
    ci.path  = lookup_def(code);
    ci.tex   = { N64_TEX_INVALID, 0 };
    ci.color = fallback_colour(code[code[0] == '^' ? 1 : 0]);
    return (u16)s_codeinfo_count;
}

// Parse a Wesnoth .map: rows of comma-separated terrain codes. A cell may start
// with a player number ("1 Ke"); an optional "border_size=1" header means the
// outer ring of hexes is border and not part of the playfield.
static bool load_map(const char* path) {
    const RomFsEntry* e = n64_romfs_find(path);
    if (!e) return false;
    u32 n = n64_romfs_read(e, s_map_buf, sizeof(s_map_buf) - 1);
    if (!n) return false;
    s_map_buf[n] = 0;

    s_codeinfo_count = 0;
    s_mw = s_mh = 0;
    int border = 0;
    int row = 0;
    for (char* line = s_map_buf; *line;) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (strchr(line, '=')) {
            if (strstr(line, "border_size=1")) border = 1;
        } else if (strchr(line, ',') || (*line && *line != '\r')) {
            int col = 0;
            for (char* cell = line; cell;) {
                char* comma = strchr(cell, ',');
                if (comma) *comma = 0;
                while (*cell == ' ' || *cell == '\t' || *cell == '\r') ++cell;
                int player = 0;
                if (*cell >= '0' && *cell <= '9') {
                    player = atoi(cell);
                    while (*cell && *cell != ' ') ++cell;
                    while (*cell == ' ') ++cell;
                }
                char* end = cell + strlen(cell);
                while (end > cell && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\t')) *--end = 0;
                if (*cell && row < 512 && col < 512) {
                    char* caret = strchr(cell, '^');
                    char basebuf[12] = {0}, overbuf[12] = {0};
                    if (caret) {
                        size_t bl = (size_t)(caret - cell);
                        if (bl >= sizeof(basebuf)) bl = sizeof(basebuf) - 1;
                        memcpy(basebuf, cell, bl);
                        strncpy(overbuf, caret, sizeof(overbuf) - 1);
                    } else {
                        strncpy(basebuf, cell, sizeof(basebuf) - 1);
                    }
                    int idx = row * 512 + col;   // temporary wide stride, packed below
                    (void)idx;
                    // Store into a per-row staging area sized by the final width below.
                    // Width is not known until the first row is done, so use row 0's width.
                    if (row == 0 && col + 1 > s_mw) s_mw = col + 1;
                    if (col < s_mw) {
                        int at = row * s_mw + col;
                        if (at < MAX_HEX) {
                            s_base[at]  = intern_code(basebuf);
                            s_over[at]  = intern_code(overbuf);
                            s_start[at] = (u8)player;
                        }
                    }
                    ++col;
                }
                cell = comma ? comma + 1 : nullptr;
            }
            if (col > 0) ++row;
        }
        if (!nl) break;
        line = nl + 1;
    }
    s_mh = row;
    if (s_mw <= 0 || s_mh <= 0 || s_mw * s_mh > MAX_HEX) { s_mw = s_mh = 0; return false; }
    (void)border;   // border hexes are kept: they carry the playfield edge art
    return true;
}

// -----------------------------------------------------------------------
// Viewer
// -----------------------------------------------------------------------
static CachedTex s_unit_tex[3];
static const char* kUnitArt[3] = {
    "images/units/human-loyalists/spearman.png",
    "images/units/orcs/grunt.png",
    "images/units/human-loyalists/bowman.png",
};

static inline int hex_x(int c) { return c * COL_STEP; }
static inline int hex_y(int c, int r) { return r * ROW_STEP + (c & 1) * HALF_ROW; }

static void draw_box(N64Layer layer, int x, int y, int w, int h, N64Color col) {
    n64_draw_rect(layer, {(s16)x, (s16)y, (s16)w, (s16)h}, col);
}

static void view_map(const char* title, const char* map_path) {
    if (!load_map(map_path)) {
        n64_message_box("Map", "Could not load this map.");
        return;
    }
    s_tex_flush_pending = true;     // start the viewer with a clean texture cache
    begin_frame_textures();

    int cx = 0, cy = 0;             // cursor hex
    // Start on the first player start position if there is one.
    for (int i = 0; i < s_mw * s_mh; ++i)
        if (s_start[i] == 1) { cx = i % s_mw; cy = i / s_mw; break; }

    int cam_x = 0, cam_y = 0;
    int hold = 0;
    bool show_help = true;
    unsigned frame = 0;
    char buf[96];

    for (;;) {
        begin_frame_textures();
        const N64InputState& in = n64_input_get(0);

        // --- input ---
        int dx = 0, dy = 0;
        if (in.just_pressed[N64_ACTION_UNIT_INFO])  dx = 1;      // D-pad right
        if (in.just_pressed[N64_ACTION_UNDO])       dx = -1;     // D-pad left
        if (in.just_pressed[N64_ACTION_END_TURN])   dy = 1;      // D-pad down
        if (in.just_pressed[N64_ACTION_QUICKSAVE])  dy = -1;     // D-pad up
        dx += in.cursor_dx; dy -= in.cursor_dy;                  // stick (y up is negative rows)
        bool dpad = in.held[N64_ACTION_UNIT_INFO] || in.held[N64_ACTION_UNDO] ||
                    in.held[N64_ACTION_END_TURN]  || in.held[N64_ACTION_QUICKSAVE];
        hold = dpad ? hold + 1 : 0;
        if (!dx && !dy && hold > 14 && (hold % 3) == 0) {         // auto-repeat
            dx = in.held[N64_ACTION_UNIT_INFO] ? 1 : in.held[N64_ACTION_UNDO] ? -1 : 0;
            dy = in.held[N64_ACTION_END_TURN]  ? 1 : in.held[N64_ACTION_QUICKSAVE] ? -1 : 0;
        }
        if (dx || dy) {
            int nx = cx + dx, ny = cy + dy;
            if (nx >= 0 && nx < s_mw) cx = nx;
            if (ny >= 0 && ny < s_mh) cy = ny;
            play(s_sfx_move);
        }
        // C buttons nudge the camera independently of the cursor.
        int nudge_x = 0, nudge_y = 0;
        if (in.held[N64_ACTION_SCROLL_LEFT])     nudge_x = -4;
        if (in.held[N64_ACTION_CYCLE_UNITS_FWD]) nudge_x = 4;    // C-Right
        if (in.held[N64_ACTION_SCROLL_UP])       nudge_y = -4;
        if (in.held[N64_ACTION_SCROLL_DOWN])     nudge_y = 4;
        // (ignore the A press that opened the map)
        if (frame++ > 10 && in.just_pressed[N64_ACTION_SELECT]) { show_help = !show_help; play(s_sfx_select); }
        if (in.just_pressed[N64_ACTION_CANCEL] || in.just_pressed[N64_ACTION_GAME_MENU]) {
            play(s_sfx_back);
            return;
        }

        // --- camera: follow the cursor, clamp to the map ---
        const int view_top = 12, view_h = N64_SCREEN_H - 12 - view_top;
        int tx = hex_x(cx) + TILE / 2 - N64_SCREEN_W / 2 + nudge_x;
        int ty = hex_y(cx, cy) + TILE / 2 - (view_top + view_h / 2) + nudge_y;
        int max_x = hex_x(s_mw - 1) + TILE - N64_SCREEN_W;
        int max_y = (s_mh - 1) * ROW_STEP + HALF_ROW + TILE - N64_SCREEN_H;
        if (max_x < 0) max_x = 0;
        if (max_y < 0) max_y = 0;
        if (tx < 0) tx = 0; if (tx > max_x) tx = max_x;
        if (ty < 0) ty = 0; if (ty > max_y) ty = max_y;
        cam_x += (tx - cam_x) / 2;
        cam_y += (ty - cam_y) / 2;
        if (abs(tx - cam_x) <= 1) cam_x = tx;
        if (abs(ty - cam_y) <= 1) cam_y = ty;

        // --- draw ---
        n64_video_clear(N64Color::from_rgb(8, 8, 16));

        int c0 = cam_x / COL_STEP - 1;               if (c0 < 0) c0 = 0;
        int c1 = (cam_x + N64_SCREEN_W) / COL_STEP + 1; if (c1 >= s_mw) c1 = s_mw - 1;
        int r0 = (cam_y - HALF_ROW) / ROW_STEP - 1;  if (r0 < 0) r0 = 0;
        int r1 = (cam_y + N64_SCREEN_H) / ROW_STEP + 1; if (r1 >= s_mh) r1 = s_mh - 1;

        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                const int at = r * s_mw + c;
                const int sx = hex_x(c) - cam_x, sy = hex_y(c, r) - cam_y;
                if (sx < -TILE || sx > N64_SCREEN_W || sy < -TILE || sy > N64_SCREEN_H) continue;

                CodeInfo& b = s_codeinfo[s_base[at]];
                N64TexHandle bh = s_base[at] ? get_tex(b.path, b.tex) : N64_TEX_INVALID;
                if (bh != N64_TEX_INVALID) n64_draw_texture(LAYER_BG, bh, (s16)sx, (s16)sy);
                else if (s_base[at]) draw_box(LAYER_BG, sx + 6, sy + 4, 20, 24, N64Color{b.color});

                if (s_over[at]) {
                    CodeInfo& o = s_codeinfo[s_over[at]];
                    N64TexHandle oh = get_tex(o.path, o.tex);
                    if (oh != N64_TEX_INVALID) n64_draw_texture(LAYER_TERRAIN, oh, (s16)sx, (s16)sy);
                }
                if (s_start[at]) {
                    int k = s_start[at] == 1 ? 0 : (s_start[at] & 1) ? 2 : 1;
                    N64TexHandle uh = get_tex(kUnitArt[k], s_unit_tex[k]);
                    if (uh != N64_TEX_INVALID) n64_draw_texture(LAYER_UNITS, uh, (s16)sx, (s16)(sy - 2));
                }
            }
        }

        // Cursor: a yellow frame around the hex.
        {
            int sx = hex_x(cx) - cam_x + 4, sy = hex_y(cx, cy) - cam_y + 2;
            const N64Color y = N64Color::from_rgb(255, 230, 60);
            draw_box(LAYER_EFFECTS, sx,      sy,      24, 2, y);
            draw_box(LAYER_EFFECTS, sx,      sy + 26, 24, 2, y);
            draw_box(LAYER_EFFECTS, sx,      sy,      2, 28, y);
            draw_box(LAYER_EFFECTS, sx + 22, sy,      2, 28, y);
        }

        // HUD
        draw_box(LAYER_UI, 0, 0, N64_SCREEN_W, 12, N64Color::from_rgb(0, 0, 0));
        draw_box(LAYER_UI, 0, N64_SCREEN_H - 12, N64_SCREEN_W, 12, N64Color::from_rgb(0, 0, 0));
        n64_draw_text(4, 2, title, N64Color::from_rgb(255, 224, 96));
        const int at = cy * s_mw + cx;
        snprintf(buf, sizeof(buf), "%s%s  (%d,%d)  %dx%d",
                 s_codeinfo[s_base[at]].code, s_codeinfo[s_over[at]].code,
                 cx + 1, cy + 1, s_mw, s_mh);
        n64_draw_text(4, N64_SCREEN_H - 10, buf, N64Color::white());
        if (show_help) {
            draw_box(LAYER_UI, 40, 80, 240, 72, N64Color::from_rgb(0, 0, 40));
            n64_draw_text(52, 88,  "D-PAD / STICK: MOVE CURSOR", N64Color::white());
            n64_draw_text(52, 100, "C BUTTONS: NUDGE CAMERA",    N64Color::white());
            n64_draw_text(52, 112, "A: HIDE THIS   B: BACK",     N64Color::white());
            n64_draw_text(52, 128, "SPEARMAN = PLAYER 1 START",  N64Color::from_rgb(160, 255, 160));
        }
        present();
    }
}

// -----------------------------------------------------------------------
// Title screen
// -----------------------------------------------------------------------
static std::string pretty(const char* s) {
    std::string out(s);
    for (char& ch : out) if (ch == '_') ch = ' ';
    if (out.size() > 4 && out.compare(out.size() - 4, 4, ".map") == 0) out.resize(out.size() - 4);
    return out;
}

static CachedTex s_logo, s_bg[3];
static const char* kBg[3] = {
    "images/terrain/grass/green.png", "images/terrain/hills/regular.png",
    "images/terrain/water/ocean-tile.png",
};

static void title_screen() {
    for (unsigned t = 0;; ++t) {
        begin_frame_textures();
        const N64InputState& in = n64_input_get(0);
        if (in.just_pressed[N64_ACTION_SELECT] || in.just_pressed[N64_ACTION_GAME_MENU]) {
            play(s_sfx_select);
            return;
        }

        n64_video_clear(N64Color::from_rgb(12, 16, 32));
        int off = (int)(t / 2) % (COL_STEP * 2);
        for (int c = 0; c < 15; ++c)
            for (int r = 0; r < 8; ++r) {
                int k = (c + 3 * r) % 3;
                N64TexHandle h = get_tex(kBg[k], s_bg[k]);
                if (h != N64_TEX_INVALID)
                    n64_draw_texture(LAYER_BG, h, (s16)(c * COL_STEP - off / 2), (s16)(r * ROW_STEP + (c & 1) * HALF_ROW - 16));
            }
        // Panel behind the title block
        draw_box(LAYER_UI, 24, 24, 272, 120, N64Color::from_rgb(0, 0, 24));
        // Drawn on the same layer as the panel, after it, so it is not covered.
        N64TexHandle lh = get_tex("images/misc/logo.png", s_logo);
        if (lh != N64_TEX_INVALID) {
            static int logo_w = 0;
            if (!logo_w) {
                const RomFsEntry* le = n64_romfs_find("images/misc/logo.png");
                logo_w = le ? le->width : 64;
            }
            n64_draw_texture(LAYER_UI, lh, (s16)((N64_SCREEN_W - logo_w) / 2), 34);
        }
        n64_draw_text(80, 100, "THE BATTLE FOR WESNOTH", N64Color::from_rgb(255, 224, 96));
        n64_draw_text(88, 114, "NINTENDO 64 EDITION", N64Color::white());
        if ((t / 30) % 2 == 0)
            n64_draw_text(104, 180, "PRESS A OR START", N64Color::from_rgb(160, 255, 160));
        char buf[64];
        snprintf(buf, sizeof(buf), "%d CAMPAIGNS  %d SCENARIOS", s_campaign_count, s_map_count);
        n64_draw_text(80, 200, buf, N64Color::from_rgb(160, 200, 255));
        present();
    }
}

static void browse() {
    int camp_sel = 0;
    for (;;) {
        std::vector<N64MenuItem> camps;
        for (int i = 0; i < s_campaign_count; ++i) {
            int n = 0;
            for (int m = 0; m < s_map_count; ++m) if (s_maps[m].campaign == i) ++n;
            char desc[48];
            snprintf(desc, sizeof(desc), "%d scenario maps", n);
            camps.emplace_back(pretty(s_campaigns[i]), std::string(desc), i);
        }
        int c = n64_menu_show("Select Campaign", camps, camp_sel, true);
        if (c == N64_MENU_CANCEL || c == N64_MENU_BACK) return;
        camp_sel = c;

        int map_sel = 0;
        for (;;) {
            std::vector<N64MenuItem> maps;
            std::vector<int> idx;
            for (int m = 0; m < s_map_count; ++m) {
                if (s_maps[m].campaign != c) continue;
                const char* slash = strrchr(s_maps[m].path, '/');
                maps.emplace_back(pretty(slash ? slash + 1 : s_maps[m].path), (int)idx.size());
                idx.push_back(m);
            }
            int s = n64_menu_show(pretty(s_campaigns[c]), maps, map_sel, true);
            if (s == N64_MENU_CANCEL || s == N64_MENU_BACK) break;
            map_sel = s;
            view_map(maps[s].label.c_str(), s_maps[idx[s]].path);
        }
    }
}

// -----------------------------------------------------------------------
int main() {
    debug_init_isviewer();
    debug_init_usblog();

    n64_memory_init();
    if (!n64_romfs_init()) {
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
        for (;;) {                                  // red screen: no ROMFS
            surface_t* d = display_get();
            graphics_fill_screen(d, 0xF800F800u);
            display_show(d);
        }
    }
    n64_video_init();
    n64_audio_init();
    n64_input_init();
    load_tables();

    s_sfx_move   = load_sfx("sounds/select.wav");
    s_sfx_select = load_sfx("sounds/button.wav");
    s_sfx_back   = load_sfx("sounds/expand.wav");

    for (;;) {
        title_screen();
        browse();
    }
}
