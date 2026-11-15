#define _GNU_SOURCE
#include <cairo.h>
#include <pango.h>
#include <pangocairo.h>
#include <json.h>
#include <libevdev/libevdev.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <wayland-server-protocol.h>

cairo_surface_t *cairo_image_surface_create(cairo_format_t format,
	int width, int height) { (void)format; (void)width; (void)height; return NULL; }
cairo_surface_t *cairo_image_surface_create_for_data(unsigned char *data,
	cairo_format_t format, int width, int height, int stride) {
	(void)data; (void)format; (void)width; (void)height; (void)stride; return NULL; }
cairo_surface_t *cairo_image_surface_create_from_png(const char *filename) {
	(void)filename; return NULL; }
cairo_status_t cairo_surface_status(cairo_surface_t *surface) { (void)surface; return CAIRO_STATUS_SUCCESS; }
unsigned char *cairo_image_surface_get_data(cairo_surface_t *surface) { (void)surface; return NULL; }
int cairo_image_surface_get_stride(cairo_surface_t *surface) { (void)surface; return 0; }
int cairo_image_surface_get_width(cairo_surface_t *surface) { (void)surface; return 0; }
int cairo_image_surface_get_height(cairo_surface_t *surface) { (void)surface; return 0; }
void cairo_surface_flush(cairo_surface_t *surface) { (void)surface; }
void cairo_surface_mark_dirty(cairo_surface_t *surface) { (void)surface; }
void cairo_surface_destroy(cairo_surface_t *surface) { (void)surface; }
cairo_surface_t *cairo_recording_surface_create(int content, const void *extents) {
	(void)content; (void)extents; return NULL; }
cairo_pattern_t *cairo_pattern_create_for_surface(cairo_surface_t *surface) { (void)surface; return NULL; }
void cairo_pattern_set_extend(cairo_pattern_t *pattern, cairo_extend_t extend) { (void)pattern; (void)extend; }
void cairo_pattern_set_matrix(cairo_pattern_t *pattern, const cairo_matrix_t *matrix) { (void)pattern; (void)matrix; }
cairo_status_t cairo_pattern_status(cairo_pattern_t *pattern) { (void)pattern; return CAIRO_STATUS_SUCCESS; }
void cairo_pattern_destroy(cairo_pattern_t *pattern) { (void)pattern; }
cairo_t *cairo_create(cairo_surface_t *surface) { (void)surface; return NULL; }
void cairo_destroy(cairo_t *cairo) { (void)cairo; }
void cairo_set_source_u32(cairo_t *cairo, unsigned int color) { (void)cairo; (void)color; }
cairo_subpixel_order_t to_cairo_subpixel_order(enum wl_output_subpixel subpixel) { (void)subpixel; return CAIRO_SUBPIXEL_ORDER_DEFAULT; }
cairo_surface_t *cairo_image_surface_scale(cairo_surface_t *image,
	int width, int height) { (void)image; (void)width; (void)height; return NULL; }
void cairo_set_source_rgba(cairo_t *cairo, double red, double green,
	double blue, double alpha) { (void)cairo; (void)red; (void)green; (void)blue; (void)alpha; }
void cairo_set_source(cairo_t *cairo, cairo_pattern_t *pattern) { (void)cairo; (void)pattern; }
void cairo_set_source_surface(cairo_t *cairo, cairo_surface_t *surface,
	double x, double y) { (void)cairo; (void)surface; (void)x; (void)y; }
void cairo_set_operator(cairo_t *cairo, cairo_operator_t operator) { (void)cairo; (void)operator; }
cairo_operator_t cairo_get_operator(cairo_t *cairo) { (void)cairo; return CAIRO_OPERATOR_OVER; }
void cairo_set_antialias(cairo_t *cairo, cairo_antialias_t antialias) { (void)cairo; (void)antialias; }
void cairo_set_font_options(cairo_t *cairo, const cairo_font_options_t *options) { (void)cairo; (void)options; }
void cairo_get_font_options(cairo_t *cairo, cairo_font_options_t *options) { (void)cairo; (void)options; }
cairo_font_options_t *cairo_font_options_create(void) { return NULL; }
void cairo_font_options_destroy(cairo_font_options_t *options) { (void)options; }
void cairo_font_options_set_antialias(cairo_font_options_t *options,
	cairo_antialias_t antialias) { (void)options; (void)antialias; }
void cairo_font_options_set_subpixel_order(cairo_font_options_t *options,
	cairo_subpixel_order_t order) { (void)options; (void)order; }
const char *cairo_status_to_string(cairo_status_t status) { (void)status; return "disabled"; }
int cairo_format_stride_for_width(cairo_format_t format, int width) {
	(void)format; return width * 4; }
void cairo_save(cairo_t *cairo) { (void)cairo; }
void cairo_restore(cairo_t *cairo) { (void)cairo; }
void cairo_move_to(cairo_t *cairo, double x, double y) { (void)cairo; (void)x; (void)y; }
void cairo_line_to(cairo_t *cairo, double x, double y) { (void)cairo; (void)x; (void)y; }
void cairo_rectangle(cairo_t *cairo, double x, double y, double width, double height) {
	(void)cairo; (void)x; (void)y; (void)width; (void)height; }
