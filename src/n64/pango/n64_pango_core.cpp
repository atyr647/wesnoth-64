/*
 * n64_pango_core.cpp -- Pango subset for the Nintendo 64: object model, font
 * descriptions, colours, attributes and attribute lists, markup parsing.
 * The layout engine and cairo rendering are in n64_pango_layout.cpp.
 */
#include "n64_pango_private.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ======================================================== GLib-like basics
gpointer g_object_ref(gpointer object)
{
    if (object) ++static_cast<n64pango::object*>(object)->refs;
    return object;
}

void g_object_unref(gpointer object)
{
    if (!object) return;
    auto* o = static_cast<n64pango::object*>(object);
    if (--o->refs == 0) delete o;
}

void g_free(gpointer mem) { std::free(mem); }

const char* pango_version_string(void) { return PANGO_VERSION_STRING; }
double pango_units_to_double(int i) { return (double)i / PANGO_SCALE; }
int pango_units_from_double(double d) { return (int)std::floor(d * PANGO_SCALE + 0.5); }

// ============================================================== colours
namespace {
struct named_color { const char* name; unsigned char r, g, b; };
// X11 colour names (Pango parses colours with the X11 table).
const named_color k_colors[] = {
    {"aliceblue",240,248,255},{"antiquewhite",250,235,215},{"aqua",0,255,255},{"aquamarine",127,255,212},
    {"azure",240,255,255},{"beige",245,245,220},{"bisque",255,228,196},{"black",0,0,0},
    {"blanchedalmond",255,235,205},{"blue",0,0,255},{"blueviolet",138,43,226},{"brown",165,42,42},
    {"burlywood",222,184,135},{"cadetblue",95,158,160},{"chartreuse",127,255,0},{"chocolate",210,105,30},
    {"coral",255,127,80},{"cornflowerblue",100,149,237},{"cornsilk",255,248,220},{"crimson",220,20,60},
    {"cyan",0,255,255},{"darkblue",0,0,139},{"darkcyan",0,139,139},{"darkgoldenrod",184,134,11},
    {"darkgray",169,169,169},{"darkgreen",0,100,0},{"darkgrey",169,169,169},{"darkkhaki",189,183,107},
    {"darkmagenta",139,0,139},{"darkolivegreen",85,107,47},{"darkorange",255,140,0},{"darkorchid",153,50,204},
    {"darkred",139,0,0},{"darksalmon",233,150,122},{"darkseagreen",143,188,143},{"darkslateblue",72,61,139},
    {"darkslategray",47,79,79},{"darkslategrey",47,79,79},{"darkturquoise",0,206,209},{"darkviolet",148,0,211},
    {"deeppink",255,20,147},{"deepskyblue",0,191,255},{"dimgray",105,105,105},{"dimgrey",105,105,105},
    {"dodgerblue",30,144,255},{"firebrick",178,34,34},{"floralwhite",255,250,240},{"forestgreen",34,139,34},
    {"fuchsia",255,0,255},{"gainsboro",220,220,220},{"ghostwhite",248,248,255},{"gold",255,215,0},
    {"goldenrod",218,165,32},{"gray",190,190,190},{"green",0,255,0},{"greenyellow",173,255,47},
    {"grey",190,190,190},{"honeydew",240,255,240},{"hotpink",255,105,180},{"indianred",205,92,92},
    {"indigo",75,0,130},{"ivory",255,255,240},{"khaki",240,230,140},{"lavender",230,230,250},
    {"lavenderblush",255,240,245},{"lawngreen",124,252,0},{"lemonchiffon",255,250,205},{"lightblue",173,216,230},
    {"lightcoral",240,128,128},{"lightcyan",224,255,255},{"lightgoldenrod",238,221,130},
    {"lightgoldenrodyellow",250,250,210},{"lightgray",211,211,211},{"lightgreen",144,238,144},
    {"lightgrey",211,211,211},{"lightpink",255,182,193},{"lightsalmon",255,160,122},{"lightseagreen",32,178,170},
    {"lightskyblue",135,206,250},{"lightslateblue",132,112,255},{"lightslategray",119,136,153},
    {"lightslategrey",119,136,153},{"lightsteelblue",176,196,222},{"lightyellow",255,255,224},{"lime",0,255,0},
    {"limegreen",50,205,50},{"linen",250,240,230},{"magenta",255,0,255},{"maroon",176,48,96},
    {"mediumaquamarine",102,205,170},{"mediumblue",0,0,205},{"mediumorchid",186,85,211},
    {"mediumpurple",147,112,219},{"mediumseagreen",60,179,113},{"mediumslateblue",123,104,238},
    {"mediumspringgreen",0,250,154},{"mediumturquoise",72,209,204},{"mediumvioletred",199,21,133},
    {"midnightblue",25,25,112},{"mintcream",245,255,250},{"mistyrose",255,228,225},{"moccasin",255,228,181},
    {"navajowhite",255,222,173},{"navy",0,0,128},{"navyblue",0,0,128},{"oldlace",253,245,230},
    {"olive",128,128,0},{"olivedrab",107,142,35},{"orange",255,165,0},{"orangered",255,69,0},
    {"orchid",218,112,214},{"palegoldenrod",238,232,170},{"palegreen",152,251,152},{"paleturquoise",175,238,238},
    {"palevioletred",219,112,147},{"papayawhip",255,239,213},{"peachpuff",255,218,185},{"peru",205,133,63},
    {"pink",255,192,203},{"plum",221,160,221},{"powderblue",176,224,230},{"purple",160,32,240},
    {"rebeccapurple",102,51,153},{"red",255,0,0},{"rosybrown",188,143,143},{"royalblue",65,105,225},
    {"saddlebrown",139,69,19},{"salmon",250,128,114},{"sandybrown",244,164,96},{"seagreen",46,139,87},
    {"seashell",255,245,238},{"sienna",160,82,45},{"silver",192,192,192},{"skyblue",135,206,235},
    {"slateblue",106,90,205},{"slategray",112,128,144},{"slategrey",112,128,144},{"snow",255,250,250},
    {"springgreen",0,255,127},{"steelblue",70,130,180},{"tan",210,180,140},{"teal",0,128,128},
    {"thistle",216,191,216},{"tomato",255,99,71},{"turquoise",64,224,208},{"violet",238,130,238},
    {"violetred",208,32,144},{"wheat",245,222,179},{"white",255,255,255},{"whitesmoke",245,245,245},
    {"yellow",255,255,0},{"yellowgreen",154,205,50},
};

int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

gboolean pango_color_parse(PangoColor* color, const char* spec)
{
    if (!spec) return FALSE;
    while (*spec == ' ') ++spec;
    if (*spec == '#') {
        ++spec;
        size_t len = std::strlen(spec);
        if (len % 3 != 0 || len < 3 || len > 12) return FALSE;
        size_t n = len / 3;
        unsigned v[3];
        for (int c = 0; c < 3; ++c) {
            unsigned x = 0;
            for (size_t i = 0; i < n; ++i) {
                int h = hexval(spec[c * n + i]);
                if (h < 0) return FALSE;
                x = x * 16 + (unsigned)h;
            }
            // scale n hex digits to 16 bits by bit replication (as Pango does)
            unsigned bits = (unsigned)n * 4, out = 0;
            for (int shift = 16 - (int)bits; ; shift -= (int)bits) {
                out |= shift >= 0 ? (x << shift) : (x >> -shift);
                if (shift <= 0) break;
            }
            v[c] = out & 0xFFFF;
        }
        if (color) { color->red = (guint16)v[0]; color->green = (guint16)v[1]; color->blue = (guint16)v[2]; }
        return TRUE;
    }
    std::string name;
    for (const char* p = spec; *p; ++p) if (*p != ' ') name += (char)std::tolower((unsigned char)*p);
    for (const auto& c : k_colors) {
        if (name == c.name) {
            if (color) { color->red = c.r * 257; color->green = c.g * 257; color->blue = c.b * 257; }
            return TRUE;
        }
    }
    return FALSE;
}

// ==================================================== font descriptions
PangoFontDescription* pango_font_description_new(void) { return new PangoFontDescription(); }
PangoFontDescription* pango_font_description_copy(const PangoFontDescription* d)
{
    return d ? new PangoFontDescription(*d) : nullptr;
}
void pango_font_description_free(PangoFontDescription* d) { delete d; }
void pango_font_description_set_family(PangoFontDescription* d, const char* family)
{
    d->family = family ? family : "";
    d->mask |= PANGO_FONT_MASK_FAMILY;
}
const char* pango_font_description_get_family(const PangoFontDescription* d)
{
    return (d->mask & PANGO_FONT_MASK_FAMILY) ? d->family.c_str() : nullptr;
}
void pango_font_description_set_size(PangoFontDescription* d, gint size)
{
    d->size = size;
    d->size_absolute = false;
    d->mask |= PANGO_FONT_MASK_SIZE;
}
void pango_font_description_set_absolute_size(PangoFontDescription* d, double size)
{
    d->size = (int)size;
    d->size_absolute = true;
    d->mask |= PANGO_FONT_MASK_SIZE;
}
gint pango_font_description_get_size(const PangoFontDescription* d) { return d->size; }
gboolean pango_font_description_get_size_is_absolute(const PangoFontDescription* d) { return d->size_absolute; }
void pango_font_description_set_weight(PangoFontDescription* d, PangoWeight w)
{
    d->weight = w;
    d->mask |= PANGO_FONT_MASK_WEIGHT;
}
PangoWeight pango_font_description_get_weight(const PangoFontDescription* d) { return d->weight; }
void pango_font_description_set_style(PangoFontDescription* d, PangoStyle s)
{
    d->style = s;
    d->mask |= PANGO_FONT_MASK_STYLE;
}
PangoStyle pango_font_description_get_style(const PangoFontDescription* d) { return d->style; }
PangoFontMask pango_font_description_get_set_fields(const PangoFontDescription* d) { return (PangoFontMask)d->mask; }

namespace n64pango {

bool parse_weight(const std::string& s, PangoWeight* out)
{
    static const struct { const char* n; PangoWeight w; } names[] = {
        {"thin",PANGO_WEIGHT_THIN},{"ultralight",PANGO_WEIGHT_ULTRALIGHT},{"ultra-light",PANGO_WEIGHT_ULTRALIGHT},
        {"extralight",PANGO_WEIGHT_ULTRALIGHT},{"extra-light",PANGO_WEIGHT_ULTRALIGHT},{"light",PANGO_WEIGHT_LIGHT},
        {"semilight",PANGO_WEIGHT_SEMILIGHT},{"semi-light",PANGO_WEIGHT_SEMILIGHT},{"demilight",PANGO_WEIGHT_SEMILIGHT},
        {"book",PANGO_WEIGHT_BOOK},{"normal",PANGO_WEIGHT_NORMAL},{"regular",PANGO_WEIGHT_NORMAL},
        {"medium",PANGO_WEIGHT_MEDIUM},{"semibold",PANGO_WEIGHT_SEMIBOLD},{"semi-bold",PANGO_WEIGHT_SEMIBOLD},
        {"demibold",PANGO_WEIGHT_SEMIBOLD},{"demi-bold",PANGO_WEIGHT_SEMIBOLD},{"bold",PANGO_WEIGHT_BOLD},
        {"ultrabold",PANGO_WEIGHT_ULTRABOLD},{"ultra-bold",PANGO_WEIGHT_ULTRABOLD},{"extrabold",PANGO_WEIGHT_ULTRABOLD},
        {"extra-bold",PANGO_WEIGHT_ULTRABOLD},{"heavy",PANGO_WEIGHT_HEAVY},{"black",PANGO_WEIGHT_HEAVY},
        {"ultraheavy",PANGO_WEIGHT_ULTRAHEAVY},{"ultra-heavy",PANGO_WEIGHT_ULTRAHEAVY},
    };
    std::string l;
    for (char c : s) l += (char)std::tolower((unsigned char)c);
    for (auto& n : names) if (l == n.n) { *out = n.w; return true; }
    char* end = nullptr;
    long v = std::strtol(l.c_str(), &end, 10);
    if (end && *end == 0 && !l.empty() && v >= 100 && v <= 1000) { *out = (PangoWeight)v; return true; }
    return false;
}

bool parse_style(const std::string& s, PangoStyle* out)
{
    std::string l;
    for (char c : s) l += (char)std::tolower((unsigned char)c);
    if (l == "normal" || l == "roman") { *out = PANGO_STYLE_NORMAL; return true; }
    if (l == "oblique") { *out = PANGO_STYLE_OBLIQUE; return true; }
    if (l == "italic") { *out = PANGO_STYLE_ITALIC; return true; }
    return false;
}

} // namespace n64pango

// "Family[,Family...] [style words] [size[px]]"
PangoFontDescription* pango_font_description_from_string(const char* str)
{
    auto* d = pango_font_description_new();
    if (!str) return d;
    std::string s(str);
    std::vector<std::string> words;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) words.push_back(s.substr(i, j - i));
        i = j;
    }
    // trailing size
    if (!words.empty()) {
        std::string w = words.back();
        bool px = w.size() > 2 && w.compare(w.size() - 2, 2, "px") == 0;
        std::string num = px ? w.substr(0, w.size() - 2) : w;
        char* end = nullptr;
        double v = std::strtod(num.c_str(), &end);
        if (!num.empty() && end && *end == 0) {
            if (px) pango_font_description_set_absolute_size(d, v * PANGO_SCALE);
            else pango_font_description_set_size(d, (int)(v * PANGO_SCALE + 0.5));
            words.pop_back();
        }
    }
    // trailing style options
    while (!words.empty()) {
        PangoWeight w;
        PangoStyle st;
        std::string l = words.back();
        if (n64pango::parse_weight(l, &w) && !std::isdigit((unsigned char)l[0])) pango_font_description_set_weight(d, w);
        else if (n64pango::parse_style(l, &st)) pango_font_description_set_style(d, st);
        else break;
        words.pop_back();
    }
    if (!words.empty()) {
        std::string fam;
        for (size_t k = 0; k < words.size(); ++k) fam += (k ? " " : "") + words[k];
        if (!fam.empty() && fam.back() == ',') fam.pop_back();
        pango_font_description_set_family(d, fam.c_str());
    }
    return d;
}

