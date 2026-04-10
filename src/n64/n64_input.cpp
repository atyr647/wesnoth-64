/**
 * n64_input.cpp
 *
 * N64 controller input implementation for Wesnoth.
 * Uses libdragon's joypad subsystem.
 */

#include "n64_input.hpp"

#ifdef N64_BUILD
#  include <joypad.h>
#endif

#include "../hotkey/hotkey_command.hpp"

// -----------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------
static constexpr s8  STICK_DEADZONE        = 20;  // raw units (N64 range ±80)
static constexpr u32 CURSOR_REPEAT_DELAY   = 20;  // frames before auto-repeat
static constexpr u32 CURSOR_REPEAT_PERIOD  = 6;   // frames between auto-repeat steps

// -----------------------------------------------------------------------
// Internal per-port state
// -----------------------------------------------------------------------
struct PortState {
    N64InputState pub;      // public snapshot
    u32           held_frames[N64_ACTION_COUNT]; // consecutive frames held
};

static PortState s_ports[4];
static u8        s_active_port = 0;

// -----------------------------------------------------------------------
// Button bitmask constants (matching joypad_buttons_t.raw bit positions)
// -----------------------------------------------------------------------
#ifdef N64_BUILD
static constexpr u16 BTN_A     = (1 << 0);
static constexpr u16 BTN_B     = (1 << 1);
static constexpr u16 BTN_Z     = (1 << 2);
static constexpr u16 BTN_START = (1 << 3);
static constexpr u16 BTN_DU    = (1 << 4);
static constexpr u16 BTN_DD    = (1 << 5);
static constexpr u16 BTN_DL    = (1 << 6);
static constexpr u16 BTN_DR    = (1 << 7);
static constexpr u16 BTN_L     = (1 << 10);
static constexpr u16 BTN_R     = (1 << 11);
static constexpr u16 BTN_CU    = (1 << 12);
static constexpr u16 BTN_CD    = (1 << 13);
static constexpr u16 BTN_CL    = (1 << 14);
static constexpr u16 BTN_CR    = (1 << 15);
#endif

// -----------------------------------------------------------------------
// Button → Action mapping table
// -----------------------------------------------------------------------
struct ButtonMap {
    u16       btn_mask;    // bitmask for joypad_buttons_t.raw
    bool      needs_z;     // requires Z held as modifier
    N64Action action;
};

#ifdef N64_BUILD
static const ButtonMap k_button_map[] = {
    { BTN_A,     false, N64_ACTION_SELECT         },
    { BTN_B,     false, N64_ACTION_CANCEL         },
    { BTN_R,     false, N64_ACTION_CONTEXT_MENU   },
    { BTN_L,     false, N64_ACTION_CYCLE_UNITS_BWD},
    { BTN_CU,    false, N64_ACTION_SCROLL_UP      },
    { BTN_CD,    false, N64_ACTION_SCROLL_DOWN    },
    { BTN_CL,    false, N64_ACTION_SCROLL_LEFT    },
    { BTN_CR,    false, N64_ACTION_CYCLE_UNITS_FWD},
    { BTN_DU,    false, N64_ACTION_QUICKSAVE      },
    { BTN_DD,    false, N64_ACTION_END_TURN       },
    { BTN_DL,    false, N64_ACTION_UNDO           },
    { BTN_DR,    false, N64_ACTION_UNIT_INFO      },
    { BTN_START, false, N64_ACTION_GAME_MENU      },
    // Z-modified
    { BTN_A,     true,  N64_ACTION_ATTACK         },
    { BTN_B,     true,  N64_ACTION_RECALL         },
    { BTN_R,     true,  N64_ACTION_RECRUIT        },
    { BTN_START, true,  N64_ACTION_SURRENDER      },
};
#endif

// -----------------------------------------------------------------------
// n64_input_init
// -----------------------------------------------------------------------
void n64_input_init() {
    for (int p = 0; p < 4; ++p) {
        std::memset(&s_ports[p], 0, sizeof(s_ports[p]));
        s_ports[p].pub.port = p;
    }
#ifdef N64_BUILD
    joypad_init();
#endif
    N64_LOG("Input init: %u controllers", n64_input_connected_count());
}

