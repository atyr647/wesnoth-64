/**
 * n64_vfs.cpp -- see n64_vfs.hpp.
 */

#include "n64_vfs.hpp"
#include "n64_romfs.hpp"

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace n64vfs {

static const char kRom[]  = "/rom";
static const char kSave[] = "/save";

static std::string s_cwd = "/";
static std::map<std::string, std::string> s_files;   // writable files under /save
static std::set<std::string> s_dirs = { "/save" };   // writable directories

const std::string& cwd() { return s_cwd; }

std::string normalize(const std::string& in) {
    std::string path = (!in.empty() && in[0] == '/') ? in : s_cwd + "/" + in;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        std::string seg = path.substr(i, j - i);
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        i = j + 1;
    }
    std::string out;
    for (auto& p : parts) out += "/" + p;
    return out.empty() ? "/" : out;
}

void set_cwd(const std::string& path) { s_cwd = normalize(path); }

static bool under(const std::string& p, const char* root) {
    size_t n = std::strlen(root);
    return p.compare(0, n, root) == 0 && (p.size() == n || p[n] == '/');
}

// "/rom/data/core/x.cfg" -> "data/core/x.cfg";  "/rom" -> ""
static std::string rom_name(const std::string& p) {
    return p.size() > 5 ? p.substr(5) : std::string();
}

kind stat(const std::string& in, std::uint64_t* size) {
    const std::string p = normalize(in);
    if (size) *size = 0;
    if (p == "/") return kind::dir;
    if (under(p, kRom)) {
        const std::string name = rom_name(p);
        if (n64_romfs_find(("vfs:" + name).c_str())) return kind::dir;
        if (name.empty()) return kind::none;
        const RomFsEntry* e = n64_romfs_find(name.c_str());
        if (!e) return kind::none;
        if (size) *size = n64_romfs_size(e);
        return kind::file;
    }
    if (s_dirs.count(p)) return kind::dir;
    auto it = s_files.find(p);
    if (it != s_files.end()) { if (size) *size = it->second.size(); return kind::file; }
    return kind::none;
}

static bool read_rom(const RomFsEntry* e, std::string& out) {
    const u32 cap = n64_romfs_read_capacity(e);
    // 16-byte aligned scratch (DMA target), then copy out.
    void* raw = std::malloc(cap + 16);
    if (!raw) return false;
    u8* buf = reinterpret_cast<u8*>((reinterpret_cast<uintptr_t>(raw) + 15) & ~uintptr_t(15));
    u32 n = n64_romfs_read(e, buf, cap);
    if (n || !n64_romfs_size(e)) out.assign(reinterpret_cast<char*>(buf), n);
    std::free(raw);
    return n || !n64_romfs_size(e);
}

bool list(const std::string& in, std::vector<std::pair<std::string, bool>>& out) {
    const std::string p = normalize(in);
    out.clear();
    if (p == "/") {
        out.push_back({ "rom", true });
        out.push_back({ "save", true });
        return true;
    }
    if (under(p, kRom)) {
        const RomFsEntry* e = n64_romfs_find(("vfs:" + rom_name(p)).c_str());
        std::string text;
        if (!e || !read_rom(e, text)) return false;
        size_t i = 0;
        while (i < text.size()) {
            size_t nl = text.find('\n', i);
            if (nl == std::string::npos) nl = text.size();
            if (nl > i + 2) out.push_back({ text.substr(i + 2, nl - i - 2), text[i] == 'd' });
            i = nl + 1;
        }
        return true;
    }
    if (!s_dirs.count(p)) return false;
    const std::string prefix = p + "/";
    for (auto& d : s_dirs)
        if (d.compare(0, prefix.size(), prefix) == 0 && d.find('/', prefix.size()) == std::string::npos)
            out.push_back({ d.substr(prefix.size()), true });
    for (auto& f : s_files)
        if (f.first.compare(0, prefix.size(), prefix) == 0 && f.first.find('/', prefix.size()) == std::string::npos)
            out.push_back({ f.first.substr(prefix.size()), false });
    return true;
}

bool read(const std::string& in, std::string& out) {
    const std::string p = normalize(in);
    if (under(p, kRom)) {
        const RomFsEntry* e = n64_romfs_find(rom_name(p).c_str());
        return e && read_rom(e, out);
    }
    auto it = s_files.find(p);
    if (it == s_files.end()) return false;
    out = it->second;
    return true;
}

static std::string parent_of(const std::string& p) {
    size_t s = p.rfind('/');
    return s == 0 ? "/" : p.substr(0, s);
}

bool write(const std::string& in, const std::string& data, bool append) {
    const std::string p = normalize(in);
    if (!under(p, kSave) || p == kSave || s_dirs.count(p)) return false;
    if (!s_dirs.count(parent_of(p))) return false;
    if (append) s_files[p] += data; else s_files[p] = data;
    return true;
}

bool make_dir(const std::string& in) {
    const std::string p = normalize(in);
    if (!under(p, kSave) || stat(p) != kind::none) return false;
    if (!s_dirs.count(parent_of(p))) return false;
    s_dirs.insert(p);
    return true;
}

bool remove(const std::string& in) {
    const std::string p = normalize(in);
    if (!under(p, kSave) || p == kSave) return false;
    if (s_files.erase(p)) return true;
    if (!s_dirs.count(p)) return false;
    std::vector<std::pair<std::string, bool>> kids;
    list(p, kids);
    if (!kids.empty()) return false;
    s_dirs.erase(p);
    return true;
}

bool rename(const std::string& from_in, const std::string& to_in) {
    const std::string from = normalize(from_in), to = normalize(to_in);
    if (!under(from, kSave) || !under(to, kSave)) return false;
    auto it = s_files.find(from);
    if (it == s_files.end() || !s_dirs.count(parent_of(to))) return false;
    s_files[to] = std::move(it->second);
    s_files.erase(from);
    return true;
}

} // namespace n64vfs

// POSIX calls newlib does not provide, over the VFS (it has no permissions).
#include <cerrno>
#include <sys/types.h>
extern "C" int chmod(const char* path, mode_t) {
    if (n64vfs::stat(path) == n64vfs::kind::none) { errno = ENOENT; return -1; }
    return 0;
}
extern "C" int chdir(const char* path) {
    if (n64vfs::stat(path) != n64vfs::kind::dir) { errno = ENOENT; return -1; }
    n64vfs::set_cwd(path);
    return 0;
}
