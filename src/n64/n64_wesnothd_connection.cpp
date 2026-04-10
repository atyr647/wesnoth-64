/**
 * n64_wesnothd_connection.cpp
 *
 * N64 stub for src/wesnothd_connection.cpp.
 * All networking is disabled — multiplayer goes through N64-NetLib if available.
 * This provides the wesnothd_connection class method implementations as no-ops.
 */

#ifdef N64_BUILD

#include "../wesnothd_connection.hpp"
#include "n64_platform.hpp"

// Constructor — initialize all members to safe defaults
wesnothd_connection::wesnothd_connection(const std::string& host, const std::string& service)
    : worker_thread_()
    , io_context_()
    , resolver_(io_context_)
    , tls_context_(boost::asio::ssl::context::tlsv12_client)
    , host_(host)
    , service_(service)
    , use_tls_(false)
    , socket_(raw_socket{})
    , last_error_()
    , handshake_finished_()
    , read_buf_()
    , handshake_response_(0)
    , payload_size_(0)
    , bytes_to_write_(0)
    , bytes_written_(0)
    , bytes_to_read_(0)
    , bytes_read_(0)
{
    N64_LOG("wesnothd_connection: stub (no network on N64) host=%s", host.c_str());
}

wesnothd_connection::~wesnothd_connection() {
}

void wesnothd_connection::send_data(const configr_of& /*request*/) {
    // No-op on N64
}

bool wesnothd_connection::receive_data(config& /*result*/) {
    return false;
}

bool wesnothd_connection::wait_and_receive_data(config& /*data*/) {
    return false;
}

void wesnothd_connection::wait_for_handshake() {
    // No-op
}

void wesnothd_connection::cancel() {
}

void wesnothd_connection::stop() {
}

// Private methods — all stubs
void wesnothd_connection::handle_resolve(const boost::system::error_code&, const results_type&) {}
void wesnothd_connection::handle_connect(const boost::system::error_code&, endpoint) {}
void wesnothd_connection::handshake() {}
void wesnothd_connection::handle_handshake(const boost::system::error_code&) {}
void wesnothd_connection::fallback_to_unencrypted() {}

std::size_t wesnothd_connection::is_write_complete(const boost::system::error_code&, std::size_t) {
    return 0;
}

void wesnothd_connection::handle_write(const boost::system::error_code&, std::size_t) {}

std::size_t wesnothd_connection::is_read_complete(const boost::system::error_code&, std::size_t) {
    return 0;
}

void wesnothd_connection::handle_read(const boost::system::error_code&, std::size_t) {}

void wesnothd_connection::send() {}
void wesnothd_connection::recv() {}
void wesnothd_connection::set_keepalive(int) {}

#endif // N64_BUILD
