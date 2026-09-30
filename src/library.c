#include "internal.h"
#include "hx_os_library.h"

#if HX_OS_ENABLE_LIBRARY
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

struct hx_os_library {
	void *native;
};

#if HX_OS_ENABLE_LIBRARY
static void hx_os_library_diagnostic(const char *message, hx_string *out)
{
	char snapshot[2048];
	char escaped[sizeof(snapshot) * 4];
	static const char digits[] = "0123456789abcdef";
	size_t bytes = 0, i, used = 0, invalid;
	int truncated;
	if (!out || !message)
		return;
	/* Snapshot before calling an allocator that may reenter the loader. */
	while (bytes < sizeof(snapshot) - 1 && message[bytes]) {
		snapshot[bytes] = message[bytes];
		++bytes;
	}
	truncated = message[bytes] != 0;
	snapshot[bytes] = 0;
	if (hx_os_text_validate_utf8(snapshot, bytes, &invalid) == HX_OS_OK)
		(void)hx_string_assign_cstr(out, snapshot, NULL);
	else {
		for (i = 0; i < bytes; ++i) {
			unsigned char value = (unsigned char)snapshot[i];
			if (value >= 32 && value < 127 && value != '\\')
				escaped[used++] = (char)value;
			else {
				escaped[used++] = '\\';
				escaped[used++] = 'x';
				escaped[used++] = digits[value >> 4];
				escaped[used++] = digits[value & 15];
			}
		}
		escaped[used] = 0;
		(void)hx_string_assign_cstr(out, escaped, NULL);
	}
	if (truncated)
		(void)hx_string_append_cstr(out, " [truncated]", NULL);
}

static hx_os_status hx_os_library_failure(
	hx_os_status status, hx_string *diagnostic, hx_os_error *error)
{
#if defined(_WIN32)
	hx_os_error value;
	char text[2048];
	size_t required;
	DWORD code = GetLastError();
	(void)hx_os_i_win32(&value, code);
	if (status == HX_OS_NOT_FOUND)
		value.status = status;
	if (diagnostic &&
		hx_os_error_message(&value, text, sizeof(text), &required) ==
			HX_OS_OK)
		hx_os_library_diagnostic(text, diagnostic);
	if (error)
		*error = value;
	return value.status;
#else
	const char *message = dlerror();
	hx_os_library_diagnostic(message, diagnostic);
	return hx_os_i_error(error, status, HX_OS_ERROR_LOADER, 0);
#endif
}
#endif

hx_os_status hx_os_library_open_ex(hx_os_context *context, const char *path,
	unsigned flags, hx_os_library **out, hx_string *diagnostic,
	hx_os_error *error)
{
	hx_os_status status;
	if (diagnostic)
		hx_string_clear(diagnostic);
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (flags & ~(unsigned)HX_OS_LIBRARY_GLOBAL)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
#if HX_OS_ENABLE_LIBRARY
	{
		hx_os_library *library =
			hx_os_i_zero(context, sizeof(*library));
		if (!library)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
#if defined(_WIN32)
		{
			hx_os_error local;
			wchar_t *wide =
				hx_os_i_windows_path(context, path, &local);
			if (!wide) {
				hx_os_memory_free(library);
				if (error)
					*error = local;
				return local.status;
			}
			library->native = LoadLibraryW(wide);
			status = library->native
				? HX_OS_OK
				: hx_os_library_failure(
					  HX_OS_IO_ERROR, diagnostic, error);
			hx_os_memory_free(wide);
		}
#else
		{
			hx_string qualified = HX_STRING_INIT;
			const char *load_path = path;
			hx_string_init(&qualified, context);
			if (!strchr(path, '/')) {
				status = hx_string_assign_cstr(
					&qualified, "./", error);
				if (status == HX_OS_OK)
					status = hx_string_append_cstr(
						&qualified, path, error);
				load_path = hx_string_c_str(&qualified);
			}
			if (status == HX_OS_OK) {
				(void)dlerror();
				library->native = dlopen(load_path,
					RTLD_NOW |
						(flags ? RTLD_GLOBAL
						       : RTLD_LOCAL));
				if (!library->native)
					status = hx_os_library_failure(
						HX_OS_IO_ERROR, diagnostic,
						error);
			}
			hx_string_destroy(&qualified);
		}
#endif
		if (status != HX_OS_OK) {
			hx_os_memory_free(library);
			return status;
		}
		*out = library;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)context;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_library_symbol_ex(hx_os_library *library, const char *name,
	void **address, hx_string *diagnostic, hx_os_error *error)
{
	if (diagnostic)
		hx_string_clear(diagnostic);
	if (!library || !name || !name[0] || !address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*address = NULL;
#if HX_OS_ENABLE_LIBRARY
#if defined(_WIN32)
	{
		FARPROC symbol = GetProcAddress((HMODULE)library->native, name);
		if (!symbol)
			return hx_os_library_failure(
				HX_OS_NOT_FOUND, diagnostic, error);
		memcpy(address, &symbol, sizeof(*address));
	}
#else
	{
		const char *message;
		void *symbol;
		(void)dlerror();
		symbol = dlsym(library->native, name);
		message = dlerror();
		if (message) {
			hx_os_library_diagnostic(message, diagnostic);
			return hx_os_i_error(
				error, HX_OS_NOT_FOUND, HX_OS_ERROR_LOADER, 0);
		}
		*address = symbol;
	}
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_library_close_ex(
	hx_os_library **library, hx_string *diagnostic, hx_os_error *error)
{
	if (diagnostic)
		hx_string_clear(diagnostic);
	if (!library)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*library)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
#if HX_OS_ENABLE_LIBRARY
	{
		hx_os_library *value = *library;
		hx_os_status status;
		*library = NULL;
#if defined(_WIN32)
		status = FreeLibrary((HMODULE)value->native)
			? HX_OS_OK
			: hx_os_library_failure(
				  HX_OS_IO_ERROR, diagnostic, error);
#else
		status = dlclose(value->native) == 0
			? HX_OS_OK
			: hx_os_library_failure(
				  HX_OS_IO_ERROR, diagnostic, error);
#endif
		hx_os_memory_free(value);
		return status == HX_OS_OK ? hx_os_i_error(error, HX_OS_OK, 0, 0)
					  : status;
	}
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_library_open(hx_os_context *context, const char *path,
	unsigned flags, hx_os_library **out, hx_os_error *error)
{
	return hx_os_library_open_ex(context, path, flags, out, NULL, error);
}

hx_os_status hx_os_library_symbol(hx_os_library *library, const char *name,
	void **address, hx_os_error *error)
{
	return hx_os_library_symbol_ex(library, name, address, NULL, error);
}

hx_os_status hx_os_library_close(hx_os_library **library, hx_os_error *error)
{
	return hx_os_library_close_ex(library, NULL, error);
}

hx_os_status hx_os_library_bind_ex(hx_os_library *library, const char *name,
	void *pointer, size_t bytes, hx_string *diagnostic, hx_os_error *error)
{
	void *address;
	hx_os_status status;
	if (diagnostic)
		hx_string_clear(diagnostic);
	if (!pointer || bytes != sizeof(address))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_library_symbol_ex(
		library, name, &address, diagnostic, error);
	if (status == HX_OS_OK)
		memcpy(pointer, &address, bytes);
	return status;
}

hx_os_status hx_os_library_bind(hx_os_library *library, const char *name,
	void *pointer, size_t bytes, hx_os_error *error)
{
	return hx_os_library_bind_ex(
		library, name, pointer, bytes, NULL, error);
}
