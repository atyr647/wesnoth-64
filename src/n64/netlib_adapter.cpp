/**
 * netlib_adapter.cpp
 *
 * NetLib adapter implementation for Wesnoth N64.
 *
 * All packet callbacks are registered at init time.
 * Received packets are decoded and pushed to s_event_queue for
 * consumption by the game loop (n64_netlib_poll / n64_netlib_dequeue).
 */

#include "netlib_adapter.hpp"
#include "n64_memory.hpp"

#ifdef N64_BUILD
extern "C" {
#  include <netlib.h>
}
#endif

// -----------------------------------------------------------------------
// Internal state
// -----------------------------------------------------------------------
static NetEventRecord s_event_queue[NET_EVENT_QUEUE_CAP];
static u8             s_queue_head  = 0;
static u8             s_queue_tail  = 0;
static u8             s_queue_count = 0;
static u8             s_client_num  = 0;
static u8             s_player_count = 0;
static bool           s_connected   = false;

// -----------------------------------------------------------------------
// Queue helpers
// -----------------------------------------------------------------------
static void enqueue(const NetEventRecord& rec) {
    if (s_queue_count >= NET_EVENT_QUEUE_CAP) {
        N64_LOG("NetLib: event queue full, dropping event %u", (unsigned)rec.type);
        return;
    }
    s_event_queue[s_queue_tail] = rec;
    s_queue_tail = (s_queue_tail + 1) % NET_EVENT_QUEUE_CAP;
    ++s_queue_count;
}

// -----------------------------------------------------------------------
// Packet receive callbacks (called by NetLib from n64_netlib_poll)
// -----------------------------------------------------------------------

#ifdef N64_BUILD

static void cb_move(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::MOVE;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.move), sizeof(PktMove));
    enqueue(r);
}

static void cb_attack(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::ATTACK;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.attack), sizeof(PktAttack));
    enqueue(r);
}

static void cb_recruit(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::RECRUIT;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.recruit), sizeof(PktRecruit));
    enqueue(r);
}

static void cb_recall(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::RECALL;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.recall), sizeof(PktRecall));
    enqueue(r);
}

static void cb_end_turn(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::END_TURN;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.end_turn), sizeof(PktEndTurn));
    enqueue(r);
}

static void cb_chat(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::CHAT;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.chat), sizeof(PktChat));
    enqueue(r);
}

static void cb_sync_rng(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::SYNC_RNG;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.sync_rng), sizeof(PktSyncRng));
    enqueue(r);
}

static void cb_state_chunk(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::STATE_CHUNK;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.state_chunk), sizeof(PktStateChunk));
    enqueue(r);
}

static void cb_lobby_info(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::LOBBY_INFO;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.lobby_info), sizeof(PktLobbyInfo));
    ++s_player_count;
    enqueue(r);
}

static void cb_scenario(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::SCENARIO;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.scenario), sizeof(PktScenario));
    enqueue(r);
}

static void cb_game_start(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::GAME_START;
    r.from_client = from;
    enqueue(r);
}

static void cb_undo(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::UNDO;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.undo), sizeof(PktUndo));
    enqueue(r);
}

static void cb_surrender(u8 from) {
    NetEventRecord r;
    r.type = NetEvent::SURRENDER;
    r.from_client = from;
    netlib_readbytes(reinterpret_cast<u8*>(&r.surrender), sizeof(PktSurrender));
    enqueue(r);
}

static void cb_disconnect() {
    s_connected = false;
    NetEventRecord r;
    r.type = NetEvent::PLAYER_DISCONNECTED;
    r.from_client = 0xFF; // unknown
    enqueue(r);
    N64_LOG("NetLib: disconnected from server");
}

static void cb_reconnect() {
    s_connected = true;
    NetEventRecord r;
    r.type = NetEvent::PLAYER_CONNECTED;
    r.from_client = s_client_num;
    enqueue(r);
    N64_LOG("NetLib: reconnected");
}

