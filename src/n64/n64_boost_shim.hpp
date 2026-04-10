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

#include <string>
#include <cstring>
#include <vector>
#include <type_traits>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <iterator>
#include <climits>
#include <any>
#include <functional>

// -----------------------------------------------------------------------
// boost/system/error_code.hpp stub
// Uses std::error_code (matches n64_network_shim.hpp definition)
// -----------------------------------------------------------------------
#include <system_error>
namespace boost {
namespace system {
#ifndef N64_BOOST_SYSTEM_ERROR_CODE_DEFINED
#define N64_BOOST_SYSTEM_ERROR_CODE_DEFINED
    using error_code = std::error_code;
#endif
} // system
} // boost

// -----------------------------------------------------------------------
// boost/filesystem.hpp stub
// Only the Wesnoth paths/operations that matter on N64 are implemented.
// The rest return sensible no-ops or ROM equivalents.
// -----------------------------------------------------------------------
namespace boost {
namespace filesystem {

struct path {
    std::string p_;
    path() {}
    path(const char* s) : p_(s ? s : "") {}
    path(const std::string& s) : p_(s) {}
    path(std::string&& s) : p_(std::move(s)) {}
    const char* c_str()          const { return p_.c_str(); }
    const std::string& string()  const { return p_; }
    const std::string& generic_string() const { return p_; }
    path filename() const {
        auto pos = p_.rfind('/');
        return pos != std::string::npos ? path(p_.substr(pos + 1)) : *this;
    }
    path extension() const {
        auto fn = filename().string();
        auto pos = fn.rfind('.');
        return pos != std::string::npos ? path(fn.substr(pos)) : path("");
    }
    path stem() const {
        auto fn = filename().string();
        auto pos = fn.rfind('.');
        return pos != std::string::npos ? path(fn.substr(0, pos)) : path(fn);
    }
    path parent_path() const {
        auto pos = p_.rfind('/');
        return pos != std::string::npos ? path(p_.substr(0, pos)) : path("");
    }
    bool empty() const { return p_.empty(); }
    bool operator==(const path& o) const { return p_ == o.p_; }
    bool operator!=(const path& o) const { return p_ != o.p_; }
    bool operator<(const path& o) const { return p_ < o.p_; }
    path operator/(const path& o) const {
        std::string combined = p_;
        if (!combined.empty() && combined.back() != '/') combined += '/';
        combined += o.p_;
        return path(std::move(combined));
    }
    path& operator/=(const path& o) {
        if (!p_.empty() && p_.back() != '/') p_ += '/';
        p_ += o.p_;
        return *this;
    }
    // Allow implicit conversion to string for compatibility
    operator const std::string&() const { return p_; }
    friend bool operator==(const std::string& s, const path& p) { return s == p.p_; }
    friend bool operator==(const path& p, const std::string& s) { return p.p_ == s; }
    friend bool operator!=(const std::string& s, const path& p) { return s != p.p_; }
    friend bool operator!=(const path& p, const std::string& s) { return p.p_ != s; }
};

// Filesystem operations — all stub; file I/O comes from ROM on N64
inline bool exists(const path&)                   { return false; }
inline bool is_directory(const path&)              { return false; }
inline bool is_directory(const path&, boost::system::error_code&) { return false; }
inline bool is_regular_file(const path&)           { return false; }
inline bool is_empty(const path&)                  { return true; }
inline bool is_empty(const path&, boost::system::error_code&) { return true; }
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
// boost::math::constants
// -----------------------------------------------------------------------
namespace boost { namespace math { namespace constants {
    template<typename T> inline constexpr T pi() { return T(3.14159265358979323846); }
}}}

// -----------------------------------------------------------------------
// boost::hash_combine
// -----------------------------------------------------------------------
namespace boost {
    template<typename T>
    inline void hash_combine(std::size_t& seed, const T& v) {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
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
    inline std::string to_lower_copy(std::string s, const std::locale&) { to_lower(s); return s; }
    inline std::string to_upper_copy(std::string s) { to_upper(s); return s; }
    inline std::string to_upper_copy(std::string s, const std::locale&) { to_upper(s); return s; }
    inline void trim(std::string& s) {
        auto l = s.find_first_not_of(" \t\r\n");
        auto r = s.find_last_not_of(" \t\r\n");
        s = (l == std::string::npos) ? "" : s.substr(l, r - l + 1);
    }
    inline std::string trim_copy(std::string s) { trim(s); return s; }
    inline void trim_right(std::string& s) {
        auto r = s.find_last_not_of(" \t\r\n");
        s = (r == std::string::npos) ? "" : s.substr(0, r + 1);
    }
    inline std::string trim_right_copy(std::string s) { trim_right(s); return s; }
    inline bool starts_with(const std::string& s, const std::string& prefix) {
        return s.rfind(prefix, 0) == 0;
    }
    inline bool ends_with(const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    // Range-based ends_with with a comparator (used in wildcard matching)
    template<typename Range1, typename Range2, typename Pred>
    inline bool ends_with(const Range1& s, const Range2& p, Pred comp) {
        auto s_len = std::distance(s.first, s.second);
        auto p_len = std::distance(p.first, p.second);
        if (p_len > s_len) return false;
        auto s_it = s.second;
        auto p_it = p.second;
        for (auto i = decltype(p_len){0}; i < p_len; ++i) {
            --s_it; --p_it;
            if (!comp(*s_it, *p_it)) return false;
        }
        return true;
    }
    inline bool istarts_with(const std::string& s, const std::string& prefix) {
        if (prefix.size() > s.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i])) return false;
        return true;
    }
    inline bool iends_with(const std::string& s, const std::string& suffix) {
        if (suffix.size() > s.size()) return false;
        size_t off = s.size() - suffix.size();
        for (size_t i = 0; i < suffix.size(); ++i)
            if (std::tolower((unsigned char)s[off+i]) != std::tolower((unsigned char)suffix[i])) return false;
        return true;
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
    inline std::string erase_first_copy(const std::string& s, const std::string& what) {
        auto pos = s.find(what);
        if (pos == std::string::npos) return s;
        std::string result = s;
        result.erase(pos, what.size());
        return result;
    }
    inline void erase_all(std::string& s, const std::string& what) {
        std::size_t pos = 0;
        while ((pos = s.find(what, pos)) != std::string::npos) {
            s.erase(pos, what.size());
        }
    }
    inline void replace_all(std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        std::string::size_type pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    inline std::string replace_all_copy(std::string s, const std::string& from, const std::string& to) {
        replace_all(s, from, to);
        return s;
    }
} // algorithm

// Pull algorithm into boost:: for code that uses boost::to_lower etc.
using algorithm::to_lower;
using algorithm::to_upper;
using algorithm::to_lower_copy;
using algorithm::to_upper_copy;
using algorithm::trim;
using algorithm::trim_copy;
using algorithm::trim_right;
using algorithm::trim_right_copy;
using algorithm::starts_with;
using algorithm::ends_with;
using algorithm::iequals;
using algorithm::contains;
using algorithm::join;
using algorithm::erase_all;
using algorithm::replace_all;
using algorithm::replace_all_copy;

} // boost

// -----------------------------------------------------------------------
// boost::any — alias to std::any (used by program_options validate())
// -----------------------------------------------------------------------
namespace boost {
    using any = std::any;

    template<typename T>
    T any_cast(const any& a) { return std::any_cast<T>(a); }

    template<typename T>
    T any_cast(any& a) { return std::any_cast<T>(a); }

    template<typename T>
    T any_cast(any&& a) { return std::any_cast<T>(std::move(a)); }

    template<typename T>
    const T* any_cast(const any* a) noexcept { return std::any_cast<T>(a); }

