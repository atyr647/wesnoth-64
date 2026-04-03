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

#include "../config.hpp"
#include "../color.hpp"
#include "../sdl/surface.hpp"
#include "../sound_music_track.hpp"
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

void floating_label::set_font_size(int) {}
void floating_label::set_color(const color_t&) {}
void floating_label::set_bg_color(const color_t&) {}
void floating_label::set_border_size(int) {}
void floating_label::set_position(double, double) {}
void floating_label::set_move(double, double) {}
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
manager::~manager() {}
handle manager::add(int, int, const std::string&, const map_location&,
                     ORIENTATION, bool) {
    return handle();
}
void manager::set_location(const handle&, int, int) {}
void manager::remove(const handle&) {}
void manager::update() {}
void manager::render(const rect&) {}

halo_record::halo_record() {}
halo_record::halo_record(const halo_record&) = default;
halo_record::~halo_record() {}
bool halo_record::valid() const { return false; }

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

// =====================================================================
// Whiteboard — disabled on N64
// =====================================================================
namespace wb {

class manager {
public:
    manager() {}
    ~manager() {}
    bool active() const { return false; }
    void set_active(bool) {}
};

} // namespace wb

// =====================================================================
// loading_screen redirect → N64 loading screen
// =====================================================================
// GUI2 dialog stubs: moved to n64_gui2_stubs.cpp

// =====================================================================
// Help system — simplified text-only unit/terrain info
// =====================================================================
#include "../help/help.hpp"

namespace help {

std::shared_ptr<help_manager> help_manager::get_instance() {
    static auto inst = std::make_shared<help_manager>();
    return inst;
}

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
    std::string msg = tt.name() + "\n"
        "Defense: " + std::to_string(tt.default_base_defense()) + "%";
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

// music_thinker
music_thinker::music_thinker() {}
music_thinker::~music_thinker() {}
void music_thinker::process(events::pump_info&) { n64_audio_update(); }

// music_muter
music_muter::music_muter() : sdl_handler(false) {}
void music_muter::handle_event(const SDL_Event&) {}
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
// =====================================================================
#include "../whiteboard/manager.hpp"

namespace wb {

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

#endif // N64_BUILD