// =========================================================== attributes
namespace {

PangoAttribute* attr_int_copy(const PangoAttribute* a)
{
    auto* n = new PangoAttrInt(*reinterpret_cast<const PangoAttrInt*>(a));
    return &n->attr;
}
void attr_int_destroy(PangoAttribute* a) { delete reinterpret_cast<PangoAttrInt*>(a); }
gboolean attr_int_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    return reinterpret_cast<const PangoAttrInt*>(a)->value == reinterpret_cast<const PangoAttrInt*>(b)->value;
}

PangoAttribute* attr_float_copy(const PangoAttribute* a)
{
    auto* n = new PangoAttrFloat(*reinterpret_cast<const PangoAttrFloat*>(a));
    return &n->attr;
}
void attr_float_destroy(PangoAttribute* a) { delete reinterpret_cast<PangoAttrFloat*>(a); }
gboolean attr_float_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    return reinterpret_cast<const PangoAttrFloat*>(a)->value == reinterpret_cast<const PangoAttrFloat*>(b)->value;
}

PangoAttribute* attr_string_copy(const PangoAttribute* a)
{
    auto* o = reinterpret_cast<const PangoAttrString*>(a);
    auto* n = new PangoAttrString(*o);
    n->value = strdup(o->value ? o->value : "");
    return &n->attr;
}
void attr_string_destroy(PangoAttribute* a)
{
    auto* s = reinterpret_cast<PangoAttrString*>(a);
    std::free(s->value);
    delete s;
}
gboolean attr_string_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    return std::strcmp(reinterpret_cast<const PangoAttrString*>(a)->value,
                       reinterpret_cast<const PangoAttrString*>(b)->value) == 0;
}

