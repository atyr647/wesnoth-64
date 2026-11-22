/**
 * n64_filesystem.cpp
 *
 * N64 filesystem adapter.
 *
 * Wesnoth's filesystem.cpp uses boost::filesystem and POSIX I/O.
 * On N64, all data comes from the ROM filesystem (n64_romfs).
 *
 * This file provides N64-specific overrides for the filesystem functions
 * that are actually called during gameplay. Functions related to writing
 * (save files, preferences) are handled by n64_save.cpp.
 *
 * Functions related to the PC filesystem (user data dir, file modification
 * times, directory scanning, etc.) are stubbed to return empty/false.
 */

#ifdef N64_BUILD

#include "n64_romfs.hpp"
#include "n64_platform.hpp"
#include "n64_binary_wml.hpp"
#include "n64_memory.hpp"

#include "../filesystem.hpp"
#include "../config.hpp"
#include "../utils/optional_fwd.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace filesystem {

// -----------------------------------------------------------------------
// Paths — on N64 these are all fixed / empty
// -----------------------------------------------------------------------
// User data/cache locations as set by wesnoth.cpp at start-up (the real
// filesystem.cpp keeps the same state). Persistent user data lives in cart
// save memory; these are logical paths within it.
static std::string s_user_data_dir;
static std::string s_cache_dir;
bool is_userdata_initialized()                      { return !s_user_data_dir.empty(); }
void set_user_data_dir(std::string newprefdir)      { s_user_data_dir = newprefdir.empty() ? "/userdata" : newprefdir; }
void set_cache_dir(const std::string& newcachedir)  { s_cache_dir = newcachedir; }

std::string get_user_config_dir()    { return s_user_data_dir; }
std::string get_user_data_dir()      { return s_user_data_dir; }
std::string get_saves_dir()          { return ""; }
std::string get_addons_dir()         { return ""; }
std::string get_logs_dir()           { return ""; }
std::string get_cache_dir()          { return s_cache_dir; }
utils::optional<std::string> get_wml_location(const std::string& path, const utils::optional<std::string>&) {
    return path; // resolved via ROMFS
}
utils::optional<std::string> get_binary_dir_location(const std::string&, const std::string& f) {
    return f;
}
utils::optional<std::string> get_binary_file_location(const std::string&, const std::string& f) {
    return f;
}
utils::optional<std::string> get_game_manual_file(const std::string&) {
    return utils::nullopt;
}
const std::string& get_version_path_suffix() {
    static const std::string v = "";
    return v;
}
std::vector<other_version_dir> find_other_version_saves_dirs() {
    return {};
}

// Extensions & globals
const std::string wml_extension = ".cfg";
const std::string map_extension = ".map";
const blacklist_pattern_list default_blacklist;

// -----------------------------------------------------------------------
// File existence — check ROM filesystem
// -----------------------------------------------------------------------
bool file_exists(const std::string& path) {
    return n64_romfs_find_wesnoth(path.c_str()) != nullptr;
}

bool is_directory(const std::string&) { return false; }

std::chrono::system_clock::time_point file_modified_time(const std::string&) { return std::chrono::system_clock::time_point(); }

int file_size(const std::string& path) {
    const RomFsEntry* e = n64_romfs_find_wesnoth(path.c_str());
    return e ? (int)e->byte_length : -1;
}

// -----------------------------------------------------------------------
// File reading — read from ROM
// -----------------------------------------------------------------------
std::string read_file(const std::string& path) {
    const RomFsEntry* e = n64_romfs_find_wesnoth(path.c_str());
    if (!e) return "";

    // DMA from ROM into arena, then copy to string
    u8* buf = static_cast<u8*>(g_arena_wml.alloc(e->byte_length, 8));
#ifdef N64_BUILD
    data_cache_hit_writeback_invalidate(buf, e->byte_length);
    dma_read(buf, e->rom_offset, e->byte_length);
#endif
    return std::string(reinterpret_cast<char*>(buf), e->byte_length);
}

