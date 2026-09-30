#include "settings_private.h"
#include <float.h>
static int hx_settings_compare_keys(hx_string_view left, hx_string_view right)
{
	size_t count = left.size < right.size ? left.size : right.size;
	int order = count ? memcmp(left.data, right.data, count) : 0;
	return order ? order
		     : (left.size > right.size) - (left.size < right.size);
}
static int hx_settings_compare_entries(const void *left, const void *right)
{
	return hx_settings_compare_keys(((const hx_settings_entry *)left)->name,
		((const hx_settings_entry *)right)->name);
}
hx_os_status hx_settings_i_key(hx_string_view key, hx_os_error *error)
{
	size_t invalid;
	hx_os_status status;
	if (!key.data && key.size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (key.size > 16u * 1024u * 1024u)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_text_validate_utf8(key.data, key.size, &invalid);
	return hx_os_i_error(error, status, 0, 0);
}
void hx_settings_value_retain(hx_settings_value *value)
{
	if (value)
		(void)hx_os_atomic_fetch_add(
			&value->references, 1, HX_OS_RELAXED);
}
void hx_settings_value_release(hx_settings_value *value)
{
	size_t i;
	if (!value ||
		hx_os_atomic_fetch_add(&value->references, -1, HX_OS_ACQ_REL) !=
			1)
		return;
	if (value->kind == HX_SETTINGS_ARRAY) {
		hx_settings_value **items = (hx_settings_value **)(value + 1);
		for (i = 0; i < value->count; ++i)
			hx_settings_value_release(items[i]);
	}
	else if (value->kind == HX_SETTINGS_OBJECT) {
		hx_settings_entry *items = (hx_settings_entry *)(value + 1);
		for (i = 0; i < value->count; ++i)
			hx_settings_value_release(items[i].value);
	}
	hx_os_memory_free(value);
}
int hx_settings_value_kind(const hx_settings_value *value)
{
	return value ? value->kind : -1;
}
size_t hx_settings_value_count(const hx_settings_value *value)
{
	return value &&
			(value->kind == HX_SETTINGS_ARRAY ||
				value->kind == HX_SETTINGS_OBJECT)
		? value->count
		: 0;
}
static hx_settings_value *hx_settings_allocate(hx_os_context *context, int kind,
	size_t bytes, size_t count, size_t memory, size_t nodes, size_t depth)
{
	hx_settings_value *value = hx_os_i_zero(context, bytes);
	if (!value)
		return NULL;
	value->references.value = 1;
	value->kind = kind;
	value->count = count;
	value->memory = memory;
	value->nodes = nodes;
	value->depth = depth;
	return value;
}
hx_os_status hx_settings_i_scalar(hx_os_context *context, int kind,
	hx_settings_scalar scalar, hx_string_view text,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	size_t bytes = sizeof(hx_settings_value), cost;
	hx_os_status status;
	hx_settings_value *value;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (kind < HX_SETTINGS_NULL || kind > HX_SETTINGS_POINTER ||
		kind == HX_SETTINGS_ARRAY || kind == HX_SETTINGS_OBJECT ||
		(kind == HX_SETTINGS_BOOL && scalar.integer != 0 &&
			scalar.integer != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (kind == HX_SETTINGS_DOUBLE &&
		!(scalar.real <= DBL_MAX && scalar.real >= -DBL_MAX))
		return hx_os_i_error(error, HX_OS_UNREPRESENTABLE, 0, 0);
	if (kind == HX_SETTINGS_STRING) {
		status = hx_settings_i_key(text, error);
		if (status != HX_OS_OK)
			return status;
		if (!hx_os_i_add_size(bytes, text.size, &bytes) ||
			!hx_os_i_add_size(bytes, 1, &bytes))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	cost = hx_os_i_allocation_cost(bytes, 0);
	if (!cost || cost > limits->memory || !limits->nodes || !limits->depth)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	value = hx_settings_allocate(context, kind, bytes,
		kind == HX_SETTINGS_STRING ? text.size : 0, cost, 1, 1);
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	value->scalar = scalar;
	if (kind == HX_SETTINGS_STRING && text.size)
		memcpy(value + 1, text.data, text.size);
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
static int hx_settings_add_metadata(const hx_settings_value *child,
	const hx_settings_limits *limits, size_t *memory, size_t *nodes,
	size_t *depth)
{
	if (child->memory > limits->memory - *memory ||
		child->nodes > limits->nodes - *nodes ||
		child->depth >= limits->depth)
		return 0;
	*memory += child->memory;
	*nodes += child->nodes;
	if (*depth <= child->depth)
		*depth = child->depth + 1;
	return 1;
}
static hx_os_status hx_settings_array_build(hx_os_context *context,
	const hx_settings_value *const *items, size_t count, int copy,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	hx_settings_value *value;
	hx_settings_value **children;
	size_t i, bytes, memory, nodes = 1, depth = 1;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!items && count)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (count >= limits->nodes || !limits->depth ||
		count > (SIZE_MAX - sizeof(*value)) / sizeof(*children))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	bytes = sizeof(*value) + count * sizeof(*children);
	memory = hx_os_i_allocation_cost(bytes, 0);
	if (!memory || memory > limits->memory)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (i = 0; i < count; ++i) {
		if (!items[i])
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		if (!hx_settings_add_metadata(
			    items[i], limits, &memory, &nodes, &depth))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	value = hx_settings_allocate(
		context, HX_SETTINGS_ARRAY, bytes, count, memory, nodes, depth);
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	children = (hx_settings_value **)(value + 1);
	for (i = 0; i < count; ++i) {
		if (copy)
			status = hx_settings_i_clone(
				context, items[i], limits, &children[i], error);
		else {
			children[i] = (hx_settings_value *)items[i];
			hx_settings_value_retain(children[i]);
			status = HX_OS_OK;
		}
		if (status != HX_OS_OK) {
			hx_settings_value_release(value);
			return status;
		}
	}
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_settings_i_array(hx_os_context *context,
	const hx_settings_value *const *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	return hx_settings_array_build(
		context, items, count, 1, limits, out, error);
}

hx_os_status hx_settings_i_array_retain(hx_os_context *context,
	const hx_settings_value *const *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	return hx_settings_array_build(
		context, items, count, 0, limits, out, error);
}
/* Either public borrowed pairs or private immutable entries are supplied.
 * Retain mode is only used by same-context mutable-root updates/merges.
 */
static hx_os_status hx_settings_object_build(hx_os_context *context,
	const hx_settings_pair *pairs, const hx_settings_entry *existing,
	size_t count, int copy, const hx_settings_limits *limits,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_value *value;
	hx_settings_entry *entries;
	char *keys;
	size_t bytes, memory, nodes = 1, depth = 1, i;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!pairs && !existing && count)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (count >= limits->nodes || !limits->depth ||
		count > (SIZE_MAX - sizeof(*value)) / sizeof(*entries))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	bytes = sizeof(*value) + count * sizeof(*entries);
	for (i = 0; i < count; ++i) {
		hx_string_view key = pairs ? pairs[i].name : existing[i].name;
		const hx_settings_value *child =
			pairs ? pairs[i].value : existing[i].value;
		if (!child)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		status = hx_settings_i_key(key, error);
		if (status != HX_OS_OK)
			return status;
		if (!hx_os_i_add_size(bytes, key.size, &bytes) ||
			!hx_os_i_add_size(bytes, 1, &bytes) ||
			bytes > limits->memory)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	memory = hx_os_i_allocation_cost(bytes, 0);
	if (!memory || memory > limits->memory)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (i = 0; i < count; ++i) {
		const hx_settings_value *child =
			pairs ? pairs[i].value : existing[i].value;
		if (!hx_settings_add_metadata(
			    child, limits, &memory, &nodes, &depth))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	value = hx_settings_allocate(context, HX_SETTINGS_OBJECT, bytes, count,
		memory, nodes, depth);
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	entries = (hx_settings_entry *)(value + 1);
	keys = (char *)(entries + count);
	for (i = 0; i < count; ++i) {
		hx_string_view key = pairs ? pairs[i].name : existing[i].name;
		const hx_settings_value *child =
			pairs ? pairs[i].value : existing[i].value;
		entries[i].name.data = keys;
		entries[i].name.size = key.size;
		if (key.size)
			memcpy(keys, key.data, key.size);
		keys[key.size] = 0;
		keys += key.size + 1;
		if (copy) {
			status = hx_settings_i_clone(context, child, limits,
				&entries[i].value, error);
			if (status != HX_OS_OK) {
				hx_settings_value_release(value);
				return status;
			}
		}
		else {
			entries[i].value = (hx_settings_value *)child;
			hx_settings_value_retain(entries[i].value);
		}
	}
	if (count > 1)
		qsort(entries, count, sizeof(*entries),
			hx_settings_compare_entries);
	for (i = 1; i < count; ++i)
		if (!hx_settings_compare_keys(
			    entries[i - 1].name, entries[i].name)) {
			hx_settings_value_release(value);
			return hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
		}
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_settings_i_object(hx_os_context *context,
	const hx_settings_pair *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	return hx_settings_object_build(
		context, items, NULL, count, 1, limits, out, error);
}
hx_os_status hx_settings_i_object_retain(hx_os_context *context,
	const hx_settings_pair *items, size_t count,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	return hx_settings_object_build(
		context, items, NULL, count, 0, limits, out, error);
}
hx_os_status hx_settings_i_clone(hx_os_context *context,
	const hx_settings_value *source, const hx_settings_limits *limits,
	hx_settings_value **out, hx_os_error *error)
{
	hx_string_view text = {NULL, 0};
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!source)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (source->memory > limits->memory || source->nodes > limits->nodes ||
		source->depth > limits->depth)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (source->kind == HX_SETTINGS_ARRAY)
		return hx_settings_i_array(context,
			(const hx_settings_value *const *)(source + 1),
			source->count, limits, out, error);
	if (source->kind == HX_SETTINGS_OBJECT)
		return hx_settings_object_build(context, NULL,
			(const hx_settings_entry *)(source + 1), source->count,
			1, limits, out, error);
	if (source->kind == HX_SETTINGS_STRING) {
		text.data = (const char *)(source + 1);
		text.size = source->count;
	}
	return hx_settings_i_scalar(context, source->kind, source->scalar, text,
		limits, out, error);
}
const hx_settings_entry *hx_settings_i_find(
	const hx_settings_value *object, hx_string_view name)
{
	const hx_settings_entry *entries =
		(const hx_settings_entry *)(object + 1);
	size_t low = 0, high = object->count;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		int order =
			hx_settings_compare_keys(entries[middle].name, name);
		if (!order)
			return entries + middle;
		if (order < 0)
			low = middle + 1;
		else
			high = middle;
	}
	return NULL;
}
const hx_settings_value *hx_settings_i_member(
	const hx_settings_value *object, hx_string_view name)
{
	const hx_settings_entry *entry = hx_settings_i_find(object, name);
	return entry ? entry->value : NULL;
}
hx_os_status hx_settings_i_replace(hx_os_context *context,
	const hx_settings_value *object, hx_string_view key,
	hx_settings_value *value, const hx_settings_limits *limits,
	hx_settings_value **out, hx_os_error *error)
{
	const hx_settings_entry *entries =
		(const hx_settings_entry *)(object + 1);
	const hx_settings_value *old = hx_settings_i_member(object, key);
	size_t count = object->count + (old ? 0 : 1) - (value ? 0 : 1), i,
	       n = 0;
	hx_settings_pair *pairs;
	hx_os_status status;
	if (!old && !value)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (count >= limits->nodes || count > SIZE_MAX / sizeof(*pairs))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	pairs = count ? hx_os_i_alloc(context, count * sizeof(*pairs), 0)
		      : NULL;
	if (count && !pairs)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	for (i = 0; i < object->count; ++i) {
		if (!hx_settings_compare_keys(entries[i].name, key))
			continue;
		pairs[n].name = entries[i].name;
		pairs[n++].value = entries[i].value;
	}
	if (value) {
		pairs[n].name = key;
		pairs[n++].value = value;
	}
	status = hx_settings_object_build(
		context, pairs, NULL, n, 0, limits, out, error);
	hx_os_memory_free(pairs);
	return status;
}
hx_os_status hx_settings_i_merge(hx_os_context *context,
	const hx_settings_value *values, const hx_settings_value *defaults,
	const hx_settings_limits *limits, hx_settings_value **out,
	hx_os_error *error)
{
	const hx_settings_entry *a = (const hx_settings_entry *)(values + 1);
	const hx_settings_entry *b = (const hx_settings_entry *)(defaults + 1);
	size_t capacity, count = 0, i;
	hx_settings_pair *pairs;
	hx_os_status status;
	if (!hx_os_i_add_size(values->count, defaults->count, &capacity) ||
		capacity > SIZE_MAX / sizeof(*pairs))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	pairs = capacity ? hx_os_i_alloc(context, capacity * sizeof(*pairs), 0)
			 : NULL;
	if (capacity && !pairs)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	for (i = 0; i < values->count; ++i) {
		pairs[count].name = a[i].name;
		pairs[count++].value = a[i].value;
	}
	for (i = 0; i < defaults->count; ++i) {
		if (hx_settings_i_member(values, b[i].name))
			continue;
		pairs[count].name = b[i].name;
		pairs[count++].value = b[i].value;
	}
	status = hx_settings_object_build(
		context, pairs, NULL, count, 0, limits, out, error);
	hx_os_memory_free(pairs);
	return status;
}

static const hx_settings_limits hx_settings_value_limits =
	HX_SETTINGS_HARD_LIMITS;

hx_os_status hx_settings_value_null(
	hx_os_context *context, hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	return hx_settings_i_scalar(context, HX_SETTINGS_NULL, scalar, empty,
		&hx_settings_value_limits, out, error);
}

hx_os_status hx_settings_value_bool(hx_os_context *context, int value,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.integer = value;
	return hx_settings_i_scalar(context, HX_SETTINGS_BOOL, scalar, empty,
		&hx_settings_value_limits, out, error);
}

hx_os_status hx_settings_value_i64(hx_os_context *context, int64_t value,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.integer = value;
	return hx_settings_i_scalar(context, HX_SETTINGS_I64, scalar, empty,
		&hx_settings_value_limits, out, error);
}

hx_os_status hx_settings_value_double(hx_os_context *context, double value,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.real = value;
	return hx_settings_i_scalar(context, HX_SETTINGS_DOUBLE, scalar, empty,
		&hx_settings_value_limits, out, error);
}

hx_os_status hx_settings_value_pointer(hx_os_context *context, void *value,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.pointer = value;
	return hx_settings_i_scalar(context, HX_SETTINGS_POINTER, scalar, empty,
		&hx_settings_value_limits, out, error);
}

hx_os_status hx_settings_value_string(hx_os_context *context,
	hx_string_view value, hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	return hx_settings_i_scalar(context, HX_SETTINGS_STRING, scalar, value,
		&hx_settings_value_limits, out, error);
}
hx_os_status hx_settings_value_array(hx_os_context *context,
	const hx_settings_value *const *items, size_t count,
	hx_settings_value **out, hx_os_error *error)
{
	return hx_settings_i_array(
		context, items, count, &hx_settings_value_limits, out, error);
}
hx_os_status hx_settings_value_object(hx_os_context *context,
	const hx_settings_pair *items, size_t count, hx_settings_value **out,
	hx_os_error *error)
{
	return hx_settings_i_object(
		context, items, count, &hx_settings_value_limits, out, error);
}
hx_os_status hx_settings_value_copy(hx_os_context *context,
	const hx_settings_value *value, hx_settings_value **out,
	hx_os_error *error)
{
	return hx_settings_i_clone(
		context, value, &hx_settings_value_limits, out, error);
}
static hx_os_status hx_settings_check_type(const hx_settings_value *value,
	int kind, const void *out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!value)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (value->kind != kind)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_settings_value_get_bool(
	const hx_settings_value *value, int *out, hx_os_error *error)
{
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_BOOL, out, error);
	if (status == HX_OS_OK)
		*out = (int)value->scalar.integer;
	return status;
}

hx_os_status hx_settings_value_get_i64(
	const hx_settings_value *value, int64_t *out, hx_os_error *error)
{
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_I64, out, error);
	if (status == HX_OS_OK)
		*out = value->scalar.integer;
	return status;
}

hx_os_status hx_settings_value_get_double(
	const hx_settings_value *value, double *out, hx_os_error *error)
{
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_DOUBLE, out, error);
	if (status == HX_OS_OK)
		*out = value->scalar.real;
	return status;
}

hx_os_status hx_settings_value_get_pointer(
	const hx_settings_value *value, void **out, hx_os_error *error)
{
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_POINTER, out, error);
	if (status == HX_OS_OK)
		*out = value->scalar.pointer;
	return status;
}

hx_os_status hx_settings_value_get_string(
	const hx_settings_value *value, hx_string *out, hx_os_error *error)
{
	hx_string_view text;
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_STRING, out, error);
	if (status != HX_OS_OK)
		return status;
	text.data = (const char *)(value + 1);
	text.size = value->count;
	return hx_string_assign(out, text, error);
}
hx_os_status hx_settings_value_at(const hx_settings_value *value, size_t index,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_value *child;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!value)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (value->kind != HX_SETTINGS_ARRAY &&
		value->kind != HX_SETTINGS_OBJECT)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (index >= value->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	child = value->kind == HX_SETTINGS_ARRAY
		? ((hx_settings_value *const *)(value + 1))[index]
		: ((const hx_settings_entry *)(value + 1))[index].value;
	hx_settings_value_retain(child);
	*out = child;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_settings_value_member(const hx_settings_value *value,
	hx_string_view name, hx_settings_value **out, hx_os_error *error)
{
	hx_settings_value *child;
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_OBJECT, out, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_settings_i_key(name, error);
	if (status != HX_OS_OK)
		return status;
	child = (hx_settings_value *)hx_settings_i_member(value, name);
	if (!child)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	hx_settings_value_retain(child);
	*out = child;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_settings_value_name_at(const hx_settings_value *value,
	size_t index, hx_string *out, hx_os_error *error)
{
	const hx_settings_entry *entries;
	hx_os_status status =
		hx_settings_check_type(value, HX_SETTINGS_OBJECT, out, error);
	if (status != HX_OS_OK)
		return status;
	if (index >= value->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	entries = (const hx_settings_entry *)(value + 1);
	return hx_string_assign(out, entries[index].name, error);
}
