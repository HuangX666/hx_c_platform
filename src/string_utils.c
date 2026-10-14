#include "internal.h"
#include "hx_string.h"
#include <assert.h>

struct hx_string_parts {
	hx_string text;
	size_t count;
	// Views follow this header.
};

static hx_os_status hx_string_check_view(hx_string_view view)
{
	size_t invalid;

	return hx_os_text_validate_utf8(view.data, view.size, &invalid);
}

static size_t hx_string_match(
	hx_string_view text, hx_string_view needle, size_t start)
{
	size_t i;

	if (start > text.size || needle.size > text.size - start)
		return SIZE_MAX;

	for (i = start; i <= text.size - needle.size; ++i)
		if (!memcmp(text.data + i, needle.data, needle.size))
			return i;

	return SIZE_MAX;
}

int hx_string_starts_with(const hx_string *string, hx_string_view prefix)
{
	return string && hx_string_check_view(prefix) == HX_OS_OK &&
		prefix.size <= string->size &&
		(!prefix.size ||
			!memcmp(hx_string_c_str(string), prefix.data,
				prefix.size));
}

int hx_string_ends_with(const hx_string *string, hx_string_view suffix)
{
	return string && hx_string_check_view(suffix) == HX_OS_OK &&
		suffix.size <= string->size &&
		(!suffix.size ||
			!memcmp(hx_string_c_str(string) + string->size -
					suffix.size,
				suffix.data, suffix.size));
}

static int hx_string_space(unsigned char value)
{
	return value == ' ' || (value >= '\t' && value <= '\r');
}

void hx_string_trim_ascii_in_place(hx_string *string)
{
	size_t start = 0, end;

	assert(string);

	end = string->size;

	while (start < end &&
		hx_string_space((unsigned char)string->data[start]))
		++start;

	while (end > start &&
		hx_string_space((unsigned char)string->data[end - 1]))
		--end;
	if (string->data) {
		memmove(string->data, string->data + start, end - start);
		string->data[end - start] = 0;
	}

	string->size = end - start;
}

