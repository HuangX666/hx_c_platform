#include "signal_private.h"

#if HX_OS_ENABLE_SIGNALS
static hx_os_status hx_os_signal_argument_value(const hx_os_calldata *data,
	const hx_os_calldata_entry *entry, hx_os_param_value *out,
	hx_os_error *error)
{
	hx_os_param_value value;

	memset(&value, 0, sizeof(value));
	switch (entry->kind) {
	case HX_OS_CALLDATA_BOOL:
		value.kind = HX_OS_PARAM_BOOL;
		value.data.integer = entry->value.integer;
		break;
	case HX_OS_CALLDATA_I64:
		value.kind = HX_OS_PARAM_I64;
		value.data.integer = entry->value.integer;
		break;
	case HX_OS_CALLDATA_DOUBLE:
		value.kind = HX_OS_PARAM_DOUBLE;
		value.data.real = entry->value.real;
		break;
	case HX_OS_CALLDATA_POINTER:
		value.kind = HX_OS_PARAM_POINTER;
		value.data.pointer = entry->value.pointer;
		break;
	case HX_OS_CALLDATA_STRING:
		value.kind = HX_OS_PARAM_STRING;
		value.data.string.data =
			(const char *)data->text + entry->value.text.offset;
		value.data.string.size = entry->value.text.bytes;
		break;
	default:
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	}
	*out = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif

size_t hx_os_signal_arguments_count(const hx_os_signal_arguments *arguments)
{
	return arguments ? arguments->data->count : 0;
}

hx_os_status hx_os_signal_arguments_get(const hx_os_signal_arguments *arguments,
	const char *name, hx_os_param_value *out, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	size_t bytes, i;
	hx_os_status status;

	if (!arguments || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_parameter_name(name, &bytes, error);
	if (status != HX_OS_OK)
		return status;

	for (i = 0; i < arguments->data->count; ++i) {
		const hx_os_calldata_entry *entry =
			arguments->data->entries + i;

		if (bytes == entry->name_bytes &&
			!memcmp(arguments->data->text + entry->name_offset,
				name, bytes))
			return hx_os_signal_argument_value(
				arguments->data, entry, out, error);
	}

	return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
#else
	(void)arguments;
	(void)name;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_signal_arguments_at(const hx_os_signal_arguments *arguments,
	size_t index, hx_string_view *name, hx_os_param_value *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	const hx_os_calldata_entry *entry;
	hx_os_status status;

	if (!arguments || !name || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (index >= arguments->data->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	entry = arguments->data->entries + index;
	status =
		hx_os_signal_argument_value(arguments->data, entry, out, error);
	if (status == HX_OS_OK) {
		name->data = (const char *)arguments->data->text +
			entry->name_offset;
		name->size = entry->name_bytes;
	}

	return status;
#else
	(void)arguments;
	(void)index;
	(void)name;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_signal_argument_get(
	const hx_os_signal_arguments *arguments, const char *name,
	uint32_t kind, const void *out, hx_os_param_value *value,
	hx_os_error *error)
{
	hx_os_status status;

	if (!HX_OS_ENABLE_SIGNALS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_signal_arguments_get(arguments, name, value, error);
	if (status == HX_OS_OK && value->kind != kind)
		status = hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);

	return status;
}

hx_os_status hx_os_signal_arguments_get_bool(
	const hx_os_signal_arguments *arguments, const char *name, int *out,
	hx_os_error *error)
{
	hx_os_param_value value;
	hx_os_status status = hx_os_signal_argument_get(
		arguments, name, HX_OS_PARAM_BOOL, out, &value, error);

	if (status == HX_OS_OK)
		*out = (int)value.data.integer;

	return status;
}

hx_os_status hx_os_signal_arguments_get_i64(
	const hx_os_signal_arguments *arguments, const char *name, int64_t *out,
	hx_os_error *error)
{
	hx_os_param_value value;
	hx_os_status status = hx_os_signal_argument_get(
		arguments, name, HX_OS_PARAM_I64, out, &value, error);

	if (status == HX_OS_OK)
		*out = value.data.integer;

	return status;
}

hx_os_status hx_os_signal_arguments_get_double(
	const hx_os_signal_arguments *arguments, const char *name, double *out,
	hx_os_error *error)
{
	hx_os_param_value value;
	hx_os_status status = hx_os_signal_argument_get(
		arguments, name, HX_OS_PARAM_DOUBLE, out, &value, error);

	if (status == HX_OS_OK)
		*out = value.data.real;

	return status;
}

hx_os_status hx_os_signal_arguments_get_string(
	const hx_os_signal_arguments *arguments, const char *name,
	hx_string_view *out, hx_os_error *error)
{
	hx_os_param_value value;
	hx_os_status status = hx_os_signal_argument_get(
		arguments, name, HX_OS_PARAM_STRING, out, &value, error);

	if (status == HX_OS_OK)
		*out = value.data.string;

	return status;
}

hx_os_status hx_os_signal_arguments_get_pointer(
	const hx_os_signal_arguments *arguments, const char *name, void **out,
	hx_os_error *error)
{
	hx_os_param_value value;
	hx_os_status status = hx_os_signal_argument_get(
		arguments, name, HX_OS_PARAM_POINTER, out, &value, error);

	if (status == HX_OS_OK)
		*out = value.data.pointer;

	return status;
}

hx_os_status hx_os_signal_arguments_copy(
	const hx_os_signal_arguments *arguments, hx_os_context *context,
	hx_os_params **out, hx_os_error *error)
{
	hx_os_params *copy = NULL;
	hx_os_status status;
	size_t i;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_SIGNALS || !HX_OS_ENABLE_PARAMETERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!arguments)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_params_create(context, &copy, error);
	for (i = 0; status == HX_OS_OK &&
		i < hx_os_signal_arguments_count(arguments);
		++i) {
		hx_string_view name;
		hx_os_param_value value;

		status = hx_os_signal_arguments_at(
			arguments, i, &name, &value, error);
		if (status == HX_OS_OK)
			status =
				hx_os_params_set(copy, name.data, value, error);
	}
	if (status != HX_OS_OK)
		(void)hx_os_params_destroy(&copy, NULL);
	*out = copy;

	return status;
}
