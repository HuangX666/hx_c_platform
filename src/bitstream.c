#include "internal.h"
#include "hx_os_bitstream.h"

hx_os_status hx_os_bit_reader_init(hx_os_bit_reader *reader, const void *data,
	size_t bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_BITSTREAM
	if (!reader || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (bytes > SIZE_MAX / 8)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	reader->data = data;
	reader->size_bits = bytes * 8;
	reader->position_bits = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)reader;
	(void)data;
	(void)bytes;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_bit_writer_init(
	hx_os_bit_writer *writer, void *data, size_t bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_BITSTREAM
	if (!writer || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (bytes > SIZE_MAX / 8)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	writer->data = data;
	writer->size_bits = bytes * 8;
	writer->position_bits = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)writer;
	(void)data;
	(void)bytes;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_bit_reader_read_bits(hx_os_bit_reader *reader,
	unsigned bit_count, uint64_t *out, hx_os_error *error)
{
#if HX_OS_ENABLE_BITSTREAM
	uint64_t value = 0;
	size_t i;

	if (!reader || !out || bit_count > 64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (bit_count > reader->size_bits - reader->position_bits)
		return hx_os_i_error(error, HX_OS_EOF, 0, 0);

	for (i = 0; i < bit_count; ++i) {
		size_t bit = reader->position_bits + i;

		value = (value << 1) |
			((reader->data[bit / 8] >> (7 - bit % 8)) & 1u);
	}

	reader->position_bits += bit_count;
	*out = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)reader;
	(void)bit_count;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_bit_writer_write_bits(hx_os_bit_writer *writer,
	unsigned bit_count, uint64_t value, hx_os_error *error)
{
#if HX_OS_ENABLE_BITSTREAM
	size_t i;

	if (!writer || bit_count > 64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (bit_count < 64 && value >> bit_count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	if (bit_count > writer->size_bits - writer->position_bits)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	for (i = 0; i < bit_count; ++i) {
		size_t bit = writer->position_bits + i;
		unsigned char mask = (unsigned char)(1u << (7 - bit % 8));
		unsigned char set =
			(unsigned char)((value >> (bit_count - i - 1)) & 1u);

		writer->data[bit / 8] =
			(unsigned char)((writer->data[bit / 8] &
						(unsigned char)~mask) |
				(set ? mask : 0));
	}

	writer->position_bits += bit_count;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)writer;
	(void)bit_count;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_bit_reader_align(
	hx_os_bit_reader *reader, hx_os_error *error)
{
#if HX_OS_ENABLE_BITSTREAM
	uint64_t ignored;
	unsigned bits =
		reader ? (unsigned)((8 - reader->position_bits % 8) % 8) : 0;

	return hx_os_bit_reader_read_bits(reader, bits, &ignored, error);
#else
	(void)reader;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_bit_writer_align(
	hx_os_bit_writer *writer, hx_os_error *error)
{
#if HX_OS_ENABLE_BITSTREAM
	unsigned bits =
		writer ? (unsigned)((8 - writer->position_bits % 8) % 8) : 0;

	return hx_os_bit_writer_write_bits(writer, bits, 0, error);
#else
	(void)writer;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
