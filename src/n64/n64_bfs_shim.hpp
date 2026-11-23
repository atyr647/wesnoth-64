#pragma once
/**
 * n64_bfs_shim.hpp -- boost::filesystem and boost::iostreams (file devices)
 * implemented over the console's VFS (n64_vfs.hpp).
 *
 * Covers what Wesnoth uses (src/filesystem.cpp and friends) so that the
 * engine's own filesystem code runs unmodified. Semantics follow Boost:
 * error_code overloads set ec instead of throwing; the throwing overloads
 * throw filesystem_error.
 */

#include "n64_vfs.hpp"

#include <cstdint>
#include <ctime>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace boost {
namespace system {
#ifndef N64_BOOST_SYSTEM_ERROR_CODE_DEFINED
#define N64_BOOST_SYSTEM_ERROR_CODE_DEFINED
using error_code = std::error_code;
#endif
namespace errc {
inline constexpr std::errc no_such_file_or_directory = std::errc::no_such_file_or_directory;
inline constexpr std::errc file_exists               = std::errc::file_exists;
inline constexpr std::errc not_a_directory           = std::errc::not_a_directory;
inline constexpr std::errc permission_denied         = std::errc::permission_denied;
inline constexpr std::errc directory_not_empty       = std::errc::directory_not_empty;
inline constexpr std::errc read_only_file_system     = std::errc::read_only_file_system;
inline constexpr std::errc operation_not_permitted   = std::errc::operation_not_permitted;
inline constexpr std::errc function_not_supported    = std::errc::function_not_supported;
inline std::error_code make_error_code(std::errc e) { return std::make_error_code(e); }
} // namespace errc
} // namespace system

namespace filesystem {

using boost::system::error_code;

// -----------------------------------------------------------------------
// path
// -----------------------------------------------------------------------
class path {
public:
    using value_type = char;
    using string_type = std::string;
    static constexpr char preferred_separator = '/';

    path() = default;
    path(const char* s) : p_(s ? s : "") {}
    path(const std::string& s) : p_(s) {}
    path(std::string&& s) : p_(std::move(s)) {}
    template<class It> path(It b, It e) : p_(b, e) {}

    const char* c_str() const               { return p_.c_str(); }
    const std::string& native() const       { return p_; }
    const std::string& string() const       { return p_; }
    const std::string& generic_string() const { return p_; }
    std::string u8string() const            { return p_; }
    std::wstring wstring() const            { return std::wstring(p_.begin(), p_.end()); }
    operator const std::string&() const     { return p_; }

    bool empty() const                      { return p_.empty(); }
    std::size_t size() const                { return p_.size(); }
    void clear()                            { p_.clear(); }
    path& make_preferred()                  { return *this; }
    path& remove_filename()                 { auto s = p_.rfind('/'); p_ = s == std::string::npos ? std::string() : p_.substr(0, s + 1); return *this; }
    path& replace_extension(const path& e = path()) {
        auto f = filename().string();
        auto dot = f.rfind('.');
        if (dot != std::string::npos && f != "." && f != "..") p_.erase(p_.size() - (f.size() - dot));
        if (!e.empty()) { if (e.p_[0] != '.') p_ += '.'; p_ += e.p_; }
        return *this;
    }

    path root_name() const                  { return path(); }
    path root_directory() const             { return (!p_.empty() && p_[0] == '/') ? path("/") : path(); }
    path root_path() const                  { return root_directory(); }
    path relative_path() const              { size_t i = 0; while (i < p_.size() && p_[i] == '/') ++i; return path(p_.substr(i)); }
    bool is_absolute() const                { return !p_.empty() && p_[0] == '/'; }
    bool is_relative() const                { return !is_absolute(); }
    bool has_root_directory() const         { return is_absolute(); }
    bool has_root_path() const              { return is_absolute(); }
    bool has_filename() const               { return !filename().empty(); }
    bool has_parent_path() const            { return !parent_path().empty(); }
    bool has_extension() const              { return !extension().empty(); }
    bool has_stem() const                   { return !stem().empty(); }