    template<typename T>
    T* any_cast(any* a) noexcept { return std::any_cast<T>(a); }
} // boost

// -----------------------------------------------------------------------
// boost/program_options.hpp — comprehensive stub (N64 has no command line)
// All parsing is no-op; the goal is compile-compatibility with Wesnoth's
// usage of boost::program_options in commandline_options.cpp.
// -----------------------------------------------------------------------
namespace boost {
namespace program_options {

// --- variable_value: type-erased holder returned by variables_map[] ---
class variable_value {
    std::any storage_;
    bool defaulted_ = true;
    bool empty_ = true;
public:
    variable_value() = default;
    template<typename T>
    explicit variable_value(const T& val) : storage_(val), defaulted_(false), empty_(false) {}
    template<typename T>
    const T& as() const { return std::any_cast<const T&>(storage_); }
    bool empty() const { return empty_; }
    bool defaulted() const { return defaulted_; }
};

// --- typed_value<T>: returned by value<T>(), supports chaining ---
template<typename T>
class typed_value {
    T default_val_{};
    bool has_default_ = false;
    bool has_implicit_ = false;
    bool multi_ = false;
    bool composing_ = false;
public:
    typed_value() = default;
    typed_value* default_value(const T&) { has_default_ = true; return this; }
    typed_value* implicit_value(const T&) { has_implicit_ = true; return this; }
    typed_value* multitoken() { multi_ = true; return this; }
    typed_value* composing() { composing_ = true; return this; }
    typed_value* value_name(const char*) { return this; }
    typed_value* value_name(const std::string&) { return this; }
};

template<typename T>
typed_value<T>* value() { static typed_value<T> v; return &v; }

template<typename T>
typed_value<T>* value(T* store_to) { (void)store_to; static typed_value<T> v; return &v; }

// --- options_adder: the object returned by add_options(), supports operator() chaining ---
class options_description; // forward

class options_adder {
public:
    options_adder() = default;
    // ("name")
    options_adder& operator()(const char*) { return *this; }
    // ("name", "description")
    options_adder& operator()(const char*, const char*) { return *this; }
    // ("name", value_semantic*, "description")
    template<typename T>
    options_adder& operator()(const char*, typed_value<T>*, const char* = nullptr) { return *this; }
};

// --- options_description ---
class options_description {
public:
    options_description() = default;
    options_description(const char*) {}
    options_description(const std::string&) {}
    options_description(const char*, unsigned) {}
    options_adder& add_options() { return adder_; }
    options_description& add(const options_description&) { return *this; }
    friend std::ostream& operator<<(std::ostream& os, const options_description&) { return os; }
private:
    options_adder adder_;
};

// --- variables_map ---
class variables_map : public std::map<std::string, variable_value> {
public:
    // count() is inherited from std::map.
    // operator[] is inherited from std::map and returns variable_value.
};

// --- parsed_options: result of command_line_parser::run() ---
struct basic_option {
    std::string string_key;
    std::vector<std::string> value;
    bool unregistered = false;
};

struct parsed_options {
    std::vector<basic_option> options;
    const options_description* description = nullptr;
};

// --- positional_options_description ---
class positional_options_description {
public:
    positional_options_description& add(const char*, int) { return *this; }
};

// --- command_line_style ---
namespace command_line_style {
    enum style_t {
        allow_short          = 1 << 0,
        short_allow_adjacent = 1 << 1,
        short_allow_next     = 1 << 2,
        allow_long           = 1 << 3,
        long_allow_adjacent  = 1 << 4,
        long_allow_next      = 1 << 5,
        allow_sticky         = 1 << 6,
        allow_guessing       = 1 << 7,
        allow_dash_for_short = 1 << 8,
        long_allow_equal     = 1 << 9,
        short_allow_equal    = 1 << 10,
        case_insensitive     = 1 << 11,
        allow_long_disguise  = 1 << 12,
        default_style        = allow_short | short_allow_adjacent | short_allow_next
                             | allow_long | long_allow_adjacent | long_allow_next
                             | long_allow_equal | allow_guessing | allow_dash_for_short
    };
}

// --- command_line_parser ---
class command_line_parser {
public:
    command_line_parser(int, const char* const*) {}
    command_line_parser(const std::vector<std::string>&) {}
    command_line_parser& options(const options_description&) { return *this; }
    command_line_parser& positional(const positional_options_description&) { return *this; }
    command_line_parser& allow_unregistered() { return *this; }
    command_line_parser& style(int) { return *this; }
    command_line_parser& extra_parser(std::function<std::pair<std::string,std::string>(const std::string&)>) { return *this; }
    parsed_options run() const { return parsed_options{}; }
};

// --- free functions ---
inline void store(const parsed_options&, variables_map&) {}
inline void notify(variables_map&) {}

// --- validation_error ---
class validation_error : public std::exception {
public:
    enum kind_t {
        invalid_option_value,
        multiple_values_not_allowed,
        at_least_one_value_required,
        invalid_bool_value,
        invalid_option,
        missing_value
    };
    validation_error(kind_t) {}
    validation_error(kind_t, const std::string&, const std::string& = {}) {}
    const char* what() const noexcept override { return "validation_error"; }
};

// --- error (base exception) ---
class error : public std::exception {
    std::string msg_;
public:
    error() : msg_("program_options error") {}
    explicit error(const std::string& m) : msg_(m) {}
    const char* what() const noexcept override { return msg_.c_str(); }
};

} // namespace program_options
} // namespace boost

// -----------------------------------------------------------------------
// boost/iostreams — minimal stub (gzip not used on N64)
// config_cache.cpp catches boost::iostreams::gzip_error, so it must exist.
// -----------------------------------------------------------------------
namespace boost {
namespace iostreams {
    class gzip_error : public std::exception {
    public:
        explicit gzip_error(int code = 0) : code_(code) {}
        int error() const { return code_; }
        const char* what() const noexcept override { return "gzip_error"; }
    private:
        int code_;
    };
} // iostreams
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
    using sregex_token_iterator = std::sregex_token_iterator;
    using ssub_match = std::ssub_match;
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

// -----------------------------------------------------------------------
// boost::iterator_facade — CRTP base for custom iterators
// -----------------------------------------------------------------------
namespace boost {

// iterator_core_access is friended by derived iterator classes so that
// iterator_facade (and this helper) can call their private dereference(),
// increment(), decrement(), and equal() methods.
struct iterator_core_access {
    template<typename It>
    static void increment(It& it) { it.increment(); }
    template<typename It>
    static void decrement(It& it) { it.decrement(); }
    template<typename It1, typename It2>
    static bool equal(const It1& a, const It2& b) { return a.equal(b); }
    template<typename It>
    static decltype(auto) dereference(const It& it) { return it.dereference(); }
};

template<typename Derived, typename Value, typename CategoryOrTraversal,
         typename Reference = Value&, typename Difference = std::ptrdiff_t>
class iterator_facade {
    // Allow iterator_core_access to reach into the derived class on our behalf
    friend struct iterator_core_access;
public:
    using value_type = Value;
    using reference = Reference;
    using pointer = typename std::add_pointer<Value>::type;
    using difference_type = Difference;
    using iterator_category = CategoryOrTraversal;

    Derived& derived() { return static_cast<Derived&>(*this); }
    const Derived& derived() const { return static_cast<const Derived&>(*this); }

    // Route through iterator_core_access so that private methods in derived
    // classes are accessible (derived classes friend iterator_core_access).
    reference operator*() const {
        return iterator_core_access::dereference(derived());
    }
    pointer operator->() const {
        return &iterator_core_access::dereference(derived());
    }

    Derived& operator++() {
        iterator_core_access::increment(derived());
        return derived();
    }
    Derived operator++(int) {
        Derived tmp = derived();
        iterator_core_access::increment(derived());
        return tmp;
    }

    friend bool operator==(const Derived& a, const Derived& b) {
        return iterator_core_access::equal(a, b);
    }
    friend bool operator!=(const Derived& a, const Derived& b) {
        return !iterator_core_access::equal(a, b);
    }
};
} // boost

// -----------------------------------------------------------------------
// boost::adjacency_list — minimal stub for schema_validator / graph usage
// -----------------------------------------------------------------------
namespace boost {
struct vecS {};
struct directedS {};
struct undirectedS {};
struct bidirectionalS {};

template<typename OEL = vecS, typename VL = vecS, typename D = directedS,
         typename VP = int, typename EP = int, typename GP = int, typename EL = vecS>
class adjacency_list {
public:
    using vertex_descriptor = unsigned int;
    using edge_descriptor = std::pair<unsigned int, unsigned int>;
    using vertex_property_type = VP;
    using edge_property_type = EP;

