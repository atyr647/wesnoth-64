/* Pango font stub for N64 build */
#ifndef PANGO_FONT_H_STUB
#define PANGO_FONT_H_STUB

#include "pango-types.h"
#include "pango-enum-types.h"

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PangoFontDescription */
struct _PangoFontDescription {
    char family[256];
    int size;
    PangoStyle style;
    PangoWeight weight;
};

/* PangoFont */
struct _PangoFont {
    int dummy;
};

/* PangoFontMetrics */
struct _PangoFontMetrics {
    int ref_count;
    int ascent;
    int descent;
    int approximate_char_width;
    int approximate_digit_width;
};

/* PangoFontMap */
struct _PangoFontMap {
    int dummy;
};

/* --- PangoFontDescription functions --- */
static inline PangoFontDescription* pango_font_description_new(void)
{
    PangoFontDescription* fd = (PangoFontDescription*)calloc(1, sizeof(PangoFontDescription));
    if(fd) {
        fd->weight = PANGO_WEIGHT_NORMAL;
        fd->style = PANGO_STYLE_NORMAL;
        fd->size = 12 * PANGO_SCALE;
    }
    return fd;
}

static inline void pango_font_description_free(PangoFontDescription* desc)
{
    if(desc) free(desc);
}

static inline PangoFontDescription* pango_font_description_copy(const PangoFontDescription* desc)
{
    if(!desc) return NULL;
    PangoFontDescription* copy = (PangoFontDescription*)malloc(sizeof(PangoFontDescription));
    if(copy) memcpy(copy, desc, sizeof(PangoFontDescription));
    return copy;
}

static inline void pango_font_description_set_family(PangoFontDescription* desc, const char* family)
{
    if(desc && family) {
        strncpy(desc->family, family, sizeof(desc->family) - 1);
        desc->family[sizeof(desc->family) - 1] = '\0';
    }
}

static inline const char* pango_font_description_get_family(const PangoFontDescription* desc)
{
    return desc ? desc->family : "";
}

static inline void pango_font_description_set_size(PangoFontDescription* desc, int size)
{
    if(desc) desc->size = size;
}

static inline int pango_font_description_get_size(const PangoFontDescription* desc)
{
    return desc ? desc->size : 0;
}

static inline void pango_font_description_set_style(PangoFontDescription* desc, PangoStyle style)
{
    if(desc) desc->style = style;
}

static inline PangoStyle pango_font_description_get_style(const PangoFontDescription* desc)
{
    return desc ? desc->style : PANGO_STYLE_NORMAL;
}

static inline void pango_font_description_set_weight(PangoFontDescription* desc, PangoWeight weight)
{
    if(desc) desc->weight = weight;
}

static inline PangoWeight pango_font_description_get_weight(const PangoFontDescription* desc)
{
    return desc ? desc->weight : PANGO_WEIGHT_NORMAL;
}

/* --- PangoFont functions --- */
static inline PangoFontMetrics* pango_font_get_metrics(PangoFont* font, void* language)
{
    (void)font; (void)language;
    PangoFontMetrics* m = (PangoFontMetrics*)calloc(1, sizeof(PangoFontMetrics));
    if(m) {
        m->ref_count = 1;
        m->ascent = 12 * PANGO_SCALE;
        m->descent = 4 * PANGO_SCALE;
        m->approximate_char_width = 8 * PANGO_SCALE;
        m->approximate_digit_width = 8 * PANGO_SCALE;
    }
    return m;
}

/* --- PangoFontMetrics functions --- */
static inline int pango_font_metrics_get_ascent(PangoFontMetrics* metrics)
{
    return metrics ? metrics->ascent : 0;
}

static inline int pango_font_metrics_get_descent(PangoFontMetrics* metrics)
{
    return metrics ? metrics->descent : 0;
}

static inline int pango_font_metrics_get_approximate_char_width(PangoFontMetrics* metrics)
{
    return metrics ? metrics->approximate_char_width : 0;
}

static inline int pango_font_metrics_get_approximate_digit_width(PangoFontMetrics* metrics)
{
    return metrics ? metrics->approximate_digit_width : 0;
}

static inline void pango_font_metrics_unref(PangoFontMetrics* metrics)
{
    if(metrics) free(metrics);
}

/* --- PangoFontMap functions --- */
static inline PangoFont* pango_font_map_load_font(PangoFontMap* fontmap, PangoContext* context, const PangoFontDescription* desc)
{
    (void)fontmap; (void)context; (void)desc;
    PangoFont* f = (PangoFont*)calloc(1, sizeof(PangoFont));
    return f;
}

static inline PangoContext* pango_font_map_create_context(PangoFontMap* fontmap)
{
    (void)fontmap;
    PangoContext* ctx = (PangoContext*)calloc(1, sizeof(PangoContext));
    return ctx;
}

#ifdef __cplusplus
}
#endif

#endif /* PANGO_FONT_H_STUB */