#endif // N64_BUILD

// -----------------------------------------------------------------------
// n64_netlib_init
// -----------------------------------------------------------------------
bool n64_netlib_init() {
#ifdef N64_BUILD
    if (!netlib_initialize()) {
        N64_LOG("NetLib: failed to initialise USB");
        return false;
    }
    s_client_num = netlib_getclient();
    s_connected  = true;

    // Register all packet type handlers
    netlib_register(PKTTYPE_MOVE,        cb_move);
    netlib_register(PKTTYPE_ATTACK,      cb_attack);
    netlib_register(PKTTYPE_RECRUIT,     cb_recruit);
    netlib_register(PKTTYPE_RECALL,      cb_recall);
    netlib_register(PKTTYPE_END_TURN,    cb_end_turn);
    netlib_register(PKTTYPE_CHAT,        cb_chat);
    netlib_register(PKTTYPE_SYNC_RNG,    cb_sync_rng);
    netlib_register(PKTTYPE_STATE_CHUNK, cb_state_chunk);
    netlib_register(PKTTYPE_LOBBY_INFO,  cb_lobby_info);
    netlib_register(PKTTYPE_SCENARIO,    cb_scenario);
    netlib_register(PKTTYPE_GAME_START,  cb_game_start);
    netlib_register(PKTTYPE_UNDO,        cb_undo);
    netlib_register(PKTTYPE_SURRENDER,   cb_surrender);

    // Disconnect / reconnect handlers
    netlib_callback_disconnect(5000, cb_disconnect);
    netlib_callback_reconnect(cb_reconnect);

    N64_LOG("NetLib: initialised, client=%u", s_client_num);
#else
    N64_LOG("NetLib: host stub (no USB), multiplayer disabled");
    s_connected = false;
#endif
    return true;
}

// -----------------------------------------------------------------------
// n64_netlib_poll — call every frame in the MP game loop
// -----------------------------------------------------------------------
void n64_netlib_poll() {
#ifdef N64_BUILD
    if (s_connected)
        netlib_poll();
#endif
}

// -----------------------------------------------------------------------
// Queue accessors
// -----------------------------------------------------------------------
u8 n64_netlib_event_count() { return s_queue_count; }

bool n64_netlib_dequeue(NetEventRecord* out) {
    if (s_queue_count == 0) return false;
    *out = s_event_queue[s_queue_head];
    s_queue_head = (s_queue_head + 1) % NET_EVENT_QUEUE_CAP;
    --s_queue_count;
    return true;
}

// -----------------------------------------------------------------------
// Send helpers
// -----------------------------------------------------------------------
#define SEND_PKT(type_id, pkt_ptr, pkt_size) \
    do { \
        netlib_start(type_id); \
        netlib_writebytes(reinterpret_cast<const u8*>(pkt_ptr), pkt_size); \
        netlib_sendtoserver(); \
    } while(0)

void n64_net_send_move(const PktMove& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_MOVE, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_attack(const PktAttack& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_ATTACK, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_recruit(const PktRecruit& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_RECRUIT, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_recall(const PktRecall& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_RECALL, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_end_turn(const PktEndTurn& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_END_TURN, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_chat(const PktChat& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_CHAT, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_undo(const PktUndo& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_UNDO, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_surrender(const PktSurrender& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_SURRENDER, &pkt, sizeof(pkt));
#endif
}
void n64_net_send_lobby_info(const PktLobbyInfo& pkt) {
#ifdef N64_BUILD
    SEND_PKT(PKTTYPE_LOBBY_INFO, &pkt, sizeof(pkt));
#endif
}

#undef SEND_PKT

// -----------------------------------------------------------------------
// State accessors
// -----------------------------------------------------------------------
bool n64_netlib_connected()    { return s_connected; }
u8   n64_netlib_client_num()   { return s_client_num; }
u8   n64_netlib_player_count() { return s_player_count; }
