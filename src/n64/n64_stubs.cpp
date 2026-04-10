/**
 * n64_stubs.cpp
 *
 * Stub implementations for Wesnoth subsystems that cannot run on N64
 * as-is but whose headers are referenced by core game logic.
 *
 * Each stub provides the minimum API surface to link successfully.
 * Where possible, the stub does something useful (e.g. floating_label
 * calls n64_draw_text). Where not (e.g. editor, add-on manager), it's
 * a pure no-op.
 *
 * Systems stubbed:
 *   - floating_label (simplified → n64_draw_text)
 *   - halo (no animated halos, too expensive for N64)
 *   - cursor (controller cursor, no bitmap cursor)
 *   - theme (hardcoded N64 layout)
 *   - reports (simplified sidebar)
 *   - help (text-only unit/terrain info)
 *   - whiteboard (disabled on N64)
 *   - editor (disabled on N64)
 *   - add-on manager (disabled on N64)
 *   - loading_screen (redirect to n64_loading_screen)
 *   - desktop notifications (disabled)
 *   - clipboard (disabled)
 *   - preferences (hardcoded defaults)
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_video.hpp"
#include "n64_gui.hpp"
#include "n64_romfs.hpp"

#include "../config.hpp"
#include "../color.hpp"
#include "../sdl/surface.hpp"
#include "../sound_music_track.hpp"
#include "../units/types.hpp"
#include "../utils/optional_fwd.hpp"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

// =====================================================================
// floating_label.cpp replacement
// =====================================================================
#include "../floating_label.hpp"

namespace font {

static int s_next_label_id = 1;

floating_label::floating_label(const std::string& text)
    : text_(text) {}

// set_font_size, set_color, set_bg_color, set_border_size, set_position, set_move
// are all defined inline in floating_label.hpp — don't redefine

void floating_label::set_lifetime(const std::chrono::milliseconds& lifetime,
                                   const std::chrono::milliseconds& fadeout) {
    lifetime_ = lifetime;
    fadeout_  = fadeout;
}

void floating_label::undraw() {}
void floating_label::move(double, double) {}
void floating_label::update(const clock::time_point&) {}
void floating_label::draw() {
    // Render as simple N64 text
    n64_draw_text_shadowed((s16)xpos_, (s16)ypos_, text_.c_str(),
                            N64Color::white());
}
bool floating_label::create_texture() { return true; }
void floating_label::clear_texture() {}

int  add_floating_label(const floating_label&) { return s_next_label_id++; }
void move_floating_label(int, double, double) {}
void scroll_floating_labels(double, double) {}
void remove_floating_label(int, const std::chrono::milliseconds&) {}
void show_floating_label(int, bool) {}
void draw_floating_labels() {}
void update_floating_labels() {}
rect get_floating_label_rect(int) { return {0,0,0,0}; }

} // namespace font

// =====================================================================
// halo.cpp replacement — no halos on N64
// =====================================================================
#include "../halo.hpp"

namespace halo {

manager::manager() {}
handle manager::add(int, int, const std::string&, const map_location&,
                     ORIENTATION, bool) {
    return handle();
}
void manager::set_location(const handle&, int, int) {}
void manager::remove(const handle&) {}
void manager::update() {}
void manager::render(const rect&) {}

halo_record::halo_record() {}
halo_record::~halo_record() {}

} // namespace halo

// =====================================================================
// cursor.cpp replacement — N64 uses a simple drawn cursor
// =====================================================================
#include "../cursor.hpp"

namespace cursor {

static CURSOR_TYPE s_cursor_type = NORMAL;

manager::manager() {}
manager::~manager() {}

void set(CURSOR_TYPE t) { s_cursor_type = t; }
void set_dragging(bool) {}
CURSOR_TYPE get() { return s_cursor_type; }
void set_focus(bool) {}

setter::setter(CURSOR_TYPE t) : old_(s_cursor_type) { set(t); }
setter::~setter() { set(old_); }

// Draw cursor as a small arrow at the current hex position
void draw_cursor(s16 x, s16 y) {
    N64Color c = N64Color::from_rgb(255, 255, 0);
    // Simple 5x5 arrow
    n64_draw_rect(LAYER_TEXT, {x, y, 2, 5}, c);
    n64_draw_rect(LAYER_TEXT, {(s16)(x+2), (s16)(y+1), 1, 3}, c);
    n64_draw_rect(LAYER_TEXT, {(s16)(x+3), (s16)(y+2), 1, 1}, c);
}

} // namespace cursor

// =====================================================================
// Desktop notifications — no-op on N64
// =====================================================================
namespace desktop {
namespace notifications {
    void send(const std::string&, const std::string&, const std::string&) {}
    bool available() { return false; }
} // notifications
} // desktop

// =====================================================================
// Clipboard — no clipboard on N64
// =====================================================================
namespace desktop {
namespace clipboard {
    void copy_to_clipboard(const std::string&) {}
    std::string copy_from_clipboard() { return ""; }
    bool available() { return false; }
} // clipboard
} // desktop

// Whiteboard class is defined in n64_stubs.cpp below (after #include of manager.hpp)

// =====================================================================
// loading_screen redirect → N64 loading screen
// =====================================================================
// GUI2 dialog stubs: moved to n64_gui2_stubs.cpp

// =====================================================================
// Help system — simplified text-only unit/terrain info
// =====================================================================
#include "../help/help.hpp"

namespace help {

// help_manager has a private constructor; use a custom shared_ptr
std::shared_ptr<help_manager> help_manager::get_instance() {
    auto existing = singleton_.lock();
    if (!existing) {
        // Use a private-accessible raw new since we're a member function
        existing.reset(new help_manager());
        singleton_ = existing;
    }
    return existing;
}

// Define the pimpl type so unique_ptr<implementation> destructor works
class help_manager::implementation {};

help_manager::help_manager() {}
help_manager::~help_manager() {}

void show_help(const std::string& topic) {
    n64_message_box("Help", "Topic: " + topic + "\n\n(Help browser not available on N64)");
}

void show_unit_description(const unit_type& type) {
    std::string msg = type.type_name() + "\n"
        "Level " + std::to_string(type.level()) + "\n"
        "HP: " + std::to_string(type.hitpoints()) + "\n"
        "MP: " + std::to_string(type.movement()) + "\n"
        "Cost: " + std::to_string(type.cost());
    n64_message_box("Unit Info", msg);
}

void show_terrain_description(const terrain_type& tt) {
    std::string msg = "Terrain info";
    n64_message_box("Terrain Info", msg);
}

} // namespace help

// =====================================================================
// Editor — disabled on N64 (no mouse, insufficient UI)
// =====================================================================
namespace editor {
    void start() {
        n64_message_box("Editor", "Map editor is not available on N64.\nUse the PC version.");
    }
} // namespace editor

// =====================================================================
// Add-on manager — disabled on N64 (no network download)
// =====================================================================
namespace addons {
    bool manager_ui() {
        n64_message_box("Add-ons", "Add-on manager is not available on N64.\nContent is built into the ROM.");
        return false;
    }
} // namespace addons

// =====================================================================
// Sound effects convenience wrappers
// Route Wesnoth sound calls → n64_audio
// =====================================================================
#include "../sound.hpp"

namespace sound {

void play_music_config(const config&, bool, int) {}
void play_music_once(const std::string&) {}
void empty_playlist() {}
void stop_music() { n64_music_stop(500); }
void stop_sound() { n64_sfx_stop_all(); }
void stop_bell() { n64_sfx_stop(CHAN_BELL); }
void stop_UI_sound() { n64_sfx_stop(CHAN_UI0); n64_sfx_stop(CHAN_UI1); }

bool is_music_playing() { return n64_music_is_playing(); }

void play_sound(const std::string& file, channel_group, unsigned) {
    const RomFsEntry* entry = n64_romfs_find_wesnoth(file.c_str());
    if (entry) {
        N64SfxHandle sfx = n64_sfx_load(entry->rom_offset, entry->byte_length);
        n64_sfx_play(CHAN_FX0, sfx, 200, 0);
    }
}

void play_bell(const std::string& file) {
    const RomFsEntry* entry = n64_romfs_find_wesnoth(file.c_str());
    if (entry) {
        N64SfxHandle sfx = n64_sfx_load(entry->rom_offset, entry->byte_length);
        n64_sfx_play(CHAN_BELL, sfx, 200, 0);
    }
}

void play_UI_sound(const std::string& file) {
    const RomFsEntry* entry = n64_romfs_find_wesnoth(file.c_str());
    if (entry) {
        N64SfxHandle sfx = n64_sfx_load(entry->rom_offset, entry->byte_length);
        n64_sfx_play(CHAN_UI0, sfx, 180, 0);
    }
}

void play_timer(const std::string&,
    const std::chrono::milliseconds&, const std::chrono::milliseconds&) {}
void stop_timer() {}

void set_music_volume(int vol) { n64_music_set_volume((u8)vol); }
void set_sound_volume(int vol) { n64_sfx_set_volume((u8)vol); }
void set_bell_volume(int) {}
void set_UI_volume(int) {}

void play_music() {}
void play_track(unsigned int) {}
void commit_music_changes() {}
bool is_sound_playing(int) { return false; }
void stop_sound(int) {}
void play_sound_positioned(const std::string&, int, int, unsigned int) {}

void reset_sound() {}
void close_sound() {}
bool init_sound() { return true; }

// music_thinker — ctor/dtor are implicit (inherits pump_monitor)
void music_thinker::process() { n64_audio_update(); }

// music_muter — handle_event is inline in header
music_muter::music_muter() : sdl_handler(false) {}
void music_muter::handle_window_event(const SDL_Event&) {}

} // namespace sound

// Image cache: handled by n64_sdl_wrappers.cpp (image namespace)

// =====================================================================
// Remaining sound:: functions needed by lua_audio.cpp etc.
// (sound_music_track.cpp is compiled for N64 — provides music_track ctors)
// (standard_colors.cpp is compiled for N64 — provides font color constants)
// =====================================================================

namespace sound {

void write_music_play_list(config&) {}
int get_music_volume() { return 180; }
int get_sound_volume() { return 200; }

utils::optional<unsigned int> get_current_track_index() { return utils::nullopt; }
std::shared_ptr<sound::music_track> get_current_track() { return nullptr; }
std::shared_ptr<sound::music_track> get_previous_music_track() { return nullptr; }
void set_previous_track(std::shared_ptr<music_track>) {}
unsigned int get_num_tracks() { return 0; }
void remove_track(unsigned int) {}

std::shared_ptr<sound::music_track> get_track(unsigned int) { return nullptr; }
void set_track(unsigned int, const std::shared_ptr<music_track>&) {}

void flush_cache() {}
void reposition_sound(int, unsigned int) {}

std::string current_driver() { return "n64_audio"; }
std::vector<std::string> enumerate_drivers() { return {"n64_audio"}; }

driver_status driver_status::query() {
    return {true, 22050, 0x8010, 1, 1024};
}

// music_track constructors (sound_music_track.cpp excluded — uses vorbis)
music_track::music_track(const std::string& file_path, const config&)
    : id_(file_path), file_path_(file_path), title_(file_path) {}
music_track::music_track(const std::string& file_path, const std::string& file)
    : id_(file), file_path_(file_path), title_(file) {}

std::shared_ptr<music_track> music_track::create(const config&) { return nullptr; }
std::shared_ptr<music_track> music_track::create(const std::string&) { return nullptr; }
void music_track::write(config&, bool) const {}

} // namespace sound

// =====================================================================
// font::get_text_renderer() — returns singleton pango_text
// =====================================================================
#include "../font/text.hpp"

namespace font {

pango_text& get_text_renderer() {
    static pango_text renderer;
    return renderer;
}

} // namespace font

// =====================================================================
// events::command_disabler — used by mouse_handler_base
// =====================================================================
#include "../mouse_handler_base.hpp"

namespace events {

int commands_disabled = 0;

command_disabler::command_disabler() { ++commands_disabled; }
command_disabler::~command_disabler() { --commands_disabled; }

} // namespace events

// =====================================================================
// video:: additional functions
// =====================================================================
#include "../video.hpp"

namespace video {

surface read_pixels(rect*) { return surface(); }
surface read_pixels_low_res(rect*) { return surface(); }

} // namespace video

// =====================================================================
// whiteboard (wb::) structs — disabled on N64
// Whiteboard uses boost::multi_index which is not available on N64.
// We provide forward declarations and stub implementations here
// without including the real headers.
// =====================================================================

// Forward declare the whiteboard types we need to stub
namespace wb {

class future_map {
    bool initial_planned_unit_map_;
public:
    future_map();
    ~future_map();
};

class future_map_if_active {
    bool initial_planned_unit_map_;
    bool whiteboard_active_;
public:
    future_map_if_active();
    ~future_map_if_active();
};

future_map::future_map() : initial_planned_unit_map_(false) {}
future_map::~future_map() {}

future_map_if_active::future_map_if_active()
    : initial_planned_unit_map_(false), whiteboard_active_(false) {}
future_map_if_active::~future_map_if_active() {}

} // namespace wb

// resources:: global pointers — defined in resources.cpp (already compiled for N64)

// =====================================================================
// Minimap prep function
// =====================================================================
#include "../minimap.hpp"

namespace image {

std::function<rect(rect)> prep_minimap_for_rendering(
    const gamemap&, const team*, const unit_map*,
    const std::map<map_location, unsigned int>*, bool) {
    // Return identity transform — minimap handled by n64_display.cpp
    return [](rect r) { return r; };
}

} // namespace image

// =====================================================================
// Additional font:: stubs
// =====================================================================
namespace font {

int get_max_height(unsigned size, font::family_class, pango_text::FONT_STYLE) {
    // Return approximate line height based on size
    return (int)size + 4;
}

floating_label_context::floating_label_context() {}
floating_label_context::~floating_label_context() {}

rect floating_label::get_bg_rect(const rect& text_rect) const {
    // Return text rect with small padding
    rect r = text_rect;
    r.x -= 2; r.y -= 2;
    r.w += 4; r.h += 4;
    return r;
}

} // namespace font

// =====================================================================
// Additional image:: stubs
// =====================================================================
#include "../picture.hpp"

namespace image {

bool is_in_hex(const locator&) { return false; }
bool is_empty_hex(const locator&) { return true; }
bool precached_file_exists(const std::string&) { return false; }
void precache_file_existence(const std::string&) {}
save_result save_image(const surface&, const std::string&) { return save_result::unsupported_format; }
void n64_display_flush_caches() {}

} // namespace image

// =====================================================================
// Additional video:: stubs
// =====================================================================
namespace video {

std::string current_driver() { return "n64"; }
std::vector<std::pair<std::string, std::string>> renderer_report() {
    return {{"renderer", "N64 libdragon"}};
}
std::vector<std::string> enumerate_drivers() { return {"n64"}; }
int native_refresh_rate() { return 60; }
int current_refresh_rate() { return 60; }
std::pair<float, float> get_dpi() { return {72.0f, 72.0f}; }

} // namespace video

// =====================================================================
// translation:: stubs
// =====================================================================
#include "../gettext.hpp"

// Define static id for boost::locale::info facet
std::locale::id boost::locale::info::id;

namespace translation {

const boost::locale::info& get_effective_locale_info() {
    // Return a reference to a statically-allocated dummy.
    // We allocate on heap because std::locale::facet has protected dtor semantics.
    static const boost::locale::info* dummy = new boost::locale::info();
    return *dummy;
}

bool ci_search(const std::string& a, const std::string& b) {
    return a.find(b) != std::string::npos;
}

bool ci_search(utils::span<const std::string> haystack, const std::string& needle) {
    for (const auto& s : haystack) {
        if (s.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace translation

// =====================================================================
// game_config:: globals
// =====================================================================
// Include filesystem.hpp so that the following definitions match the
// extern declarations (and get external linkage + cxx11 ABI tag).
#include "../filesystem.hpp"
namespace game_config {
std::string path = "";
const std::string observer_team_name = "observer";
int cache_compression_level = 6;
} // namespace game_config

// =====================================================================
// draw:: stubs — most drawing done via n64_video directly
// =====================================================================
#include "../draw.hpp"

namespace draw {

void fill(const ::rect&) {}
void fill(const ::rect&, uint8_t, uint8_t, uint8_t) {}
void fill(const SDL_FRect&, const color_t&) {}
void fill(uint8_t, uint8_t, uint8_t) {}
void rect(const ::rect&) {}
void rect(const ::rect&, uint8_t, uint8_t, uint8_t) {}
void line(int, int, int, int) {}
void circle(int, int, int, uint8_t) {}
void disc(int, int, int, uint8_t) {}
void cairo_circle(int, int, int, const color_t&, int) {}
void cairo_disc(int, int, int, const color_t&) {}
void set_color(const color_t&) {}
void blit(const texture&) {}
void tiled_highres(const texture&, const ::rect&, bool, bool) {}
void flipped(const texture&, const ::rect&, bool, bool) {}

viewport_setter::viewport_setter(const ::rect& v) : v_(v), c_{}, clip_enabled_(false) {}
viewport_setter::~viewport_setter() {}

viewport_setter set_viewport(const ::rect& v) { return viewport_setter(v); }

} // namespace draw

// =====================================================================
// gui::floating_textbox — disabled on N64
// =====================================================================
#include "../floating_textbox.hpp"

namespace gui {

floating_textbox::floating_textbox() {}
void floating_textbox::close() {}
void floating_textbox::show(TEXTBOX_MODE, const std::string&, const std::string&, bool, game_display&) {}
void floating_textbox::tab(const std::set<std::string>&) {}
void floating_textbox::memorize_command(const std::string&) {}

} // namespace gui

// =====================================================================
// Misc single symbols
// =====================================================================
bool ad_hoc_addon_fetch_session(const std::vector<std::string>&) { return false; }

#endif // N64_BUILD
