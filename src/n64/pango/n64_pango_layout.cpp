/*
 * n64_pango_layout.cpp -- PangoLayout for the Nintendo 64 Pango subset.
 *
 * Itemizes the text by attributes and font (with per-character fallback),
 * shapes each item with HarfBuzz, breaks paragraphs into lines (word, char
 * and word-char wrapping), ellipsizes, aligns and stacks lines with Pango's
 * line-spacing rules, answers index/position queries, and draws with cairo.
 * Glyph positions are rounded to whole pixels, as Pango does with hinted
 * metrics (Wesnoth's setting). Left-to-right text only.
 */
#include "n64_pango_private.hpp"

#include <hb.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace n64pango {
namespace {

// ------------------------------------------------------------------ UTF-8
unsigned decode(const std::string& s, size_t i, size_t* len)
{
    const unsigned char c = (unsigned char)s[i];
    unsigned cp = c, n = 1;
    if (c >= 0xF0 && i + 3 < s.size() + 0) { cp = c & 0x07; n = 4; }
    else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
    else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
    if (i + n > s.size()) n = 1;
    for (unsigned k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    if (len) *len = n;
    return cp;
}

int round_px(int pu) { return (pu >= 0 ? pu + PANGO_SCALE / 2 : pu - PANGO_SCALE / 2) / PANGO_SCALE * PANGO_SCALE; }

bool is_mark(unsigned cp)
{
    if (cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF)) return true;
    switch (hb_unicode_general_category(hb_unicode_funcs_get_default(), cp)) {
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK:
        return true;
    default:
        return false;
    }
}

bool is_break_space(unsigned cp)
{
    return cp == ' ' || cp == '\t' || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x2006) ||
           (cp >= 0x2008 && cp <= 0x200A) || cp == 0x205F || cp == 0x3000 || cp == 0x200B;
}

bool is_ideographic(unsigned cp)
{
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x3FFFF);
}

bool is_letter(unsigned cp)
{
    switch (hb_unicode_general_category(hb_unicode_funcs_get_default(), cp)) {
    case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
        return true;
    default:
        return false;
    }
}

// The hyphen Pango inserts when it breaks inside a word: U+2010 if the font
// has it, else U+002D. Returns the advance (Pango units).
int hyphen_glyph(font_inst* f, unsigned* glyph)
{
    hb_codepoint_t g = 0;
    if (!f || !(hb_font_get_nominal_glyph(f->hb, 0x2010, &g) || hb_font_get_nominal_glyph(f->hb, '-', &g))) {
        *glyph = 0xFFFFFFFFu;
        return 0;
    }
    *glyph = g;
    return round_px(hb_font_get_glyph_h_advance(f->hb, g));
}

bool no_break_before(unsigned cp)
{
    static const unsigned list[] = { ')', ']', '}', ',', '.', '!', '?', ':', ';', 0x3001, 0x3002, 0xFF0C,
                                     0xFF0E, 0xFF01, 0xFF1F, 0x300D, 0x300F, 0xFF09, 0x30FC, 0xFF1A, 0xFF1B };
    for (unsigned c : list) if (c == cp) return true;
    return false;
}

bool no_break_after(unsigned cp)
{
    static const unsigned list[] = { '(', '[', '{', 0x300C, 0x300E, 0xFF08 };
    for (unsigned c : list) if (c == cp) return true;
    return false;
}

// ------------------------------------------------------- resolved style
struct style {
    std::string family = "sans";
    int size = 12 * PANGO_SCALE;
    bool size_abs = false;
    double scale = 1.0;
    int weight = PANGO_WEIGHT_NORMAL;
    PangoStyle slant = PANGO_STYLE_NORMAL;
    bool has_fg = false, has_bg = false, has_ul_color = false;
    PangoColor fg{}, bg{}, ul_color{};
    int fg_alpha = 65535, bg_alpha = 65535;
    PangoUnderline underline = PANGO_UNDERLINE_NONE;
    bool strike = false;
    int rise = 0, letter_spacing = 0;
    double line_height = 0;
    int abs_line_height = 0;
    PangoAttrShape* shape = nullptr;
};

void apply_desc(style& st, const PangoFontDescription* d)
{
    if (!d) return;
    if (d->mask & PANGO_FONT_MASK_FAMILY) st.family = d->family;
    if (d->mask & PANGO_FONT_MASK_SIZE) { st.size = d->size; st.size_abs = d->size_absolute; }
    if (d->mask & PANGO_FONT_MASK_WEIGHT) st.weight = d->weight;
    if (d->mask & PANGO_FONT_MASK_STYLE) st.slant = d->style;
}

void apply_attr(style& st, const PangoAttribute* a)
{
    switch (a->klass->type) {
    case PANGO_ATTR_FAMILY: st.family = reinterpret_cast<const PangoAttrString*>(a)->value; break;
    case PANGO_ATTR_STYLE: st.slant = (PangoStyle)reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_WEIGHT: st.weight = reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_SIZE:
    case PANGO_ATTR_ABSOLUTE_SIZE: {
        auto* s = reinterpret_cast<const PangoAttrSize*>(a);
        st.size = s->size;
        st.size_abs = s->absolute;
        break;
    }
    case PANGO_ATTR_FONT_DESC: apply_desc(st, reinterpret_cast<const PangoAttrFontDesc*>(a)->desc); break;
    case PANGO_ATTR_SCALE: st.scale *= reinterpret_cast<const PangoAttrFloat*>(a)->value; break;
    case PANGO_ATTR_FOREGROUND: st.has_fg = true; st.fg = reinterpret_cast<const PangoAttrColor*>(a)->color; break;
    case PANGO_ATTR_BACKGROUND: st.has_bg = true; st.bg = reinterpret_cast<const PangoAttrColor*>(a)->color; break;
    case PANGO_ATTR_UNDERLINE_COLOR:
        st.has_ul_color = true; st.ul_color = reinterpret_cast<const PangoAttrColor*>(a)->color; break;
    case PANGO_ATTR_FOREGROUND_ALPHA: st.fg_alpha = reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_BACKGROUND_ALPHA: st.bg_alpha = reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_UNDERLINE: st.underline = (PangoUnderline)reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_STRIKETHROUGH: st.strike = reinterpret_cast<const PangoAttrInt*>(a)->value != 0; break;
    case PANGO_ATTR_RISE: st.rise = reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_LETTER_SPACING: st.letter_spacing = reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_LINE_HEIGHT: st.line_height = reinterpret_cast<const PangoAttrFloat*>(a)->value; break;
    case PANGO_ATTR_ABSOLUTE_LINE_HEIGHT: st.abs_line_height = reinterpret_cast<const PangoAttrInt*>(a)->value; break;
    case PANGO_ATTR_SHAPE: st.shape = const_cast<PangoAttrShape*>(reinterpret_cast<const PangoAttrShape*>(a)); break;
    default: break;
    }
}

