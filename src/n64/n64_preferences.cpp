/**
 * n64_preferences.cpp
 *
 * N64 implementation of the preferences::prefs singleton class.
 *
 * On PC, preferences are stored in a config file on disk. On N64,
 * preferences use hardcoded defaults suitable for the console.
 * The PREF_GETTER_SETTER macros in the header are inline and work
 * with the preferences_ config member — they return their default
 * value when the config key is absent.
 *
 * This file provides implementations of all non-inline prefs methods.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_gui.hpp"

#include "../preferences/preferences.hpp"
#include "../game_config.hpp"
#include "../terrain/translation.hpp"
#include "../sdl/point.hpp"

#include <string>
#include <vector>
#include <set>
#include <chrono>

// prefs class is at global scope (not inside namespace preferences)

// -----------------------------------------------------------------------
// Constructor / Destructor
// -----------------------------------------------------------------------
prefs::prefs()
    : preferences_()
    , fps_(false)
    , completed_campaigns_()
    , encountered_units_set_()
    , encountered_terrains_set_()
    , history_map_()
    , acquaintances_()
    , advanced_prefs_()
{
    // Set N64-appropriate defaults
    preferences_["fullscreen"] = true;
    preferences_["animate_map"] = true;
    preferences_["animate_water"] = false; // save CPU
    preferences_["music_volume"] = 180;
    preferences_["sound_volume"] = 200;
    preferences_["sound"] = true;
    preferences_["music"] = true;
    preferences_["turn_bell"] = true;
    preferences_["scroll_speed"] = 30;
    preferences_["font_scaling"] = 100;
    preferences_["pixel_scale"] = 1;
    preferences_["grid"] = false;
    preferences_["idle_anim"] = true;
    preferences_["save_replays"] = false; // save SRAM space
    preferences_["auto_save_max"] = 1;
    preferences_["floating_labels"] = true;
    preferences_["show_side_colors"] = true;
    preferences_["ally_sighted_interrupts"] = true;
}

prefs::~prefs() {}

// -----------------------------------------------------------------------
// File I/O — no-ops on N64 (prefs are hardcoded or saved to SRAM)
// -----------------------------------------------------------------------
void prefs::write_preferences() {}
void prefs::load_advanced_prefs(const game_config_view&) {}
void prefs::migrate_preferences(const std::string&) {}
void prefs::reload_preferences() {}
std::set<std::string> prefs::all_attributes() { return {}; }

// -----------------------------------------------------------------------
// Hotkeys
// -----------------------------------------------------------------------
void prefs::load_hotkeys() {}
void prefs::save_hotkeys() {}
void prefs::clear_hotkeys() {}

// -----------------------------------------------------------------------
// Show FPS / message_private — simple boolean state
// -----------------------------------------------------------------------
bool prefs::show_fps() { return fps_; }
void prefs::set_show_fps(bool v) { fps_ = v; }

bool prefs::message_private() { return false; }
void prefs::set_message_private(bool) {}

// -----------------------------------------------------------------------
// Getter-only methods
// -----------------------------------------------------------------------
bool prefs::message_bell() { return true; }
bool prefs::middle_click_scrolls() { return false; }
int prefs::mouse_scroll_threshold() { return 10; }
bool prefs::confirm_load_save_from_different_version() { return true; }
bool prefs::use_twelve_hour_clock_format() { return false; }
sort_order::type prefs::addon_manager_saved_order_direction() { return sort_order::type::none; }
void prefs::set_addon_manager_saved_order_direction(sort_order::type) {}
bool prefs::get_show_deprecation(bool def) { return def; }
bool prefs::get_scroll_when_mouse_outside(bool def) { return def; }
bool prefs::auto_open_whisper_windows() { return false; }
bool prefs::show_combat() { return true; }

std::string prefs::get_chat_timestamp(const std::chrono::system_clock::time_point&) {
    return "";
}

compression::format prefs::save_compression_format() {
    return compression::format::none; // N64 saves uncompressed (zlib handled by n64_save)
}

std::vector<std::string>* prefs::get_history(const std::string& id) {
    return &history_map_[id];
}

// -----------------------------------------------------------------------
// Resolution — fixed 320x240 on N64
// -----------------------------------------------------------------------
point prefs::resolution() { return {N64_SCREEN_W, N64_SCREEN_H}; }
void prefs::set_resolution(const point&) {}

int prefs::pixel_scale() { return 1; }
void prefs::set_pixel_scale(const int) {}

bool prefs::turbo() { return false; }
void prefs::set_turbo(bool) {}

int prefs::font_scaling() { return 100; }
void prefs::set_font_scaling(int) {}
int prefs::font_scaled(int size) { return size; }

int prefs::keepalive_timeout() { return 30; }
void prefs::keepalive_timeout(int) {}

// -----------------------------------------------------------------------
// Sound settings
// -----------------------------------------------------------------------
bool prefs::sound() { return true; }
bool prefs::set_sound(bool) { return true; }

unsigned int prefs::sample_rate() { return 22050; }
void prefs::save_sample_rate(const unsigned int) {}

std::size_t prefs::sound_buffer_size() { return 1024; }
void prefs::save_sound_buffer_size(const std::size_t) {}

int prefs::sound_volume() { return 200; }
void prefs::set_sound_volume(int) {}

int prefs::bell_volume() { return 128; }
void prefs::set_bell_volume(int) {}

int prefs::ui_volume() { return 128; }
void prefs::set_ui_volume(int) {}

bool prefs::music_on() { return true; }
bool prefs::set_music(bool) { return true; }

int prefs::music_volume() { return 180; }
void prefs::set_music_volume(int) {}

bool prefs::turn_bell() { return true; }
bool prefs::set_turn_bell(bool) { return true; }

bool prefs::ui_sound_on() { return true; }
bool prefs::set_ui_sound(bool) { return true; }

// -----------------------------------------------------------------------
// Aliases
// -----------------------------------------------------------------------
void prefs::add_alias(const std::string&, const std::string&) {}
optional_const_config prefs::get_alias() { return optional_const_config(); }

// -----------------------------------------------------------------------
// Team colors
// -----------------------------------------------------------------------
std::string prefs::allied_color() { return "green"; }
void prefs::set_allied_color(const std::string&) {}
std::string prefs::enemy_color() { return "red"; }
void prefs::set_enemy_color(const std::string&) {}
std::string prefs::unmoved_color() { return "yellow"; }
void prefs::set_unmoved_color(const std::string&) {}
std::string prefs::partial_color() { return "yellow"; }
void prefs::set_partial_color(const std::string&) {}
std::string prefs::moved_color() { return "gray"; }
void prefs::set_moved_color(const std::string&) {}
std::string prefs::reach_map_color() { return "blue"; }
void prefs::set_reach_map_color(const std::string&) {}
std::string prefs::reach_map_enemy_color() { return "red"; }
void prefs::set_reach_map_enemy_color(const std::string&) {}
int prefs::reach_map_border_opacity() { return 128; }
void prefs::set_reach_map_border_opacity(const int) {}
int prefs::reach_map_tint_opacity() { return 64; }
void prefs::set_reach_map_tint_opacity(const int) {}

int prefs::scroll_speed() { return 30; }
void prefs::set_scroll_speed(const int) {}

// -----------------------------------------------------------------------
// Editor / bookmarks — not used on N64
// -----------------------------------------------------------------------
void prefs::set_dir_bookmarks(const config&) {}
optional_const_config prefs::dir_bookmarks() { return optional_const_config(); }
std::size_t prefs::editor_mru_limit() { return 0; }
std::vector<std::string> prefs::do_read_editor_mru() { return {}; }
void prefs::do_commit_editor_mru(const std::vector<std::string>&) {}
std::vector<std::string> prefs::recent_files() { return {}; }
void prefs::add_recent_files_entry(const std::string&) {}

bool prefs::use_color_cursors() { return false; }
void prefs::set_color_cursors(bool) {}

bool prefs::show_standing_animations() { return true; }
void prefs::set_show_standing_animations(bool) {}

void prefs::show_wesnothd_server_search() {}
bool prefs::show_theme_dialog() { return false; }

void prefs::set_theme(const std::string&) {}
std::string prefs::theme() { return ""; }

void prefs::set_mp_server_program_name(const std::string&) {}
std::string prefs::get_mp_server_program_name() { return ""; }

// -----------------------------------------------------------------------
// Lobby / MP settings
// -----------------------------------------------------------------------
bool prefs::parse_should_show_lobby_join(const std::string&, const std::string&) { return false; }
pref_constants::lobby_joins prefs::get_lobby_joins() { return pref_constants::lobby_joins::show_none; }
void prefs::set_lobby_joins(pref_constants::lobby_joins) {}

const std::map<std::string, preferences::acquaintance>& prefs::get_acquaintances() {
    return acquaintances_;
}
const std::string prefs::get_ignored_delim() { return ""; }
std::map<std::string, std::string> prefs::get_acquaintances_nice(const std::string&) { return {}; }
std::pair<preferences::acquaintance*, bool> prefs::add_acquaintance(
    const std::string&, const std::string&, const std::string&) {
    return {nullptr, false};
}
void prefs::add_completed_campaign(const std::string&, const std::string&) {}
bool prefs::remove_acquaintance(const std::string&) { return false; }
bool prefs::is_friend(const std::string&) { return false; }
bool prefs::is_ignored(const std::string&) { return false; }
bool prefs::is_campaign_completed(const std::string&) { return false; }
bool prefs::is_campaign_completed(const std::string&, const std::string&) { return false; }

void prefs::add_game_preset(config&&) {}
void prefs::remove_game_preset(int) {}
config::child_itors prefs::get_game_presets() {
    static config empty;
    return empty.child_range("game_preset");
}
optional_const_config prefs::get_game_preset(int) { return optional_const_config(); }

const std::vector<game_config::server_info>& prefs::builtin_servers_list() {
    static std::vector<game_config::server_info> list;
    return list;
}
std::vector<game_config::server_info> prefs::user_servers_list() { return {}; }
void prefs::set_user_servers_list(const std::vector<game_config::server_info>&) {}

std::string prefs::network_host() { return ""; }
void prefs::set_network_host(const std::string&) {}
std::string prefs::campaign_server() { return ""; }
void prefs::set_campaign_server(const std::string&) {}

const config& prefs::options() {
    static config opts;
    return opts;
}
void prefs::set_options(const config&) {}

// -----------------------------------------------------------------------
// Countdown / timer settings
// -----------------------------------------------------------------------
std::chrono::seconds prefs::countdown_init_time() { return std::chrono::seconds(270); }
void prefs::set_countdown_init_time(const std::chrono::seconds&) {}
void prefs::clear_countdown_init_time() {}

std::chrono::seconds prefs::countdown_turn_bonus() { return std::chrono::seconds(35); }
void prefs::set_countdown_turn_bonus(const std::chrono::seconds&) {}
void prefs::clear_countdown_turn_bonus() {}

std::chrono::seconds prefs::countdown_reservoir_time() { return std::chrono::seconds(330); }
void prefs::set_countdown_reservoir_time(const std::chrono::seconds&) {}
void prefs::clear_countdown_reservoir_time() {}

std::chrono::seconds prefs::countdown_action_bonus() { return std::chrono::seconds(13); }
void prefs::set_countdown_action_bonus(const std::chrono::seconds&) {}
void prefs::clear_countdown_action_bonus() {}

std::chrono::minutes prefs::chat_message_aging() { return std::chrono::minutes(20); }
void prefs::set_chat_message_aging(const std::chrono::minutes&) {}

// -----------------------------------------------------------------------
// Game settings
// -----------------------------------------------------------------------
int prefs::village_gold() { return 2; }
void prefs::set_village_gold(int) {}
int prefs::village_support() { return 1; }
void prefs::set_village_support(int) {}
int prefs::xp_modifier() { return 100; }
void prefs::set_xp_modifier(int) {}

bool prefs::remember_password() { return false; }
void prefs::set_remember_password(bool) {}

std::string prefs::login() { return "N64Player"; }
void prefs::set_login(const std::string&) {}
std::string prefs::password(const std::string&, const std::string&) { return ""; }
void prefs::set_password(const std::string&, const std::string&, const std::string&) {}

const std::vector<std::string>& prefs::modifications(bool) {
    static std::vector<std::string> mods;
    return mods;
}
void prefs::set_modifications(const std::vector<std::string>&, bool) {}

std::set<std::string>& prefs::encountered_units() { return encountered_units_set_; }
std::set<t_translation::terrain_code>& prefs::encountered_terrains() { return encountered_terrains_set_; }

bool prefs::yellow_confirm() { return false; }
bool prefs::green_confirm() { return false; }
bool prefs::confirm_no_moves() { return true; }

void prefs::encounter_recruitable_units(const std::vector<team>&) {}
void prefs::encounter_start_units(const unit_map&) {}
void prefs::encounter_recallable_units(const std::vector<team>&) {}
void prefs::encounter_map_terrain(const gamemap&) {}
void prefs::encounter_map_terrain(const terrain_type&) {}
void prefs::encounter_all_content(const game_board&) {}

// -----------------------------------------------------------------------
// Achievements
// -----------------------------------------------------------------------
bool prefs::achievement(const std::string&, const std::string&) { return false; }
void prefs::set_achievement(const std::string&, const std::string&) {}
int prefs::progress_achievement(const std::string&, const std::string&, int, int, int) { return 0; }
bool prefs::sub_achievement(const std::string&, const std::string&, const std::string&) { return false; }
void prefs::set_sub_achievement(const std::string&, const std::string&, const std::string&) {}

void prefs::clear_mp_alert_prefs() {}

#endif // N64_BUILD
