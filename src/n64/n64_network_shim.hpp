/**
 * n64_network_shim.hpp
 *
 * Replaces boost::asio and related networking headers for N64 builds.
 *
 * Wesnoth uses boost::asio for TCP networking (wesnothd_connection).
 * On N64, multiplayer goes through N64-NetLib (USB→PC→server).
 * This shim stubs out boost::asio so the code compiles, while the actual
 * multiplayer traffic is handled by netlib_adapter.hpp/cpp.
 */

#ifndef N64_NETWORK_SHIM_HPP
#define N64_NETWORK_SHIM_HPP

#ifdef N64_BUILD

// Block real boost::asio headers
#define BOOST_ASIO_HPP
#define BOOST_ASIO_IO_SERVICE_HPP
#define BOOST_ASIO_IO_CONTEXT_HPP
#define BOOST_ASIO_IP_TCP_HPP
#define BOOST_ASIO_SSL_HPP
#define BOOST_ASIO_STREAMBUF_HPP
#define BOOST_ASIO_READ_HPP
#define BOOST_ASIO_WRITE_HPP
#define BOOST_ASIO_CONNECT_HPP
#define BOOST_ASIO_STEADY_TIMER_HPP

#include <string>
#include <functional>
#include <system_error>

// -----------------------------------------------------------------------
// boost::asio stub types
// -----------------------------------------------------------------------
namespace boost {
namespace asio {

// Error code type
using error_code = std::error_code;

class io_context {
public:
    io_context() = default;
    std::size_t run() { return 0; }
    std::size_t run_one() { return 0; }
    std::size_t poll() { return 0; }
    std::size_t poll_one() { return 0; }
    void stop() {}
    void reset() {}
    void restart() {}
    bool stopped() const { return true; }

    class work {
    public:
        explicit work(io_context&) {}
    };

    using executor_type = io_context*;
    executor_type get_executor() { return this; }
};

// Legacy name
using io_service = io_context;

class mutable_buffer {
public:
    mutable_buffer() : data_(nullptr), size_(0) {}
    mutable_buffer(void* d, std::size_t s) : data_(d), size_(s) {}
    void* data() const { return data_; }
    std::size_t size() const { return size_; }
private:
    void* data_;
    std::size_t size_;
};

class const_buffer {
public:
    const_buffer() : data_(nullptr), size_(0) {}
    const_buffer(const void* d, std::size_t s) : data_(d), size_(s) {}
    const void* data() const { return data_; }
    std::size_t size() const { return size_; }
private:
    const void* data_;
    std::size_t size_;
};

inline mutable_buffer buffer(void* d, std::size_t s) { return mutable_buffer(d, s); }
inline const_buffer buffer(const void* d, std::size_t s) { return const_buffer(d, s); }
inline mutable_buffer buffer(std::string& s) { return mutable_buffer(s.data(), s.size()); }

class streambuf {
    std::string data_;
public:
    using const_buffers_type = const_buffer;
    using mutable_buffers_type = mutable_buffer;

    mutable_buffers_type prepare(std::size_t n) {
        data_.resize(n);
        return mutable_buffer(data_.data(), n);
    }
    void commit(std::size_t) {}
    void consume(std::size_t n) { if (n < data_.size()) data_.erase(0, n); else data_.clear(); }
    std::size_t size() const { return data_.size(); }
    const_buffers_type data() const { return const_buffer(data_.data(), data_.size()); }
};

namespace ip {

class address {
    std::string addr_;
public:
    address() = default;
    static address from_string(const std::string& s) { address a; a.addr_ = s; return a; }
    std::string to_string() const { return addr_; }
    bool is_v4() const { return true; }
    bool is_v6() const { return false; }
};

class tcp {
public:
    struct endpoint_t {
        endpoint_t() = default;
        endpoint_t(ip::address, unsigned short) {}
        ip::address address() const { return ip::address(); }
        unsigned short port() const { return 0; }
    };
    using endpoint = endpoint_t;

    using socket = struct socket_t {
        socket_t() = default;
        explicit socket_t(io_context&) {}
        bool is_open() const { return false; }
        void close() {}
        void close(error_code&) {}
        void shutdown(int) {}
        void cancel() {}
        endpoint_t remote_endpoint() const { return {}; }
        endpoint_t local_endpoint() const { return {}; }
        template<typename B, typename H>
        void async_read_some(const B&, H&&) {}
        template<typename B, typename H>
        void async_write_some(const B&, H&&) {}
    };

    using resolver = struct resolver_t {
        using results_type = struct {
            struct iterator {
                endpoint_t ep;
                endpoint_t operator*() const { return ep; }
                bool operator!=(const iterator&) const { return false; }
                iterator& operator++() { return *this; }
            };
            iterator begin() const { return {}; }
            iterator end() const { return {}; }
            bool empty() const { return true; }
        };

        explicit resolver_t(io_context&) {}
        results_type resolve(const std::string&, const std::string&) { return {}; }
        results_type resolve(const std::string&, const std::string&, error_code&) { return {}; }
        template<typename H>
        void async_resolve(const std::string&, const std::string&, H&&) {}
    };

    using acceptor = struct acceptor_t {
        acceptor_t() = default;
        explicit acceptor_t(io_context&) {}
        acceptor_t(io_context&, const endpoint_t&) {}
        void open(tcp) {}
        void bind(const endpoint_t&) {}
        void listen() {}
        void close() {}
        template<typename H>
        void async_accept(socket_t&, H&&) {}
    };

