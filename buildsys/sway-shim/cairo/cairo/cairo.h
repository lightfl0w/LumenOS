#ifndef LUMEN_CAIRO_SHIM_H
#define LUMEN_CAIRO_SHIM_H

typedef struct cairo_t cairo_t;
typedef struct cairo_surface_t cairo_surface_t;
typedef struct cairo_pattern_t cairo_pattern_t;
typedef struct cairo_font_options_t cairo_font_options_t;
typedef struct {
	double xx, yx;
	double xy, yy;
	double x0, y0;
} cairo_matrix_t;

typedef enum {
	CAIRO_FORMAT_ARGB32,
	CAIRO_FORMAT_RGB24,
	CAIRO_FORMAT_A8,
	CAIRO_FORMAT_A1
} cairo_format_t;
typedef enum {
	CAIRO_STATUS_SUCCESS = 0
} cairo_status_t;
typedef enum {
	CAIRO_OPERATOR_OVER,
	CAIRO_OPERATOR_SOURCE
} cairo_operator_t;
typedef enum {
	CAIRO_ANTIALIAS_DEFAULT,
	CAIRO_ANTIALIAS_NONE,
	CAIRO_ANTIALIAS_GRAY,
	CAIRO_ANTIALIAS_SUBPIXEL,
	CAIRO_ANTIALIAS_BEST
} cairo_antialias_t;
typedef enum {
	CAIRO_SUBPIXEL_ORDER_DEFAULT,
	CAIRO_SUBPIXEL_ORDER_RGB,
	CAIRO_SUBPIXEL_ORDER_BGR,
	CAIRO_SUBPIXEL_ORDER_VRGB,
	CAIRO_SUBPIXEL_ORDER_VBGR
} cairo_subpixel_order_t;
typedef enum {
	CAIRO_EXTEND_NONE,
	CAIRO_EXTEND_REPEAT,
	CAIRO_EXTEND_REFLECT,
	CAIRO_EXTEND_PAD
} cairo_extend_t;

cairo_surface_t *cairo_image_surface_create(cairo_format_t format,
	int width, int height);
cairo_surface_t *cairo_image_surface_create_for_data(unsigned char *data,
	cairo_format_t format, int width, int height, int stride);
cairo_surface_t *cairo_image_surface_create_from_png(const char *filename);
cairo_status_t cairo_surface_status(cairo_surface_t *surface);
unsigned char *cairo_image_surface_get_data(cairo_surface_t *surface);
int cairo_image_surface_get_stride(cairo_surface_t *surface);
int cairo_image_surface_get_width(cairo_surface_t *surface);
int cairo_image_surface_get_height(cairo_surface_t *surface);
void cairo_surface_flush(cairo_surface_t *surface);
void cairo_surface_mark_dirty(cairo_surface_t *surface);
void cairo_surface_destroy(cairo_surface_t *surface);
cairo_surface_t *cairo_recording_surface_create(int content,
	const void *extents);
cairo_pattern_t *cairo_pattern_create_for_surface(cairo_surface_t *surface);
void cairo_pattern_set_extend(cairo_pattern_t *pattern, cairo_extend_t extend);
void cairo_pattern_set_matrix(cairo_pattern_t *pattern,
	const cairo_matrix_t *matrix);
cairo_status_t cairo_pattern_status(cairo_pattern_t *pattern);
void cairo_pattern_destroy(cairo_pattern_t *pattern);
cairo_t *cairo_create(cairo_surface_t *surface);
void cairo_destroy(cairo_t *cairo);
void cairo_set_source_u32(cairo_t *cairo, unsigned int color);
cairo_surface_t *cairo_image_surface_scale(cairo_surface_t *image,
	int width, int height);
void cairo_set_source_rgba(cairo_t *cairo, double red, double green,
	double blue, double alpha);
void cairo_set_source(cairo_t *cairo, cairo_pattern_t *pattern);
void cairo_set_source_surface(cairo_t *cairo, cairo_surface_t *surface,
	double x, double y);
void cairo_set_operator(cairo_t *cairo, cairo_operator_t operator);
cairo_operator_t cairo_get_operator(cairo_t *cairo);
void cairo_set_antialias(cairo_t *cairo, cairo_antialias_t antialias);
void cairo_set_font_options(cairo_t *cairo,
	const cairo_font_options_t *options);
void cairo_get_font_options(cairo_t *cairo, cairo_font_options_t *options);
cairo_font_options_t *cairo_font_options_create(void);
void cairo_font_options_destroy(cairo_font_options_t *options);
void cairo_font_options_set_antialias(cairo_font_options_t *options,
	cairo_antialias_t antialias);
void cairo_font_options_set_subpixel_order(cairo_font_options_t *options,
	cairo_subpixel_order_t order);
const char *cairo_status_to_string(cairo_status_t status);
int cairo_format_stride_for_width(cairo_format_t format, int width);
void cairo_save(cairo_t *cairo);
void cairo_restore(cairo_t *cairo);
void cairo_move_to(cairo_t *cairo, double x, double y);
void cairo_line_to(cairo_t *cairo, double x, double y);
void cairo_rectangle(cairo_t *cairo, double x, double y,
	double width, double height);
void cairo_arc(cairo_t *cairo, double x, double y, double radius,
	double angle1, double angle2);
void cairo_fill(cairo_t *cairo);
void cairo_stroke(cairo_t *cairo);
void cairo_paint(cairo_t *cairo);
void cairo_scale(cairo_t *cairo, double sx, double sy);
void cairo_translate(cairo_t *cairo, double tx, double ty);
void cairo_matrix_init_scale(cairo_matrix_t *matrix, double sx, double sy);
void cairo_matrix_translate(cairo_matrix_t *matrix, double tx, double ty);

#endif