PangoAttribute* attr_color_copy(const PangoAttribute* a)
{
    auto* n = new PangoAttrColor(*reinterpret_cast<const PangoAttrColor*>(a));
    return &n->attr;
}
void attr_color_destroy(PangoAttribute* a) { delete reinterpret_cast<PangoAttrColor*>(a); }
gboolean attr_color_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    auto* x = reinterpret_cast<const PangoAttrColor*>(a);
    auto* y = reinterpret_cast<const PangoAttrColor*>(b);
    return x->color.red == y->color.red && x->color.green == y->color.green && x->color.blue == y->color.blue;
}

PangoAttribute* attr_size_copy(const PangoAttribute* a)
{
    auto* n = new PangoAttrSize(*reinterpret_cast<const PangoAttrSize*>(a));
    return &n->attr;
}
void attr_size_destroy(PangoAttribute* a) { delete reinterpret_cast<PangoAttrSize*>(a); }
gboolean attr_size_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    auto* x = reinterpret_cast<const PangoAttrSize*>(a);
    auto* y = reinterpret_cast<const PangoAttrSize*>(b);
    return x->size == y->size && x->absolute == y->absolute;
}

PangoAttribute* attr_fontdesc_copy(const PangoAttribute* a)
{
    auto* o = reinterpret_cast<const PangoAttrFontDesc*>(a);
    auto* n = new PangoAttrFontDesc(*o);
    n->desc = pango_font_description_copy(o->desc);
    return &n->attr;
}
void attr_fontdesc_destroy(PangoAttribute* a)
{
    auto* f = reinterpret_cast<PangoAttrFontDesc*>(a);
    pango_font_description_free(f->desc);
    delete f;
}
gboolean attr_fontdesc_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    auto* x = reinterpret_cast<const PangoAttrFontDesc*>(a)->desc;
    auto* y = reinterpret_cast<const PangoAttrFontDesc*>(b)->desc;
    return x->mask == y->mask && x->family == y->family && x->size == y->size &&
           x->size_absolute == y->size_absolute && x->weight == y->weight && x->style == y->style;
}

