/**
 * n64_gui.cpp
 *
 * N64 GUI system implementation.
 *
 * All dialogs are modal: they run their own input/render loop until
 * the user makes a selection. This matches Wesnoth's modal_dialog pattern.
 */

#ifdef N64_BUILD

#include "n64_gui.hpp"
#include "n64_platform.hpp"
#include <cstdio>
#include <algorithm>
#include <cstring>

// -----------------------------------------------------------------------
// Directional navigation for the menus: the D-pad and the analog stick both
// work, with auto-repeat on a held D-pad. Returns -1 (up/left), 0, +1 (down/right).
// The input module reports stick-up as cursor_dy > 0, so it is flipped here to
// screen orientation. (The D-pad is mapped to game actions -- Up=quicksave,
// Down=end turn, Left=undo, Right=unit info -- so those are read directly.)
// -----------------------------------------------------------------------
static int nav_axis(bool neg_pressed, bool neg_held, bool pos_pressed, bool pos_held,
                    int stick_screen, int& hold) {
    if (neg_pressed) { hold = 0; return -1; }
    if (pos_pressed) { hold = 0; return  1; }
    if (stick_screen) return stick_screen;
    if (neg_held || pos_held) {
        ++hold;
        if (hold > 14 && (hold % 4) == 0) return neg_held ? -1 : 1;
    } else {
        hold = 0;
    }
    return 0;
}
static int nav_dy(const N64InputState& in) {
    static int hold = 0;
    return nav_axis(in.just_pressed[N64_ACTION_QUICKSAVE], in.held[N64_ACTION_QUICKSAVE],
                    in.just_pressed[N64_ACTION_END_TURN],  in.held[N64_ACTION_END_TURN],
                    -(int)in.cursor_dy, hold);
}
static int nav_dx(const N64InputState& in) {
    static int hold = 0;
    return nav_axis(in.just_pressed[N64_ACTION_UNDO],      in.held[N64_ACTION_UNDO],
                    in.just_pressed[N64_ACTION_UNIT_INFO], in.held[N64_ACTION_UNIT_INFO],
                    (int)in.cursor_dx, hold);
}


// -----------------------------------------------------------------------
// Shared rendering helpers
// -----------------------------------------------------------------------
static constexpr s16 MENU_X      = 20;
static constexpr s16 MENU_Y      = 30;
static constexpr s16 ITEM_H      = 12;
static constexpr s16 MAX_VISIBLE = 16;

static void draw_frame() {
    n64_video_clear(N64Color::from_rgb(16, 16, 32));
    for (int l = 0; l < LAYER_COUNT; ++l)
        n64_flush_layer(static_cast<N64Layer>(l));
    n64_video_flip();
}

static void draw_title_bar(const std::string& title) {
    n64_draw_rect(LAYER_UI, {0, 0, N64_SCREEN_W, 14},
                  N64Color::from_rgb(40, 40, 80));
    n64_draw_text(4, 3, title.c_str(), N64Color::white());
    n64_draw_hline(LAYER_UI, 0, N64_SCREEN_W, 14,
                   N64Color::from_rgb(100, 100, 160));
}

static void draw_button_hints(const char* a_label, const char* b_label) {
    s16 y = N64_SCREEN_H - 10;
    n64_draw_rect(LAYER_UI, {0, y, N64_SCREEN_W, 10},
                  N64Color::from_rgb(40, 40, 80));
    if (a_label) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "A:%s", a_label);
        n64_draw_text(4, y + 1, buf, N64Color::from_rgb(0, 255, 0));
    }
    if (b_label) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "B:%s", b_label);
        n64_draw_text(N64_SCREEN_W - 80, y + 1, buf, N64Color::from_rgb(255, 100, 100));
    }
}

