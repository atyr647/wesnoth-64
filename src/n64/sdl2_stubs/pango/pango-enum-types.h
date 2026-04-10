/* Pango enum types stub for N64 build */
#ifndef PANGO_ENUM_TYPES_H_STUB
#define PANGO_ENUM_TYPES_H_STUB

#ifdef __cplusplus
extern "C" {
#endif

/* PangoStyle */
typedef enum {
    PANGO_STYLE_NORMAL,
    PANGO_STYLE_OBLIQUE,
    PANGO_STYLE_ITALIC
} PangoStyle;

/* PangoWeight */
typedef enum {
    PANGO_WEIGHT_THIN = 100,
    PANGO_WEIGHT_ULTRALIGHT = 200,
    PANGO_WEIGHT_LIGHT = 300,
    PANGO_WEIGHT_SEMILIGHT = 350,
    PANGO_WEIGHT_BOOK = 380,
    PANGO_WEIGHT_NORMAL = 400,
    PANGO_WEIGHT_MEDIUM = 500,
    PANGO_WEIGHT_SEMIBOLD = 600,
    PANGO_WEIGHT_BOLD = 700,
    PANGO_WEIGHT_ULTRABOLD = 800,
    PANGO_WEIGHT_HEAVY = 900,
    PANGO_WEIGHT_ULTRAHEAVY = 1000
} PangoWeight;

/* PangoUnderline */
typedef enum {
    PANGO_UNDERLINE_NONE,
    PANGO_UNDERLINE_SINGLE,
    PANGO_UNDERLINE_DOUBLE,
    PANGO_UNDERLINE_LOW,
    PANGO_UNDERLINE_ERROR
} PangoUnderline;

/* PangoAlignment */
typedef enum {
    PANGO_ALIGN_LEFT,
    PANGO_ALIGN_CENTER,
    PANGO_ALIGN_RIGHT
} PangoAlignment;

/* PangoEllipsizeMode */
typedef enum {
    PANGO_ELLIPSIZE_NONE,
    PANGO_ELLIPSIZE_START,
    PANGO_ELLIPSIZE_MIDDLE,
    PANGO_ELLIPSIZE_END
} PangoEllipsizeMode;

/* PangoWrapMode */
typedef enum {
    PANGO_WRAP_WORD,
    PANGO_WRAP_CHAR,
    PANGO_WRAP_WORD_CHAR
} PangoWrapMode;

#ifdef __cplusplus
}
#endif

#endif /* PANGO_ENUM_TYPES_H_STUB */
