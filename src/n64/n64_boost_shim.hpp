#pragma once

/**
 * n64_boost_shim.hpp
 *
 * Replaces Boost library components used by Wesnoth on N64.
 *
 * Boost is not available on N64. This header provides minimal
 * drop-in replacements for the specific Boost APIs Wesnoth uses.
 *
 * Included by the build system for N64 targets only.
 *
 * Components replaced:
 *   boost::filesystem       → n64_filesystem (ROM-based)
 *   boost::random           → C++11 <random> / custom
 *   boost::algorithm::string → std:: equivalents in <algorithm>/<cstring>
 *   boost::program_options  → stub (command line unused on N64)
 *   boost::iostreams        → custom ROM stream (n64_romfs)
 *   boost::regex            → simple string match helpers
 *   boost::scope_exit       → trivial RAII wrapper
 *   boost::graph            → standalone A* (in src/pathfind/)
 *   boost::locale           → stub (single locale on N64)
 *   boost::coroutine        → stub (single-threaded N64)
 *   boost::thread           → stub (no threads on N64)
 */

#ifdef N64_BUILD

// -----------------------------------------------------------------------
// boost/filesystem.hpp stub
// Only the Wesnoth paths/operations that matter on N64 are implemented.
// The rest return sensible no-ops or ROM equivalents.
// -----------------------------------------------------------------------
namespace boost {
namespace filesystem {

struct path {
    const char* p_;
    path() : p_("") {}
    path(const char* s) : p_(s) {}
    path(const std::string& s) : p_(s.c_str()) {}
    const char* c_str()          const { return p_; }
    std::string string()         const { return p_; }
    std::string generic_string() const { return p_; }
    path filename()    const { /* last path component */
        const char* sl = std::strrchr(p_, '/');
        return sl ? path(sl + 1) : path(p_);
    }
    path extension()   const { /* e.g. ".png" */
        const char* dot = std::strrchr(p_, '.');
        return dot ? path(dot) : path("");
    }
    path stem() const { /* filename without extension */
        // Not needed for N64 ROM path ops; stub
        return filename();
    }
    path parent_path() const { return path(""); }
    bool empty() const { return !p_ || !*p_; }
    bool operator==(const path& o) const { return std::strcmp(p_, o.p_) == 0; }
    bool operator!=(const path& o) const { return !(*this == o); }
    path operator/(const path& o)  const {
        // Not used at runtime on N64; stub
        return path(o.p_);
    }
};

// Filesystem operations — all stub; file I/O comes from ROM on N64
inline bool exists(const path&)                   { return false; }
inline bool is_directory(const path&)              { return false; }
inline bool is_regular_file(const path&)           { return false; }
inline bool create_directories(const path&)        { return false; }
inline bool remove(const path&)                    { return false; }
inline uintmax_t file_size(const path&)            { return 0; }

struct directory_iterator {
    directory_iterator()  {} // end iterator
    directory_iterator(const path&) {} // no real iteration on ROM
    bool operator==(const directory_iterator& o) const { return true; }
    bool operator!=(const directory_iterator& o) const { return false; }
};

} // filesystem
} // boost

// -----------------------------------------------------------------------
// boost/random.hpp
// Replaced by C++11 <random> — already available under std::
// Wesnoth uses boost::mt19937 and boost::uniform_int_distribution;
// provide aliases to std equivalents.
// -----------------------------------------------------------------------
#include <random>
namespace boost {
    using mt19937 = std::mt19937;
    template<typename T>
    using uniform_int_distribution = std::uniform_int_distribution<T>;
    using random_device = std::random_device;
}

