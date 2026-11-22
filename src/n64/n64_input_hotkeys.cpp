/**
 * n64_input_hotkeys.cpp
 *
 * Translates N64 actions to Wesnoth hotkey commands. Kept apart from
 * n64_input.cpp because it needs the engine's hotkey headers; the input
 * module itself stays engine-free.
 */

#include "n64_input.hpp"
#include "../hotkey/hotkey_command.hpp"

// -----------------------------------------------------------------------
// Hotkey translation
// Maps N64 actions to src/hotkey/hotkey_command.hpp hotkey_command::type
// -----------------------------------------------------------------------
int n64_action_to_hotkey(N64Action action) {
    using namespace hotkey;
    switch (action) {
    case N64_ACTION_SELECT:          return HOTKEY_SELECT_HEX;
    case N64_ACTION_CANCEL:          return HOTKEY_DESELECT_HEX;
    case N64_ACTION_END_TURN:        return HOTKEY_ENDTURN;
    case N64_ACTION_UNDO:            return HOTKEY_UNDO;
    case N64_ACTION_CYCLE_UNITS_FWD: return HOTKEY_CYCLE_UNITS;
    case N64_ACTION_RECRUIT:         return HOTKEY_RECRUIT;
    case N64_ACTION_RECALL:          return HOTKEY_RECALL;
    case N64_ACTION_ATTACK:          return HOTKEY_SELECT_AND_ACTION;
    case N64_ACTION_QUICKSAVE:       return HOTKEY_SAVE_GAME;
    case N64_ACTION_GAME_MENU:       return HOTKEY_QUIT_GAME;
    case N64_ACTION_UNIT_INFO:       return HOTKEY_UNIT_DESCRIPTION;
    default:                         return HOTKEY_NULL;
    }
}
