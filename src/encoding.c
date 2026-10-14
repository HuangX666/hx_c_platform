#include "internal.h"
#include "hx_encoding.h"

int hx_os_encoding_available(void)
{
	return HX_OS_ENABLE_ENCODING;
}

static int hx_os_encoding_overlap(
	const void *a, size_t a_bytes, const void *b, size_t b_bytes)
{
	uintptr_t left = (uintptr_t)a, right = (uintptr_t)b;

	return a && b && a_bytes && b_bytes &&
		(left >= right ? left - right < b_bytes
			       : right - left < a_bytes);
}

static hx_os_status hx_os_encoding_check(const void *input, size_t bytes,
	const void *output, size_t capacity, const size_t *required)
{
	if (!HX_OS_ENABLE_ENCODING)
		return HX_OS_NOT_SUPPORTED;
	if ((!input && bytes) || (!output && capacity) || !required)
		return HX_OS_INVALID;
	if (hx_os_encoding_overlap(input, bytes, output, capacity))
		return HX_OS_INVALID;

	return HX_OS_OK;
}

static int hx_os_base64_value(unsigned char value, uint32_t flags)
{
	if (value >= 'A' && value <= 'Z')
		return value - 'A';
	if (value >= 'a' && value <= 'z')
		return value - 'a' + 26;
	if (value >= '0' && value <= '9')
		return value - '0' + 52;
	if (value == ((flags & HX_OS_BASE64_URL) ? '-' : '+'))
		return 62;
	if (value == ((flags & HX_OS_BASE64_URL) ? '_' : '/'))
		return 63;

	return -1;
}

