#include "serial_private.h"

#include <windows.h>

hx_os_status hx_os_i_serial_open(hx_os_context *context, const char *device,
	const hx_os_serial_options *options, hx_os_serial *serial,
	hx_os_error *error)
{
	hx_string path;
	wchar_t *wide;
	HANDLE handle;
	DCB settings;
	COMMTIMEOUTS timeouts;
	DWORD code;
	hx_os_status status;

	hx_string_init(&path, context);
	status = hx_string_format(&path, error, "%s%s",
		!strncmp(device, "\\\\.\\", 4) ? "" : "\\\\.\\", device);
	if (status != HX_OS_OK) {
		hx_string_destroy(&path);
		return status;
	}
	wide = hx_os_i_windows_utf16(context, hx_string_c_str(&path), error);
	hx_string_destroy(&path);
	if (!wide)
		return error ? error->status : HX_OS_NO_MEMORY;
	handle = CreateFileW(wide, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	hx_os_memory_free(wide);
	if (handle == INVALID_HANDLE_VALUE)
		return hx_os_i_win32(error, GetLastError());
	memset(&settings, 0, sizeof(settings));
	settings.DCBlength = sizeof(settings);
	if (!GetCommState(handle, &settings))
		goto failed;
	settings.BaudRate = options->baud;
	settings.ByteSize = (BYTE)options->data_bits;
	settings.StopBits = options->stop_bits == 2 ? TWOSTOPBITS : ONESTOPBIT;
	settings.Parity = options->parity == HX_OS_SERIAL_ODD ? ODDPARITY
		: options->parity == HX_OS_SERIAL_EVEN	      ? EVENPARITY
							      : NOPARITY;
	settings.fBinary = TRUE;
	settings.fParity = options->parity != HX_OS_SERIAL_NO_PARITY;
	settings.fOutX = FALSE;
	settings.fInX = FALSE;
	settings.fErrorChar = FALSE;
	settings.fNull = FALSE;
	settings.fAbortOnError = FALSE;
	settings.fOutxDsrFlow = FALSE;
	settings.fDsrSensitivity = FALSE;
	settings.fDtrControl = DTR_CONTROL_DISABLE;
	settings.fOutxCtsFlow = options->flow == HX_OS_SERIAL_RTS_CTS;
	settings.fRtsControl =
		options->flow ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_DISABLE;
	memset(&timeouts, 0, sizeof(timeouts));
	timeouts.ReadIntervalTimeout = MAXDWORD;
	timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
	timeouts.ReadTotalTimeoutConstant = 50;
	if (!SetCommState(handle, &settings) ||
		!SetCommTimeouts(handle, &timeouts))
		goto failed;
	serial->read_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!serial->read_event)
		goto failed;
	serial->write_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!serial->write_event)
		goto failed;
	serial->native = (intptr_t)handle;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
failed:
	code = GetLastError();
	if (serial->read_event)
		CloseHandle(serial->read_event);
	CloseHandle(handle);
	return hx_os_i_win32(error, code);
}

hx_os_status hx_os_i_serial_transfer(hx_os_serial *serial, void *data,
	size_t bytes, int write, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, size_t *completed, hx_os_error *error)
{
	HANDLE handle = (HANDLE)serial->native;
	HANDLE event = write ? serial->write_event : serial->read_event;

	for (;;) {
		OVERLAPPED operation;
		DWORD count = 0, code = 0;
		BOOL success;
		hx_os_status stop = HX_OS_OK;

		if (hx_os_cancel_requested(cancel))
			return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
		memset(&operation, 0, sizeof(operation));
		operation.hEvent = event;
		ResetEvent(event);
		success = write ? WriteFile(handle, data, (DWORD)bytes, &count,
					  &operation)
				: ReadFile(handle, data, (DWORD)bytes, &count,
					  &operation);
		if (!success)
			code = GetLastError();
		if (!success && code != ERROR_IO_PENDING)
			return hx_os_i_win32(error, code);
		if (!success) {
			for (;;) {
				uint32_t remaining = hx_os_i_wait_ms(deadline);
				DWORD waited = WaitForSingleObject(
					event, remaining > 25 ? 25 : remaining);

				if (waited == WAIT_OBJECT_0)
					break;
				if (waited == WAIT_FAILED) {
					code = GetLastError();
					stop = HX_OS_IO_ERROR;
				}
				else if (hx_os_cancel_requested(cancel))
					stop = HX_OS_CANCELLED;
				else if (hx_os_deadline_expired(deadline))
					stop = HX_OS_TIMEOUT;
				if (stop != HX_OS_OK) {
					(void)CancelIoEx(handle, &operation);
					break;
				}
			}
			/* Keep operation and caller buffer alive through
			 * retirement. */
			success = GetOverlappedResult(
				handle, &operation, &count, TRUE);
			if (!success && stop == HX_OS_OK)
				code = GetLastError();
		}
		*completed = count;
		if (stop != HX_OS_OK)
			return hx_os_i_error(error, stop, HX_OS_ERROR_WIN32,
				stop == HX_OS_IO_ERROR
					? code
					: ERROR_OPERATION_ABORTED);
		if (!success)
			return hx_os_i_win32(error, code);
		if (count)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (write)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	}
}

hx_os_status hx_os_i_serial_discard(
	hx_os_serial *serial, int input, int output, hx_os_error *error)
{
	DWORD flags =
		(input ? PURGE_RXCLEAR : 0) | (output ? PURGE_TXCLEAR : 0);

	return PurgeComm((HANDLE)serial->native, flags)
		? hx_os_i_error(error, HX_OS_OK, 0, 0)
		: hx_os_i_win32(error, GetLastError());
}

hx_os_status hx_os_i_serial_close(hx_os_serial *serial, hx_os_error *error)
{
	BOOL success = CloseHandle((HANDLE)serial->native);
	DWORD code = success ? 0 : GetLastError();

	CloseHandle(serial->read_event);
	CloseHandle(serial->write_event);
	return success ? hx_os_i_error(error, HX_OS_OK, 0, 0)
		       : hx_os_i_win32(error, code);
}
