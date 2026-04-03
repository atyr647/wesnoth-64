#pragma once

/**
 * n64_gui.hpp
 *
 * Minimal N64 GUI system replacing Wesnoth's 93-widget GUI2 framework.
 *
 * On N64 with 320x240 resolution and a controller, we replace the full
 * widget tree with a simple cursor-driven menu system.
 *
 * Components:
 *   N64Menu          — vertical list of selectable items (A=select, B=back)
 *   N64MessageBox    — text display with OK/Cancel buttons
 *   N64UnitList      — scrollable unit list for recruit/recall
 *   N64AttackDialog  — weapon selection for combat
 *   N64NumberPicker  — number selection (e.g. gold slider)
 *   N64TextInput     — on-screen keyboard for save names / chat
 *
 * All dialogs block the game loop until dismissed (modal).
 * They handle their own input polling and rendering.
 */

#include "n64_platform.hpp"
#include "n64_video.hpp"
#include "n64_input.hpp"
#include "n64_audio.hpp"

#include <string>
#include <vector>
#include <functional>

// -----------------------------------------------------------------------
// Menu item
// -----------------------------------------------------------------------
struct N64MenuItem {
    std::string label;
    std::string description; // shown below the menu
    bool        enabled;
    int         value;       // return value when selected

    N64MenuItem() : enabled(true), value(0) {}
    N64MenuItem(const std::string& l, int v = 0, bool e = true)
        : label(l), enabled(e), value(v) {}
    N64MenuItem(const std::string& l, const std::string& d, int v = 0, bool e = true)
        : label(l), description(d), enabled(e), value(v) {}
};

// -----------------------------------------------------------------------
// Menu result
// -----------------------------------------------------------------------
static constexpr int N64_MENU_CANCEL = -1;
static constexpr int N64_MENU_BACK   = -2;

// -----------------------------------------------------------------------
// N64Menu — modal vertical menu
//
// Returns the selected item's value, or N64_MENU_CANCEL/BACK on B.
// -----------------------------------------------------------------------
int n64_menu_show(const std::string& title,
                  const std::vector<N64MenuItem>& items,
                  int initial_selection = 0,
                  bool allow_cancel = true);

// -----------------------------------------------------------------------
// N64MessageBox — modal message with button(s)
//
// Returns 0 for OK/Yes, 1 for Cancel/No
// -----------------------------------------------------------------------
enum class N64MsgButtons { OK, OK_CANCEL, YES_NO };

int n64_message_box(const std::string& title,
                    const std::string& message,
                    N64MsgButtons buttons = N64MsgButtons::OK);

// -----------------------------------------------------------------------
// N64 Attack Dialog — choose weapon for combat
//
// Shows attacker/defender stats side by side, weapon list.
// Returns selected weapon index, or -1 on cancel.
// -----------------------------------------------------------------------
struct N64AttackChoice {
    std::string weapon_name;
    std::string damage_str;   // e.g. "8-3 blade"
    std::string range;        // "melee" or "ranged"
    int         weapon_idx;
};

int n64_attack_dialog(const std::string& attacker_name, int attacker_hp, int attacker_max_hp,
                      const std::string& defender_name, int defender_hp, int defender_max_hp,
                      const std::vector<N64AttackChoice>& weapons);

// -----------------------------------------------------------------------
// N64 Unit recruit/recall list
//
// Shows unit types with cost, returns selected index or -1.
// -----------------------------------------------------------------------
struct N64UnitEntry {
    std::string name;
    std::string type;
    int         cost;
    int         level;
    int         hp;
    int         mp;
};

int n64_unit_list(const std::string& title,
                  const std::vector<N64UnitEntry>& units,
                  int gold_available);

// -----------------------------------------------------------------------
// N64 Loading screen — shows progress text
// -----------------------------------------------------------------------
void n64_loading_screen_show(const std::string& stage_text);
void n64_loading_screen_hide();

// -----------------------------------------------------------------------
// N64 Story screen — display campaign story text
//
// Shows text and optional image. A advances, B skips.
// -----------------------------------------------------------------------
struct N64StoryPart {
    std::string text;
    std::string background_image; // ROM path or empty
    std::string title;
};

void n64_story_show(const std::vector<N64StoryPart>& parts);

// -----------------------------------------------------------------------
// N64 Game Menu (Start button) — save/load/quit/options
// Returns action enum.
// -----------------------------------------------------------------------
enum class N64GameMenuAction {
    RESUME, SAVE, LOAD, OPTIONS, QUIT, SURRENDER
};

N64GameMenuAction n64_game_menu();

// -----------------------------------------------------------------------
// N64 Title Screen
// Returns action enum.
// -----------------------------------------------------------------------
enum class N64TitleAction {
    CAMPAIGN, MULTIPLAYER, LOAD_GAME, TUTORIAL, QUIT
};

N64TitleAction n64_title_screen();

// -----------------------------------------------------------------------
// N64 Campaign Selection
// Returns campaign index or -1 on cancel.
// -----------------------------------------------------------------------
struct N64CampaignEntry {
    std::string name;
    std::string description;
    int         difficulty_count;
};

struct N64CampaignChoice {
    int campaign_idx;
    int difficulty;
};

N64CampaignChoice n64_campaign_select(const std::vector<N64CampaignEntry>& campaigns);
