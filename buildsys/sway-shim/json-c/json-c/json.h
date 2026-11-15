#ifndef LUMEN_JSON_C_SHIM_H
#define LUMEN_JSON_C_SHIM_H

#include <stddef.h>

typedef struct json_object json_object;
typedef enum json_type {
	json_type_null,
	json_type_boolean,
	json_type_double,
	json_type_int,
	json_type_object,
	json_type_array,
	json_type_string
} json_type;

json_object *json_object_new_object(void);
json_object *json_object_new_array(void);
json_object *json_object_new_string(const char *value);
json_object *json_object_new_int(int value);
json_object *json_object_new_double(double value);
json_object *json_object_new_boolean(int value);
void json_object_object_add(json_object *object, const char *key,
	json_object *value);
void json_object_array_add(json_object *array, json_object *value);
int json_object_array_length(json_object *array);
json_object *json_object_array_get_idx(json_object *array, int index);
json_object *json_object_put(json_object *object);
const char *json_object_to_json_string(json_object *object);
const char *json_object_get_string(json_object *object);
int json_object_get_int(json_object *object);
json_object *json_object_get(json_object *object);
json_object *json_tokener_parse(const char *text);
enum json_type json_object_get_type(json_object *object);
void json_object_object_del(json_object *object, const char *key);

#endif
