#include "number_private.h"
#include "hx_text_utils.h"
#include <assert.h>

hx_os_status hx_os_text_split_init(hx_os_text_split *split,
	hx_string_view input, hx_string_view delimiter, int keep_empty,
	hx_os_error *error)
{
	hx_os_status status;
	size_t invalid;

	if (!split || (!input.data && input.size) || !delimiter.data ||
		!delimiter.size || (keep_empty != 0 && keep_empty != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_text_validate_utf8(input.data, input.size, &invalid);
	if (status == HX_OS_OK)
		status = hx_os_text_validate_utf8(
			delimiter.data, delimiter.size, &invalid);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	split->input = input;
	split->delimiter = delimiter;
	split->position = 0;
	split->keep_empty = keep_empty;
	split->finished = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_text_split_next(
	hx_os_text_split *split, hx_string_view *out, hx_os_error *error)
{
	if (!split || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	while (!split->finished) {
		size_t start = split->position, end = start;
		int found = 0;

		while (split->delimiter.size <= split->input.size - end) {
			if (!memcmp(split->input.data + end,
				    split->delimiter.data,
				    split->delimiter.size)) {
				found = 1;
				break;
			}

			++end;
		}

		if (found)
			split->position = end + split->delimiter.size;
		else {
			end = split->input.size;
			split->position = end;
			split->finished = 1;
		}

		if (end != start || split->keep_empty) {
			out->data = split->input.data
				? split->input.data + start
				: NULL;
			out->size = end - start;

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}

	return hx_os_i_error(error, HX_OS_EOF, 0, 0);
}

#if HX_OS_ENABLE_TEXT_NUMBER
static hx_os_status hx_os_text_parse_integer(hx_string_view text,
	unsigned base, int is_signed, uint64_t *magnitude, int *negative)
{
	uint64_t value = 0, limit;
	size_t i = 0;
	int overflow = 0;

	if ((!text.data && text.size) ||
		(base != 2 && base != 8 && base != 10 && base != 16))
		return HX_OS_INVALID;
	if (!text.size)
		return HX_OS_TYPE_MISMATCH;

	*negative = text.data[0] == '-';
	if (*negative && !is_signed)
		return HX_OS_TYPE_MISMATCH;
	if (*negative || text.data[0] == '+')
		i = 1;
	if (i == text.size)
		return HX_OS_TYPE_MISMATCH;

	limit = is_signed ? (uint64_t)INT64_MAX + (unsigned)*negative
			  : UINT64_MAX;
	for (; i < text.size; ++i) {
		int digit = hx_os_i_number_digit((unsigned char)text.data[i]);

		if (digit < 0 || (unsigned)digit >= base)
			return HX_OS_TYPE_MISMATCH;
		if (overflow || value > (limit - (unsigned)digit) / base)
			overflow = 1;
		else
			value = value * base + (unsigned)digit;
	}

	if (overflow)
		return HX_OS_RANGE;

	*magnitude = value;

	return HX_OS_OK;
}
#endif

hx_os_status hx_os_text_parse_i64(hx_string_view text, unsigned base,
	int64_t *out, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_NUMBER
	uint64_t magnitude;
	int negative;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_text_parse_integer(text, base, 1, &magnitude, &negative);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (negative)
		*out = magnitude == (uint64_t)INT64_MAX + 1
			? INT64_MIN
			: -(int64_t)magnitude;
	else
		*out = (int64_t)magnitude;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)text;
	(void)base;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_parse_u64(hx_string_view text, unsigned base,
	uint64_t *out, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_NUMBER
	uint64_t magnitude;
	int negative;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_text_parse_integer(text, base, 0, &magnitude, &negative);
	if (status == HX_OS_OK)
		*out = magnitude;

	return hx_os_i_error(error, status, 0, 0);
#else
	(void)text;
	(void)base;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static int hx_os_text_decimal(unsigned char value)
{
	return value >= '0' && value <= '9';
}

static int hx_os_text_natural_digits(hx_string_view left, size_t *left_at,
	hx_string_view right, size_t *right_at)
{
	size_t left_start = *left_at, right_start = *right_at;
	size_t left_end = left_start, right_end = right_start;
	size_t left_significant = left_start, right_significant = right_start;
	size_t left_size, right_size;
	int comparison;

	while (left_end < left.size &&
		hx_os_text_decimal((unsigned char)left.data[left_end]))
		++left_end;
	while (right_end < right.size &&
		hx_os_text_decimal((unsigned char)right.data[right_end]))
		++right_end;

	while (left_significant < left_end &&
		left.data[left_significant] == '0')
		++left_significant;
	while (right_significant < right_end &&
		right.data[right_significant] == '0')
		++right_significant;

	left_size = left_end - left_significant;
	right_size = right_end - right_significant;
	if (left_size != right_size)
		return left_size < right_size ? -1 : 1;

	comparison = left_size
		? memcmp(left.data + left_significant,
			right.data + right_significant, left_size)
		: 0;
	if (comparison)
		return comparison < 0 ? -1 : 1;

	left_size = left_end - left_start;
	right_size = right_end - right_start;
	if (left_size != right_size)
		return left_size < right_size ? -1 : 1;

	*left_at = left_end;
	*right_at = right_end;

	return 0;
}

int hx_os_text_compare_natural(hx_string_view left, hx_string_view right)
{
	size_t a = 0, b = 0;

	assert(left.data || !left.size);
	assert(right.data || !right.size);

	while (a < left.size && b < right.size) {
		unsigned char x = (unsigned char)left.data[a];
		unsigned char y = (unsigned char)right.data[b];

		if (hx_os_text_decimal(x) && hx_os_text_decimal(y)) {
			int comparison =
				hx_os_text_natural_digits(left, &a, right, &b);
			if (comparison)
				return comparison;
		}
		else {
			if (x != y)
				return x < y ? -1 : 1;

			++a;
			++b;
		}
	}

	return a < left.size ? 1 : b < right.size ? -1 : 0;
}