    path filename() const {
        if (p_.empty()) return path();
        if (p_ == "/") return path("/");
        if (p_.back() == '/') return path(".");
        auto s = p_.rfind('/');
        return s == std::string::npos ? *this : path(p_.substr(s + 1));
    }
    path stem() const {
        std::string f = filename().string();
        if (f == "." || f == "..") return path(f);
        auto d = f.rfind('.');
        return (d == std::string::npos || d == 0) ? path(f) : path(f.substr(0, d));
    }
    path extension() const {
        std::string f = filename().string();
        if (f == "." || f == "..") return path();
        auto d = f.rfind('.');
        return (d == std::string::npos || d == 0) ? path() : path(f.substr(d));
    }
    path parent_path() const {
        if (p_.empty() || p_ == "/") return path();
        std::string s = p_;
        while (s.size() > 1 && s.back() == '/') s.pop_back();
        auto i = s.rfind('/');
        if (i == std::string::npos) return path();
        if (i == 0) return path("/");
        return path(s.substr(0, i));
    }
    path lexically_normal() const {
        if (p_.empty()) return path();
        std::vector<std::string> parts;
        for (auto& c : components()) {
            if (c == "/" || c == ".") continue;
            if (c == ".." && !parts.empty() && parts.back() != "..") { parts.pop_back(); continue; }
            parts.push_back(c);
        }
        std::string out = is_absolute() ? "/" : "";
        for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
        return path(out.empty() ? std::string(".") : out);
    }
    path lexically_relative(const path& base) const {
        auto a = lexically_normal().components(), b = base.lexically_normal().components();
        size_t i = 0;
        while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
        if (i == 0 && is_absolute() != base.is_absolute()) return path();
        std::string out;
        for (size_t k = i; k < b.size(); ++k) if (b[k] != ".") out += (out.empty() ? "" : "/") + std::string("..");
        for (size_t k = i; k < a.size(); ++k) out += (out.empty() ? "" : "/") + a[k];
        return path(out.empty() ? std::string(".") : out);
    }

    // boost::filesystem v3 semantics (which Wesnoth relies on): appending
    // never replaces the path, even with an absolute right-hand side --
    // path("/rom/data") / "/" is "/rom/data/" (std::filesystem would give "/").
    path& operator/=(const path& o) {
        if (p_.empty()) { p_ = o.p_; return *this; }
        if (!o.p_.empty() && o.p_[0] == '/') {
            p_ += (p_.back() == '/') ? o.p_.substr(1) : o.p_;
            return *this;
        }
        if (p_.back() != '/' && !o.p_.empty()) p_ += '/';
        p_ += o.p_;
        return *this;
    }
    path& operator+=(const path& o)         { p_ += o.p_; return *this; }
    path& operator+=(const std::string& s)  { p_ += s; return *this; }
    path& operator+=(const char* s)         { p_ += s; return *this; }
    path& operator+=(char c)                { p_ += c; return *this; }
    friend path operator/(path a, const path& b) { a /= b; return a; }
    int compare(const path& o) const        { return p_.compare(o.p_); }
    friend bool operator==(const path& a, const path& b) { return a.p_ == b.p_; }
    friend bool operator!=(const path& a, const path& b) { return a.p_ != b.p_; }
    friend bool operator<(const path& a, const path& b)  { return a.p_ < b.p_; }
    friend std::ostream& operator<<(std::ostream& os, const path& p) { return os << '"' << p.p_ << '"'; }

