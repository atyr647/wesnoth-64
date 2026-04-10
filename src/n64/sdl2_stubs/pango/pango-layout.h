/* Comprehensive Pango stub for N64 build */
#ifndef PANGO_LAYOUT_H_STUB
#define PANGO_LAYOUT_H_STUB

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* Include enum types (defined separately to avoid conflicts) */
#include "pango-enum-types.h"

/* Basic Pango types */
typedef unsigned int guint;
typedef int gint;
typedef unsigned short guint16;
typedef int gboolean;
typedef void* gpointer;
typedef const void* gconstpointer;
typedef void (*GDestroyNotify)(gpointer data);

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef PANGO_SCALE
#define PANGO_SCALE 1024
#endif

/* Opaque types */
typedef struct _PangoContext PangoContext;
typedef struct _PangoLayout PangoLayout;
typedef struct _PangoFontDescription PangoFontDescription;
typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoAttrList PangoAttrList;
typedef struct _PangoLayoutIter PangoLayoutIter;
typedef struct _PangoLayoutLine PangoLayoutLine;
typedef struct _PangoTabArray PangoTabArray;

/* PangoRectangle */
typedef struct {
    int x, y, width, height;
} PangoRectangle;

/* PangoAttribute */
typedef struct _PangoAttrClass PangoAttrClass;
typedef struct _PangoAttribute {
    const PangoAttrClass* klass;
    guint start_index;
    guint end_index;
} PangoAttribute;

struct _PangoAttrClass {
    guint type;
};

typedef enum {
    PANGO_FONT_MASK_FAMILY  = 1 << 0,
    PANGO_FONT_MASK_STYLE   = 1 << 1,
    PANGO_FONT_MASK_VARIANT = 1 << 2,
    PANGO_FONT_MASK_WEIGHT  = 1 << 3,
    PANGO_FONT_MASK_STRETCH = 1 << 4,
    PANGO_FONT_MASK_SIZE    = 1 << 5
} PangoFontMask;

/* PangoColor */
typedef struct {
    guint16 red, green, blue;
} PangoColor;

