/**
 * n64_display.cpp
 *
 * N64 replacement for critical display methods that depend on SDL.
 *
 * Rather than replacing the entire 4000-line display.cpp, we provide
 * N64 implementations of the methods that touch SDL directly. The rest
 * of display.cpp (game logic, hex math, overlay management) compiles
 * as-is because it uses our SDL/Boost shims.
 *
 * This file provides N64-specific overrides compiled alongside display.cpp.
 * Functions here are called via #ifdef N64_BUILD blocks inserted into
 * the display class, or via virtual dispatch in game_display.
 *
 * Key rendering adaptations:
 *   - Hex tiles: 72px → 18px (1/4 scale)
 *   - Drawing buffer: SDL vertex list → N64 draw command queue
 *   - Minimap: SDL texture → N64 8-color bitmap
 *   - Fog/shroud: alpha blend → 1-bit coverage mask
 *   - Scrolling: pixel-precise → tile-granular (snap to hex grid)
 */

#ifdef N64_BUILD

#include "n64_video.hpp"
#include "n64_romfs.hpp"
#include "n64_platform.hpp"
#include "n64_memory.hpp"

#include "../display.hpp"
#include "../game_display.hpp"
#include "../terrain/builder.hpp"
#include "../map/map.hpp"
#include "../units/unit.hpp"
#include "../units/map.hpp"
#include "../team.hpp"

// -----------------------------------------------------------------------
// Hex geometry constants (N64 scale)
// -----------------------------------------------------------------------
static constexpr int HEX_SIZE = N64_HEX_SIZE; // 18px
static constexpr int HEX_W    = HEX_SIZE;
static constexpr int HEX_H    = HEX_SIZE;
static constexpr int HEX_STAGGER = HEX_H / 2;

// -----------------------------------------------------------------------
// Texture cache for terrain tiles and unit sprites
// Maps terrain string IDs → N64TexHandle
// -----------------------------------------------------------------------
static constexpr int MAX_TERRAIN_TEX = 256;
static constexpr int MAX_UNIT_TEX    = 128;

struct TerrainTexEntry {
    u16           terrain_hash;
    N64TexHandle  handle;
    bool          valid;
};

static TerrainTexEntry s_terrain_tex[MAX_TERRAIN_TEX];
static int             s_terrain_tex_count = 0;

struct UnitTexEntry {
    u16           type_hash;
    N64TexHandle  handle;
    bool          valid;
};

static UnitTexEntry s_unit_tex[MAX_UNIT_TEX];
static int          s_unit_tex_count = 0;

// -----------------------------------------------------------------------
// Simple hash for terrain/unit type strings
// -----------------------------------------------------------------------
static u16 str_hash16(const char* s) {
    u16 h = 0;
    while (*s) { h = h * 31 + (u16)*s++; }
    return h;
}

// -----------------------------------------------------------------------
// Load or find a terrain texture
// -----------------------------------------------------------------------
static N64TexHandle get_terrain_texture(const std::string& terrain_id) {
    u16 h = str_hash16(terrain_id.c_str());
    for (int i = 0; i < s_terrain_tex_count; ++i) {
        if (s_terrain_tex[i].valid && s_terrain_tex[i].terrain_hash == h)
            return s_terrain_tex[i].handle;
    }

    // Try to load from ROM
    std::string rom_path = "images/terrain/" + terrain_id + ".tex";
    const RomFsEntry* entry = n64_romfs_find(rom_path.c_str());
    N64TexHandle tex = N64_TEX_INVALID;
    if (entry) {
        tex = n64_tex_load(entry->rom_offset, HEX_W, HEX_H, true);
    }

    if (s_terrain_tex_count < MAX_TERRAIN_TEX) {
        s_terrain_tex[s_terrain_tex_count++] = { h, tex, true };
    }
    return tex;
}

