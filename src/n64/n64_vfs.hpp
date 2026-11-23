#pragma once
/**
 * n64_vfs.hpp -- the console's filesystem, as seen by the engine.
 *
 *   /rom   the game's data tree (data/, images/, sounds/, fonts/), read-only.
 *   /save  writable user data (preferences, saves, cache).
 *
 * Two backends:
 *
 *   SD card (SummerCart64): /rom is the directory "wesnoth" on the card --
 *   an unmodified copy of the game's data tree -- and /save is the directory
 *   "wesnoth-userdata", so saves and preferences persist. Directory listings
 *   are cached in RAM (Wesnoth probes many paths per image); file reads stream.
 *
 *   Cartridge ROM (no card, or no "wesnoth" directory on it): /rom is the
 *   ROMFS built by tools/n64-asset-pipeline --vfs (exact paths, one listing
 *   entry "vfs:<dir>" per directory), and /save lives in RAM only.
 *
 * Wesnoth's own src/filesystem.cpp runs on top of this through the
 * boost::filesystem / boost::iostreams implementation in n64_bfs_shim.hpp, so
 * its path resolution, binary paths and the WML preprocessor's directory
 * walks are the engine's, unmodified. The engine is started with
 * --data-dir /rom --userdata-dir /save (see src/n64/n64_main.cpp).
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

namespace n64vfs {

enum class kind { none, file, dir };

// Mount the SD card if present. Call once at start-up; without it (or
// without a card) the ROM backend is used. Returns true if /rom is the card.
bool init();
bool on_sd_card();

// Absolute, '/'-separated, without "." / ".." / duplicate separators.
std::string normalize(const std::string& path);

// Current directory (the engine may change it; always absolute).
const std::string& cwd();
void set_cwd(const std::string& path);

kind stat(const std::string& path, std::uint64_t* size = nullptr);

// Children of a directory: (name, is_directory). False if not a directory.
bool list(const std::string& dir, std::vector<std::pair<std::string, bool>>& out);

// Sequential/random-access reading.
class reader {
public:
    virtual ~reader() = default;
    virtual std::size_t read(void* dst, std::size_t n) = 0;   // bytes read; 0 at end
    virtual bool seek(std::uint64_t pos) = 0;
    virtual std::uint64_t tell() const = 0;
    virtual std::uint64_t size() const = 0;
};
std::unique_ptr<reader> open_read(const std::string& path);

// Whole-file I/O.
bool read(const std::string& path, std::string& out);
bool write(const std::string& path, const std::string& data, bool append);

bool make_dir(const std::string& path);        // parent must exist; true if created
bool remove(const std::string& path);           // file or empty directory
bool rename(const std::string& from, const std::string& to);

// std::streambuf over a reader (buffered, seekable).
class istreambuf : public std::streambuf {
public:
    explicit istreambuf(std::unique_ptr<reader> r) : r_(std::move(r)) {}
    bool is_open() const { return static_cast<bool>(r_); }
protected:
    int_type underflow() override;
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override;
    pos_type seekpos(pos_type pos, std::ios_base::openmode) override;
    std::streamsize showmanyc() override;
    std::streamsize xsgetn(char* s, std::streamsize n) override;
private:
    std::unique_ptr<reader> r_;
    char buf_[4096];
    std::uint64_t buf_pos_ = 0;     // file position of buf_[0]
};

} // namespace n64vfs
