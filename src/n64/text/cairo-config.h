/* cairo build configuration for the N64 (newlib, one thread). */
#define WORDS_BIGENDIAN 1
#define FLOAT_WORDS_BIGENDIAN 1
#define CAIRO_NO_MUTEX 1
#define HAS_PIXMAN_GLYPHS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_UINT64_T 1
#define HAVE_CTYPE_H 1
#define HAVE_FENV_H 1
#define HAVE_FT_LOAD_NO_SVG 1
#define HAVE_STRNDUP 1
#define SIZEOF_VOID_P 4
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_SIZE_T 4
#define WARN_UNUSED_RESULT __attribute__((__warn_unused_result__))