// -----------------------------------------------------------------------
// Word-wrap text to fit a given pixel width
// -----------------------------------------------------------------------
static std::vector<std::string> word_wrap(const std::string& text, int max_w) {
    std::vector<std::string> lines;
    int chars_per_line = max_w / 8; // 8px per glyph
    if (chars_per_line < 1) chars_per_line = 1;

    size_t pos = 0;
    while (pos < text.size()) {
        // Handle explicit newlines
        size_t nl = text.find('\n', pos);
        size_t end = (nl != std::string::npos && nl - pos <= (size_t)chars_per_line)
                     ? nl : pos + chars_per_line;
        if (end > text.size()) end = text.size();

        // Try to break at a space
        if (end < text.size() && end - pos == (size_t)chars_per_line) {
            size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > pos) end = space;
        }

        lines.push_back(text.substr(pos, end - pos));
        pos = end;
        if (pos < text.size() && (text[pos] == ' ' || text[pos] == '\n')) ++pos;
    }
    return lines;
}

// -----------------------------------------------------------------------
// n64_menu_show
// -----------------------------------------------------------------------
int n64_menu_show(const std::string& title,
                  const std::vector<N64MenuItem>& items,
                  int initial_selection,
                  bool allow_cancel) {
    if (items.empty()) return N64_MENU_CANCEL;

    int sel = initial_selection;
    int scroll_offset = 0;

    while (true) {
        n64_input_poll();
        n64_audio_update();
        n64_video_clear(N64Color::from_rgb(16, 16, 32));

        draw_title_bar(title);
        draw_button_hints("Select", allow_cancel ? "Back" : nullptr);

        // Draw items
        int visible = std::min((int)items.size(), (int)MAX_VISIBLE);
        if (sel < scroll_offset) scroll_offset = sel;
        if (sel >= scroll_offset + visible) scroll_offset = sel - visible + 1;

        for (int i = 0; i < visible; ++i) {
            int idx = scroll_offset + i;
            if (idx >= (int)items.size()) break;
            const N64MenuItem& item = items[idx];

            s16 y = MENU_Y + i * ITEM_H;
            bool is_sel = (idx == sel);

            if (is_sel) {
                n64_draw_rect(LAYER_UI, {MENU_X - 2, y, (s16)(N64_SCREEN_W - MENU_X * 2 + 4), ITEM_H},
                              N64Color::from_rgb(60, 60, 120));
            }

            N64Color col = item.enabled
                ? (is_sel ? N64Color::from_rgb(255, 255, 100) : N64Color::white())
                : N64Color::from_rgb(80, 80, 80);

            // Cursor arrow
            if (is_sel) {
                n64_draw_text(MENU_X - 10, y + 2, ">", N64Color::from_rgb(255, 255, 0));
            }

            n64_draw_text(MENU_X, y + 2, item.label.c_str(), col);
        }

        // Scroll indicators
        if (scroll_offset > 0)
            n64_draw_text(N64_SCREEN_W / 2 - 4, MENU_Y - 10, "^",
                          N64Color::from_rgb(180, 180, 180));
        if (scroll_offset + visible < (int)items.size())
            n64_draw_text(N64_SCREEN_W / 2 - 4, MENU_Y + visible * ITEM_H + 2, "v",
                          N64Color::from_rgb(180, 180, 180));

        // Description area
        if (sel >= 0 && sel < (int)items.size() && !items[sel].description.empty()) {
            s16 desc_y = MENU_Y + visible * ITEM_H + 14;
            n64_draw_hline(LAYER_UI, MENU_X, N64_SCREEN_W - MENU_X, desc_y - 3,
                           N64Color::from_rgb(80, 80, 120));
            auto lines = word_wrap(items[sel].description, N64_SCREEN_W - MENU_X * 2);
            for (size_t l = 0; l < lines.size() && l < 4; ++l) {
                n64_draw_text(MENU_X, desc_y + (s16)(l * 10), lines[l].c_str(),
                              N64Color::from_rgb(180, 180, 200));
            }
        }

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();

        // Input
        const N64InputState& inp = n64_input_get(0);
        {
            int d = nav_dy(inp);
            if (d < 0 && sel > 0) --sel;
            if (d > 0 && sel < (int)items.size() - 1) ++sel;
        }

        if (inp.just_pressed[N64_ACTION_SELECT]) {
            if (items[sel].enabled) return items[sel].value;
        }
        if (allow_cancel && inp.just_pressed[N64_ACTION_CANCEL]) {
            return N64_MENU_CANCEL;
        }
    }
}

