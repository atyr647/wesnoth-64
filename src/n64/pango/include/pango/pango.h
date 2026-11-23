/*
 * pango.h -- the part of the Pango API used by Wesnoth, for the Nintendo 64.
 *
 * Implemented in src/n64/pango/n64_pango.cpp on top of HarfBuzz (shaping),
 * FreeType (fonts) and cairo (rendering), the same libraries Pango itself
 * uses. Types, constants and semantics follow Pango 1.5x so that the engine's
 * own text code (src/font/text.cpp, attributes.cpp, gui2) compiles unchanged.
 * Left to right text only; no bidi reordering.
 */
#ifndef N64_PANGO_H
#define N64_PANGO_H

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- GLib */
typedef int gboolean;
typedef int gint;
typedef unsigned int guint;
typedef unsigned short guint16;
typedef unsigned int guint32;
typedef unsigned char guint8;
typedef char gchar;
typedef double gdouble;
typedef void* gpointer;
typedef const void* gconstpointer;
typedef unsigned int gunichar;
typedef struct _GError GError;
typedef void (*GDestroyNotify)(gpointer data);
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define G_MAXUINT 0xFFFFFFFFu

gpointer g_object_ref(gpointer object);
void g_object_unref(gpointer object);
void g_free(gpointer mem);

/* --------------------------------------------------------------- units */
#define PANGO_SCALE 1024
#define PANGO_PIXELS(d) (((int)(d) + 512) >> 10)
#define PANGO_PIXELS_FLOOR(d) (((int)(d)) >> 10)
#define PANGO_PIXELS_CEIL(d) (((int)(d) + 1023) >> 10)
#define PANGO_VERSION_STRING "1.56 (Nintendo 64 subset)"

const char* pango_version_string(void);
double pango_units_to_double(int i);
int pango_units_from_double(double d);

typedef struct _PangoRectangle { int x, y, width, height; } PangoRectangle;

/* ---------------------------------------------------------------- color */
typedef struct _PangoColor { guint16 red, green, blue; } PangoColor;
gboolean pango_color_parse(PangoColor* color, const char* spec);

/* ----------------------------------------------------------------- enums */
typedef enum { PANGO_STYLE_NORMAL, PANGO_STYLE_OBLIQUE, PANGO_STYLE_ITALIC } PangoStyle;
typedef enum { PANGO_VARIANT_NORMAL, PANGO_VARIANT_SMALL_CAPS } PangoVariant;
typedef enum {
    PANGO_WEIGHT_THIN = 100, PANGO_WEIGHT_ULTRALIGHT = 200, PANGO_WEIGHT_LIGHT = 300,
    PANGO_WEIGHT_SEMILIGHT = 350, PANGO_WEIGHT_BOOK = 380, PANGO_WEIGHT_NORMAL = 400,
    PANGO_WEIGHT_MEDIUM = 500, PANGO_WEIGHT_SEMIBOLD = 600, PANGO_WEIGHT_BOLD = 700,
    PANGO_WEIGHT_ULTRABOLD = 800, PANGO_WEIGHT_HEAVY = 900, PANGO_WEIGHT_ULTRAHEAVY = 1000
} PangoWeight;
typedef enum {
    PANGO_STRETCH_ULTRA_CONDENSED, PANGO_STRETCH_EXTRA_CONDENSED, PANGO_STRETCH_CONDENSED,
    PANGO_STRETCH_SEMI_CONDENSED, PANGO_STRETCH_NORMAL, PANGO_STRETCH_SEMI_EXPANDED,
    PANGO_STRETCH_EXPANDED, PANGO_STRETCH_EXTRA_EXPANDED, PANGO_STRETCH_ULTRA_EXPANDED
} PangoStretch;
typedef enum {
    PANGO_UNDERLINE_NONE, PANGO_UNDERLINE_SINGLE, PANGO_UNDERLINE_DOUBLE, PANGO_UNDERLINE_LOW,
    PANGO_UNDERLINE_ERROR, PANGO_UNDERLINE_SINGLE_LINE, PANGO_UNDERLINE_DOUBLE_LINE,
    PANGO_UNDERLINE_ERROR_LINE
} PangoUnderline;
typedef enum { PANGO_ALIGN_LEFT, PANGO_ALIGN_CENTER, PANGO_ALIGN_RIGHT } PangoAlignment;
typedef enum { PANGO_WRAP_WORD, PANGO_WRAP_CHAR, PANGO_WRAP_WORD_CHAR } PangoWrapMode;
typedef enum {
    PANGO_ELLIPSIZE_NONE, PANGO_ELLIPSIZE_START, PANGO_ELLIPSIZE_MIDDLE, PANGO_ELLIPSIZE_END
} PangoEllipsizeMode;
typedef enum { PANGO_DIRECTION_LTR = 0, PANGO_DIRECTION_RTL = 1, PANGO_DIRECTION_NEUTRAL = 6 } PangoDirection;