// -----------------------------------------------------------------------
// Load or find a unit sprite texture
// -----------------------------------------------------------------------
static N64TexHandle get_unit_texture(const std::string& type_id) {
    u16 h = str_hash16(type_id.c_str());
    for (int i = 0; i < s_unit_tex_count; ++i) {
        if (s_unit_tex[i].valid && s_unit_tex[i].type_hash == h)
            return s_unit_tex[i].handle;
    }

    std::string rom_path = "images/units/" + type_id + ".tex";
    const RomFsEntry* entry = n64_romfs_find(rom_path.c_str());
    N64TexHandle tex = N64_TEX_INVALID;
    if (entry) {
        tex = n64_tex_load(entry->rom_offset, HEX_W, HEX_H, true);
    }

    if (s_unit_tex_count < MAX_UNIT_TEX) {
        s_unit_tex[s_unit_tex_count++] = { h, tex, true };
    }
    return tex;
}

// -----------------------------------------------------------------------
// n64_draw_game_map — render the visible portion of the hex map
//
// Called from display::expose() or game_display::draw_invalidated()
// -----------------------------------------------------------------------
void n64_draw_game_map(const gamemap& map, const unit_map& units,
                        const std::vector<team>& teams,
                        int viewing_team_idx,
                        int scroll_x, int scroll_y,
                        const map_location& selected_hex,
                        const map_location& mouseover_hex) {
    const team& vteam = teams[viewing_team_idx];

    // Determine visible hex range based on scroll position
    int start_col = scroll_x / (HEX_W * 3 / 4);
    int start_row = scroll_y / HEX_H;
    int end_col   = start_col + (N64_SCREEN_W / (HEX_W * 3 / 4)) + 2;
    int end_row   = start_row + (N64_SCREEN_H / HEX_H) + 2;

    // Clamp to map bounds
    start_col = std::max(0, start_col);
    start_row = std::max(0, start_row);
    end_col   = std::min(end_col, map.w());
    end_row   = std::min(end_row, map.h());

    // --- Layer 0: Terrain background ---
    for (int x = start_col; x < end_col; ++x) {
        for (int y = start_row; y < end_row; ++y) {
            map_location loc(x, y);
            if (!map.on_board(loc)) continue;

            // Fog/shroud check
            if (vteam.shrouded(loc)) continue;

            // Get terrain string
            const terrain_type& tt = map.get_terrain_info(loc);
            N64TexHandle tex = get_terrain_texture(tt.id());

            // Calculate screen position
            N64Point sp = n64_hex_to_screen(x, y, scroll_x, scroll_y);

            // Skip if off screen
            if (sp.x + HEX_W < 0 || sp.x > N64_SCREEN_W ||
                sp.y + HEX_H < 0 || sp.y > N64_SCREEN_H)
                continue;

            if (tex != N64_TEX_INVALID) {
                n64_draw_texture(LAYER_BG, tex, sp.x, sp.y);
            } else {
                // Fallback: colored rectangle based on terrain type
                N64Color c;
                if (tt.is_castle())       c = N64Color::from_rgb(128, 128, 128);
                else if (tt.is_village())  c = N64Color::from_rgb(200, 180, 100);
                else if (tt.is_keep())     c = N64Color::from_rgb(160, 160, 160);
                else                       c = N64Color::from_rgb(80, 140, 80);  // grass
                n64_draw_rect(LAYER_BG, {sp.x, sp.y, HEX_W, HEX_H}, c);
            }

            // Fog overlay (dimmed)
            if (vteam.fogged(loc)) {
                n64_draw_rect(LAYER_EFFECTS, {sp.x, sp.y, HEX_W, HEX_H},
                              N64Color::from_rgba(0, 0, 0, 128));
            }
        }
    }

    // --- Layer 2: Units ---
    for (const auto& up : units) {
        const unit& u = up;
        map_location loc = u.get_location();

        if (vteam.shrouded(loc)) continue;
        if (vteam.fogged(loc) && !vteam.is_enemy(u.side())) continue;

        N64Point sp = n64_hex_to_screen(loc.wml_x(), loc.wml_y(), scroll_x, scroll_y);
        if (sp.x + HEX_W < 0 || sp.x > N64_SCREEN_W ||
            sp.y + HEX_H < 0 || sp.y > N64_SCREEN_H)
            continue;

        N64TexHandle tex = get_unit_texture(u.type_id());
        if (tex != N64_TEX_INVALID) {
            n64_draw_texture(LAYER_UNITS, tex, sp.x, sp.y);
        } else {
            // Fallback: colored dot for the unit's team color
            N64Color tc = N64Color::from_rgb(
                (u.side() * 73) & 0xFF,
                (u.side() * 137 + 50) & 0xFF,
                (u.side() * 211 + 100) & 0xFF);
            n64_draw_rect(LAYER_UNITS, {(s16)(sp.x + 4), (s16)(sp.y + 4),
                                         (s16)(HEX_W - 8), (s16)(HEX_H - 8)}, tc);
        }

        // HP bar (2px wide, left side of hex)
        float hp_pct = (float)u.hitpoints() / (float)u.max_hitpoints();
        s16 bar_h = (s16)(HEX_H * hp_pct);
        N64Color hp_color = hp_pct > 0.5f
            ? N64Color::from_rgb(0, 255, 0)
            : (hp_pct > 0.25f ? N64Color::from_rgb(255, 255, 0) : N64Color::from_rgb(255, 0, 0));
        n64_draw_rect(LAYER_UI, {sp.x, (s16)(sp.y + HEX_H - bar_h), 2, bar_h}, hp_color);

        // XP bar (2px wide, right side)
        if (u.can_advance()) {
            float xp_pct = (float)u.experience() / (float)u.max_experience();
            s16 xp_h = (s16)(HEX_H * xp_pct);
            n64_draw_rect(LAYER_UI,
                          {(s16)(sp.x + HEX_W - 2), (s16)(sp.y + HEX_H - xp_h), 2, xp_h},
                          N64Color::from_rgb(64, 128, 255));
        }
    }

    // --- Selected hex highlight ---
    if (map.on_board(selected_hex)) {
        N64Point sp = n64_hex_to_screen(selected_hex.wml_x(), selected_hex.wml_y(),
                                         scroll_x, scroll_y);
        n64_draw_rect_outline(LAYER_EFFECTS, {sp.x, sp.y, HEX_W, HEX_H},
                               N64Color::from_rgb(255, 255, 255));
    }

    // --- Mouseover hex highlight ---
    if (map.on_board(mouseover_hex) && mouseover_hex != selected_hex) {
        N64Point sp = n64_hex_to_screen(mouseover_hex.wml_x(), mouseover_hex.wml_y(),
                                         scroll_x, scroll_y);
        n64_draw_rect_outline(LAYER_EFFECTS, {sp.x, sp.y, HEX_W, HEX_H},
                               N64Color::from_rgb(200, 200, 200));
    }
}

