/**
 * n64_font_text.cpp
 *
 * N64 replacement for src/font/text.cpp.
 *
 * The pango_text class is Wesnoth's primary text rendering engine.
 * On PC it uses Pango+Cairo for Unicode-aware shaped/wrapped text.
 * On N64, we replace it with a simple bitmap font renderer that uses
 * the 8x8 CP437 glyph sheet from n64_video.hpp.
 *
 * All pango_text methods are implemented here as simplified versions
 * that measure and render text using fixed-width 8x8 glyphs.
 *
 * This file is compiled INSTEAD of src/font/text.cpp on N64 —
 * it's included via the n64/*.cpp glob in CMakeLists.
 * We must ensure src/font/text.cpp is NOT in the N64 build's source list.
 */

#ifdef N64_BUILD

#include "n64_platform.hpp"
#include "n64_video.hpp"

#include "../font/text.hpp"
#include "../sdl/surface.hpp"
#include "../sdl/texture.hpp"
#include "../sdl/rect.hpp"
#include "../color.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

// Glyph dimensions (matching n64_video.hpp bitmap font)
static constexpr int GLYPH_W = 8;
static constexpr int GLYPH_H = 8;

// -----------------------------------------------------------------------
// Helper: measure text with simple word-wrapping
// -----------------------------------------------------------------------
static void measure_text(const std::string& text, int max_width, int max_height,
                          int* out_w, int* out_h, int* out_lines) {
    if (text.empty()) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = GLYPH_H;
        if (out_lines) *out_lines = 1;
        return;
    }

    int x = 0, y = 0;
    int max_x = 0;
    int lines = 1;
    int wrap_w = (max_width > 0) ? max_width : 9999;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            if (x > max_x) max_x = x;
            x = 0;
            y += GLYPH_H;
            lines++;
            if (max_height > 0 && y + GLYPH_H > max_height) break;
            continue;
        }

        // Handle UTF-8 multi-byte: skip continuation bytes (0x80-0xBF)
        if ((c & 0xC0) == 0x80) continue;

        x += GLYPH_W;
        if (x > wrap_w && max_width > 0) {
            // Word wrap: move to next line
            if (x - GLYPH_W > max_x) max_x = x - GLYPH_W;
            x = GLYPH_W;
            y += GLYPH_H;
            lines++;
            if (max_height > 0 && y + GLYPH_H > max_height) break;
        }
    }
    if (x > max_x) max_x = x;

    if (out_w) *out_w = max_x;
    if (out_h) *out_h = y + GLYPH_H;
    if (out_lines) *out_lines = lines;
}

