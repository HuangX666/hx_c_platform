#include "internal.h"
#include "hx_os_stream.h"
#include <float.h>

static int hx_os_stream_number_valid(unsigned width, int order)
{
	return width >= 1 && width <= 8 &&
		(order == HX_OS_LITTLE_ENDIAN || order == HX_OS_BIG_ENDIAN);
}

hx_os_status hx_os_stream_read_uint(hx_os_stream *stream, unsigned width,
	int order, uint64_t *out, hx_os_error *error)
{
	unsigned char data[8];
	unsigned i;
	size_t completed;
	uint64_t value = 0;
	hx_os_status status;

	if (!out || !hx_os_stream_number_valid(width, order))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status =
		hx_os_stream_read_exact(stream, data, width, &completed, error);
	if (status != HX_OS_OK)
		return status;

	for (i = 0; i < width; ++i) {
		unsigned shift =
			(order == HX_OS_LITTLE_ENDIAN ? i : width - i - 1) * 8;

		value |= (uint64_t)data[i] << shift;
	}

	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_stream_write_uint(hx_os_stream *stream, unsigned width,
	int order, uint64_t value, hx_os_error *error)
{
	unsigned char data[8];
	unsigned i;
	size_t completed;

	if (!hx_os_stream_number_valid(width, order))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (width < 8 && value >> (width * 8))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	for (i = 0; i < width; ++i) {
		unsigned shift =
			(order == HX_OS_LITTLE_ENDIAN ? i : width - i - 1) * 8;

		data[i] = (unsigned char)(value >> shift);
	}

	return hx_os_stream_write_all(stream, data, width, &completed, error);
}

hx_os_status hx_os_stream_read_int(hx_os_stream *stream, unsigned width,
	int order, int64_t *out, hx_os_error *error)
{
	uint64_t value;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_stream_read_uint(stream, width, order, &value, error);
	if (status != HX_OS_OK)
		return status;

	if (width < 8 && (value & (UINT64_C(1) << (width * 8 - 1))))
		value |= UINT64_MAX << (width * 8);

	*out = value <= INT64_MAX ? (int64_t)value : -(int64_t)(~value) - 1;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_stream_write_int(hx_os_stream *stream, unsigned width,
	int order, int64_t value, hx_os_error *error)
{
	uint64_t encoded;

	if (!hx_os_stream_number_valid(width, order))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (width < 8) {
		int64_t bound = INT64_C(1) << (width * 8 - 1);

		if (value < -bound || value >= bound)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	encoded = (uint64_t)value & (UINT64_MAX >> (64 - width * 8));
	return hx_os_stream_write_uint(stream, width, order, encoded, error);
}

static int hx_os_stream_binary32(void)
{
	float one = 1.0f;
	uint32_t bits = 0;

	if (sizeof(one) != sizeof(bits) || FLT_RADIX != 2 ||
		FLT_MANT_DIG != 24 || FLT_MAX_EXP != 128)
		return 0;

	memcpy(&bits, &one, sizeof(bits));
	return bits == UINT32_C(0x3f800000);
}

static int hx_os_stream_binary64(void)
{
	double one = 1.0;
	uint64_t bits = 0;

	if (sizeof(one) != sizeof(bits) || FLT_RADIX != 2 ||
		DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024)
		return 0;

	memcpy(&bits, &one, sizeof(bits));
	return bits == UINT64_C(0x3ff0000000000000);
}

hx_os_status hx_os_stream_read_f32(
	hx_os_stream *stream, int order, float *out, hx_os_error *error)
{
	uint64_t value;
	uint32_t bits;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_stream_binary32())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_stream_read_uint(stream, 4, order, &value, error);
	if (status == HX_OS_OK) {
		bits = (uint32_t)value;
		memcpy(out, &bits, sizeof(bits));
	}

	return status;
}

hx_os_status hx_os_stream_write_f32(
	hx_os_stream *stream, int order, float value, hx_os_error *error)
{
	uint32_t bits;

	if (!hx_os_stream_binary32())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	memcpy(&bits, &value, sizeof(bits));
	return hx_os_stream_write_uint(stream, 4, order, bits, error);
}

hx_os_status hx_os_stream_read_f64(
	hx_os_stream *stream, int order, double *out, hx_os_error *error)
{
	uint64_t bits;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_stream_binary64())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_stream_read_uint(stream, 8, order, &bits, error);
	if (status == HX_OS_OK)
		memcpy(out, &bits, sizeof(bits));

	return status;
}

hx_os_status hx_os_stream_write_f64(
	hx_os_stream *stream, int order, double value, hx_os_error *error)
{
	uint64_t bits;

	if (!hx_os_stream_binary64())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	memcpy(&bits, &value, sizeof(bits));
	return hx_os_stream_write_uint(stream, 8, order, bits, error);
}