    // Iteration over components: "/" root, then names; a trailing '/' yields ".".
    std::vector<std::string> components() const {
        std::vector<std::string> out;
        if (is_absolute()) out.push_back("/");
        size_t i = 0;
        while (i < p_.size()) {
            while (i < p_.size() && p_[i] == '/') ++i;
            if (i >= p_.size()) break;
            size_t j = p_.find('/', i);
            if (j == std::string::npos) j = p_.size();
            out.push_back(p_.substr(i, j - i));
            i = j;
        }
        if (p_.size() > 1 && p_.back() == '/') out.push_back(".");
        return out;
    }
    class iterator {
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = path;
        using difference_type = std::ptrdiff_t;
        using pointer = const path*;
        using reference = const path&;
        iterator() = default;
        iterator(std::shared_ptr<std::vector<path>> v, size_t i) : v_(std::move(v)), i_(i) {}
        reference operator*() const  { return (*v_)[i_]; }
        pointer operator->() const   { return &(*v_)[i_]; }
        iterator& operator++()       { ++i_; return *this; }
        iterator operator++(int)     { auto t = *this; ++i_; return t; }
        iterator& operator--()       { --i_; return *this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.i_ == b.i_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.i_ != b.i_; }
    private:
        std::shared_ptr<std::vector<path>> v_;
        size_t i_ = 0;
    };
    using const_iterator = iterator;
    iterator begin() const { return iterator(parts(), 0); }
    iterator end() const   { auto v = parts(); size_t n = v->size(); return iterator(std::move(v), n); }

private:
    std::shared_ptr<std::vector<path>> parts() const {
        auto v = std::make_shared<std::vector<path>>();
        for (auto& c : components()) v->push_back(path(c));
        return v;
    }
    std::string p_;
};

class filesystem_error : public std::system_error {
public:
    filesystem_error(const std::string& what, error_code ec) : std::system_error(ec, what) {}
    filesystem_error(const std::string& what, const path& p1, error_code ec)
        : std::system_error(ec, what + ": " + p1.string()), p1_(p1) {}
    filesystem_error(const std::string& what, const path& p1, const path& p2, error_code ec)
        : std::system_error(ec, what + ": " + p1.string() + ", " + p2.string()), p1_(p1), p2_(p2) {}
    const path& path1() const { return p1_; }
    const path& path2() const { return p2_; }
private:
    path p1_, p2_;
};

// -----------------------------------------------------------------------
// status
// -----------------------------------------------------------------------
enum file_type { status_error, status_unknown = status_error, file_not_found, regular_file,
                 directory_file, symlink_file, block_file, character_file, fifo_file,
                 socket_file, type_unknown };
enum class perms : unsigned {
    no_perms = 0, owner_read = 0400, owner_write = 0200, owner_exe = 0100, owner_all = 0700,
    group_read = 040, group_write = 020, group_exe = 010, group_all = 070,
    others_read = 04, others_write = 02, others_exe = 01, others_all = 07, all_all = 0777,
    add_perms = 0x1000, remove_perms = 0x2000 };
inline perms operator|(perms a, perms b) { return perms(unsigned(a) | unsigned(b)); }

class file_status {
public:
    file_status() = default;
    explicit file_status(file_type t, perms p = perms::all_all) : t_(t), p_(p) {}
    file_type type() const     { return t_; }
    perms permissions() const  { return p_; }
private:
    file_type t_ = status_error;
    perms p_ = perms::all_all;
};

namespace detail {
inline error_code not_found() { return std::make_error_code(std::errc::no_such_file_or_directory); }
inline void fail(const char* op, const path& p, error_code ec) { throw filesystem_error(op, p, ec); }
}

inline file_status status(const path& p, error_code& ec) {
    ec.clear();
    switch (n64vfs::stat(p.string())) {
    case n64vfs::kind::file: return file_status(regular_file);
    case n64vfs::kind::dir:  return file_status(directory_file);
    default: ec = detail::not_found(); return file_status(file_not_found);
    }
}
inline file_status status(const path& p) { error_code ec; return status(p, ec); }
inline file_status symlink_status(const path& p, error_code& ec) { return status(p, ec); }
inline file_status symlink_status(const path& p) { return status(p); }

inline bool exists(file_status s)       { return s.type() != status_error && s.type() != file_not_found; }
inline bool is_directory(file_status s) { return s.type() == directory_file; }
inline bool is_regular_file(file_status s) { return s.type() == regular_file; }
inline bool is_symlink(file_status)     { return false; }
inline bool exists(const path& p)                        { return exists(status(p)); }
inline bool exists(const path& p, error_code& ec)        { auto s = status(p, ec); if (s.type() == file_not_found) ec.clear(); return exists(s); }
inline bool is_directory(const path& p)                  { return is_directory(status(p)); }
inline bool is_directory(const path& p, error_code& ec)  { return is_directory(status(p, ec)); }
inline bool is_regular_file(const path& p)               { return is_regular_file(status(p)); }
inline bool is_regular_file(const path& p, error_code& ec) { return is_regular_file(status(p, ec)); }
inline bool is_symlink(const path&)                      { return false; }
inline bool is_symlink(const path&, error_code& ec)      { ec.clear(); return false; }

inline std::uintmax_t file_size(const path& p, error_code& ec) {
    std::uint64_t n = 0;
    if (n64vfs::stat(p.string(), &n) != n64vfs::kind::file) { ec = detail::not_found(); return std::uintmax_t(-1); }
    ec.clear();
    return n;
}
inline std::uintmax_t file_size(const path& p) { error_code ec; auto n = file_size(p, ec); if (ec) detail::fail("file_size", p, ec); return n; }

inline bool is_empty(const path& p, error_code& ec) {
    auto s = status(p, ec);
    if (ec) return false;
    if (is_directory(s)) { std::vector<std::pair<std::string, bool>> k; n64vfs::list(p.string(), k); return k.empty(); }
    return file_size(p, ec) == 0;
}
inline bool is_empty(const path& p) { error_code ec; return is_empty(p, ec); }

// No clock-backed file times on the console: everything is "as old as the ROM".
inline std::time_t last_write_time(const path& p, error_code& ec) { status(p, ec); return 0; }
inline std::time_t last_write_time(const path& p) { return 0; (void)p; }
inline void last_write_time(const path&, std::time_t, error_code& ec) { ec.clear(); }

inline path current_path() { return path(n64vfs::cwd()); }
inline path current_path(error_code& ec) { ec.clear(); return current_path(); }
inline void current_path(const path& p, error_code& ec) {
    if (n64vfs::stat(p.string()) != n64vfs::kind::dir) { ec = detail::not_found(); return; }
    ec.clear();
    n64vfs::set_cwd(p.string());
}
inline void current_path(const path& p) { error_code ec; current_path(p, ec); if (ec) detail::fail("current_path", p, ec); }

inline path absolute(const path& p, const path& base = current_path()) {
    return p.is_absolute() ? p : base / p;
}
inline path absolute(const path& p, error_code& ec) { ec.clear(); return absolute(p); }
inline path canonical(const path& p, error_code& ec) {
    std::string n = n64vfs::normalize(p.string());
    if (n64vfs::stat(n) == n64vfs::kind::none) { ec = detail::not_found(); return path(); }
    ec.clear();
    return path(n);
}
inline path canonical(const path& p) { error_code ec; auto r = canonical(p, ec); if (ec) detail::fail("canonical", p, ec); return r; }
inline path canonical(const path& p, const path& base) { return canonical(absolute(p, base)); }
inline path weakly_canonical(const path& p, error_code& ec) { ec.clear(); return path(n64vfs::normalize(p.string())); }
inline path weakly_canonical(const path& p) { return path(n64vfs::normalize(p.string())); }
inline path relative(const path& p, const path& base, error_code& ec) {
    ec.clear();
    return weakly_canonical(p).lexically_relative(weakly_canonical(base));
}
inline path relative(const path& p, const path& base = current_path()) { error_code ec; return relative(p, base, ec); }
inline bool equivalent(const path& a, const path& b, error_code& ec) {
    ec.clear();
    return n64vfs::normalize(a.string()) == n64vfs::normalize(b.string()) && exists(a);
}
inline bool equivalent(const path& a, const path& b) { error_code ec; return equivalent(a, b, ec); }

inline bool create_directory(const path& p, error_code& ec) {
    ec.clear();
    if (n64vfs::stat(p.string()) == n64vfs::kind::dir) return false;
    if (!n64vfs::make_dir(p.string())) { ec = std::make_error_code(std::errc::permission_denied); return false; }
    return true;
}
inline bool create_directory(const path& p) { error_code ec; bool r = create_directory(p, ec); if (ec) detail::fail("create_directory", p, ec); return r; }
inline bool create_directories(const path& p, error_code& ec) {
    ec.clear();
    if (p.empty()) return false;
    if (n64vfs::stat(p.string()) == n64vfs::kind::dir) return false;
    path parent = path(n64vfs::normalize(p.string())).parent_path();
    if (!parent.empty() && n64vfs::stat(parent.string()) == n64vfs::kind::none) {
        create_directories(parent, ec);
        if (ec) return false;
    }
    return create_directory(p, ec);
}
inline bool create_directories(const path& p) { error_code ec; bool r = create_directories(p, ec); if (ec) detail::fail("create_directories", p, ec); return r; }
inline bool remove(const path& p, error_code& ec) {
    ec.clear();
    if (n64vfs::stat(p.string()) == n64vfs::kind::none) return false;
    if (!n64vfs::remove(p.string())) { ec = std::make_error_code(std::errc::permission_denied); return false; }
    return true;
}
inline bool remove(const path& p) { error_code ec; bool r = remove(p, ec); if (ec) detail::fail("remove", p, ec); return r; }
inline std::uintmax_t remove_all(const path& p, error_code& ec) {
    ec.clear();
    std::uintmax_t n = 0;
    if (n64vfs::stat(p.string()) == n64vfs::kind::dir) {
        std::vector<std::pair<std::string, bool>> kids;
        n64vfs::list(p.string(), kids);
        for (auto& k : kids) n += remove_all(p / k.first, ec);
    }
    if (remove(p, ec)) ++n;
    return n;
}
inline std::uintmax_t remove_all(const path& p) { error_code ec; return remove_all(p, ec); }
inline void rename(const path& a, const path& b, error_code& ec) {
    ec.clear();
    if (!n64vfs::rename(a.string(), b.string())) ec = std::make_error_code(std::errc::permission_denied);
}
inline void rename(const path& a, const path& b) { error_code ec; rename(a, b, ec); if (ec) throw filesystem_error("rename", a, b, ec); }
inline bool copy_file(const path& a, const path& b, error_code& ec) {
    std::string data;
    if (!n64vfs::read(a.string(), data)) { ec = detail::not_found(); return false; }
    if (!n64vfs::write(b.string(), data, false)) { ec = std::make_error_code(std::errc::permission_denied); return false; }
    ec.clear();
    return true;
}
inline bool copy_file(const path& a, const path& b) { error_code ec; bool r = copy_file(a, b, ec); if (ec) throw filesystem_error("copy_file", a, b, ec); return r; }
inline void permissions(const path& p, perms, error_code& ec) { status(p, ec); }
inline void permissions(const path& p, perms pr) { error_code ec; permissions(p, pr, ec); }
inline path read_symlink(const path&, error_code& ec) { ec = std::make_error_code(std::errc::function_not_supported); return path(); }
inline path read_symlink(const path& p) { error_code ec; return read_symlink(p, ec); }
inline void create_symlink(const path&, const path&, error_code& ec) { ec = std::make_error_code(std::errc::function_not_supported); }
inline void create_symlink(const path& a, const path& b) { error_code ec; create_symlink(a, b, ec); throw filesystem_error("create_symlink", a, b, ec); }
inline path temp_directory_path() { return path("/save/tmp"); }
inline path temp_directory_path(error_code& ec) { ec.clear(); return temp_directory_path(); }
inline path unique_path(const path& model = path("%%%%-%%%%-%%%%-%%%%")) {
    static unsigned counter = 0;
    std::string s = model.string();
    unsigned v = ++counter * 2654435761u;
    for (char& c : s) if (c == '%') { c = "0123456789abcdef"[v & 15]; v = (v >> 4) | (v << 28); }
    return path(s);
}

// -----------------------------------------------------------------------
// directory iteration
// -----------------------------------------------------------------------
class directory_entry {
public:
    directory_entry() = default;
    explicit directory_entry(const boost::filesystem::path& p, file_type t = status_error) : p_(p), t_(t) {}
    const boost::filesystem::path& path() const { return p_; }
    operator const boost::filesystem::path&() const { return p_; }
    file_status status() const { return t_ == status_error ? boost::filesystem::status(p_) : file_status(t_); }
    file_status status(error_code& ec) const { ec.clear(); return status(); }
    file_status symlink_status() const { return status(); }
    file_status symlink_status(error_code& ec) const { return status(ec); }
    bool is_directory() const { return status().type() == directory_file; }
    bool is_regular_file() const { return status().type() == regular_file; }
    bool exists() const { return boost::filesystem::exists(status()); }
private:
    boost::filesystem::path p_;
    file_type t_ = status_error;
};

class directory_iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = directory_entry;
    using difference_type = std::ptrdiff_t;
    using pointer = const directory_entry*;
    using reference = const directory_entry&;

