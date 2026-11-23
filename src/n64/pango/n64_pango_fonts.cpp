/*
 * n64_pango_fonts.cpp -- fonts for the Nintendo 64 Pango subset.
 *
 * Replaces fontconfig + PangoFcFontMap: font files registered from a directory
 * (Wesnoth's fonts/), matched by family, weight and style as fontconfig
 * would, with per-character fallback to other registered fonts. Each face is
 * loaded with FreeType (whole file in memory for normal fonts, streamed from
 * the VFS for the large CJK fallbacks), shaped with HarfBuzz and drawn with
 * cairo-ft -- the libraries Pango uses on the PC.
 */
#include "n64_pango_private.hpp"
#include "n64_vfs.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <cairo-ft.h>
#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

namespace n64pango {
namespace {

FT_Library s_ft = nullptr;
std::vector<std::unique_ptr<face_entry>> s_faces;
std::map<std::tuple<face_entry*, int, bool, bool>, std::unique_ptr<font_inst>> s_insts;
std::map<unsigned, face_entry*> s_fallback_cache;
cairo_font_options_t* s_options = nullptr;

bool ft_init()
{
    if (!s_ft && FT_Init_FreeType(&s_ft) != 0) {
        std::fprintf(stderr, "[pango] FreeType initialisation failed\n");
        return false;
    }
    return true;
}

std::string lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// FreeType stream over a VFS reader, with a small block cache: FreeType
// reads tables and glyph outlines in many small pieces.
struct font_stream {
    std::unique_ptr<n64vfs::reader> r;
    static constexpr unsigned long k_block = 8192;
    unsigned char block[k_block];
    unsigned long block_pos = ~0ul, block_len = 0;
};

unsigned long stream_read(FT_Stream s, unsigned long offset, unsigned char* buffer, unsigned long count)
{
    auto* fs = static_cast<font_stream*>(s->descriptor.pointer);
    if (count == 0) return offset <= s->size ? 0 : 1;     // seek only (0 = success)
    unsigned long done = 0;
    while (done < count) {
        const unsigned long pos = offset + done;
        if (pos >= s->size) break;
        if (fs->block_pos == ~0ul || pos < fs->block_pos || pos >= fs->block_pos + fs->block_len) {
            const unsigned long start = pos / font_stream::k_block * font_stream::k_block;
            if (!fs->r->seek(start)) break;
            fs->block_len = (unsigned long)fs->r->read(fs->block, font_stream::k_block);
            fs->block_pos = start;
            if (fs->block_len == 0) break;
        }
        const unsigned long avail = fs->block_pos + fs->block_len - pos;
        const unsigned long n = std::min(avail, count - done);
        std::memcpy(buffer + done, fs->block + (pos - fs->block_pos), n);
        done += n;
    }
    return done;
}
void stream_close(FT_Stream s)
{
    delete static_cast<font_stream*>(s->descriptor.pointer);
    delete s;
}

// Faces are streamed from the VFS (never loaded whole): the font directory
// holds ~28 MB of fonts and fallback lookups touch many of them.
FT_Error open_face(const std::string& path, int index, FT_Face* out)
{
    auto r = n64vfs::open_read(path);
    if (!r) return FT_Err_Cannot_Open_Resource;
    auto* fs = new font_stream();
    auto* st = new FT_StreamRec();
    st->size = (unsigned long)r->size();
    fs->r = std::move(r);
    st->descriptor.pointer = fs;
    st->read = stream_read;
    st->close = stream_close;
    FT_Open_Args args{};
    args.flags = FT_OPEN_STREAM;
    args.stream = st;
    return FT_Open_Face(s_ft, &args, index, out);   // on failure FreeType closes the stream
}

void register_file(const std::string& path)
{
    if (!ft_init()) return;
    int count = 1;
    for (int i = 0; i < count; ++i) {
        FT_Face f = nullptr;
        if (open_face(path, i, &f) != 0) return;
        count = (int)f->num_faces;
        auto e = std::make_unique<face_entry>();
        e->path = path;
        e->index = i;
        e->family = f->family_name ? f->family_name : "";
        e->italic = (f->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
        if (auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(f, FT_SFNT_OS2)))
            e->weight = os2->usWeightClass;
        else
            e->weight = (f->style_flags & FT_STYLE_FLAG_BOLD) ? 700 : 400;
        FT_Done_Face(f);
        s_faces.push_back(std::move(e));
    }
}

// fontconfig-style aliases for generic family names used in markup.
std::string resolve_alias(const std::string& fam)
{
    const std::string l = lower(fam);
    if (l == "monospace" || l == "mono") return "dejavu sans mono";
    if (l == "sans" || l == "sans-serif" || l == "serif") return "dejavu sans";
    return l;
}

std::vector<std::string> split_families(const std::string& families)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= families.size()) {
        size_t j = families.find(',', i);
        if (j == std::string::npos) j = families.size();
        std::string f = families.substr(i, j - i);
        while (!f.empty() && f.front() == ' ') f.erase(f.begin());
        while (!f.empty() && f.back() == ' ') f.pop_back();
        if (!f.empty()) out.push_back(resolve_alias(f));
        i = j + 1;
    }
    return out;
}