PangoAttribute* attr_shape_copy(const PangoAttribute* a)
{
    auto* o = reinterpret_cast<const PangoAttrShape*>(a);
    auto* n = new PangoAttrShape(*o);
    n->data = o->copy_func ? o->copy_func(o->data) : o->data;
    return &n->attr;
}
void attr_shape_destroy(PangoAttribute* a)
{
    auto* s = reinterpret_cast<PangoAttrShape*>(a);
    if (s->destroy_func) s->destroy_func(s->data);
    delete s;
}
gboolean attr_shape_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    auto* x = reinterpret_cast<const PangoAttrShape*>(a);
    auto* y = reinterpret_cast<const PangoAttrShape*>(b);
    return x->data == y->data && std::memcmp(&x->logical_rect, &y->logical_rect, sizeof(PangoRectangle)) == 0;
}

#define INT_CLASS(t)    { t, attr_int_copy, attr_int_destroy, attr_int_equal }
const PangoAttrClass k_family     = { PANGO_ATTR_FAMILY, attr_string_copy, attr_string_destroy, attr_string_equal };
const PangoAttrClass k_style      = INT_CLASS(PANGO_ATTR_STYLE);
const PangoAttrClass k_weight     = INT_CLASS(PANGO_ATTR_WEIGHT);
const PangoAttrClass k_size       = { PANGO_ATTR_SIZE, attr_size_copy, attr_size_destroy, attr_size_equal };
const PangoAttrClass k_abs_size   = { PANGO_ATTR_ABSOLUTE_SIZE, attr_size_copy, attr_size_destroy, attr_size_equal };
const PangoAttrClass k_font_desc  = { PANGO_ATTR_FONT_DESC, attr_fontdesc_copy, attr_fontdesc_destroy, attr_fontdesc_equal };
const PangoAttrClass k_fg         = { PANGO_ATTR_FOREGROUND, attr_color_copy, attr_color_destroy, attr_color_equal };
const PangoAttrClass k_bg         = { PANGO_ATTR_BACKGROUND, attr_color_copy, attr_color_destroy, attr_color_equal };
const PangoAttrClass k_ul_color   = { PANGO_ATTR_UNDERLINE_COLOR, attr_color_copy, attr_color_destroy, attr_color_equal };
const PangoAttrClass k_fg_alpha   = INT_CLASS(PANGO_ATTR_FOREGROUND_ALPHA);
const PangoAttrClass k_bg_alpha   = INT_CLASS(PANGO_ATTR_BACKGROUND_ALPHA);
const PangoAttrClass k_underline  = INT_CLASS(PANGO_ATTR_UNDERLINE);
const PangoAttrClass k_strike     = INT_CLASS(PANGO_ATTR_STRIKETHROUGH);
const PangoAttrClass k_rise       = INT_CLASS(PANGO_ATTR_RISE);
const PangoAttrClass k_letter_sp  = INT_CLASS(PANGO_ATTR_LETTER_SPACING);
const PangoAttrClass k_abs_lh     = INT_CLASS(PANGO_ATTR_ABSOLUTE_LINE_HEIGHT);
const PangoAttrClass k_scale      = { PANGO_ATTR_SCALE, attr_float_copy, attr_float_destroy, attr_float_equal };
const PangoAttrClass k_lh         = { PANGO_ATTR_LINE_HEIGHT, attr_float_copy, attr_float_destroy, attr_float_equal };
const PangoAttrClass k_shape      = { PANGO_ATTR_SHAPE, attr_shape_copy, attr_shape_destroy, attr_shape_equal };