    directory_iterator() = default;                       // end
    explicit directory_iterator(const path& p) { error_code ec; open(p, ec); if (ec) detail::fail("directory_iterator", p, ec); }
    directory_iterator(const path& p, error_code& ec) { open(p, ec); }

    reference operator*() const  { return (*items_)[i_]; }
    pointer operator->() const   { return &(*items_)[i_]; }
    directory_iterator& operator++() { if (items_ && ++i_ >= items_->size()) items_.reset(); return *this; }
    directory_iterator& increment(error_code& ec) { ec.clear(); return ++*this; }
    friend bool operator==(const directory_iterator& a, const directory_iterator& b) {
        return (!a.items_ && !b.items_) || (a.items_ == b.items_ && a.i_ == b.i_);
    }
    friend bool operator!=(const directory_iterator& a, const directory_iterator& b) { return !(a == b); }
    friend directory_iterator begin(directory_iterator it) { return it; }
    friend directory_iterator end(const directory_iterator&) { return directory_iterator(); }

private:
    void open(const path& p, error_code& ec) {
        std::vector<std::pair<std::string, bool>> kids;
        if (!n64vfs::list(p.string(), kids)) {
            ec = n64vfs::stat(p.string()) == n64vfs::kind::none ? detail::not_found()
                                                                : std::make_error_code(std::errc::not_a_directory);
            return;
        }
        ec.clear();
        if (kids.empty()) return;
        items_ = std::make_shared<std::vector<directory_entry>>();
        for (auto& k : kids) items_->emplace_back(p / k.first, k.second ? directory_file : regular_file);
    }
    std::shared_ptr<std::vector<directory_entry>> items_;
    size_t i_ = 0;
};

