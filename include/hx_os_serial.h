#ifndef HX_C_PLATFORM_SERIAL_H
#define HX_C_PLATFORM_SERIAL_H

#include "hx_os_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_serial hx_os_serial;

enum { HX_OS_SERIAL_NO_PARITY, HX_OS_SERIAL_ODD, HX_OS_SERIAL_EVEN };

enum { HX_OS_SERIAL_NO_FLOW, HX_OS_SERIAL_RTS_CTS };

typedef struct hx_os_serial_options {
	uint32_t size, version, baud;
	unsigned data_bits, stop_bits, parity, flow;
} hx_os_serial_options;

#define HX_OS_SERIAL_OPTIONS_INIT                                              \
	{                                                                      \
		sizeof(hx_os_serial_options), 1, 115200, 8, 1,                 \
			HX_OS_SERIAL_NO_PARITY, HX_OS_SERIAL_NO_FLOW           \
	}

/* Windows COM name (e.g. COM3) or POSIX UTF-8 device path. Common POSIX baud
 * rates use native termios constants; unknown rates are NOT_SUPPORTED.
 * Missing hardware/driver support fails explicitly. Native configuration
 * changes the selected device; coordinate other applications using it.
 * One reader and one writer may run concurrently; serialize each direction.
 * No worker/queue. Buffers remain borrowed until return, including native
 * cancellation retirement. Deadlines/cancel are cooperative; driver cleanup
 * and internal mutex admission can exceed them. This is byte I/O, not frames.
 */
HX_OS_API int HX_OS_CALL hx_os_serial_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_serial_options_init(
	hx_os_serial_options *options, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_serial_open(hx_os_context *context,
	const char *device, const hx_os_serial_options *options,
	hx_os_serial **out, hx_os_error *error);
/* Short progress is valid. Zero requested bytes succeeds. Timeout/cancel
 * preserves actual completed count; only retry the untransferred suffix.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_serial_read_until(hx_os_serial *serial,
	void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_serial_write_until(hx_os_serial *serial,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, size_t *completed, hx_os_error *error);
/* Explicitly discards pending native input/output; exclude I/O callers. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_serial_discard(
	hx_os_serial *serial, int input, int output, hx_os_error *error);
/* Exclude all callers first. Consumes owner even on a native close error;
 * no implicit drain of unsent device bytes or native close retry. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_serial_close(
	hx_os_serial **serial, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
