#ifndef HX_C_PLATFORM_CONFIGURATION_H
#define HX_C_PLATFORM_CONFIGURATION_H

#include "hx_os_base.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_config_document hx_os_config_document;
typedef struct hx_os_config_node hx_os_config_node;

enum {
	HX_OS_CONFIG_JSON,
	HX_OS_CONFIG_YAML,
	HX_OS_CONFIG_XML,
	HX_OS_CONFIG_INI,
	HX_OS_CONFIG_TOML,
	HX_OS_CONFIG_ENV,
	HX_OS_CONFIG_PROPERTIES
};
enum {
	HX_OS_CONFIG_BUILTIN,
	HX_OS_CONFIG_CJSON,
	HX_OS_CONFIG_LIBYAML,
	HX_OS_CONFIG_MINIXML,
	HX_OS_CONFIG_INIH,
	HX_OS_CONFIG_TOMLC
};
enum {
	HX_OS_CONFIG_NULL,
	HX_OS_CONFIG_BOOL,
	HX_OS_CONFIG_INTEGER,
	HX_OS_CONFIG_REAL,
	HX_OS_CONFIG_STRING,
	HX_OS_CONFIG_OBJECT,
	HX_OS_CONFIG_ARRAY,
	HX_OS_CONFIG_ELEMENT,
	HX_OS_CONFIG_TEXT,
	HX_OS_CONFIG_ATTRIBUTE,
	HX_OS_CONFIG_DATETIME
};
enum {
	HX_OS_CONFIG_DUPLICATE_ERROR,
	HX_OS_CONFIG_DUPLICATE_FIRST,
	HX_OS_CONFIG_DUPLICATE_LAST
};

typedef struct hx_os_config_slice {
	const char *data;
	size_t bytes;
} hx_os_config_slice;

typedef struct hx_os_config_options {
	uint32_t size;
	uint32_t version;
	int backend;
	int duplicates;
	size_t max_input_bytes;
	size_t max_memory_bytes;
	size_t max_nodes;
	size_t max_depth;
	size_t max_work;
	const char *source_name;
} hx_os_config_options;

/*
 * Initialize size/version and nonzero limits with options_init. Depth counts
 * the root as one and is capped at 128. Memory bounds parsing allocations,
 * including native parser allocations, but not caller/file-read buffers or
 * C-library locale objects. Work units are not a native instruction budget.
 * source_name is a borrowed UTF-8 label for this call; error copies it.
 */

/*
 * Input is UTF-8. A leading UTF-8 BOM is accepted. No encoding guessing,
 * environment mutation, variable expansion, file inclusion or external
 * entity loading occurs. Decode other encodings explicitly before parsing.
 * Positions are relative to UTF-8 input; lines/columns are one-based Unicode
 * codepoint positions (a tab counts as one). Unknown positions use SIZE_MAX
 * for byte_offset and zero for line/column. The copied source label may be
 * truncated at a UTF-8 boundary. Messages never contain configuration values.
 */
typedef struct hx_os_config_error {
	hx_os_error detail;
	size_t byte_offset;
	size_t line;
	size_t column;
	int source_truncated;
	char source[256];
	char message[192];
} hx_os_config_error;

HX_OS_API void HX_OS_CALL hx_os_config_options_init(
	hx_os_config_options *options);
HX_OS_API int HX_OS_CALL hx_os_config_backend_available(
	int format, int backend);
HX_OS_API const char *HX_OS_CALL hx_os_config_format_name(int format);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_parse(hx_os_context *context,
	int format, const void *input, size_t bytes,
	const hx_os_config_options *options, hx_os_config_document **out,
	hx_os_config_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_load(hx_os_context *context,
	int format, const char *utf8_path, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_config_error *error);
/*
 * Serialize JSON, INI or properties into the output string's context.
 * Failure preserves its old value. Options default to finite parser limits:
 * max_input_bytes bounds output, max_memory_bytes bounds new output storage
 * including allocator overhead, max_work bounds both passes, depth/nodes bound
 * each traversal. Recursion is bounded to 128 frames. Existing output/document
 * memory is outside this new-storage budget. backend must be BUILTIN.
 * Unrepresentable values fail; no numeric-to-string coercion/flattening.
 * INI global keys precede sections; order within groups is preserved.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_serialize(
	const hx_os_config_document *document, int format, hx_string *out,
	const hx_os_config_options *options, hx_os_error *error);
/* Serialize then atomically publish using file_save_atomic. Needs FILE_UTILS.
 * Same publication/durability boundaries; no automatic backup or rollback.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_save(
	const hx_os_config_document *document, int format, const char *path,
	const hx_os_config_options *options, hx_os_error *error);
/*
 * Documents are immutable after successful parse and may be read concurrently.
 * Nodes/slices borrow the document. Retain it before sharing, and exclude
 * release of the last reference while any query or borrowed access is active.
 * Failed parse publishes no document; release(NULL) is allowed.
 */
HX_OS_API void HX_OS_CALL hx_os_config_retain(hx_os_config_document *document);
HX_OS_API void HX_OS_CALL hx_os_config_release(hx_os_config_document *document);
HX_OS_API const hx_os_config_node *HX_OS_CALL hx_os_config_root(
	const hx_os_config_document *document);
HX_OS_API int HX_OS_CALL hx_os_config_kind(const hx_os_config_node *node);
HX_OS_API hx_os_config_slice HX_OS_CALL hx_os_config_name(
	const hx_os_config_node *node);
HX_OS_API size_t HX_OS_CALL hx_os_config_count(const hx_os_config_node *node);
HX_OS_API const hx_os_config_node *HX_OS_CALL hx_os_config_first(
	const hx_os_config_node *node);
HX_OS_API const hx_os_config_node *HX_OS_CALL hx_os_config_next(
	const hx_os_config_node *node);
HX_OS_API const hx_os_config_node *HX_OS_CALL hx_os_config_at(
	const hx_os_config_node *node, size_t index);
/* Exact, case-sensitive byte keys; NUL inside a key is representable. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_member(
	const hx_os_config_node *object, const char *key, size_t bytes,
	const hx_os_config_node **out);
/* XML children remain ordered; repeated elements and text stay distinct. */
HX_OS_API const hx_os_config_node *HX_OS_CALL hx_os_config_attributes(
	const hx_os_config_node *element);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_attribute(
	const hx_os_config_node *element, const char *name,
	const hx_os_config_node **out);
/* RFC 6901 lookup on objects/arrays. Empty path selects the supplied root. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_pointer(
	const hx_os_config_node *root, const char *utf8_pointer,
	const hx_os_config_node **out);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_get_string(
	const hx_os_config_node *node, hx_os_config_slice *out);
/* Numbers retain their lexical spelling, without double rounding. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_scalar_text(
	const hx_os_config_node *node, hx_os_config_slice *out);
/*
 * Numeric getters accept numeric nodes or complete numeric strings.
 * Integer conversion never truncates a fraction. Bool accepts true/false
 * only. A missing node reports NOT_FOUND; an incompatible type reports
 * TYPE_MISMATCH. No process locale is modified.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_get_i64(
	const hx_os_config_node *node, int64_t *out);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_get_u64(
	const hx_os_config_node *node, uint64_t *out);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_get_double(
	const hx_os_config_node *node, double *out);
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_get_bool(
	const hx_os_config_node *node, int *out);

#ifdef __cplusplus
}
#endif
#endif
