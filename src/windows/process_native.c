#include "process_private.h"
#include "duplex_private.h"
#include <wchar.h>

// CRT/CommandLineToArgvW quoting, including empty and trailing backslashes.
static size_t hx_os_process_quote(wchar_t *out, const wchar_t *input)
{
	size_t used = 0, slashes;

	if (out)
		out[used] = L'"';

	++used;

	while (*input) {
		slashes = 0;

		while (*input == L'\\') {
			++slashes;
			++input;
		}

		if (*input == L'"' || !*input)
			slashes *= 2;

		if (*input == L'"')
			++slashes;

		while (slashes--) {
			if (out)
				out[used] = L'\\';

			++used;
		}

		if (*input) {
			if (out)
				out[used] = *input;

			++used;
			++input;
		}
	}

	if (out)
		out[used] = L'"';

	return used + 1;
}

static int hx_os_process_env_compare(const void *left, const void *right)
{
	return CompareStringOrdinal(*(const wchar_t *const *)left, -1,
		       *(const wchar_t *const *)right, -1, TRUE) -
		CSTR_EQUAL;
}

static hx_os_status hx_os_process_wide_list(hx_os_context *context,
	const hx_os_string_list *list, wchar_t ***out, wchar_t **storage,
	hx_os_error *error)
{
	size_t i, count = 0, offset = 0;
	wchar_t **items, *block;

	for (i = 0; i < list->count; ++i) {
		int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			list->items[i], -1, NULL, 0);
		if (!needed)
			return hx_os_i_win32(error, GetLastError());

		count += (size_t)needed;
	}

	items = hx_os_i_alloc(context, (list->count + 1) * sizeof(*items), 0);
	block = hx_os_i_zero(context, (count + 2) * sizeof(*block));
	if (!items || !block) {
		hx_os_memory_free(items);
		hx_os_memory_free(block);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	for (i = 0; i < list->count; ++i) {
		int written;

		items[i] = block + offset;
		written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
			list->items[i], -1, items[i], (int)(count - offset));
		if (!written) {
			DWORD code = GetLastError();

			hx_os_memory_free(items);
			hx_os_memory_free(block);

			return hx_os_i_win32(error, code);
		}

		offset += (size_t)written;
	}

	items[list->count] = NULL;
	*out = items;
	*storage = block;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_process_spawn(hx_os_context *context, const char *path,
	const hx_os_string_list *arguments,
	const hx_os_string_list *environment,
	const hx_os_process_options *options, hx_os_process *process,
	hx_os_error *error)
{
	hx_os_error local;
	STARTUPINFOEXW startup;
	PROCESS_INFORMATION native;
	HANDLE handles[3] = {NULL, NULL, NULL};
	hx_os_file *streams[3] = {options->standard_input,
		options->standard_output, options->standard_error};
	const DWORD identifiers[3] = {
		STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
	wchar_t *executable = NULL, *arg_block = NULL, *env_block = NULL;
	wchar_t **args = NULL, **env = NULL, *command = NULL,
		*sorted_env = NULL;
	size_t i, count = 1, offset = 0, env_count = 2;
	SIZE_T attribute_bytes = 0;
	int attributes_ready = 0;
	hx_os_status status;

	if (!error)
		error = &local;

	memset(&startup, 0, sizeof(startup));
	memset(&native, 0, sizeof(native));
	startup.StartupInfo.cb = sizeof(startup);
	executable = hx_os_i_windows_path(context, path, error);
	if (!executable)
		return error->status;

	status = hx_os_process_wide_list(
		context, arguments, &args, &arg_block, error);
	if (status != HX_OS_OK)
		goto done;

	for (i = 0; i < arguments->count; ++i)
		count += hx_os_process_quote(NULL, args[i]) + (i != 0);

	if (count > 32767) {
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		goto done;
	}

	command = hx_os_i_alloc(context, count * sizeof(*command), 0);
	if (!command) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}

	for (i = 0; i < arguments->count; ++i) {
		if (i)
			command[offset++] = L' ';

		offset += hx_os_process_quote(command + offset, args[i]);
	}

	command[offset] = 0;

	status = hx_os_process_wide_list(
		context, environment, &env, &env_block, error);
	if (status != HX_OS_OK)
		goto done;

	qsort(env, environment->count, sizeof(*env), hx_os_process_env_compare);

	for (i = 0; i < environment->count; ++i) {
		// Inherited Windows blocks include hidden =C: drive entries.
		const wchar_t *equal =
			wcschr(env[i] + (env[i][0] == L'='), L'=');

		if (!equal || equal == env[i]) {
			status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
			goto done;
		}

		if (i) {
			size_t name = (size_t)(equal - env[i]) + 1;

			if (wcslen(env[i - 1]) >= name &&
				CompareStringOrdinal(env[i], (int)name,
					env[i - 1], (int)name,
					TRUE) == CSTR_EQUAL) {
				status = hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
				goto done;
			}
		}

		env_count += wcslen(env[i]) + 1;
	}

	sorted_env = hx_os_i_zero(context, env_count * sizeof(*sorted_env));
	if (!sorted_env) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}

	offset = 0;

	for (i = 0; i < environment->count; ++i) {
		count = wcslen(env[i]) + 1;
		memcpy(sorted_env + offset, env[i],
			count * sizeof(*sorted_env));
		offset += count;
	}

	for (i = 0; i < 3; ++i) {
		if (streams[i] && streams[i]->pipe == HX_OS_I_FILE_SOCKET) {
			status =
				hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
			goto done;
		}

		HANDLE source = streams[i] ? streams[i]->handle
					   : GetStdHandle(identifiers[i]);

		if (!source || source == INVALID_HANDLE_VALUE) {
			SECURITY_ATTRIBUTES security = {
				sizeof(security), NULL, TRUE};

			handles[i] = CreateFileW(L"NUL",
				i ? GENERIC_WRITE : GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
				OPEN_EXISTING, 0, NULL);
			if (handles[i] == INVALID_HANDLE_VALUE) {
				handles[i] = NULL;

				status = hx_os_i_win32(error, GetLastError());
				goto done;
			}
		}
		else if (!DuplicateHandle(GetCurrentProcess(), source,
				 GetCurrentProcess(), &handles[i], 0, TRUE,
				 DUPLICATE_SAME_ACCESS)) {
			status = hx_os_i_win32(error, GetLastError());
			goto done;
		}
	}

	(void)InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_bytes);
	startup.lpAttributeList = hx_os_i_alloc(context, attribute_bytes, 0);
	if (!startup.lpAttributeList) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}

	if (!InitializeProcThreadAttributeList(
		    startup.lpAttributeList, 1, 0, &attribute_bytes)) {
		status = hx_os_i_win32(error, GetLastError());
		goto done;
	}

	attributes_ready = 1;
	if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
		    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles),
		    NULL, NULL)) {
		status = hx_os_i_win32(error, GetLastError());
		goto done;
	}

	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = handles[0];
	startup.StartupInfo.hStdOutput = handles[1];
	startup.StartupInfo.hStdError = handles[2];
	if (!CreateProcessW(executable, command, NULL, NULL, TRUE,
		    EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
		    sorted_env, NULL, &startup.StartupInfo, &native)) {
		status = hx_os_i_win32(error, GetLastError());
		goto done;
	}

	(void)CloseHandle(native.hThread);
	process->native = native.hProcess;
	process->identifier = native.dwProcessId;

	status = hx_os_i_error(error, HX_OS_OK, 0, 0);

done:
	for (i = 0; i < 3; ++i)
		if (handles[i])
			(void)CloseHandle(handles[i]);

	if (attributes_ready)
		DeleteProcThreadAttributeList(startup.lpAttributeList);

	hx_os_memory_free(startup.lpAttributeList);
	hx_os_memory_free(sorted_env);
	hx_os_memory_free(command);
	hx_os_memory_free(args);
	hx_os_memory_free(env);
	hx_os_memory_free(arg_block);
	hx_os_memory_free(env_block);
	hx_os_memory_free(executable);

	return status;
}

hx_os_status hx_os_i_process_poll(hx_os_process *process, hx_os_error *error)
{
	DWORD code, waited;

	if (process->finished)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	waited = WaitForSingleObject(process->native, 0);
	if (waited == WAIT_TIMEOUT)
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);

	if (waited != WAIT_OBJECT_0)
		return hx_os_i_win32(error, GetLastError());

	if (!GetExitCodeProcess(process->native, &code))
		return hx_os_i_win32(error, GetLastError());

	process->result.exit_code = code;
	process->finished = 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_process_dispose(hx_os_process *process, hx_os_error *error)
{
	if (!CloseHandle(process->native))
		return hx_os_i_win32(error, GetLastError());

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