    adjacency_list() = default;

    // Add vertex without property
    vertex_descriptor add_vertex() {
        vertex_props_.push_back(VP());
        adj_.push_back({});
        return vertices_++;
    }

    // Add vertex with property
    vertex_descriptor add_vertex(const VP& vp) {
        vertex_props_.push_back(vp);
        adj_.push_back({});
        return vertices_++;
    }

    // Add edge without property
    std::pair<edge_descriptor, bool> add_edge(vertex_descriptor u, vertex_descriptor v) {
        edge_descriptor ed{u, v};
        edges_.push_back(ed);
        edge_props_.push_back(EP());
        adj_[u].push_back(edges_.size() - 1);
        return {ed, true};
    }

    // Add edge with property
    std::pair<edge_descriptor, bool> add_edge(vertex_descriptor u, vertex_descriptor v, const EP& ep) {
        edge_descriptor ed{u, v};
        edges_.push_back(ed);
        edge_props_.push_back(ep);
        adj_[u].push_back(edges_.size() - 1);
        return {ed, true};
    }

    std::size_t num_vertices() const { return vertices_; }
    std::size_t num_edges() const { return edges_.size(); }

    // operator[] for edge property access by edge_descriptor
    EP& operator[](const edge_descriptor& ed) {
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            if (edges_[i] == ed) return edge_props_[i];
        }
        // Fallback: should not happen in correct usage — search by components
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            if (edges_[i].first == ed.first && edges_[i].second == ed.second)
                return edge_props_[i];
        }
        // Truly not found — undefined behavior territory, return first
        return edge_props_[0];
    }
    const EP& operator[](const edge_descriptor& ed) const {
        for (std::size_t i = 0; i < edges_.size(); ++i) {
            if (edges_[i] == ed) return edge_props_[i];
        }
        return edge_props_[0]; // should not reach here
    }

    // Vertex property access by vertex_descriptor
    VP& operator[](vertex_descriptor v) { return vertex_props_[v]; }
    const VP& operator[](vertex_descriptor v) const { return vertex_props_[v]; }

    // Adjacency data for DFS
    const std::vector<edge_descriptor>& edges() const { return edges_; }
    const std::vector<std::vector<std::size_t>>& adjacency() const { return adj_; }

private:
    unsigned int vertices_ = 0;
    std::vector<VP> vertex_props_;
    std::vector<edge_descriptor> edges_;
    std::vector<EP> edge_props_;
    std::vector<std::vector<std::size_t>> adj_; // adj_[v] = list of edge indices
};

// Free function: add_vertex without property
template<typename G>
typename G::vertex_descriptor add_vertex(G& g) { return g.add_vertex(); }

// Free function: add_vertex with property
template<typename OEL, typename VL, typename D, typename VP, typename EP, typename GP, typename EL>
typename adjacency_list<OEL,VL,D,VP,EP,GP,EL>::vertex_descriptor
add_vertex(const VP& vp, adjacency_list<OEL,VL,D,VP,EP,GP,EL>& g) {
    return g.add_vertex(vp);
}

// Free function: add_edge without property
template<typename G>
std::pair<typename G::edge_descriptor, bool> add_edge(
    typename G::vertex_descriptor u, typename G::vertex_descriptor v, G& g) {
    return g.add_edge(u, v);
}

// Free function: add_edge with property
template<typename OEL, typename VL, typename D, typename VP, typename EP, typename GP, typename EL>
std::pair<typename adjacency_list<OEL,VL,D,VP,EP,GP,EL>::edge_descriptor, bool>
add_edge(typename adjacency_list<OEL,VL,D,VP,EP,GP,EL>::vertex_descriptor u,
         typename adjacency_list<OEL,VL,D,VP,EP,GP,EL>::vertex_descriptor v,
         const EP& ep,
         adjacency_list<OEL,VL,D,VP,EP,GP,EL>& g) {
    return g.add_edge(u, v, ep);
}

// Free function: source / target of an edge
template<typename G>
typename G::vertex_descriptor source(const typename G::edge_descriptor& ed, const G&) {
    return ed.first;
}
template<typename G>
typename G::vertex_descriptor target(const typename G::edge_descriptor& ed, const G&) {
    return ed.second;
}

// -----------------------------------------------------------------------
// boost DFS support
// -----------------------------------------------------------------------

// Default DFS visitor — base class with no-op hooks
struct default_dfs_visitor {
    template<typename V, typename G> void initialize_vertex(V, const G&) {}
    template<typename V, typename G> void start_vertex(V, const G&) {}
    template<typename V, typename G> void discover_vertex(V, const G&) {}
    template<typename V, typename G> void finish_vertex(V, const G&) {}
    template<typename E, typename G> void examine_edge(E, const G&) {}
    template<typename E, typename G> void tree_edge(E, const G&) {}
    template<typename E, typename G> void back_edge(E, const G&) {}
    template<typename E, typename G> void forward_or_cross_edge(E, const G&) {}
};

// dfs_visitor template — inheritable DFS visitor (same as default_dfs_visitor)
template<typename... EventVisitors>
struct dfs_visitor : public default_dfs_visitor {};

// Visitor wrapper — returned by boost::visitor(v)
template<typename Vis>
struct dfs_visitor_wrapper {
    Vis vis;
    explicit dfs_visitor_wrapper(Vis v) : vis(v) {}
};

template<typename Vis>
dfs_visitor_wrapper<Vis> visitor(Vis v) { return dfs_visitor_wrapper<Vis>(v); }

// Minimal DFS implementation
namespace detail {
    template<typename G, typename Vis>
    void dfs_visit(const G& g, typename G::vertex_descriptor u,
                   std::vector<int>& color, Vis& vis) {
        // color: 0=white, 1=gray, 2=black
        color[u] = 1;
        vis.discover_vertex(u, g);
        const auto& adj = g.adjacency();
        const auto& edges = g.edges();
        if (u < adj.size()) {
            for (auto ei : adj[u]) {
                auto v = edges[ei].second;
                vis.examine_edge(edges[ei], g);
                if (color[v] == 0) {
                    vis.tree_edge(edges[ei], g);
                    dfs_visit(g, v, color, vis);
                } else if (color[v] == 1) {
                    vis.back_edge(edges[ei], g);
                } else {
                    vis.forward_or_cross_edge(edges[ei], g);
                }
            }
        }
        color[u] = 2;
        vis.finish_vertex(u, g);
    }
} // detail

// depth_first_search with visitor wrapper
template<typename G, typename Vis>
void depth_first_search(const G& g, dfs_visitor_wrapper<Vis> vw) {
    auto n = g.num_vertices();
    if (n == 0) return;
    std::vector<int> color(n, 0);
    for (unsigned int u = 0; u < n; ++u) {
        vw.vis.initialize_vertex(u, g);
    }
    for (unsigned int u = 0; u < n; ++u) {
        if (color[u] == 0) {
            vw.vis.start_vertex(u, g);
            detail::dfs_visit(g, u, color, vw.vis);
        }
    }
}

} // boost

// -----------------------------------------------------------------------
// boost::iterator_range — lightweight range from iterator pair
// -----------------------------------------------------------------------
namespace boost {
template<typename Iterator>
class iterator_range {
public:
    using iterator = Iterator;
    using const_iterator = Iterator;
private:
    Iterator begin_;
    Iterator end_;
public:
    iterator_range() : begin_(), end_() {}
    iterator_range(Iterator b, Iterator e) : begin_(b), end_(e) {}
    template<typename OtherIter>
    iterator_range(const iterator_range<OtherIter>& o) : begin_(o.begin()), end_(o.end()) {}
    Iterator begin() const { return begin_; }
    Iterator end()   const { return end_; }
    bool empty()     const { return begin_ == end_; }
    explicit operator bool() const { return !empty(); }
    std::size_t size() const { return std::distance(begin_, end_); }

    // Random access via operator[] (for code that indexes into ranges)
    auto operator[](std::size_t n) const -> decltype(*begin_) {
        auto it = begin_;
        std::advance(it, n);
        return *it;
    }
    auto operator[](std::size_t n) -> decltype(*begin_) {
        auto it = begin_;
        std::advance(it, n);
        return *it;
    }

