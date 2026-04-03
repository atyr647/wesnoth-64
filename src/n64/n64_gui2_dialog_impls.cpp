/**
 * n64_gui2_dialog_impls.cpp
 *
 * N64 implementations of GUI2 dialog constructors/destructors.
 *
 * Game code (menu_events.cpp, mouse_events.cpp, savegame.cpp) includes
 * the real dialog headers and uses DEFINE_SIMPLE_EXECUTE_WRAPPER /
 * DEFINE_SIMPLE_DISPLAY_WRAPPER macros. These templates instantiate
 * dialog constructors, call modal_dialog::show(), and destroy the object.
 *
 * Since the dialog .cpp files are not compiled for N64, we provide
 * the constructors, destructors, window_id() methods, and custom
 * execute/display methods here.
 *
 * The modal_dialog::show() (now compiled from the real source) returns
 * true, so all "execute" dialogs report success.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_gui.hpp"

// Include the REAL dialog headers — these give us the class declarations
// that the game code also sees
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

#include "../vconfig.hpp"
#include "../replay.hpp"

namespace gui2::dialogs {

// =====================================================================
// edit_label — used by menu_events.cpp for terrain labels
// Template: DEFINE_SIMPLE_EXECUTE_WRAPPER
// =====================================================================
edit_label::edit_label(std::string&, bool&)
    : modal_dialog("edit_label") {}
edit_label::~edit_label() = default;
const std::string& edit_label::window_id() const {
    static const std::string id("edit_label");
    return id;
}

// =====================================================================
// edit_text — used by menu_events.cpp for unit rename etc.
// Template: DEFINE_SIMPLE_EXECUTE_WRAPPER
// =====================================================================
edit_text::edit_text(const std::string&, const std::string&, std::string&, bool)
    : modal_dialog("edit_text") {}
edit_text::~edit_text() = default;
const std::string& edit_text::window_id() const {
    static const std::string id("edit_text");
    return id;
}

// =====================================================================
// game_stats — used by menu_events.cpp for statistics display
// Custom execute method
// =====================================================================
game_stats::game_stats(const display_context&, const team&, int&)
    : modal_dialog("game_stats") {}
game_stats::~game_stats() = default;
const std::string& game_stats::window_id() const {
    static const std::string id("game_stats");
    return id;
}

// =====================================================================
// label_settings — used by menu_events.cpp
// Template: DEFINE_SIMPLE_EXECUTE_WRAPPER
// =====================================================================
label_settings::label_settings(display_context&)
    : modal_dialog("label_settings") {}
label_settings::~label_settings() = default;
const std::string& label_settings::window_id() const {
    static const std::string id("label_settings");
    return id;
}

// =====================================================================
// simple_item_selector — used by menu_events.cpp for debug scenario picker
// No template macro — constructed and shown manually
// =====================================================================
simple_item_selector::simple_item_selector(
    const std::string&, const std::string&, const list_type&, bool, bool)
    : modal_dialog("simple_item_selector") {}
simple_item_selector::~simple_item_selector() = default;
const std::string& simple_item_selector::window_id() const {
    static const std::string id("simple_item_selector");
    return id;
}
int simple_item_selector::selected_index() const { return -1; }

// =====================================================================
// preferences_dialog — used by menu_events.cpp
// Template: DEFINE_SIMPLE_DISPLAY_WRAPPER
// =====================================================================
preferences_dialog::preferences_dialog(const pref_constants::PREFERENCE_VIEW)
    : modal_dialog("preferences") {}
preferences_dialog::~preferences_dialog() = default;
const std::string& preferences_dialog::window_id() const {
    static const std::string id("preferences");
    return id;
}

// =====================================================================
// chat_log — used by menu_events.cpp
// Template: DEFINE_SIMPLE_DISPLAY_WRAPPER
// =====================================================================
chat_log::chat_log(const vconfig&, const replay&)
    : modal_dialog("chat_log") {}
chat_log::~chat_log() = default;
const std::string& chat_log::window_id() const {
    static const std::string id("chat_log");
    return id;
}

// =====================================================================
// mp_change_control — used by menu_events.cpp
// Template: DEFINE_SIMPLE_DISPLAY_WRAPPER
// =====================================================================
mp_change_control::mp_change_control(events::menu_handler&)
    : modal_dialog("mp_change_control") {}
mp_change_control::~mp_change_control() = default;
const std::string& mp_change_control::window_id() const {
    static const std::string id("mp_change_control");
    return id;
}

// =====================================================================
// file_dialog — used by menu_events.cpp (builder pattern + show())
// =====================================================================
file_dialog::file_dialog()
    : modal_dialog("file_dialog") {}
file_dialog::~file_dialog() = default;
const std::string& file_dialog::window_id() const {
    static const std::string id("file_dialog");
    return id;
}
file_dialog& file_dialog::set_title(const std::string&) { return *this; }
file_dialog& file_dialog::set_message(const std::string&) { return *this; }
file_dialog& file_dialog::set_ok_label(const std::string&) { return *this; }
file_dialog& file_dialog::set_path(const std::string&) { return *this; }
file_dialog& file_dialog::set_filename(const std::string&) { return *this; }
file_dialog& file_dialog::set_extension(const std::string&) { return *this; }
file_dialog& file_dialog::set_read_only(bool) { return *this; }
file_dialog& file_dialog::set_save_mode(bool) { return *this; }
std::string file_dialog::path() const { return ""; }

// =====================================================================
// unit_attack — used by mouse_events.cpp for attack dialog
// =====================================================================
unit_attack::unit_attack(const unit_map::iterator&, const unit_map::iterator&,
                          std::vector<battle_context>&&, const int,
                          std::vector<gui2::widget_data>&)
    : modal_dialog("unit_attack") {}
unit_attack::~unit_attack() = default;
const std::string& unit_attack::window_id() const {
    static const std::string id("unit_attack");
    return id;
}
int unit_attack::get_selected_weapon() const { return 0; }

// =====================================================================
// gamestate_inspector — used by hotkey system for debug
// Template: DEFINE_SIMPLE_DISPLAY_WRAPPER
// =====================================================================
gamestate_inspector::gamestate_inspector(const config&, const game_events::manager&,
                                          const display_context&, const std::string&)
    : modal_dialog("gamestate_inspector") {}
gamestate_inspector::~gamestate_inspector() = default;
const std::string& gamestate_inspector::window_id() const {
    static const std::string id("gamestate_inspector");
    return id;
}

} // namespace gui2::dialogs

#endif // N64_BUILD
