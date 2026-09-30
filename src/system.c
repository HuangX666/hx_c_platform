#include "system_private.h"

hx_os_status hx_os_i_strings_from_block(hx_os_context *context,
	const char *block, size_t bytes, int validate_utf8,
	hx_os_string_list **out, hx_os_error *error)
{
	size_t offset = 0, count = 0, total, invalid, i;
	hx_os_string_list *list;
	char *storage;
	if (!out || (!block && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (bytes > HX_OS_I_SYSTEM_BYTES)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	while (offset < bytes) {
		const char *end = memchr(block + offset, '\0', bytes - offset);
		size_t length;
		if (!end)
			return hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		length = (size_t)(end - block - offset);
		if (++count > HX_OS_I_SYSTEM_ITEMS)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		if (validate_utf8 &&
			hx_os_text_validate_utf8(
				block + offset, length, &invalid) != HX_OS_OK)
			return hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
		offset += length + 1;
	}
	total = sizeof(*list) + (count + 1) * sizeof(*list->items) + bytes;
	list = hx_os_i_zero(context, total);
	if (!list)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	list->count = count;
	list->items = (const char **)(list + 1);
	storage = (char *)(list->items + count + 1);
	if (bytes)
		memcpy(storage, block, bytes);
	offset = 0;
	for (i = 0; i < count; ++i) {
		list->items[i] = storage + offset;
		offset += strlen(storage + offset) + 1;
	}
	*out = list;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_string_list_count(const hx_os_string_list *list)
{
	return list ? list->count : 0;
}
const char *hx_os_string_list_at(const hx_os_string_list *list, size_t index)
{
	return list && index < list->count ? list->items[index] : NULL;
}
const char *const *hx_os_string_list_data(const hx_os_string_list *list)
{
	return list ? list->items : NULL;
}
void hx_os_string_list_destroy(hx_os_string_list **list)
{
	if (list) {
		hx_os_memory_free(*list);
		*list = NULL;
	}
}

hx_os_status hx_os_process_arguments(
	hx_os_context *context, hx_os_string_list **out, hx_os_error *error)
{
#if HX_OS_ENABLE_SYSTEM_UTILS
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	return hx_os_i_arguments(context, out, error);
#else
	(void)context;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_process_id(uint64_t *identifier)
{
#if HX_OS_ENABLE_SYSTEM_UTILS
	if (!identifier)
		return HX_OS_INVALID;
	*identifier = hx_os_i_process_id();
	return HX_OS_OK;
#else
	(void)identifier;
	return HX_OS_NOT_SUPPORTED;
#endif
}

hx_os_status hx_os_host_name_string(hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_SYSTEM_UTILS
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_host_name(out, error);
#else
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_host_name(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
	hx_string text = HX_STRING_INIT;
	hx_os_status status;
	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_host_name_string(&text, error);
	if (status == HX_OS_OK)
		status = hx_os_i_text_copy(hx_string_c_str(&text), buffer,
			capacity, required, error);
	hx_string_destroy(&text);
	return status;
}

hx_os_status hx_os_sys_emulated(int *emulated, hx_os_error *error)
{
#if HX_OS_ENABLE_SYSTEM_UTILS
	if (!emulated)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_emulated(emulated, error);
#else
	(void)emulated;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
