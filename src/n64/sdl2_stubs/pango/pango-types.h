/* Pango types stub for N64 build */
#ifndef PANGO_TYPES_H_STUB
#define PANGO_TYPES_H_STUB

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Basic Pango types */
typedef int gboolean;
typedef int gint;
typedef unsigned int guint;
typedef unsigned short guint16;
typedef char gchar;
typedef unsigned char guchar;
typedef void* gpointer;
typedef double gdouble;
typedef float gfloat;
typedef unsigned long gulong;
typedef long glong;
typedef size_t gsize;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif

typedef void (*GDestroyNotify)(gpointer data);
typedef struct _GError GError;
struct _GError {
    int domain;
    int code;
    char* message;
};

/* g_object_unref stub */
static inline void g_object_unref(gpointer obj) { (void)obj; }
static inline void g_free(gpointer mem) { (void)mem; }

/* PANGO_SCALE: Number of pango units in a device unit (point for font sizes) */
#define PANGO_SCALE 1024

/* Convert between pango units and device units */
#define PANGO_PIXELS(d) (((int)(d) + 512) >> 10)
#define PANGO_PIXELS_FLOOR(d) ((int)(d) >> 10)
#define PANGO_PIXELS_CEIL(d) (((int)(d) + 1023) >> 10)

static inline double pango_units_to_double(int i) { return (double)i / (double)PANGO_SCALE; }
static inline int pango_units_from_double(double d) { return (int)(d * PANGO_SCALE + 0.5); }

/* PangoRectangle */
typedef struct {
    int x;
    int y;
    int width;
    int height;
} PangoRectangle;

/* Forward declarations */
typedef struct _PangoContext PangoContext;
typedef struct _PangoLayout PangoLayout;
typedef struct _PangoFontDescription PangoFontDescription;
typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoFont PangoFont;
typedef struct _PangoFontMetrics PangoFontMetrics;
typedef struct _PangoLayoutIter PangoLayoutIter;
typedef struct _PangoLayoutLine PangoLayoutLine;
typedef struct _PangoAttrList PangoAttrList;

#ifdef __cplusplus
}
#endif

#endif /* PANGO_TYPES_H_STUB */
