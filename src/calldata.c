#include "signal_private.h"
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
hx_os_status hx_os_i_signal_name(
	const char *name, size_t *bytes, hx_os_error *error)
{
	size_t invalid, n = 0;
	hx_os_status status;

	if (!name)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	while (n <= 127 && name[n])
		++n;

	if (!n)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (n > 127)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_text_validate_utf8(name, n, &invalid);
	if (status == HX_OS_OK)
		*bytes = n;

	return hx_os_i_error(error, status, 0, 0);
}

static int hx_os_calldata_pack(hx_os_calldata *data, const char *name,
	size_t bytes, unsigned kind, hx_os_calldata_value value,
	hx_string_view text)
{
	hx_os_calldata_entry *entry;
	size_t need = bytes + 1;

	if (kind == HX_OS_CALLDATA_STRING &&
		(!hx_os_i_add_size(need, text.size, &need) ||
			!hx_os_i_add_size(need, 1, &need)))
		return 0;
	if (data->count == 16 || need > sizeof(data->text) - data->text_bytes)
		return 0;

	entry = &data->entries[data->count++];
	entry->kind = kind;
	entry->name_offset = (uint16_t)data->text_bytes;
	entry->name_bytes = (uint16_t)bytes;
	memcpy(data->text + data->text_bytes, name, bytes);
	data->text_bytes += (uint32_t)bytes;
	data->text[data->text_bytes++] = 0;
	if (kind == HX_OS_CALLDATA_STRING) {
		value.text.offset = (uint16_t)data->text_bytes;
		value.text.bytes = (uint16_t)text.size;
		if (text.size)
			memcpy(data->text + data->text_bytes, text.data,
				text.size);

		data->text_bytes += (uint32_t)text.size;
		data->text[data->text_bytes++] = 0;
	}

	entry->value = value;

	return 1;
}
#endif
hx_os_status hx_os_calldata_init(hx_os_calldata *data, size_t capacity)
{
	if (!data)
		return HX_OS_INVALID;

	if (capacity < sizeof(*data))
		return HX_OS_OUTPUT_FULL;

	memset(data, 0, sizeof(*data));

	return HX_OS_OK;
}