    // front/back
    auto front() const -> decltype(*begin_) { return *begin_; }
    auto back() const -> decltype(*begin_) { auto it = end_; --it; return *it; }
    void pop_front() { ++begin_; }
    void advance_begin(std::size_t n) { std::advance(begin_, n); }
    void advance_end(std::size_t n) { std::advance(end_, n); }
};
template<typename Iterator>
inline iterator_range<Iterator> make_iterator_range(Iterator b, Iterator e) {
    return iterator_range<Iterator>(b, e);
}
} // boost

// -----------------------------------------------------------------------
// boost::ptr_vector — simple owning pointer vector
// -----------------------------------------------------------------------
namespace boost {
template<typename T>
class ptr_vector {
    std::vector<T*> v_;
public:
    ptr_vector() = default;
    ~ptr_vector() { clear(); }
    ptr_vector(const ptr_vector& o) { for (auto* p : o.v_) v_.push_back(new T(*p)); }
    ptr_vector& operator=(const ptr_vector& o) {
        if (this != &o) { clear(); for (auto* p : o.v_) v_.push_back(new T(*p)); }
        return *this;
    }
    ptr_vector(ptr_vector&& o) noexcept : v_(std::move(o.v_)) { o.v_.clear(); }
    ptr_vector& operator=(ptr_vector&& o) noexcept {
        if (this != &o) { clear(); v_ = std::move(o.v_); o.v_.clear(); }
        return *this;
    }
    void push_back(T* p) { v_.push_back(p); }
    T& operator[](std::size_t i) { return *v_[i]; }
    const T& operator[](std::size_t i) const { return *v_[i]; }
    T& front() { return *v_.front(); }
    T& back()  { return *v_.back(); }
    std::size_t size() const { return v_.size(); }
    bool empty() const { return v_.empty(); }
    void clear() { for (auto* p : v_) delete p; v_.clear(); }

    void swap(ptr_vector& o) noexcept { v_.swap(o.v_); }

    void resize(std::size_t n) {
        while (v_.size() > n) { delete v_.back(); v_.pop_back(); }
        while (v_.size() < n) { v_.push_back(new T()); }
    }

    // iterator support — iterates over T&
    struct iterator {
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = T*;
        using reference = T&;
        using iterator_category = std::random_access_iterator_tag;
        typename std::vector<T*>::iterator it;
        T& operator*() const { return **it; }
        T* operator->() const { return *it; }
        iterator& operator++() { ++it; return *this; }
        iterator operator++(int) { auto tmp = *this; ++it; return tmp; }
        iterator& operator--() { --it; return *this; }
        iterator operator--(int) { auto tmp = *this; --it; return tmp; }
        iterator operator+(difference_type n) const { return iterator{it + n}; }
        iterator operator-(difference_type n) const { return iterator{it - n}; }
        difference_type operator-(const iterator& o) const { return it - o.it; }
        iterator& operator+=(difference_type n) { it += n; return *this; }
        iterator& operator-=(difference_type n) { it -= n; return *this; }
        bool operator==(const iterator& o) const { return it == o.it; }
        bool operator!=(const iterator& o) const { return it != o.it; }
        bool operator<(const iterator& o) const { return it < o.it; }
    };
    struct const_iterator {
        using difference_type = std::ptrdiff_t;
        using value_type = const T;
        using pointer = const T*;
        using reference = const T&;
        using iterator_category = std::random_access_iterator_tag;
        typename std::vector<T*>::const_iterator it;
        const T& operator*() const { return **it; }
        const T* operator->() const { return *it; }
        const_iterator& operator++() { ++it; return *this; }
        const_iterator operator++(int) { auto tmp = *this; ++it; return tmp; }
        const_iterator& operator--() { --it; return *this; }
        const_iterator operator+(difference_type n) const { return const_iterator{it + n}; }
        const_iterator operator-(difference_type n) const { return const_iterator{it - n}; }
        difference_type operator-(const const_iterator& o) const { return it - o.it; }
        bool operator==(const const_iterator& o) const { return it == o.it; }
        bool operator!=(const const_iterator& o) const { return it != o.it; }
    };
    iterator begin() { return iterator{v_.begin()}; }
    iterator end()   { return iterator{v_.end()}; }
    const_iterator begin() const { return const_iterator{v_.begin()}; }
    const_iterator end()   const { return const_iterator{v_.end()}; }
    void erase(iterator pos) { delete *pos.it; v_.erase(pos.it); }
    iterator insert(iterator pos, T* p) {
        auto it = v_.insert(pos.it, p);
        return iterator{it};
    }
};
} // boost

// -----------------------------------------------------------------------
// boost::format — simple positional string formatter (%1% %2% ...)
// -----------------------------------------------------------------------
namespace boost {
class format {
    std::string fmt_;
    int next_arg_;
public:
    explicit format(const std::string& f) : fmt_(f), next_arg_(1) {}
    template<typename T>
    format& operator%(const T& val) {
        std::ostringstream oss;
        oss << val;
        std::string marker = "%" + std::to_string(next_arg_) + "%";
        std::string::size_type pos = fmt_.find(marker);
        if (pos != std::string::npos) {
            fmt_.replace(pos, marker.size(), oss.str());
        }
        ++next_arg_;
        return *this;
    }
    std::string str() const { return fmt_; }
    friend std::ostream& operator<<(std::ostream& os, const format& f) {
        return os << f.fmt_;
    }
};
} // boost

// -----------------------------------------------------------------------
// boost::circular_buffer — simple ring buffer
// -----------------------------------------------------------------------
namespace boost {
template<typename T>
class circular_buffer {
    // Use aligned storage to avoid requiring default-constructible T
    std::size_t capacity_;
    std::size_t head_;
    std::size_t count_;
    T* buf_;

    void destroy_all() {
        for (std::size_t i = 0; i < count_; ++i) {
            buf_[(head_ + i) % capacity_].~T();
        }
    }
public:
    explicit circular_buffer(std::size_t cap = 0)
        : capacity_(cap), head_(0), count_(0),
          buf_(cap ? static_cast<T*>(::operator new(cap * sizeof(T))) : nullptr) {}
    ~circular_buffer() {
        destroy_all();
        ::operator delete(buf_);
    }
    circular_buffer(const circular_buffer&) = delete;
    circular_buffer& operator=(const circular_buffer&) = delete;

    void set_capacity(std::size_t cap) {
        destroy_all();
        ::operator delete(buf_);
        capacity_ = cap;
        head_ = 0;
        count_ = 0;
        buf_ = cap ? static_cast<T*>(::operator new(cap * sizeof(T))) : nullptr;
    }
    void push_back(const T& val) {
        if (capacity_ == 0) return;
        std::size_t idx = (head_ + count_) % capacity_;
        if (count_ < capacity_) {
            new (&buf_[idx]) T(val);
            ++count_;
        } else {
            buf_[head_].~T();
            head_ = (head_ + 1) % capacity_;
            new (&buf_[idx]) T(val);
        }
    }
    T& operator[](std::size_t i) { return buf_[(head_ + i) % capacity_]; }
    const T& operator[](std::size_t i) const { return buf_[(head_ + i) % capacity_]; }
    T& front() { return buf_[head_]; }
    T& back()  { return buf_[(head_ + count_ - 1) % capacity_]; }
    std::size_t size() const { return count_; }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == capacity_; }
    void pop_front() {
        if (count_ > 0) {
            buf_[head_].~T();
            head_ = (head_ + 1) % capacity_;
            --count_;
        }
    }
    void clear() { destroy_all(); head_ = 0; count_ = 0; }

    using value_type = T;