// -----------------------------------------------------------------------
// boost/algorithm/string.hpp
// Only the functions actually called in the Wesnoth codebase are stubbed.
// -----------------------------------------------------------------------
#include <algorithm>
#include <cctype>
#include <cstring>
namespace boost {
namespace algorithm {
    inline void to_lower(std::string& s) {
        for (auto& c : s) c = std::tolower((unsigned char)c);
    }
    inline void to_upper(std::string& s) {
        for (auto& c : s) c = std::toupper((unsigned char)c);
    }
    inline std::string to_lower_copy(std::string s) { to_lower(s); return s; }
    inline std::string to_upper_copy(std::string s) { to_upper(s); return s; }
    inline void trim(std::string& s) {
        auto l = s.find_first_not_of(" \t\r\n");
        auto r = s.find_last_not_of(" \t\r\n");
        s = (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
    }
    inline std::string trim_copy(std::string s) { trim(s); return s; }
    inline bool starts_with(const std::string& s, const std::string& prefix) {
        return s.rfind(prefix, 0) == 0;
    }
    inline bool ends_with(const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    inline bool iequals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
                return false;
        return true;
    }
    inline bool contains(const std::string& s, const std::string& sub) {
        return s.find(sub) != std::string::npos;
    }
    template<typename R, typename S>
    inline std::string join(const R& range, const S& sep) {
        std::string out;
        bool first = true;
        for (const auto& x : range) {
            if (!first) out += sep;
            out += x;
            first = false;
        }
        return out;
    }
} // algorithm

// Pull algorithm into boost:: for code that uses boost::to_lower etc.
using algorithm::to_lower;
using algorithm::to_upper;
using algorithm::to_lower_copy;
using algorithm::to_upper_copy;
using algorithm::trim;
using algorithm::trim_copy;
using algorithm::starts_with;
using algorithm::ends_with;
using algorithm::iequals;
using algorithm::contains;
using algorithm::join;

} // boost

// -----------------------------------------------------------------------
// boost/program_options.hpp — stub (N64 has no command line)
// -----------------------------------------------------------------------
namespace boost {
namespace program_options {
struct options_description { options_description(const char*) {} };
struct variables_map {};
inline void store(int, variables_map&) {}
inline void notify(variables_map&) {}
} // program_options
} // boost

// -----------------------------------------------------------------------
// boost/scope_exit.hpp — RAII scope guard
// -----------------------------------------------------------------------
namespace boost {
template<typename F>
struct scope_exit_guard {
    F f_;
    bool active_;
    explicit scope_exit_guard(F f) : f_(f), active_(true) {}
    ~scope_exit_guard() { if (active_) f_(); }
    void release() { active_ = false; }
};
template<typename F>
inline scope_exit_guard<F> make_scope_exit(F f) { return scope_exit_guard<F>(f); }
} // boost

#define BOOST_SCOPE_EXIT(vars) \
    auto _scope_guard_ = boost::make_scope_exit([&]()
#define BOOST_SCOPE_EXIT_END );

// -----------------------------------------------------------------------
// boost/regex.hpp — stub; Wesnoth uses it for a few config string checks
// Replace with std::regex (available in C++11 <regex>)
// -----------------------------------------------------------------------
#include <regex>
namespace boost {
    using regex = std::regex;
    using smatch = std::smatch;
    using cmatch = std::cmatch;
    inline bool regex_match(const std::string& s, smatch& m, const regex& r) {
        return std::regex_match(s, m, r);
    }
    inline bool regex_search(const std::string& s, smatch& m, const regex& r) {
        return std::regex_search(s, m, r);
    }
    inline std::string regex_replace(const std::string& s, const regex& r,
                                     const std::string& fmt) {
        return std::regex_replace(s, r, fmt);
    }
} // boost

// -----------------------------------------------------------------------
// boost/locale.hpp — stub (English-only on N64)
// -----------------------------------------------------------------------
namespace boost {
namespace locale {
    inline std::string translate(const std::string& s) { return s; }
    inline std::string translate(const char* s) { return s ? s : ""; }

    // boost::locale::info — used by gettext.hpp
    class info : public std::locale::facet {
    public:
        static std::locale::id id;
        info() = default;
        std::string name() const { return "en_US.UTF-8"; }
        std::string language() const { return "en"; }
        std::string country() const { return "US"; }
        std::string variant() const { return ""; }
        std::string encoding() const { return "UTF-8"; }
        bool utf8() const { return true; }
    };

    // boost::locale::generator
    class generator {
    public:
        generator() = default;
        void add_messages_path(const std::string&) {}
        void add_messages_domain(const std::string&) {}
        std::locale generate(const std::string&) const { return std::locale(); }
        std::locale operator()(const std::string&) const { return std::locale(); }
    };
} // locale
} // boost

// -----------------------------------------------------------------------
// boost/thread.hpp — no threads on N64 (single-threaded cooperative)
// -----------------------------------------------------------------------
namespace boost {
struct mutex {
    void lock() {}
    void unlock() {}
};
struct lock_guard {
    explicit lock_guard(mutex&) {}
};
struct recursive_mutex : mutex {};
namespace this_thread {
    inline void yield() {}
    inline void sleep_for(int) {}
}
} // boost

// -----------------------------------------------------------------------
// boost/coroutine2 — no coroutines on N64
// Wesnoth uses coroutines for its asynchronous save I/O; on N64 saves
// are synchronous (written directly to cartridge SRAM/Flash).
// Stub the coroutine type as a no-op.
// -----------------------------------------------------------------------
namespace boost {
namespace coroutines2 {
template<typename T>
struct coroutine {
    struct pull_type {};
    struct push_type { void operator()(T) {} };
};
} // coroutines2
} // boost

#endif // N64_BUILD