PangoAttribute* new_int(const PangoAttrClass* k, int v)
{
    auto* a = new PangoAttrInt();
    pango_attribute_init(&a->attr, k);
    a->value = v;
    return &a->attr;
}
PangoAttribute* new_float(const PangoAttrClass* k, double v)
{
    auto* a = new PangoAttrFloat();
    pango_attribute_init(&a->attr, k);
    a->value = v;
    return &a->attr;
}
PangoAttribute* new_color(const PangoAttrClass* k, guint16 r, guint16 g, guint16 b)
{
    auto* a = new PangoAttrColor();
    pango_attribute_init(&a->attr, k);
    a->color = { r, g, b };
    return &a->attr;
}
PangoAttribute* new_size(const PangoAttrClass* k, int size, bool absolute)
{
    auto* a = new PangoAttrSize();
    pango_attribute_init(&a->attr, k);
    a->size = size;
    a->absolute = absolute;
    return &a->attr;
}
} // namespace

void pango_attribute_init(PangoAttribute* attr, const PangoAttrClass* klass)
{
    attr->klass = klass;
    attr->start_index = PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
    attr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
}
PangoAttribute* pango_attribute_copy(const PangoAttribute* a)
{
    PangoAttribute* n = a->klass->copy(a);
    n->start_index = a->start_index;
    n->end_index = a->end_index;
    return n;
}
void pango_attribute_destroy(PangoAttribute* a) { if (a) a->klass->destroy(a); }
gboolean pango_attribute_equal(const PangoAttribute* a, const PangoAttribute* b)
{
    return a->klass->type == b->klass->type && a->klass->equal(a, b);
}

PangoAttribute* pango_attr_family_new(const char* family)
{
    auto* a = new PangoAttrString();
    pango_attribute_init(&a->attr, &k_family);
    a->value = strdup(family ? family : "");
    return &a->attr;
}
PangoAttribute* pango_attr_style_new(PangoStyle style) { return new_int(&k_style, style); }
PangoAttribute* pango_attr_weight_new(PangoWeight weight) { return new_int(&k_weight, weight); }
PangoAttribute* pango_attr_size_new(int size) { return new_size(&k_size, size, false); }
PangoAttribute* pango_attr_size_new_absolute(int size) { return new_size(&k_abs_size, size, true); }
PangoAttribute* pango_attr_font_desc_new(const PangoFontDescription* desc)
{
    auto* a = new PangoAttrFontDesc();
    pango_attribute_init(&a->attr, &k_font_desc);
    a->desc = pango_font_description_copy(desc);
    return &a->attr;
}
PangoAttribute* pango_attr_foreground_new(guint16 r, guint16 g, guint16 b) { return new_color(&k_fg, r, g, b); }
PangoAttribute* pango_attr_background_new(guint16 r, guint16 g, guint16 b) { return new_color(&k_bg, r, g, b); }
PangoAttribute* pango_attr_foreground_alpha_new(guint16 alpha) { return new_int(&k_fg_alpha, alpha); }
PangoAttribute* pango_attr_background_alpha_new(guint16 alpha) { return new_int(&k_bg_alpha, alpha); }
PangoAttribute* pango_attr_underline_new(PangoUnderline u) { return new_int(&k_underline, u); }
PangoAttribute* pango_attr_underline_color_new(guint16 r, guint16 g, guint16 b) { return new_color(&k_ul_color, r, g, b); }
PangoAttribute* pango_attr_strikethrough_new(gboolean s) { return new_int(&k_strike, s ? 1 : 0); }
PangoAttribute* pango_attr_rise_new(int rise) { return new_int(&k_rise, rise); }
PangoAttribute* pango_attr_scale_new(double f) { return new_float(&k_scale, f); }
PangoAttribute* pango_attr_letter_spacing_new(int s) { return new_int(&k_letter_sp, s); }
PangoAttribute* pango_attr_line_height_new(double f) { return new_float(&k_lh, f); }
PangoAttribute* pango_attr_line_height_new_absolute(int h) { return new_int(&k_abs_lh, h); }
PangoAttribute* pango_attr_shape_new_with_data(const PangoRectangle* ink, const PangoRectangle* logical,
                                               gpointer data, gpointer (*copy_func)(gconstpointer),
                                               GDestroyNotify destroy_func)
{
    auto* a = new PangoAttrShape();
    pango_attribute_init(&a->attr, &k_shape);
    a->ink_rect = *ink;
    a->logical_rect = *logical;
    a->data = data;
    a->copy_func = copy_func;
    a->destroy_func = destroy_func;
    return &a->attr;
}
PangoAttribute* pango_attr_shape_new(const PangoRectangle* ink, const PangoRectangle* logical)
{
    return pango_attr_shape_new_with_data(ink, logical, nullptr, nullptr, nullptr);
}

