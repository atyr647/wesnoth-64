/**
 * n64_wesnothd_connection.cpp
 *
 * N64 replacement for src/wesnothd_connection.cpp.
 *
 * On PC, wesnothd_connection manages a TCP socket to the Wesnoth server
 * using boost::asio with background threads. On N64, multiplayer goes
 * through N64-NetLib (USB → PC relay app → Java game server).
 *
 * This provides the wesnothd_connection class interface but routes all
 * traffic through netlib_adapter.hpp's packet system.
 */

#ifdef N64_BUILD

#include "../wesnothd_connection.hpp"
#include "netlib_adapter.hpp"
#include "n64_platform.hpp"

// -----------------------------------------------------------------------
// wesnothd_connection implementation for N64
// -----------------------------------------------------------------------

wesnothd_connection::wesnothd_connection(const std::string& host, const std::string& service)
    : host_(host)
    , service_(service)
{
    N64_LOG("wesnothd_connection: connecting via N64-NetLib to %s:%s",
            host.c_str(), service.c_str());
    n64_netlib_connect();
}

wesnothd_connection::~wesnothd_connection() {
    n64_netlib_disconnect();
}

bool wesnothd_connection::receive_data(config& result) {
    // Check the NetLib receive ring buffer for incoming WML data
    N64NetlibEvent ev;
    if (n64_netlib_poll_event(ev)) {
        // Convert binary packet back to WML config
        // The server sends pre-formatted binary WML
        if (ev.type != 0 && ev.data_len > 0) {
            try {
                // Parse the packet payload as simplified WML
                std::string payload(reinterpret_cast<const char*>(ev.data), ev.data_len);
                read(result, payload);
                return true;
            } catch (...) {
                N64_LOG("wesnothd_connection: failed to parse incoming packet type %d", ev.type);
            }
        }
    }
    return false;
}

void wesnothd_connection::send_data(const config& request, const std::string& /*packet_type*/) {
    // Serialize config to binary and send via NetLib
    std::string data;
    write(data, request);

    if (data.size() <= N64_NETLIB_MAX_PAYLOAD) {
        n64_netlib_send_raw(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    } else {
        // Fragment large payloads
        size_t offset = 0;
        while (offset < data.size()) {
            size_t chunk = std::min(data.size() - offset, (size_t)N64_NETLIB_MAX_PAYLOAD);
            n64_netlib_send_raw(reinterpret_cast<const uint8_t*>(data.data() + offset), chunk);
            offset += chunk;
        }
    }
}

bool wesnothd_connection::has_data_received() const {
    return n64_netlib_has_data();
}

bool wesnothd_connection::is_sending_data() const {
    return false; // Sends are synchronous on N64
}

std::size_t wesnothd_connection::bytes_to_write() const {
    return 0;
}

void wesnothd_connection::cancel() {
    n64_netlib_disconnect();
}

void wesnothd_connection::stop() {
    n64_netlib_disconnect();
}

bool wesnothd_connection::running() const {
    return n64_netlib_is_connected();
}

// Handshake — N64-NetLib handles this at the transport layer
bool wesnothd_connection::handshake_finished() const {
    return n64_netlib_is_connected();
}

// Wait for data (blocking on N64 — poll with timeout)
bool wesnothd_connection::wait_for_data(std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    while (!has_data_received()) {
        n64_netlib_poll();
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= timeout) return false;
    }
    return true;
}

// Helper: simple WML serialization for network
void wesnothd_connection::write(std::string& out, const config& cfg) {
    // Minimal WML text serialization
    std::ostringstream ss;
    ::write(ss, cfg);
    out = ss.str();
}

void wesnothd_connection::read(config& cfg, const std::string& data) {
    std::istringstream ss(data);
    ::read(cfg, ss);
}

#endif // N64_BUILD