// -----------------------------------------------------------------------
// n64_message_box
// -----------------------------------------------------------------------
int n64_message_box(const std::string& title,
                    const std::string& message,
                    N64MsgButtons buttons) {
    auto lines = word_wrap(message, N64_SCREEN_W - 40);
    int btn_sel = 0;
    int btn_count = (buttons == N64MsgButtons::OK) ? 1 : 2;

    const char* btn_labels[2][2] = {
        {"OK", nullptr},
        {buttons == N64MsgButtons::YES_NO ? "Yes" : "OK",
         buttons == N64MsgButtons::YES_NO ? "No"  : "Cancel"}
    };
    int btn_idx = (btn_count == 1) ? 0 : 1;

    while (true) {
        n64_input_poll();
        n64_audio_update();
        n64_video_clear(N64Color::from_rgb(16, 16, 32));

        draw_title_bar(title);

        // Message text
        for (size_t i = 0; i < lines.size(); ++i) {
            n64_draw_text(20, (s16)(30 + i * 12), lines[i].c_str(), N64Color::white());
        }

        // Buttons
        s16 btn_y = N64_SCREEN_H - 30;
        for (int b = 0; b < btn_count; ++b) {
            s16 btn_x = 60 + b * 100;
            bool is_sel = (b == btn_sel);
            N64Color bg = is_sel ? N64Color::from_rgb(60, 60, 120) : N64Color::from_rgb(30, 30, 60);
            n64_draw_rect(LAYER_UI, {btn_x, btn_y, 80, 14}, bg);
            n64_draw_rect_outline(LAYER_UI, {btn_x, btn_y, 80, 14},
                                   is_sel ? N64Color::from_rgb(255, 255, 100) : N64Color::from_rgb(80, 80, 120));
            n64_draw_text(btn_x + 8, btn_y + 3, btn_labels[btn_idx][b],
                          is_sel ? N64Color::from_rgb(255, 255, 100) : N64Color::white());
        }

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();

        const N64InputState& inp = n64_input_get(0);
        {
            int d = nav_dx(inp);
            if (d < 0 && btn_sel > 0) --btn_sel;
            if (d > 0 && btn_sel < btn_count - 1) ++btn_sel;
        }
        if (inp.just_pressed[N64_ACTION_SELECT]) return btn_sel;
        if (inp.just_pressed[N64_ACTION_CANCEL] && btn_count > 1) return 1;
    }
}

// -----------------------------------------------------------------------
// n64_attack_dialog
// -----------------------------------------------------------------------
int n64_attack_dialog(const std::string& attacker_name, int attacker_hp, int attacker_max_hp,
                      const std::string& defender_name, int defender_hp, int defender_max_hp,
                      const std::vector<N64AttackChoice>& weapons) {
    if (weapons.empty()) return -1;
    int sel = 0;

    while (true) {
        n64_input_poll();
        n64_audio_update();
        n64_video_clear(N64Color::from_rgb(16, 16, 32));

        draw_title_bar("Attack");
        draw_button_hints("Attack", "Cancel");

        // Attacker info (left)
        n64_draw_text(10, 20, attacker_name.c_str(), N64Color::from_rgb(100, 255, 100));
        char buf[32];
        std::snprintf(buf, sizeof(buf), "HP: %d/%d", attacker_hp, attacker_max_hp);
        n64_draw_text(10, 32, buf, N64Color::white());

        // Defender info (right)
        s16 rx = N64_SCREEN_W / 2 + 10;
        n64_draw_text(rx, 20, defender_name.c_str(), N64Color::from_rgb(255, 100, 100));
        std::snprintf(buf, sizeof(buf), "HP: %d/%d", defender_hp, defender_max_hp);
        n64_draw_text(rx, 32, buf, N64Color::white());

        // Divider
        n64_draw_hline(LAYER_UI, 0, N64_SCREEN_W, 46, N64Color::from_rgb(80, 80, 120));

        // Weapon list
        n64_draw_text(10, 50, "Choose weapon:", N64Color::from_rgb(200, 200, 200));
        for (int i = 0; i < (int)weapons.size(); ++i) {
            s16 y = 64 + i * 20;
            bool is_sel = (i == sel);

            if (is_sel) {
                n64_draw_rect(LAYER_UI, {8, y, (s16)(N64_SCREEN_W - 16), 18},
                              N64Color::from_rgb(60, 60, 120));
                n64_draw_text(10, y, ">", N64Color::from_rgb(255, 255, 0));
            }

            N64Color col = is_sel ? N64Color::from_rgb(255, 255, 100) : N64Color::white();
            n64_draw_text(20, y + 1, weapons[i].weapon_name.c_str(), col);
            n64_draw_text(20, y + 10, weapons[i].damage_str.c_str(),
                          N64Color::from_rgb(180, 180, 180));
            n64_draw_text(140, y + 10, weapons[i].range.c_str(),
                          N64Color::from_rgb(140, 140, 180));
        }

        for (int l = 0; l < LAYER_COUNT; ++l)
            n64_flush_layer(static_cast<N64Layer>(l));
        n64_video_flip();

        const N64InputState& inp = n64_input_get(0);
        {
            int d = nav_dy(inp);
            if (d < 0 && sel > 0) --sel;
            if (d > 0 && sel < (int)weapons.size() - 1) ++sel;
        }
        if (inp.just_pressed[N64_ACTION_SELECT]) return sel;
        if (inp.just_pressed[N64_ACTION_CANCEL]) return -1;
    }
}