void cairo_arc(cairo_t *cairo, double x, double y, double radius,
	double angle1, double angle2) { (void)cairo; (void)x; (void)y; (void)radius; (void)angle1; (void)angle2; }
void cairo_fill(cairo_t *cairo) { (void)cairo; }
void cairo_stroke(cairo_t *cairo) { (void)cairo; }
void cairo_paint(cairo_t *cairo) { (void)cairo; }
void cairo_scale(cairo_t *cairo, double sx, double sy) { (void)cairo; (void)sx; (void)sy; }
void cairo_translate(cairo_t *cairo, double tx, double ty) { (void)cairo; (void)tx; (void)ty; }
void cairo_matrix_init_scale(cairo_matrix_t *matrix, double sx, double sy) {
	*matrix = (cairo_matrix_t){sx, 0, 0, sy, 0, 0}; }
void cairo_matrix_translate(cairo_matrix_t *matrix, double tx, double ty) {
	matrix->x0 = tx; matrix->y0 = ty; }

PangoFontDescription *pango_font_description_from_string(const char *text) { (void)text; return NULL; }
void pango_font_description_free(PangoFontDescription *description) { (void)description; }
const char *pango_font_description_get_family(PangoFontDescription *description) { (void)description; return "monospace"; }
int pango_font_description_get_size(PangoFontDescription *description) { (void)description; return 10; }
char *pango_font_description_to_string(PangoFontDescription *description) { (void)description; return strdup("monospace 10"); }
PangoFontDescription *pango_font_description_copy(PangoFontDescription *description) { (void)description; return NULL; }
PangoFontMetrics *pango_context_get_metrics(PangoContext *context,
	PangoFontDescription *description, void *language) { (void)context; (void)description; (void)language; return NULL; }
void pango_font_metrics_unref(PangoFontMetrics *metrics) { (void)metrics; }
int pango_font_metrics_get_ascent(PangoFontMetrics *metrics) { (void)metrics; return 8; }
int pango_font_metrics_get_descent(PangoFontMetrics *metrics) { (void)metrics; return 2; }
PangoAttrList *pango_attr_list_new(void) { return NULL; }
void pango_attr_list_insert(PangoAttrList *list, void *attribute) { (void)list; (void)attribute; }
void pango_attr_list_unref(PangoAttrList *list) { (void)list; }
void *pango_attr_scale_new(double scale) { (void)scale; return NULL; }
int pango_parse_markup(const char *markup, int length, signed char accelerator,
	PangoAttrList **attributes, char **text, signed char *accelerator_char, void **error) {
	(void)markup; (void)length; (void)accelerator; (void)attributes; (void)text;
	(void)accelerator_char; (void)error; return 0; }
PangoContext *pango_cairo_create_context(cairo_t *cairo) { (void)cairo; return NULL; }
PangoLayout *pango_cairo_create_layout(cairo_t *cairo) { (void)cairo; return NULL; }
void pango_cairo_update_layout(cairo_t *cairo, PangoLayout *layout) { (void)cairo; (void)layout; }
void pango_cairo_show_layout(cairo_t *cairo, PangoLayout *layout) { (void)cairo; (void)layout; }
void pango_cairo_context_set_font_options(PangoContext *context,
	const cairo_font_options_t *options) { (void)context; (void)options; }
void pango_cairo_font_map_set_default(void *font_map) { (void)font_map; }

json_object *json_object_new_object(void) { return NULL; }
json_object *json_object_new_array(void) { return NULL; }
json_object *json_object_new_string(const char *value) { (void)value; return NULL; }
json_object *json_object_new_int(int value) { (void)value; return NULL; }
json_object *json_object_new_double(double value) { (void)value; return NULL; }
json_object *json_object_new_boolean(int value) { (void)value; return NULL; }
void json_object_object_add(json_object *object, const char *key, json_object *value) {
	(void)object; (void)key; (void)value; }
void json_object_array_add(json_object *array, json_object *value) { (void)array; (void)value; }
int json_object_array_length(json_object *array) { (void)array; return 0; }
json_object *json_object_array_get_idx(json_object *array, int index) { (void)array; (void)index; return NULL; }
json_object *json_object_put(json_object *object) { (void)object; return NULL; }
const char *json_object_to_json_string(json_object *object) { (void)object; return "{}"; }
const char *json_object_get_string(json_object *object) { (void)object; return ""; }
int json_object_get_int(json_object *object) { (void)object; return 0; }
json_object *json_object_get(json_object *object) { (void)object; return object; }
json_object *json_tokener_parse(const char *text) { (void)text; return NULL; }
enum json_type json_object_get_type(json_object *object) { (void)object; return json_type_null; }
void json_object_object_del(json_object *object, const char *key) { (void)object; (void)key; }

const char *libevdev_event_code_get_name(unsigned int type, unsigned int code) {
	(void)type; (void)code; return ""; }
int libevdev_event_code_from_name(unsigned int type, const char *name) {
	(void)type; (void)name; return 0; }

size_t escape_markup_text(const char *src, char *dest) {
	if (dest) strcpy(dest, src);
	return strlen(src);
}