class recursive_directory_iterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type = directory_entry;
    using difference_type = std::ptrdiff_t;
    using pointer = const directory_entry*;
    using reference = const directory_entry&;

    recursive_directory_iterator() = default;
    explicit recursive_directory_iterator(const path& p) { error_code ec; init(p, ec); if (ec) detail::fail("recursive_directory_iterator", p, ec); }
    recursive_directory_iterator(const path& p, error_code& ec) { init(p, ec); }

    reference operator*() const { return (*items_)[i_]; }
    pointer operator->() const  { return &(*items_)[i_]; }
    recursive_directory_iterator& operator++() { if (items_ && ++i_ >= items_->size()) items_.reset(); return *this; }
    recursive_directory_iterator& increment(error_code& ec) { ec.clear(); return ++*this; }
    friend bool operator==(const recursive_directory_iterator& a, const recursive_directory_iterator& b) {
        return (!a.items_ && !b.items_) || (a.items_ == b.items_ && a.i_ == b.i_);
    }
    friend bool operator!=(const recursive_directory_iterator& a, const recursive_directory_iterator& b) { return !(a == b); }

private:
    // Pre-order listing of the whole subtree.
    void walk(const path& p) {
        std::vector<std::pair<std::string, bool>> kids;
        if (!n64vfs::list(p.string(), kids)) return;
        for (auto& k : kids) {
            items_->emplace_back(p / k.first, k.second ? directory_file : regular_file);
            if (k.second) walk(p / k.first);
        }
    }
    void init(const path& p, error_code& ec) {
        if (n64vfs::stat(p.string()) != n64vfs::kind::dir) { ec = detail::not_found(); return; }
        ec.clear();
        items_ = std::make_shared<std::vector<directory_entry>>();
        walk(p);
        if (items_->empty()) items_.reset();
    }
    std::shared_ptr<std::vector<directory_entry>> items_;
    size_t i_ = 0;
};