// -----------------------------------------------------------------------
// n64_draw_minimap — render a small overview of the entire map
//
// Positioned in the bottom-right corner: 60x45 pixels
// Each hex → 1 or 2 pixels
// -----------------------------------------------------------------------
static constexpr int MINIMAP_W = 60;
static constexpr int MINIMAP_H = 45;
static constexpr int MINIMAP_X = N64_SCREEN_W - MINIMAP_W - 2;
static constexpr int MINIMAP_Y = N64_SCREEN_H - MINIMAP_H - 2;

void n64_draw_minimap(const gamemap& map, const unit_map& units,
                       const std::vector<team>& teams,
                       int viewing_team_idx,
                       int scroll_x, int scroll_y) {
    // Background
    n64_draw_rect(LAYER_UI, {MINIMAP_X - 1, MINIMAP_Y - 1,
                              MINIMAP_W + 2, MINIMAP_H + 2},
                  N64Color::black());

    // Scale factor
    float sx = (float)MINIMAP_W / (float)std::max(map.w(), 1);
    float sy = (float)MINIMAP_H / (float)std::max(map.h(), 1);

    const team& vteam = teams[viewing_team_idx];

    // Terrain dots
    for (int x = 0; x < map.w(); ++x) {
        for (int y = 0; y < map.h(); ++y) {
            map_location loc(x, y);
            if (vteam.shrouded(loc)) continue;

            const terrain_type& tt = map.get_terrain_info(loc);
            N64Color c;
            if (tt.is_castle())      c = N64Color::from_rgb(160, 160, 160);
            else if (tt.is_village()) c = N64Color::from_rgb(200, 180, 100);
            else if (tt.is_keep())    c = N64Color::from_rgb(180, 180, 180);
            else                      c = N64Color::from_rgb(60, 120, 60);

            if (vteam.fogged(loc))
                c = N64Color::from_rgb(40, 40, 40);

            s16 px = MINIMAP_X + (s16)(x * sx);
            s16 py = MINIMAP_Y + (s16)(y * sy);
            n64_draw_rect(LAYER_UI, {px, py, 1, 1}, c);
        }
    }

    // Unit dots
    for (const auto& up : units) {
        const unit& u = up;
        map_location loc = u.get_location();
        if (vteam.shrouded(loc)) continue;

        N64Color tc = N64Color::from_rgb(
            (u.side() * 73) & 0xFF,
            (u.side() * 137 + 50) & 0xFF,
            (u.side() * 211 + 100) & 0xFF);

        s16 px = MINIMAP_X + (s16)(loc.wml_x() * sx);
        s16 py = MINIMAP_Y + (s16)(loc.wml_y() * sy);
        n64_draw_rect(LAYER_UI, {px, py, 2, 2}, tc);
    }

    // Viewport rectangle
    s16 vx = MINIMAP_X + (s16)(scroll_x / (HEX_W * 3 / 4) * sx);
    s16 vy = MINIMAP_Y + (s16)(scroll_y / HEX_H * sy);
    s16 vw = (s16)((N64_SCREEN_W / (HEX_W * 3 / 4)) * sx);
    s16 vh = (s16)((N64_SCREEN_H / HEX_H) * sy);
    n64_draw_rect_outline(LAYER_UI, {vx, vy, vw, vh},
                           N64Color::from_rgb(255, 255, 255));
}

