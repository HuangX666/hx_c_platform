#ifndef HX_C_PLATFORM_FRAME_H
#define HX_C_PLATFORM_FRAME_H

#include "hx_bytes.h"
#include "hx_os_stream.h"
#include "hx_os_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_frame hx_os_frame;
typedef hx_os_status(HX_OS_CALL *hx_os_frame_read_fn)(void *user, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_frame_write_fn)(void *user,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_frame_flush_fn)(
	void *user, hx_os_deadline deadline, hx_os_error *error);

typedef struct hx_os_frame_callbacks {
	hx_os_frame_read_fn read;
	hx_os_frame_write_fn write;
	hx_os_frame_flush_fn flush;
} hx_os_frame_callbacks;

/* Wire: four-byte big-endian unsigned length, followed by exact payload.
 * No device/request/session protocol or authentication is added. Empty
 * messages are valid. Default maximum is 1 MiB, up to 64 MiB. Read/write
 * state each retain at most that payload, excluding output copies/overhead.
 * Callbacks are copied; user/backend is borrowed until destruction and must
 * not retain I/O buffers or reenter this frame. Native errors are retained.
 * Directions serialize independently; backend must allow full duplex for
 * concurrent read/write. Stream adapters require external serialization.
 * Mutex admission, native I/O and flush are not hard deadline guarantees.
 */
HX_OS_API int HX_OS_CALL hx_os_frame_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_create(hx_os_context *context,
	const hx_os_frame_callbacks *callbacks, void *user, size_t max_bytes,
	hx_os_frame **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_from_stream(
	hx_os_context *context, hx_os_stream *stream, size_t max_bytes,
	hx_os_frame **out, hx_os_error *error);
/* Separate read/write streams permit pipe pairs without callback glue.
 * NULL selects an absent direction; at least one must be supplied. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_from_streams(
	hx_os_context *context, hx_os_stream *reader, hx_os_stream *writer,
	size_t max_bytes, hx_os_frame **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_from_socket(
	hx_os_context *context, hx_os_socket *socket, size_t max_bytes,
	hx_os_frame **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_from_tls(hx_os_context *context,
	hx_os_tls *tls, uint64_t peer, size_t max_bytes, hx_os_frame **out,
	hx_os_error *error);

/* Copies the complete message before sending. *accepted is 0 on failure
 * before this copy, 1 once owned (also for empty messages). On timeout after
 * acceptance, retry flush, NOT send of the same message. Later sends first
 * flush an older accepted frame before admitting their own message. Delivery
 * remains uncertain until peer acknowledgement. No silent retransmission.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_send_until(hx_os_frame *frame,
	hx_os_bytes_view message, hx_os_deadline deadline, int *accepted,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_send_text_until(
	hx_os_frame *frame, hx_string_view message, hx_os_deadline deadline,
	int *accepted, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_flush_until(
	hx_os_frame *frame, hx_os_deadline deadline, hx_os_error *error);

/* Returns one full message, retaining partial framing on timeout and output
 * allocation failure. Outputs keep their old value on failure. Clean EOF
 * before a new header is EOF; partial EOF is TRUNCATED_INPUT. Excessive length
 * poisons reading, requiring transport abandonment. Invalid text preserves
 * the received frame: retry bytes_receive to inspect the raw payload.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_receive_until(hx_os_frame *frame,
	hx_os_deadline deadline, hx_bytes *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_receive_text_until(
	hx_os_frame *frame, hx_os_deadline deadline, hx_string *out,
	hx_os_error *error);
/* Abort explicitly discards pending state. Afterwards the borrowed transport
 * must be abandoned/closed by its owner; it cannot resume this wire stream.
 * Exclude callers before abort/destroy. Destroy returns BUSY while partial
 * framing is retained; flush/receive or abort first. It never closes backend.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_abort(
	hx_os_frame *frame, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_frame_destroy(
	hx_os_frame **frame, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
