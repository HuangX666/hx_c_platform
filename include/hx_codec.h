#ifndef HX_C_PLATFORM_CODEC_H
#define HX_C_PLATFORM_CODEC_H

#include "hx_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_codec_schema hx_os_codec_schema;
typedef struct hx_os_codec_type hx_os_codec_type;
typedef struct hx_os_codec_result hx_os_codec_result;

/* ARRAY is a borrowed contiguous span on encode and owner-borrowed on decode.
 * STRING uses hx_string_view; BUFFER uses an inline, NUL-terminated char array.
 * NODE uses const hx_os_config_node * and preserves heterogeneous subtrees.
 * BOOL uses int or a one-byte bool. All other scalar widths are exact.
 */
enum {
	HX_OS_CODEC_BOOL,
	HX_OS_CODEC_I8,
	HX_OS_CODEC_U8,
	HX_OS_CODEC_I16,
	HX_OS_CODEC_U16,
	HX_OS_CODEC_I32,
	HX_OS_CODEC_U32,
	HX_OS_CODEC_I64,
	HX_OS_CODEC_U64,
	HX_OS_CODEC_FLOAT,
	HX_OS_CODEC_DOUBLE,
	HX_OS_CODEC_STRING,
	HX_OS_CODEC_BUFFER,
	HX_OS_CODEC_OBJECT,
	HX_OS_CODEC_ARRAY,
	HX_OS_CODEC_FIXED_ARRAY,
	HX_OS_CODEC_NODE
};
enum { HX_OS_CODEC_OPTIONAL = 1, HX_OS_CODEC_XML_ATTRIBUTE = 2 };

typedef struct hx_os_codec_array {
	const void *data;
	size_t count;
} hx_os_codec_array;

struct hx_os_codec_type {
	const hx_os_codec_schema *schema;
	const hx_os_codec_type *element;
	size_t size;
	size_t count;
	int kind;
};

typedef struct hx_os_codec_field {
	const char *name;
	hx_os_codec_type type;
	size_t offset;
	unsigned flags;
} hx_os_codec_field;

/* Borrowed for the call; no registration or global registry. C-compatible,
 * acyclic value layouts only. Do not describe owning/nontrivial C++ members,
 * pointers to arbitrary objects, unions, packed or over-aligned structures.
 * Unknown input members are ignored. Missing required fields fail; OPTIONAL
 * fields remain zero/empty. Null is a real NODE value, never scalar zero.
 */
struct hx_os_codec_schema {
	const hx_os_codec_field *fields;
	const char *xml_name;
	size_t size;
	size_t count;
};

#define HX_OS_CODEC_TYPE(type, kind)                                           \
	{                                                                      \
		NULL, NULL, sizeof(type), 0, kind                              \
	}
#define HX_OS_CODEC_FIELD(type, member, kind)                                  \
	{                                                                      \
		#member, {NULL, NULL, sizeof(((type *)0)->member), 0, kind},   \
			offsetof(type, member), 0                              \
	}
#define HX_OS_CODEC_OBJECT_FIELD(type, member, schema)                         \
	{                                                                      \
		#member,                                                       \
			{schema, NULL, sizeof(((type *)0)->member), 0,         \
				HX_OS_CODEC_OBJECT},                           \
			offsetof(type, member), 0                              \
	}
#define HX_OS_CODEC_ARRAY_FIELD(type, member, element)                         \
	{                                                                      \
		#member,                                                       \
			{NULL, element, sizeof(((type *)0)->member), 0,        \
				HX_OS_CODEC_ARRAY},                            \
			offsetof(type, member), 0                              \
	}
#define HX_OS_CODEC_FIXED_FIELD(type, member, element)                         \
	{                                                                      \
		#member,                                                       \
			{NULL, element, sizeof(((type *)0)->member),           \
				sizeof(((type *)0)->member) /                  \
					sizeof(((type *)0)->member[0]),        \
				HX_OS_CODEC_FIXED_ARRAY},                      \
			offsetof(type, member), 0                              \
	}
#define HX_OS_CODEC_SCHEMA(type, fields, xml_name)                             \
	{                                                                      \
		fields, xml_name, sizeof(type),                                \
			sizeof(fields) / sizeof((fields)[0])                   \
	}

/* Bounded by config options. Parse allocations and decoded storage share
 * max_memory_bytes; encoding tree and output each use that bound separately.
 * Success copies/owns all data. No partially decoded object is published.
 * Strings/arrays/NODE pointers from data() borrow result until release().
 * Returned data is read-only and may be read concurrently while kept alive.
 * Encode borrows input for the call; failure preserves the output string.
 * XML maps objects to elements, arrays to repeated <item> children, scalar
 * fields to text (or attributes when explicitly flagged). Mixed XML content
 * is available via NODE, not converted implicitly to scalar fields.
 * INI allows one section level; ENV/properties are flat. These text formats
 * and XML convert scalars according to the declared schema. JSON/YAML/TOML
 * check native node types. Unrepresentable shapes fail, never flatten.
 */
HX_OS_API int HX_OS_CALL hx_os_codec_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_codec_encode(
	const hx_os_codec_schema *schema, const void *object, int format,
	hx_string *out, const hx_os_config_options *options,
	hx_os_config_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_codec_decode(hx_os_context *context,
	const hx_os_codec_schema *schema, int format, const void *text,
	size_t bytes, const hx_os_config_options *options,
	hx_os_codec_result **out, hx_os_config_error *error);
HX_OS_API const void *HX_OS_CALL hx_os_codec_data(
	const hx_os_codec_result *result);
HX_OS_API void HX_OS_CALL hx_os_codec_release(hx_os_codec_result *result);
HX_OS_API hx_os_status HX_OS_CALL hx_os_codec_save(
	const hx_os_codec_schema *schema, const void *object, int format,
	const char *path, const hx_os_config_options *options,
	hx_os_config_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_codec_load(hx_os_context *context,
	const hx_os_codec_schema *schema, int format, const char *path,
	const hx_os_config_options *options, hx_os_codec_result **out,
	hx_os_config_error *error);

#ifdef __cplusplus
}
#endif
#endif
