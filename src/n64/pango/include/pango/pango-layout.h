/* pango-layout.h -- PangoLayout subset for the Nintendo 64 (see pango.h). */
#ifndef N64_PANGO_LAYOUT_H
#define N64_PANGO_LAYOUT_H
#include <pango/pango.h>

typedef struct _PangoLayout PangoLayout;
typedef struct _PangoLayoutIter PangoLayoutIter;
typedef struct _PangoLayoutLine {
    PangoLayout* layout;
    gint start_index;           /* byte index of the line's first character */
    gint length;                /* bytes in the line */
    void* runs;                 /* not exposed */
    guint is_paragraph_start : 1;
    guint resolved_dir : 3;
} PangoLayoutLine;

PangoLayout* pango_layout_new(PangoContext* context);
PangoContext* pango_layout_get_context(PangoLayout* layout);
void pango_layout_set_text(PangoLayout* layout, const char* text, int length);
const char* pango_layout_get_text(PangoLayout* layout);
void pango_layout_set_markup(PangoLayout* layout, const char* markup, int length);
void pango_layout_set_attributes(PangoLayout* layout, PangoAttrList* attrs);
PangoAttrList* pango_layout_get_attributes(PangoLayout* layout);
void pango_layout_set_font_description(PangoLayout* layout, const PangoFontDescription* desc);
void pango_layout_set_width(PangoLayout* layout, int width);
int pango_layout_get_width(PangoLayout* layout);
void pango_layout_set_height(PangoLayout* layout, int height);
int pango_layout_get_height(PangoLayout* layout);
void pango_layout_set_wrap(PangoLayout* layout, PangoWrapMode wrap);
PangoWrapMode pango_layout_get_wrap(PangoLayout* layout);
void pango_layout_set_ellipsize(PangoLayout* layout, PangoEllipsizeMode ellipsize);
PangoEllipsizeMode pango_layout_get_ellipsize(PangoLayout* layout);
gboolean pango_layout_is_ellipsized(PangoLayout* layout);
void pango_layout_set_alignment(PangoLayout* layout, PangoAlignment alignment);
PangoAlignment pango_layout_get_alignment(PangoLayout* layout);
void pango_layout_set_line_spacing(PangoLayout* layout, float factor);
float pango_layout_get_line_spacing(PangoLayout* layout);
void pango_layout_set_spacing(PangoLayout* layout, int spacing);
void pango_layout_context_changed(PangoLayout* layout);

void pango_layout_get_extents(PangoLayout* layout, PangoRectangle* ink_rect, PangoRectangle* logical_rect);
void pango_layout_get_pixel_extents(PangoLayout* layout, PangoRectangle* ink_rect, PangoRectangle* logical_rect);
void pango_layout_get_size(PangoLayout* layout, int* width, int* height);
void pango_layout_get_pixel_size(PangoLayout* layout, int* width, int* height);
int pango_layout_get_baseline(PangoLayout* layout);

int pango_layout_get_line_count(PangoLayout* layout);
PangoLayoutLine* pango_layout_get_line(PangoLayout* layout, int line);
PangoLayoutLine* pango_layout_get_line_readonly(PangoLayout* layout, int line);

void pango_layout_index_to_pos(PangoLayout* layout, int index, PangoRectangle* pos);
void pango_layout_index_to_line_x(PangoLayout* layout, int index, gboolean trailing, int* line, int* x_pos);
void pango_layout_get_cursor_pos(PangoLayout* layout, int index, PangoRectangle* strong_pos, PangoRectangle* weak_pos);
gboolean pango_layout_xy_to_index(PangoLayout* layout, int x, int y, int* index, int* trailing);

PangoLayoutIter* pango_layout_get_iter(PangoLayout* layout);
void pango_layout_iter_free(PangoLayoutIter* iter);
gboolean pango_layout_iter_next_line(PangoLayoutIter* iter);
gboolean pango_layout_iter_next_char(PangoLayoutIter* iter);
int pango_layout_iter_get_index(PangoLayoutIter* iter);
PangoLayoutLine* pango_layout_iter_get_line(PangoLayoutIter* iter);
PangoLayoutLine* pango_layout_iter_get_line_readonly(PangoLayoutIter* iter);
void pango_layout_iter_get_line_extents(PangoLayoutIter* iter, PangoRectangle* ink_rect, PangoRectangle* logical_rect);
int pango_layout_iter_get_baseline(PangoLayoutIter* iter);

void pango_layout_line_get_extents(PangoLayoutLine* line, PangoRectangle* ink_rect, PangoRectangle* logical_rect);
void pango_layout_line_get_pixel_extents(PangoLayoutLine* line, PangoRectangle* ink_rect, PangoRectangle* logical_rect);
void pango_layout_line_index_to_x(PangoLayoutLine* line, int index, gboolean trailing, int* x_pos);

#endif
