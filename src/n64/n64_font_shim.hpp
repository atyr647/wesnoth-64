/**
 * n64_font_shim.hpp
 *
 * Replaces Pango and Cairo headers for N64 builds.
 *
 * Wesnoth uses Pango (text layout) and Cairo (2D vector graphics) for its
 * font rendering pipeline. On N64, all text rendering goes through
 * n64_video.hpp's bitmap font system (8x8 CP437 glyphs).
 *
 * This header is force-included via -include and provides stub types/functions
 * that satisfy compilation of font/text.hpp, font/text.cpp, font/cairo.hpp,
 * and the draw:: cairo_circle/cairo_disc functions.
 */

#ifndef N64_FONT_SHIM_HPP
#define N64_FONT_SHIM_HPP

#ifdef N64_BUILD

// -----------------------------------------------------------------------
// Prevent real Pango/Cairo headers from being included
// -----------------------------------------------------------------------
#define __PANGO_H__
#define __PANGOCAIRO_H__
#define __PANGO_LAYOUT_H__
#define __PANGO_FONT_H__
#define __PANGO_CONTEXT_H__
#define __PANGO_TYPES_H__
#define __PANGO_ATTRIBUTES_H__
#define _CAIRO_H
#define _PANGO_H_
#define _PANGOCAIRO_H_

// Also block pango sub-headers
#define __PANGO_FONTMAP_H__
#define __PANGO_FONTSET_H__
#define __PANGO_ENGINE_H__
#define __PANGO_RENDERER_H__
#define __PANGO_GLYPH_H__
#define __PANGO_GLYPH_ITEM_H__

// Block include paths
#define PANGO_PANGO_H_INSIDE
#define PANGO_PANGOCAIRO_H_INSIDE

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <tuple>

// -----------------------------------------------------------------------
// Pango type stubs
// -----------------------------------------------------------------------
typedef int gboolean;
typedef unsigned int guint;
typedef void* gpointer;

inline void g_object_unref(void*) {}

// Pango scale factor (same as real Pango)
#define PANGO_SCALE 1024

// PangoRectangle
struct PangoRectangle {
    int x, y, width, height;
};

// Pango enums
enum PangoEllipsizeMode {
    PANGO_ELLIPSIZE_NONE = 0,
    PANGO_ELLIPSIZE_START,
    PANGO_ELLIPSIZE_MIDDLE,
    PANGO_ELLIPSIZE_END
};

enum PangoAlignment {
    PANGO_ALIGN_LEFT = 0,
    PANGO_ALIGN_CENTER,
    PANGO_ALIGN_RIGHT
};

enum PangoWrapMode {
    PANGO_WRAP_WORD = 0,
    PANGO_WRAP_CHAR,
    PANGO_WRAP_WORD_CHAR
};

enum PangoWeight {
    PANGO_WEIGHT_THIN = 100,
    PANGO_WEIGHT_ULTRALIGHT = 200,
    PANGO_WEIGHT_LIGHT = 300,
    PANGO_WEIGHT_NORMAL = 400,
    PANGO_WEIGHT_MEDIUM = 500,
    PANGO_WEIGHT_SEMIBOLD = 600,
    PANGO_WEIGHT_BOLD = 700,
    PANGO_WEIGHT_ULTRABOLD = 800,
    PANGO_WEIGHT_HEAVY = 900
};

enum PangoStyle {
    PANGO_STYLE_NORMAL = 0,
    PANGO_STYLE_OBLIQUE,
    PANGO_STYLE_ITALIC
};

enum PangoUnderline {
    PANGO_UNDERLINE_NONE = 0,
    PANGO_UNDERLINE_SINGLE,
    PANGO_UNDERLINE_DOUBLE,
    PANGO_UNDERLINE_LOW
};

// Forward-declare opaque Pango types
struct PangoContext {};
struct PangoLayout {};
struct PangoLayoutLine {};
struct PangoLayoutIter {};
struct PangoFontMap {};
struct PangoFontDescription {};
struct PangoAttrList {};
struct PangoAttribute {};
struct PangoAttrShape {
    PangoRectangle ink_rect;
    PangoRectangle logical_rect;
    void* data;
};

// -----------------------------------------------------------------------
// Pango function stubs
// -----------------------------------------------------------------------
inline PangoContext* pango_font_map_create_context(PangoFontMap*) {
    static PangoContext ctx;
    return &ctx;
}

inline PangoFontMap* pango_cairo_font_map_get_default() {
    static PangoFontMap fm;
    return &fm;
}

inline PangoLayout* pango_layout_new(PangoContext*) {
    static PangoLayout layout;
    return &layout;
}