// -----------------------------------------------------------------------
// Scoped istream — reads from ROM via string
// -----------------------------------------------------------------------
class rom_istream : public std::istringstream {
public:
    rom_istream(const std::string& data) : std::istringstream(data) {}
};

std::unique_ptr<std::istream> istream_file(const std::string& path, bool) {
    std::string data = read_file(path);
    return std::make_unique<rom_istream>(data);
}

// -----------------------------------------------------------------------
// File writing — redirected to SRAM via n64_save, or no-op
// -----------------------------------------------------------------------
void write_file(const std::string& path, const std::string&, std::ios_base::openmode) {
    N64_LOG("filesystem: write_file ignored on N64: %s", path.c_str());
}

scoped_ostream ostream_file(const std::string& path, std::ios_base::openmode, bool) {
    N64_LOG("filesystem: ostream_file ignored on N64: %s", path.c_str());
    return std::make_unique<std::ostringstream>();
}

// -----------------------------------------------------------------------
// Directory operations — stubbed
// -----------------------------------------------------------------------
void get_files_in_dir(const std::string&,
                       std::vector<std::string>*,
                       std::vector<std::string>*,
                       name_mode,
                       filter_mode,
                       reorder_mode,
                       file_tree_checksum*) {
    // No-op on N64
}

bool make_directory(const std::string&) { return false; }
bool delete_file(const std::string&)    { return false; }
bool delete_directory(const std::string&, bool) { return false; }
void copy_file(const std::string&, const std::string&) { /* no-op on N64 */ }

std::string get_exe_dir()       { return ""; }
std::string get_cwd()           { return ""; }
std::string get_dir(const std::string& d) { return d; }
std::string sanitize_path(const std::string& p) { return p; }
std::string get_next_filename(const std::string& name, const std::string& ext) { return name + ext; }
std::string normalize_path(const std::string& p, bool, bool) { return p; }
bool is_path_sep(char c)        { return c == '/'; }
char path_separator()           { return '/'; }
bool is_cfg(const std::string& f) {
    return f.size() >= 4 && f.compare(f.size()-4, 4, ".cfg") == 0;
}
bool is_gzip_file(const std::string& f) {
    return f.size() >= 3 && f.compare(f.size()-3, 3, ".gz") == 0;
}
bool is_bzip2_file(const std::string& f) {
    return f.size() >= 4 && f.compare(f.size()-4, 4, ".bz2") == 0;
}
bool is_root(const std::string& p) {
    return p == "/" || p.empty();
}
std::string root_name(const std::string&) {
    return "/";
}

std::string base_name(const std::string& path, bool) {
    auto pos = path.rfind('/');
    return pos != std::string::npos ? path.substr(pos + 1) : path;
}

std::string directory_name(const std::string& path) {
    auto pos = path.rfind('/');
    return pos != std::string::npos ? path.substr(0, pos) : "";
}

std::string nearest_extant_parent(const std::string& p) { return p; }

bool is_relative(const std::string& p) {
    return p.empty() || p[0] != '/';
}

// binary_paths_manager stub implementation
binary_paths_manager::binary_paths_manager(const game_config_view&) {}
binary_paths_manager::~binary_paths_manager() = default;
void binary_paths_manager::set_paths(const game_config_view&) {}
void binary_paths_manager::cleanup() {}

// -----------------------------------------------------------------------
// Checksum / binary paths
// -----------------------------------------------------------------------
std::string get_short_wml_path(const std::string& p) { return p; }
utils::optional<std::string> get_independent_binary_file_path(const std::string&, const std::string& f) { return f; }

// -----------------------------------------------------------------------
// File tree checksum (used for cache validation; trivial on N64)
// -----------------------------------------------------------------------
const file_tree_checksum& data_tree_checksum(bool) {
    static file_tree_checksum ftc;
    return ftc;
}

} // namespace filesystem

#endif // N64_BUILD
