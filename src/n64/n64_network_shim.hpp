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
    using endpoint = struct endpoint_t {
        endpoint_t() = default;
        endpoint_t(address, unsigned short) {}
        address address() const { return ip::address(); }
        unsigned short port() const { return 0; }
    };

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

} // namespace asio

// -----------------------------------------------------------------------
// boost::system::error_code (used alongside asio)
// -----------------------------------------------------------------------
namespace system {
    using error_code = std::error_code;
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
