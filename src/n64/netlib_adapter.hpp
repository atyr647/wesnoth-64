#pragma once

/**
 * netlib_adapter.hpp
 *
 * Wesnoth multiplayer adapter for N64-NetLib.
 * https://github.com/buu342/N64-NetLib
 *
 * This layer sits between Wesnoth's existing multiplayer logic
 * (src/playmp_controller.cpp, src/synced_commands.cpp) and the
 * NetLib send/receive API.
 *
 * Architecture:
 *   - NetLib routes packets via flashcart USB → Client App PC → game server
 *   - The game server is a relay: it validates turn order and broadcasts commands
 *   - All game logic runs identically on each N64 in lockstep
 *   - RNG seed is synced at scenario start so combat is deterministic
 *
 * Packet type IDs (0–63, NetLib limit):
 *   0x01  PKTTYPE_MOVE        Unit movement
 *   0x02  PKTTYPE_ATTACK      Attack command
 *   0x03  PKTTYPE_RECRUIT     Recruit unit
 *   0x04  PKTTYPE_RECALL      Recall unit
 *   0x05  PKTTYPE_END_TURN    End turn
 *   0x06  PKTTYPE_CHAT        Chat message
 *   0x07  PKTTYPE_SYNC_RNG    RNG seed sync (server → all at scenario start)
 *   0x08  PKTTYPE_STATE_CHUNK Game state sync chunk (chunked, for scenario load)
 *   0x09  PKTTYPE_LOBBY_INFO  Player name / side / faction (lobby phase)
 *   0x0A  PKTTYPE_SCENARIO    Scenario selection
 *   0x0B  PKTTYPE_GAME_START  Game start signal from server
 *   0x0C  PKTTYPE_UNDO        Undo request
 *   0x0D  PKTTYPE_SURRENDER   Player surrenders/resigns
 *   0x0E  PKTTYPE_STATE_ACK   Acknowledge receipt of a state chunk
 */

#include "n64_platform.hpp"

#ifdef N64_BUILD
extern "C" {
#  include <netlib.h>  // N64-NetLib single-file header
}
#endif

// -----------------------------------------------------------------------
// Packet type constants
// -----------------------------------------------------------------------
enum WesnothPacketType : u8 {
    PKTTYPE_MOVE        = 0x01,
    PKTTYPE_ATTACK      = 0x02,
    PKTTYPE_RECRUIT     = 0x03,
    PKTTYPE_RECALL      = 0x04,
    PKTTYPE_END_TURN    = 0x05,
    PKTTYPE_CHAT        = 0x06,
    PKTTYPE_SYNC_RNG    = 0x07,
    PKTTYPE_STATE_CHUNK = 0x08,
    PKTTYPE_LOBBY_INFO  = 0x09,
    PKTTYPE_SCENARIO    = 0x0A,
    PKTTYPE_GAME_START  = 0x0B,
    PKTTYPE_UNDO        = 0x0C,
    PKTTYPE_SURRENDER   = 0x0D,
    PKTTYPE_STATE_ACK   = 0x0E,
};

// -----------------------------------------------------------------------
// Compact packet payloads (all little-endian, packed)
// Max payload is 4096 bytes (NetLib limit).
// -----------------------------------------------------------------------

// PKTTYPE_MOVE (max path length 64 hexes = 128 bytes)
struct PktMove {
    u8  unit_id;       // unit index on game board (0-127)
    u8  path_len;      // number of hex steps
    s8  path_dx[64];   // per-step dx (−1, 0, +1)
    s8  path_dy[64];   // per-step dy (−1, 0, +1)
    u8  vision_update; // 1 = recalculate fog after move
} __attribute__((packed));

// PKTTYPE_ATTACK
struct PktAttack {
    u8  attacker_id;   // unit index of attacker
    u8  defender_id;   // unit index of defender
    u8  weapon_idx;    // which attack the attacker uses (0-based)
} __attribute__((packed));

// PKTTYPE_RECRUIT
struct PktRecruit {
    u8  side;          // side number (0-based)
    u16 unit_type_id;  // interned unit type string handle
    s16 hex_x;
    s16 hex_y;
} __attribute__((packed));