// Best face of a family for weight/style (fontconfig's closest-match rule).
face_entry* match_family(const std::string& lfam, int weight, PangoStyle style)
{
    face_entry* best = nullptr;
    int best_score = 1 << 30;
    const bool want_italic = style != PANGO_STYLE_NORMAL;
    for (auto& f : s_faces) {
        if (lower(f->family) != lfam) continue;
        int score = std::abs(f->weight - weight) + ((f->italic != want_italic) ? 10000 : 0);
        // between equally distant weights, prefer the heavier for bold
        // requests and the lighter for light ones (CSS font matching)
        if (f->weight > weight && weight <= 400) score += 1;
        if (f->weight < weight && weight > 400) score += 1;
        if (score < best_score) { best = f.get(); best_score = score; }
    }
    return best;
}

face_entry* default_face()
{
    if (face_entry* f = match_family("dejavu sans", 400, PANGO_STYLE_NORMAL)) return f;
    return s_faces.empty() ? nullptr : s_faces.front().get();
}

} // namespace

// --------------------------------------------------------------- face_entry
bool face_entry::load()
{
    if (ft) return true;
    if (!ft_init()) return false;
    if (open_face(path, index, &ft) != 0) {
        std::fprintf(stderr, "[pango] cannot load font %s\n", path.c_str());
        ft = nullptr;
        return false;
    }
    hb = hb_ft_face_create_referenced(ft);
    cairo_plain = cairo_ft_font_face_create_for_ft_face(ft, 0);
    return true;
}

bool face_entry::has_glyph(unsigned cp)
{
    return load() && FT_Get_Char_Index(ft, cp) != 0;
}