// -----------------------------------------------------------------------
// fstreams (whole-file, through the VFS)
// -----------------------------------------------------------------------
class ifstream : public std::istream {
public:
    // Like std::ifstream, a closed stream has a (empty) buffer, so its state
    // is good until it is read; clear() never adds badbit.
    ifstream() : std::istream(nullptr), buf_(std::make_unique<n64vfs::istreambuf>(nullptr)) { rdbuf(buf_.get()); }
    explicit ifstream(const path& p, std::ios_base::openmode m = std::ios_base::in) : ifstream() { open(p, m); }
    void open(const path& p, std::ios_base::openmode = std::ios_base::in) {
        auto r = n64vfs::open_read(p.string());
        if (r) { buf_ = std::make_unique<n64vfs::istreambuf>(std::move(r)); rdbuf(buf_.get()); clear(); }
        else setstate(std::ios_base::failbit);
    }
    bool is_open() const { return buf_ && buf_->is_open(); }
    void close() { buf_ = std::make_unique<n64vfs::istreambuf>(nullptr); rdbuf(buf_.get()); }
private:
    std::unique_ptr<n64vfs::istreambuf> buf_;
};

class ofstream : public std::ostream {
public:
    ofstream() : std::ostream(&buf_) { setstate(std::ios_base::failbit); }
    explicit ofstream(const path& p, std::ios_base::openmode m = std::ios_base::out) : std::ostream(&buf_) { open(p, m); }
    ~ofstream() override { close(); }
    void open(const path& p, std::ios_base::openmode m = std::ios_base::out) {
        append_ = (m & std::ios_base::app) != 0;
        if (n64vfs::write(p.string(), std::string(), append_)) { p_ = p; open_ = true; clear(); }
        else setstate(std::ios_base::failbit);
    }
    bool is_open() const { return open_; }
    void close() {
        if (!open_) return;
        n64vfs::write(p_.string(), buf_.str(), append_);
        open_ = false;
    }
private:
    std::stringbuf buf_;
    path p_;
    bool open_ = false, append_ = false;
};

} // namespace filesystem

