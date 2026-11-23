/**
 * n64_gettext.cpp
 *
 * N64 replacement for src/gettext.cpp.
 *
 * Wesnoth uses boost::locale and spirit_po for internationalization.
 * On N64, the game is English-only — all translation functions return
 * the input string unchanged (passthrough).
 */

#ifdef N64_BUILD

#include "../gettext.hpp"
#include "n64_platform.hpp"

#include <string>
#include <algorithm>
#include <cstring>

namespace translation {

// All gettext functions simply return the input string (English passthrough)
std::string dgettext(const char*, const char* msgid) {
    return msgid ? msgid : "";
}

std::string egettext(const char* msgid) {
    return msgid ? msgid : "";
}

// sgettext: an untranslated "context^text" message is shown as "text"
// (first '^', as in src/gettext.cpp). '|' is ordinary text (name generator
// grammars use it).
std::string dsgettext(const char*, const char* msgid) {
    if (!msgid) return "";
    const char* hat = std::strchr(msgid, '^');
    return hat ? (hat + 1) : msgid;
}

std::string dsngettext(const char*, const char* singular, const char* plural, int n) {
    const char* s = (n == 1) ? singular : plural;
    if (!s) return "";
    const char* hat = std::strchr(s, '^');
    return hat ? (hat + 1) : s;
}

void bind_textdomain(const char*, const char*, const char*) {}
void set_default_textdomain(const char*) {}
void set_language(const std::string&, const std::vector<std::string>*) {}

int compare(const std::string& s1, const std::string& s2) {
    return s1.compare(s2);
}

int icompare(const std::string& s1, const std::string& s2) {
    std::string a = s1, b = s2;
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    return a.compare(b);
}

} // namespace translation

// Wesnoth macros: _() calls translation::gettext(), N_() is a no-op marker
// These are defined in gettext.hpp and work with the functions above.

#endif // N64_BUILD