static hx_os_status hx_os_calldata_set(hx_os_calldata *data, const char *name,
	unsigned kind, hx_os_calldata_value value, hx_string_view text,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	hx_os_calldata result = HX_OS_CALLDATA_INIT;
	size_t bytes, invalid;
	uint32_t i;
	hx_os_status status;

	if (!data ||
		(kind == HX_OS_CALLDATA_STRING && !text.data && text.size) ||
		(kind == HX_OS_CALLDATA_BOOL && value.integer != 0 &&
			value.integer != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_signal_name(name, &bytes, error);
	if (status != HX_OS_OK)
		return status;

	if (text.size > sizeof(data->text))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	if (kind == HX_OS_CALLDATA_STRING) {
		status = hx_os_text_validate_utf8(
			text.data, text.size, &invalid);
		if (status != HX_OS_OK)
			return hx_os_i_error(error, status, 0, 0);
	}

	for (i = 0; i < data->count; ++i) {
		const hx_os_calldata_entry *entry = &data->entries[i];
		const char *old_name =
			(const char *)data->text + entry->name_offset;
		hx_string_view old_text = {NULL, 0};

		if (!strcmp(old_name, name))
			continue;

		if (entry->kind == HX_OS_CALLDATA_STRING) {
			old_text.data = (const char *)data->text +
				entry->value.text.offset;
			old_text.size = entry->value.text.bytes;
		}

		if (!hx_os_calldata_pack(&result, old_name, entry->name_bytes,
			    entry->kind, entry->value, old_text))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}

	if (!hx_os_calldata_pack(&result, name, bytes, kind, value, text))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	*data = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)data;
	(void)name;
	(void)kind;
	(void)value;
	(void)text;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_calldata_get(const hx_os_calldata *data,
	const char *name, unsigned kind, const void *out,
	hx_os_calldata_value *value, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	size_t bytes;
	uint32_t i;
	hx_os_status status;

	if (!data || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_signal_name(name, &bytes, error);
	if (status != HX_OS_OK)
		return status;

	for (i = 0; i < data->count; ++i) {
		const hx_os_calldata_entry *entry = &data->entries[i];

		if (entry->name_bytes != bytes ||
			memcmp(name, data->text + entry->name_offset, bytes))
			continue;
		if (entry->kind != kind)
			return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);

		*value = entry->value;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
#else
	(void)data;
	(void)name;
	(void)kind;
	(void)out;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_calldata_set_bool(
	hx_os_calldata *data, const char *name, int value, hx_os_error *error)
{
	hx_os_calldata_value stored = {0};
	hx_string_view empty = {NULL, 0};

	stored.integer = value;

	return hx_os_calldata_set(
		data, name, HX_OS_CALLDATA_BOOL, stored, empty, error);
}

hx_os_status hx_os_calldata_set_i64(hx_os_calldata *data, const char *name,
	int64_t value, hx_os_error *error)
{
	hx_os_calldata_value stored = {0};
	hx_string_view empty = {NULL, 0};

	stored.integer = value;

	return hx_os_calldata_set(
		data, name, HX_OS_CALLDATA_I64, stored, empty, error);
}

hx_os_status hx_os_calldata_set_double(hx_os_calldata *data, const char *name,
	double value, hx_os_error *error)
{
	hx_os_calldata_value stored = {0};
	hx_string_view empty = {NULL, 0};

	stored.real = value;

	return hx_os_calldata_set(
		data, name, HX_OS_CALLDATA_DOUBLE, stored, empty, error);
}

hx_os_status hx_os_calldata_set_string(hx_os_calldata *data, const char *name,
	hx_string_view value, hx_os_error *error)
{
	hx_os_calldata_value stored = {0};

	return hx_os_calldata_set(
		data, name, HX_OS_CALLDATA_STRING, stored, value, error);
}

hx_os_status hx_os_calldata_set_pointer(
	hx_os_calldata *data, const char *name, void *value, hx_os_error *error)
{
	hx_os_calldata_value stored = {0};
	hx_string_view empty = {NULL, 0};

	stored.pointer = value;

	return hx_os_calldata_set(
		data, name, HX_OS_CALLDATA_POINTER, stored, empty, error);
}

hx_os_status hx_os_calldata_get_bool(const hx_os_calldata *data,
	const char *name, int *out, hx_os_error *error)
{
	hx_os_calldata_value value;

	hx_os_status status = hx_os_calldata_get(
		data, name, HX_OS_CALLDATA_BOOL, out, &value, error);
	if (status == HX_OS_OK)
		*out = (int)value.integer;

	return status;
}

hx_os_status hx_os_calldata_get_i64(const hx_os_calldata *data,
	const char *name, int64_t *out, hx_os_error *error)
{
	hx_os_calldata_value value;

	hx_os_status status = hx_os_calldata_get(
		data, name, HX_OS_CALLDATA_I64, out, &value, error);
	if (status == HX_OS_OK)
		*out = value.integer;

	return status;
}

hx_os_status hx_os_calldata_get_double(const hx_os_calldata *data,
	const char *name, double *out, hx_os_error *error)
{
	hx_os_calldata_value value;

	hx_os_status status = hx_os_calldata_get(
		data, name, HX_OS_CALLDATA_DOUBLE, out, &value, error);
	if (status == HX_OS_OK)
		*out = value.real;

	return status;
}

hx_os_status hx_os_calldata_get_string(const hx_os_calldata *data,
	const char *name, hx_string_view *out, hx_os_error *error)
{
	hx_os_calldata_value value;

	hx_os_status status = hx_os_calldata_get(
		data, name, HX_OS_CALLDATA_STRING, out, &value, error);
	if (status == HX_OS_OK) {
		out->data = (const char *)data->text + value.text.offset;
		out->size = value.text.bytes;
	}

	return status;
}

hx_os_status hx_os_calldata_get_pointer(const hx_os_calldata *data,
	const char *name, void **out, hx_os_error *error)
{
	hx_os_calldata_value value;

	hx_os_status status = hx_os_calldata_get(
		data, name, HX_OS_CALLDATA_POINTER, out, &value, error);
	if (status == HX_OS_OK)
		*out = value.pointer;

	return status;
}