// ------------------------------------------------------------------ fonts
const cairo_font_options_t* font_options()
{
    if (!s_options) {
        // Wesnoth's fonts.conf: grey antialiasing, full hinting, no subpixel
        // rendering; text.cpp sets hint style full and hint metrics on.
        s_options = cairo_font_options_create();
        cairo_font_options_set_antialias(s_options, CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(s_options, CAIRO_HINT_STYLE_FULL);
        cairo_font_options_set_hint_metrics(s_options, CAIRO_HINT_METRICS_ON);
        cairo_font_options_set_subpixel_order(s_options, CAIRO_SUBPIXEL_ORDER_DEFAULT);
    }
    return s_options;
}

void register_font_dir(const std::string& dir)
{
    std::vector<std::pair<std::string, bool>> items;
    if (!n64vfs::list(dir, items)) return;
    std::sort(items.begin(), items.end());
    for (auto& it : items) {
        if (it.second) continue;
        const std::string l = lower(it.first);
        auto ends = [&](const char* ext) {
            size_t n = std::strlen(ext);
            return l.size() > n && l.compare(l.size() - n, n, ext) == 0;
        };
        if (ends(".ttf") || ends(".otf") || ends(".ttc")) register_file(dir + "/" + it.first);
    }
    s_fallback_cache.clear();
}

static font_inst* instantiate(face_entry* face, double px, bool bold, bool italic)
{
    if (!face || !face->load()) return nullptr;
    const int key_px = (int)std::lround(px * 64);
    auto key = std::make_tuple(face, key_px, bold, italic);
    auto it = s_insts.find(key);
    if (it != s_insts.end()) return it->second.get();

    auto fi = std::make_unique<font_inst>();
    fi->face = face;
    fi->px = px;
    fi->synth_bold = bold;
    fi->synth_italic = italic;

    // Positions in Pango units (1/1024 px), as Pango scales its HarfBuzz fonts.
    fi->hb = hb_font_create(face->hb);
    const int scale_pu = (int)std::lround(px * PANGO_SCALE);
    hb_font_set_scale(fi->hb, scale_pu, scale_pu);
    hb_font_set_ppem(fi->hb, (unsigned)std::lround(px), (unsigned)std::lround(px));
    hb_ot_font_set_funcs(fi->hb);

    cairo_font_face_t* cf = face->cairo_plain;
    if (bold || italic) {
        cairo_font_face_t** slot = bold && italic ? &face->cairo_bold_oblique
                                 : bold ? &face->cairo_bold : &face->cairo_oblique;
        if (!*slot) {
            *slot = cairo_ft_font_face_create_for_ft_face(face->ft, 0);
            cairo_ft_font_face_set_synthesize(*slot, (bold ? CAIRO_FT_SYNTHESIZE_BOLD : 0) |
                                                     (italic ? CAIRO_FT_SYNTHESIZE_OBLIQUE : 0));
        }
        cf = *slot;
    }
    cairo_matrix_t fm, ctm;
    cairo_matrix_init_scale(&fm, px, px);
    cairo_matrix_init_identity(&ctm);
    fi->scaled = cairo_scaled_font_create(cf, &fm, &ctm, font_options());

    cairo_font_extents_t fe;
    cairo_scaled_font_extents(fi->scaled, &fe);
    fi->ascent = (int)std::lround(fe.ascent * PANGO_SCALE);
    fi->descent = (int)std::lround(fe.descent * PANGO_SCALE);

    const double upem = face->ft->units_per_EM ? face->ft->units_per_EM : 2048;
    const double scale = px / upem * PANGO_SCALE;
    int ul_thick = (int)std::lround(face->ft->underline_thickness * scale);
    int ul_pos = (int)std::lround(-face->ft->underline_position * scale);
    if (ul_thick < PANGO_SCALE) ul_thick = PANGO_SCALE;               // hinted: at least 1 px
    fi->underline_thickness = (ul_thick + PANGO_SCALE / 2) / PANGO_SCALE * PANGO_SCALE;
    fi->underline_pos = (ul_pos + PANGO_SCALE / 2) / PANGO_SCALE * PANGO_SCALE;
    if (auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face->ft, FT_SFNT_OS2))) {
        fi->strike_pos = (int)std::lround(os2->yStrikeoutPosition * scale);
        fi->strike_thickness = std::max<int>(PANGO_SCALE, (int)std::lround(os2->yStrikeoutSize * scale));
    } else {
        fi->strike_pos = fi->ascent / 3;
        fi->strike_thickness = fi->underline_thickness;
    }

    // Approximate widths as Pango computes them: the average advance over the
    // language's sample string, and the widest digit.
    auto advance_of = [&](const char* s) {
        hb_buffer_t* b = hb_buffer_create();
        hb_buffer_add_utf8(b, s, -1, 0, -1);
        hb_buffer_guess_segment_properties(b);
        hb_shape(fi->hb, b, nullptr, 0);
        unsigned n = 0;
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(b, &n);
        long w = 0;
        for (unsigned i = 0; i < n; ++i)                // whole pixels
            w += (pos[i].x_advance + PANGO_SCALE / 2) / PANGO_SCALE * PANGO_SCALE;
        hb_buffer_destroy(b);
        return w;
    };
    static const char sample[] = "The quick brown fox jumps over the lazy dog.";
    fi->approx_char_width = (int)(advance_of(sample) / (long)(sizeof sample - 1));
    long digit = 0;
    for (char d = '0'; d <= '9'; ++d) {
        char s[2] = { d, 0 };
        digit = std::max(digit, advance_of(s));
    }
    fi->approx_digit_width = (int)digit;

    font_inst* raw = fi.get();
    s_insts.emplace(key, std::move(fi));
    return raw;
}

