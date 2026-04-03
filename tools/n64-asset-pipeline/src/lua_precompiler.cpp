/**
 * lua_precompiler.cpp
 *
 * Pre-compiles Wesnoth Lua scripts to Lua 5.4 bytecode using luac.
 *
 * Pre-compiled bytecode:
 *   - Loads faster on N64 (skips tokenization/parsing at runtime)
 *   - Is portable across identical Lua versions
 *   - Saves ~30-50% RAM vs source text for large scripts
 *
 * luac must be in PATH or pointed to by LUAC_PATH environment variable.
 * It must match the Lua version embedded in Wesnoth (5.4.x).
 */

#include "pipeline.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>

#ifdef _WIN32
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  include <unistd.h>
#  include <sys/stat.h>
#  define POPEN  popen
#  define PCLOSE pclose
#endif

namespace n64pack {

// -----------------------------------------------------------------------
// Find luac executable
// -----------------------------------------------------------------------
static std::string find_luac() {
    const char* env = std::getenv("LUAC_PATH");
    if (env && *env) return env;

    // Try common locations
    const char* candidates[] = {
        "luac5.4", "luac54", "luac",
        "/usr/bin/luac5.4", "/usr/local/bin/luac5.4",
        nullptr
    };
    for (const char** c = candidates; *c; ++c) {
        // Check if it exists and is executable
        std::string cmd = std::string(*c) + " -v 2>&1";
        FILE* fp = POPEN(cmd.c_str(), "r");
        if (!fp) continue;
        char buf[64] = {};
        std::fread(buf, 1, sizeof(buf) - 1, fp);
        PCLOSE(fp);
        // Check it says "Lua 5.4"
        if (std::strstr(buf, "Lua 5.4") || std::strstr(buf, "Lua5.4"))
            return *c;
    }
    return "";
}

static std::string s_luac_path;
static bool        s_luac_checked = false;

// -----------------------------------------------------------------------
// compile_lua — compile a single .lua file to bytecode
// -----------------------------------------------------------------------
std::vector<uint8_t> compile_lua(const std::filesystem::path& lua_path) {
    // Find luac once
    if (!s_luac_checked) {
        s_luac_path    = find_luac();
        s_luac_checked = true;
        if (s_luac_path.empty())
            std::fprintf(stderr, "Warning: luac5.4 not found; Lua files will be included as source text.\n");
    }

    if (s_luac_path.empty()) {
        // Fall back: include raw source text (Lua can interpret source too)
        std::ifstream f(lua_path, std::ios::binary | std::ios::ate);
        if (!f) return {};
        auto sz = f.tellg(); f.seekg(0);
        std::vector<uint8_t> data(sz);
        f.read(reinterpret_cast<char*>(data.data()), sz);
        return data;
    }

    // Create a temp output file
    char tmp_out[] = "/tmp/wesnoth_n64_lua_XXXXXX";
#ifdef _WIN32
    char tmp_buf[256];
    std::snprintf(tmp_buf, sizeof(tmp_buf), "wesnoth_n64_%u.luac",
                  (unsigned)std::hash<std::string>{}(lua_path.string()));
    std::string tmp_path = tmp_buf;
#else
    int fd = mkstemp(tmp_out);
    if (fd >= 0) close(fd);
    std::string tmp_path = tmp_out;
#endif

    // Run luac
    // -s : strip debug information (saves ROM space)
    // -o : output file
    std::string cmd = s_luac_path + " -s -o \"" + tmp_path + "\" \"" +
                      lua_path.string() + "\" 2>&1";

    FILE* fp = POPEN(cmd.c_str(), "r");
    if (fp) {
        char err[256] = {};
        if (std::fread(err, 1, sizeof(err) - 1, fp) > 0)
            std::fprintf(stderr, "  luac: %s: %s\n", lua_path.filename().c_str(), err);
        PCLOSE(fp);
    }

    // Read compiled bytecode
    std::ifstream f(tmp_path, std::ios::binary | std::ios::ate);
    if (!f) {
        // Compilation failed — fall back to source
        std::fprintf(stderr, "  luac failed for %s, including source\n",
                     lua_path.c_str());
        std::remove(tmp_path.c_str());
        std::ifstream src(lua_path, std::ios::binary | std::ios::ate);
        if (!src) return {};
        auto sz = src.tellg(); src.seekg(0);
        std::vector<uint8_t> data(sz);
        src.read(reinterpret_cast<char*>(data.data()), sz);
        return data;
    }

    auto sz = f.tellg(); f.seekg(0);
    std::vector<uint8_t> data(sz);
    f.read(reinterpret_cast<char*>(data.data()), sz);
    f.close();
    std::remove(tmp_path.c_str());
    return data;
}

} // namespace n64pack