// -----------------------------------------------------------------------
// n64_draw_sidebar — right-side info panel (selected unit stats)
//
// Positioned: right edge, 70px wide, full height
// -----------------------------------------------------------------------
static constexpr int SIDEBAR_W = 70;
static constexpr int SIDEBAR_X = N64_SCREEN_W - SIDEBAR_W;

void n64_draw_sidebar(const unit* selected_unit,
                       const std::vector<team>& teams,
                       int active_side, int turn_num,
                       int gold) {
    // Background
    n64_draw_rect(LAYER_UI, {SIDEBAR_X, 0, SIDEBAR_W, N64_SCREEN_H},
                  N64Color::from_rgba(0, 0, 0, 180));

    s16 y = 2;
    char buf[64];

    // Turn info
    std::snprintf(buf, sizeof(buf), "Turn %d", turn_num);
    n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::white());
    y += 10;

    // Gold
    std::snprintf(buf, sizeof(buf), "Gold: %d", gold);
    n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::from_rgb(255, 255, 0));
    y += 10;

    // Separator
    n64_draw_hline(LAYER_UI, SIDEBAR_X, N64_SCREEN_W - 1, y, N64Color::from_rgb(80, 80, 80));
    y += 3;

    // Selected unit info
    if (selected_unit) {
        const unit& u = *selected_unit;

        // Unit name
        n64_draw_text(SIDEBAR_X + 2, y, u.name().c_str(), N64Color::white());
        y += 10;

        // Level
        std::snprintf(buf, sizeof(buf), "Lv.%d %s", u.level(), u.type_name().c_str());
        n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::from_rgb(200, 200, 200));
        y += 10;

        // HP
        std::snprintf(buf, sizeof(buf), "HP: %d/%d", u.hitpoints(), u.max_hitpoints());
        N64Color hp_col = u.hitpoints() > u.max_hitpoints() / 2
            ? N64Color::from_rgb(0, 255, 0)
            : (u.hitpoints() > u.max_hitpoints() / 4
                ? N64Color::from_rgb(255, 255, 0)
                : N64Color::from_rgb(255, 0, 0));
        n64_draw_text(SIDEBAR_X + 2, y, buf, hp_col);
        y += 10;

        // XP
        std::snprintf(buf, sizeof(buf), "XP: %d/%d", u.experience(), u.max_experience());
        n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::from_rgb(64, 128, 255));
        y += 10;

        // Movement
        std::snprintf(buf, sizeof(buf), "MP: %d/%d", u.movement_left(), u.total_movement());
        n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::from_rgb(200, 200, 200));
        y += 10;

        // Separator
        n64_draw_hline(LAYER_UI, SIDEBAR_X, N64_SCREEN_W - 1, y,
                        N64Color::from_rgb(80, 80, 80));
        y += 3;

        // Attacks
        for (const auto& atk : u.attacks()) {
            std::snprintf(buf, sizeof(buf), "%s %d-%d",
                          atk.name().c_str(), atk.damage(), atk.num_attacks());
            n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::from_rgb(180, 180, 180));
            y += 10;

            // Attack type and range
            std::snprintf(buf, sizeof(buf), " %s %s",
                          atk.type().c_str(), atk.range().c_str());
            n64_draw_text(SIDEBAR_X + 2, y, buf, N64Color::from_rgb(140, 140, 140));
            y += 10;

            if (y > N64_SCREEN_H - 60) break; // don't overflow
        }

        // Traits
        y += 3;
        for (const auto& trait : u.get_traits_list()) {
            if (y > N64_SCREEN_H - 50) break;
            n64_draw_text(SIDEBAR_X + 2, y, trait.c_str(),
                          N64Color::from_rgb(160, 200, 160));
            y += 10;
        }
    }
}