static face_entry* select_face(const std::string& families, int weight, PangoStyle style,
                               bool* synth_bold, bool* synth_italic)
{
    face_entry* f = nullptr;
    for (const auto& fam : split_families(families)) {
        if ((f = match_family(fam, weight, style))) break;
    }
    if (!f) f = match_family("dejavu sans", weight, style);
    if (!f) f = default_face();
    if (f) {
        *synth_bold = weight >= 600 && f->weight < 600;
        *synth_italic = style != PANGO_STYLE_NORMAL && !f->italic;
    }
    return f;
}

font_inst* get_font(const std::string& families, int weight, PangoStyle style, double px)
{
    bool sb = false, si = false;
    face_entry* f = select_face(families, weight, style, &sb, &si);
    return instantiate(f, px, sb, si);
}

font_inst* get_font_for_char(const std::string& families, int weight, PangoStyle style, double px, unsigned cp)
{
    bool sb = false, si = false;
    face_entry* primary = select_face(families, weight, style, &sb, &si);
    // Characters every font "has" (spaces, controls) stay in the primary font.
    if (!primary || cp < 0x21 || (cp >= 0x7F && cp < 0xA0) || cp == 0x200B || cp == 0xFEFF ||
        primary->has_glyph(cp))
        return instantiate(primary, px, sb, si);

    for (const auto& fam : split_families(families)) {
        face_entry* f = match_family(fam, weight, style);
        if (f && f->has_glyph(cp)) return instantiate(f, px, weight >= 600 && f->weight < 600,
                                                      style != PANGO_STYLE_NORMAL && !f->italic);
    }
    face_entry* fb = nullptr;
    auto it = s_fallback_cache.find(cp);
    if (it != s_fallback_cache.end()) {
        fb = it->second;
    } else {
        // DejaVu Sans first (it covers most symbols), then everything else in
        // registration order; the big CJK fonts come last alphabetically-ish.
        face_entry* dv = match_family("dejavu sans", weight, style);
        if (dv && dv->has_glyph(cp)) fb = dv;
        for (size_t i = 0; !fb && i < s_faces.size(); ++i) {
            face_entry* f = s_faces[i].get();
            if (f != primary && f->has_glyph(cp)) fb = f;
        }
        s_fallback_cache[cp] = fb;
    }
    if (!fb) return instantiate(primary, px, sb, si);          // .notdef box
    return instantiate(fb, px, weight >= 600 && fb->weight < 600, style != PANGO_STYLE_NORMAL && !fb->italic);
}

} // namespace n64pango

// ============================================================ public API
using namespace n64pango;

PangoFontMap* pango_cairo_font_map_get_default(void)
{
    static PangoFontMap* map = [] { auto* m = new PangoFontMap(); m->refs = 1 << 30; return m; }();
    return map;
}

PangoContext* pango_font_map_create_context(PangoFontMap* fontmap)
{
    auto* c = new PangoContext();
    c->map = fontmap;
    return c;
}

PangoFontMap* pango_context_get_font_map(PangoContext* context) { return context->map; }

void pango_cairo_context_set_resolution(PangoContext* context, double dpi) { context->dpi = dpi > 0 ? dpi : 96.0; }
double pango_cairo_context_get_resolution(PangoContext* context) { return context->dpi; }

