#include "internal.h"
#include "hx_os_obfuscate.h"
#include "hx_os_util.h"
hx_os_status hx_os_obfuscate_decode(const hx_os_obfuscated *value, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
#if HX_OS_ENABLE_OBFUSCATION
	size_t i;
	if (!value || !value->key || (!value->data && value->bytes) ||
		!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->bytes == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*required = value->bytes + 1;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	for (i = 0; i < value->bytes; ++i)
		buffer[i] = (char)(value->data[i] ^ value->key);
	buffer[value->bytes] = 0;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)value;
	(void)buffer;
	(void)capacity;
	(void)required;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_obfuscate_string(
	const hx_os_obfuscated *value, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_OBFUSCATION
	size_t bytes;
	char *buffer;
	hx_string_view view;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_obfuscate_decode(value, NULL, 0, &bytes, error);
	if (status != HX_OS_OUTPUT_FULL)
		return status;
	buffer = hx_os_i_alloc(out->context, bytes, 0);
	if (!buffer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_obfuscate_decode(value, buffer, bytes, &bytes, error);
	if (status == HX_OS_OK) {
		view.data = buffer;
		view.size = bytes - 1;
		status = hx_string_assign(out, view, error);
	}
	hx_os_secure_zero(buffer, bytes);
	hx_os_memory_free(buffer);
	return status;
#else
	(void)value;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