// -----------------------------------------------------------------------
// n64_unit_list
// -----------------------------------------------------------------------
int n64_unit_list(const std::string& title,
                  const std::vector<N64UnitEntry>& units,
                  int gold_available) {
    std::vector<N64MenuItem> items;
    for (int i = 0; i < (int)units.size(); ++i) {
        char label[64];
        std::snprintf(label, sizeof(label), "%-14s Lv%d %dg",
                      units[i].name.c_str(), units[i].level, units[i].cost);
        char desc[128];
        std::snprintf(desc, sizeof(desc), "%s  HP:%d  MP:%d  Cost:%d gold",
                      units[i].type.c_str(), units[i].hp, units[i].mp, units[i].cost);
        bool can_afford = (units[i].cost <= gold_available);
        items.emplace_back(label, std::string(desc), i, can_afford);
    }

    char title_buf[64];
    std::snprintf(title_buf, sizeof(title_buf), "%s (Gold: %d)", title.c_str(), gold_available);
    return n64_menu_show(title_buf, items, 0, true);
}

// -----------------------------------------------------------------------
// n64_loading_screen_show / hide
// -----------------------------------------------------------------------
void n64_loading_screen_show(const std::string& stage_text) {
    n64_video_clear(N64Color::black());
    n64_draw_text(N64_SCREEN_W / 2 - 40, N64_SCREEN_H / 2 - 10,
                  "Loading...", N64Color::white());
    n64_draw_text(N64_SCREEN_W / 2 - (s16)(stage_text.size() * 4),
                  N64_SCREEN_H / 2 + 5,
                  stage_text.c_str(), N64Color::from_rgb(180, 180, 200));
    for (int l = 0; l < LAYER_COUNT; ++l)
        n64_flush_layer(static_cast<N64Layer>(l));
    n64_video_flip();
}

void n64_loading_screen_hide() {
    // No-op; next frame will clear
}