double style_px(const style& st, double dpi)
{
    const double base = st.size_abs ? st.size / (double)PANGO_SCALE : st.size / (double)PANGO_SCALE * dpi / 72.0;
    return std::max(1.0, base * st.scale);
}

// One character of a paragraph.
struct chr {
    int idx = 0, len = 1;          // byte index, bytes
    unsigned cp = 0;
    int width = 0;                 // Pango units (advance of its glyphs)
    bool grapheme_start = true;    // a cursor/char-break position before it
    bool cluster_start = true;     // not inside a ligature
    bool break_after = false;      // word-break opportunity after it
    bool space = false;
    int item = 0;
};

// A shaped piece of a paragraph (same style and font).
struct item {
    int start = 0, end = 0;
    style st;
    font_inst* font = nullptr;
    std::vector<unsigned> glyphs;
    std::vector<int> x_adv, x_off, y_off, cluster;
};

struct builder {
    PangoLayout* L;
    double dpi;
    style base;
    std::vector<const PangoAttribute*> attrs;
    std::vector<int> bounds;       // attribute boundaries (sorted, unique)

    explicit builder(PangoLayout* l) : L(l)
    {
        dpi = l->context ? l->context->dpi : 96.0;
        apply_desc(base, l->desc);
        if (l->attrs)
            for (auto* a : l->attrs->attrs) attrs.push_back(a);
        bounds.push_back(0);
        bounds.push_back((int)l->text.size());
        for (auto* a : attrs) {
            if (a->start_index < l->text.size()) bounds.push_back((int)a->start_index);
            if (a->end_index < l->text.size()) bounds.push_back((int)a->end_index);
        }
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    }

    style style_at(int idx) const
    {
        style st = base;
        for (auto* a : attrs)
            if ((int)a->start_index <= idx && (idx < (int)a->end_index || a->end_index == PANGO_ATTR_INDEX_TO_TEXT_END))
                apply_attr(st, a);
        return st;
    }

    // segment id = index of the last boundary <= idx
    int segment_of(int idx) const
    {
        return (int)(std::upper_bound(bounds.begin(), bounds.end(), idx) - bounds.begin()) - 1;
    }

    font_inst* font_for(const style& st, unsigned cp) const
    {
        return get_font_for_char(st.family, st.weight, st.slant, style_px(st, dpi), cp);
    }

