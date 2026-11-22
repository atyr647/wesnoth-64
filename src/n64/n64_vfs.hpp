#pragma once
/**
 * n64_vfs.hpp -- the console's filesystem, as seen by the engine.
 *
 *   /rom   read-only data tree from the cartridge (data/, images/, sounds/),
 *          stored by tools/n64-asset-pipeline --vfs under exact paths with a
 *          listing entry ("vfs:<dir>") per directory.
 *   /save  writable user data (preferences, saves). Kept in RAM; files are
 *          real files to the engine but do not yet persist across power-off.
 *
 * Wesnoth's own src/filesystem.cpp runs on top of this through the
 * boost::filesystem / boost::iostreams implementation in n64_bfs_shim.hpp, so
 * its path resolution, binary paths and the WML preprocessor's directory
 * walks are the engine's, unmodified. The engine is started with
 * --data-dir /rom --userdata-dir /save (see src/n64/n64_main.cpp).
 */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace n64vfs {

enum class kind { none, file, dir };

// Absolute, '/'-separated, without "." / ".." / duplicate separators.
std::string normalize(const std::string& path);

// Current directory (the engine may change it; always absolute).
const std::string& cwd();
void set_cwd(const std::string& path);

kind stat(const std::string& path, std::uint64_t* size = nullptr);

// Children of a directory: (name, is_directory). False if not a directory.
bool list(const std::string& dir, std::vector<std::pair<std::string, bool>>& out);

// Whole-file I/O.
bool read(const std::string& path, std::string& out);
bool write(const std::string& path, const std::string& data, bool append);

bool make_dir(const std::string& path);        // parent must exist; true if created
bool remove(const std::string& path);           // file or empty directory
bool rename(const std::string& from, const std::string& to);

} // namespace n64vfs
