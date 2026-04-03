/**
 * n64_game_launcher.cpp
 *
 * N64 replacement for game_launcher that drives the full game flow:
 *
 *   Title Screen → Campaign Select → Difficulty → Loading → Scenario Play
 *                ↘ Multiplayer (via NetLib)
 *                ↘ Load Game (from SRAM)
 *                ↘ Tutorial
 *
 * This file implements the game_launcher class methods that depend on
 * GUI2 dialogs, replacing them with our N64 minimal GUI.
 *
 * The actual game logic (play_controller, playsingle_controller) runs
 * unchanged — it's pure C++ game logic with no SDL dependencies.
 */

#ifdef N64_BUILD

#include "n64_gui.hpp"
#include "n64_save.hpp"
#include "n64_romfs.hpp"
#include "n64_binary_wml.hpp"
#include "n64_platform.hpp"
#include "n64_video.hpp"
#include "n64_audio.hpp"
#include "netlib_adapter.hpp"

#include "../game_launcher.hpp"
#include "../game_config_manager.hpp"
#include "../game_config.hpp"
#include "../saved_game.hpp"
#include "../savegame.hpp"
#include "../playsingle_controller.hpp"
#include "../game_initialization/playcampaign.hpp"
#include "../serialization/parser.hpp"
#include "../log.hpp"

static lg::log_domain log_n64("n64");
#define LOG_N64 LOG_STREAM(info, log_n64)

// -----------------------------------------------------------------------
// Forward declarations for N64-specific campaign/scenario loading
// -----------------------------------------------------------------------
static bool load_campaign_from_rom(const std::string& campaign_id,
                                    int difficulty,
                                    saved_game& state);
static std::vector<N64CampaignEntry> enumerate_campaigns();

// -----------------------------------------------------------------------
// game_launcher::play_game — the main game loop entry point
//
// This is called repeatedly by n64_main.cpp.
// Returns true to keep running, false to exit.
// -----------------------------------------------------------------------
bool game_launcher::play_game() {
    // Show title screen
    N64TitleAction action = n64_title_screen();

    switch (action) {
    case N64TitleAction::QUIT:
        return false;

    case N64TitleAction::CAMPAIGN: {
        auto campaigns = enumerate_campaigns();
        if (campaigns.empty()) {
            n64_message_box("Error", "No campaigns found in ROM.");
            return true;
        }

        N64CampaignChoice choice = n64_campaign_select(campaigns);
        if (choice.campaign_idx < 0) return true; // user cancelled

        n64_loading_screen_show("Loading campaign...");

        // Load campaign data from ROM
        saved_game state;
        std::string campaign_id = campaigns[choice.campaign_idx].name;
        if (!load_campaign_from_rom(campaign_id, choice.difficulty, state)) {
            n64_loading_screen_hide();
            n64_message_box("Error", "Failed to load campaign data.");
            return true;
        }

        n64_loading_screen_hide();

        // Play the campaign
        // The campaign_controller manages the scenario sequence
        try {
            campaign_controller cc(state, game_config_manager::get()->game_config(),
                                    game_config_manager::get()->terrain_types());
            cc.play_game();
        } catch (const std::exception& e) {
            N64_LOG("Campaign exception: %s", e.what());
            n64_message_box("Error", std::string("Game error: ") + e.what());
        }
        return true;
    }

    case N64TitleAction::TUTORIAL: {
        n64_loading_screen_show("Loading tutorial...");
        saved_game state;
        if (!load_campaign_from_rom("Tutorial", 0, state)) {
            n64_loading_screen_hide();
            n64_message_box("Error", "Tutorial not found in ROM.");
            return true;
        }
        n64_loading_screen_hide();

        try {
            campaign_controller cc(state, game_config_manager::get()->game_config(),
                                    game_config_manager::get()->terrain_types());
            cc.play_game();
        } catch (const std::exception& e) {
            n64_message_box("Error", std::string("Error: ") + e.what());
        }
        return true;
    }

    case N64TitleAction::LOAD_GAME: {
        // Show save slot selection
        std::vector<N64MenuItem> slots;
        for (int i = 0; i < N64_SAVE_SLOTS; ++i) {
            const N64SaveSlot& s = n64_save_slot_info(i);
            if (s.occupied) {
                char label[64];
                std::snprintf(label, sizeof(label), "Slot %d: %s (Turn %d)",
                              i + 1, s.label, s.turn_number);
                slots.emplace_back(label, i);
            } else {
                char label[32];
                std::snprintf(label, sizeof(label), "Slot %d: [Empty]", i + 1);
                slots.emplace_back(label, "", i, false);
            }
        }

        int slot = n64_menu_show("Load Game", slots, 0, true);
        if (slot == N64_MENU_CANCEL) return true;

        n64_loading_screen_show("Loading save...");
        std::string wml_data;
        if (!n64_save_read(slot, wml_data)) {
            n64_loading_screen_hide();
            n64_message_box("Error", "Failed to read save file.");
            return true;
        }

        // Parse WML and restore game state
        saved_game state;
        config cfg;
        std::istringstream is(wml_data);
        read(cfg, is);
        state.set_data(cfg);

        n64_loading_screen_hide();

        try {
            campaign_controller cc(state, game_config_manager::get()->game_config(),
                                    game_config_manager::get()->terrain_types());
            cc.play_game();
        } catch (const std::exception& e) {
            n64_message_box("Error", std::string("Error: ") + e.what());
        }
        return true;
    }

    case N64TitleAction::MULTIPLAYER: {
        if (!n64_netlib_connected()) {
            n64_message_box("Multiplayer",
                "No USB connection detected.\n\n"
                "To play online, connect your\n"
                "flashcart USB cable to a PC\n"
                "running the NetLib Client App.");
            return true;
        }

        // Multiplayer lobby
        n64_message_box("Multiplayer",
            "Connected to server!\n"
            "Waiting for other players...\n\n"
            "Multiplayer lobby coming soon.");
        // TODO: Full multiplayer lobby implementation
        // For now, return to title
        return true;
    }
    }

    return true;
}