    static tcp v4() { return {}; }
    static tcp v6() { return {}; }
};

} // namespace ip

// Async read/write free functions
template<typename S, typename B, typename H>
void async_read(S&, B&, H&&) {}

template<typename S, typename B, typename H>
void async_write(S&, const B&, H&&) {}

template<typename S, typename B, typename H>
void async_read_until(S&, B&, const std::string&, H&&) {}

// Steady timer stub
class steady_timer {
public:
    steady_timer(io_context&) {}
    void expires_after(std::chrono::steady_clock::duration) {}
    void cancel() {}
    template<typename H>
    void async_wait(H&&) {}
};

// -----------------------------------------------------------------------
// boost::asio::ssl stub types
// -----------------------------------------------------------------------
namespace ssl {

// Verify mode flags
enum verify_mode : int {
    verify_none                 = 0,
    verify_peer                 = 1,
    verify_fail_if_no_peer_cert = 2,
    verify_client_once          = 4
};

inline verify_mode operator|(verify_mode a, verify_mode b) {
    return static_cast<verify_mode>(static_cast<int>(a) | static_cast<int>(b));
}

// Stub verify_context (wraps X509_STORE_CTX* in real Boost)
class verify_context {
public:
    verify_context() = default;
    void* native_handle() { return nullptr; }
};

class context {
public:
    enum method {
        sslv23,
        sslv23_client,
        sslv23_server,
        tlsv12,
        tlsv12_client,
        tlsv12_server,
        tlsv13,
        tlsv13_client,
        tls,
        tls_client,
        tls_server
    };

    enum file_format { pem, asn1 };

    // Option flags
    enum options : long {
        default_workarounds = 0x1,
        no_sslv2            = 0x2,
        no_sslv3            = 0x4,
        single_dh_use       = 0x8
    };

    explicit context(method) {}
    context(int) {}

    void set_options(long) {}
    void set_default_verify_paths() {}
    void set_verify_mode(int) {}
    void add_certificate_authority(const const_buffer&) {}
    void use_certificate_chain_file(const std::string&) {}
    void use_private_key_file(const std::string&, file_format) {}
    void use_tmp_dh_file(const std::string&) {}
};

inline long operator|(context::options a, context::options b) {
    return static_cast<long>(a) | static_cast<long>(b);
}
inline long operator|(long a, context::options b) {
    return a | static_cast<long>(b);
}

class stream_base {
public:
    enum handshake_type { client, server };
};

template<typename Stream>
class stream : public stream_base {
    Stream next_layer_;
    context& ctx_;
public:
    using next_layer_type = Stream;
    using lowest_layer_type = Stream;

    stream(Stream s, context& c) : next_layer_(std::move(s)), ctx_(c) {}

    // For io_context-based construction
    stream(io_context& ioc, context& c) : next_layer_(ioc), ctx_(c) {}

    next_layer_type& next_layer() { return next_layer_; }
    lowest_layer_type& lowest_layer() { return next_layer_; }
    const lowest_layer_type& lowest_layer() const { return next_layer_; }

    void set_verify_mode(int) {}

    template<typename VerifyCallback>
    void set_verify_callback(VerifyCallback&&) {}

    template<typename HandshakeHandler>
    void async_handshake(handshake_type, HandshakeHandler&&) {}

    void handshake(handshake_type) {}
    void handshake(handshake_type, error_code&) {}

    template<typename B, typename H>
    void async_read_some(const B&, H&&) {}
    template<typename B, typename H>
    void async_write_some(const B&, H&&) {}

    void shutdown() {}
    void shutdown(error_code&) {}
};

// Verification functors
struct host_name_verification {
    explicit host_name_verification(const std::string&) {}
    bool operator()(bool, verify_context&) const { return true; }
};

// Legacy name
using rfc2818_verification = host_name_verification;

} // namespace ssl

namespace error {
    inline const std::error_code operation_aborted = std::make_error_code(std::errc::operation_canceled);
    inline const std::error_code eof = std::make_error_code(std::errc::broken_pipe);
} // namespace error

} // namespace asio

// -----------------------------------------------------------------------
// boost::system::error_code (used alongside asio)
// -----------------------------------------------------------------------
namespace system {
#ifndef N64_BOOST_SYSTEM_ERROR_CODE_DEFINED
#define N64_BOOST_SYSTEM_ERROR_CODE_DEFINED
    using error_code = std::error_code;
#endif
    using error_category = std::error_category;
    class system_error : public std::system_error {
    public:
        using std::system_error::system_error;
    };
}

} // namespace boost

// -----------------------------------------------------------------------
// Block <sys/stat.h> if not available — provide minimal stubs
// -----------------------------------------------------------------------
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <ctime>

struct stat {
    unsigned st_mode;
    unsigned st_size;
    time_t   st_mtime;
};

#define S_ISDIR(m) (0)
#define S_ISREG(m) (1)
#define S_IRWXU 0700
#define S_IRWXG 0070
#define S_IRWXO 0007

inline int stat(const char*, struct stat*) { return -1; }
inline int mkdir(const char*, unsigned) { return -1; }

#endif // _SYS_STAT_H

#endif // N64_BUILD
#endif // N64_NETWORK_SHIM_HPP
