package com.wesnoth.n64;

/**
 * WesnothN64Server.java
 *
 * NetLib-compatible game server for Wesnoth N64 multiplayer.
 *
 * This server:
 *   1. Accepts connections from N64-NetLib Client Apps (up to 4 players)
 *   2. Manages a lobby (player registration, side/faction selection)
 *   3. Relays game packets between players during a scenario
 *   4. Validates turn order (rejects out-of-turn actions)
 *   5. Sends an RNG seed to all players at scenario start
 *   6. Sends heartbeats to the NetLib Master Server for discovery
 *
 * Protocol: N64-NetLib packet format (see netlib_adapter.hpp)
 * Transport: TCP via NetLib Client App
 *
 * Packet types handled:
 *   0x01 MOVE, 0x02 ATTACK, 0x03 RECRUIT, 0x04 RECALL,
 *   0x05 END_TURN, 0x06 CHAT, 0x07 SYNC_RNG, 0x09 LOBBY_INFO,
 *   0x0A SCENARIO, 0x0B GAME_START, 0x0C UNDO, 0x0D SURRENDER
 *
 * Usage:
 *   java -jar wesnoth-n64-server.jar [--port 6465] [--master-server <host:port>]
 *                                    [--max-players 4] [--game-name "My Game"]
 */

import java.io.*;
import java.net.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import java.util.logging.*;

public class WesnothN64Server {

    // -----------------------------------------------------------------------
    // Packet type constants (mirror netlib_adapter.hpp)
    // -----------------------------------------------------------------------
    static final byte PKT_MOVE        = 0x01;
    static final byte PKT_ATTACK      = 0x02;
    static final byte PKT_RECRUIT     = 0x03;
    static final byte PKT_RECALL      = 0x04;
    static final byte PKT_END_TURN    = 0x05;
    static final byte PKT_CHAT        = 0x06;
    static final byte PKT_SYNC_RNG    = 0x07;
    static final byte PKT_STATE_CHUNK = 0x08;
    static final byte PKT_LOBBY_INFO  = 0x09;
    static final byte PKT_SCENARIO    = 0x0A;
    static final byte PKT_GAME_START  = 0x0B;
    static final byte PKT_UNDO        = 0x0C;
    static final byte PKT_SURRENDER   = 0x0D;
    static final byte PKT_STATE_ACK   = 0x0E;

    // NetLib packet header magic bytes
    static final byte[] NETLIB_MAGIC = {'N', 'L', 'P'};

    // -----------------------------------------------------------------------
    // Game state
    // -----------------------------------------------------------------------
    enum Phase { LOBBY, IN_GAME, FINISHED }

    static final int MAX_PLAYERS = 4;
    static volatile Phase s_phase = Phase.LOBBY;
    static final PlayerSlot[] s_players = new PlayerSlot[MAX_PLAYERS];
    static volatile int s_active_side = 0;         // whose turn it is (0-based side index)
    static volatile int s_player_count = 0;
    static volatile String s_game_name = "Wesnoth N64";
    static volatile int s_port = 6465;
    static volatile String s_master_server = "localhost:6464";

    static final Logger LOG = Logger.getLogger("WesnothN64Server");

    // -----------------------------------------------------------------------
    // PlayerSlot
    // -----------------------------------------------------------------------
    static class PlayerSlot {
        final int        client_num;
        String           player_name = "Player";
        int              side        = -1;   // -1 = unassigned
        int              faction     = 0;
        Socket           socket;
        DataInputStream  in;
        DataOutputStream out;
        volatile boolean connected   = false;
        volatile boolean surrendered = false;

        PlayerSlot(int num) { this.client_num = num; }
    }

