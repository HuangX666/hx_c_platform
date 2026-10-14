#ifndef HX_C_PLATFORM_BITSTREAM_H
#define HX_C_PLATFORM_BITSTREAM_H

#include "hx_os_base.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * \brief Stack values borrow the buffer, no allocation or ownership transfer.
 *
 * \details Never modify fields directly. Initialize before use; no concurrent
 * use. Fixed layouts: future extensions use new types rather than growing
 * these.
 */
typedef struct hx_os_bit_reader {
	const unsigned char *data;
	size_t size_bits;
	size_t position_bits;
} hx_os_bit_reader;

typedef struct hx_os_bit_writer {
	unsigned char *data;
	size_t size_bits;
	size_t position_bits;
} hx_os_bit_writer;

/**
 * \brief MSB first within each byte and value; independent of CPU byte order.
 *
 * \details bytes are buffer bytes; bit_count is 0..64. read returns EOF if
 * insufficient bits, write returns OUTPUT_FULL. Failure changes neither cursor
 * nor data/ output. Values too large for bit_count return RANGE; zero bits
 * require 0. Init leaves buffers unchanged. Written bits replace only that
 * interval. NULL buffers are allowed only with zero bytes; size multiplication
 * checked.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bit_reader_init(
	hx_os_bit_reader *reader, const void *data, size_t bytes,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bit_writer_init(
	hx_os_bit_writer *writer, void *data, size_t bytes, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bit_reader_read_bits(
	hx_os_bit_reader *reader, unsigned bit_count, uint64_t *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bit_writer_write_bits(
	hx_os_bit_writer *writer, unsigned bit_count, uint64_t value,
	hx_os_error *error);

/**
 * \brief Reader skips to the next byte; writer zero-pads to it.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bit_reader_align(
	hx_os_bit_reader *reader, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bit_writer_align(
	hx_os_bit_writer *writer, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
