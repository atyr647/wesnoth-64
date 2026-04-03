/**
 * n64_combat_display.cpp
 *
 * N64 combat animation and damage display.
 *
 * On PC, Wesnoth shows elaborate sprite-based attack animations with
 * multiple frames, projectiles, sound effects, and hit/miss/death sequences.
 *
 * On N64, we show a simplified but faithful combat display:
 *   1. Units flash/blink during their attack
 *   2. Damage numbers float up from the target
 *   3. HP bars update in real time
 *   4. Death: unit sprite fades out
 *   5. Sound effects play for hit/miss
 *
 * This hooks into src/units/udisplay.cpp (unit_display namespace)
 * which is called by the attack action code.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_video.hpp"
#include "n64_audio.hpp"
#include "n64_romfs.hpp"

#include "../units/unit.hpp"
#include "../map/location.hpp"

// -----------------------------------------------------------------------
// Combat animation state
// -----------------------------------------------------------------------
struct CombatAnimState {
    bool    active;
    s16     attacker_x, attacker_y;
    s16     defender_x, defender_y;
    int     attacker_damage;      // damage dealt this strike
    int     defender_damage;
    bool    attacker_hit;         // did this strike hit?
    bool    defender_hit;
    int     frame;                // animation frame counter
    int     total_frames;         // total frames for this strike
};

static CombatAnimState s_combat;

// -----------------------------------------------------------------------
// Play a single combat strike animation
// -----------------------------------------------------------------------
void n64_combat_strike(s16 atk_x, s16 atk_y, s16 def_x, s16 def_y,
                        int damage, bool hit, bool attacker_turn) {
    s_combat.active = true;
    s_combat.attacker_x = atk_x;
    s_combat.attacker_y = atk_y;
    s_combat.defender_x = def_x;
    s_combat.defender_y = def_y;
    s_combat.frame = 0;
    s_combat.total_frames = 20; // ~0.6s at 30fps

    if (attacker_turn) {
        s_combat.attacker_damage = 0;
        s_combat.defender_damage = damage;
        s_combat.attacker_hit = true;
        s_combat.defender_hit = hit;
    } else {
        s_combat.attacker_damage = damage;
        s_combat.defender_damage = 0;
        s_combat.attacker_hit = hit;
        s_combat.defender_hit = true;
    }

    // Play hit or miss sound
    if (hit) {
        const RomFsEntry* sfx = n64_romfs_find("sounds/sword-1.pcm");
        if (sfx) {
            N64SfxHandle h = n64_sfx_load(sfx->rom_offset, sfx->byte_length);
            n64_sfx_play(CHAN_FX0, h, 220, 0);
        }
    }

    // Animate
    for (int f = 0; f < s_combat.total_frames; ++f) {
        s_combat.frame = f;

        // Flash the attacking unit (blink every 4 frames)
        bool flash = (f / 4) % 2 == 0;

        // Lunge: move attacker 2px toward defender during frames 4-8
        s16 lunge_dx = 0, lunge_dy = 0;
        if (f >= 4 && f < 8) {
            lunge_dx = (def_x > atk_x) ? 2 : (def_x < atk_x ? -2 : 0);
            lunge_dy = (def_y > atk_y) ? 2 : (def_y < atk_y ? -2 : 0);
        }

        // Draw attacker (flashing)
        if (flash || f > 10) {
            n64_draw_rect(LAYER_UNITS,
                          {(s16)(atk_x + 4 + lunge_dx), (s16)(atk_y + 4 + lunge_dy),
                           (s16)(N64_HEX_W - 8), (s16)(N64_HEX_H - 8)},
                          N64Color::from_rgb(100, 200, 100));
        }

        // Draw defender
        N64Color def_col = hit && f >= 6 && f < 12
            ? N64Color::from_rgb(255, 60, 60) // flash red on hit
            : N64Color::from_rgb(200, 100, 100);
        n64_draw_rect(LAYER_UNITS,
                      {(s16)(def_x + 4), (s16)(def_y + 4),
                       (s16)(N64_HEX_W - 8), (s16)(N64_HEX_H - 8)},
                      def_col);

        // Floating damage number (rises from frame 8 onward)
        if (hit && damage > 0 && f >= 8) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "-%d", damage);
            s16 float_y = def_y - (f - 8);
            n64_draw_text_shadowed(def_x + 2, float_y, buf,
                                    N64Color::from_rgb(255, 50, 50));
        }
        if (!hit && f >= 8 && f < 16) {
            n64_draw_text_shadowed(def_x - 2, def_y - (f - 8), "miss",
                                    N64Color::from_rgb(200, 200, 200));
        }

        // Flush and flip
        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();

        n64_audio_update();
    }

    s_combat.active = false;
}

// -----------------------------------------------------------------------
// Death animation — fade unit out
// -----------------------------------------------------------------------
void n64_combat_death(s16 unit_x, s16 unit_y) {
    const RomFsEntry* sfx = n64_romfs_find("sounds/human-die-1.pcm");
    if (sfx) {
        N64SfxHandle h = n64_sfx_load(sfx->rom_offset, sfx->byte_length);
        n64_sfx_play(CHAN_FX1, h, 220, 0);
    }

    // Fade out over 15 frames
    for (int f = 0; f < 15; ++f) {
        // Shrink the unit rectangle
        s16 shrink = (s16)(f / 2);
        if (shrink < N64_HEX_W / 2 - 2) {
            n64_draw_rect(LAYER_UNITS,
                          {(s16)(unit_x + 4 + shrink), (s16)(unit_y + 4 + shrink),
                           (s16)(N64_HEX_W - 8 - shrink * 2),
                           (s16)(N64_HEX_H - 8 - shrink * 2)},
                          N64Color::from_rgb(100, 0, 0));
        }

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();
        n64_audio_update();
    }
}

// -----------------------------------------------------------------------
// Level-up animation — flash gold
// -----------------------------------------------------------------------
void n64_combat_levelup(s16 unit_x, s16 unit_y, const char* new_type) {
    const RomFsEntry* sfx = n64_romfs_find("sounds/levelup.pcm");
    if (sfx) {
        N64SfxHandle h = n64_sfx_load(sfx->rom_offset, sfx->byte_length);
        n64_sfx_play(CHAN_FX1, h, 220, 0);
    }

    for (int f = 0; f < 30; ++f) {
        bool flash = (f / 3) % 2 == 0;
        N64Color c = flash
            ? N64Color::from_rgb(255, 255, 100)
            : N64Color::from_rgb(200, 200, 50);

        n64_draw_rect(LAYER_UNITS,
                      {(s16)(unit_x + 2), (s16)(unit_y + 2),
                       (s16)(N64_HEX_W - 4), (s16)(N64_HEX_H - 4)}, c);

        if (f > 15) {
            n64_draw_text_shadowed(unit_x - 10, unit_y - 12, "Level Up!",
                                    N64Color::from_rgb(255, 255, 0));
        }

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();
        n64_audio_update();
    }
}

// -----------------------------------------------------------------------
// Movement animation — slide unit between hexes
// -----------------------------------------------------------------------
void n64_move_unit(s16 from_x, s16 from_y, s16 to_x, s16 to_y, int steps) {
    int frames = steps > 0 ? 8 : 6; // longer path = faster per-step

    for (int f = 0; f <= frames; ++f) {
        float t = (float)f / frames;
        s16 cur_x = from_x + (s16)((to_x - from_x) * t);
        s16 cur_y = from_y + (s16)((to_y - from_y) * t);

        n64_draw_rect(LAYER_UNITS,
                      {(s16)(cur_x + 4), (s16)(cur_y + 4),
                       (s16)(N64_HEX_W - 8), (s16)(N64_HEX_H - 8)},
                      N64Color::from_rgb(100, 200, 100));

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();
        n64_audio_update();
    }
}

#endif // N64_BUILD
