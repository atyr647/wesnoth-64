/**
 * n64_gui2_dialog_stubs.cpp
 *
 * Link-time stubs for GUI2 dialog static methods called by N64-compiled game code.
 *
 * The GUI2 .cpp files aren't compiled for N64. Game code calls these dialog
 * methods at specific points; we provide N64-compatible stubs that either
 * do nothing or route through n64_gui for user interaction.
 *
 * Each stub matches the exact signature from the GUI2 dialog headers.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_gui.hpp"

#include "../config.hpp"
#include "../units/unit.hpp"
#include "../units/types.hpp"
#include "../team.hpp"

// Include dialog headers for type info (declarations only, no .cpp compiled)
#include "../gui/dialogs/loading_screen.hpp"
#include "../gui/dialogs/message.hpp"
#include "../gui/dialogs/transient_message.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>

// =====================================================================
// draw_manager — functions called by display.cpp
// These are implemented in n64_draw_manager.cpp; adding any additional
// missing overloads here.
// =====================================================================

// =====================================================================
// GUI2 dialog stubs — static methods called from game code
// =====================================================================

namespace gui2 { namespace dialogs {

// -----------------------------------------------------------------------
// screenshot_notification::display
// -----------------------------------------------------------------------
// Declared in gui/dialogs/screenshot_notification.hpp
// Called from hotkey/command_executor.cpp
struct screenshot_notification {
    static void display(const std::string&, surface) {
        n64_message_box("Screenshot", "Screenshots not supported on N64.");
    }
};

// -----------------------------------------------------------------------
// achievements_dialog
// -----------------------------------------------------------------------
// Called from hotkey/command_executor.cpp
struct achievements_dialog {
    static void display() {
        n64_message_box("Achievements", "Achievements viewer not\navailable on N64.");
    }
};

// -----------------------------------------------------------------------
// lua_interpreter::display
// -----------------------------------------------------------------------
// Called from hotkey/command_executor.cpp, scripting/lua_gui2.cpp
struct lua_interpreter {
    enum WHICH_KERNEL { APP, GAME };
    static void display(WHICH_KERNEL) {
        n64_message_box("Lua", "Lua interpreter not available\non N64 console.");
    }
};

// -----------------------------------------------------------------------
// story_viewer::display
// -----------------------------------------------------------------------
// Called from playsingle_controller.cpp, scripting/lua_gui2.cpp
struct story_viewer {
    static void display(const std::string& scenario_name, const config& story) {
        // Show story text using N64 story viewer
        std::string text;
        for (const config& part : story.child_range("part")) {
            std::string msg = part["story"].str();
            if (!msg.empty()) {
                if (!text.empty()) text += "\n\n";
                text += msg;
            }
        }
        if (text.empty()) text = "(No story text)";
        n64_story_show(scenario_name.c_str(), text.c_str());
    }
};

// -----------------------------------------------------------------------
// help_browser::display
// -----------------------------------------------------------------------
// Called from help/help.cpp
struct help_browser {
    static void display(const void*, const std::string& show_topic) {
        n64_message_box("Help", ("Topic: " + show_topic + "\n\nHelp browser not available\non N64.").c_str());
    }
};

// -----------------------------------------------------------------------
// drop_down_menu
// -----------------------------------------------------------------------
// Called from hotkey/command_executor.cpp, scripting/lua_gui2.cpp
struct drop_down_menu {
    struct entry { std::string label; };
    static int execute(const std::vector<entry>& items, int x, int y) {
        std::vector<N64MenuItem> menu_items;
        for (size_t i = 0; i < items.size(); ++i) {
            menu_items.emplace_back(items[i].label.c_str(), (int)i);
        }
        return n64_menu_show("Select", menu_items, 0, true);
    }
};

// -----------------------------------------------------------------------
// unit_advance — unit level-up dialog
// -----------------------------------------------------------------------
// Called from actions/advancement.cpp
struct unit_advance {
    static int execute(const std::vector<unit_const_ptr>& options, size_t) {
        if (options.empty()) return 0;
        if (options.size() == 1) return 0;

        std::vector<N64MenuItem> items;
        for (size_t i = 0; i < options.size(); ++i) {
            std::string label = options[i]->type_name().str();
            items.emplace_back(label.c_str(), (int)i);
        }
        int sel = n64_menu_show("Level Up!", items, 0, false);
        return sel >= 0 ? sel : 0;
    }
};

// -----------------------------------------------------------------------
// gamestate_inspector
// -----------------------------------------------------------------------
struct gamestate_inspector {
    static void display(const config&, const void*, const void*, const std::string&) {
        n64_message_box("Debug", "Gamestate inspector not\navailable on N64.");
    }
};

// -----------------------------------------------------------------------
// units_dialog — recruit/recall dialogs
// -----------------------------------------------------------------------
struct units_dialog {
    int selection = -1;

    static std::unique_ptr<units_dialog> build_recruit_dialog(
            const std::vector<const unit_type*>& types, void*, const team&) {
        auto dlg = std::make_unique<units_dialog>();
        if (types.empty()) return dlg;

        std::vector<N64MenuItem> items;
        for (size_t i = 0; i < types.size(); ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s (Cost: %d)",
                          types[i]->type_name().str().c_str(),
                          types[i]->cost());
            items.emplace_back(buf, (int)i);
        }
        dlg->selection = n64_menu_show("Recruit", items, 0, true);
        return dlg;
    }

    static std::unique_ptr<units_dialog> build_recall_dialog(
            std::vector<unit_const_ptr>& units, const team&) {
        auto dlg = std::make_unique<units_dialog>();
        if (units.empty()) return dlg;

        std::vector<N64MenuItem> items;
        for (size_t i = 0; i < units.size(); ++i) {
            std::string label = units[i]->name().str() + " (" +
                               units[i]->type_name().str() + " L" +
                               std::to_string(units[i]->level()) + ")";
            items.emplace_back(label.c_str(), (int)i);
        }
        dlg->selection = n64_menu_show("Recall", items, 0, true);
        return dlg;
    }

    bool show(unsigned = 0) { return selection >= 0; }
    int get_selected_index() const { return selection; }
};

// -----------------------------------------------------------------------
// game_load::execute
// -----------------------------------------------------------------------
// Called from savegame.cpp
struct game_load {
    static bool execute(void* /* savegame::load_game_metadata& */) {
        // On N64, game loading is handled by n64_game_launcher
        n64_message_box("Load", "Use the title screen to load games.");
        return false;
    }
};