    // Simple iterator wrapping index-based access
    struct iterator {
        circular_buffer* cb_; std::size_t idx_;
        T& operator*() { return (*cb_)[idx_]; }
        iterator& operator++() { ++idx_; return *this; }
        bool operator!=(const iterator& o) const { return idx_ != o.idx_; }
        bool operator==(const iterator& o) const { return idx_ == o.idx_; }
    };
    struct const_iterator {
        const circular_buffer* cb_; std::size_t idx_;
        const T& operator*() const { return (*cb_)[idx_]; }
        const_iterator& operator++() { ++idx_; return *this; }
        bool operator!=(const const_iterator& o) const { return idx_ != o.idx_; }
        bool operator==(const const_iterator& o) const { return idx_ == o.idx_; }
    };
    iterator begin() { return {this, 0}; }
    iterator end() { return {this, count_}; }
    const_iterator begin() const { return {this, 0}; }
    const_iterator end() const { return {this, count_}; }
};
} // boost

// -----------------------------------------------------------------------
// boost::dynamic_bitset — resizable bit vector
// -----------------------------------------------------------------------
namespace boost {
template<typename Block = unsigned long>
class dynamic_bitset {
    std::vector<Block> blocks_;
    std::size_t nbits_;
    static constexpr std::size_t BITS_PER_BLOCK = sizeof(Block) * 8;
    std::size_t block_index(std::size_t pos) const { return pos / BITS_PER_BLOCK; }
    Block bit_mask(std::size_t pos)   const { return Block(1) << (pos % BITS_PER_BLOCK); }
public:
    dynamic_bitset() : nbits_(0) {}
    explicit dynamic_bitset(std::size_t n) : blocks_((n + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK, 0), nbits_(n) {}
    dynamic_bitset(std::size_t n, unsigned long val)
        : blocks_((n + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK, 0), nbits_(n) {
        if (!blocks_.empty()) blocks_[0] = static_cast<Block>(val);
    }
    void resize(std::size_t n) {
        blocks_.resize((n + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK, 0);
        nbits_ = n;
    }
    void resize(std::size_t n, bool val) {
        std::size_t old = nbits_;
        resize(n);
        if (val) { for (std::size_t i = old; i < n; ++i) set(i); }
    }
    std::size_t size() const { return nbits_; }
    bool test(std::size_t pos) const { return (blocks_[block_index(pos)] & bit_mask(pos)) != 0; }
    dynamic_bitset& set() { for (auto& b : blocks_) b = ~Block(0); return *this; }
    dynamic_bitset& set(std::size_t pos) { blocks_[block_index(pos)] |= bit_mask(pos); return *this; }
    dynamic_bitset& reset(std::size_t pos) { blocks_[block_index(pos)] &= ~bit_mask(pos); return *this; }
    dynamic_bitset& reset() { for (auto& b : blocks_) b = 0; return *this; }
    void clear() { blocks_.clear(); nbits_ = 0; }
    dynamic_bitset& flip() {
        for (auto& b : blocks_) b = ~b;
        return *this;
    }
    dynamic_bitset& flip(std::size_t pos) {
        blocks_[block_index(pos)] ^= bit_mask(pos);
        return *this;
    }
    void push_back(bool val) { resize(nbits_ + 1); if (val) set(nbits_ - 1); }
    bool empty() const { return nbits_ == 0; }
    bool test_set(std::size_t pos, bool val = true) {
        bool was = test(pos);
        if (val) set(pos); else reset(pos);
        return was;
    }
    // Proxy reference for operator[]
    struct reference {
        dynamic_bitset& bs_; std::size_t pos_;
        reference(dynamic_bitset& bs, std::size_t pos) : bs_(bs), pos_(pos) {}
        operator bool() const { return bs_.test(pos_); }
        reference& operator=(bool val) { if (val) bs_.set(pos_); else bs_.reset(pos_); return *this; }
        reference& operator=(const reference& o) { return operator=(bool(o)); }
        reference& operator&=(bool val) { if (!val) bs_.reset(pos_); return *this; }
        reference& operator|=(bool val) { if (val) bs_.set(pos_); return *this; }
    };
    reference operator[](std::size_t pos) { return reference(*this, pos); }
    bool operator[](std::size_t pos) const { return test(pos); }
    std::size_t count() const {
        std::size_t c = 0;
        for (auto b : blocks_) { while (b) { c += b & 1; b >>= 1; } }
        return c;
    }
    bool none() const { return count() == 0; }
    bool any()  const { return !none(); }
    // Bitwise operators
    dynamic_bitset operator&(const dynamic_bitset& o) const {
        dynamic_bitset result(std::max(nbits_, o.nbits_));
        std::size_t min_blocks = std::min(blocks_.size(), o.blocks_.size());
        for (std::size_t i = 0; i < min_blocks; ++i) result.blocks_[i] = blocks_[i] & o.blocks_[i];
        return result;
    }
    dynamic_bitset operator|(const dynamic_bitset& o) const {
        dynamic_bitset result(std::max(nbits_, o.nbits_));
        for (std::size_t i = 0; i < blocks_.size(); ++i) result.blocks_[i] = blocks_[i];
        for (std::size_t i = 0; i < o.blocks_.size(); ++i) result.blocks_[i] |= o.blocks_[i];
        return result;
    }
    dynamic_bitset operator^(const dynamic_bitset& o) const {
        dynamic_bitset result(std::max(nbits_, o.nbits_));
        for (std::size_t i = 0; i < blocks_.size(); ++i) result.blocks_[i] = blocks_[i];
        for (std::size_t i = 0; i < o.blocks_.size(); ++i) result.blocks_[i] ^= o.blocks_[i];
        return result;
    }
    dynamic_bitset operator~() const {
        dynamic_bitset result(*this);
        result.flip();
        return result;
    }
    bool operator==(const dynamic_bitset& o) const {
        if (nbits_ != o.nbits_) return false;
        return blocks_ == o.blocks_;
    }
    bool operator!=(const dynamic_bitset& o) const { return !(*this == o); }
    dynamic_bitset& operator&=(const dynamic_bitset& o) {
        std::size_t min_blocks = std::min(blocks_.size(), o.blocks_.size());
        for (std::size_t i = 0; i < min_blocks; ++i) blocks_[i] &= o.blocks_[i];
        for (std::size_t i = min_blocks; i < blocks_.size(); ++i) blocks_[i] = 0;
        return *this;
    }
    dynamic_bitset& operator|=(const dynamic_bitset& o) {
        if (o.blocks_.size() > blocks_.size()) blocks_.resize(o.blocks_.size(), 0);
        for (std::size_t i = 0; i < o.blocks_.size(); ++i) blocks_[i] |= o.blocks_[i];
        if (o.nbits_ > nbits_) nbits_ = o.nbits_;
        return *this;
    }
    // find_first/find_next — find set bits
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    std::size_t find_first() const {
        for (std::size_t i = 0; i < nbits_; ++i) if (test(i)) return i;
        return npos;
    }
    std::size_t find_next(std::size_t pos) const {
        for (std::size_t i = pos + 1; i < nbits_; ++i) if (test(i)) return i;
        return npos;
    }
};
} // boost

// -----------------------------------------------------------------------
// boost::indirect_iterator — dereferences twice (for containers of ptrs)
// -----------------------------------------------------------------------
namespace boost {
namespace detail {
    template<typename T, typename = void>
    struct pointee { typedef typename std::remove_reference<decltype(*std::declval<T>())>::type type; };
    template<typename T>
    struct pointee<T, typename std::enable_if<
        !std::is_void<typename T::element_type>::value>::type> {
        typedef typename T::element_type type;
    };
} // detail

template<typename BaseIter>
class indirect_iterator {
    BaseIter it_;
public:
    // Defer pointee resolution to avoid incomplete-type errors when the
    // iterator_range (or any other container) stores indirect_iterator
    // members before the pointed-to type is complete.  The actual
    // value_type/reference/pointer aliases are only evaluated in contexts
    // that truly need them (e.g. when operator* is called).
    using difference_type = typename std::iterator_traits<BaseIter>::difference_type;
    using iterator_category = typename std::iterator_traits<BaseIter>::iterator_category;

    // These three aliases used to eagerly instantiate detail::pointee,
    // which requires the pointee type to be complete.  We keep them as
    // dependent defaults so that the class itself can be instantiated
    // with an incomplete pointee type (the aliases are only checked when
    // actually used, which happens after the type is complete).
    template<typename B = BaseIter>
    using value_type_of = typename detail::pointee<
        typename std::iterator_traits<B>::value_type>::type;

    using value_type = void;   // safe placeholder — real type via operators
    using reference  = void;
    using pointer    = void;

    indirect_iterator() : it_() {}
    explicit indirect_iterator(BaseIter it) : it_(it) {}
    template<typename OtherIter>
    indirect_iterator(const indirect_iterator<OtherIter>& other) : it_(other.base()) {}

    // Return types are deduced only when the member function is
    // instantiated, so the pointee type only needs to be complete at
    // the call site — not at the point where indirect_iterator itself
    // is first mentioned.
    auto operator*()  const -> decltype(**it_) { return **it_; }
    auto operator->() const -> decltype(&(**it_)) { return &(**it_); }

    indirect_iterator& operator++() { ++it_; return *this; }
    indirect_iterator operator++(int) { auto tmp = *this; ++it_; return tmp; }
    indirect_iterator& operator--() { --it_; return *this; }
    indirect_iterator operator--(int) { auto tmp = *this; --it_; return tmp; }

    // Random access operations
    indirect_iterator& operator+=(difference_type n) { it_ += n; return *this; }
    indirect_iterator& operator-=(difference_type n) { it_ -= n; return *this; }
    indirect_iterator operator+(difference_type n) const { return indirect_iterator(it_ + n); }
    indirect_iterator operator-(difference_type n) const { return indirect_iterator(it_ - n); }
    difference_type operator-(const indirect_iterator& o) const { return it_ - o.it_; }
    auto operator[](difference_type n) const -> decltype(**(it_ + n)) { return **(it_ + n); }

    friend indirect_iterator operator+(difference_type n, const indirect_iterator& it) {
        return indirect_iterator(it.it_ + n);
    }

    // Relational comparisons
    bool operator==(const indirect_iterator& o) const { return it_ == o.it_; }
    bool operator!=(const indirect_iterator& o) const { return it_ != o.it_; }
    bool operator<(const indirect_iterator& o)  const { return it_ < o.it_; }
    bool operator<=(const indirect_iterator& o) const { return it_ <= o.it_; }
    bool operator>(const indirect_iterator& o)  const { return it_ > o.it_; }
    bool operator>=(const indirect_iterator& o) const { return it_ >= o.it_; }

    BaseIter base() const { return it_; }
};
template<typename BaseIter>
inline indirect_iterator<BaseIter> make_indirect_iterator(BaseIter it) {
    return indirect_iterator<BaseIter>(it);
}
} // boost

// -----------------------------------------------------------------------
// boost::bimaps::bimap — bidirectional map
// -----------------------------------------------------------------------
namespace boost {
namespace bimaps {
// Tags used in bimap template parameters (must precede bimap)
struct set_of_tag {};
template<typename T> struct set_of { typedef T value_type; };
template<typename T> struct multiset_of { typedef T value_type; };

// Unwrap set_of<T>/multiset_of<T> to T; pass through plain types
template<typename T> struct unwrap_bimap_type { using type = T; };
template<typename T> struct unwrap_bimap_type<set_of<T>> { using type = T; };
template<typename T> struct unwrap_bimap_type<multiset_of<T>> { using type = T; };

template<typename LeftSpec, typename RightSpec>
class bimap {
public:
    using Left = typename unwrap_bimap_type<LeftSpec>::type;
    using Right = typename unwrap_bimap_type<RightSpec>::type;
    struct value_type {
        Left first;
        Right second;
        Left& left = first;
        Right& right = second;
        value_type(const Left& l, const Right& r) : first(l), second(r) {}
        value_type(const value_type& o) : first(o.first), second(o.second) {}
        value_type& operator=(const value_type& o) { first = o.first; second = o.second; return *this; }
    };
    using left_key_type = Left;
    using right_key_type = Right;
private:
    std::vector<value_type> data_;
public:
    // --- left view ---
    struct left_iterator {
        typename std::vector<value_type>::iterator it;
        value_type& operator*() { return *it; }
        value_type* operator->() { return &(*it); }
        left_iterator& operator++() { ++it; return *this; }
        left_iterator operator++(int) { auto t = *this; ++it; return t; }
        bool operator==(const left_iterator& o) const { return it == o.it; }
        bool operator!=(const left_iterator& o) const { return it != o.it; }
    };
    struct const_left_iterator {
        typename std::vector<value_type>::const_iterator it;
        const value_type& operator*() const { return *it; }
        const value_type* operator->() const { return &(*it); }
        const_left_iterator& operator++() { ++it; return *this; }
        const_left_iterator operator++(int) { auto t = *this; ++it; return t; }
        bool operator==(const const_left_iterator& o) const { return it == o.it; }
        bool operator!=(const const_left_iterator& o) const { return it != o.it; }
    };
    struct left_map {
        bimap* bm;
        using iterator = left_iterator;
        using const_iterator = const_left_iterator;
        using value_type = bimap::value_type;
        left_map& operator=(const left_map& o) { bm = o.bm; return *this; }
        iterator begin() { return iterator{bm->data_.begin()}; }
        iterator end()   { return iterator{bm->data_.end()}; }
        const_iterator begin() const { return const_iterator{bm->data_.begin()}; }
        const_iterator end()   const { return const_iterator{bm->data_.end()}; }
        template<typename K>
        iterator find(const K& k) {
            for (auto it = bm->data_.begin(); it != bm->data_.end(); ++it)
                if (it->first == Left(k)) return iterator{it};
            return end();
        }
        template<typename K>
        const_iterator find(const K& k) const {
            for (auto it = bm->data_.begin(); it != bm->data_.end(); ++it)
                if (it->first == Left(k)) return const_iterator{it};
            return end();
        }
        void erase(iterator pos) { bm->data_.erase(pos.it); }
        std::size_t erase(const Left& k) {
            std::size_t removed = 0;
            for (auto it = bm->data_.begin(); it != bm->data_.end(); ) {
                if (it->first == k) { it = bm->data_.erase(it); ++removed; }
                else ++it;
            }
            return removed;
        }
        void replace_data(iterator pos, const Right& new_val) {
            pos.it->second = new_val;
        }
        template<typename Modifier>
        void modify_data(iterator pos, Modifier mod) {
            mod(pos.it->second);
        }
        iterator insert(iterator /*hint*/, const std::pair<Left, Right>& p) {
            // hint is ignored in this simple implementation
            bm->data_.push_back(bimap::value_type(p.first, p.second));
            return iterator{bm->data_.end() - 1};
        }
        std::size_t count(const Left& k) const {
            std::size_t c = 0;
            for (auto& v : bm->data_) if (v.first == k) ++c;
            return c;
        }
        std::size_t size() const { return bm->data_.size(); }
        bool empty() const { return bm->data_.empty(); }
    };

    // --- right view ---
    // Right view iterators present a flipped view: first=Right, second=Left
    struct right_value_ref {
        Right& first;
        Left& second;
        right_value_ref(value_type& v) : first(v.second), second(v.first) {}
    };
    struct const_right_value_ref {
        const Right& first;
        const Left& second;
        const_right_value_ref(const value_type& v) : first(v.second), second(v.first) {}
    };
    struct right_iterator {
        typename std::vector<value_type>::iterator it;
        right_value_ref operator*() { return right_value_ref(*it); }
        struct arrow_proxy {
            right_value_ref ref;
            right_value_ref* operator->() { return &ref; }
        };
        arrow_proxy operator->() { return arrow_proxy{right_value_ref(*it)}; }
        right_iterator& operator++() { ++it; return *this; }
        right_iterator operator++(int) { auto t = *this; ++it; return t; }
        bool operator==(const right_iterator& o) const { return it == o.it; }
        bool operator!=(const right_iterator& o) const { return it != o.it; }
    };
    struct const_right_iterator {
        typename std::vector<value_type>::const_iterator it;
        const_right_value_ref operator*() const { return const_right_value_ref(*it); }
        struct arrow_proxy {
            const_right_value_ref ref;
            const const_right_value_ref* operator->() const { return &ref; }
        };
        arrow_proxy operator->() const { return arrow_proxy{const_right_value_ref(*it)}; }
        const_right_iterator& operator++() { ++it; return *this; }
        const_right_iterator operator++(int) { auto t = *this; ++it; return t; }
        bool operator==(const const_right_iterator& o) const { return it == o.it; }
        bool operator!=(const const_right_iterator& o) const { return it != o.it; }
    };
    struct right_map {
        bimap* bm;
        using iterator = right_iterator;
        using const_iterator = const_right_iterator;
        using value_type = bimap::value_type;
        right_map& operator=(const right_map& o) { bm = o.bm; return *this; }
        iterator begin() { return iterator{bm->data_.begin()}; }
        iterator end()   { return iterator{bm->data_.end()}; }
        const_iterator begin() const { return const_iterator{bm->data_.begin()}; }
        const_iterator end()   const { return const_iterator{bm->data_.end()}; }
        iterator find(const Right& k) {
            for (auto it = bm->data_.begin(); it != bm->data_.end(); ++it)
                if (it->second == k) return iterator{it};
            return end();
        }
        const_iterator find(const Right& k) const {
            for (auto it = bm->data_.begin(); it != bm->data_.end(); ++it)
                if (it->second == k) return const_iterator{it};
            return end();
        }
        void erase(iterator pos) { bm->data_.erase(pos.it); }
        std::size_t count(const Right& k) const {
            std::size_t c = 0;
            for (auto& v : bm->data_) if (v.second == k) ++c;
            return c;
        }
        std::pair<const_iterator, const_iterator> equal_range(const Right& k) const {
            // Collect matching range - since data isn't sorted by right key,
            // return begin/end of full range for simplicity (correct but not optimal)
            // Find first and past-last matching entries
            auto first = end(), last = end();
            bool found_first = false;
            for (auto it = bm->data_.begin(); it != bm->data_.end(); ++it) {
                if (it->second == k) {
                    if (!found_first) { first = const_iterator{it}; found_first = true; }
                    last = const_iterator{it}; ++last;
                }
            }
            if (!found_first) return {end(), end()};
            return {first, last};
        }
        std::size_t size() const { return bm->data_.size(); }
        bool empty() const { return bm->data_.empty(); }
    };

    left_map left;
    right_map right;

    bimap() : left{this}, right{this} {}

    bimap(const bimap& o) : data_(o.data_), left{this}, right{this} {}
    bimap(bimap&& o) noexcept : data_(std::move(o.data_)), left{this}, right{this} {}

    bimap& operator=(const bimap& o) {
        if (this != &o) {
            data_ = o.data_;
            left.bm = this;
            right.bm = this;
        }
        return *this;
    }
    bimap& operator=(bimap&& o) noexcept {
        if (this != &o) {
            data_ = std::move(o.data_);
            left.bm = this;
            right.bm = this;
        }
        return *this;
    }

    using iterator = left_iterator;
    using const_iterator = const_left_iterator;
    using left_const_iterator = const_left_iterator;
    iterator begin() { return left.begin(); }
    iterator end()   { return left.end(); }
    const_iterator begin() const { return left.begin(); }
    const_iterator end()   const { return left.end(); }

    void insert(const value_type& v) { data_.push_back(v); }
    std::size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); }
    iterator erase(iterator pos) {
        auto it = data_.erase(pos.it);
        return iterator{it};
    }
};

// (set_of, multiset_of defined above bimap class)

} // bimaps
} // boost

// boost::program_options::error is now defined in the main program_options stub above.

// -----------------------------------------------------------------------
// boost::remove_erase — erase-remove idiom
// -----------------------------------------------------------------------
namespace boost {
    template<typename Container, typename T>
    void remove_erase(Container& c, const T& val) {
        c.erase(std::remove(c.begin(), c.end(), val), c.end());
    }
    template<typename Container, typename Pred>
    void remove_erase_if(Container& c, Pred p) {
        c.erase(std::remove_if(c.begin(), c.end(), p), c.end());
    }
}

// -----------------------------------------------------------------------
// boost::multi_array — minimal 2D stub
// -----------------------------------------------------------------------
namespace boost {
struct extents_range {
    std::size_t dims[2]{};
    int ndims{0};
    extents_range operator[](std::size_t n) const {
        extents_range r = *this;
        r.dims[r.ndims] = n;
        r.ndims++;
        return r;
    }
};
inline const extents_range extents{};

template<typename T, int N>
class multi_array {
    std::vector<T> data_;
    std::size_t dims_[2]{};
public:
    multi_array() = default;
    multi_array(extents_range er) : data_(er.dims[0] * er.dims[1]), dims_{er.dims[0], er.dims[1]} {}
    T* operator[](std::size_t i) { return data_.data() + i * dims_[1]; }
    const T* operator[](std::size_t i) const { return data_.data() + i * dims_[1]; }
    void resize(extents_range er) { dims_[0] = er.dims[0]; dims_[1] = er.dims[1]; data_.resize(dims_[0] * dims_[1]); }
};
} // boost

// -----------------------------------------------------------------------
// boost::adaptors::strided — stub
// -----------------------------------------------------------------------
namespace boost {
namespace adaptors {
struct strided_holder { std::size_t step; };
inline strided_holder strided(std::size_t s) { return {s}; }
using strided_t = strided_holder;

// map_keys adaptor
struct map_keys_t {};
constexpr map_keys_t map_keys{};

// map_values adaptor
struct map_values_t {};
constexpr map_values_t map_values{};

// reversed adaptor
struct reversed_t {};
constexpr reversed_t reversed{};

// filtered adaptor
template<typename Pred>
struct filtered_holder { Pred pred; };
template<typename Pred>
inline filtered_holder<Pred> filtered(Pred p) { return {p}; }

// transformed adaptor — wraps a range by applying a function to each element
template<typename Func>
struct transformed_holder { Func func; };
template<typename Func>
inline transformed_holder<Func> transformed(Func f) { return {f}; }
} // adaptors

// Pipe operator: range | transformed(f) -> transformed_range
template<typename Range, typename Func>
struct transformed_range {
    const Range& range_; Func func_;
    struct iterator {
        decltype(std::declval<Range>().begin()) it_;
        Func func_;
        auto operator*() const -> decltype(func_(*it_)) { return func_(*it_); }
        iterator& operator++() { ++it_; return *this; }
        bool operator!=(const iterator& o) const { return it_ != o.it_; }
        bool operator==(const iterator& o) const { return it_ == o.it_; }
    };
    iterator begin() const { return {range_.begin(), func_}; }
    iterator end() const { return {range_.end(), func_}; }
};
namespace adaptors {
template<typename Range, typename Func>
inline transformed_range<Range, Func> operator|(const Range& r, transformed_holder<Func> h) {
    return {r, h.func};
}
} // adaptors

// Strided range adaptor — range | strided(n) -> strided_range
template<typename Range>
struct strided_range {
    const Range& range_;
    std::size_t step_;
    using base_iter = decltype(std::declval<const Range&>().begin());
    using base_value = typename std::remove_reference<decltype(*std::declval<base_iter>())>::type;
    struct iterator {
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::remove_cv_t<base_value>;
        using difference_type = std::ptrdiff_t;
        using pointer = const base_value*;
        using reference = const base_value&;