    // -----------------------------------------------------------------------
    // main
    // -----------------------------------------------------------------------
    public static void main(String[] args) throws Exception {
        // Parse args
        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--port":          s_port = Integer.parseInt(args[++i]); break;
                case "--master-server": s_master_server = args[++i]; break;
                case "--game-name":     s_game_name = args[++i]; break;
                case "--max-players":   /* MAX_PLAYERS is constant; informational only */ break;
                case "--help":
                    System.out.println("Usage: WesnothN64Server [--port N] [--master-server host:port] [--game-name name]");
                    return;
            }
        }

        for (int i = 0; i < MAX_PLAYERS; i++) s_players[i] = new PlayerSlot(i);

        LOG.info("Wesnoth N64 Game Server starting on port " + s_port);

        // Start master server heartbeat thread
        Thread heartbeat = new Thread(WesnothN64Server::masterHeartbeatLoop, "MasterHeartbeat");
        heartbeat.setDaemon(true);
        heartbeat.start();

        // Accept connections
        try (ServerSocket ss = new ServerSocket(s_port)) {
            LOG.info("Listening for NetLib Client Apps on port " + s_port);
            while (true) {
                Socket client = ss.accept();
                assignPlayer(client);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Assign a new connection to a player slot
    // -----------------------------------------------------------------------
    static void assignPlayer(Socket sock) {
        for (PlayerSlot p : s_players) {
            if (!p.connected) {
                p.socket = sock;
                try {
                    p.in  = new DataInputStream(new BufferedInputStream(sock.getInputStream()));
                    p.out = new DataOutputStream(new BufferedOutputStream(sock.getOutputStream()));
                } catch (IOException e) {
                    LOG.warning("Could not get streams for new client: " + e.getMessage());
                    return;
                }
                p.connected = true;
                s_player_count++;
                LOG.info("Player " + p.client_num + " connected from " + sock.getRemoteSocketAddress());

                Thread t = new Thread(() -> playerLoop(p), "Player-" + p.client_num);
                t.setDaemon(true);
                t.start();
                return;
            }
        }
        LOG.warning("No free player slots; rejecting connection from " + sock.getRemoteSocketAddress());
        try { sock.close(); } catch (IOException ignored) {}
    }

    // -----------------------------------------------------------------------
    // Per-player receive loop
    // -----------------------------------------------------------------------
    static void playerLoop(PlayerSlot p) {
        try {
            while (p.connected) {
                // Read NetLib packet header: magic(3) + version(1) + type(1) + flags(1) + seq(2) + recipient_mask(2) + payload_len(2)
                // Simplified: read type + payload_len + payload
                byte[] magic = new byte[3];
                p.in.readFully(magic);
                if (!Arrays.equals(magic, NETLIB_MAGIC)) {
                    LOG.warning("Player " + p.client_num + ": bad magic, disconnecting");
                    break;
                }
                byte version  = p.in.readByte();
                byte pkt_type = p.in.readByte();
                byte flags    = p.in.readByte();
                short seq     = p.in.readShort();
                short recipient_mask = p.in.readShort();
                short payload_len    = p.in.readShort();

                byte[] payload = new byte[payload_len & 0xFFFF];
                p.in.readFully(payload);

                handlePacket(p, pkt_type, payload);
            }
        } catch (IOException e) {
            LOG.info("Player " + p.client_num + " disconnected: " + e.getMessage());
        } finally {
            p.connected = false;
            s_player_count--;
            try { p.socket.close(); } catch (IOException ignored) {}
            broadcastDisconnect(p);
        }
    }

    // -----------------------------------------------------------------------
    // Packet dispatcher
    // -----------------------------------------------------------------------
    static void handlePacket(PlayerSlot from, byte type, byte[] payload) {
        switch (type) {
        case PKT_LOBBY_INFO:
            handleLobbyInfo(from, payload);
            break;

        case PKT_SCENARIO:
            if (from.client_num == 0) { // only host can select scenario
                broadcast(from, type, payload, true);
            }
            break;

        case PKT_GAME_START:
            if (from.client_num == 0 && s_phase == Phase.LOBBY) {
                startGame(from);
            }
            break;

        case PKT_MOVE:
        case PKT_ATTACK:
        case PKT_RECRUIT:
        case PKT_RECALL:
        case PKT_UNDO:
            // Validate it's this player's turn
            if (s_phase == Phase.IN_GAME && from.side == s_active_side) {
                broadcast(from, type, payload, true);
            } else {
                LOG.warning("Player " + from.client_num + " tried to act out of turn (active=" + s_active_side + ")");
            }
            break;

        case PKT_END_TURN:
            if (s_phase == Phase.IN_GAME && from.side == s_active_side) {
                broadcast(from, type, payload, true);
                advanceTurn();
            }
            break;

        case PKT_CHAT:
            // Chat is always relayed regardless of turn
            broadcast(from, type, payload, true);
            break;

        case PKT_SURRENDER:
            from.surrendered = true;
            broadcast(from, type, payload, true);
            LOG.info("Player " + from.client_num + " (" + from.player_name + ") surrendered");
            checkGameOver();
            break;

        default:
            LOG.warning("Unknown packet type 0x" + Integer.toHexString(type & 0xFF) +
                        " from player " + from.client_num);
        }
    }

    // -----------------------------------------------------------------------
    // Lobby info handler
    // -----------------------------------------------------------------------
    static void handleLobbyInfo(PlayerSlot p, byte[] payload) {
        if (payload.length < 35) return;
        p.side    = payload[1] & 0xFF;
        p.faction = payload[2] & 0xFF;
        // Name is null-terminated in bytes 3..34
        int name_end = 3;
        while (name_end < payload.length && payload[name_end] != 0) name_end++;
        try {
            p.player_name = new String(payload, 3, name_end - 3, "UTF-8");
        } catch (Exception e) { p.player_name = "Player" + p.client_num; }

        LOG.info("Player " + p.client_num + " is " + p.player_name +
                 " (side=" + p.side + " faction=" + p.faction + ")");

        // Relay lobby info to all other players so they update their lobby display
        broadcast(p, PKT_LOBBY_INFO, payload, true);
    }

    // -----------------------------------------------------------------------
    // Start game: send RNG seed to all, broadcast GAME_START
    // -----------------------------------------------------------------------
    static void startGame(PlayerSlot host) {
        s_phase = Phase.IN_GAME;
        s_active_side = 0;

        // Generate and broadcast RNG seed
        long seed = new Random().nextLong();
        byte[] rng_pkt = new byte[4];
        rng_pkt[0] = (byte)(seed & 0xFF);
        rng_pkt[1] = (byte)((seed >> 8)  & 0xFF);
        rng_pkt[2] = (byte)((seed >> 16) & 0xFF);
        rng_pkt[3] = (byte)((seed >> 24) & 0xFF);
        broadcastAll(PKT_SYNC_RNG, rng_pkt);

        // Broadcast GAME_START
        broadcastAll(PKT_GAME_START, new byte[0]);
        LOG.info("Game started! RNG seed=" + seed + " players=" + s_player_count);
    }

    // -----------------------------------------------------------------------
    // Advance to next active (non-surrendered) side
    // -----------------------------------------------------------------------
    static void advanceTurn() {
        int sides = s_player_count;
        for (int i = 1; i <= sides; i++) {
            int next = (s_active_side + i) % sides;
            // Find the player for this side
            boolean surrendered = false;
            for (PlayerSlot p : s_players) {
                if (p.connected && p.side == next && p.surrendered) { surrendered = true; break; }
            }
            if (!surrendered) { s_active_side = next; return; }
        }
        // All players surrendered or only one remains
        checkGameOver();
    }

    // -----------------------------------------------------------------------
    // Check if game is over (only one side remaining)
    // -----------------------------------------------------------------------
    static void checkGameOver() {
        int active = 0;
        for (PlayerSlot p : s_players) {
            if (p.connected && !p.surrendered) active++;
        }
        if (active <= 1) {
            s_phase = Phase.FINISHED;
            LOG.info("Game over!");
        }
    }

    // -----------------------------------------------------------------------
    // Send a packet to all players except 'from'
    // -----------------------------------------------------------------------
    static void broadcast(PlayerSlot from, byte type, byte[] payload, boolean include_from) {
        for (PlayerSlot p : s_players) {
            if (!p.connected) continue;
            if (!include_from && p == from) continue;
            sendPacket(p, type, payload, (byte)from.client_num);
        }
    }

    static void broadcastAll(byte type, byte[] payload) {
        for (PlayerSlot p : s_players) {
            if (p.connected) sendPacket(p, type, payload, (byte)0xFF);
        }
    }

    static void broadcastDisconnect(PlayerSlot who) {
        // Inform remaining players
        byte[] notice = new byte[]{(byte)who.client_num};
        for (PlayerSlot p : s_players) {
            if (p.connected && p != who)
                sendPacket(p, (byte)0xFF, notice, (byte)0xFF); // 0xFF = disconnect notification
        }
    }

    // -----------------------------------------------------------------------
    // Send a NetLib-framed packet to a player
    // -----------------------------------------------------------------------
    static void sendPacket(PlayerSlot p, byte type, byte[] payload, byte from_client) {
        try {
            synchronized (p.out) {
                p.out.write(NETLIB_MAGIC);
                p.out.writeByte(1);              // version
                p.out.writeByte(type);
                p.out.writeByte(0);              // flags
                p.out.writeShort(0);             // sequence
                p.out.writeShort(0xFFFF);        // recipient mask = broadcast
                p.out.writeShort(payload.length);
                p.out.write(payload);
                p.out.flush();
            }
        } catch (IOException e) {
            LOG.warning("Failed to send to player " + p.client_num + ": " + e.getMessage());
            p.connected = false;
        }
    }

    // -----------------------------------------------------------------------
    // Master server heartbeat (every 5 minutes per NetLib spec, 10-min TTL)
    // -----------------------------------------------------------------------
    static void masterHeartbeatLoop() {
        String[] parts = s_master_server.split(":");
        String host = parts[0];
        int port = parts.length > 1 ? Integer.parseInt(parts[1]) : 6464;

        while (true) {
            try {
                // Send a heartbeat packet to the master server
                // Format is NetLib-specific; this is a minimal implementation
                try (Socket ms = new Socket(host, port)) {
                    DataOutputStream out = new DataOutputStream(ms.getOutputStream());
                    // Heartbeat: magic + game name + player count + game port
                    out.write(NETLIB_MAGIC);
                    out.writeByte(1);    // version
                    out.writeByte(0x20); // HEARTBEAT type
                    byte[] name_bytes = s_game_name.getBytes("UTF-8");
                    out.writeShort(name_bytes.length + 3);
                    out.write(name_bytes);
                    out.writeByte(s_player_count);
                    out.writeShort(s_port);
                    out.flush();
                }
            } catch (Exception e) {
                LOG.warning("Heartbeat to master server failed: " + e.getMessage());
            }

            try { Thread.sleep(5 * 60 * 1000L); } catch (InterruptedException e) { break; }
        }
    }
}
