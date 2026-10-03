#include "ipc_private.h"
#include <sddl.h>
#include <stdio.h>

static hx_os_status hx_ipc_user_sid(
	hx_os_context *context, char **out, hx_os_error *error)
{
	HANDLE token = NULL;
	DWORD bytes = 0, code = 0;
	TOKEN_USER *user = NULL;
	LPSTR sid = NULL;
	*out = NULL;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return hx_os_i_win32(error, GetLastError());
	(void)GetTokenInformation(token, TokenUser, NULL, 0, &bytes);
	user = hx_os_i_alloc(context, bytes, 0);
	if (!user)
		code = ERROR_NOT_ENOUGH_MEMORY;
	else if (!GetTokenInformation(token, TokenUser, user, bytes, &bytes) ||
		!ConvertSidToStringSidA(user->User.Sid, &sid))
		code = GetLastError();
	(void)CloseHandle(token);
	hx_os_memory_free(user);
	if (!code)
		*out = sid;
	return hx_os_i_win32(error, code);
}

static hx_os_status hx_ipc_pipe_instance(
	hx_ipc *endpoint, int first, hx_os_error *error)
{
	hx_os_pipe *pipe;
	wchar_t *name;
	char *sid = NULL, descriptor[256];
	PSECURITY_DESCRIPTOR security = NULL;
	SECURITY_ATTRIBUTES attributes;
	hx_os_status status = hx_ipc_user_sid(endpoint->context, &sid, error);
	DWORD code;
	if (status != HX_OS_OK)
		return status;
	(void)snprintf(descriptor, sizeof(descriptor),
		"D:P(A;;GA;;;%s)(A;;GA;;;SY)", sid);
	LocalFree(sid);
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
		    descriptor, SDDL_REVISION_1, &security, NULL))
		return hx_os_i_win32(error, GetLastError());
	name = hx_os_i_windows_utf16(endpoint->context, endpoint->name, error);
	if (!name) {
		LocalFree(security);
		return error ? error->status : HX_OS_NO_MEMORY;
	}
	pipe = hx_os_i_zero(endpoint->context, sizeof(*pipe));
	if (!pipe) {
		hx_os_memory_free(name);
		hx_os_memory_free(pipe);
		LocalFree(security);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	memset(&attributes, 0, sizeof(attributes));
	attributes.nLength = sizeof(attributes);
	attributes.lpSecurityDescriptor = security;
	pipe->native = CreateNamedPipeW(name,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
			(first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
			PIPE_REJECT_REMOTE_CLIENTS,
		PIPE_UNLIMITED_INSTANCES, 16384, 16384, 0, &attributes);
	code = pipe->native == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(name);
	LocalFree(security);
	if (code) {
		hx_os_memory_free(pipe);
		return hx_os_i_win32(error, code);
	}
	pipe->direction = HX_OS_WAIT_READ;
	endpoint->accept_pipe = pipe;
	return HX_OS_OK;
}

hx_os_status hx_ipc_i_native_prepare(
	hx_ipc *endpoint, const char *address, hx_os_error *error)
{
	char full[512], *sid = NULL;
	hx_os_status status;
	if (!strncmp(address, "local://", 8)) {
		const char *name = address + 8;
		size_t index;
		if (!*name || strlen(name) > 64)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		for (index = 0; name[index]; ++index)
			if (!((name[index] >= 'a' && name[index] <= 'z') ||
				    (name[index] >= 'A' &&
					    name[index] <= 'Z') ||
				    (name[index] >= '0' &&
					    name[index] <= '9') ||
				    strchr("._-", name[index])))
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
		status = hx_ipc_user_sid(endpoint->context, &sid, error);
		if (status != HX_OS_OK)
			return status;
		(void)snprintf(full, sizeof(full), "\\\\.\\pipe\\hx-ipc-%s-%s",
			sid, name);
		LocalFree(sid);
	}
	else if (!strncmp(address, "pipe://", 7)) {
		if (!address[7] || strlen(address + 7) > 200 ||
			strchr(address + 7, '\\') || strchr(address + 7, '/'))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		(void)snprintf(
			full, sizeof(full), "\\\\.\\pipe\\%s", address + 7);
	}
	else
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	endpoint->name = hx_os_i_alloc(endpoint->context, strlen(full) + 1, 0);
	if (!endpoint->name)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	memcpy(endpoint->name, full, strlen(full) + 1);
	status = hx_string_assign_cstr(
		&endpoint->address_text, "pipe://", error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(
			&endpoint->address_text, full + 9, error);
	if (status == HX_OS_OK && endpoint->server)
		status = hx_ipc_pipe_instance(endpoint, 1, error);
	return status;
}

hx_os_status hx_ipc_i_pipe_write_end(hx_ipc *endpoint, hx_os_pipe *reader,
	hx_os_pipe **out, hx_os_error *error)
{
	hx_os_pipe *writer = hx_os_i_zero(endpoint->context, sizeof(*writer));
	if (!writer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (!DuplicateHandle(GetCurrentProcess(), reader->native,
		    GetCurrentProcess(), &writer->native, 0, FALSE,
		    DUPLICATE_SAME_ACCESS)) {
		DWORD code = GetLastError();
		hx_os_memory_free(writer);
		return hx_os_i_win32(error, code);
	}
	writer->direction = HX_OS_WAIT_WRITE;
	writer->association = reader;
	*out = writer;
	return HX_OS_OK;
}

hx_os_status hx_ipc_i_native_connect(
	hx_ipc_connection *peer, hx_os_error *error)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_os_pipe *reader;
	wchar_t *name;
	hx_os_status status;
	DWORD code;
	name = hx_os_i_windows_utf16(endpoint->context, endpoint->name, error);
	if (!name)
		return error ? error->status : HX_OS_NO_MEMORY;
	reader = hx_os_i_zero(endpoint->context, sizeof(*reader));
	if (!reader) {
		hx_os_memory_free(name);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	reader->native = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0,
		NULL, OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT |
			SECURITY_IDENTIFICATION,
		NULL);
	code = reader->native == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(name);
	if (code) {
		hx_os_memory_free(reader);
		status = hx_os_i_win32(error, code);
		if (code == ERROR_PIPE_BUSY || code == ERROR_FILE_NOT_FOUND)
			status = hx_os_i_error(error, HX_OS_WOULD_BLOCK,
				HX_OS_ERROR_WIN32, code);
		return status;
	}
	reader->direction = HX_OS_WAIT_READ;
	status =
		hx_ipc_i_pipe_write_end(endpoint, reader, &peer->writer, error);
	if (status != HX_OS_OK) {
		(void)hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL);
		return status;
	}
	peer->reader = reader;
	return HX_OS_OK;
}

hx_os_status hx_ipc_i_native_accept(
	hx_ipc *endpoint, int first, hx_os_error *error)
{
	hx_os_operation operation;
	hx_os_status status = HX_OS_OK;
	(void)first;
	if (!endpoint->accept_pipe)
		status = hx_ipc_pipe_instance(endpoint, 0, error);
	if (status != HX_OS_OK)
		return status;
	hx_os_operation_init(&operation, HX_OS_I_OP_PIPE_ACCEPT);
	return hx_os_i_loop_submit(endpoint->loop, &operation,
		endpoint->accept_pipe, NULL, hx_ipc_i_pipe_accepted, endpoint,
		&endpoint->accepting, error);
}

void hx_ipc_i_native_cleanup(hx_ipc *endpoint)
{
	(void)endpoint;
}