// PKTTYPE_RECALL
struct PktRecall {
    u8  side;
    u8  unit_id;       // unit ID from recall list
    s16 hex_x;
    s16 hex_y;
} __attribute__((packed));

// PKTTYPE_END_TURN
struct PktEndTurn {
    u8  side;
} __attribute__((packed));

// PKTTYPE_CHAT (max 200 chars)
struct PktChat {
    u8  side;
    u8  msg_len;
    char msg[200];
} __attribute__((packed));

// PKTTYPE_SYNC_RNG
struct PktSyncRng {
    u32 seed;          // MT19937 seed for this scenario
} __attribute__((packed));

// PKTTYPE_LOBBY_INFO
struct PktLobbyInfo {
    u8  client_num;
    u8  side;          // requested side (0 = any)
    u8  faction;       // faction index
    char player_name[32];
} __attribute__((packed));

// PKTTYPE_SCENARIO
struct PktScenario {
    u16 campaign_id;   // interned campaign name
    u16 scenario_idx;  // scenario index within campaign
} __attribute__((packed));

// PKTTYPE_STATE_CHUNK (chunked game state sync, up to 4096 bytes each)
struct PktStateChunk {
    u16 chunk_idx;
    u16 total_chunks;
    u16 payload_len;
    u8  data[4064];    // 4096 - 32 bytes header
} __attribute__((packed));

// PKTTYPE_UNDO
struct PktUndo {
    u8  side;
    u8  action_count;  // how many actions to undo
} __attribute__((packed));

// PKTTYPE_SURRENDER
struct PktSurrender {
    u8  side;
} __attribute__((packed));

// -----------------------------------------------------------------------
// Received event (queued by callbacks, consumed by game loop)
// -----------------------------------------------------------------------
enum class NetEvent : u8 {
    NONE,
    MOVE, ATTACK, RECRUIT, RECALL, END_TURN,
    CHAT, SYNC_RNG, STATE_CHUNK, LOBBY_INFO,
    SCENARIO, GAME_START, UNDO, SURRENDER, STATE_ACK,
    PLAYER_CONNECTED, PLAYER_DISCONNECTED,
};

struct NetEventRecord {
    NetEvent type;
    u8       from_client;
    union {
        PktMove        move;
        PktAttack      attack;
        PktRecruit     recruit;
        PktRecall      recall;
        PktEndTurn     end_turn;
        PktChat        chat;
        PktSyncRng     sync_rng;
        PktStateChunk  state_chunk;
        PktLobbyInfo   lobby_info;
        PktScenario    scenario;
        PktUndo        undo;
        PktSurrender   surrender;
    };
};

// Event queue (ring buffer, max 64 pending events)
static constexpr u8 NET_EVENT_QUEUE_CAP = 64;

// -----------------------------------------------------------------------
// Multiplayer adapter API
// -----------------------------------------------------------------------

// Initialise NetLib USB and register all packet callbacks.
// Call once at startup if multiplayer is enabled.
bool n64_netlib_init();

// Poll for incoming packets. Call every frame.
// Dispatches received data into the event queue.
void n64_netlib_poll();

// Returns the number of pending events in the queue.
u8   n64_netlib_event_count();

// Dequeue the next event. Returns false if queue is empty.
bool n64_netlib_dequeue(NetEventRecord* out);

// --- Send helpers (called by game logic to transmit actions) -----------

void n64_net_send_move      (const PktMove&      pkt);
void n64_net_send_attack    (const PktAttack&    pkt);
void n64_net_send_recruit   (const PktRecruit&   pkt);
void n64_net_send_recall    (const PktRecall&    pkt);
void n64_net_send_end_turn  (const PktEndTurn&   pkt);
void n64_net_send_chat      (const PktChat&      pkt);
void n64_net_send_undo      (const PktUndo&      pkt);
void n64_net_send_surrender (const PktSurrender& pkt);
void n64_net_send_lobby_info(const PktLobbyInfo& pkt);

// --- Connection state --------------------------------------------------

// Returns true if connected to the Client App and registered with a server.
bool n64_netlib_connected();

// Returns this N64's client number (0-3).
u8   n64_netlib_client_num();

// Returns number of players currently in the game/lobby.
u8   n64_netlib_player_count();
