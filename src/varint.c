#include "internal.h"
#include "hx_os_varint.h"

size_t hx_os_varint_size(uint64_t value)
{
	size_t size = 1;

	while (value >= 128) {
		value >>= 7;
		++size;
	}

	return size;
}

hx_os_status hx_os_varint_encode(uint64_t value, void *output,
	size_t capacity, size_t *written, hx_os_error *error)
{
	unsigned char encoded[10];
	size_t size = 0;

	if (!written || (!output && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	do {
		unsigned char byte = (unsigned char)(value & 127);

		value >>= 7;
		encoded[size++] = (unsigned char)(byte | (value ? 128 : 0));
	} while (value);

	*written = size;
	if (capacity < size)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	memcpy(output, encoded, size);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_varint_decode(const void *input, size_t bytes,
	uint64_t *value, size_t *consumed, hx_os_error *error)
{
	const unsigned char *data = input;
	uint64_t result = 0;
	size_t i;

	if (consumed)
		*consumed = 0;
	if ((!input && bytes) || !value || !consumed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (i = 0; i < bytes && i < 10; ++i) {
		unsigned byte = data[i];

		if (i == 9 && byte > 1)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		result |= (uint64_t)(byte & 127) << (7 * i);
		if (!(byte & 128)) {
			if (i && !byte)
				return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

			*value = result;
			*consumed = i + 1;
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}

	return hx_os_i_error(error, HX_OS_TRUNCATED_INPUT, 0, 0);
}

uint64_t hx_os_zigzag_encode(int64_t value)
{
	return value < 0 ? (uint64_t)(-(value + 1)) * 2 + 1
			 : (uint64_t)value * 2;
}

int64_t hx_os_zigzag_decode(uint64_t value)
{
	return value & 1 ? -(int64_t)(value >> 1) - 1
			 : (int64_t)(value >> 1);
}

hx_os_status hx_os_stream_read_varuint(
	hx_os_stream *stream, uint64_t *value, hx_os_error *error)
{
	unsigned char data[10];
	size_t i, used, consumed;
	hx_os_status status;

	if (!stream || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (i = 0; i < sizeof(data); ++i) {
		status = hx_os_stream_read_exact(
			stream, data + i, 1, &used, error);
		if (status != HX_OS_OK) {
			if (status == HX_OS_EOF && i)
				return hx_os_i_error(
					error, HX_OS_TRUNCATED_INPUT, 0, 0);
			return status;
		}
		if (!(data[i] & 128) || i == 9)
			return hx_os_varint_decode(
				data, i + 1, value, &consumed, error);
	}

	return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
}

hx_os_status hx_os_stream_write_varuint(hx_os_stream *stream, uint64_t value,
	size_t *completed, hx_os_error *error)
{
	unsigned char data[10];
	size_t bytes;

	if (completed)
		*completed = 0;
	if (!stream || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	(void)hx_os_varint_encode(value, data, sizeof(data), &bytes, NULL);
	return hx_os_stream_write_all(stream, data, bytes, completed, error);
}

hx_os_status hx_os_stream_read_varint(
	hx_os_stream *stream, int64_t *value, hx_os_error *error)
{
	uint64_t encoded;
	hx_os_status status;

	if (!value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_stream_read_varuint(stream, &encoded, error);
	if (status == HX_OS_OK)
		*value = hx_os_zigzag_decode(encoded);

	return status;
}

hx_os_status hx_os_stream_write_varint(hx_os_stream *stream, int64_t value,
	size_t *completed, hx_os_error *error)
{
	return hx_os_stream_write_varuint(
		stream, hx_os_zigzag_encode(value), completed, error);
}