// ======================================================= attribute lists
PangoAttrList* pango_attr_list_new(void) { return new PangoAttrList(); }
PangoAttrList* pango_attr_list_ref(PangoAttrList* l) { if (l) ++l->refs; return l; }
void pango_attr_list_unref(PangoAttrList* l)
{
    if (l && --l->refs == 0) delete l;
}
_PangoAttrList::~_PangoAttrList()
{
    for (auto* a : attrs) pango_attribute_destroy(a);
}
PangoAttrList* pango_attr_list_copy(PangoAttrList* l)
{
    if (!l) return nullptr;
    auto* n = new PangoAttrList();
    for (auto* a : l->attrs) n->attrs.push_back(pango_attribute_copy(a));
    return n;
}
void pango_attr_list_insert(PangoAttrList* l, PangoAttribute* a)
{
    auto it = std::upper_bound(l->attrs.begin(), l->attrs.end(), a,
        [](const PangoAttribute* x, const PangoAttribute* y) { return x->start_index < y->start_index; });
    l->attrs.insert(it, a);
}
void pango_attr_list_insert_before(PangoAttrList* l, PangoAttribute* a)
{
    auto it = std::lower_bound(l->attrs.begin(), l->attrs.end(), a,
        [](const PangoAttribute* x, const PangoAttribute* y) { return x->start_index < y->start_index; });
    l->attrs.insert(it, a);
}

// Replace attributes of the same type on [start,end) and merge with equal
// adjoining/overlapping ones (pango_attr_list_change).
void pango_attr_list_change(PangoAttrList* l, PangoAttribute* a)
{
    const guint s = a->start_index, e = a->end_index;
    if (s == e) { pango_attribute_destroy(a); return; }
    std::vector<PangoAttribute*> keep, added;
    for (PangoAttribute* x : l->attrs) {
        if (x->klass->type != a->klass->type || x->end_index < s || x->start_index > e) {
            keep.push_back(x);
            continue;
        }
        if (pango_attribute_equal(x, a)) {
            // merge into the new attribute
            a->start_index = std::min(a->start_index, x->start_index);
            a->end_index = std::max(a->end_index, x->end_index);
            pango_attribute_destroy(x);
            continue;
        }
        if (x->end_index == s || x->start_index == e) { keep.push_back(x); continue; }   // just touching
        // different value overlapping: keep the parts outside [s,e)
        if (x->start_index < s && x->end_index > e) {
            PangoAttribute* tail = pango_attribute_copy(x);
            tail->start_index = e;
            x->end_index = s;
            keep.push_back(x);
            added.push_back(tail);
        } else if (x->start_index < s) {
            x->end_index = s;
            keep.push_back(x);
        } else if (x->end_index > e) {
            x->start_index = e;
            added.push_back(x);
        } else {
            pango_attribute_destroy(x);
        }
    }
    l->attrs.clear();
    for (auto* x : keep) pango_attr_list_insert(l, x);
    for (auto* x : added) pango_attr_list_insert(l, x);
    pango_attr_list_insert(l, a);
}

void pango_attr_list_splice(PangoAttrList* l, PangoAttrList* other, gint pos, gint len)
{
    const guint upos = (guint)pos, ulen = (guint)len;
    auto clamp_add = [](guint v, guint d) -> guint {
        return (v == PANGO_ATTR_INDEX_TO_TEXT_END || v > PANGO_ATTR_INDEX_TO_TEXT_END - d) ? PANGO_ATTR_INDEX_TO_TEXT_END : v + d;
    };
    for (auto* a : l->attrs) {
        if (a->start_index >= upos && ulen) a->start_index = clamp_add(a->start_index, ulen);
        if (a->end_index >= upos && ulen) a->end_index = clamp_add(a->end_index, ulen);
    }
    if (!other) return;
    for (auto* o : other->attrs) {
        PangoAttribute* c = pango_attribute_copy(o);
        c->start_index = clamp_add(c->start_index, upos);
        c->end_index = clamp_add(c->end_index, upos);
        if (c->start_index >= c->end_index) { pango_attribute_destroy(c); continue; }
        pango_attr_list_change(l, c);
    }
}

// ================================================================ markup
namespace {

struct markup_parser {
    const char* p;
    const char* end;
    std::string out;
    std::vector<PangoAttribute*> attrs;   // in creation order
    struct open_tag { std::string name; size_t start; std::vector<PangoAttribute*> pending; };
    std::vector<open_tag> stack;

    ~markup_parser() { for (auto* a : attrs) pango_attribute_destroy(a); }

    static bool is_name_char(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == ':'; }
    void skip_ws() { while (p < end && std::isspace((unsigned char)*p)) ++p; }

    static void put_utf8(std::string& s, unsigned cp)
    {
        if (cp < 0x80) s += (char)cp;
        else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
        else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    }

    // &...; at p; appends the character to s
    bool entity(std::string& s)
    {
        const char* semi = static_cast<const char*>(std::memchr(p, ';', end - p));
        if (!semi || semi - p > 12) return false;
        std::string e(p + 1, semi);
        if (e == "amp") s += '&';
        else if (e == "lt") s += '<';
        else if (e == "gt") s += '>';
        else if (e == "quot") s += '"';
        else if (e == "apos") s += '\'';
        else if (e.size() > 1 && e[0] == '#') {
            char* ep = nullptr;
            unsigned long cp = (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, &ep, 16)
                                                             : std::strtoul(e.c_str() + 1, &ep, 10);
            if (!ep || *ep || cp == 0 || cp > 0x10FFFF) return false;
            put_utf8(s, (unsigned)cp);
        } else {
            return false;
        }
        p = semi + 1;
        return true;
    }

