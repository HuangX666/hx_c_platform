#ifndef HX_C_PLATFORM_SETTINGS_PRIVATE_H
#define HX_C_PLATFORM_SETTINGS_PRIVATE_H

#include "internal.h"
#include "hx_settings.h"

typedef union hx_settings_scalar {
	int64_t integer;
	double real;
	void *pointer;
} hx_settings_scalar;

struct hx_settings_value {
	hx_os_atomic references;
	size_t memory;
	size_t nodes;
	size_t depth;
	size_t count;
	hx_settings_scalar scalar;
	int kind;
};

typedef struct hx_settings_entry {
	hx_string_view name;
	hx_settings_value *value;
} hx_settings_entry;

typedef struct hx_settings_limits {
	size_t nodes;
	size_t memory;
	size_t depth;
} hx_settings_limits;
#define HX_SETTINGS_HARD_LIMITS                                                \
	{                                                                      \
		65536, 16 * 1024 * 1024, 64                                    \
	}
struct hx_settings {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_settings_value *roots[2];
	hx_settings_options options;
	uint64_t revision;
	size_t operations;
	int closing;
};

typedef struct hx_settings_frame {
	struct hx_settings_frame *previous;
	hx_settings *settings;
	hx_settings_value *roots[2];
	uint64_t revision;
	int notifying;
} hx_settings_frame;

hx_os_status hx_settings_i_key(hx_string_view key, hx_os_error *error);

hx_os_status hx_settings_i_scalar(hx_os_context *context, int kind,
	hx_settings_scalar scalar, hx_string_view text,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error);

hx_os_status hx_settings_i_clone(hx_os_context *context,
	const hx_settings_value *source, const hx_settings_limits *limits,
	hx_settings_value **out, hx_os_error *error);

hx_os_status hx_settings_i_array(hx_os_context *context,
	const hx_settings_value *const *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error);

hx_os_status hx_settings_i_object(hx_os_context *context,
	const hx_settings_pair *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error);

const hx_settings_value *hx_settings_i_member(
	const hx_settings_value *object, hx_string_view name);

const hx_settings_entry *hx_settings_i_find(
	const hx_settings_value *object, hx_string_view name);

/* Object replacement/merge retain immutable same-context children. */
hx_os_status hx_settings_i_replace(hx_os_context *context,
	const hx_settings_value *object, hx_string_view key,
	hx_settings_value *value, const hx_settings_limits *limits,
	hx_settings_value **out, hx_os_error *error);

hx_os_status hx_settings_i_merge(hx_os_context *context,
	const hx_settings_value *values, const hx_settings_value *defaults,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error);

hx_os_status hx_settings_i_begin(
	hx_settings *settings, hx_settings_frame *frame, hx_os_error *error);

void hx_settings_i_end(hx_settings_frame *frame);

hx_settings_limits hx_settings_i_limits(const hx_settings *settings);

hx_os_status hx_settings_i_array_retain(hx_os_context *context,
	const hx_settings_value *const *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error);

hx_os_status hx_settings_i_object_retain(hx_os_context *context,
	const hx_settings_pair *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error);

hx_os_status hx_settings_i_commit_root(
	hx_settings_frame *frame, hx_settings_value *root, hx_os_error *error);

hx_os_status hx_settings_i_import(hx_settings_frame *frame,
	const hx_os_config_document *document, hx_os_error *error);

hx_os_status hx_settings_i_document(hx_os_context *context,
	const hx_settings_value *value, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_error *error);
#endif
