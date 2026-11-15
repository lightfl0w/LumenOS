#ifndef LUMEN_PANGO_SHIM_H
#define LUMEN_PANGO_SHIM_H

typedef struct PangoContext PangoContext;
typedef struct PangoLayout PangoLayout;
typedef struct PangoFontDescription PangoFontDescription;
typedef struct PangoFontMetrics PangoFontMetrics;
typedef struct PangoAttrList PangoAttrList;

enum PangoEllipsizeMode {
	PANGO_ELLIPSIZE_NONE,
	PANGO_ELLIPSIZE_START,
	PANGO_ELLIPSIZE_MIDDLE,
	PANGO_ELLIPSIZE_END
};
enum PangoWrapMode {
	PANGO_WRAP_WORD,
	PANGO_WRAP_CHAR,
	PANGO_WRAP_WORD_CHAR
};

PangoFontDescription *pango_font_description_from_string(const char *text);
void pango_font_description_free(PangoFontDescription *description);
const char *pango_font_description_get_family(PangoFontDescription *description);
int pango_font_description_get_size(PangoFontDescription *description);
char *pango_font_description_to_string(PangoFontDescription *description);
PangoFontDescription *pango_font_description_copy(PangoFontDescription *description);
PangoFontMetrics *pango_context_get_metrics(PangoContext *context,
	PangoFontDescription *description, void *language);
void pango_font_metrics_unref(PangoFontMetrics *metrics);
int pango_font_metrics_get_ascent(PangoFontMetrics *metrics);
int pango_font_metrics_get_descent(PangoFontMetrics *metrics);
PangoAttrList *pango_attr_list_new(void);
void pango_attr_list_insert(PangoAttrList *list, void *attribute);
void pango_attr_list_unref(PangoAttrList *list);
void *pango_attr_scale_new(double scale);
int pango_parse_markup(const char *markup, int length, signed char accelerator,
	PangoAttrList **attributes, char **text, signed char *accelerator_char, void **error);

#endif
