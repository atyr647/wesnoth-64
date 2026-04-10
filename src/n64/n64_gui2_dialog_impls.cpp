/**
 * n64_gui2_dialog_impls.cpp
 *
 * N64 implementations of GUI2 dialog constructors and window_id() methods.
 *
 * The dialog .cpp files are not compiled for N64, so we provide the
 * out-of-line constructor bodies and REGISTER_DIALOG expansions here.
 * Destructors are all implicit (compiler-generated) per the headers.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_gui.hpp"

#include "../gui/dialogs/edit_label.hpp"
#include "../gui/dialogs/edit_text.hpp"
#include "../gui/dialogs/game_stats.hpp"
#include "../gui/dialogs/label_settings.hpp"
#include "../gui/dialogs/simple_item_selector.hpp"
#include "../gui/dialogs/preferences_dialog.hpp"
#include "../gui/dialogs/chat_log.hpp"
#include "../gui/dialogs/multiplayer/mp_change_control.hpp"
#include "../gui/dialogs/file_dialog.hpp"
#include "../gui/dialogs/unit_attack.hpp"
#include "../gui/dialogs/gamestate_inspector.hpp"

#include "../variable.hpp"
#include "../replay.hpp"

namespace gui2::dialogs {

// =====================================================================
// edit_label
// =====================================================================
edit_label::edit_label(std::string& label, bool& team_only)
    : modal_dialog("edit_label") {}

const std::string& edit_label::window_id() const {
    static const std::string id("edit_label");
    return id;
}

// =====================================================================
// edit_text
// =====================================================================
edit_text::edit_text(const std::string&, const std::string&, std::string& text, bool)
    : modal_dialog("edit_text") {}

const std::string& edit_text::window_id() const {
    static const std::string id("edit_text");
    return id;
}

// =====================================================================
// game_stats — has reference members that must be initialized
// =====================================================================
game_stats::game_stats(const display_context& board, const team& viewing_team, int& selected_side_number)
    : modal_dialog("game_stats")
    , board_(board)
    , viewing_team_(viewing_team)
    , selected_side_number_(selected_side_number) {}

const std::string& game_stats::window_id() const {
    static const std::string id("game_stats");
    return id;
}

// =====================================================================
// label_settings — has reference member
// =====================================================================
label_settings::label_settings(display_context& dc)
    : modal_dialog("label_settings")
    , viewer_(dc) {}

const std::string& label_settings::window_id() const {
    static const std::string id("label_settings");
    return id;
}

// =====================================================================
// simple_item_selector
// selected_index() etc. are defined inline in header — don't redefine
// =====================================================================
simple_item_selector::simple_item_selector(
    const std::string&, const std::string&, const list_type& items, bool, bool)
    : modal_dialog("simple_item_selector") {}

const std::string& simple_item_selector::window_id() const {
    static const std::string id("simple_item_selector");
    return id;
}

// =====================================================================
// preferences_dialog — has const pair<int,int>& member
// =====================================================================
// The initial_index_ is a const reference to a pair stored as a
// static to avoid dangling reference issues.
static const std::pair<int,int> s_pref_default_index{0, 0};

preferences_dialog::preferences_dialog(const pref_constants::PREFERENCE_VIEW)
    : modal_dialog("preferences")
    , initial_index_(s_pref_default_index) {}

const std::string& preferences_dialog::window_id() const {
    static const std::string id("preferences");
    return id;
}

// =====================================================================
// chat_log
// =====================================================================
chat_log::chat_log(const vconfig&, const replay&)
    : modal_dialog("chat_log") {}

const std::string& chat_log::window_id() const {
    static const std::string id("chat_log");
    return id;
}

// =====================================================================
// mp_change_control — has reference member
// =====================================================================
mp_change_control::mp_change_control(events::menu_handler& mh)
    : modal_dialog("mp_change_control")
    , menu_handler_(mh) {}

const std::string& mp_change_control::window_id() const {
    static const std::string id("mp_change_control");
    return id;
}

// =====================================================================
// file_dialog — all setters defined inline in header
// =====================================================================
file_dialog::file_dialog()
    : modal_dialog("file_dialog") {}

const std::string& file_dialog::window_id() const {
    static const std::string id("file_dialog");
    return id;
}

// =====================================================================
// unit_attack — has reference members, get_selected_weapon() is inline
// =====================================================================
unit_attack::unit_attack(const unit_map::iterator& attacker_itor,
                          const unit_map::iterator& defender_itor,
                          std::vector<battle_context>&& weapons,
                          const int best_weapon,
                          std::vector<gui2::widget_data>& bc_widget_data_vector)
    : modal_dialog("unit_attack")
    , attacker_itor_(attacker_itor)
    , defender_itor_(defender_itor)
    , weapons_(std::move(weapons))
    , best_weapon_(best_weapon)
    , bc_widget_data_vector_(bc_widget_data_vector) {}

const std::string& unit_attack::window_id() const {
    static const std::string id("unit_attack");
    return id;
}

// =====================================================================
// gamestate_inspector — has reference members
// =====================================================================
gamestate_inspector::gamestate_inspector(const config& vars,
                                          const game_events::manager& events,
                                          const display_context& dc,
                                          const std::string&)
    : modal_dialog("gamestate_inspector")
    , vars_(vars)
    , events_(events)
    , dc_(dc) {}

const std::string& gamestate_inspector::window_id() const {
    static const std::string id("gamestate_inspector");
    return id;
}

} // namespace gui2::dialogs

#endif // N64_BUILD