// -----------------------------------------------------------------------
// n64_input_poll — call once per frame
// -----------------------------------------------------------------------
void n64_input_poll() {
#ifdef N64_BUILD
    joypad_poll();
#endif

    for (int p = 0; p < 4; ++p) {
        PortState& ps = s_ports[p];
        N64InputState& st = ps.pub;

        // Reset edge-detect and delta
        std::memset(st.just_pressed, 0, sizeof(st.just_pressed));
        st.cursor_dx = 0;
        st.cursor_dy = 0;

#ifdef N64_BUILD
        if (!joypad_is_connected((joypad_port_t)p)) continue;

        joypad_buttons_t pressed  = joypad_get_buttons_pressed((joypad_port_t)p);
        joypad_buttons_t released = joypad_get_buttons_released((joypad_port_t)p);
        joypad_buttons_t held_raw = joypad_get_buttons_held((joypad_port_t)p);
        joypad_inputs_t  inputs   = joypad_get_inputs((joypad_port_t)p);

        st.stick_x = inputs.stick_x;
        st.stick_y = inputs.stick_y;

        bool z_held = held_raw.z;

        for (const auto& bm : k_button_map) {
            if (bm.needs_z != z_held) continue;
            bool is_pressed = (pressed.raw  & bm.btn_mask) != 0;
            bool is_held    = (held_raw.raw & bm.btn_mask) != 0;
            N64Action act   = bm.action;

            if (is_pressed) {
                st.just_pressed[act] = true;
                st.held[act]         = true;
                ps.held_frames[act]  = 0;
            } else if (is_held) {
                ps.held_frames[act]++;
            } else if ((released.raw & bm.btn_mask) != 0) {
                st.held[act]        = false;
                ps.held_frames[act] = 0;
            }
        }

        // Analog stick → cursor delta (with deadzone and auto-repeat)
        auto stick_axis = [&](s8 raw, N64Action neg_act, N64Action pos_act) -> s8 {
            if (raw > STICK_DEADZONE) {
                ps.held_frames[pos_act]++;
                st.held[pos_act] = true;
                st.held[neg_act] = false;
                if (ps.held_frames[pos_act] == 1 ||
                    (ps.held_frames[pos_act] > CURSOR_REPEAT_DELAY &&
                     ps.held_frames[pos_act] % CURSOR_REPEAT_PERIOD == 0)) {
                    return 1;
                }
            } else if (raw < -STICK_DEADZONE) {
                ps.held_frames[neg_act]++;
                st.held[neg_act] = true;
                st.held[pos_act] = false;
                if (ps.held_frames[neg_act] == 1 ||
                    (ps.held_frames[neg_act] > CURSOR_REPEAT_DELAY &&
                     ps.held_frames[neg_act] % CURSOR_REPEAT_PERIOD == 0)) {
                    return -1;
                }
            } else {
                ps.held_frames[neg_act] = 0;
                ps.held_frames[pos_act] = 0;
                st.held[neg_act] = false;
                st.held[pos_act] = false;
            }
            return 0;
        };

        st.cursor_dx = stick_axis(inputs.stick_x, N64_ACTION_CURSOR_LEFT,  N64_ACTION_CURSOR_RIGHT);
        st.cursor_dy = stick_axis(inputs.stick_y, N64_ACTION_CURSOR_DOWN,  N64_ACTION_CURSOR_UP);

#endif // N64_BUILD
    }
}

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------
const N64InputState& n64_input_get(u8 port) {
    N64_ASSERT(port < 4);
    return s_ports[port].pub;
}

void n64_input_set_active_port(u8 port) {
    N64_ASSERT(port < 4);
    s_active_port = port;
}

bool n64_input_connected(u8 port) {
#ifdef N64_BUILD
    return joypad_is_connected((joypad_port_t)port);
#else
    return port == 0;
#endif
}

u8 n64_input_connected_count() {
    u8 c = 0;
    for (u8 p = 0; p < 4; ++p)
        if (n64_input_connected(p)) ++c;
    return c;
}

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