// -----------------------------------------------------------------------
// n64_story_show
// -----------------------------------------------------------------------
void n64_story_show(const std::vector<N64StoryPart>& parts) {
    for (const auto& part : parts) {
        auto lines = word_wrap(part.text, N64_SCREEN_W - 40);
        int scroll = 0;
        int max_scroll = std::max(0, (int)lines.size() - 20);

        bool done = false;
        while (!done) {
            n64_input_poll();
            n64_audio_update();
            n64_video_clear(N64Color::black());

            // Title
            if (!part.title.empty()) {
                n64_draw_text(N64_SCREEN_W / 2 - (s16)(part.title.size() * 4), 5,
                              part.title.c_str(), N64Color::from_rgb(255, 220, 100));
                n64_draw_hline(LAYER_UI, 20, N64_SCREEN_W - 20, 16,
                               N64Color::from_rgb(100, 80, 40));
            }

            // Story text
            s16 text_y = 22;
            for (int i = scroll; i < (int)lines.size() && text_y < N64_SCREEN_H - 20; ++i) {
                n64_draw_text(20, text_y, lines[i].c_str(),
                              N64Color::from_rgb(220, 220, 220));
                text_y += 10;
            }

            // Prompt
            n64_draw_text(N64_SCREEN_W / 2 - 40, N64_SCREEN_H - 12,
                          "A:Continue  B:Skip", N64Color::from_rgb(140, 140, 140));

            for (int l = 0; l < LAYER_COUNT; ++l)
                n64_flush_layer(static_cast<N64Layer>(l));
            n64_video_flip();

            const N64InputState& inp = n64_input_get(0);
            int d = nav_dy(inp);
            if (d < 0 && scroll > 0) --scroll;
            if (d > 0 && scroll < max_scroll) ++scroll;
            if (inp.just_pressed[N64_ACTION_SELECT]) done = true;
            if (inp.just_pressed[N64_ACTION_CANCEL]) return; // skip all
        }
    }
}

// -----------------------------------------------------------------------
// n64_game_menu (Start button)
// -----------------------------------------------------------------------
N64GameMenuAction n64_game_menu() {
    std::vector<N64MenuItem> items = {
        {"Resume Game",  0},
        {"Save Game",    1},
        {"Load Game",    2},
        {"Options",      3},
        {"Quit Mission", 4},
    };

    int r = n64_menu_show("Game Menu", items, 0, true);
    switch (r) {
    case 0: return N64GameMenuAction::RESUME;
    case 1: return N64GameMenuAction::SAVE;
    case 2: return N64GameMenuAction::LOAD;
    case 3: return N64GameMenuAction::OPTIONS;
    case 4: return N64GameMenuAction::QUIT;
    default: return N64GameMenuAction::RESUME;
    }
}

// -----------------------------------------------------------------------
// n64_title_screen
// -----------------------------------------------------------------------
N64TitleAction n64_title_screen() {
    std::vector<N64MenuItem> items = {
        {"Campaign",    "Play through a Wesnoth campaign",   0},
        {"Tutorial",    "Learn how to play",                 1},
        {"Multiplayer", "Play online via NetLib USB",        2},
        {"Load Game",   "Continue a saved game",             3},
        {"Quit",        "Exit to N64 menu",                  4},
    };

    // Title logo text
    while (true) {
        int r = n64_menu_show("The Battle for Wesnoth", items, 0, false);
        switch (r) {
        case 0: return N64TitleAction::CAMPAIGN;
        case 1: return N64TitleAction::TUTORIAL;
        case 2: return N64TitleAction::MULTIPLAYER;
        case 3: return N64TitleAction::LOAD_GAME;
        case 4: return N64TitleAction::QUIT;
        }
    }
}

// -----------------------------------------------------------------------
// n64_campaign_select
// -----------------------------------------------------------------------
N64CampaignChoice n64_campaign_select(const std::vector<N64CampaignEntry>& campaigns) {
    N64CampaignChoice result = { -1, 0 };

    // Build campaign menu
    std::vector<N64MenuItem> items;
    for (int i = 0; i < (int)campaigns.size(); ++i) {
        items.emplace_back(campaigns[i].name, campaigns[i].description, i);
    }

    int sel = n64_menu_show("Select Campaign", items, 0, true);
    if (sel == N64_MENU_CANCEL) return result;
    result.campaign_idx = sel;

    // Difficulty selection
    if (campaigns[sel].difficulty_count > 1) {
        std::vector<N64MenuItem> diff_items;
        const char* diff_names[] = {"Easy", "Normal", "Hard", "Nightmare"};
        for (int d = 0; d < campaigns[sel].difficulty_count && d < 4; ++d) {
            diff_items.emplace_back(diff_names[d], d);
        }
        int d = n64_menu_show("Select Difficulty", diff_items, 1, true);
        if (d == N64_MENU_CANCEL) { result.campaign_idx = -1; return result; }
        result.difficulty = d;
    }

    return result;
}

#endif // N64_BUILD
