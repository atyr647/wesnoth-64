/* Pango attributes stub for N64 build */
#ifndef PANGO_ATTRIBUTES_H_STUB
#define PANGO_ATTRIBUTES_H_STUB

#include "pango-types.h"
#include "pango-enum-types.h"

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PangoAttribute */
typedef struct _PangoAttribute PangoAttribute;
struct _PangoAttribute {
    unsigned int start_index;
    unsigned int end_index;
};

/* PangoAttrShape - used for inline images */
typedef struct _PangoAttrShape PangoAttrShape;
struct _PangoAttrShape {
    PangoAttribute attr;
    PangoRectangle ink_rect;
    PangoRectangle logical_rect;
    gpointer data;
    GDestroyNotify copy_func;
    GDestroyNotify destroy_func;
};

/* PangoAttrList is opaque */
struct _PangoAttrList {
    int dummy;
};

/* Callback type for shape rendering */
typedef void (*PangoAttrDataCopyFunc)(gpointer);

/* --- PangoAttribute functions --- */
static inline void pango_attribute_destroy(PangoAttribute* attr)
{
    if(attr) free(attr);
}

static inline PangoAttribute* pango_attribute_copy(const PangoAttribute* attr)
{
    if(!attr) return NULL;
    PangoAttribute* copy = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(copy) {
        copy->start_index = attr->start_index;
        copy->end_index = attr->end_index;
    }
    return copy;
}

/* --- PangoAttrList functions --- */
static inline PangoAttrList* pango_attr_list_new(void)
{
    PangoAttrList* list = (PangoAttrList*)malloc(sizeof(PangoAttrList));
    if(list) list->dummy = 0;
    return list;
}

static inline PangoAttrList* pango_attr_list_ref(PangoAttrList* list)
{
    return list;
}

static inline void pango_attr_list_unref(PangoAttrList* list)
{
    if(list) free(list);
}

static inline void pango_attr_list_insert(PangoAttrList* list, PangoAttribute* attr)
{
    (void)list;
    if(attr) free(attr);
}

static inline void pango_attr_list_change(PangoAttrList* list, PangoAttribute* attr)
{
    (void)list;
    if(attr) free(attr);
}

static inline void pango_attr_list_splice(PangoAttrList* list, PangoAttrList* other, gint pos, gint len)
{
    (void)list; (void)other; (void)pos; (void)len;
}

/* --- Attribute creation functions --- */
static inline PangoAttribute* pango_attr_size_new(int size)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)size;
    return attr;
}

static inline PangoAttribute* pango_attr_size_new_absolute(int size)
{
    return pango_attr_size_new(size);
}

static inline PangoAttribute* pango_attr_weight_new(PangoWeight weight)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)weight;
    return attr;
}

static inline PangoAttribute* pango_attr_style_new(PangoStyle style)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)style;
    return attr;
}

static inline PangoAttribute* pango_attr_underline_new(PangoUnderline underline)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)underline;
    return attr;
}

static inline PangoAttribute* pango_attr_foreground_new(guint16 red, guint16 green, guint16 blue)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)red; (void)green; (void)blue;
    return attr;
}

static inline PangoAttribute* pango_attr_background_new(guint16 red, guint16 green, guint16 blue)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)red; (void)green; (void)blue;
    return attr;
}

static inline PangoAttribute* pango_attr_family_new(const char* family)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)family;
    return attr;
}

static inline PangoAttribute* pango_attr_line_height_new(double factor)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttribute));
    if(attr) { attr->start_index = 0; attr->end_index = (unsigned int)-1; }
    (void)factor;
    return attr;
}

static inline PangoAttribute* pango_attr_shape_new_with_data(
    const PangoRectangle* ink_rect,
    const PangoRectangle* logical_rect,
    gpointer data,
    PangoAttrDataCopyFunc copy_func,
    GDestroyNotify destroy_func)
{
    PangoAttribute* attr = (PangoAttribute*)malloc(sizeof(PangoAttrShape));
    if(attr) {
        attr->start_index = 0;
        attr->end_index = (unsigned int)-1;
        PangoAttrShape* shape = (PangoAttrShape*)attr;
        if(ink_rect) shape->ink_rect = *ink_rect;
        if(logical_rect) shape->logical_rect = *logical_rect;
        shape->data = data;
    }
    (void)copy_func; (void)destroy_func;
    return attr;
}

#ifdef __cplusplus
}
#endif

#endif /* PANGO_ATTRIBUTES_H_STUB */