    bool attr_value(std::string& v)
    {
        if (p >= end || (*p != '"' && *p != '\'')) return false;
        char q = *p++;
        while (p < end && *p != q) {
            if (*p == '&') { if (!entity(v)) return false; continue; }
            if (*p == '<') return false;
            v += *p++;
        }
        if (p >= end) return false;
        ++p;
        return true;
    }

    static double size_scale(const std::string& v)
    {
        if (v == "xx-small") return 1.0 / (1.2 * 1.2 * 1.2);
        if (v == "x-small") return 1.0 / (1.2 * 1.2);
        if (v == "small") return 1.0 / 1.2;
        if (v == "medium") return 1.0;
        if (v == "large") return 1.2;
        if (v == "x-large") return 1.2 * 1.2;
        if (v == "xx-large") return 1.2 * 1.2 * 1.2;
        return 0.0;
    }

    bool span_attr(const std::string& name, const std::string& v, std::vector<PangoAttribute*>& out)
    {
        PangoColor c;
        if (name == "font" || name == "font_desc") {
            PangoFontDescription* d = pango_font_description_from_string(v.c_str());
            out.push_back(pango_attr_font_desc_new(d));
            pango_font_description_free(d);
        } else if (name == "font_family" || name == "face") {
            out.push_back(pango_attr_family_new(v.c_str()));
        } else if (name == "size" || name == "font_size") {
            double sc = size_scale(v);
            if (sc > 0) out.push_back(pango_attr_scale_new(sc));
            else if (v == "smaller") out.push_back(pango_attr_scale_new(1.0 / 1.2));
            else if (v == "larger") out.push_back(pango_attr_scale_new(1.2));
            else {
                char* ep = nullptr;
                double n = std::strtod(v.c_str(), &ep);
                if (!ep || ep == v.c_str()) return false;
                std::string unit(ep);
                if (unit.empty()) out.push_back(pango_attr_size_new((int)n));
                else if (unit == "pt") out.push_back(pango_attr_size_new((int)(n * PANGO_SCALE)));
                else if (unit == "px") out.push_back(pango_attr_size_new_absolute((int)(n * PANGO_SCALE)));
                else if (unit == "%") out.push_back(pango_attr_scale_new(n / 100.0));
                else return false;
            }
        } else if (name == "style" || name == "font_style") {
            PangoStyle s;
            if (!n64pango::parse_style(v, &s)) return false;
            out.push_back(pango_attr_style_new(s));
        } else if (name == "weight" || name == "font_weight") {
            PangoWeight w;
            if (!n64pango::parse_weight(v, &w)) return false;
            out.push_back(pango_attr_weight_new(w));
        } else if (name == "variant" || name == "font_variant" || name == "stretch" || name == "font_stretch" ||
                   name == "lang" || name == "fallback" || name == "gravity" || name == "gravity_hint" ||
                   name == "font_features" || name == "allow_breaks" || name == "insert_hyphens" ||
                   name == "show" || name == "text_transform" || name == "segment" ||
                   name == "overline" || name == "overline_color" || name == "strikethrough_color") {
            // accepted; no effect on the console's rendering
        } else if (name == "foreground" || name == "fgcolor" || name == "color") {
            if (!pango_color_parse(&c, v.c_str())) return false;
            out.push_back(pango_attr_foreground_new(c.red, c.green, c.blue));
        } else if (name == "background" || name == "bgcolor") {
            if (!pango_color_parse(&c, v.c_str())) return false;
            out.push_back(pango_attr_background_new(c.red, c.green, c.blue));
        } else if (name == "alpha" || name == "fgalpha" || name == "bgalpha") {
            int a;
            if (!v.empty() && v.back() == '%') a = (int)(std::strtod(v.c_str(), nullptr) * 65535 / 100);
            else a = (int)std::strtol(v.c_str(), nullptr, 10);
            a = std::max(0, std::min(65535, a));
            out.push_back(name == "bgalpha" ? pango_attr_background_alpha_new((guint16)a)
                                            : pango_attr_foreground_alpha_new((guint16)a));
        } else if (name == "underline") {
            PangoUnderline u;
            if (v == "none" || v == "false") u = PANGO_UNDERLINE_NONE;
            else if (v == "single" || v == "true") u = PANGO_UNDERLINE_SINGLE;
            else if (v == "double") u = PANGO_UNDERLINE_DOUBLE;
            else if (v == "low") u = PANGO_UNDERLINE_LOW;
            else if (v == "error") u = PANGO_UNDERLINE_ERROR;
            else return false;
            out.push_back(pango_attr_underline_new(u));
        } else if (name == "underline_color") {
            if (!pango_color_parse(&c, v.c_str())) return false;
            out.push_back(pango_attr_underline_color_new(c.red, c.green, c.blue));
        } else if (name == "strikethrough") {
            if (v != "true" && v != "false") return false;
            out.push_back(pango_attr_strikethrough_new(v == "true"));
        } else if (name == "rise" || name == "baseline_shift") {
            out.push_back(pango_attr_rise_new((int)std::strtol(v.c_str(), nullptr, 10)));
        } else if (name == "letter_spacing") {
            out.push_back(pango_attr_letter_spacing_new((int)std::strtol(v.c_str(), nullptr, 10)));
        } else if (name == "line_height") {
            char* ep = nullptr;
            double f = std::strtod(v.c_str(), &ep);
            if (ep == v.c_str()) return false;
            if (f < 1024) out.push_back(pango_attr_line_height_new(f));
            else out.push_back(pango_attr_line_height_new_absolute((int)f));
        } else if (name == "font_scale") {
            if (v == "superscript" || v == "subscript" || v == "small-caps")
                out.push_back(pango_attr_scale_new(1.0 / 1.2));
        } else {
            return false;           // unknown attribute: invalid markup (as in Pango)
        }
        return true;
    }