// -----------------------------------------------------------------------
// n64_draw_top_bar — turn/side indicator + action hints
// -----------------------------------------------------------------------
void n64_draw_top_bar(int active_side, const char* side_name,
                       bool your_turn) {
    // Background bar
    n64_draw_rect(LAYER_UI, {0, 0, (s16)(N64_SCREEN_W - SIDEBAR_W), 10},
                  N64Color::from_rgba(0, 0, 0, 200));

    if (your_turn) {
        n64_draw_text(2, 1, side_name, N64Color::from_rgb(0, 255, 0));
        n64_draw_text(N64_SCREEN_W - SIDEBAR_W - 80, 1, "D-Down:End Turn",
                      N64Color::from_rgb(180, 180, 180));
    } else {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Side %d thinking...", active_side);
        n64_draw_text(2, 1, buf, N64Color::from_rgb(200, 200, 100));
    }
}

// -----------------------------------------------------------------------
// n64_draw_bottom_bar — terrain info for mouseover hex
// -----------------------------------------------------------------------
void n64_draw_bottom_bar(const gamemap& map, map_location mouseover_hex) {
    if (!map.on_board(mouseover_hex)) return;

    s16 bar_y = N64_SCREEN_H - 10;
    n64_draw_rect(LAYER_UI, {0, bar_y, (s16)(N64_SCREEN_W - SIDEBAR_W), 10},
                  N64Color::from_rgba(0, 0, 0, 200));

    const terrain_type& tt = map.get_terrain_info(mouseover_hex);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s Def:%d%%",
                  tt.name().c_str(), 0);
    n64_draw_text(2, bar_y + 1, buf, N64Color::from_rgb(200, 200, 200));

    std::snprintf(buf, sizeof(buf), "(%d,%d)",
                  mouseover_hex.wml_x() + 1, mouseover_hex.wml_y() + 1);
    n64_draw_text(N64_SCREEN_W - SIDEBAR_W - 50, bar_y + 1, buf,
                  N64Color::from_rgb(140, 140, 140));
}

// -----------------------------------------------------------------------
// Flush terrain/unit texture caches (between scenarios)
// -----------------------------------------------------------------------
void n64_display_flush_caches() {
    s_terrain_tex_count = 0;
    s_unit_tex_count = 0;
    n64_texcache_flush();
}

#endif // N64_BUILD
