#ifndef HX_C_PLATFORM_SERIAL_PRIVATE_H
#define HX_C_PLATFORM_SERIAL_PRIVATE_H

#include "internal.h"
#include "hx_os_serial.h"

struct hx_os_serial {
	hx_os_mutex *read_mutex, *write_mutex;
	intptr_t native;
#ifdef _WIN32
	void *read_event, *write_event;
#endif
};

hx_os_status hx_os_i_serial_open(hx_os_context *context, const char *device,
	const hx_os_serial_options *options, hx_os_serial *serial,
	hx_os_error *error);
hx_os_status hx_os_i_serial_transfer(hx_os_serial *serial, void *data,
	size_t bytes, int write, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, size_t *completed, hx_os_error *error);
hx_os_status hx_os_i_serial_discard(
	hx_os_serial *serial, int input, int output, hx_os_error *error);
hx_os_status hx_os_i_serial_close(hx_os_serial *serial, hx_os_error *error);
#endif