    bool open(const std::string& name, const std::vector<std::pair<std::string, std::string>>& a)
    {
        open_tag t{ name, out.size(), {} };
        auto& v = t.pending;
        if (name == "b") v.push_back(pango_attr_weight_new(PANGO_WEIGHT_BOLD));
        else if (name == "i") v.push_back(pango_attr_style_new(PANGO_STYLE_ITALIC));
        else if (name == "u") v.push_back(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        else if (name == "s") v.push_back(pango_attr_strikethrough_new(TRUE));
        else if (name == "tt") v.push_back(pango_attr_family_new("Monospace"));
        else if (name == "big") v.push_back(pango_attr_scale_new(1.2));
        else if (name == "small") v.push_back(pango_attr_scale_new(1.0 / 1.2));
        else if (name == "sub") { v.push_back(pango_attr_rise_new(-5000)); v.push_back(pango_attr_scale_new(1.0 / 1.2)); }
        else if (name == "sup") { v.push_back(pango_attr_rise_new(5000)); v.push_back(pango_attr_scale_new(1.0 / 1.2)); }
        else if (name == "span" || name == "markup") {
            for (const auto& kv : a) {
                if (!span_attr(kv.first, kv.second, v)) { for (auto* x : v) pango_attribute_destroy(x); return false; }
            }
        } else {
            return false;
        }
        if (name != "span" && name != "markup" && !a.empty()) { for (auto* x : v) pango_attribute_destroy(x); return false; }
        stack.push_back(std::move(t));
        return true;
    }

    void close_top()
    {
        open_tag& t = stack.back();
        for (auto* a : t.pending) {
            a->start_index = (guint)t.start;
            a->end_index = (guint)out.size();
            attrs.push_back(a);
        }
        stack.pop_back();
    }

    bool parse()
    {
        while (p < end) {
            char ch = *p;
            if (ch == '&') {
                if (!entity(out)) return false;
            } else if (ch == '<') {
                ++p;
                if (p < end && *p == '!') {                 // comment
                    const char* c = std::strstr(p, "-->");
                    if (!c || c >= end) return false;
                    p = c + 3;
                    continue;
                }
                bool closing = p < end && *p == '/';
                if (closing) ++p;
                const char* n0 = p;
                while (p < end && is_name_char(*p)) ++p;
                std::string name(n0, p);
                if (name.empty()) return false;
                if (closing) {
                    skip_ws();
                    if (p >= end || *p != '>') return false;
                    ++p;
                    if (stack.empty() || stack.back().name != name) return false;
                    close_top();
                    continue;
                }
                std::vector<std::pair<std::string, std::string>> a;
                for (;;) {
                    skip_ws();
                    if (p >= end) return false;
                    if (*p == '>' || *p == '/') break;
                    const char* k0 = p;
                    while (p < end && is_name_char(*p)) ++p;
                    std::string key(k0, p);
                    if (key.empty()) return false;
                    skip_ws();
                    if (p >= end || *p != '=') return false;
                    ++p;
                    skip_ws();
                    std::string val;
                    if (!attr_value(val)) return false;
                    a.emplace_back(key, val);
                }
                bool empty_tag = *p == '/';
                if (empty_tag) { ++p; if (p >= end || *p != '>') return false; }
                ++p;
                if (!open(name, a)) return false;
                if (empty_tag) close_top();
            } else {
                out += ch;
                ++p;
            }
        }
        return stack.empty();
    }
};

} // namespace

gboolean pango_parse_markup(const char* markup_text, int length, gunichar /*accel_marker*/,
                            PangoAttrList** attr_list, char** text, gunichar* accel_char, GError** /*error*/)
{
    if (accel_char) *accel_char = 0;
    if (!markup_text) return FALSE;
    if (length < 0) length = (int)std::strlen(markup_text);
    markup_parser mp;
    mp.p = markup_text;
    mp.end = markup_text + length;
    if (!mp.parse()) return FALSE;
    if (attr_list) {
        PangoAttrList* l = pango_attr_list_new();
        // Inner elements close first; insert outer ones first so that, among
        // attributes starting at the same index, inner ones come later in the
        // list and take precedence (the layout applies attributes in order).
        for (auto it = mp.attrs.rbegin(); it != mp.attrs.rend(); ++it) pango_attr_list_insert(l, *it);
        mp.attrs.clear();
        *attr_list = l;
    }
    if (text) {
        *text = static_cast<char*>(std::malloc(mp.out.size() + 1));
        std::memcpy(*text, mp.out.c_str(), mp.out.size() + 1);
    }
    return TRUE;
}