    void shape(item& it) const
    {
        if (it.st.shape) {
            // each character becomes one "glyph" the size of the shape
            for (int i = it.start; i < it.end;) {
                size_t n;
                decode(L->text, i, &n);
                it.glyphs.push_back(0xFFFFFFFFu);
                it.x_adv.push_back(it.st.shape->logical_rect.width);
                it.x_off.push_back(0);
                it.y_off.push_back(0);
                it.cluster.push_back(i);
                i += (int)n;
            }
            return;
        }
        hb_buffer_t* b = hb_buffer_create();
        hb_buffer_set_cluster_level(b, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
        hb_buffer_add_utf8(b, L->text.data(), (int)L->text.size(), (unsigned)it.start, it.end - it.start);
        hb_buffer_set_direction(b, HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(b);
        hb_shape(it.font->hb, b, nullptr, 0);
        unsigned n = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(b, &n);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(b, &n);
        for (unsigned g = 0; g < n; ++g) {
            const int c = (int)info[g].cluster;
            size_t clen;
            const unsigned cp = decode(L->text, c, &clen);
            int adv = round_px(pos[g].x_advance);
            if (cp == '\t') adv = 0;                     // set by tab stops
            it.glyphs.push_back((cp == '\t' || cp == 0x200B || cp == 0xFEFF || cp == 0x200D) ? 0xFFFFFFFFu
                                                                                             : info[g].codepoint);
            it.x_adv.push_back(adv);
            it.x_off.push_back(round_px(pos[g].x_offset));
            it.y_off.push_back(round_px(pos[g].y_offset));
            it.cluster.push_back(c);
        }
        hb_buffer_destroy(b);
        if (it.st.letter_spacing) {
            // Pango adds letter spacing after each grapheme cluster
            for (unsigned g = 0; g < it.glyphs.size(); ++g)
                if (g + 1 == it.glyphs.size() || it.cluster[g + 1] != it.cluster[g])
                    it.x_adv[g] += round_px(it.st.letter_spacing);
        }
    }
};

// Space for a tab: up to the next multiple of 8 space widths.
int tab_width(font_inst* f, int x)
{
    int space = f ? f->approx_char_width : 8 * PANGO_SCALE;
    if (f) {
        hb_codepoint_t g;
        if (hb_font_get_nominal_glyph(f->hb, ' ', &g))
            space = round_px(hb_font_get_glyph_h_advance(f->hb, g));
    }
    const int stop = std::max(PANGO_SCALE, 8 * space);
    return (x / stop + 1) * stop - x;
}

} // namespace

// Build one line from the paragraph's items for chars [c0, c1).
static line make_line(const std::vector<item>& items, const std::vector<chr>& chars, int c0, int c1,
                      bool para_start, PangoLayout* L, int byte_end_if_empty, bool zero_final_space,
                      bool add_hyphen = false)
{
    line ln;
    ln.pub.layout = L;
    ln.pub.is_paragraph_start = para_start;
    ln.pub.start_index = c0 < c1 ? chars[c0].idx : byte_end_if_empty;
    const int byte_end = c0 < c1 ? chars[c1 - 1].idx + chars[c1 - 1].len : byte_end_if_empty;
    ln.pub.length = byte_end - ln.pub.start_index;

    int x = 0;
    for (const item& it : items) {
        if (it.end <= ln.pub.start_index || it.start >= byte_end) continue;
        run r;
        r.start = std::max(it.start, ln.pub.start_index);
        r.end = std::min(it.end, byte_end);
        r.font = it.font;
        r.has_fg = it.st.has_fg; r.fg = it.st.fg; r.fg_alpha = it.st.fg_alpha;
        r.has_bg = it.st.has_bg; r.bg = it.st.bg; r.bg_alpha = it.st.bg_alpha;
        r.has_ul_color = it.st.has_ul_color; r.ul_color = it.st.ul_color;
        r.underline = it.st.underline;
        r.strikethrough = it.st.strike;
        r.rise = it.st.rise;
        r.line_height = it.st.line_height;
        r.abs_line_height = it.st.abs_line_height;
        r.shape = it.st.shape;
        r.x = x;
        for (size_t g = 0; g < it.glyphs.size(); ++g) {
            const int c = it.cluster[g];
            if (c < r.start || c >= r.end) continue;
            int adv = it.x_adv[g];
            size_t n;
            const unsigned cp = decode(L->text, c, &n);
            if (cp == '\t') adv = tab_width(it.font, x);
            if (zero_final_space && c == chars[c1 - 1].idx && chars[c1 - 1].space) adv = 0;
            r.glyphs.push_back(it.glyphs[g]);
            r.x_adv.push_back(adv);
            r.x_off.push_back(it.x_off[g]);
            r.y_off.push_back(it.y_off[g]);
            r.cluster.push_back(c);
            r.width += adv;
            x += adv;
        }
        ln.runs.push_back(std::move(r));
    }
    if (add_hyphen && !ln.runs.empty()) {
        run& last = ln.runs.back();
        unsigned g;
        const int adv = hyphen_glyph(last.font, &g);
        last.glyphs.push_back(g);
        last.x_adv.push_back(adv);
        last.x_off.push_back(0);
        last.y_off.push_back(0);
        last.cluster.push_back(chars[c1 - 1].idx);
        last.width += adv;
        x += adv;
    }
    ln.width = x;
    return ln;
}

// Replace a line's content by an ellipsized version of chars [c0, c1).
static line make_ellipsized(const std::vector<item>& items, const std::vector<chr>& chars, int c0, int c1,
                            bool para_start, PangoLayout* L, const builder& B, int avail, bool* did)
{
    *did = false;
    int total = 0;
    for (int i = c0; i < c1; ++i) total += chars[i].width;
    if (total <= avail || c0 >= c1) {
        return make_line(items, chars, c0, c1, para_start, L, c0 < c1 ? chars[c0].idx : 0, false);
    }
    *did = true;

    // the ellipsis, shaped in the font of the first removed character
    auto shape_ellipsis = [&](int at) {
        const item& src = items[chars[std::min(at, c1 - 1)].item];
        style st = src.st;
        st.shape = nullptr;
        font_inst* f = B.font_for(st, 0x2026);
        run e;
        e.font = f;
        e.has_fg = st.has_fg; e.fg = st.fg; e.fg_alpha = st.fg_alpha;
        e.ellipsis = true;
        hb_buffer_t* b = hb_buffer_create();
        hb_codepoint_t g;
        const bool has = f && hb_font_get_nominal_glyph(f->hb, 0x2026, &g);
        hb_buffer_add_utf8(b, has ? "\xE2\x80\xA6" : "...", -1, 0, -1);
        hb_buffer_set_direction(b, HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(b);
        hb_shape(f->hb, b, nullptr, 0);
        unsigned n = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(b, &n);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(b, &n);
        for (unsigned i = 0; i < n; ++i) {
            e.glyphs.push_back(info[i].codepoint);
            const int adv = round_px(pos[i].x_advance);
            e.x_adv.push_back(adv);
            e.x_off.push_back(0);
            e.y_off.push_back(0);
            e.width += adv;
        }
        hb_buffer_destroy(b);
        return e;
    };

    const PangoEllipsizeMode mode = L->ellipsize;
    int keep_head = c0, keep_tail = c1;     // chars [c0,keep_head) and [keep_tail,c1) are kept
    run ell = shape_ellipsis(c0);
    const int budget = std::max(0, avail - ell.width);
    auto grapheme_end = [&](int i) { do { ++i; } while (i < c1 && !chars[i].grapheme_start); return i; };
    if (mode == PANGO_ELLIPSIZE_END || mode == PANGO_ELLIPSIZE_MIDDLE) {
        const int head_budget = mode == PANGO_ELLIPSIZE_END ? budget : budget / 2;
        int w = 0, i = c0;
        while (i < c1) {
            int j = grapheme_end(i), gw = 0;
            for (int k = i; k < j; ++k) gw += chars[k].width;
            if (w + gw > head_budget) break;
            w += gw;
            i = j;
        }
        keep_head = i;
        if (mode == PANGO_ELLIPSIZE_MIDDLE) {
            int tw = 0, t = c1;
            while (t > keep_head) {
                int s = t - 1;
                while (s > keep_head && !chars[s].grapheme_start) --s;
                int gw = 0;
                for (int k = s; k < t; ++k) gw += chars[k].width;
                if (w + tw + gw > budget) break;
                tw += gw;
                t = s;
            }
            keep_tail = t;
        } else {
            keep_tail = c1;
        }
    } else {   // START
        int tw = 0, t = c1;
        while (t > c0) {
            int s = t - 1;
            while (s > c0 && !chars[s].grapheme_start) --s;
            int gw = 0;
            for (int k = s; k < t; ++k) gw += chars[k].width;
            if (tw + gw > budget) break;
            tw += gw;
            t = s;
        }
        keep_head = c0;
        keep_tail = t;
    }
    if (mode != PANGO_ELLIPSIZE_START) ell = shape_ellipsis(keep_head);

    line head = make_line(items, chars, c0, keep_head, para_start, L, chars[c0].idx, false);
    line tail = make_line(items, chars, keep_tail, c1, false, L, chars[c1 - 1].idx + chars[c1 - 1].len, false);
    line out;
    out.pub = head.pub;
    out.pub.start_index = chars[c0].idx;
    out.pub.length = chars[c1 - 1].idx + chars[c1 - 1].len - chars[c0].idx;
    out.ellipsized = true;
    int x = 0;
    for (auto& r : head.runs) { r.x = x; x += r.width; out.runs.push_back(std::move(r)); }
    ell.start = keep_head < c1 ? chars[keep_head].idx : out.pub.start_index + out.pub.length;
    ell.end = keep_tail < c1 ? chars[keep_tail].idx : out.pub.start_index + out.pub.length;
    for (int i = 0; i < (int)ell.glyphs.size(); ++i) ell.cluster.push_back(ell.start);
    // Pango's ellipsized line fills the available width exactly.
    {
        int content = ell.width;
        for (auto& r : head.runs) content += r.width;
        for (auto& r : tail.runs) content += r.width;
        if (content < avail && !ell.x_adv.empty()) {
            ell.x_adv.back() += avail - content;
            ell.width += avail - content;
        }
    }
    ell.x = x;
    x += ell.width;
    out.runs.push_back(std::move(ell));
    for (auto& r : tail.runs) { r.x = x; x += r.width; out.runs.push_back(std::move(r)); }
    out.width = x;
    return out;
}

// Vertical metrics of a line (relative to its baseline).
static void line_metrics(line& ln, font_inst* empty_font, int* font_height)
{
    int top = 0, bottom = 0, fh = 0;
    bool any = false;
    for (const run& r : ln.runs) {
        font_inst* f = r.font ? r.font : empty_font;
        if (!f) continue;
        int asc = f->ascent, desc = f->descent;
        if (r.shape) {
            asc = std::max(asc, -r.shape->logical_rect.y);
            desc = std::max(desc, r.shape->logical_rect.y + r.shape->logical_rect.height);
        }
        int y = -asc - r.rise, h = asc + desc;
        int height = h;
        if (r.line_height > 0 || r.abs_line_height > 0) {
            const int lh = r.line_height > 0 ? (int)(r.line_height * h) : r.abs_line_height;
            const int leading = lh - h;
            y -= leading / 2;
            h += leading;
            height = lh;
        }
        if (!any || y < top) top = y;
        if (!any || y + h > bottom) bottom = y + h;
        fh = std::max(fh, height);
        any = true;
    }
    if (!any && empty_font) {
        top = -empty_font->ascent;
        bottom = empty_font->descent;
        fh = bottom - top;
    }
    ln.logical_y = top;
    ln.logical_h = bottom - top;
    *font_height = fh;
}

void layout_update(PangoLayout* L)
{
    if (!L->dirty) return;
    L->dirty = false;
    L->lines.clear();
    L->is_ellipsized = false;

    builder B(L);
    const std::string& t = L->text;
    const bool ellipsize = L->ellipsize != PANGO_ELLIPSIZE_NONE && L->width >= 0;
    const int lines_per_para = (ellipsize && L->height <= 0) ? std::max(1, L->height == 0 ? 1 : -L->height) : INT_MAX;
    const int height_limit = (ellipsize && L->height > 0) ? L->height : -1;

    std::vector<int> font_heights;
    font_inst* default_font = B.font_for(B.style_at(0), 'x');

    // ------------------------------------------------ paragraphs and lines
    size_t p = 0;
    for (;;) {
        size_t q = p;
        while (q < t.size() && t[q] != '\n' && t[q] != '\r' && t.compare(q, 3, "\xE2\x80\xA9") != 0) ++q;
        const int pstart = (int)p, pend = (int)q;

        // characters
        std::vector<chr> chars;
        for (size_t i = p; i < q;) {
            size_t n;
            chr c;
            c.idx = (int)i;
            c.cp = decode(t, i, &n);
            c.len = (int)n;
            c.space = is_break_space(c.cp);
            c.grapheme_start = chars.empty() || !is_mark(c.cp);
            chars.push_back(c);
            i += n;
        }

        // items
        std::vector<item> items;
        for (size_t ci = 0; ci < chars.size(); ++ci) {
            chr& c = chars[ci];
            const int seg = B.segment_of(c.idx);
            font_inst* f;
            style st;
            if (!items.empty() && !c.grapheme_start) {       // marks stay with their base
                c.item = (int)items.size() - 1;
                items.back().end = c.idx + c.len;
                continue;
            }
            st = B.style_at(c.idx);
            f = B.font_for(st, c.cp);
            if (!items.empty() && B.segment_of(items.back().start) == seg && items.back().font == f &&
                !st.shape && !items.back().st.shape) {
                items.back().end = c.idx + c.len;
            } else {
                item it;
                it.start = c.idx;
                it.end = c.idx + c.len;
                it.st = st;
                it.font = f;
                items.push_back(std::move(it));
            }
            c.item = (int)items.size() - 1;
        }
        for (item& it : items) B.shape(it);

        // widths, ligature clusters
        {
            std::vector<int> width_at(pend - pstart + 1, 0);
            std::vector<char> is_cluster(pend - pstart + 1, 0);
            for (const item& it : items)
                for (size_t g = 0; g < it.glyphs.size(); ++g) {
                    width_at[it.cluster[g] - pstart] += it.x_adv[g];
                    is_cluster[it.cluster[g] - pstart] = 1;
                }
            for (chr& c : chars) {
                c.width = width_at[c.idx - pstart];
                c.cluster_start = is_cluster[c.idx - pstart] != 0;
                if (c.cp == '\t') c.width = tab_width(items.empty() ? nullptr : items[c.item].font, 0);
            }
        }
        // break opportunities
        for (size_t i = 0; i + 1 < chars.size(); ++i) {
            const unsigned a = chars[i].cp, b = chars[i + 1].cp;
            bool br = false;
            if (chars[i].space && !chars[i + 1].space) br = true;
            else if ((a == '-' || a == 0x2010) && i > 0 && !chars[i - 1].space && !chars[i + 1].space &&
                     !(b >= '0' && b <= '9')) br = true;
            else if ((is_ideographic(a) || is_ideographic(b)) && !chars[i + 1].space) br = true;
            if (no_break_before(b) || no_break_after(a)) br = false;
            if (!chars[i + 1].cluster_start || !chars[i + 1].grapheme_start) br = false;
            chars[i].break_after = br;
        }

        // wrapping
        const bool wrap = L->width >= 0 && !(ellipsize && lines_per_para == 1);
        std::vector<std::pair<int, int>> spans;      // char ranges of lines
        std::vector<char> hyphenate;                 // line ends inside a word
        auto needs_hyphen = [&](int end) {
            return end > 0 && end < (int)chars.size() && is_letter(chars[end - 1].cp) && is_letter(chars[end].cp);
        };
        if (!wrap || chars.empty()) {
            spans.push_back({ 0, (int)chars.size() });
            hyphenate.push_back(0);
        } else {
            int start = 0;
            while (start < (int)chars.size()) {
                int x = 0, last_break = -1, i = start, end = (int)chars.size();
                bool hyph = false;
                for (; i < (int)chars.size(); ++i) {
                    const chr& c = chars[i];
                    if (!c.space && x + c.width > L->width && i > start) {
                        if (last_break >= start) { end = last_break + 1; break; }
                        if (L->wrap != PANGO_WRAP_WORD) {
                            int j = i, w = x;
                            for (;;) {
                                while (j > start + 1 && (!chars[j].grapheme_start || !chars[j].cluster_start)) {
                                    --j;
                                    w -= chars[j].width;
                                }
                                unsigned hg;
                                font_inst* hf = items.empty() ? nullptr : items[chars[j - 1].item].font;
                                const int hw = needs_hyphen(j) ? hyphen_glyph(hf, &hg) : 0;
                                if (w + hw <= L->width || j <= start + 1) break;
                                --j;
                                w -= chars[j].width;
                            }
                            end = j;
                            hyph = needs_hyphen(j);
                            break;
                        }
                    }
                    if (L->wrap == PANGO_WRAP_CHAR && c.grapheme_start && i > start) last_break = i - 1;
                    x += c.width;
                    if (c.break_after) last_break = i;
                }
                if (i >= (int)chars.size()) end = (int)chars.size();
                spans.push_back({ start, end });
                hyphenate.push_back(hyph ? 1 : 0);
                start = end;
            }
        }

        // lines, with ellipsization per paragraph line limit
        for (size_t s = 0; s < spans.size(); ++s) {
            const bool para_first = s == 0;
            const bool limit_here = ellipsize && (int)s + 1 == lines_per_para && s + 1 < spans.size();
            line ln;
            if (ellipsize && !chars.empty() && (lines_per_para == 1 || limit_here)) {
                bool did = false;
                ln = make_ellipsized(items, chars, spans[s].first, (int)chars.size(), para_first, L, B, L->width, &did);
                if (did) L->is_ellipsized = true;
            } else {
                const bool wrapped = s + 1 < spans.size();
                ln = make_line(items, chars, spans[s].first, spans[s].second, para_first, L,
                               spans[s].first < (int)chars.size() ? chars[spans[s].first].idx : pend, wrapped,
                               hyphenate[s] != 0);
            }
            int fh = 0;
            line_metrics(ln, items.empty() ? B.font_for(B.style_at(pstart), 'x') : default_font, &fh);
            if (ln.runs.empty()) {
                font_inst* ef = B.font_for(B.style_at(pstart), 'x');
                line_metrics(ln, ef, &fh);
            }
            L->lines.push_back(std::move(ln));
            font_heights.push_back(fh);
            if (limit_here || (ellipsize && lines_per_para == 1)) break;
        }

        // the empty "line" of an empty paragraph is handled by make_line above
        if (q >= t.size()) break;
        // paragraph separator
        if (t[q] == '\r' && q + 1 < t.size() && t[q + 1] == '\n') p = q + 2;
        else if (t[q] == '\n' || t[q] == '\r') p = q + 1;
        else p = q + 3;
    }

    // ------------------------------------------------ vertical placement
    int y_offset = 0, baseline = 0, max_w = 0;
    for (size_t i = 0; i < L->lines.size(); ++i) {
        line& ln = L->lines[i];
        int nb;
        if (i == 0 || L->line_spacing == 0.0f) nb = y_offset - ln.logical_y;
        else nb = std::max((int)(baseline + L->line_spacing * font_heights[i]), y_offset - ln.logical_y);
        ln.baseline = nb;
        ln.top = nb + ln.logical_y;
        baseline = nb;
        y_offset = ln.top + ln.logical_h + L->spacing;
        max_w = std::max(max_w, ln.width);
    }
    // height limit (positive height with ellipsization): keep the lines that
    // fit (at least one) and ellipsize the last kept line's paragraph remainder
    if (height_limit >= 0 && !L->lines.empty()) {
        size_t keep = 1;
        while (keep < L->lines.size() && L->lines[keep].top + L->lines[keep].logical_h <= height_limit) ++keep;
        if (keep < L->lines.size()) {
            L->lines.resize(keep);
            line& last = L->lines.back();
            // the last kept line takes the rest of its paragraph, ellipsized
            const int from = last.pub.start_index;
            const size_t para_end = L->text.find('\n', from);
            {
                builder B2(L);
                // collect chars of [from, para_end)
                const int pend2 = (int)(para_end == std::string::npos ? L->text.size() : para_end);
                std::vector<chr> chars;
                std::vector<item> items;
                for (int i = from; i < pend2;) {
                    size_t n;
                    chr c;
                    c.idx = i;
                    c.cp = decode(L->text, i, &n);
                    c.len = (int)n;
                    c.grapheme_start = chars.empty() || !is_mark(c.cp);
                    style st = B2.style_at(i);
                    font_inst* f = B2.font_for(st, c.cp);
                    if (!items.empty() && items.back().font == f && B2.segment_of(items.back().start) == B2.segment_of(i)
                        && !st.shape) {
                        items.back().end = i + (int)n;
                    } else {
                        item it; it.start = i; it.end = i + (int)n; it.st = st; it.font = f;
                        items.push_back(std::move(it));
                    }
                    c.item = (int)items.size() - 1;
                    chars.push_back(c);
                    i += (int)n;
                }
                for (item& it : items) B2.shape(it);
                for (chr& c : chars) {
                    const item& it = items[c.item];
                    for (size_t g = 0; g < it.glyphs.size(); ++g) if (it.cluster[g] == c.idx) c.width += it.x_adv[g];
                }
                if (!chars.empty()) {
                    bool did = false;
                    const int tb = last.baseline, tt = last.top;
                    const bool ps = last.pub.is_paragraph_start;
                    line ln = make_ellipsized(items, chars, 0, (int)chars.size(), ps, L, B2, L->width, &did);
                    int fh;
                    line_metrics(ln, default_font, &fh);
                    ln.baseline = tb;
                    ln.top = tt;
                    last = std::move(ln);
                }
            }
            L->is_ellipsized = true;
            max_w = 0;
            for (auto& ln : L->lines) max_w = std::max(max_w, ln.width);
        }
    }

    // ------------------------------------------------ alignment, extents
    const int ref_w = L->width >= 0 ? L->width : max_w;
    int min_x = INT_MAX, max_x = INT_MIN;
    for (line& ln : L->lines) {
        int off = 0;
        if (L->alignment == PANGO_ALIGN_CENTER) off = (ref_w - ln.width) / 2;
        else if (L->alignment == PANGO_ALIGN_RIGHT) off = ref_w - ln.width;
        off = off / PANGO_SCALE * PANGO_SCALE;          // whole pixels
        if (L->width >= 0 && off < 0) off = 0;
        ln.x = off;
        min_x = std::min(min_x, ln.x);
        max_x = std::max(max_x, ln.x + ln.width);
    }
    if (L->lines.empty()) { min_x = max_x = 0; }
    L->logical.x = min_x;
    L->logical.y = 0;
    L->logical.width = max_x - min_x;
    L->logical.height = L->lines.empty() ? 0 : L->lines.back().top + L->lines.back().logical_h;
}

static int utf8_count(const std::string& t, int a, int b)
{
    int n = 0;
    for (int i = a; i < b; ++i) if (((unsigned char)t[i] & 0xC0) != 0x80) ++n;
    return n;
}

// x (relative to the line start) of the character at byte index, and its
// width. A cluster's width (e.g. a ligature) is shared evenly by its
// characters, as in pango_glyph_string_index_to_x.
static void char_x(const PangoLayout* L, const line& ln, int index, int* x, int* w)
{
    *x = ln.width;
    *w = 0;
    for (const run& r : ln.runs) {
        if (r.ellipsis) {
            if (index >= r.start && index < r.end) { *x = r.x; *w = r.width; return; }
            continue;
        }
        if (index < r.start || index >= r.end) continue;
        int gx = r.x;
        size_t g = 0;
        while (g < r.glyphs.size()) {
            const int cs = r.cluster[g];
            int cw = 0;
            size_t h = g;
            while (h < r.glyphs.size() && r.cluster[h] == cs) cw += r.x_adv[h++];
            const int ce = h < r.glyphs.size() ? r.cluster[h] : r.end;
            if (index >= cs && index < ce) {
                const int n = std::max(1, utf8_count(L->text, cs, ce));
                const int k = utf8_count(L->text, cs, index);
                *x = gx + (int)((long long)cw * k / n);
                *w = cw / n;
                return;
            }
            gx += cw;
            g = h;
        }
        *x = gx;
        return;
    }
}

static int line_of_index(PangoLayout* L, int index)
{
    for (size_t i = 0; i < L->lines.size(); ++i) {
        const auto& p = L->lines[i].pub;
        if (index >= p.start_index && index < p.start_index + p.length) return (int)i;
        if (index == p.start_index + p.length &&
            (i + 1 == L->lines.size() || L->lines[i + 1].pub.start_index > index))
            return (int)i;
    }
    return L->lines.empty() ? -1 : (int)L->lines.size() - 1;
}

static int next_grapheme(const std::string& t, int i, int end)
{
    size_t n;
    if (i >= end) return end;
    decode(t, i, &n);
    i += (int)n;
    while (i < end) {
        const unsigned cp = decode(t, i, &n);
        if (!is_mark(cp)) break;
        i += (int)n;
    }
    return i;
}

} // namespace n64pango

using namespace n64pango;

// ================================================================ layouts
_PangoLayout::~_PangoLayout()
{
    if (attrs) pango_attr_list_unref(attrs);
    delete desc;
    if (context) g_object_unref(context);
}

PangoLayout* pango_layout_new(PangoContext* context)
{
    auto* l = new PangoLayout();
    l->context = static_cast<PangoContext*>(g_object_ref(context));
    return l;
}
PangoContext* pango_layout_get_context(PangoLayout* l) { return l->context; }

void pango_layout_set_text(PangoLayout* l, const char* text, int length)
{
    if (!text) text = "";
    if (length < 0) length = (int)std::strlen(text);
    l->text.assign(text, (size_t)length);
    l->dirty = true;
}
const char* pango_layout_get_text(PangoLayout* l) { return l->text.c_str(); }

void pango_layout_set_markup(PangoLayout* l, const char* markup, int length)
{
    PangoAttrList* attrs = nullptr;
    char* text = nullptr;
    if (!pango_parse_markup(markup, length, 0, &attrs, &text, nullptr, nullptr)) return;
    pango_layout_set_text(l, text, -1);
    std::free(text);
    pango_layout_set_attributes(l, attrs);
    pango_attr_list_unref(attrs);
}

void pango_layout_set_attributes(PangoLayout* l, PangoAttrList* attrs)
{
    if (attrs) pango_attr_list_ref(attrs);
    if (l->attrs) pango_attr_list_unref(l->attrs);
    l->attrs = attrs;
    l->dirty = true;
}
PangoAttrList* pango_layout_get_attributes(PangoLayout* l) { l->dirty = true; return l->attrs; }

void pango_layout_set_font_description(PangoLayout* l, const PangoFontDescription* desc)
{
    delete l->desc;
    l->desc = desc ? pango_font_description_copy(desc) : nullptr;
    l->dirty = true;
}

void pango_layout_set_width(PangoLayout* l, int w) { if (w != l->width) { l->width = w < 0 ? -1 : w; l->dirty = true; } }
int pango_layout_get_width(PangoLayout* l) { return l->width; }
void pango_layout_set_height(PangoLayout* l, int h) { if (h != l->height) { l->height = h; l->dirty = true; } }
int pango_layout_get_height(PangoLayout* l) { return l->height; }
void pango_layout_set_wrap(PangoLayout* l, PangoWrapMode w) { if (w != l->wrap) { l->wrap = w; l->dirty = true; } }
PangoWrapMode pango_layout_get_wrap(PangoLayout* l) { return l->wrap; }
void pango_layout_set_ellipsize(PangoLayout* l, PangoEllipsizeMode e) { if (e != l->ellipsize) { l->ellipsize = e; l->dirty = true; } }
PangoEllipsizeMode pango_layout_get_ellipsize(PangoLayout* l) { return l->ellipsize; }
gboolean pango_layout_is_ellipsized(PangoLayout* l) { layout_update(l); return l->is_ellipsized; }
void pango_layout_set_alignment(PangoLayout* l, PangoAlignment a) { if (a != l->alignment) { l->alignment = a; l->dirty = true; } }
PangoAlignment pango_layout_get_alignment(PangoLayout* l) { return l->alignment; }
void pango_layout_set_line_spacing(PangoLayout* l, float f) { if (f != l->line_spacing) { l->line_spacing = f; l->dirty = true; } }
float pango_layout_get_line_spacing(PangoLayout* l) { return l->line_spacing; }
void pango_layout_set_spacing(PangoLayout* l, int s) { if (s != l->spacing) { l->spacing = s; l->dirty = true; } }
void pango_layout_context_changed(PangoLayout* l) { l->dirty = true; }

void pango_layout_get_extents(PangoLayout* l, PangoRectangle* ink, PangoRectangle* logical)
{
    layout_update(l);
    if (ink) *ink = l->logical;
    if (logical) *logical = l->logical;
}

static void to_pixels(PangoRectangle* r)
{
    // pango_extents_to_pixels: inclusive (floor origin, ceil far edge)
    const int x0 = PANGO_PIXELS_FLOOR(r->x), y0 = PANGO_PIXELS_FLOOR(r->y);
    const int x1 = PANGO_PIXELS_CEIL(r->x + r->width), y1 = PANGO_PIXELS_CEIL(r->y + r->height);
    r->x = x0; r->y = y0; r->width = x1 - x0; r->height = y1 - y0;
}

void pango_layout_get_pixel_extents(PangoLayout* l, PangoRectangle* ink, PangoRectangle* logical)
{
    pango_layout_get_extents(l, ink, logical);
    if (ink) to_pixels(ink);
    if (logical) to_pixels(logical);
}
void pango_layout_get_size(PangoLayout* l, int* w, int* h)
{
    layout_update(l);
    if (w) *w = l->logical.width;
    if (h) *h = l->logical.height;
}
void pango_layout_get_pixel_size(PangoLayout* l, int* w, int* h)
{
    PangoRectangle r;
    pango_layout_get_pixel_extents(l, nullptr, &r);
    if (w) *w = r.width;
    if (h) *h = r.height;
}
int pango_layout_get_baseline(PangoLayout* l)
{
    layout_update(l);
    return l->lines.empty() ? 0 : l->lines[0].baseline;
}

int pango_layout_get_line_count(PangoLayout* l) { layout_update(l); return (int)l->lines.size(); }
PangoLayoutLine* pango_layout_get_line_readonly(PangoLayout* l, int i)
{
    layout_update(l);
    return (i >= 0 && i < (int)l->lines.size()) ? &l->lines[i].pub : nullptr;
}
PangoLayoutLine* pango_layout_get_line(PangoLayout* l, int i) { return pango_layout_get_line_readonly(l, i); }

void pango_layout_index_to_pos(PangoLayout* l, int index, PangoRectangle* pos)
{
    layout_update(l);
    const int li = line_of_index(l, index);
    if (li < 0) { *pos = {0, 0, 0, 0}; return; }
    const line& ln = l->lines[li];
    int x, w;
    char_x(l, ln, index, &x, &w);
    pos->x = ln.x + x;
    pos->y = ln.top;
    pos->width = w;
    pos->height = ln.logical_h;
}

// The run whose font sizes the cursor at index: the run of the character
// before the cursor (the trailing edge of the previous character), else the
// run of the character at index.
static const run* cursor_run(const line& ln, int index)
{
    const run* at = nullptr;
    const run* before = nullptr;
    for (const run& r : ln.runs) {
        if (index > r.start && index <= r.end) before = &r;
        if (index >= r.start && index < r.end && !at) at = &r;
    }
    return before ? before : at;
}

void pango_layout_get_cursor_pos(PangoLayout* l, int index, PangoRectangle* strong, PangoRectangle* weak)
{
    PangoRectangle r;
    pango_layout_index_to_pos(l, index, &r);
    r.width = 0;
    const int li = line_of_index(l, index);
    if (li >= 0) {
        const line& ln = l->lines[li];
        if (const run* cr = cursor_run(ln, index)) {
            if (cr->font) {
                r.y = ln.baseline - cr->font->ascent - cr->rise;
                r.height = cr->font->ascent + cr->font->descent;
            }
        }
    }
    if (strong) *strong = r;
    if (weak) *weak = r;
}

void pango_layout_index_to_line_x(PangoLayout* l, int index, gboolean trailing, int* line_out, int* x_pos)
{
    layout_update(l);
    const int li = line_of_index(l, index);
    if (li < 0) { if (line_out) *line_out = -1; if (x_pos) *x_pos = 0; return; }
    int x, w;
    char_x(l, l->lines[li], index, &x, &w);
    if (line_out) *line_out = li;
    if (x_pos) *x_pos = x + (trailing ? w : 0);
}

void pango_layout_line_index_to_x(PangoLayoutLine* pl, int index, gboolean trailing, int* x_pos)
{
    PangoLayout* l = pl->layout;
    layout_update(l);
    for (auto& ln : l->lines) {
        if (&ln.pub == pl) {
            int x, w;
            char_x(l, ln, index, &x, &w);
            if (x_pos) *x_pos = x + (trailing ? w : 0);
            return;
        }
    }
    if (x_pos) *x_pos = 0;
}

gboolean pango_layout_xy_to_index(PangoLayout* l, int x, int y, int* index, int* trailing)
{
    layout_update(l);
    if (l->lines.empty()) { if (index) *index = 0; if (trailing) *trailing = 0; return FALSE; }
    bool inside = true;
    size_t li = 0;
    if (y < 0) { inside = false; li = 0; }
    else {
        li = l->lines.size();
        for (size_t i = 0; i < l->lines.size(); ++i) {
            const line& ln = l->lines[i];
            if (y < ln.top + ln.logical_h) { li = i; break; }
        }
        if (li == l->lines.size()) { inside = false; li = l->lines.size() - 1; }
    }
    const line& ln = l->lines[li];
    const int start = ln.pub.start_index, end = ln.pub.start_index + ln.pub.length;
    int rel = x - ln.x;
    if (rel < 0 || start == end) {
        if (index) *index = start;
        if (trailing) *trailing = 0;
        return rel >= 0 && inside && start != end ? TRUE : FALSE;
    }
    int i = start, last = start;
    while (i < end) {
        const int j = next_grapheme(l->text, i, end);
        int cx, cw;
        char_x(l, ln, i, &cx, &cw);
        // characters inside a ligature share the ligature's span
        if (rel < cx + cw) {
            if (index) *index = i;
            if (trailing) *trailing = rel >= cx + cw / 2 ? 1 : 0;
            return inside ? TRUE : FALSE;
        }
        last = i;
        i = j;
    }
    if (index) *index = last;
    if (trailing) *trailing = 1;
    return FALSE;
}

// ============================================================== iterators
PangoLayoutIter* pango_layout_get_iter(PangoLayout* l)
{
    layout_update(l);
    auto* it = new PangoLayoutIter();
    it->layout = l;
    it->line = 0;
    it->index = l->lines.empty() ? 0 : l->lines[0].pub.start_index;
    return it;
}
void pango_layout_iter_free(PangoLayoutIter* it) { delete it; }

gboolean pango_layout_iter_next_line(PangoLayoutIter* it)
{
    PangoLayout* l = it->layout;
    if (it->line + 1 >= (int)l->lines.size()) return FALSE;
    ++it->line;
    it->index = l->lines[it->line].pub.start_index;
    return TRUE;
}

gboolean pango_layout_iter_next_char(PangoLayoutIter* it)
{
    PangoLayout* l = it->layout;
    if (l->lines.empty()) return FALSE;
    const auto& p = l->lines[it->line].pub;
    const int end = p.start_index + p.length;
    if (it->index < end) {
        it->index = next_grapheme(l->text, it->index, end);
        if (it->index < end) return TRUE;
    }
    if (it->line + 1 < (int)l->lines.size()) {
        ++it->line;
        it->index = l->lines[it->line].pub.start_index;
        return TRUE;
    }
    return it->index < end ? TRUE : FALSE;
}

int pango_layout_iter_get_index(PangoLayoutIter* it) { return it->index; }
PangoLayoutLine* pango_layout_iter_get_line_readonly(PangoLayoutIter* it)
{
    return it->layout->lines.empty() ? nullptr : &it->layout->lines[it->line].pub;
}
PangoLayoutLine* pango_layout_iter_get_line(PangoLayoutIter* it) { return pango_layout_iter_get_line_readonly(it); }

void pango_layout_iter_get_line_extents(PangoLayoutIter* it, PangoRectangle* ink, PangoRectangle* logical)
{
    PangoRectangle r{0, 0, 0, 0};
    if (!it->layout->lines.empty()) {
        const line& ln = it->layout->lines[it->line];
        r = { ln.x, ln.top, ln.width, ln.logical_h };
    }
    if (ink) *ink = r;
    if (logical) *logical = r;
}

int pango_layout_iter_get_baseline(PangoLayoutIter* it)
{
    return it->layout->lines.empty() ? 0 : it->layout->lines[it->line].baseline;
}

void pango_layout_line_get_extents(PangoLayoutLine* pl, PangoRectangle* ink, PangoRectangle* logical)
{
    PangoRectangle r{0, 0, 0, 0};
    for (auto& ln : pl->layout->lines)
        if (&ln.pub == pl) r = { 0, ln.logical_y, ln.width, ln.logical_h };
    if (ink) *ink = r;
    if (logical) *logical = r;
}
void pango_layout_line_get_pixel_extents(PangoLayoutLine* pl, PangoRectangle* ink, PangoRectangle* logical)
{
    pango_layout_line_get_extents(pl, ink, logical);
    if (ink) to_pixels(ink);
    if (logical) to_pixels(logical);
}

// ============================================================== rendering
static void set_rgba(cairo_t* cr, const PangoColor& c, int alpha)
{
    cairo_set_source_rgba(cr, c.red / 65535.0, c.green / 65535.0, c.blue / 65535.0, alpha / 65535.0);
}

static void draw_layout(cairo_t* cr, PangoLayout* l, bool path)
{
    layout_update(l);
    double ox = 0, oy = 0;
    if (cairo_has_current_point(cr)) cairo_get_current_point(cr, &ox, &oy);
    cairo_pattern_t* source = cairo_pattern_reference(cairo_get_source(cr));
    PangoContext* ctx = l->context;
    std::vector<cairo_glyph_t> glyphs;

    for (const line& ln : l->lines) {
        const double base_y = oy + ln.baseline / (double)PANGO_SCALE;
        for (const run& r : ln.runs) {
            const double rx = ox + (ln.x + r.x) / (double)PANGO_SCALE;
            const double by = base_y - r.rise / (double)PANGO_SCALE;
            if (!path && r.has_bg) {
                set_rgba(cr, r.bg, r.bg_alpha);
                cairo_rectangle(cr, rx, oy + ln.top / (double)PANGO_SCALE, r.width / (double)PANGO_SCALE,
                                ln.logical_h / (double)PANGO_SCALE);
                cairo_fill(cr);
            }
            if (r.shape) {
                int gx = 0;
                for (size_t g = 0; g < r.glyphs.size(); ++g) {
                    if (ctx && ctx->shape_renderer) {
                        cairo_save(cr);
                        cairo_move_to(cr, rx + gx / (double)PANGO_SCALE, by);
                        ctx->shape_renderer(cr, r.shape, path ? TRUE : FALSE, ctx->shape_data);
                        cairo_restore(cr);
                    }
                    gx += r.x_adv[g];
                }
                continue;
            }
            if (!r.font || !r.font->scaled) continue;
            glyphs.clear();
            int gx = 0;
            for (size_t g = 0; g < r.glyphs.size(); ++g) {
                if (r.glyphs[g] != 0xFFFFFFFFu) {
                    cairo_glyph_t cg;
                    cg.index = r.glyphs[g];
                    cg.x = rx + (gx + r.x_off[g]) / (double)PANGO_SCALE;
                    cg.y = by - r.y_off[g] / (double)PANGO_SCALE;
                    glyphs.push_back(cg);
                }
                gx += r.x_adv[g];
            }
            if (path) {
                cairo_set_scaled_font(cr, r.font->scaled);
                if (!glyphs.empty()) cairo_glyph_path(cr, glyphs.data(), (int)glyphs.size());
                continue;
            }
            if (r.has_fg) set_rgba(cr, r.fg, r.fg_alpha);
            else cairo_set_source(cr, source);
            cairo_set_scaled_font(cr, r.font->scaled);
            if (!glyphs.empty()) cairo_show_glyphs(cr, glyphs.data(), (int)glyphs.size());

            const double w = r.width / (double)PANGO_SCALE;
            const double th = r.font->underline_thickness / (double)PANGO_SCALE;
            if (r.underline != PANGO_UNDERLINE_NONE) {
                if (r.has_ul_color) set_rgba(cr, r.ul_color, 65535);
                double uy = by + r.font->underline_pos / (double)PANGO_SCALE;
                if (r.underline == PANGO_UNDERLINE_LOW || r.underline == PANGO_UNDERLINE_ERROR)
                    uy = by + r.font->descent / (double)PANGO_SCALE - th;
                cairo_rectangle(cr, rx, uy, w, th);
                if (r.underline == PANGO_UNDERLINE_DOUBLE || r.underline == PANGO_UNDERLINE_DOUBLE_LINE)
                    cairo_rectangle(cr, rx, uy + 2 * th, w, th);
                cairo_fill(cr);
                if (r.has_ul_color) {
                    if (r.has_fg) set_rgba(cr, r.fg, r.fg_alpha);
                    else cairo_set_source(cr, source);
                }
            }
            if (r.strikethrough) {
                const double sth = r.font->strike_thickness / (double)PANGO_SCALE;
                cairo_rectangle(cr, rx, by - r.font->strike_pos / (double)PANGO_SCALE - sth / 2, w, sth);
                cairo_fill(cr);
            }
        }
    }
    cairo_set_source(cr, source);
    cairo_pattern_destroy(source);
    if (!path) cairo_move_to(cr, ox, oy);
}

void pango_cairo_show_layout(cairo_t* cr, PangoLayout* layout) { draw_layout(cr, layout, false); }
void pango_cairo_layout_path(cairo_t* cr, PangoLayout* layout) { draw_layout(cr, layout, true); }
void pango_cairo_update_context(cairo_t*, PangoContext*) {}
void pango_cairo_update_layout(cairo_t*, PangoLayout* layout) { layout->dirty = true; }
PangoContext* pango_cairo_create_context(cairo_t*)
{
    return pango_font_map_create_context(pango_cairo_font_map_get_default());
}
PangoLayout* pango_cairo_create_layout(cairo_t* cr)
{
    PangoContext* c = pango_cairo_create_context(cr);
    PangoLayout* l = pango_layout_new(c);
    g_object_unref(c);
    return l;
}