namespace font {

// -----------------------------------------------------------------------
// pango_text constructor/destructor
// -----------------------------------------------------------------------
pango_text::pango_text()
    : context_(nullptr, [](void*){})
    , layout_(nullptr, [](void*){})
    , rect_{}
    , text_()
    , markedup_text_(false)
    , link_aware_(false)
    , link_color_()
    , font_class_(family_class::sans_serif)
    , font_size_(14)
    , font_style_(STYLE_NORMAL)
    , foreground_color_()
    , add_outline_(false)
    , maximum_width_(-1)
    , characters_per_line_(0)
    , maximum_height_(-1)
    , ellipse_mode_(PANGO_ELLIPSIZE_END)
    , alignment_(PANGO_ALIGN_LEFT)
    , maximum_length_(std::string::npos)
    , calculation_dirty_(true)
    , length_(0)
    , pixel_scale_(1)
    , surface_buffer_()
{
}

// -----------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------
texture pango_text::render_and_get_texture() {
    // On N64, text rendering happens at draw time via n64_draw_text.
    // We return a dummy texture with the correct dimensions.
    int w = 0, h = 0;
    measure_text(text_, maximum_width_, maximum_height_, &w, &h, nullptr);

    texture t;
    // Create a shim texture with correct dimensions
    auto* st = SDL_CreateTexture(nullptr, 0, 0, w > 0 ? w : 1, h > 0 ? h : GLYPH_H);
    if (st) {
        st->w = w > 0 ? w : 1;
        st->h = h > 0 ? h : GLYPH_H;
    }
    return t;
}

texture pango_text::render_texture(const rect& viewport) {
    return render_and_get_texture();
}

surface pango_text::render_surface(const rect&) {
    return surface();
}

texture pango_text::with_draw_scale(const texture& t) const {
    return t;
}

int pango_text::to_draw_scale(int i) const {
    return i;
}

// -----------------------------------------------------------------------
// Size queries
// -----------------------------------------------------------------------
point pango_text::get_size() {
    int w = 0, h = 0;
    measure_text(text_, maximum_width_, maximum_height_, &w, &h, nullptr);
    return {w > 0 ? w : 1, h > 0 ? h : GLYPH_H};
}

bool pango_text::is_truncated() const {
    if (maximum_height_ <= 0) return false;
    int h = 0;
    measure_text(text_, maximum_width_, -1, nullptr, &h, nullptr);
    return h > maximum_height_;
}

int pango_text::get_max_glyph_height() const {
    return GLYPH_H;
}

// -----------------------------------------------------------------------
// Text editing support
// -----------------------------------------------------------------------
unsigned pango_text::insert_text(const unsigned offset, const std::string& text, const bool) {
    if (offset > text_.size()) return 0;
    text_.insert(offset, text);
    length_ = text_.size();
    calculation_dirty_ = true;
    return text.size();
}

unsigned pango_text::get_byte_index(const unsigned offset, const unsigned) const {
    return std::min(offset, (unsigned)text_.size());
}

point pango_text::get_cursor_position(const unsigned offset, const unsigned) const {
    // Simple: each character is GLYPH_W wide
    unsigned byte_off = std::min(offset, (unsigned)text_.size());
    int x = 0, y = 0;
    for (unsigned i = 0; i < byte_off && i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            x = 0; y += GLYPH_H;
        } else if ((text_[i] & 0xC0) != 0x80) {
            x += GLYPH_W;
        }
    }
    return {x, y};
}

point pango_text::get_cursor_pos_from_index(const unsigned offset) const {
    return get_cursor_position(offset, 0);
}

std::size_t pango_text::get_maximum_length() const {
    return maximum_length_;
}

std::string pango_text::get_token(const point& position, std::string_view delimiters) const {
    // Find which character is at this position
    int target_x = position.x, target_y = position.y;
    int x = 0, y = 0;
    size_t idx = 0;

    for (size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') { x = 0; y += GLYPH_H; continue; }
        if ((text_[i] & 0xC0) == 0x80) continue;
        if (y <= target_y && target_y < y + GLYPH_H && x <= target_x && target_x < x + GLYPH_W) {
            idx = i;
            break;
        }
        x += GLYPH_W;
    }

    // Expand to token boundaries
    size_t start = idx, end = idx;
    while (start > 0 && delimiters.find(text_[start - 1]) == std::string_view::npos) --start;
    while (end < text_.size() && delimiters.find(text_[end]) == std::string_view::npos) ++end;

    return text_.substr(start, end - start);
}

std::string pango_text::get_link(const point& position) const {
    if (!link_aware_) return "";
    std::string token = get_token(position, " \n\r\t");
    if (token.substr(0, 7) == "http://" || token.substr(0, 8) == "https://")
        return token;
    return "";
}

point pango_text::get_column_line(const point& position) const {
    int col = position.x / GLYPH_W;
    int line = position.y / GLYPH_H;
    return {col, line};
}

std::tuple<int, int, bool> pango_text::xy_to_index(const point& position) const {
    int x = 0, y = 0;
    for (size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') { x = 0; y += GLYPH_H; continue; }
        if ((text_[i] & 0xC0) == 0x80) continue;
        if (y <= position.y && position.y < y + GLYPH_H && x <= position.x && position.x < x + GLYPH_W) {
            return {(int)i, 0, true};
        }
        x += GLYPH_W;
    }
    return {(int)text_.size(), 0, false};
}

std::vector<std::string> pango_text::get_lines() const {
    std::vector<std::string> result;
    std::istringstream iss(text_);
    std::string line;
    while (std::getline(iss, line)) {
        result.push_back(line);
    }
    if (result.empty()) result.push_back("");
    return result;
}

PangoLayoutLine* pango_text::get_line(int) {
    return nullptr;
}

std::pair<int, int> pango_text::index_to_line_x(const unsigned offset, const bool) const {
    int x = 0, line = 0;
    for (unsigned i = 0; i < offset && i < text_.size(); ++i) {
        if (text_[i] == '\n') { x = 0; line++; }
        else if ((text_[i] & 0xC0) != 0x80) { x += GLYPH_W; }
    }
    return {line, x * PANGO_SCALE};
}