        base_iter it_;
        base_iter end_;
        std::size_t step_;
        decltype(auto) operator*() const { return *it_; }
        iterator& operator++() { for (std::size_t i = 0; i < step_ && it_ != end_; ++i) ++it_; return *this; }
        iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }
        bool operator!=(const iterator& o) const { return it_ != o.it_; }
        bool operator==(const iterator& o) const { return it_ == o.it_; }
    };
    iterator begin() const { return {range_.begin(), range_.end(), step_}; }
    iterator end() const { return {range_.end(), range_.end(), step_}; }
    std::size_t size() const {
        auto total = static_cast<std::size_t>(range_.end() - range_.begin());
        return (total + step_ - 1) / step_;
    }
};
namespace adaptors {
template<typename Range>
inline strided_range<Range> operator|(const Range& r, strided_holder h) {
    return {r, h.step};
}
} // adaptors

// map_keys: range of pairs -> range of first elements
template<typename Range>
struct map_keys_range {
    const Range& range_;
    struct iterator {
        decltype(std::declval<const Range&>().begin()) it_;
        auto operator*() const -> decltype(it_->first) { return it_->first; }
        iterator& operator++() { ++it_; return *this; }
        bool operator!=(const iterator& o) const { return it_ != o.it_; }
        bool operator==(const iterator& o) const { return it_ == o.it_; }
    };
    iterator begin() const { return {range_.begin()}; }
    iterator end() const { return {range_.end()}; }
};
namespace adaptors {
template<typename Range>
inline map_keys_range<Range> operator|(const Range& r, map_keys_t) {
    return {r};
}
} // adaptors

// map_values: range of pairs -> range of second elements
template<typename Range>
struct map_values_range {
    const Range& range_;
    struct iterator {
        decltype(std::declval<const Range&>().begin()) it_;
        auto operator*() const -> decltype(it_->second) { return it_->second; }
        iterator& operator++() { ++it_; return *this; }
        bool operator!=(const iterator& o) const { return it_ != o.it_; }
        bool operator==(const iterator& o) const { return it_ == o.it_; }
    };
    iterator begin() const { return {range_.begin()}; }
    iterator end() const { return {range_.end()}; }
};
namespace adaptors {
template<typename Range>
inline map_values_range<Range> operator|(const Range& r, map_values_t) {
    return {r};
}
} // adaptors

// reversed range
template<typename Range>
struct reversed_range {
    const Range& range_;
    auto begin() const { return range_.rbegin(); }
    auto end() const { return range_.rend(); }
};
namespace adaptors {
template<typename Range>
inline reversed_range<Range> operator|(const Range& r, reversed_t) {
    return {r};
}
} // adaptors

// filtered range
template<typename Range, typename Pred>
struct filtered_range {
    const Range& range_;
    Pred pred_;
    using base_iter = decltype(std::declval<const Range&>().begin());
    struct iterator {
        base_iter it_;
        base_iter end_;
        Pred pred_;
        void advance() { while (it_ != end_ && !pred_(*it_)) ++it_; }
        decltype(auto) operator*() const { return *it_; }
        iterator& operator++() { ++it_; advance(); return *this; }
        bool operator!=(const iterator& o) const { return it_ != o.it_; }
        bool operator==(const iterator& o) const { return it_ == o.it_; }
    };
    iterator begin() const { iterator i{range_.begin(), range_.end(), pred_}; i.advance(); return i; }
    iterator end() const { return {range_.end(), range_.end(), pred_}; }
};
namespace adaptors {
template<typename Range, typename Pred>
inline filtered_range<Range, Pred> operator|(const Range& r, filtered_holder<Pred> h) {
    return {r, h.pred};
}
} // adaptors
} // boost

// -----------------------------------------------------------------------
// Include guards — prevent real Boost headers from being picked up
// -----------------------------------------------------------------------
#define BOOST_FILESYSTEM_PATH_HPP 1
#define BOOST_FILESYSTEM_OPERATIONS_HPP 1
#define BOOST_FILESYSTEM_HPP 1
#define BOOST_RANDOM_HPP 1
#define BOOST_RANDOM_MERSENNE_TWISTER_HPP 1
#define BOOST_ALGORITHM_STRING_HPP 1
#define BOOST_ALGORITHM_STRING_TRIM_HPP 1
#define BOOST_ALGORITHM_STRING_PREDICATE_HPP 1
#define BOOST_ALGORITHM_STRING_REPLACE_HPP 1
#define BOOST_ALGORITHM_STRING_CLASSIFICATION_HPP 1
#define BOOST_ALGORITHM_STRING_SPLIT_HPP 1
#define BOOST_ALGORITHM_STRING_JOIN_HPP 1
#define BOOST_PROGRAM_OPTIONS_HPP 1
#define BOOST_PROGRAM_OPTIONS_OPTIONS_DESCRIPTION_HPP 1
#define BOOST_PROGRAM_OPTIONS_VARIABLES_MAP_HPP 1
#define BOOST_PROGRAM_OPTIONS_ERRORS_HPP 1
#define BOOST_SCOPE_EXIT_HPP 1
#define BOOST_REGEX_HPP 1
#define BOOST_LOCALE_HPP 1
#define BOOST_LOCALE_GENERATOR_HPP 1
#define BOOST_LOCALE_INFO_HPP 1
#define BOOST_THREAD_HPP 1
#define BOOST_THREAD_MUTEX_HPP 1
#define BOOST_COROUTINE2_COROUTINE_HPP 1
#define BOOST_ITERATOR_RANGE_HPP 1
#define BOOST_RANGE_ITERATOR_RANGE_HPP 1
#define BOOST_PTR_CONTAINER_PTR_VECTOR_HPP 1
#define BOOST_FORMAT_HPP 1
#define BOOST_CIRCULAR_BUFFER_HPP 1
#define BOOST_CIRCULAR_BUFFER_FWD_HPP 1
#define BOOST_DYNAMIC_BITSET_HPP 1
#define BOOST_DYNAMIC_BITSET_DYNAMIC_BITSET_HPP 1
#define BOOST_ITERATOR_INDIRECT_ITERATOR_HPP 1
#define BOOST_BIMAP_HPP 1
#define BOOST_BIMAP_BIMAP_HPP 1
#define BOOST_ADAPTORS_HPP 1
#define BOOST_RANGE_ADAPTOR_STRIDED_HPP 1
#define BOOST_IOSTREAMS_HPP 1
#define BOOST_GRAPH_ADJACENCY_LIST_HPP 1
#define BOOST_GRAPH_ASTAR_SEARCH_HPP 1
#define BOOST_LOGIC_TRIBOOL_HPP 1
#define BOOST_LOGIC_TRIBOOL_FWD_HPP 1
#define BOOST_MULTI_INDEX_CONTAINER_HPP 1
#define BOOST_ITERATOR_FACADE_HPP 1

// -----------------------------------------------------------------------
// boost::tribool — three-valued boolean
// -----------------------------------------------------------------------
namespace boost {
namespace logic {
class tribool {
    int value_; // 0=false, 1=true, 2=indeterminate
public:
    tribool() : value_(2) {}
    tribool(bool v) : value_(v ? 1 : 0) {}
    explicit operator bool() const { return value_ == 1; }
    bool operator!() const { return value_ == 0; }
    friend bool operator==(tribool a, tribool b) { return a.value_ == b.value_; }
    friend bool operator!=(tribool a, tribool b) { return a.value_ != b.value_; }
    friend bool operator==(tribool a, bool b) { return a.value_ == (b ? 1 : 0); }
    friend bool operator==(bool a, tribool b) { return (a ? 1 : 0) == b.value_; }
    bool is_indeterminate() const { return value_ == 2; }
};
// Provide indeterminate as a struct with conversion operator and callable check
struct indeterminate_t {
    operator tribool() const { return tribool(); }
    bool operator()(tribool t) const { return t.is_indeterminate(); }
};
inline constexpr indeterminate_t indeterminate{};
} // logic
using logic::tribool;
using logic::indeterminate;
} // boost

// -----------------------------------------------------------------------
// boost::iostreams compressor/decompressor stubs (no compression on N64)
// -----------------------------------------------------------------------
namespace boost { namespace iostreams {
    struct gzip_params { int level; gzip_params(int l = 6) : level(l) {} };
    struct bzip2_params { int block_size; bzip2_params(int b = 9) : block_size(b) {} };
    struct gzip_compressor { gzip_compressor() {} gzip_compressor(const gzip_params&) {} };
    struct bzip2_compressor { bzip2_compressor() {} bzip2_compressor(const bzip2_params&) {} };
    struct gzip_decompressor {};
    struct bzip2_decompressor {};
}}

// -----------------------------------------------------------------------
// Boost.Preprocessor macros used by Wesnoth's Lua attribute system
// -----------------------------------------------------------------------
#define BOOST_PP_CAT(a, b) BOOST_PP_CAT_I(a, b)
#define BOOST_PP_CAT_I(a, b) a ## b

#define BOOST_PP_STRINGIZE(text) BOOST_PP_STRINGIZE_I(text)
#define BOOST_PP_STRINGIZE_I(text) #text

#endif // N64_BUILD
