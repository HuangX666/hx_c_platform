#ifndef HX_C_PLATFORM_VARINT_H
#define HX_C_PLATFORM_VARINT_H

#include "hx_os_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unsigned base-128 little-endian varints, at most ten bytes. Decoding
 * requires the shortest representation. Failure preserves value and sets
 * consumed=0; trailing bytes after a valid integer are left unread.
 * Encode reports required size through written on OUTPUT_FULL, preserving
 * output. No terminator, allocation or alignment requirement. Output fields
 * must not overlap the input/output byte spans.
 */
HX_OS_API size_t HX_OS_CALL hx_os_varint_size(uint64_t value);
HX_OS_API hx_os_status HX_OS_CALL hx_os_varint_encode(uint64_t value,
	void *output, size_t capacity, size_t *written, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_varint_decode(const void *input,
	size_t bytes, uint64_t *value, size_t *consumed, hx_os_error *error);

/* Defined for the entire int64 range without signed shifts/overflow. */
HX_OS_API uint64_t HX_OS_CALL hx_os_zigzag_encode(int64_t value);
HX_OS_API int64_t HX_OS_CALL hx_os_zigzag_decode(uint64_t value);

/* STREAM is required only by these entries. Reads preserve numeric output,
 * but may consume a prefix. EOF before any byte stays EOF; EOF inside a
 * field is TRUNCATED_INPUT. Writes report actual byte progress. Retrying a
 * whole field after partial I/O is unsafe. No deadline or hidden buffering.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_varuint(
	hx_os_stream *stream, uint64_t *value, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_varuint(
	hx_os_stream *stream, uint64_t value, size_t *completed,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_varint(
	hx_os_stream *stream, int64_t *value, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_varint(
	hx_os_stream *stream, int64_t value, size_t *completed,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
