/**
 * n64_gui2_stubs.cpp
 *
 * N64 stub implementations for Wesnoth's GUI2 dialog system.
 *
 * The PC version uses gui/dialogs/*.cpp with a full widget framework
 * (80+ dialog classes, layout engine, event system). On N64, all dialogs
 * are replaced by the modal n64_gui system (simple menus, message boxes).
 *
 * This file provides:
 *   1. gui2 free functions (show_message, show_transient_message, etc.)
 *   2. loading_screen implementation
 *   3. modal_dialog base class methods
 *
 * The gui2 source files (gui/dialogs/*.cpp, gui/widgets/*.cpp, gui/core/*.cpp)
 * are NOT included in the N64 build. Only the headers are used for type info.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_gui.hpp"
#include "n64_video.hpp"

#include "../gui/dialogs/message.hpp"
#include "../gui/dialogs/transient_message.hpp"
#include "../gui/dialogs/loading_screen.hpp"
#include "../gui/dialogs/modal_dialog.hpp"
#include "../gui/widgets/retval.hpp"

#include "../config.hpp"

#include <string>
#include <functional>
#include <vector>

// =====================================================================
// gui2 free functions — message boxes
// =====================================================================
namespace gui2 {

void show_message(const std::string& title,
                  const std::string& message,
                  const std::string&,
                  const bool,
                  const bool,
                  const bool) {
    n64_message_box(title.c_str(), message.c_str(), N64_MB_OK);
}

int show_message(const std::string& title,
                 const std::string& message,
                 const dialogs::message::button_style style,
                 bool, bool) {
    N64MBType mb = N64_MB_OK;
    if (style == dialogs::message::yes_no_buttons)
        mb = N64_MB_YESNO;
    else if (style == dialogs::message::ok_cancel_buttons)
        mb = N64_MB_OKCANCEL;

    int result = n64_message_box(title.c_str(), message.c_str(), mb);
    return result == 0 ? gui2::retval::OK : gui2::retval::CANCEL;
}

void show_error_message(const std::string& message, bool) {
    n64_message_box("Error", message.c_str(), N64_MB_OK);
}

void show_transient_message(const std::string& title,
                             const std::string& message,
                             const std::string&,
                             const bool,
                             const bool) {
    n64_message_box(title.empty() ? "Info" : title.c_str(),
                    message.c_str(), N64_MB_OK);
}

void show_transient_error_message(const std::string& message, bool) {
    n64_message_box("Error", message.c_str(), N64_MB_OK);
}

// =====================================================================
// Dialog class stubs
//
// modal_dialog inherits from window → panel → ... (deep hierarchy).
// We don't provide constructor/destructor implementations for the base
// classes since we never instantiate dialog objects on N64 — we only
// call the static execute()/display() methods.
// =====================================================================
namespace dialogs {

// =====================================================================
// loading_screen
// =====================================================================
loading_screen* loading_screen::singleton_ = nullptr;

void loading_screen::display(std::function<void()> f) {
    n64_loading_screen_show("Loading...");
    if (f) f();
    n64_loading_screen_hide();
}

bool loading_screen::displaying() {
    return singleton_ != nullptr;
}

void loading_screen::progress(loading_stage stage) {
    const char* names[] = {
        "Building terrain...", "Creating cache...", "Init display...",
        "Init fonts...", "Init teams...", "Init theme...",
        "Loading config...", "Loading data...", "Loading game...",
        "Loading map...", "Loading music...", "Loading units...",
        "Placing units...", "Preparing scenario...", "Running events...",
        "Verifying cache...", "Starting game...",
    };
    int idx = static_cast<int>(stage);
    const char* name = (idx >= 0 && idx < static_cast<int>(sizeof(names)/sizeof(names[0])))
                       ? names[idx] : "Loading...";
    n64_loading_screen_show(name);
}

void loading_screen::spin() {}
void loading_screen::raise() {}

} // namespace dialogs

// gui2 init/theme (from gui/gui.hpp)
void init() {}
void switch_theme(const std::string&) {}

// retval constants: defined as enum in gui/widgets/retval.hpp (included above)

// gui2::event functions — now provided by gui/core/event/handler.cpp (compiled for N64)

} // namespace gui2

// =====================================================================
// video namespace — N64 replacement for src/video.cpp
// (video.cpp is not compiled for N64; n64_video_adapter.cpp handles it,
//  but some inline functions reference free functions we must provide)
// =====================================================================
namespace video {
    int get_pixel_scale() { return 1; }
}

// =====================================================================
// floating_label namespace — stub (implemented in n64_stubs.cpp)
// =====================================================================

// =====================================================================
// draw_manager — N64 replacement for src/draw_manager.cpp
// (draw_manager.cpp is not in the N64 source list; n64_draw_manager.cpp
//  provides the implementation, auto-globbed from src/n64/)
// =====================================================================

#endif // N64_BUILD