void pango_cairo_context_set_font_options(PangoContext*, const cairo_font_options_t* options)
{
    if (!options) return;
    cairo_font_options_t* o = const_cast<cairo_font_options_t*>(font_options());
    // Antialias "default" means the fontconfig setting (grey, see fonts.conf).
    cairo_font_options_t* m = cairo_font_options_copy(options);
    if (cairo_font_options_get_antialias(m) == CAIRO_ANTIALIAS_DEFAULT)
        cairo_font_options_set_antialias(m, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_merge(o, m);
    cairo_font_options_destroy(m);
}

void pango_cairo_context_set_shape_renderer(PangoContext* context, PangoCairoShapeRendererFunc func,
                                            gpointer data, GDestroyNotify dnotify)
{
    if (context->shape_notify) context->shape_notify(context->shape_data);
    context->shape_renderer = func;
    context->shape_data = data;
    context->shape_notify = dnotify;
}

static double desc_px(PangoContext* ctx, const PangoFontDescription* d)
{
    const int size = (d && (d->mask & PANGO_FONT_MASK_SIZE)) ? d->size : 12 * PANGO_SCALE;
    const bool abs = d && d->size_absolute;
    const double dpi = ctx ? ctx->dpi : 96.0;
    return abs ? size / (double)PANGO_SCALE : size / (double)PANGO_SCALE * dpi / 72.0;
}

PangoFont* pango_font_map_load_font(PangoFontMap*, PangoContext* context, const PangoFontDescription* desc)
{
    const std::string fam = (desc && (desc->mask & PANGO_FONT_MASK_FAMILY)) ? desc->family : "sans";
    font_inst* fi = get_font(fam, desc ? desc->weight : 400, desc ? desc->style : PANGO_STYLE_NORMAL,
                             desc_px(context, desc));
    if (!fi) return nullptr;
    auto* f = new PangoFont();
    f->inst = fi;
    return f;
}

PangoFontMetrics* pango_font_get_metrics(PangoFont* font, PangoLanguage*)
{
    auto* m = new PangoFontMetrics();
    if (font && font->inst) {
        font_inst* fi = font->inst;
        m->ascent = fi->ascent;
        m->descent = fi->descent;
        m->height = fi->ascent + fi->descent;
        m->approx_char_width = fi->approx_char_width;
        m->approx_digit_width = fi->approx_digit_width;
        m->underline_position = -fi->underline_pos;
        m->underline_thickness = fi->underline_thickness;
    }
    return m;
}
PangoFontMetrics* pango_font_metrics_ref(PangoFontMetrics* m) { if (m) ++m->refs; return m; }
void pango_font_metrics_unref(PangoFontMetrics* m) { if (m && --m->refs == 0) delete m; }
int pango_font_metrics_get_ascent(PangoFontMetrics* m) { return m->ascent; }
int pango_font_metrics_get_descent(PangoFontMetrics* m) { return m->descent; }
int pango_font_metrics_get_height(PangoFontMetrics* m) { return m->height; }
int pango_font_metrics_get_approximate_char_width(PangoFontMetrics* m) { return m->approx_char_width; }
int pango_font_metrics_get_approximate_digit_width(PangoFontMetrics* m) { return m->approx_digit_width; }
int pango_font_metrics_get_underline_position(PangoFontMetrics* m) { return m->underline_position; }
int pango_font_metrics_get_underline_thickness(PangoFontMetrics* m) { return m->underline_thickness; }

// ============================================================ fontconfig
struct _FcConfig { int dummy; };
#include <fontconfig/fontconfig.h>

FcConfig* FcConfigGetCurrent(void) { static FcConfig c; return &c; }
FcBool FcInit(void) { return FcTrue; }
FcBool FcConfigAppFontAddDir(FcConfig*, const FcChar8* dir)
{
    std::string d(reinterpret_cast<const char*>(dir));
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    if (n64vfs::stat(d) != n64vfs::kind::dir) return FcFalse;
    register_font_dir(d);
    return FcTrue;
}
FcBool FcConfigAppFontAddFile(FcConfig*, const FcChar8* file)
{
    register_file(reinterpret_cast<const char*>(file));
    return FcTrue;
}
void FcConfigAppFontClear(FcConfig*) {}
FcBool FcConfigParseAndLoadFromMemory(FcConfig*, const FcChar8*, FcBool) { return FcTrue; }