inline void pango_layout_set_ellipsize(PangoLayout*, PangoEllipsizeMode) {}
inline void pango_layout_set_alignment(PangoLayout*, PangoAlignment) {}
inline void pango_layout_set_wrap(PangoLayout*, PangoWrapMode) {}
inline void pango_layout_set_line_spacing(PangoLayout*, float) {}
inline void pango_layout_set_width(PangoLayout*, int) {}
inline void pango_layout_set_height(PangoLayout*, int) {}
inline void pango_layout_set_text(PangoLayout*, const char*, int) {}
inline void pango_layout_set_markup(PangoLayout*, const char*, int) {}
inline void pango_layout_set_font_description(PangoLayout*, const PangoFontDescription*) {}
inline void pango_layout_set_attributes(PangoLayout*, PangoAttrList*) {}

inline void pango_layout_get_pixel_extents(PangoLayout*, PangoRectangle*, PangoRectangle* logical) {
    if (logical) { logical->x = 0; logical->y = 0; logical->width = 0; logical->height = 0; }
}

inline void pango_layout_get_extents(PangoLayout*, PangoRectangle*, PangoRectangle* logical) {
    if (logical) { logical->x = 0; logical->y = 0; logical->width = 0; logical->height = 0; }
}

inline void pango_layout_get_pixel_size(PangoLayout*, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
}

inline int pango_layout_get_line_count(PangoLayout*) { return 1; }
inline PangoLayoutLine* pango_layout_get_line_readonly(PangoLayout*, int) { return nullptr; }
inline PangoLayoutLine* pango_layout_get_line(PangoLayout*, int) { return nullptr; }

inline gboolean pango_layout_xy_to_index(PangoLayout*, int, int, int* index, int* trailing) {
    if (index) *index = 0;
    if (trailing) *trailing = 0;
    return 0;
}

inline void pango_layout_index_to_pos(PangoLayout*, int, PangoRectangle* pos) {
    if (pos) { pos->x = 0; pos->y = 0; pos->width = 0; pos->height = 0; }
}

inline void pango_layout_index_to_line_x(PangoLayout*, int, gboolean, int* line, int* x_pos) {
    if (line) *line = 0;
    if (x_pos) *x_pos = 0;
}

inline const char* pango_layout_get_text(PangoLayout*) { return ""; }

inline PangoLayoutIter* pango_layout_get_iter(PangoLayout*) { return nullptr; }
inline void pango_layout_iter_free(PangoLayoutIter*) {}
inline gboolean pango_layout_iter_next_line(PangoLayoutIter*) { return 0; }
inline void pango_layout_iter_get_line_extents(PangoLayoutIter*, PangoRectangle*, PangoRectangle*) {}
inline int pango_layout_iter_get_baseline(PangoLayoutIter*) { return 0; }

// Font description
inline PangoFontDescription* pango_font_description_new() { return nullptr; }
inline void pango_font_description_free(PangoFontDescription*) {}
inline void pango_font_description_set_family(PangoFontDescription*, const char*) {}
inline void pango_font_description_set_size(PangoFontDescription*, int) {}
inline void pango_font_description_set_weight(PangoFontDescription*, PangoWeight) {}
inline void pango_font_description_set_style(PangoFontDescription*, PangoStyle) {}
inline int pango_font_description_get_size(const PangoFontDescription*) { return 12 * PANGO_SCALE; }

// Attribute list
inline PangoAttrList* pango_attr_list_new() { return nullptr; }
inline void pango_attr_list_unref(PangoAttrList*) {}
inline void pango_attr_list_insert(PangoAttrList*, PangoAttribute*) {}

inline PangoAttribute* pango_attr_foreground_new(guint, guint, guint) { return nullptr; }
inline PangoAttribute* pango_attr_underline_new(PangoUnderline) { return nullptr; }
inline PangoAttribute* pango_attr_weight_new(PangoWeight) { return nullptr; }
inline PangoAttribute* pango_attr_style_new(PangoStyle) { return nullptr; }
inline PangoAttribute* pango_attr_size_new(int) { return nullptr; }

// Cairo-Pango bridge
inline void pango_cairo_context_set_resolution(PangoContext*, double) {}
inline void pango_cairo_context_set_font_options(PangoContext*, void*) {}
inline void pango_cairo_context_set_shape_renderer(PangoContext*, void*, void*, void*) {}
inline void pango_cairo_show_layout(void*, PangoLayout*) {}
inline void pango_cairo_update_layout(void*, PangoLayout*) {}

// -----------------------------------------------------------------------
// Cairo type and function stubs
// -----------------------------------------------------------------------
typedef struct _cairo cairo_t;
typedef struct _cairo_surface cairo_surface_t;

struct _cairo { int dummy; };
struct _cairo_surface { int dummy; };

enum cairo_format_t {
    CAIRO_FORMAT_INVALID = -1,
    CAIRO_FORMAT_ARGB32 = 0,
    CAIRO_FORMAT_RGB24 = 1,
    CAIRO_FORMAT_A8 = 2,
    CAIRO_FORMAT_A1 = 3,
    CAIRO_FORMAT_RGB16_565 = 4
};

