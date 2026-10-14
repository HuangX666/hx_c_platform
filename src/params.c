#include "internal.h"
#include "hx_os_params.h"

typedef struct hx_os_param_entry {
	union {
		int64_t integer;
		uint64_t unsigned_integer;
		double real;
		void *pointer;
	} scalar;

	char *storage;
	size_t value_bytes, capacity;
	uint16_t name_bytes, kind;
} hx_os_param_entry;

struct hx_os_params {
	hx_os_context *context;
	hx_os_param_entry *entries;
	size_t count, capacity, bytes, max_entries, max_bytes;
	int busy;
	uint32_t borrows;
};

int hx_os_params_available(void)
{
	return HX_OS_ENABLE_PARAMETERS;
}

hx_os_status hx_os_params_options_init(
	hx_os_params_options *options, size_t capacity)
{
	const hx_os_params_options defaults = HX_OS_PARAMS_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;
	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;
	*options = defaults;

	return HX_OS_OK;
}

static hx_os_status hx_os_params_ready(
	const hx_os_params *params, hx_os_error *error)
{
	if (!HX_OS_ENABLE_PARAMETERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!params)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (params->busy)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	return HX_OS_OK;
}

hx_os_status hx_os_i_params_hold(hx_os_params *params, hx_os_error *error)
{
	hx_os_status status = hx_os_params_ready(params, error);

	if (status != HX_OS_OK)
		return status;
	if (params->borrows == UINT32_MAX)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	++params->borrows;

	return HX_OS_OK;
}

void hx_os_i_params_release(hx_os_params *params)
{
	--params->borrows;
}

hx_os_status hx_os_i_parameter_name(
	const char *name, size_t *bytes, hx_os_error *error)
{
	size_t length = 0, invalid;
	hx_os_status status;

	if (!name || !name[0])
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	while (length <= 127 && name[length])
		++length;
	if (length > 127)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_text_validate_utf8(name, length, &invalid);
	if (status == HX_OS_OK)
		*bytes = length;

	return hx_os_i_error(error, status, 0, 0);
}

static size_t hx_os_params_find(const hx_os_params *params, const char *name)
{
	size_t first = 0, last = params->count;

	while (first != last) {
		size_t middle = first + (last - first) / 2;

		if (strcmp(params->entries[middle].storage, name) < 0)
			first = middle + 1;
		else
			last = middle;
	}

	return first;
}

static hx_os_param_value hx_os_params_value(const hx_os_param_entry *entry)
{
	hx_os_param_value value;

	memset(&value, 0, sizeof(value));
	value.kind = entry->kind;
	switch (entry->kind) {
	case HX_OS_PARAM_BOOL:
	case HX_OS_PARAM_I64:
		value.data.integer = entry->scalar.integer;
		break;
	case HX_OS_PARAM_U64:
		value.data.unsigned_integer = entry->scalar.unsigned_integer;
		break;
	case HX_OS_PARAM_DOUBLE:
		value.data.real = entry->scalar.real;
		break;
	case HX_OS_PARAM_POINTER:
		value.data.pointer = entry->scalar.pointer;
		break;
	case HX_OS_PARAM_STRING:
		value.data.string.data = entry->storage + entry->name_bytes + 1;
		value.data.string.size = entry->value_bytes;
		break;
	case HX_OS_PARAM_BYTES:
		value.data.bytes.data = entry->storage + entry->name_bytes + 1;
		value.data.bytes.size = entry->value_bytes;
		break;
	default:
		break;
	}

	return value;
}

hx_os_status hx_os_params_create(
	hx_os_context *context, hx_os_params **out, hx_os_error *error)
{
	return hx_os_params_create_with_options(context, NULL, out, error);
}

