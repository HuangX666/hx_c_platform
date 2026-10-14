#ifndef HX_C_PLATFORM_STREAM_H
#define HX_C_PLATFORM_STREAM_H

#include "hx_os_file.h"
#include "hx_os_bits.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_stream hx_os_stream;

typedef hx_os_status(HX_OS_CALL *hx_os_stream_read_fn)(void *user, void *data,
	size_t bytes, size_t *completed, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_stream_write_fn)(void *user,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_stream_seek_fn)(void *user,
	int64_t offset, int origin, uint64_t *position, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_stream_flush_fn)(
	void *user, int persistent, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_stream_close_fn)(
	void *user, hx_os_error *error);

/**
 * \brief Fixed layout.
 *
 * \details The copied callbacks use call-borrowed buffers and must not retain
 * them, throw, longjmp or reenter this stream. Short progress is valid. Missing
 * nonempty operations return NOT_SUPPORTED. Zero-byte transfers succeed without
 * calling the backend. close consumes backend resources even on failure.
 * Rejected creation calls no callbacks and takes no user ownership; accepted
 * creation borrows user until close returns.
 */
typedef struct hx_os_stream_callbacks {
	hx_os_stream_read_fn read;
	hx_os_stream_write_fn write;
	hx_os_stream_seek_fn seek;
	hx_os_stream_flush_fn flush;
	hx_os_stream_close_fn close;
} hx_os_stream_callbacks;

/**
 * \brief Streams are externally serialized.
 *
 * \details I/O buffers must not overlap the memory borrowed or owned by a
 * memory stream. File/callback operations may block; there is no hidden thread,
 * queue, hard deadline or cancellation promise.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_open(hx_os_context *context,
	const char *path, uint32_t flags, hx_os_stream **out,
	hx_os_error *error);

/**
 * \brief On success transfers *file to the stream and sets it NULL.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_from_file(hx_os_context *context,
	hx_os_file **file, hx_os_stream **out, hx_os_error *error);

/**
 * \brief Borrows read-only bytes through close.
 *
 * \details Input must remain unchanged.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_from_memory(
	hx_os_context *context, const void *data, size_t bytes,
	hx_os_stream **out, hx_os_error *error);

/**
 * \brief Owned, readable and writable memory.
 *
 * \details max_bytes=0 selects 16 MiB. Growth can temporarily hold old+new
 * buffers (at most twice the limit). Seeking beyond size is allowed within the
 * limit; a later write zero-fills the gap. No buffer is allocated until a
 * nonempty write requires it.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_memory(hx_os_context *context,
	size_t max_bytes, hx_os_stream **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_create(hx_os_context *context,
	const hx_os_stream_callbacks *callbacks, void *user, hx_os_stream **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_some(hx_os_stream *stream,
	void *data, size_t bytes, size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_some(hx_os_stream *stream,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);

/**
 * \brief Exact operations loop over short progress.
 *
 * \details EOF before any byte stays EOF; EOF after a prefix becomes
 * TRUNCATED_INPUT. Errors preserve actual progress in completed; the
 * destination/stream position may already have changed. OK with zero progress
 * from a nonempty callback operation is IO_ERROR.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_exact(hx_os_stream *stream,
	void *data, size_t bytes, size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_all(hx_os_stream *stream,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_seek(hx_os_stream *stream,
	int64_t offset, int origin, uint64_t *position, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_tell(
	hx_os_stream *stream, uint64_t *position, hx_os_error *error);

/**
 * \brief persistent=0 drains backend user buffers only.
 *
 * \details Files already use unbuffered native I/O. persistent=1 requests file
 * synchronization; memory streams do not support persistence. Neither mode
 * promises hardware durability.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_flush(
	hx_os_stream *stream, int persistent, hx_os_error *error);

/**
 * \brief Memory streams only; borrowed until mutation or close, no terminator.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_data(hx_os_stream *stream,
	const void **data, size_t *bytes, hx_os_error *error);

/**
 * \brief Consumes *stream even on a backend close failure.
 *
 * \details No implicit durable flush. A callback reentering close receives BUSY
 * without consuming the stream.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_close(
	hx_os_stream **stream, hx_os_error *error);

/**
 * \brief Integer widths are 1..8 bytes, including 24-bit fields.
 *
 * \details Signed encoding is two's complement. order is HX_OS_LITTLE_ENDIAN or
 * HX_OS_BIG_ENDIAN. Numeric reads preserve *out on failure, but can consume a
 * partial field. Numeric writes can leave a prefix on failure; do not blindly
 * retry them. Use write_all with completed when transfer-level recovery is
 * needed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_uint(hx_os_stream *stream,
	unsigned width, int order, uint64_t *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_uint(hx_os_stream *stream,
	unsigned width, int order, uint64_t value, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_int(hx_os_stream *stream,
	unsigned width, int order, int64_t *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_int(hx_os_stream *stream,
	unsigned width, int order, int64_t value, hx_os_error *error);

/**
 * \brief IEEE binary32/binary64 only; unsupported host representations are
 * rejected.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_f32(
	hx_os_stream *stream, int order, float *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_f32(
	hx_os_stream *stream, int order, float value, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_read_f64(
	hx_os_stream *stream, int order, double *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_stream_write_f64(
	hx_os_stream *stream, int order, double value, hx_os_error *error);

#define HX_OS_STREAM_INTEGER(suffix, type, wide, base, bytes)                  \
	static inline hx_os_status HX_OS_CALL hx_os_stream_read_##suffix(      \
		hx_os_stream *stream, int order, type *out,                    \
		hx_os_error *error)                                            \
	{                                                                      \
		wide value;                                                    \
		hx_os_status status = hx_os_stream_read_##base(                \
			stream, bytes, order, out ? &value : NULL, error);     \
		if (status == HX_OS_OK)                                        \
			*out = (type)value;                                    \
		return status;                                                 \
	}                                                                      \
	static inline hx_os_status HX_OS_CALL hx_os_stream_write_##suffix(     \
		hx_os_stream *stream, int order, type value,                   \
		hx_os_error *error)                                            \
	{                                                                      \
		return hx_os_stream_write_##base(                              \
			stream, bytes, order, (wide)value, error);             \
	}

HX_OS_STREAM_INTEGER(u8, uint8_t, uint64_t, uint, 1)
HX_OS_STREAM_INTEGER(u16, uint16_t, uint64_t, uint, 2)
HX_OS_STREAM_INTEGER(u24, uint32_t, uint64_t, uint, 3)
HX_OS_STREAM_INTEGER(u32, uint32_t, uint64_t, uint, 4)
HX_OS_STREAM_INTEGER(u64, uint64_t, uint64_t, uint, 8)
HX_OS_STREAM_INTEGER(i8, int8_t, int64_t, int, 1)
HX_OS_STREAM_INTEGER(i16, int16_t, int64_t, int, 2)
HX_OS_STREAM_INTEGER(i24, int32_t, int64_t, int, 3)
HX_OS_STREAM_INTEGER(i32, int32_t, int64_t, int, 4)
HX_OS_STREAM_INTEGER(i64, int64_t, int64_t, int, 8)

#undef HX_OS_STREAM_INTEGER

#ifdef __cplusplus
}
#endif
#endif