// -----------------------------------------------------------------------
// game_save
// -----------------------------------------------------------------------
struct game_save {
    static bool execute(std::string& name, const std::string&) {
        // On N64, saving is handled by n64_game_launcher/n64_save
        return true; // auto-save succeeds
    }
};

// -----------------------------------------------------------------------
// wml_error
// -----------------------------------------------------------------------
struct wml_error {
    static void display(const std::string& summary, const std::string& post,
                         const std::string& details, bool) {
        std::string msg = summary;
        if (!post.empty()) msg += "\n" + post;
        n64_message_box("WML Error", msg.c_str());
    }
};

// -----------------------------------------------------------------------
// wml_message — scripted message dialogs
// -----------------------------------------------------------------------
struct wml_message_portrait {};
struct wml_message_options {};
struct wml_message_input {};

// -----------------------------------------------------------------------
// sp_options_configure — singleplayer options
// -----------------------------------------------------------------------
struct sp_options_configure {
    static bool execute(void*) { return true; }
};

// -----------------------------------------------------------------------
// outro — end-of-campaign
// -----------------------------------------------------------------------
struct outro {
    static void display(const void*) {
        n64_message_box("The End", "Campaign complete!\nThank you for playing!");
    }
};

// -----------------------------------------------------------------------
// campaign_difficulty / campaign_selection
// -----------------------------------------------------------------------
struct campaign_difficulty {
    static std::string execute(const config& cfg) {
        // Return first difficulty
        for (const config& d : cfg.child_range("difficulty")) {
            return d["define"].str();
        }
        return "";
    }
};

}} // namespace gui2::dialogs

// =====================================================================
// gui2 free functions that game code calls
// =====================================================================
namespace gui2 {

// show_wml_message — used by scripting/lua_gui2.cpp for [message] WML tag
int show_wml_message(const std::string& title, const std::string& message,
                      const void*, const void*,
                      const void*, const void*) {
    n64_message_box(title.empty() ? "Message" : title.c_str(),
                    message.c_str(), N64_MB_OK);
    return 0;
}

// add_single_widget_definition — used by scripting/lua_gui2.cpp
bool add_single_widget_definition(const std::string&, const std::string&, const config&) {
    return true; // pretend it worked
}

} // namespace gui2

#endif // N64_BUILD
