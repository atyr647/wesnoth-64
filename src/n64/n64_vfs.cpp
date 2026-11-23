/**
 * n64_vfs.cpp -- see n64_vfs.hpp.
 */

#include "n64_vfs.hpp"
#include "n64_romfs.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

#ifdef N64_BUILD
#  include <fatfs/ff.h>
extern "C" bool debug_init_sdfs(const char* prefix, int npart);
#endif

namespace n64vfs {

static const char kRom[]  = "/rom";
static const char kSave[] = "/save";

static std::string s_cwd = "/";

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

static std::string parent_of(const std::string& p) {
    size_t s = p.rfind('/');
    return s == 0 ? "/" : p.substr(0, s);
}

static std::string base_of(const std::string& p) {
    return p.substr(p.rfind('/') + 1);
}

// In-memory reader (ROM entries, RAM files).
class memory_reader : public reader {
public:
    explicit memory_reader(std::string data) : d_(std::move(data)) {}
    std::size_t read(void* dst, std::size_t n) override {
        n = std::min<std::size_t>(n, d_.size() - std::min<std::size_t>(pos_, d_.size()));
        std::memcpy(dst, d_.data() + pos_, n);
        pos_ += n;
        return n;
    }
    bool seek(std::uint64_t pos) override { if (pos > d_.size()) return false; pos_ = pos; return true; }
    std::uint64_t tell() const override { return pos_; }
    std::uint64_t size() const override { return d_.size(); }
private:
    std::string d_;
    std::size_t pos_ = 0;
};

// =========================================================================
// SD card backend
// =========================================================================
#ifdef N64_BUILD
static bool s_sd = false;
static const char kSdRom[]  = "/wesnoth";
static const char kSdSave[] = "/wesnoth-userdata";

// /rom/x -> /wesnoth/x, /save/x -> /wesnoth-userdata/x
static std::string sd_path(const std::string& p) {
    if (under(p, kRom)) return kSdRom + p.substr(std::strlen(kRom));
    return kSdSave + p.substr(std::strlen(kSave));
}

struct sd_entry { std::string name; bool dir; std::uint64_t size; };
struct sd_dir {
    bool exists = false;
    std::vector<sd_entry> items;                  // sorted by name
    const sd_entry* find(const std::string& name) const {
        auto it = std::lower_bound(items.begin(), items.end(), name,
            [](const sd_entry& e, const std::string& n) { return e.name < n; });
        return (it != items.end() && it->name == name) ? &*it : nullptr;
    }
};
static std::unordered_map<std::string, sd_dir> s_dir_cache;

static const sd_dir& sd_list(const std::string& sddir) {
    auto it = s_dir_cache.find(sddir);
    if (it != s_dir_cache.end()) return it->second;
    sd_dir d;
    DIR dir;
    if (f_opendir(&dir, sddir.c_str()) == FR_OK) {
        d.exists = true;
        FILINFO fno;
        while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
            d.items.push_back({ fno.fname, (fno.fattrib & AM_DIR) != 0, (std::uint64_t)fno.fsize });
        }
        f_closedir(&dir);
        std::sort(d.items.begin(), d.items.end(),
                  [](const sd_entry& a, const sd_entry& b) { return a.name < b.name; });
    }
    return s_dir_cache.emplace(sddir, std::move(d)).first->second;
}

static void sd_forget(const std::string& sdpath) {
    s_dir_cache.erase(sdpath);
    s_dir_cache.erase(parent_of(sdpath));
}

static kind sd_stat(const std::string& sdpath, std::uint64_t* size) {
    if (sdpath == kSdRom || sdpath == kSdSave) return kind::dir;
    const sd_dir& d = sd_list(parent_of(sdpath));
    const sd_entry* e = d.exists ? d.find(base_of(sdpath)) : nullptr;
    if (!e) return kind::none;
    if (size) *size = e->dir ? 0 : e->size;
    return e->dir ? kind::dir : kind::file;
}

// FatFs reads and writes whole sectors straight to the caller's buffer by
// PI DMA, which needs physical memory; the engine's buffers may live in the
// swappable (TLB-mapped) heap. Everything goes through this bounce buffer.
alignas(16) static char s_bounce[8192];

