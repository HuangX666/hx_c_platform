#include "serial_private.h"

int hx_os_serial_available(void)
{
	return HX_OS_ENABLE_SERIAL;
}

hx_os_status hx_os_serial_options_init(
	hx_os_serial_options *options, size_t capacity)
{
	const hx_os_serial_options defaults = HX_OS_SERIAL_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;
	return HX_OS_OK;
}

hx_os_status hx_os_serial_open(hx_os_context *context, const char *device,
	const hx_os_serial_options *options, hx_os_serial **out,
	hx_os_error *error)
{
	const hx_os_serial_options defaults = HX_OS_SERIAL_OPTIONS_INIT;
	hx_os_serial *serial;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_SERIAL)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!options)
		options = &defaults;
	if (!device || options->size < sizeof(*options) ||
		options->version != 1 || !options->baud ||
		options->data_bits < 5 || options->data_bits > 8 ||
		options->stop_bits < 1 || options->stop_bits > 2 ||
		options->parity > HX_OS_SERIAL_EVEN ||
		options->flow > HX_OS_SERIAL_RTS_CTS)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(device, error);
	if (status != HX_OS_OK)
		return status;
	serial = hx_os_i_zero(context, sizeof(*serial));
	if (!serial)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_mutex_create(context, 0, &serial->read_mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(
			context, 0, &serial->write_mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_i_serial_open(
			context, device, options, serial, error);
	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(serial->read_mutex);
		hx_os_mutex_destroy(serial->write_mutex);
		hx_os_memory_free(serial);
		return status;
	}
	*out = serial;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_serial_transfer(hx_os_serial *serial, void *data,
	size_t bytes, int write, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, size_t *completed, hx_os_error *error)
{
	hx_os_mutex *mutex;
	hx_os_status status;

	if (completed)
		*completed = 0;
	if (!HX_OS_ENABLE_SERIAL)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!serial || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (bytes > INT_MAX)
		bytes = INT_MAX;
	mutex = write ? serial->write_mutex : serial->read_mutex;
	status = hx_os_mutex_lock(mutex, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_serial_transfer(
		serial, data, bytes, write, deadline, cancel, completed, error);
	(void)hx_os_mutex_unlock(mutex, NULL);
	return status;
}

hx_os_status hx_os_serial_read_until(hx_os_serial *serial, void *data,
	size_t bytes, hx_os_deadline deadline, hx_os_cancel_token *cancel,
	size_t *completed, hx_os_error *error)
{
	return hx_os_serial_transfer(
		serial, data, bytes, 0, deadline, cancel, completed, error);
}

hx_os_status hx_os_serial_write_until(hx_os_serial *serial, const void *data,
	size_t bytes, hx_os_deadline deadline, hx_os_cancel_token *cancel,
	size_t *completed, hx_os_error *error)
{
	return hx_os_serial_transfer(serial, (void *)(uintptr_t)data, bytes, 1,
		deadline, cancel, completed, error);
}

hx_os_status hx_os_serial_discard(
	hx_os_serial *serial, int input, int output, hx_os_error *error)
{
	if (!HX_OS_ENABLE_SERIAL)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!serial || (input != 0 && input != 1) ||
		(output != 0 && output != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!input && !output)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	return hx_os_i_serial_discard(serial, input, output, error);
}

hx_os_status hx_os_serial_close(hx_os_serial **serial, hx_os_error *error)
{
	hx_os_serial *value;
	hx_os_status status;

	if (!serial)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *serial;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	*serial = NULL;
	status = hx_os_i_serial_close(value, error);
	hx_os_mutex_destroy(value->read_mutex);
	hx_os_mutex_destroy(value->write_mutex);
	hx_os_memory_free(value);
	return status;
}
