#pragma once

/**
 * n64_input.hpp
 *
 * N64 controller input layer for Wesnoth.
 *
 * Replaces mouse + keyboard input from SDL2.
 *
 * Controller mapping:
 *
 *   Analog Stick       Move hex cursor
 *   A                  Select / Confirm
 *   B                  Cancel / Back
 *   Z                  Modifier (hold for secondary actions)
 *   R                  Open context menu / right-click equivalent
 *   L                  Cycle units backward
 *   C-Right            Cycle units forward
 *   C-Up               Scroll map up
 *   C-Down             Scroll map down
 *   C-Left             Scroll map left
 *   Start              Game menu (options, save, quit)
 *   D-Pad Up           Quick-save
 *   D-Pad Down         End turn
 *   D-Pad Left         Undo move
 *   D-Pad Right        Show unit info
 *   Z + A              Open attack dialog
 *   Z + B              Recall unit
 *   Z + R              Recruit unit
 *   Z + Start          Surrender / resign
 *
 * Player 1 = controller port 1; hotseat players use ports 1-4 in turn.
 */

#include "n64_platform.hpp"

// -----------------------------------------------------------------------
// Action enum — these map to Wesnoth's hotkey IDs
// (see src/hotkey/hotkey_command.hpp HOTKEY_* values)
// -----------------------------------------------------------------------
enum N64Action : u16 {
    N64_ACTION_NONE = 0,

    // Cursor movement (resolved as a delta each frame)
    N64_ACTION_CURSOR_UP,
    N64_ACTION_CURSOR_DOWN,
    N64_ACTION_CURSOR_LEFT,
    N64_ACTION_CURSOR_RIGHT,

    // Primary game actions
    N64_ACTION_SELECT,          // A
    N64_ACTION_CANCEL,          // B
    N64_ACTION_CONTEXT_MENU,    // R
    N64_ACTION_CYCLE_UNITS_FWD, // C-Right
    N64_ACTION_CYCLE_UNITS_BWD, // L

    // Map scrolling
    N64_ACTION_SCROLL_UP,       // C-Up
    N64_ACTION_SCROLL_DOWN,     // C-Down
    N64_ACTION_SCROLL_LEFT,     // C-Left
    N64_ACTION_SCROLL_RIGHT,    // (implied by scroll scheme)

    // Turn actions
    N64_ACTION_END_TURN,        // D-Pad Down
    N64_ACTION_UNDO,            // D-Pad Left
    N64_ACTION_UNIT_INFO,       // D-Pad Right
    N64_ACTION_QUICKSAVE,       // D-Pad Up

    // Z-modified actions
    N64_ACTION_ATTACK,          // Z + A
    N64_ACTION_RECALL,          // Z + B
    N64_ACTION_RECRUIT,         // Z + R

    // Menus
    N64_ACTION_GAME_MENU,       // Start
    N64_ACTION_SURRENDER,       // Z + Start

    N64_ACTION_COUNT,
};

// -----------------------------------------------------------------------
// Input state snapshot (one per controller port)
// -----------------------------------------------------------------------
struct N64InputState {
    // Pressed this frame (not last frame) — edge-detect
    bool just_pressed[N64_ACTION_COUNT];
    // Held (including auto-repeat for held directions/scroll)
    bool held[N64_ACTION_COUNT];

    // Analog cursor delta in hex-grid units (−1, 0, +1 per axis)
    // Accumulated from stick position; steps at cursor_move_threshold
    s8   cursor_dx;
    s8   cursor_dy;

    // Raw analog values (−80 to 80, N64 stick range)
    s8   stick_x;
    s8   stick_y;

    // Which controller port this state is for (0-3)
    u8   port;
};

// -----------------------------------------------------------------------
// Input subsystem API
// -----------------------------------------------------------------------

// Initialise controller subsystem. Call once after n64_memory_init().
void n64_input_init();

// Poll all connected controllers; update internal state.
// Call once per frame before reading any input.
void n64_input_poll();

// Get the input state for the active player's controller port.
const N64InputState& n64_input_get(u8 port = 0);

// Set which controller port is "active" for the current player's turn.
// Used in hotseat multiplayer to route the right controller.
void n64_input_set_active_port(u8 port);

// Returns true if controller on 'port' is connected.
bool n64_input_connected(u8 port);

// Returns number of connected controllers.
u8   n64_input_connected_count();

// -----------------------------------------------------------------------
// Hotkey mapping helpers
// (translate N64Action → src/hotkey/hotkey_command.hpp hotkey_command::type)
// -----------------------------------------------------------------------
int n64_action_to_hotkey(N64Action action);
