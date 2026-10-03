#include "pipe_private.h"
#include "file_private.h"
#if HX_OS_ENABLE_LOOP
#include "loop_private.h"
#endif
#include <stdio.h>

static hx_os_atomic hx_os_pipe_serial;

hx_os_status hx_os_i_pipe_pair(int read_async, int write_async,
	hx_os_i_pipe_handle handles[2], hx_os_error *error)
{
	HANDLE reader, writer;
	DWORD code = 0;
	if (!read_async && !write_async) {
		if (!CreatePipe(&reader, &writer, NULL, 0))
			return hx_os_i_win32(error, GetLastError());
	}
	else {
		wchar_t name[160];
		OVERLAPPED connect;
		ULONG client_pid = 0;
		unsigned long long serial =
			(unsigned long long)hx_os_atomic_fetch_add(
				&hx_os_pipe_serial, 1, HX_OS_RELAXED);
		(void)swprintf(name, sizeof(name) / sizeof(*name),
			L"\\\\.\\pipe\\hx-platform-%lu-%llx-%llx",
			(unsigned long)GetCurrentProcessId(),
			(unsigned long long)hx_os_monotonic_ns(), serial);
		reader = CreateNamedPipeW(name,
			PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE |
				(read_async ? FILE_FLAG_OVERLAPPED : 0),
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
				PIPE_REJECT_REMOTE_CLIENTS,
			1, 65536, 65536, 0, NULL);
		if (reader == INVALID_HANDLE_VALUE)
			return hx_os_i_win32(error, GetLastError());
		writer = CreateFileW(name, GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING,
			(write_async ? FILE_FLAG_OVERLAPPED : 0) |
				SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
			NULL);
		if (writer == INVALID_HANDLE_VALUE) {
			code = GetLastError();
			(void)CloseHandle(reader);
			return hx_os_i_win32(error, code);
		}
		memset(&connect, 0, sizeof(connect));
		if (read_async) {
			connect.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
			if (!connect.hEvent)
				code = GetLastError();
		}
		if (!code &&
			!ConnectNamedPipe(
				reader, read_async ? &connect : NULL)) {
			code = GetLastError();
			if (code == ERROR_PIPE_CONNECTED)
				code = 0;
			else if (code == ERROR_IO_PENDING) {
				DWORD bytes;
				/* The client is already open; retire any
				 * unexpected pending connect before releasing
				 * its stack state. */
				(void)CancelIoEx(reader, &connect);
				(void)GetOverlappedResult(
					reader, &connect, &bytes, TRUE);
				code = ERROR_PIPE_NOT_CONNECTED;
			}
		}
		if (connect.hEvent)
			(void)CloseHandle(connect.hEvent);
		if (!code && !GetNamedPipeClientProcessId(reader, &client_pid))
			code = GetLastError();
		if (!code && client_pid != GetCurrentProcessId())
			code = ERROR_ACCESS_DENIED;
	}
	if (!code &&
		(!SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0) ||
			!SetHandleInformation(writer, HANDLE_FLAG_INHERIT, 0)))
		code = GetLastError();
	if (code) {
		(void)CloseHandle(reader);
		(void)CloseHandle(writer);
		return hx_os_i_win32(error, code);
	}
	handles[0] = reader;
	handles[1] = writer;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pipe_file(hx_os_context *context, HANDLE native,
	hx_os_file **out, hx_os_error *error)
{
	hx_os_file *file = hx_os_i_zero(context, sizeof(*file));
	if (!file)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	file->handle = native;
	file->pipe = 1;
	*out = file;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pipe_dispose(HANDLE handle, hx_os_error *error)
{
	return hx_os_i_win32(error, CloseHandle(handle) ? 0 : GetLastError());
}

#if HX_OS_ENABLE_LOOP
static void hx_os_pipe_native_error(hx_os_request *request, DWORD code)
{
	hx_os_status status = hx_os_i_win32(&request->completion.error, code);
	if (code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA)
		status = request->operation.kind == HX_OS_I_OP_PIPE_READ
			? HX_OS_EOF
			: HX_OS_CLOSED;
	if (code == ERROR_OPERATION_ABORTED)
		status = HX_OS_CANCELLED;
	request->completion.error.status = status;
}

void hx_os_i_pipe_step(hx_os_request *request)
{
	hx_os_operation *op = &request->operation;
	size_t remaining;
	DWORD bytes, code;
	BOOL result;
	if (request->native_pending) {
		if (request->stop_status && !request->native_cancelled) {
			(void)CancelIoEx(
				request->pipe->native, &request->overlapped);
			request->native_cancelled = 1;
		}
		return;
	}
	if (op->kind == HX_OS_I_OP_PIPE_ACCEPT) {
		if (request->native_done) {
			request->native_done = 0;
			hx_os_pipe_native_error(request, request->native_error);
			request->ready = 1;
			return;
		}
		if (request->stop_status) {
			request->ready = 1;
			return;
		}
		request->started = 1;
		memset(&request->overlapped, 0, sizeof(request->overlapped));
		result = ConnectNamedPipe(
			request->pipe->native, &request->overlapped);
		code = result ? 0 : GetLastError();
		if (!code || code == ERROR_IO_PENDING)
			request->native_pending = 1;
		else {
			hx_os_pipe_native_error(request,
				code == ERROR_PIPE_CONNECTED ? 0 : code);
			request->ready = 1;
		}
		return;
	}
	if (request->native_done) {
		request->native_done = 0;
		request->completion.bytes += request->native_bytes;
		if (request->native_error) {
			hx_os_pipe_native_error(request, request->native_error);
			request->ready = 1;
			return;
		}
		if (op->kind == HX_OS_I_OP_PIPE_READ) {
			(void)hx_os_i_error(&request->completion.error,
				request->native_bytes ? HX_OS_OK : HX_OS_EOF, 0,
				0);
			request->ready = 1;
			return;
		}
		if (!request->native_bytes &&
			request->completion.bytes < op->bytes) {
			(void)hx_os_i_error(&request->completion.error,
				HX_OS_IO_ERROR, 0, 0);
			request->ready = 1;
			return;
		}
	}
	remaining = op->bytes - request->completion.bytes;
	if (request->stop_status || !remaining) {
		request->ready = 1;
		return;
	}
	bytes = remaining > INT_MAX ? INT_MAX : (DWORD)remaining;
	memset(&request->overlapped, 0, sizeof(request->overlapped));
	request->native_cancelled = 0;
	request->native_bytes = 0;
	request->started = 1;
	result = op->kind == HX_OS_I_OP_PIPE_READ
		? ReadFile(request->pipe->native, op->data, bytes,
			  &request->native_bytes, &request->overlapped)
		: WriteFile(request->pipe->native,
			  (char *)op->data + request->completion.bytes, bytes,
			  &request->native_bytes, &request->overlapped);
	code = result ? 0 : GetLastError();
	if (!code || code == ERROR_IO_PENDING)
		request->native_pending = 1;
	else {
		hx_os_pipe_native_error(request, code);
		request->ready = 1;
	}
}
#endif
