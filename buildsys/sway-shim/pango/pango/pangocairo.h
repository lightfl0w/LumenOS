#ifndef LUMEN_PANGO_CAIRO_SHIM_H
#define LUMEN_PANGO_CAIRO_SHIM_H

#include <cairo.h>
#include "pango.h"

PangoContext *pango_cairo_create_context(cairo_t *cairo);
PangoLayout *pango_cairo_create_layout(cairo_t *cairo);
void pango_cairo_update_layout(cairo_t *cairo, PangoLayout *layout);
void pango_cairo_show_layout(cairo_t *cairo, PangoLayout *layout);
void pango_cairo_context_set_font_options(PangoContext *context,
	const cairo_font_options_t *options);
void pango_cairo_font_map_set_default(void *font_map);

#endif