hx_os_status hx_string_trim_ascii(hx_string *string, hx_os_error *error)
{
	if (!string)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_trim_ascii_in_place(string);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_string_case_in_place(hx_string *string, int upper)
{
	size_t i;

	assert(string);

	for (i = 0; i < string->size; ++i) {
		unsigned char c = (unsigned char)string->data[i];

		if (upper && c >= 'a' && c <= 'z')
			string->data[i] = (char)(c - 'a' + 'A');

		if (!upper && c >= 'A' && c <= 'Z')
			string->data[i] = (char)(c - 'A' + 'a');
	}
}

void hx_string_lower_ascii_in_place(hx_string *string)
{
	hx_string_case_in_place(string, 0);
}

void hx_string_upper_ascii_in_place(hx_string *string)
{
	hx_string_case_in_place(string, 1);
}

hx_os_status hx_string_lower_ascii(hx_string *string, hx_os_error *error)
{
	if (!string)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_lower_ascii_in_place(string);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_string_upper_ascii(hx_string *string, hx_os_error *error)
{
	if (!string)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_upper_ascii_in_place(string);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_string_replace_all(hx_string *string, hx_string_view needle,
	hx_string_view replacement, size_t *replaced, hx_os_error *error)
{
	hx_string temporary;
	hx_string_view input;
	hx_os_status status;
	size_t count = 0, start = 0, found, bytes, written = 0;

	if (!string || !needle.size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_string_check_view(needle);
	if (status == HX_OS_OK)
		status = hx_string_check_view(replacement);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	input = hx_string_view_of(string);

	while ((found = hx_string_match(input, needle, start)) != SIZE_MAX) {
		++count;
		start = found + needle.size;
	}

	if (!count) {
		if (replaced)
			*replaced = 0;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	bytes = string->size - count * needle.size;
	if (replacement.size &&
		count > (SIZE_MAX - 1 - bytes) / replacement.size)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	bytes += count * replacement.size;
	hx_string_init(&temporary, string->context);

	status = hx_string_reserve(&temporary, bytes, error);
	start = 0;
	if (status == HX_OS_OK) {
		while ((found = hx_string_match(input, needle, start)) !=
			SIZE_MAX) {
			memcpy(temporary.data + written, input.data + start,
				found - start);
			written += found - start;
			if (replacement.size)
				memcpy(temporary.data + written,
					replacement.data, replacement.size);

			written += replacement.size;
			start = found + needle.size;
		}

		memcpy(temporary.data + written, input.data + start,
			input.size - start);
		temporary.size = bytes;
		temporary.data[bytes] = 0;
		hx_string_move(string, &temporary);
		if (replaced)
			*replaced = count;
	}

	hx_string_destroy(&temporary);

	return status;
}

static hx_os_status hx_string_split_scan(hx_string_view input,
	hx_string_view separator, size_t maximum, int keep_empty,
	hx_string_view *views, size_t *count)
{
	size_t start = 0, found, end, used = 0;

	for (;;) {
		found = hx_string_match(input, separator, start);
		end = found == SIZE_MAX ? input.size : found;
		if (end != start || keep_empty) {
			if (used == maximum)
				return HX_OS_LIMIT;

			if (views) {
				views[used].data = input.data + start;
				views[used].size = end - start;
			}

			++used;
		}

		if (found == SIZE_MAX)
			break;

		start = found + separator.size;
	}

	*count = used;

	return HX_OS_OK;
}

hx_os_status hx_string_split(const hx_string *string, hx_string_view separator,
	hx_string_parts **out, hx_os_error *error)
{
	return hx_string_split_ex(string, separator, 4096, 1, out, error);
}

hx_os_status hx_string_split_ex(const hx_string *string,
	hx_string_view separator, size_t max_parts, int keep_empty,
	hx_string_parts **out, hx_os_error *error)
{
	hx_string_parts *parts;
	hx_os_status status;
	size_t count;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!string || !separator.size || !max_parts || max_parts > 65536 ||
		(keep_empty != 0 && keep_empty != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_string_check_view(separator);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_string_split_scan(hx_string_view_of(string), separator,
		max_parts, keep_empty, NULL, &count);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	parts = hx_os_i_zero(string->context,
		sizeof(*parts) + count * sizeof(hx_string_view));
	if (!parts)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	hx_string_init(&parts->text, string->context);

	status = hx_string_copy(&parts->text, string, error);
	if (status == HX_OS_OK)
		status = hx_string_split_scan(hx_string_view_of(&parts->text),
			separator, max_parts, keep_empty,
			(hx_string_view *)(parts + 1), &parts->count);

	if (status != HX_OS_OK) {
		hx_string_parts_destroy(&parts);

		return hx_os_i_error(error, status, 0, 0);
	}

	*out = parts;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_string_parts_count(const hx_string_parts *parts)
{
	return parts ? parts->count : 0;
}

hx_os_status hx_string_parts_at(
	const hx_string_parts *parts, size_t index, hx_string_view *out)
{
	if (!parts || !out)
		return HX_OS_INVALID;

	if (index >= parts->count)
		return HX_OS_RANGE;

	*out = ((const hx_string_view *)(parts + 1))[index];

	return HX_OS_OK;
}

void hx_string_parts_destroy(hx_string_parts **parts)
{
	if (!parts || !*parts)
		return;

	hx_string_destroy(&(*parts)->text);
	hx_os_memory_free(*parts);
	*parts = NULL;
}

hx_os_status hx_string_join(hx_string *out, const hx_string_view *parts,
	size_t count, hx_string_view separator, hx_os_error *error)
{
	hx_string temporary;
	hx_os_status status;
	size_t i, bytes = 0, written = 0;

	if (!out || (!parts && count))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_string_check_view(separator);

	for (i = 0; i < count && status == HX_OS_OK; ++i) {
		status = hx_string_check_view(parts[i]);
		if (status == HX_OS_OK &&
			(!hx_os_i_add_size(bytes, parts[i].size, &bytes) ||
				(i &&
					!hx_os_i_add_size(bytes, separator.size,
						&bytes)) ||
				bytes == SIZE_MAX))
			status = HX_OS_RANGE;
	}

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	hx_string_init(&temporary, out->context);

	status = hx_string_reserve(&temporary, bytes, error);
	if (status == HX_OS_OK) {
		for (i = 0; i < count; ++i) {
			if (i && separator.size) {
				memcpy(temporary.data + written, separator.data,
					separator.size);
				written += separator.size;
			}

			if (parts[i].size)
				memcpy(temporary.data + written, parts[i].data,
					parts[i].size);

			written += parts[i].size;
		}

		temporary.data[written] = 0;
		temporary.size = written;
		hx_string_move(out, &temporary);
	}

	hx_string_destroy(&temporary);

	return status;
}
