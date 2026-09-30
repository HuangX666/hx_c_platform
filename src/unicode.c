#include "internal.h"

static uint32_t hx_os_utf_word(
	const unsigned char *input, size_t count, int little)
{
	uint32_t result = 0;
	size_t i;
	for (i = 0; i < count; ++i) {
		size_t index = little ? count - i - 1 : i;
		result = (result << 8) | input[index];
	}
	return result;
}

hx_os_status hx_os_i_utf_decode(hx_os_encoding encoding,
	const unsigned char *input, size_t bytes, uint32_t *point, size_t *used)
{
	uint32_t value;
	size_t count;
	size_t i;
	*used = 0;
	if (!bytes)
		return HX_OS_NEED_INPUT;
	if (encoding == HX_OS_ASCII || encoding == HX_OS_ISO_8859_1) {
		if (encoding == HX_OS_ASCII && input[0] > 127)
			return HX_OS_INVALID_ENCODING;
		*point = input[0];
		*used = 1;
		return HX_OS_OK;
	}
	if (encoding == HX_OS_UTF8) {
		unsigned char lead = input[0];
		if (lead < 0x80) {
			*point = lead;
			*used = 1;
			return HX_OS_OK;
		}
		if (lead >= 0xc2 && lead <= 0xdf)
			count = 2;
		else if (lead >= 0xe0 && lead <= 0xef)
			count = 3;
		else if (lead >= 0xf0 && lead <= 0xf4)
			count = 4;
		else
			return HX_OS_INVALID_ENCODING;
		for (i = 1; i < count && i < bytes; ++i)
			if ((input[i] & 0xc0) != 0x80)
				return HX_OS_INVALID_ENCODING;
		if (bytes > 1 &&
			((lead == 0xe0 && input[1] < 0xa0) ||
				(lead == 0xed && input[1] >= 0xa0) ||
				(lead == 0xf0 && input[1] < 0x90) ||
				(lead == 0xf4 && input[1] >= 0x90)))
			return HX_OS_INVALID_ENCODING;
		if (bytes < count)
			return HX_OS_NEED_INPUT;
		value = lead & (0x7f >> count);
		for (i = 1; i < count; ++i)
			value = (value << 6) | (input[i] & 0x3f);
		if ((count == 2 && value < 0x80) ||
			(count == 3 && value < 0x800) ||
			(count == 4 && value < 0x10000))
			return HX_OS_INVALID_ENCODING;
	}
	else if (encoding == HX_OS_UTF16LE || encoding == HX_OS_UTF16BE) {
		if (bytes < 2)
			return HX_OS_NEED_INPUT;
		value = hx_os_utf_word(input, 2, encoding == HX_OS_UTF16LE);
		count = 2;
		if (value >= 0xd800 && value <= 0xdbff) {
			uint32_t low;
			if (bytes < 4)
				return HX_OS_NEED_INPUT;
			low = hx_os_utf_word(
				input + 2, 2, encoding == HX_OS_UTF16LE);
			if (low < 0xdc00 || low > 0xdfff)
				return HX_OS_INVALID_ENCODING;
			value = 0x10000 + ((value - 0xd800) << 10) + low -
				0xdc00;
			count = 4;
		}
	}
	else if (encoding == HX_OS_UTF32LE || encoding == HX_OS_UTF32BE) {
		if (bytes < 4)
			return HX_OS_NEED_INPUT;
		value = hx_os_utf_word(input, 4, encoding == HX_OS_UTF32LE);
		count = 4;
	}
	else
		return HX_OS_NOT_SUPPORTED;
	if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
		return HX_OS_INVALID_ENCODING;
	*point = value;
	*used = count;
	return HX_OS_OK;
}

hx_os_status hx_os_i_utf_encode(hx_os_encoding encoding, uint32_t point,
	unsigned char *output, size_t capacity, size_t *used)
{
	unsigned char data[8];
	size_t count;
	size_t i;
	*used = 0;
	if (point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
		return HX_OS_INVALID_ENCODING;
	if (encoding == HX_OS_ASCII || encoding == HX_OS_ISO_8859_1) {
		if (point > (encoding == HX_OS_ASCII ? 127u : 255u))
			return HX_OS_UNREPRESENTABLE;
		data[0] = (unsigned char)point;
		count = 1;
	}
	else if (encoding == HX_OS_UTF8) {
		if (point < 0x80) {
			data[0] = (unsigned char)point;
			count = 1;
		}
		else {
			uint32_t value = point;
			count = point < 0x800 ? 2 : (point < 0x10000 ? 3 : 4);
			for (i = count - 1; i > 0; --i) {
				data[i] =
					(unsigned char)(0x80 | (value & 0x3f));
				value >>= 6;
			}
			data[0] = (unsigned char)((count == 2 ? 0xc0
								  : count == 3
								  ? 0xe0
								  : 0xf0) |
				value);
		}
	}
	else if (encoding == HX_OS_UTF16LE || encoding == HX_OS_UTF16BE) {
		uint32_t words[2];
		int little = encoding == HX_OS_UTF16LE;
		if (point < 0x10000) {
			words[0] = point;
			count = 2;
		}
		else {
			words[0] = 0xd800 + ((point - 0x10000) >> 10);
			words[1] = 0xdc00 + ((point - 0x10000) & 0x3ff);
			count = 4;
		}
		for (i = 0; i < count / 2; ++i) {
			data[2 * i + (little ? 0 : 1)] =
				(unsigned char)words[i];
			data[2 * i + (little ? 1 : 0)] =
				(unsigned char)(words[i] >> 8);
		}
	}
	else if (encoding == HX_OS_UTF32LE || encoding == HX_OS_UTF32BE) {
		count = 4;
		for (i = 0; i < count; ++i) {
			size_t index = encoding == HX_OS_UTF32LE ? i : 3 - i;
			data[index] = (unsigned char)(point >> (i * 8));
		}
	}
	else
		return HX_OS_NOT_SUPPORTED;
	if (count > capacity)
		return HX_OS_OUTPUT_FULL;
	memcpy(output, data, count);
	*used = count;
	return HX_OS_OK;
}

hx_os_status hx_os_text_validate_utf8(
	const void *input, size_t bytes, size_t *error_offset)
{
	const unsigned char *data = input;
	size_t offset = 0;
	if ((!input && bytes) || !error_offset)
		return HX_OS_INVALID;
	while (offset < bytes) {
		uint32_t point;
		size_t used;
		hx_os_status status = hx_os_i_utf_decode(HX_OS_UTF8,
			data + offset, bytes - offset, &point, &used);
		if (status != HX_OS_OK) {
			*error_offset = offset;
			return status == HX_OS_NEED_INPUT
				? HX_OS_TRUNCATED_INPUT
				: status;
		}
		offset += used;
	}
	*error_offset = bytes;
	return HX_OS_OK;
}