/* ------------------------------------------------------ font descriptions */
typedef struct _PangoFontDescription PangoFontDescription;
typedef enum {
    PANGO_FONT_MASK_FAMILY = 1 << 0, PANGO_FONT_MASK_STYLE = 1 << 1, PANGO_FONT_MASK_VARIANT = 1 << 2,
    PANGO_FONT_MASK_WEIGHT = 1 << 3, PANGO_FONT_MASK_STRETCH = 1 << 4, PANGO_FONT_MASK_SIZE = 1 << 5
} PangoFontMask;

PangoFontDescription* pango_font_description_new(void);
PangoFontDescription* pango_font_description_copy(const PangoFontDescription* desc);
PangoFontDescription* pango_font_description_from_string(const char* str);
void pango_font_description_free(PangoFontDescription* desc);
void pango_font_description_set_family(PangoFontDescription* desc, const char* family);
const char* pango_font_description_get_family(const PangoFontDescription* desc);
void pango_font_description_set_size(PangoFontDescription* desc, gint size);
void pango_font_description_set_absolute_size(PangoFontDescription* desc, double size);
gint pango_font_description_get_size(const PangoFontDescription* desc);
gboolean pango_font_description_get_size_is_absolute(const PangoFontDescription* desc);
void pango_font_description_set_weight(PangoFontDescription* desc, PangoWeight weight);
PangoWeight pango_font_description_get_weight(const PangoFontDescription* desc);
void pango_font_description_set_style(PangoFontDescription* desc, PangoStyle style);
PangoStyle pango_font_description_get_style(const PangoFontDescription* desc);
PangoFontMask pango_font_description_get_set_fields(const PangoFontDescription* desc);

/* ------------------------------------------------------------ attributes */
typedef enum {
    PANGO_ATTR_INVALID, PANGO_ATTR_LANGUAGE, PANGO_ATTR_FAMILY, PANGO_ATTR_STYLE, PANGO_ATTR_WEIGHT,
    PANGO_ATTR_VARIANT, PANGO_ATTR_STRETCH, PANGO_ATTR_SIZE, PANGO_ATTR_FONT_DESC,
    PANGO_ATTR_FOREGROUND, PANGO_ATTR_BACKGROUND, PANGO_ATTR_UNDERLINE, PANGO_ATTR_STRIKETHROUGH,
    PANGO_ATTR_RISE, PANGO_ATTR_SHAPE, PANGO_ATTR_SCALE, PANGO_ATTR_FALLBACK,
    PANGO_ATTR_LETTER_SPACING, PANGO_ATTR_UNDERLINE_COLOR, PANGO_ATTR_STRIKETHROUGH_COLOR,
    PANGO_ATTR_ABSOLUTE_SIZE, PANGO_ATTR_GRAVITY, PANGO_ATTR_GRAVITY_HINT, PANGO_ATTR_FONT_FEATURES,
    PANGO_ATTR_FOREGROUND_ALPHA, PANGO_ATTR_BACKGROUND_ALPHA, PANGO_ATTR_ALLOW_BREAKS,
    PANGO_ATTR_SHOW, PANGO_ATTR_INSERT_HYPHENS, PANGO_ATTR_OVERLINE, PANGO_ATTR_OVERLINE_COLOR,
    PANGO_ATTR_LINE_HEIGHT, PANGO_ATTR_ABSOLUTE_LINE_HEIGHT, PANGO_ATTR_TEXT_TRANSFORM,
    PANGO_ATTR_WORD, PANGO_ATTR_SENTENCE, PANGO_ATTR_BASELINE_SHIFT, PANGO_ATTR_FONT_SCALE
} PangoAttrType;

#define PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING ((guint)0)
#define PANGO_ATTR_INDEX_TO_TEXT_END ((guint)G_MAXUINT)

typedef struct _PangoAttribute PangoAttribute;
typedef struct _PangoAttrClass {
    PangoAttrType type;
    PangoAttribute* (*copy)(const PangoAttribute* attr);
    void (*destroy)(PangoAttribute* attr);
    gboolean (*equal)(const PangoAttribute* attr1, const PangoAttribute* attr2);
} PangoAttrClass;
struct _PangoAttribute {
    const PangoAttrClass* klass;
    guint start_index;
    guint end_index;
};
typedef struct _PangoAttrInt { PangoAttribute attr; int value; } PangoAttrInt;
typedef struct _PangoAttrFloat { PangoAttribute attr; double value; } PangoAttrFloat;
typedef struct _PangoAttrString { PangoAttribute attr; char* value; } PangoAttrString;
typedef struct _PangoAttrColor { PangoAttribute attr; PangoColor color; } PangoAttrColor;
typedef struct _PangoAttrSize { PangoAttribute attr; int size; guint absolute : 1; } PangoAttrSize;
typedef struct _PangoAttrFontDesc { PangoAttribute attr; PangoFontDescription* desc; } PangoAttrFontDesc;
typedef PangoAttribute* (*PangoAttrDataCopyFunc)(gconstpointer user_data);
typedef struct _PangoAttrShape {
    PangoAttribute attr;
    PangoRectangle ink_rect;
    PangoRectangle logical_rect;
    gpointer data;
    gpointer (*copy_func)(gconstpointer data);
    GDestroyNotify destroy_func;
} PangoAttrShape;