enum cairo_antialias_t {
    CAIRO_ANTIALIAS_DEFAULT = 0,
    CAIRO_ANTIALIAS_NONE,
    CAIRO_ANTIALIAS_GRAY,
    CAIRO_ANTIALIAS_SUBPIXEL
};

enum cairo_hint_style_t {
    CAIRO_HINT_STYLE_DEFAULT = 0,
    CAIRO_HINT_STYLE_NONE,
    CAIRO_HINT_STYLE_SLIGHT,
    CAIRO_HINT_STYLE_MEDIUM,
    CAIRO_HINT_STYLE_FULL
};

enum cairo_hint_metrics_t {
    CAIRO_HINT_METRICS_DEFAULT = 0,
    CAIRO_HINT_METRICS_OFF,
    CAIRO_HINT_METRICS_ON
};

// Cairo font options
typedef struct _cairo_font_options cairo_font_options_t;
struct _cairo_font_options { int dummy; };

inline cairo_font_options_t* cairo_font_options_create() {
    static _cairo_font_options fo;
    return &fo;
}
inline void cairo_font_options_destroy(cairo_font_options_t*) {}
inline void cairo_font_options_set_hint_style(cairo_font_options_t*, cairo_hint_style_t) {}
inline void cairo_font_options_set_hint_metrics(cairo_font_options_t*, cairo_hint_metrics_t) {}
inline void cairo_font_options_set_antialias(cairo_font_options_t*, cairo_antialias_t) {}

// Cairo surface functions
inline int cairo_format_stride_for_width(cairo_format_t, int width) { return width * 4; }
inline cairo_surface_t* cairo_image_surface_create_for_data(
    unsigned char*, cairo_format_t, int, int, int) {
    static _cairo_surface surf;
    return &surf;
}
inline cairo_surface_t* cairo_image_surface_create(cairo_format_t, int, int) {
    static _cairo_surface surf;
    return &surf;
}
inline void cairo_surface_destroy(cairo_surface_t*) {}
inline void cairo_surface_flush(cairo_surface_t*) {}
inline unsigned char* cairo_image_surface_get_data(cairo_surface_t*) { return nullptr; }
inline int cairo_image_surface_get_width(cairo_surface_t*) { return 0; }
inline int cairo_image_surface_get_height(cairo_surface_t*) { return 0; }
inline int cairo_image_surface_get_stride(cairo_surface_t*) { return 0; }

// Cairo context functions
inline cairo_t* cairo_create(cairo_surface_t*) {
    static _cairo cr;
    return &cr;
}
inline void cairo_destroy(cairo_t*) {}
inline void cairo_save(cairo_t*) {}
inline void cairo_restore(cairo_t*) {}
inline void cairo_set_source_rgb(cairo_t*, double, double, double) {}
inline void cairo_set_source_rgba(cairo_t*, double, double, double, double) {}
inline void cairo_set_source_surface(cairo_t*, cairo_surface_t*, double, double) {}
inline void cairo_set_line_width(cairo_t*, double) {}
inline void cairo_move_to(cairo_t*, double, double) {}
inline void cairo_line_to(cairo_t*, double, double) {}
inline void cairo_rel_move_to(cairo_t*, double, double) {}
inline void cairo_get_current_point(cairo_t*, double* x, double* y) {
    if (x) *x = 0;
    if (y) *y = 0;
}
inline void cairo_translate(cairo_t*, double, double) {}
inline void cairo_scale(cairo_t*, double, double) {}
inline void cairo_arc(cairo_t*, double, double, double, double, double) {}
inline void cairo_rectangle(cairo_t*, double, double, double, double) {}
inline void cairo_fill(cairo_t*) {}
inline void cairo_stroke(cairo_t*) {}
inline void cairo_paint(cairo_t*) {}
inline void cairo_set_operator(cairo_t*, int) {}

// -----------------------------------------------------------------------
// Block the real pango/cairo includes
// -----------------------------------------------------------------------
// When code does #include <pango/pangocairo.h>, the guards above prevent it.
// When code does #include "font/cairo.hpp", our guards + stubs satisfy it.

// -----------------------------------------------------------------------
// fontconfig stubs (used by font/font_config.cpp)
// -----------------------------------------------------------------------
#ifndef _FONTCONFIG_FONTCONFIG_H_
#define _FONTCONFIG_FONTCONFIG_H_

typedef unsigned char FcChar8;
typedef int FcBool;
#define FcFalse 0
#define FcTrue  1

struct FcConfig {};

inline FcConfig* FcConfigGetCurrent() { static FcConfig cfg; return &cfg; }
inline FcBool FcConfigAppFontAddDir(FcConfig*, const FcChar8*) { return FcTrue; }
inline FcBool FcConfigParseAndLoadFromMemory(FcConfig*, const FcChar8*, FcBool) { return FcTrue; }
inline void FcConfigAppFontClear(FcConfig*) {}

#endif // _FONTCONFIG_FONTCONFIG_H_

#endif // N64_BUILD
#endif // N64_FONT_SHIM_HPP
