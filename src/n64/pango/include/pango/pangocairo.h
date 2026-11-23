/* pangocairo.h -- Pango/cairo glue subset for the Nintendo 64 (see pango.h). */
#ifndef N64_PANGOCAIRO_H
#define N64_PANGOCAIRO_H
#include <pango/pango.h>
#include <cairo.h>

typedef void (*PangoCairoShapeRendererFunc)(cairo_t* cr, PangoAttrShape* attr, gboolean do_path, gpointer data);

PangoFontMap* pango_cairo_font_map_get_default(void);
void pango_cairo_context_set_resolution(PangoContext* context, double dpi);
double pango_cairo_context_get_resolution(PangoContext* context);
void pango_cairo_context_set_font_options(PangoContext* context, const cairo_font_options_t* options);
void pango_cairo_context_set_shape_renderer(PangoContext* context, PangoCairoShapeRendererFunc func,
                                            gpointer data, GDestroyNotify dnotify);
PangoContext* pango_cairo_create_context(cairo_t* cr);
PangoLayout* pango_cairo_create_layout(cairo_t* cr);
void pango_cairo_update_context(cairo_t* cr, PangoContext* context);
void pango_cairo_update_layout(cairo_t* cr, PangoLayout* layout);
void pango_cairo_show_layout(cairo_t* cr, PangoLayout* layout);
void pango_cairo_layout_path(cairo_t* cr, PangoLayout* layout);
#endif