hx_os_status hx_os_params_create_with_options(hx_os_context *context,
	const hx_os_params_options *options, hx_os_params **out,
	hx_os_error *error)
{
	const hx_os_params_options defaults = HX_OS_PARAMS_OPTIONS_INIT;
	hx_os_params *params;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_PARAMETERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!options)
		options = &defaults;
	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_entries || options->max_entries > 65535 ||
		!options->max_bytes || options->max_bytes > 64u * 1024u * 1024u)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	params = hx_os_i_zero(context, sizeof(*params));
	if (!params)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	params->context = context;
	params->max_entries = options->max_entries;
	params->max_bytes = options->max_bytes;
	*out = params;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_params_grow(hx_os_params *params)
{
	hx_os_param_entry *entries;
	size_t capacity;

	if (params->count < params->capacity)
		return HX_OS_OK;
	capacity = params->capacity ? params->capacity * 2 : 8;
	if (capacity > params->max_entries)
		capacity = params->max_entries;
	entries =
		hx_os_i_alloc(params->context, capacity * sizeof(*entries), 0);
	if (!entries)
		return HX_OS_NO_MEMORY;
	if (params->count)
		memcpy(entries, params->entries,
			params->count * sizeof(*entries));
	hx_os_memory_free(params->entries);
	params->entries = entries;
	params->capacity = capacity;

	return HX_OS_OK;
}