hx_os_status hx_os_base64_encode(const void *input, size_t bytes,
	uint32_t flags, char *output, size_t capacity, size_t *required,
	hx_os_error *error)
{
	const unsigned char *data = input;
	const char *alphabet = (flags & HX_OS_BASE64_URL)
		? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567"
		  "89-_"
		: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567"
		  "89+/";
	hx_os_status status =
		hx_os_encoding_check(input, bytes, output, capacity, required);
	size_t size, extra, i = 0, target = 0;

	if (status == HX_OS_OK && (flags & ~UINT32_C(3)))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (bytes / 3 > (SIZE_MAX - 1) / 4)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	size = bytes / 3 * 4;
	extra = bytes % 3
		? ((flags & HX_OS_BASE64_NO_PADDING) ? bytes % 3 + 1 : 4)
		: 0;
	if (size > SIZE_MAX - extra - 1)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*required = size + extra + 1;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	while (bytes - i >= 3) {
		uint32_t value = ((uint32_t)data[i] << 16) |
			((uint32_t)data[i + 1] << 8) | data[i + 2];

		output[target++] = alphabet[value >> 18];
		output[target++] = alphabet[(value >> 12) & 63];
		output[target++] = alphabet[(value >> 6) & 63];
		output[target++] = alphabet[value & 63];
		i += 3;
	}
	if (i < bytes) {
		uint32_t value = (uint32_t)data[i] << 16;
		int two = bytes - i == 2;

		if (two)
			value |= (uint32_t)data[i + 1] << 8;
		output[target++] = alphabet[value >> 18];
		output[target++] = alphabet[(value >> 12) & 63];
		if (two)
			output[target++] = alphabet[(value >> 6) & 63];
		else if (!(flags & HX_OS_BASE64_NO_PADDING))
			output[target++] = '=';
		if (!(flags & HX_OS_BASE64_NO_PADDING))
			output[target++] = '=';
	}

	output[target] = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_base64_decode(const char *input, size_t bytes,
	uint32_t flags, void *output, size_t capacity, size_t *required,
	hx_os_error *error)
{
	unsigned char *data = output;
	hx_os_status status =
		hx_os_encoding_check(input, bytes, output, capacity, required);
	size_t symbols = bytes, padding = 0, i, target = 0;
	uint32_t value = 0;
	unsigned bits = 0;
	int last = 0;

	if (status == HX_OS_OK && (flags & ~UINT32_C(3)))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (!(flags & HX_OS_BASE64_NO_PADDING)) {
		if (bytes % 4)
			return hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
		while (padding < 2 && symbols && input[symbols - 1] == '=') {
			--symbols;
			++padding;
		}
	}
	if (symbols % 4 == 1)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	for (i = 0; i < symbols; ++i) {
		last = hx_os_base64_value((unsigned char)input[i], flags);
		if (last < 0)
			return hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
	}
	if ((symbols % 4 == 2 && (last & 15)) ||
		(symbols % 4 == 3 && (last & 3)))
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);

	*required = symbols / 4 * 3 + (symbols % 4 ? symbols % 4 - 1 : 0);
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	for (i = 0; i < symbols; ++i) {
		value = (value << 6) |
			(uint32_t)hx_os_base64_value(
				(unsigned char)input[i], flags);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			data[target++] = (unsigned char)(value >> bits);
		}
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static int hx_os_hex_value(unsigned char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;

	return -1;
}

hx_os_status hx_os_hex_encode(const void *input, size_t bytes, uint32_t flags,
	char *output, size_t capacity, size_t *required, hx_os_error *error)
{
	const unsigned char *data = input;
	const char *digits = flags ? "0123456789ABCDEF" : "0123456789abcdef";
	hx_os_status status =
		hx_os_encoding_check(input, bytes, output, capacity, required);
	size_t i;

	if (status == HX_OS_OK && (flags & ~UINT32_C(1)))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (bytes > (SIZE_MAX - 1) / 2)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*required = bytes * 2 + 1;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	for (i = 0; i < bytes; ++i) {
		output[i * 2] = digits[data[i] >> 4];
		output[i * 2 + 1] = digits[data[i] & 15];
	}
	output[bytes * 2] = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_hex_decode(const char *input, size_t bytes, void *output,
	size_t capacity, size_t *required, hx_os_error *error)
{
	unsigned char *data = output;
	hx_os_status status =
		hx_os_encoding_check(input, bytes, output, capacity, required);
	size_t i;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (bytes % 2)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	for (i = 0; i < bytes; ++i)
		if (hx_os_hex_value((unsigned char)input[i]) < 0)
			return hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);

	*required = bytes / 2;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	for (i = 0; i < bytes; i += 2)
		data[i / 2] = (unsigned char)(hx_os_hex_value(
						      (unsigned char)input[i]) *
				16 +
			hx_os_hex_value((unsigned char)input[i + 1]));

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_encoding_string(const void *input, size_t bytes,
	uint32_t flags, hx_string *out, int hex, hx_os_error *error)
{
	hx_string result;
	hx_os_status status;
	size_t required = 0;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hex ? hx_os_hex_encode(
			       input, bytes, flags, NULL, 0, &required, error)
		     : hx_os_base64_encode(
			       input, bytes, flags, NULL, 0, &required, error);
	if (status != HX_OS_OUTPUT_FULL)
		return status;
	if (required - 1 > 16 * 1024 * 1024)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	hx_string_init(&result, out->context);
	status = hx_string_reserve(&result, required - 1, error);
	if (status == HX_OS_OK)
		status = hex
			? hx_os_hex_encode(input, bytes, flags, result.data,
				  result.capacity, &required, error)
			: hx_os_base64_encode(input, bytes, flags, result.data,
				  result.capacity, &required, error);
	if (status == HX_OS_OK) {
		result.size = required - 1;
		hx_string_move(out, &result);
	}

	hx_string_destroy(&result);

	return status;
}

hx_os_status hx_os_base64_encode_string(const void *input, size_t bytes,
	uint32_t flags, hx_string *out, hx_os_error *error)
{
	return hx_os_encoding_string(input, bytes, flags, out, 0, error);
}

hx_os_status hx_os_hex_encode_string(const void *input, size_t bytes,
	uint32_t flags, hx_string *out, hx_os_error *error)
{
	return hx_os_encoding_string(input, bytes, flags, out, 1, error);
}