class sd_reader : public reader {
public:
    bool open(const std::string& sdpath) {
        ok_ = f_open(&f_, sdpath.c_str(), FA_READ) == FR_OK;
        return ok_;
    }
    ~sd_reader() override { if (ok_) f_close(&f_); }
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t total = 0;
        while (n > 0) {
            UINT chunk = (UINT)std::min<std::size_t>(n, sizeof s_bounce), got = 0;
            if (f_read(&f_, s_bounce, chunk, &got) != FR_OK || got == 0) break;
            std::memcpy(static_cast<char*>(dst) + total, s_bounce, got);
            total += got;
            n -= got;
            if (got < chunk) break;
        }
        return total;
    }
    bool seek(std::uint64_t pos) override { return f_lseek(&f_, (FSIZE_t)pos) == FR_OK; }
    std::uint64_t tell() const override { return f_tell(&f_); }
    std::uint64_t size() const override { return f_size(&f_); }
private:
    FIL f_;
    bool ok_ = false;
};

static bool sd_write(const std::string& sdpath, const std::string& data, bool append) {
    FIL f;
    if (f_open(&f, sdpath.c_str(), append ? (FA_WRITE | FA_OPEN_APPEND) : (FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK)
        return false;
    bool ok = true;
    std::size_t done = 0;
    while (ok && done < data.size()) {
        UINT chunk = (UINT)std::min<std::size_t>(data.size() - done, sizeof s_bounce), put = 0;
        std::memcpy(s_bounce, data.data() + done, chunk);
        ok = f_write(&f, s_bounce, chunk, &put) == FR_OK && put == chunk;
        done += put;
    }
    ok = (f_close(&f) == FR_OK) && ok;
    sd_forget(sdpath);
    return ok;
}
#endif // N64_BUILD

bool on_sd_card() {
#ifdef N64_BUILD
    return s_sd;
#else
    return false;
#endif
}

bool init() {
#ifdef N64_BUILD
    if (s_sd) return true;
    if (!debug_init_sdfs("sd:/", -1)) return false;
    FILINFO fno;
    if (f_stat("/wesnoth/data/_main.cfg", &fno) != FR_OK) return false;
    if (f_stat(kSdSave, &fno) != FR_OK) f_mkdir(kSdSave);
    s_sd = true;
    return true;
#else
    return false;
#endif
}

// =========================================================================
// ROM + RAM backend
// =========================================================================
static std::map<std::string, std::string> s_files;   // writable files under /save
static std::set<std::string> s_dirs = { "/save" };   // writable directories

// "/rom/data/core/x.cfg" -> "data/core/x.cfg";  "/rom" -> ""
static std::string rom_name(const std::string& p) {
    return p.size() > 5 ? p.substr(5) : std::string();
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

// =========================================================================
// Public interface
// =========================================================================
kind stat(const std::string& in, std::uint64_t* size) {
    const std::string p = normalize(in);
    if (size) *size = 0;
    if (p == "/") return kind::dir;
#ifdef N64_BUILD
    if (s_sd && (under(p, kRom) || under(p, kSave))) return sd_stat(sd_path(p), size);
#endif
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

bool list(const std::string& in, std::vector<std::pair<std::string, bool>>& out) {
    const std::string p = normalize(in);
    out.clear();
    if (p == "/") {
        out.push_back({ "rom", true });
        out.push_back({ "save", true });
        return true;
    }
#ifdef N64_BUILD
    if (s_sd && (under(p, kRom) || under(p, kSave))) {
        const sd_dir& d = sd_list(sd_path(p));
        if (!d.exists) return false;
        for (const auto& e : d.items) out.push_back({ e.name, e.dir });
        return true;
    }
#endif
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

std::unique_ptr<reader> open_read(const std::string& in) {
    const std::string p = normalize(in);
#ifdef N64_BUILD
    if (s_sd && (under(p, kRom) || under(p, kSave))) {
        if (sd_stat(sd_path(p), nullptr) != kind::file) return nullptr;
        auto r = std::make_unique<sd_reader>();
        if (!r->open(sd_path(p))) return nullptr;
        return r;
    }
#endif
    std::string data;
    if (under(p, kRom)) {
        const RomFsEntry* e = n64_romfs_find(rom_name(p).c_str());
        if (!e || !read_rom(e, data)) return nullptr;
    } else {
        auto it = s_files.find(p);
        if (it == s_files.end()) return nullptr;
        data = it->second;
    }
    return std::make_unique<memory_reader>(std::move(data));
}

bool read(const std::string& in, std::string& out) {
    auto r = open_read(in);
    if (!r) return false;
    out.resize((std::size_t)r->size());
    out.resize(r->read(out.data(), out.size()));
    return true;
}

bool write(const std::string& in, const std::string& data, bool append) {
    const std::string p = normalize(in);
    if (!under(p, kSave) || p == kSave) return false;
#ifdef N64_BUILD
    if (s_sd) {
        if (sd_stat(sd_path(parent_of(p)), nullptr) != kind::dir) return false;
        if (sd_stat(sd_path(p), nullptr) == kind::dir) return false;
        return sd_write(sd_path(p), data, append);
    }
#endif
    if (s_dirs.count(p) || !s_dirs.count(parent_of(p))) return false;
    if (append) s_files[p] += data; else s_files[p] = data;
    return true;
}

bool make_dir(const std::string& in) {
    const std::string p = normalize(in);
    if (!under(p, kSave) || stat(p) != kind::none) return false;
#ifdef N64_BUILD
    if (s_sd) {
        if (sd_stat(sd_path(parent_of(p)), nullptr) != kind::dir) return false;
        const bool ok = f_mkdir(sd_path(p).c_str()) == FR_OK;
        sd_forget(sd_path(p));
        return ok;
    }
#endif
    if (!s_dirs.count(parent_of(p))) return false;
    s_dirs.insert(p);
    return true;
}

bool remove(const std::string& in) {
    const std::string p = normalize(in);
    if (!under(p, kSave) || p == kSave) return false;
#ifdef N64_BUILD
    if (s_sd) {
        const bool ok = f_unlink(sd_path(p).c_str()) == FR_OK;   // fails on non-empty dirs
        sd_forget(sd_path(p));
        return ok;
    }
#endif
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
#ifdef N64_BUILD
    if (s_sd) {
        if (sd_stat(sd_path(to), nullptr) == kind::file) f_unlink(sd_path(to).c_str());
        const bool ok = f_rename(sd_path(from).c_str(), sd_path(to).c_str()) == FR_OK;
        sd_forget(sd_path(from));
        sd_forget(sd_path(to));
        return ok;
    }
#endif
    auto it = s_files.find(from);
    if (it == s_files.end() || !s_dirs.count(parent_of(to))) return false;
    s_files[to] = std::move(it->second);
    s_files.erase(from);
    return true;
}

// -------------------------------------------------------------------------
// istreambuf
// -------------------------------------------------------------------------
istreambuf::int_type istreambuf::underflow() {
    if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
    if (!r_) return traits_type::eof();
    buf_pos_ = r_->tell();
    const std::size_t n = r_->read(buf_, sizeof buf_);
    setg(buf_, buf_, buf_ + n);
    return n ? traits_type::to_int_type(*gptr()) : traits_type::eof();
}

istreambuf::pos_type istreambuf::seekpos(pos_type pos, std::ios_base::openmode) {
    if (!r_ || pos < 0 || (std::uint64_t)pos > r_->size()) return pos_type(off_type(-1));
    const std::uint64_t target = (std::uint64_t)pos;
    if (eback() && target >= buf_pos_ && target <= buf_pos_ + (egptr() - eback())) {
        setg(eback(), eback() + (target - buf_pos_), egptr());
    } else {
        if (!r_->seek(target)) return pos_type(off_type(-1));
        buf_pos_ = target;
        setg(buf_, buf_, buf_);
    }
    return pos;
}

istreambuf::pos_type istreambuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode m) {
    if (!r_) return pos_type(off_type(-1));
    const std::int64_t cur = (std::int64_t)buf_pos_ + (eback() ? (gptr() - eback()) : 0);
    std::int64_t base = dir == std::ios_base::beg ? 0 : dir == std::ios_base::cur ? cur : (std::int64_t)r_->size();
    return seekpos(pos_type(off_type(base + off)), m);
}

std::streamsize istreambuf::showmanyc() {
    if (!r_) return -1;
    const std::int64_t cur = (std::int64_t)buf_pos_ + (eback() ? (gptr() - eback()) : 0);
    const std::int64_t left = (std::int64_t)r_->size() - cur;
    return left > 0 ? left : -1;
}

std::streamsize istreambuf::xsgetn(char* s, std::streamsize n) {
    std::streamsize done = 0;
    while (done < n) {
        std::streamsize avail = egptr() - gptr();
        if (avail > 0) {
            const std::streamsize k = std::min(avail, n - done);
            std::memcpy(s + done, gptr(), (std::size_t)k);
            gbump((int)k);
            done += k;
            continue;
        }
        if (!r_) break;
        if (n - done >= (std::streamsize)sizeof buf_) {   // large read: bypass the buffer
            const std::size_t k = r_->read(s + done, (std::size_t)(n - done));
            buf_pos_ = r_->tell();
            setg(buf_, buf_, buf_);
            if (!k) break;
            done += (std::streamsize)k;
            continue;
        }
        if (underflow() == traits_type::eof()) break;
    }
    return done;
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