/* Attribute functions */
static inline PangoAttribute* pango_attr_size_new(int size) {
    (void)size;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_size_new_absolute(int size) {
    return pango_attr_size_new(size);
}
static inline PangoAttribute* pango_attr_weight_new(PangoWeight w) {
    (void)w;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_style_new(PangoStyle s) {
    (void)s;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_underline_new(PangoUnderline u) {
    (void)u;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_foreground_new(guint16 r, guint16 g, guint16 b) {
    (void)r; (void)g; (void)b;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_background_new(guint16 r, guint16 g, guint16 b) {
    (void)r; (void)g; (void)b;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_family_new(const char* family) {
    (void)family;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_line_height_new(double factor) {
    (void)factor;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}
static inline PangoAttribute* pango_attr_shape_new_with_data(
    const PangoRectangle* ink, const PangoRectangle* logical,
    gpointer data, GDestroyNotify copy_func, GDestroyNotify destroy_func) {
    (void)ink; (void)logical; (void)data; (void)copy_func; (void)destroy_func;
    static PangoAttribute attr = {0, 0, (guint)-1};
    return &attr;
}

static inline void pango_attribute_destroy(PangoAttribute* attr) { (void)attr; }
static inline PangoAttribute* pango_attribute_copy(const PangoAttribute* attr) {
    (void)attr;
    static PangoAttribute copy = {0, 0, (guint)-1};
    return &copy;
}

/* Attr list functions */
static inline PangoAttrList* pango_attr_list_new(void) { return (PangoAttrList*)1; }
static inline PangoAttrList* pango_attr_list_ref(PangoAttrList* l) { return l; }
static inline void pango_attr_list_unref(PangoAttrList* l) { (void)l; }
static inline void pango_attr_list_insert(PangoAttrList* l, PangoAttribute* a) { (void)l; (void)a; }
static inline void pango_attr_list_change(PangoAttrList* l, PangoAttribute* a) { (void)l; (void)a; }
static inline void pango_attr_list_splice(PangoAttrList* l, PangoAttrList* other, gint pos, gint len) {
    (void)l; (void)other; (void)pos; (void)len;
}

/* Font description */
static inline PangoFontDescription* pango_font_description_new(void) { return (PangoFontDescription*)1; }
static inline void pango_font_description_free(PangoFontDescription* d) { (void)d; }
static inline PangoFontDescription* pango_font_description_copy(const PangoFontDescription* d) {
    (void)d; return (PangoFontDescription*)1;
}
static inline void pango_font_description_set_family(PangoFontDescription* d, const char* f) { (void)d; (void)f; }
static inline void pango_font_description_set_size(PangoFontDescription* d, gint s) { (void)d; (void)s; }
static inline void pango_font_description_set_weight(PangoFontDescription* d, PangoWeight w) { (void)d; (void)w; }
static inline void pango_font_description_set_style(PangoFontDescription* d, PangoStyle s) { (void)d; (void)s; }
static inline const char* pango_font_description_get_family(const PangoFontDescription* d) { (void)d; return "sans"; }
static inline gint pango_font_description_get_size(const PangoFontDescription* d) { (void)d; return 12 * PANGO_SCALE; }
static inline PangoWeight pango_font_description_get_weight(const PangoFontDescription* d) { (void)d; return PANGO_WEIGHT_NORMAL; }
static inline PangoStyle pango_font_description_get_style(const PangoFontDescription* d) { (void)d; return PANGO_STYLE_NORMAL; }

/* Layout */
static inline PangoLayout* pango_layout_new(PangoContext* ctx) { (void)ctx; return (PangoLayout*)1; }
static inline void pango_layout_set_text(PangoLayout* l, const char* t, int len) { (void)l; (void)t; (void)len; }
static inline void pango_layout_set_markup(PangoLayout* l, const char* m, int len) { (void)l; (void)m; (void)len; }
static inline void pango_layout_set_font_description(PangoLayout* l, const PangoFontDescription* d) { (void)l; (void)d; }
static inline void pango_layout_set_width(PangoLayout* l, int w) { (void)l; (void)w; }
static inline void pango_layout_set_height(PangoLayout* l, int h) { (void)l; (void)h; }
static inline void pango_layout_set_alignment(PangoLayout* l, PangoAlignment a) { (void)l; (void)a; }
static inline void pango_layout_set_wrap(PangoLayout* l, PangoWrapMode m) { (void)l; (void)m; }
static inline void pango_layout_set_ellipsize(PangoLayout* l, PangoEllipsizeMode m) { (void)l; (void)m; }
static inline void pango_layout_set_attributes(PangoLayout* l, PangoAttrList* a) { (void)l; (void)a; }
static inline void pango_layout_set_tabs(PangoLayout* l, PangoTabArray* t) { (void)l; (void)t; }
static inline void pango_layout_get_pixel_size(PangoLayout* l, int* w, int* h) {
    (void)l; if (w) *w = 0; if (h) *h = 0;
}
static inline void pango_layout_get_size(PangoLayout* l, int* w, int* h) {
    (void)l; if (w) *w = 0; if (h) *h = 0;
}
static inline void pango_layout_get_pixel_extents(PangoLayout* l, PangoRectangle* ink, PangoRectangle* logical) {
    (void)l;
    if (ink) { ink->x = 0; ink->y = 0; ink->width = 0; ink->height = 0; }
    if (logical) { logical->x = 0; logical->y = 0; logical->width = 0; logical->height = 0; }
}
static inline const char* pango_layout_get_text(PangoLayout* l) { (void)l; return ""; }
static inline int pango_layout_get_line_count(PangoLayout* l) { (void)l; return 1; }
static inline PangoLayoutLine* pango_layout_get_line_readonly(PangoLayout* l, int i) { (void)l; (void)i; return (PangoLayoutLine*)1; }
static inline PangoLayoutIter* pango_layout_get_iter(PangoLayout* l) { (void)l; return (PangoLayoutIter*)1; }
static inline void pango_layout_iter_free(PangoLayoutIter* i) { (void)i; }
static inline gboolean pango_layout_iter_next_line(PangoLayoutIter* i) { (void)i; return FALSE; }
static inline void pango_layout_iter_get_line_extents(PangoLayoutIter* i, PangoRectangle* ink, PangoRectangle* logical) {
    (void)i;
    if (ink) { ink->x = 0; ink->y = 0; ink->width = 0; ink->height = 0; }
    if (logical) { logical->x = 0; logical->y = 0; logical->width = 0; logical->height = 0; }
}
static inline int pango_layout_iter_get_baseline(PangoLayoutIter* i) { (void)i; return 0; }
static inline PangoContext* pango_layout_get_context(PangoLayout* l) { (void)l; return (PangoContext*)1; }
static inline const PangoFontDescription* pango_layout_get_font_description(PangoLayout* l) { (void)l; return (PangoFontDescription*)1; }

/* Context */
static inline PangoFontMap* pango_context_get_font_map(PangoContext* c) { (void)c; return (PangoFontMap*)1; }

/* Misc Pango functions */
static inline gboolean pango_parse_markup(const char* markup, int len, guint accel_marker,
    PangoAttrList** attr_list, char** text, guint* accel_char, void** error) {
    (void)markup; (void)len; (void)accel_marker; (void)error;
    if (attr_list) *attr_list = pango_attr_list_new();
    if (text) *text = 0;
    if (accel_char) *accel_char = 0;
    return TRUE;
}

/* Version strings */
static inline const char* pango_version_string(void) { return "N64 stub"; }
#define PANGO_VERSION_STRING "N64 stub"

#ifdef __cplusplus
}
#endif

#endif /* PANGO_LAYOUT_H_STUB */
