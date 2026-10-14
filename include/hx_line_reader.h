#ifndef HX_C_PLATFORM_LINE_READER_H
#define HX_C_PLATFORM_LINE_READER_H

#include "hx_os_stream.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_line_reader hx_os_line_reader;

HX_OS_API int HX_OS_CALL hx_os_line_reader_available(void);

/**
 * \brief UTF-8 lines with a 4 KiB read-ahead buffer and bounded line staging.
 *
 * \details max_line_bytes=0 selects 1 MiB, measured after BOM/newline removal.
 * from_stream borrows the stream exclusively until reader close. Read-ahead
 * advances its position beyond the last returned line; never mix raw reads,
 * seeks or close with reader use. Closing does not rewind a borrowed stream.
 * open accepts a UTF-8 path and owns the internally opened read-only stream. No
 * worker, global state or internal lock; callbacks cannot reenter reader.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_line_reader_from_stream(
	hx_os_context *context, hx_os_stream *stream, size_t max_line_bytes,
	hx_os_line_reader **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_line_reader_open(hx_os_context *context,
	const char *path, size_t max_line_bytes, hx_os_line_reader **out,
	hx_os_error *error);

/**
 * \brief LF/CRLF removed, lone CR retained; only the very first UTF-8 BOM is
 * removed.
 *
 * \details Every returned line is validated UTF-8, with embedded NUL allowed.
 * EOF may first return one final line without LF; later calls return EOF. Empty
 * files produce no line, and trailing LF does not create an additional empty
 * line. Success uses out's context; all failures/EOF preserve out. out storage
 * must not back the borrowed stream. A pending complete line survives
 * allocation failure for retry. Partial native I/O progress is retained; errors
 * preserve native details and may be retried, without promising backend
 * recovery. LIMIT/invalid UTF-8 stays latched until skip_line or close.
 * skip_line drops only that physical line, including when its LF was already
 * consumed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_line_reader_next(
	hx_os_line_reader *reader, hx_string *out, hx_os_error *error);

/**
 * \brief Explicitly discard the current/next physical line without validating
 * it.
 *
 * \details Can block reading until LF/EOF. After an I/O failure, retry
 * skip_line; next returns BUSY until the interrupted discard completes. EOF is
 * returned only if there was no line to discard. No extra line-sized
 * allocation.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_line_reader_skip_line(
	hx_os_line_reader *reader, hx_os_error *error);

/**
 * \brief Consumes reader even when an owned stream's close fails.
 *
 * \details Borrowed streams remain open. BUSY from reentry leaves reader alive.
 * No implicit seek.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_line_reader_close(
	hx_os_line_reader **reader, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
