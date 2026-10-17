#ifndef HX_C_PLATFORM_BUFFERED_FILE_H
#define HX_C_PLATFORM_BUFFERED_FILE_H

#include "hx_os_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_file_writer hx_os_file_writer;

typedef struct hx_os_file_writer_options {
	uint32_t size;
	uint32_t version;
	size_t queue_bytes;
	size_t chunk_bytes;
} hx_os_file_writer_options;

#define HX_OS_FILE_WRITER_OPTIONS_INIT                                         \
	{                                                                      \
		sizeof(hx_os_file_writer_options), 1, 1024 * 1024, 0           \
	}

/**
 * \brief A writer starts one explicit worker.
 *
 * \details Default queue: 1 MiB, preallocated. chunk_bytes=0 selects
 * min(queue_bytes, 64 KiB). No per-write allocation. Thread stack, object and
 * native synchronization storage are additional. The worker exclusively owns
 * the sink; callbacks must return normally. This feature requires STREAM and
 * BUFFERED_FILE, not a thread pool.
 */
HX_OS_API int HX_OS_CALL hx_os_file_writer_available(void);

HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_options_init(
	hx_os_file_writer_options *options, size_t capacity);

/**
 * \brief Creates or truncates a regular UTF-8 path with default
 * options/context.
 *
 * \details Failure after opening may already have created/truncated the file.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_open(
	const char *path, hx_os_file_writer **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_open_ex(
	hx_os_context *context, const char *path, uint32_t flags,
	const hx_os_file_writer_options *options, hx_os_file_writer **out,
	hx_os_error *error);

/**
 * \brief Transfers *sink only on success and sets it NULL.
 *
 * \details The sink must support write, flush(persistent=0) and close. Its user
 * storage lives through close.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_from_stream(
	hx_os_context *context, hx_os_stream **sink,
	const hx_os_file_writer_options *options, hx_os_file_writer **out,
	hx_os_error *error);

/**
 * \brief Copies bytes before returning; accepted is always initialized.
 *
 * \details Calls from multiple producers are serialized, with no interleaving
 * inside a call. A timeout/error can accept a prefix: retry only the remaining
 * suffix. Acceptance is not native completion or persistence. The queue
 * includes bytes currently being written. NOW accepts immediately available
 * space; INFINITE waits without a deadline. No FIFO promise among waiting
 * producers. An outstanding sink failure stops acceptance until retry is
 * requested.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_write_until(
	hx_os_file_writer *writer, const void *data, size_t bytes,
	size_t *accepted, hx_os_deadline deadline, hx_os_error *error);

/**
 * \brief Waits for bytes accepted at entry, then calls the sink's flush.
 *
 * \details New writes do not extend the target. persistent=1 requests
 * durability, not a hardware guarantee. A timeout may leave a flush in flight;
 * the object remains valid. Flush calls serialize; only one flush request
 * occupies internal storage.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_flush_until(
	hx_os_file_writer *writer, int persistent, hx_os_deadline deadline,
	hx_os_error *error);

/**
 * \brief Clears a latched write/flush error and retries the unwritten suffix.
 *
 * \details Already written bytes are never replayed. Diagnose/fix the cause
 * first. Also valid after close began. A terminal worker failure returns
 * CLOSED.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_retry(
	hx_os_file_writer *writer, hx_os_error *error);

/**
 * \brief Reports queued bytes (including in-flight bytes) and any latched
 * failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_pending(
	hx_os_file_writer *writer, size_t *bytes, hx_os_error *error);

/**
 * \brief Stops acceptance and wakes producers while retaining the object.
 *
 * \details May run concurrently with write/flush. Use before joining producer
 * threads, then exclude all public calls before final close. Repeated stop is
 * harmless.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_stop(
	hx_os_file_writer *writer, hx_os_error *error);

/**
 * \brief Close stops acceptance, drains, flushes user buffers and closes the
 * sink.
 *
 * \details A write/flush error or timeout retains *writer for retry/abort. A
 * sink close error consumes *writer after the worker has been reclaimed.
 * A worker join/release error retains *writer for retry without closing the
 * sink twice. No implicit persistence request. Abort explicitly discards
 * remaining bytes after the current native operation returns; it cannot cancel
 * or kill a blocked native call. Both
 * accept NOW/INFINITE and can be retried on the same owner. Final close/abort
 * requires external exclusion of all other public calls, including calls
 * waiting to enter; stop() can wake producers beforehand. Sink callbacks cannot
 * wait on this writer (DEADLOCK).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_close_until(
	hx_os_file_writer **writer, hx_os_deadline deadline,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_file_writer_abort_until(
	hx_os_file_writer **writer, hx_os_deadline deadline,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
