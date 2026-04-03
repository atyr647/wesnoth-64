/**
 * n64_unit_display.cpp
 *
 * N64 replacement for src/units/udisplay.cpp.
 *
 * On PC, unit animations use SDL sprite sheets with frame interpolation.
 * On N64, animations are simplified: movement shows instant position
 * changes, attacks show brief flash effects, deaths remove the sprite.
 * The core game logic (damage, HP deduction) is identical.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_video.hpp"

#include "../units/udisplay.hpp"
#include "../units/unit.hpp"
#include "../units/map.hpp"
#include "../game_display.hpp"
#include "../game_board.hpp"
#include "../display.hpp"
#include "../resources.hpp"

namespace unit_display {

// -----------------------------------------------------------------------
// unit_movement_animator — simplified for N64
// -----------------------------------------------------------------------
unit_movement_animator::unit_movement_animator(
    const std::vector<map_location>& path, bool animate, bool force_scroll)
    : disp_(game_display::get_singleton())
    , can_draw_(disp_ != nullptr)
    , animate_(animate)
    , force_scroll_(force_scroll)
    , animator_()
    , wait_until_(std::chrono::milliseconds::min())
    , shown_unit_()
    , path_(path)
    , current_(0)
    , temp_unit_ptr_()
    , was_hidden_(false)
    , is_enemy_(false)
{}

unit_movement_animator::~unit_movement_animator() {}

void unit_movement_animator::start(const unit_ptr&) {
    current_ = 0;
}

void unit_movement_animator::proceed_to(const unit_ptr& u, std::size_t path_index,
                                         bool, bool) {
    if (!u) return;
    current_ = path_index;
    if (current_ < path_.size()) {
        u->set_location(path_[current_]);
    }
}

void unit_movement_animator::wait_for_anims() {
    // On N64, animations are instant
}

void unit_movement_animator::finish(const unit_ptr& u, map_location::direction) {
    if (!u || path_.empty()) return;
    u->set_location(path_.back());
}

void unit_movement_animator::replace_temporary(const unit_ptr&) {}
void unit_movement_animator::update_shown_unit() {}

// -----------------------------------------------------------------------
// Free functions
// -----------------------------------------------------------------------

void move_unit(const std::vector<map_location>& path, const unit_ptr& u,
               bool animate, map_location::direction dir, bool force_scroll) {
    if (!u || path.empty()) return;

    unit_movement_animator anim(path, animate, force_scroll);
    anim.start(u);
    for (std::size_t i = 1; i < path.size(); ++i) {
        anim.proceed_to(u, i);
    }
    anim.finish(u, dir);
}

void unit_draw_weapon(const map_location&, unit&,
                       const const_attack_ptr&, const const_attack_ptr&,
                       const map_location&, const unit_ptr&) {
    // Brief flash could be shown; for now no-op
}

void unit_sheath_weapon(const map_location&, const unit_ptr&,
                         const const_attack_ptr&, const const_attack_ptr&,
                         const map_location&, const unit_ptr&) {
    // No-op on N64
}

void unit_die(const map_location&, unit& u,
              const const_attack_ptr&, const const_attack_ptr&,
              const map_location&, const unit_ptr&) {
    // On N64, the death effect is handled by n64_combat_display
    // The unit sprite will simply disappear when removed from the unit map
}

void unit_attack(display*, game_board&,
                  const map_location&, const map_location&, int,
                  const attack_type&, const const_attack_ptr&,
                  int, const std::string&, int, const std::string&,
                  const std::vector<std::string>*, bool) {
    // Combat animation handled by n64_combat_display.cpp
    // This function is called from attack::perform() — the actual
    // damage/HP changes happen in the caller, not here.
}

void unit_recruited(const map_location&, const map_location&) {
    // Unit appears on map immediately — no recruitment animation
}

void unit_healing(unit&, const std::vector<unit*>&, int,
                   const std::string&) {
    // Healing is applied directly; no animation needed
}

} // namespace unit_display

#endif // N64_BUILD
