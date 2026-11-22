/**
 * lua_precompiler.cpp
 *
 * Lua scripts are shipped to the ROM as SOURCE TEXT, not bytecode.
 *
 * Lua bytecode is not portable across targets: lundump.c's checkHeader()
 * verifies sizeof(Instruction), sizeof(lua_Integer), sizeof(lua_Number) and
 * then loads a native-endian integer (LUAC_INT) and double (LUAC_NUM) to
 * detect byte order. A luac running on an x86-64 build host emits
 * little-endian, 64-bit-integer chunks, which the big-endian N64 runtime
 * rejects with "bad binary format". There is no cross-endian luac, so the
 * N64's embedded Lua compiles the source at load time instead.
 *
 * What a host luac IS good for is catching broken scripts at pack time: if
 * luac5.4 is available every script is syntax-checked with `luac -p`, and a
 * failure is reported (the file is still packed, as the on-target parser is
 * the authority).
 */

#include "pipeline.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#  include <sys/wait.h>
#endif

namespace n64pack {

static std::string find_luac() {
    const char* env = std::getenv("LUAC_PATH");
    if (env && *env) return env;
    for (const char* c : {"luac5.4", "luac54", "luac"}) {
        std::string cmd = std::string(c) + " -v 2>&1";
        FILE* fp = popen(cmd.c_str(), "r");
        if (!fp) continue;
        char buf[64] = {};
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, fp);
        (void)n;
        pclose(fp);
        if (std::strstr(buf, "Lua 5.4")) return c;
    }
    return "";
}

static std::string shell_quote(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}

std::vector<uint8_t> compile_lua(const std::filesystem::path& lua_path) {
    static const std::string luac = find_luac();
    static bool warned = false;
    static int  syntax_errors = 0;

    std::ifstream f(lua_path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data((size_t)sz);
    f.read(reinterpret_cast<char*>(data.data()), sz);

    if (luac.empty()) {
        if (!warned) {
            std::fprintf(stderr, "Note: luac5.4 not found; Lua syntax is not checked at pack time.\n");
            warned = true;
        }
        return data;
    }

    // -p: parse only, no output file.
    std::string cmd = luac + " -p " + shell_quote(lua_path.string()) + " 2>&1";
    if (FILE* fp = popen(cmd.c_str(), "r")) {
        char err[512] = {};
        size_t n = std::fread(err, 1, sizeof(err) - 1, fp);
        int rc = pclose(fp);
        if (n > 0 || rc != 0) {
            ++syntax_errors;
            std::fprintf(stderr, "  luac -p: %s", n ? err : (lua_path.string() + ": failed\n").c_str());
        }
    }
    return data;
}

} // namespace n64pack