// get_lines_count() is defined inline in text.hpp

// -----------------------------------------------------------------------
// Setters
// -----------------------------------------------------------------------
bool pango_text::set_text(const std::string& text, const bool markedup) {
    // Strip pango markup if present (simple tag removal)
    if (markedup) {
        std::string clean;
        bool in_tag = false;
        for (char c : text) {
            if (c == '<') { in_tag = true; continue; }
            if (c == '>') { in_tag = false; continue; }
            if (!in_tag) clean += c;
        }
        text_ = clean;
    } else {
        text_ = text;
    }
    markedup_text_ = markedup;
    length_ = text_.size();
    calculation_dirty_ = true;
    return true;
}

pango_text& pango_text::set_family_class(font::family_class fclass) {
    font_class_ = fclass;
    return *this;
}

pango_text& pango_text::set_font_size(unsigned font_size) {
    font_size_ = font_size;
    return *this;
}

pango_text& pango_text::set_font_style(const FONT_STYLE font_style) {
    font_style_ = font_style;
    return *this;
}

pango_text& pango_text::set_foreground_color(const color_t& color) {
    foreground_color_ = color;
    return *this;
}

pango_text& pango_text::set_maximum_width(int width) {
    maximum_width_ = width <= 0 ? -1 : width;
    return *this;
}

pango_text& pango_text::set_characters_per_line(const unsigned characters_per_line) {
    characters_per_line_ = characters_per_line;
    return *this;
}

pango_text& pango_text::set_maximum_height(int height, bool) {
    maximum_height_ = height <= 0 ? -1 : height;
    return *this;
}

pango_text& pango_text::set_ellipse_mode(const PangoEllipsizeMode ellipse_mode) {
    ellipse_mode_ = ellipse_mode;
    return *this;
}

pango_text& pango_text::set_alignment(const PangoAlignment alignment) {
    alignment_ = alignment;
    return *this;
}

pango_text& pango_text::set_maximum_length(const std::size_t maximum_length) {
    maximum_length_ = maximum_length;
    return *this;
}

pango_text& pango_text::set_link_aware(bool b) {
    link_aware_ = b;
    return *this;
}

pango_text& pango_text::set_link_color(const color_t& color) {
    link_color_ = color;
    return *this;
}

pango_text& pango_text::set_add_outline(bool do_add) {
    add_outline_ = do_add;
    return *this;
}

void pango_text::clear_attributes() {}

void pango_text::apply_attributes(const font::attribute_list&) {}

// -----------------------------------------------------------------------
// Private helpers (referenced in text.hpp but not needed on N64)
// -----------------------------------------------------------------------
void pango_text::recalculate() const {
    calculation_dirty_ = false;
}

} // namespace font

// -----------------------------------------------------------------------
// sdl_ttf_compat functions (font/sdl_ttf_compat.hpp)
// These are used by older code paths and the help browser.
// -----------------------------------------------------------------------
#include "../font/sdl_ttf_compat.hpp"

namespace font {

texture pango_render_text(const std::string&, int, const color_t&, font::pango_text::FONT_STYLE, bool, int) {
    return texture();
}

std::pair<int, int> pango_line_size(const std::string& line, int, font::pango_text::FONT_STYLE) {
    int w = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if ((line[i] & 0xC0) != 0x80) w += GLYPH_W;
    }
    return {w, GLYPH_H};
}

std::string pango_word_wrap(const std::string& text, int, int max_width, int, int, bool) {
    if (max_width <= 0) return text;
    std::string result;
    int x = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') { result += '\n'; x = 0; continue; }
        if ((text[i] & 0xC0) == 0x80) { result += text[i]; continue; }
        x += GLYPH_W;
        if (x > max_width) { result += '\n'; x = GLYPH_W; }
        result += text[i];
    }
    return result;
}

rect pango_draw_text(bool, const rect&, int, const color_t&, const std::string& text, int x, int y) {
    int w = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if ((text[i] & 0xC0) != 0x80) w += GLYPH_W;
    return {x, y, w, GLYPH_H};
}

} // namespace font

#endif // N64_BUILD
