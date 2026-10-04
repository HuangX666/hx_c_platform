#include "internal.h"
#include "hx_string.h"

#include <stdio.h>

void hx_string_init(hx_string *string, hx_os_context *context)
{
	memset(string, 0, sizeof(*string));
	string->context = context;
	hx_os_i_context_retain(context);
}

void hx_string_destroy(hx_string *string)
{
	hx_os_context *context = string->context;

	hx_os_memory_free(string->data);
	memset(string, 0, sizeof(*string));

	hx_os_i_context_release(context);
}

const char *hx_string_c_str(const hx_string *string)
{
	return string->data ? string->data : "";
}

size_t hx_string_size_bytes(const hx_string *string)
{
	return string->size;
}

size_t hx_string_codepoint_count(const hx_string *string)
{
	size_t i;
	size_t count = 0;

	for (i = 0; i < string->size; ++i)
		if (((unsigned char)string->data[i] & 0xc0) != 0x80)
			++count;

	return count;
}

hx_string_view hx_string_view_of(const hx_string *string)
{
	hx_string_view result;

	result.data = hx_string_c_str(string);
	result.size = string->size;

	return result;
}

hx_string_view hx_string_view_from_cstr(const char *text)
{
	hx_string_view result;

	result.data = text ? text : "";
	result.size = text ? strlen(text) : 0;

	return result;
}