// -----------------------------------------------------------------------
// enumerate_campaigns — scan ROMFS for campaign data
// -----------------------------------------------------------------------
static std::vector<N64CampaignEntry> enumerate_campaigns() {
    std::vector<N64CampaignEntry> result;

    // Look for compiled campaign WML files in ROM
    // Convention: data/campaigns/<name>/_main.bwml
    for (u16 i = 0; i < n64_romfs_entry_count(); ++i) {
        // We scan the string table for campaign main files
        // This is a simplified approach; a real implementation would
        // parse a campaign index built by the asset pipeline
    }

    // Fallback: hardcode the campaigns known to be in the ROM
    // The asset pipeline writes a campaign manifest at a known ROM offset
    const RomFsEntry* manifest = n64_romfs_find("campaigns/manifest.bwml");
    if (manifest) {
        config cfg;
        if (n64_bwml_load(manifest->rom_offset, manifest->byte_length, cfg)) {
            for (const config& c : cfg.child_range("campaign")) {
                N64CampaignEntry entry;
                entry.name = c["id"].str();
                entry.description = c["description"].str();
                entry.difficulty_count = c["difficulties"].to_int(3);
                result.push_back(entry);
            }
        }
    }

    // If manifest is empty, add a stub entry for testing
    if (result.empty()) {
        result.push_back({"Heir_To_The_Throne",
                          "The classic first campaign of Wesnoth.", 3});
        result.push_back({"Tutorial",
                          "Learn the basics of Wesnoth.", 1});
    }

    return result;
}

// -----------------------------------------------------------------------
// load_campaign_from_rom — load campaign starting state from ROMFS
// -----------------------------------------------------------------------
static bool load_campaign_from_rom(const std::string& campaign_id,
                                    int difficulty,
                                    saved_game& state) {
    std::string path = "campaigns/" + campaign_id + "/_main.bwml";
    const RomFsEntry* entry = n64_romfs_find(path.c_str());
    if (!entry) {
        N64_LOG("Campaign not found: %s", path.c_str());
        return false;
    }

    config cfg;
    if (!n64_bwml_load(entry->rom_offset, entry->byte_length, cfg)) {
        N64_LOG("Failed to parse campaign WML: %s", path.c_str());
        return false;
    }

    // Set difficulty
    cfg["difficulty"] = std::to_string(difficulty);

    state.set_data(cfg);
    return true;
}

// -----------------------------------------------------------------------
// N64 save game integration
//
// Called from play_controller when the user triggers a save
// (D-Pad Up = quicksave, or Game Menu > Save)
// -----------------------------------------------------------------------
namespace savegame {

bool n64_do_save(const saved_game& gamestate, const std::string& label,
                  int campaign_idx, int scenario_idx, int turn) {
    // Show slot selection
    std::vector<N64MenuItem> slots;
    for (int i = 0; i < N64_SAVE_SLOTS; ++i) {
        const N64SaveSlot& s = n64_save_slot_info(i);
        char buf[64];
        if (s.occupied) {
            std::snprintf(buf, sizeof(buf), "Slot %d: %s (Turn %d) [Overwrite]",
                          i + 1, s.label, s.turn_number);
        } else {
            std::snprintf(buf, sizeof(buf), "Slot %d: [Empty]", i + 1);
        }
        slots.emplace_back(buf, i);
    }

    int slot = n64_menu_show("Save Game", slots, 0, true);
    if (slot == N64_MENU_CANCEL) return false;

    // Confirm overwrite
    if (n64_save_slot_info(slot).occupied) {
        int r = n64_message_box("Overwrite?",
            "This slot already has a save.\nOverwrite it?",
            N64MsgButtons::YES_NO);
        if (r != 0) return false;
    }

    n64_loading_screen_show("Saving...");

    // Serialise game state to WML text
    std::ostringstream os;
    config cfg;
    gamestate.write_config(cfg);
    ::write(os, cfg);
    std::string wml_data = os.str();

    bool ok = n64_save_write(slot, wml_data, label.c_str(),
                              (u8)campaign_idx, (u8)scenario_idx, (u16)turn);

    n64_loading_screen_hide();

    if (ok) {
        n64_message_box("Save", "Game saved successfully!");
    } else {
        n64_message_box("Error", "Failed to save game.\nSave data may be too large.");
    }
    return ok;
}

} // namespace savegame

#endif // N64_BUILD