// -----------------------------------------------------------------------
// boost::iostreams file devices (used by filesystem.cpp's istream_file and
// ostream_file) over the VFS.
// -----------------------------------------------------------------------
namespace iostreams {

#define BOOST_IOSTREAMS_FAILURE std::ios_base::failure

template<class Device> class stream;

class file_descriptor_source {
public:
    file_descriptor_source() = default;
    file_descriptor_source(const filesystem::path& p, std::ios_base::openmode = std::ios_base::in) {
        auto r = n64vfs::open_read(p.string());
        if (!r) throw BOOST_IOSTREAMS_FAILURE("cannot open " + p.string());
        reader_ = std::shared_ptr<n64vfs::reader>(std::move(r));
    }
    bool is_open() const { return static_cast<bool>(reader_); }
    // The stream built from this device takes the reader (devices are copied).
private:
    std::shared_ptr<n64vfs::reader> reader_;
    friend class stream<file_descriptor_source>;
};

class file_descriptor_sink {
public:
    file_descriptor_sink() = default;
    file_descriptor_sink(const filesystem::path& p, std::ios_base::openmode m = std::ios_base::out)
        : path_(p.string()), append_((m & std::ios_base::app) != 0) {
        if (!n64vfs::write(path_, std::string(), append_))
            throw BOOST_IOSTREAMS_FAILURE("cannot open " + path_ + " for writing");
    }
    bool is_open() const { return !path_.empty(); }
    const std::string& path() const { return path_; }
    bool append() const { return append_; }
private:
    std::string path_;
    bool append_ = false;
};


template<> class stream<file_descriptor_source> : public std::istream {
public:
    stream(const file_descriptor_source& d, std::streamsize = 0, std::streamsize = 0)
        : std::istream(nullptr), keep_(d.reader_) {
        buf_ = std::make_unique<n64vfs::istreambuf>(
            keep_ ? std::make_unique<shared_reader>(keep_) : std::unique_ptr<n64vfs::reader>());
        rdbuf(buf_.get());
        if (!keep_) setstate(std::ios_base::failbit);
    }
private:
    // Forwards to the device's reader (the device keeps it alive too).
    struct shared_reader : n64vfs::reader {
        explicit shared_reader(std::shared_ptr<n64vfs::reader> r) : r_(std::move(r)) {}
        std::size_t read(void* d, std::size_t n) override { return r_->read(d, n); }
        bool seek(std::uint64_t pos) override { return r_->seek(pos); }
        std::uint64_t tell() const override { return r_->tell(); }
        std::uint64_t size() const override { return r_->size(); }
        std::shared_ptr<n64vfs::reader> r_;
    };
    std::shared_ptr<n64vfs::reader> keep_;
    std::unique_ptr<n64vfs::istreambuf> buf_;
};

template<> class stream<file_descriptor_sink> : public std::ostream {
public:
    stream(const file_descriptor_sink& d, std::streamsize = 0, std::streamsize = 0)
        : std::ostream(&buf_), dev_(d) {}
    ~stream() override { n64vfs::write(dev_.path(), buf_.str(), dev_.append()); }
private:
    std::stringbuf buf_;
    file_descriptor_sink dev_;
};

} // namespace iostreams
} // namespace boost