hx_os_status hx_os_params_set(hx_os_params *params, const char *name,
	hx_os_param_value value, hx_os_error *error)
{
	hx_os_param_entry *entry;
	size_t name_bytes, needed, index, previous = 0, invalid;
	hx_os_bytes_view input = {NULL, 0};
	char *storage;
	int found, span;
	hx_os_status status = hx_os_params_ready(params, error);

	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_parameter_name(name, &name_bytes, error);
	if (status != HX_OS_OK)
		return status;
	if (value.kind < HX_OS_PARAM_BOOL || value.kind > HX_OS_PARAM_U64 ||
		(value.kind == HX_OS_PARAM_BOOL && value.data.integer != 0 &&
			value.data.integer != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	span = value.kind == HX_OS_PARAM_STRING ||
		value.kind == HX_OS_PARAM_BYTES;
	if (value.kind == HX_OS_PARAM_STRING) {
		input.data = value.data.string.data;
		input.size = value.data.string.size;
	}
	else if (value.kind == HX_OS_PARAM_BYTES)
		input = value.data.bytes;
	if (!input.data && input.size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	needed = name_bytes + 1;
	if (span &&
		(!hx_os_i_add_size(needed, input.size, &needed) ||
			!hx_os_i_add_size(needed, 1, &needed)))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (needed > params->max_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (value.kind == HX_OS_PARAM_STRING) {
		status = hx_os_text_validate_utf8(
			input.data, input.size, &invalid);
		if (status != HX_OS_OK)
			return hx_os_i_error(error, status, 0, 0);
	}

	index = hx_os_params_find(params, name);
	found = index < params->count &&
		!strcmp(params->entries[index].storage, name);
	if (found)
		previous = params->entries[index].capacity;
	else if (params->count == params->max_entries)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (needed > previous &&
		needed > params->max_bytes - (params->bytes - previous))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	params->busy = 1;
	storage = found ? params->entries[index].storage : NULL;
	if (needed > previous) {
		storage = hx_os_i_alloc(params->context, needed, 0);
		if (!storage) {
			status = HX_OS_NO_MEMORY;
			goto done;
		}
		memcpy(storage, name, name_bytes + 1);
	}
	if (span) {
		if (input.size)
			memmove(storage + name_bytes + 1, input.data,
				input.size);
		storage[name_bytes + 1 + input.size] = 0;
	}

	if (!found) {
		status = hx_os_params_grow(params);
		if (status != HX_OS_OK) {
			hx_os_memory_free(storage);
			goto done;
		}
		memmove(params->entries + index + 1, params->entries + index,
			(params->count - index) * sizeof(*entry));
		memset(params->entries + index, 0, sizeof(*entry));
		++params->count;
	}
	entry = params->entries + index;
	if (needed > previous) {
		hx_os_memory_free(entry->storage);
		entry->storage = storage;
		entry->capacity = needed;
		params->bytes = params->bytes - previous + needed;
	}
	entry->name_bytes = (uint16_t)name_bytes;
	entry->value_bytes = input.size;
	entry->kind = (uint16_t)value.kind;
	switch (value.kind) {
	case HX_OS_PARAM_BOOL:
	case HX_OS_PARAM_I64:
		entry->scalar.integer = value.data.integer;
		break;
	case HX_OS_PARAM_U64:
		entry->scalar.unsigned_integer = value.data.unsigned_integer;
		break;
	case HX_OS_PARAM_DOUBLE:
		entry->scalar.real = value.data.real;
		break;
	case HX_OS_PARAM_POINTER:
		entry->scalar.pointer = value.data.pointer;
		break;
	default:
		break;
	}

done:
	params->busy = 0;

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_params_get(const hx_os_params *params, const char *name,
	hx_os_param_value *out, hx_os_error *error)
{
	size_t bytes, index;
	hx_os_status status = hx_os_params_ready(params, error);

	if (status != HX_OS_OK)
		return status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_parameter_name(name, &bytes, error);
	if (status != HX_OS_OK)
		return status;
	index = hx_os_params_find(params, name);
	if (index == params->count ||
		strcmp(params->entries[index].storage, name))
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	*out = hx_os_params_value(params->entries + index);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_params_at(const hx_os_params *params, size_t index,
	hx_string_view *name, hx_os_param_value *out, hx_os_error *error)
{
	hx_os_status status = hx_os_params_ready(params, error);

	if (status != HX_OS_OK)
		return status;
	if (!name || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (index >= params->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	name->data = params->entries[index].storage;
	name->size = params->entries[index].name_bytes;
	*out = hx_os_params_value(params->entries + index);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_params_count(const hx_os_params *params)
{
	return params && !params->busy ? params->count : 0;
}

size_t hx_os_params_storage_bytes(const hx_os_params *params)
{
	return params && !params->busy ? params->bytes : 0;
}

hx_os_status hx_os_params_remove(
	hx_os_params *params, const char *name, hx_os_error *error)
{
	size_t bytes, index;
	char *storage;
	hx_os_status status = hx_os_params_ready(params, error);

	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_parameter_name(name, &bytes, error);
	if (status != HX_OS_OK)
		return status;
	index = hx_os_params_find(params, name);
	if (index == params->count ||
		strcmp(params->entries[index].storage, name))
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);

	params->busy = 1;
	storage = params->entries[index].storage;
	params->bytes -= params->entries[index].capacity;
	--params->count;
	memmove(params->entries + index, params->entries + index + 1,
		(params->count - index) * sizeof(*params->entries));
	hx_os_memory_free(storage);
	params->busy = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_os_params_dispose(hx_os_params *params)
{
	size_t i;

	for (i = 0; i < params->count; ++i)
		hx_os_memory_free(params->entries[i].storage);
	hx_os_memory_free(params->entries);
	params->entries = NULL;
	params->count = params->capacity = params->bytes = 0;
}

hx_os_status hx_os_params_clear(hx_os_params *params, hx_os_error *error)
{
	hx_os_status status = hx_os_params_ready(params, error);

	if (status != HX_OS_OK)
		return status;
	params->busy = 1;
	hx_os_params_dispose(params);
	params->busy = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_params_destroy(hx_os_params **owner, hx_os_error *error)
{
	hx_os_status status;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*owner)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_params_ready(*owner, error);
	if (status != HX_OS_OK)
		return status;
	if ((*owner)->borrows)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	(*owner)->busy = 1;
	hx_os_params_dispose(*owner);
	hx_os_memory_free(*owner);
	*owner = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_params_clone(const hx_os_params *source,
	hx_os_context *context, hx_os_params **out, hx_os_error *error)
{
	hx_os_params_options options = HX_OS_PARAMS_OPTIONS_INIT;
	hx_os_params *copy = NULL;
	hx_os_status status;
	size_t i;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_params_ready(source, error);
	if (status != HX_OS_OK)
		return status;

	// Opaque heap storage is mutable; guard allocator reentry during copy.
	((hx_os_params *)source)->busy = 1;
	options.max_entries = source->max_entries;
	options.max_bytes = source->max_bytes;
	status = hx_os_params_create_with_options(
		context, &options, &copy, error);
	for (i = 0; status == HX_OS_OK && i < source->count; ++i)
		status = hx_os_params_set(copy, source->entries[i].storage,
			hx_os_params_value(source->entries + i), error);
	if (status != HX_OS_OK)
		(void)hx_os_params_destroy(&copy, NULL);
	((hx_os_params *)source)->busy = 0;
	*out = copy;

	return status;
}

hx_os_status hx_os_params_set_bool(
	hx_os_params *params, const char *name, int value, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_BOOL;
	stored.data.integer = value;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_bool(const hx_os_params *params, const char *name,
	int *out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_BOOL)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = (int)stored.data.integer;

	return status;
}

hx_os_status hx_os_params_set_i64(hx_os_params *params, const char *name,
	int64_t value, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_I64;
	stored.data.integer = value;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_i64(const hx_os_params *params, const char *name,
	int64_t *out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_I64)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = stored.data.integer;

	return status;
}

hx_os_status hx_os_params_set_u64(hx_os_params *params, const char *name,
	uint64_t value, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_U64;
	stored.data.unsigned_integer = value;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_u64(const hx_os_params *params, const char *name,
	uint64_t *out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_U64)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = stored.data.unsigned_integer;

	return status;
}

hx_os_status hx_os_params_set_double(hx_os_params *params, const char *name,
	double value, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_DOUBLE;
	stored.data.real = value;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_double(const hx_os_params *params,
	const char *name, double *out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_DOUBLE)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = stored.data.real;

	return status;
}

hx_os_status hx_os_params_set_pointer(
	hx_os_params *params, const char *name, void *value, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_POINTER;
	stored.data.pointer = value;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_pointer(const hx_os_params *params,
	const char *name, void **out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_POINTER)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = stored.data.pointer;

	return status;
}

hx_os_status hx_os_params_set_string(hx_os_params *params, const char *name,
	hx_string_view value, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_STRING;
	stored.data.string = value;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_string(const hx_os_params *params,
	const char *name, hx_string_view *out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_STRING)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = stored.data.string;

	return status;
}

hx_os_status hx_os_params_set_bytes(hx_os_params *params, const char *name,
	const void *data, size_t size, hx_os_error *error)
{
	hx_os_param_value stored;

	memset(&stored, 0, sizeof(stored));
	stored.kind = HX_OS_PARAM_BYTES;
	stored.data.bytes.data = data;
	stored.data.bytes.size = size;

	return hx_os_params_set(params, name, stored, error);
}

hx_os_status hx_os_params_get_bytes(const hx_os_params *params,
	const char *name, hx_os_bytes_view *out, hx_os_error *error)
{
	hx_os_param_value stored;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_params_get(params, name, &stored, error);
	if (status == HX_OS_OK && stored.kind != HX_OS_PARAM_BYTES)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (status == HX_OS_OK)
		*out = stored.data.bytes;

	return status;
}

hx_os_status hx_os_params_set_cstr(hx_os_params *params, const char *name,
	const char *value, hx_os_error *error)
{
	if (!value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_params_set_string(
		params, name, hx_string_view_from_cstr(value), error);
}

hx_os_params_edit hx_os_params_edit_begin(hx_os_params *params)
{
	hx_os_params_edit edit;

	memset(&edit, 0, sizeof(edit));
	edit.params = params;
	(void)hx_os_params_ready(params, &edit.error);

	return edit;
}

hx_os_status hx_os_params_edit_finish(
	const hx_os_params_edit *edit, hx_os_error *error)
{
	if (!edit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (error)
		*error = edit->error;

	return edit->error.status;
}

void hx_os_params_edit_set_bool(
	hx_os_params_edit *edit, const char *name, int value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_bool(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_i64(
	hx_os_params_edit *edit, const char *name, int64_t value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_i64(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_u64(
	hx_os_params_edit *edit, const char *name, uint64_t value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_u64(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_double(
	hx_os_params_edit *edit, const char *name, double value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_double(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_pointer(
	hx_os_params_edit *edit, const char *name, void *value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_pointer(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_string(
	hx_os_params_edit *edit, const char *name, hx_string_view value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_string(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_cstr(
	hx_os_params_edit *edit, const char *name, const char *value)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_cstr(
			edit->params, name, value, &edit->error);
}

void hx_os_params_edit_set_bytes(hx_os_params_edit *edit, const char *name,
	const void *data, size_t size)
{
	if (edit && edit->error.status == HX_OS_OK)
		(void)hx_os_params_set_bytes(
			edit->params, name, data, size, &edit->error);
}
