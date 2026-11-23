#pragma once
/*
 * n64_pango_private.hpp -- internals of the Nintendo 64 Pango subset.
 */
#include <pango/pangocairo.h>

#include <memory>
#include <string>
#include <vector>

typedef struct FT_FaceRec_* FT_Face;
typedef struct hb_face_t hb_face_t;
typedef struct hb_font_t hb_font_t;

namespace n64pango {

// GObject stand-in: g_object_ref/unref work on everything derived from this.
struct object {
    int refs = 1;
    virtual ~object() = default;
};

bool parse_weight(const std::string& s, PangoWeight* out);
bool parse_style(const std::string& s, PangoStyle* out);

// ------------------------------------------------------------ font files
struct face_entry {
    std::string path;           // VFS path
    int index = 0;              // face index in the file
    std::string family;         // FreeType family name
    int weight = 400;           // OS/2 usWeightClass
    bool italic = false;
    // loaded on first use
    FT_Face ft = nullptr;
    hb_face_t* hb = nullptr;
    cairo_font_face_t* cairo_plain = nullptr;
    cairo_font_face_t* cairo_bold = nullptr;      // synthetic emboldening
    cairo_font_face_t* cairo_oblique = nullptr;   // synthetic slant
    cairo_font_face_t* cairo_bold_oblique = nullptr;
    bool load();
    bool has_glyph(unsigned cp);
};

// A face at a size, with HarfBuzz and cairo objects and metrics.
struct font_inst {
    face_entry* face = nullptr;
    double px = 0;              // pixel size (em)
    bool synth_bold = false, synth_italic = false;
    hb_font_t* hb = nullptr;
    cairo_scaled_font_t* scaled = nullptr;
    int ascent = 0, descent = 0;          // Pango units (logical, hinted)
    int underline_pos = 0, underline_thickness = 0;   // Pango units (pos: below baseline > 0)
    int strike_pos = 0, strike_thickness = 0;          // pos: above baseline > 0
    int approx_char_width = 0, approx_digit_width = 0;
};

// Font selection (the fontconfig/Pango font map replacement).
void register_font_dir(const std::string& dir);
font_inst* get_font(const std::string& families, int weight, PangoStyle style, double px);
// The font for a code point: the first of `families` with a glyph for it,
// then any registered font (fallback, as fontconfig would).
font_inst* get_font_for_char(const std::string& families, int weight, PangoStyle style, double px, unsigned cp);
const cairo_font_options_t* font_options();

} // namespace n64pango

// ---------------------------------------------------------------- objects
struct _PangoFontDescription {
    unsigned mask = 0;
    std::string family;
    int size = 0;                    // Pango units (points, or device units if absolute)
    bool size_absolute = false;
    PangoWeight weight = PANGO_WEIGHT_NORMAL;
    PangoStyle style = PANGO_STYLE_NORMAL;
};

struct _PangoAttrList {
    int refs = 1;
    std::vector<PangoAttribute*> attrs;    // sorted by start_index
    ~_PangoAttrList();
};

struct _PangoFontMap : n64pango::object {};

struct _PangoContext : n64pango::object {
    PangoFontMap* map = nullptr;
    double dpi = 96.0;
    PangoCairoShapeRendererFunc shape_renderer = nullptr;
    gpointer shape_data = nullptr;
    GDestroyNotify shape_notify = nullptr;
    ~_PangoContext() override { if (shape_notify) shape_notify(shape_data); }
};

struct _PangoFont : n64pango::object {
    n64pango::font_inst* inst = nullptr;
};

struct _PangoFontMetrics {
    int refs = 1;
    int ascent = 0, descent = 0, height = 0;
    int approx_char_width = 0, approx_digit_width = 0;
    int underline_position = 0, underline_thickness = 0;
};

namespace n64pango {

// One shaped piece of a line: a single font and attribute set.
struct run {
    int start = 0, end = 0;              // byte range in the layout text
    font_inst* font = nullptr;
    // attributes
    bool has_fg = false, has_bg = false, has_ul_color = false;
    PangoColor fg{}, bg{}, ul_color{};
    int fg_alpha = 65535, bg_alpha = 65535;
    PangoUnderline underline = PANGO_UNDERLINE_NONE;
    bool strikethrough = false;
    int rise = 0;                        // Pango units
    double line_height = 0;              // factor (0 = unset)
    int abs_line_height = 0;             // Pango units (0 = unset)
    PangoAttrShape* shape = nullptr;     // owned by the layout's attribute copies
    bool ellipsis = false;               // the "…" run
    // glyphs
    std::vector<unsigned> glyphs;
    std::vector<int> x_adv, x_off, y_off;  // Pango units
    std::vector<int> cluster;              // byte index of each glyph's character
    int width = 0;                       // Pango units
    int x = 0;                           // position in the line
};

struct line {
    PangoLayoutLine pub{};
    std::vector<run> runs;
    int width = 0;                       // logical width (Pango units)
    int ascent = 0, descent = 0;         // font extents
    int logical_y = 0, logical_h = 0;    // relative to the baseline (y < 0)
    int x = 0;                           // alignment offset
    int top = 0;                         // y of the logical rectangle's top
    int baseline = 0;                    // y of the baseline
    bool ellipsized = false;
};

} // namespace n64pango

struct _PangoLayout : n64pango::object {
    PangoContext* context = nullptr;
    std::string text;
    PangoAttrList* attrs = nullptr;            // as set by the caller (referenced)
    PangoFontDescription* desc = nullptr;
    int width = -1, height = -1;
    PangoWrapMode wrap = PANGO_WRAP_WORD;
    PangoEllipsizeMode ellipsize = PANGO_ELLIPSIZE_NONE;
    PangoAlignment alignment = PANGO_ALIGN_LEFT;
    float line_spacing = 0;
    int spacing = 0;
    // computed
    bool dirty = true;
    std::vector<n64pango::line> lines;
    std::vector<PangoAttribute*> shape_attrs;  // copies kept alive for runs
    PangoRectangle logical{};
    bool is_ellipsized = false;
    ~_PangoLayout() override;
};

struct _PangoLayoutIter {
    PangoLayout* layout = nullptr;
    int line = 0;
    int index = 0;
};