void pango_attribute_init(PangoAttribute* attr, const PangoAttrClass* klass);
PangoAttribute* pango_attribute_copy(const PangoAttribute* attr);
void pango_attribute_destroy(PangoAttribute* attr);
gboolean pango_attribute_equal(const PangoAttribute* attr1, const PangoAttribute* attr2);

PangoAttribute* pango_attr_family_new(const char* family);
PangoAttribute* pango_attr_style_new(PangoStyle style);
PangoAttribute* pango_attr_weight_new(PangoWeight weight);
PangoAttribute* pango_attr_size_new(int size);
PangoAttribute* pango_attr_size_new_absolute(int size);
PangoAttribute* pango_attr_font_desc_new(const PangoFontDescription* desc);
PangoAttribute* pango_attr_foreground_new(guint16 red, guint16 green, guint16 blue);
PangoAttribute* pango_attr_background_new(guint16 red, guint16 green, guint16 blue);
PangoAttribute* pango_attr_foreground_alpha_new(guint16 alpha);
PangoAttribute* pango_attr_background_alpha_new(guint16 alpha);
PangoAttribute* pango_attr_underline_new(PangoUnderline underline);
PangoAttribute* pango_attr_underline_color_new(guint16 red, guint16 green, guint16 blue);
PangoAttribute* pango_attr_strikethrough_new(gboolean strikethrough);
PangoAttribute* pango_attr_rise_new(int rise);
PangoAttribute* pango_attr_scale_new(double scale_factor);
PangoAttribute* pango_attr_letter_spacing_new(int letter_spacing);
PangoAttribute* pango_attr_line_height_new(double factor);
PangoAttribute* pango_attr_line_height_new_absolute(int height);
PangoAttribute* pango_attr_shape_new(const PangoRectangle* ink_rect, const PangoRectangle* logical_rect);
PangoAttribute* pango_attr_shape_new_with_data(const PangoRectangle* ink_rect, const PangoRectangle* logical_rect,
                                               gpointer data, gpointer (*copy_func)(gconstpointer),
                                               GDestroyNotify destroy_func);

typedef struct _PangoAttrList PangoAttrList;
PangoAttrList* pango_attr_list_new(void);
PangoAttrList* pango_attr_list_ref(PangoAttrList* list);
void pango_attr_list_unref(PangoAttrList* list);
PangoAttrList* pango_attr_list_copy(PangoAttrList* list);
void pango_attr_list_insert(PangoAttrList* list, PangoAttribute* attr);
void pango_attr_list_insert_before(PangoAttrList* list, PangoAttribute* attr);
void pango_attr_list_change(PangoAttrList* list, PangoAttribute* attr);
void pango_attr_list_splice(PangoAttrList* list, PangoAttrList* other, gint pos, gint len);

gboolean pango_parse_markup(const char* markup_text, int length, gunichar accel_marker,
                            PangoAttrList** attr_list, char** text, gunichar* accel_char, GError** error);

/* ------------------------------------------------- fonts, maps, contexts */
typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoContext PangoContext;
typedef struct _PangoFont PangoFont;
typedef struct _PangoLanguage PangoLanguage;
typedef struct _PangoFontMetrics PangoFontMetrics;

PangoContext* pango_font_map_create_context(PangoFontMap* fontmap);
PangoFontMap* pango_context_get_font_map(PangoContext* context);
PangoFont* pango_font_map_load_font(PangoFontMap* fontmap, PangoContext* context, const PangoFontDescription* desc);
PangoFontMetrics* pango_font_get_metrics(PangoFont* font, PangoLanguage* language);
PangoFontMetrics* pango_font_metrics_ref(PangoFontMetrics* metrics);
void pango_font_metrics_unref(PangoFontMetrics* metrics);
int pango_font_metrics_get_ascent(PangoFontMetrics* metrics);
int pango_font_metrics_get_descent(PangoFontMetrics* metrics);
int pango_font_metrics_get_height(PangoFontMetrics* metrics);
int pango_font_metrics_get_approximate_char_width(PangoFontMetrics* metrics);
int pango_font_metrics_get_approximate_digit_width(PangoFontMetrics* metrics);
int pango_font_metrics_get_underline_position(PangoFontMetrics* metrics);
int pango_font_metrics_get_underline_thickness(PangoFontMetrics* metrics);

#include <pango/pango-layout.h>

#endif /* N64_PANGO_H */