hx_os_status hx_string_reserve(
	hx_string *string, size_t bytes, hx_os_error *error)
{
	char *memory;
	size_t capacity;

	if (bytes == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	if (bytes + 1 <= string->capacity)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	capacity = string->capacity ? string->capacity : 32;
	if (capacity <= SIZE_MAX - capacity / 2)
		capacity += capacity / 2;

	if (capacity < bytes + 1)
		capacity = bytes + 1;

	memory = hx_os_i_alloc(string->context, capacity, 0);
	if (!memory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	if (string->size)
		memcpy(memory, string->data, string->size);

	memory[string->size] = 0;

	hx_os_memory_free(string->data);
	string->data = memory;
	string->capacity = capacity;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_string_clear(hx_string *string)
{
	string->size = 0;
	if (string->data)
		string->data[0] = 0;
}

static int hx_string_boundary(const hx_string *string, size_t offset)
{
	return offset <= string->size &&
		(offset == string->size ||
			((unsigned char)string->data[offset] & 0xc0) != 0x80);
}

hx_os_status hx_string_replace(hx_string *string, size_t offset, size_t bytes,
	hx_string_view text, hx_os_error *error)
{
	size_t size;
	size_t error_offset;
	const char *input = text.data;
	char *temporary = NULL;
	hx_os_status status;
	int aliases;

	if (offset > string->size || bytes > string->size - offset ||
		!hx_string_boundary(string, offset) ||
		!hx_string_boundary(string, offset + bytes) ||
		(!text.data && text.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	aliases = text.size && string->data &&
		(uintptr_t)text.data >= (uintptr_t)string->data &&
		(uintptr_t)text.data - (uintptr_t)string->data <
			string->capacity;
	if (aliases) {
		size_t start = (size_t)((uintptr_t)text.data -
			(uintptr_t)string->data);

		if (start > string->size ||
			text.size > string->size + 1 - start)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	status = hx_os_text_validate_utf8(text.data, text.size, &error_offset);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (!hx_os_i_add_size(string->size - bytes, text.size, &size) ||
		size == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (aliases) {
		temporary = hx_os_i_alloc(string->context, text.size, 0);
		if (!temporary)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		memcpy(temporary, text.data, text.size);
		input = temporary;
	}

	status = hx_string_reserve(string, size, error);
	if (status == HX_OS_OK) {
		memmove(string->data + offset + text.size,
			string->data + offset + bytes,
			string->size - offset - bytes + 1);
		if (text.size)
			memcpy(string->data + offset, input, text.size);

		string->size = size;
	}

	hx_os_memory_free(temporary);

	return status;
}

hx_os_status hx_string_assign(
	hx_string *string, hx_string_view text, hx_os_error *error)
{
	return hx_string_replace(string, 0, string->size, text, error);
}

hx_os_status hx_string_assign_cstr(
	hx_string *string, const char *text, hx_os_error *error)
{
	return hx_string_assign(string, hx_string_view_from_cstr(text), error);
}

hx_os_status hx_string_append(
	hx_string *string, hx_string_view text, hx_os_error *error)
{
	return hx_string_replace(string, string->size, 0, text, error);
}

hx_os_status hx_string_append_cstr(
	hx_string *string, const char *text, hx_os_error *error)
{
	return hx_string_append(string, hx_string_view_from_cstr(text), error);
}

hx_os_status hx_string_insert(hx_string *string, size_t offset,
	hx_string_view text, hx_os_error *error)
{
	return hx_string_replace(string, offset, 0, text, error);
}

hx_os_status hx_string_erase(
	hx_string *string, size_t offset, size_t bytes, hx_os_error *error)
{
	hx_string_view empty = {NULL, 0};

	return hx_string_replace(string, offset, bytes, empty, error);
}

hx_os_status hx_string_substring(hx_string *destination,
	const hx_string *source, size_t offset, size_t bytes,
	hx_os_error *error)
{
	hx_string_view view;

	if (offset > source->size || bytes > source->size - offset ||
		!hx_string_boundary(source, offset) ||
		!hx_string_boundary(source, offset + bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	view.data = hx_string_c_str(source) + offset;
	view.size = bytes;

	return hx_string_assign(destination, view, error);
}

hx_os_status hx_string_copy(
	hx_string *destination, const hx_string *source, hx_os_error *error)
{
	return hx_string_assign(destination, hx_string_view_of(source), error);
}

void hx_string_move(hx_string *destination, hx_string *source)
{
	if (destination == source)
		return;

	hx_string_destroy(destination);
	*destination = *source;
	memset(source, 0, sizeof(*source));
}

size_t hx_string_find(
	const hx_string *string, hx_string_view needle, size_t start)
{
	size_t i;
	size_t error_offset;

	if (!hx_string_boundary(string, start) ||
		needle.size > string->size - start ||
		hx_os_text_validate_utf8(
			needle.data, needle.size, &error_offset) != HX_OS_OK)
		return HX_STRING_NOT_FOUND;
	if (!needle.size)
		return start;

	for (i = start; i <= string->size - needle.size; ++i)
		if (memcmp(string->data + i, needle.data, needle.size) == 0)
			return i;

	return HX_STRING_NOT_FOUND;
}

int hx_string_compare(const hx_string *string, hx_string_view other)
{
	size_t bytes = string->size < other.size ? string->size : other.size;

	int result = bytes ? memcmp(string->data, other.data, bytes) : 0;
	if (result)
		return result < 0 ? -1 : 1;

	return string->size < other.size ? -1 : string->size > other.size;
}

hx_os_status hx_string_vformat(hx_string *string, hx_os_error *error,
	const char *format, va_list arguments)
{
	va_list copy;
	int count;
	int written;
	char *memory;
	size_t offset;
	hx_os_status status;

	if (!format)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	va_copy(copy, arguments);
	count = vsnprintf(NULL, 0, format, copy);
	va_end(copy);
	if (count < 0)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	memory = hx_os_i_alloc(string->context, (size_t)count + 1, 0);
	if (!memory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	va_copy(copy, arguments);
	written = vsnprintf(memory, (size_t)count + 1, format, copy);
	va_end(copy);

	status = written == count
		? hx_os_text_validate_utf8(memory, (size_t)count, &offset)
		: HX_OS_INVALID;
	if (status != HX_OS_OK) {
		hx_os_memory_free(memory);

		return hx_os_i_error(error, status, 0, 0);
	}

	hx_os_memory_free(string->data);
	string->data = memory;
	string->size = (size_t)count;
	string->capacity = (size_t)count + 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_string_format(
	hx_string *string, hx_os_error *error, const char *format, ...)
{
	va_list arguments;
	hx_os_status status;

	va_start(arguments, format);

	status = hx_string_vformat(string, error, format, arguments);
	va_end(arguments);

	return status;
}

hx_os_status hx_string_append_format(
	hx_string *string, hx_os_error *error, const char *format, ...)
{
	va_list arguments;
	hx_string temporary;
	hx_os_status status;

	hx_string_init(&temporary, string->context);
	va_start(arguments, format);

	status = hx_string_vformat(&temporary, error, format, arguments);
	va_end(arguments);
	if (status == HX_OS_OK)
		status = hx_string_append(
			string, hx_string_view_of(&temporary), error);

	hx_string_destroy(&temporary);

	return status;
}

hx_os_status hx_string_assign_encoded(hx_string *string,
	hx_os_encoding encoding, const void *input, size_t bytes,
	hx_os_error *error)
{
	hx_os_text_converter *converter;
	hx_string temporary;
	hx_os_status status;
	size_t used = 0;

	if (!input && bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (encoding == HX_OS_UTF8) {
		hx_string_view view;

		view.data = input;
		view.size = bytes;

		return hx_string_assign(string, view, error);
	}

	status = hx_os_text_converter_create(string->context, encoding,
		HX_OS_UTF8, HX_OS_TEXT_STRICT, &converter, error);
	if (status != HX_OS_OK)
		return status;

	hx_string_init(&temporary, string->context);

	status = hx_string_reserve(&temporary, bytes, error);

	while (status == HX_OS_OK) {
		hx_os_text_result result;
		size_t next;

		status = hx_os_text_converter_update(converter,
			input ? (const char *)input + used : NULL, bytes - used,
			temporary.data + temporary.size,
			temporary.capacity - temporary.size - 1, 1, &result,
			error);
		used += result.input_used;
		temporary.size += result.output_used;
		temporary.data[temporary.size] = 0;
		if (status != HX_OS_OUTPUT_FULL)
			break;

		if (!hx_os_i_add_size(temporary.capacity,
			    temporary.capacity / 2 + 16, &next)) {
			status = hx_os_i_error(error, HX_OS_RANGE, 0, 0);
			break;
		}

		status = hx_string_reserve(&temporary, next, error);
	}

	hx_os_text_converter_destroy(converter);
	if (status == HX_OS_OK)
		hx_string_move(string, &temporary);

	hx_string_destroy(&temporary);

	return status;
}

hx_os_status hx_string_encode(const hx_string *string, hx_os_encoding encoding,
	void *output, size_t capacity, hx_os_text_result *result,
	hx_os_error *error)
{
	hx_os_text_converter *converter;
	hx_os_status status;

	if (!result)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	memset(result, 0, sizeof(*result));
	result->error_offset = UINT64_MAX;

	status = hx_os_text_converter_create(string->context, HX_OS_UTF8,
		encoding, HX_OS_TEXT_STRICT, &converter, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_text_converter_update(converter, string->data,
		string->size, output, capacity, 1, result, error);

	hx_os_text_converter_destroy(converter);

	return status;
}
