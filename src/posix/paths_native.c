#include "paths_private.h"

#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#define HX_OS_PATH_LIMIT ((size_t)1048576)
static const char hx_os_paths_module_anchor = 0;

static hx_os_status hx_os_path_environment(
	const char *name, hx_string *out, hx_os_error *error)
{
	hx_os_status status = hx_os_i_env_get_string(name, out, error);
	if (status == HX_OS_OK && (!out->size || out->data[0] != '/'))
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	return status;
}

static hx_os_status hx_os_path_user_home(hx_string *out, hx_os_error *error)
{
	struct passwd entry, *result = NULL;
	size_t capacity;
	char *buffer;
	int code;
	hx_os_status status = hx_os_path_environment("HOME", out, error);
	if (status != HX_OS_NOT_FOUND)
		return status;
	for (capacity = 1024; capacity <= HX_OS_PATH_LIMIT; capacity *= 2) {
		buffer = hx_os_i_alloc(out->context, capacity, 0);
		if (!buffer)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		code = getpwuid_r(getuid(), &entry, buffer, capacity, &result);
		if (!code && result && entry.pw_dir && entry.pw_dir[0] == '/')
			status =
				hx_string_assign_cstr(out, entry.pw_dir, error);
		else
			status = code
				? hx_os_i_errno(error, code)
				: hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
		hx_os_memory_free(buffer);
		if (code != ERANGE)
			return status;
	}
	return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
}

static hx_os_status hx_os_path_exe(hx_string *out, hx_os_error *error)
{
#if defined(__APPLE__)
	uint32_t size = 0;
	char *buffer, *absolute;
	hx_os_status status;
	(void)_NSGetExecutablePath(NULL, &size);
	if (!size || size > HX_OS_PATH_LIMIT)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	buffer = hx_os_i_alloc(out->context, size, 0);
	if (!buffer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (_NSGetExecutablePath(buffer, &size)) {
		hx_os_memory_free(buffer);
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}
	absolute = realpath(buffer, NULL);
	status = absolute ? hx_string_assign_cstr(out, absolute, error)
			  : hx_os_i_errno(error, errno);
	free(absolute);
	hx_os_memory_free(buffer);
	return status;
#else
	size_t capacity;
	hx_os_status status;
	for (capacity = 256; capacity <= HX_OS_PATH_LIMIT; capacity *= 2) {
		ssize_t length;
		status = hx_string_reserve(out, capacity, error);
		if (status != HX_OS_OK)
			return status;
		length = readlink("/proc/self/exe", out->data, capacity);
		if (length < 0)
			return hx_os_i_errno(error, errno);
		if ((size_t)length < capacity) {
			size_t invalid;
			out->data[length] = 0;
			out->size = (size_t)length;
			status = hx_os_text_validate_utf8(
				out->data, out->size, &invalid);
			return hx_os_i_error(error, status, 0, 0);
		}
	}
	return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
#endif
}

hx_os_status hx_os_i_special_path(int kind, hx_string *out, hx_os_error *error)
{
	hx_os_status status;
	const char *suffix;
#if defined(__ANDROID__)
	/* App-private roots need a Java/NativeActivity host; never guess them.
	 */
	if (kind != HX_OS_I_PATH_EXECUTABLE && kind != HX_OS_I_PATH_MODULE)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	if (kind == HX_OS_I_PATH_HOME)
		return hx_os_path_user_home(out, error);
	if (kind == HX_OS_I_PATH_EXECUTABLE)
		return hx_os_path_exe(out, error);
	if (kind == HX_OS_I_PATH_MODULE) {
		Dl_info info;
		const char *slash;
		if (!dladdr(&hx_os_paths_module_anchor, &info) ||
			!info.dli_fname || info.dli_fname[0] != '/')
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		slash = strrchr(info.dli_fname, '/');
		return hx_string_assign_encoded(out, HX_OS_UTF8, info.dli_fname,
			slash == info.dli_fname
				? 1
				: (size_t)(slash - info.dli_fname),
			error);
	}
	if (kind == HX_OS_I_PATH_PROGRAM_DATA)
		return hx_string_assign_cstr(out,
#if defined(__APPLE__)
			"/Library/Application Support",
#else
			"/var/lib",
#endif
			error);
	if (kind == HX_OS_I_PATH_TEMP) {
		status = hx_os_path_environment("TMPDIR", out, error);
		if (status != HX_OS_NOT_FOUND)
			return status;
#if defined(__APPLE__)
		{
			size_t size =
				confstr(_CS_DARWIN_USER_TEMP_DIR, NULL, 0);
			if (size) {
				if (size > HX_OS_PATH_LIMIT)
					return hx_os_i_error(
						error, HX_OS_LIMIT, 0, 0);
				status =
					hx_string_reserve(out, size - 1, error);
				if (status != HX_OS_OK)
					return status;
				if (confstr(_CS_DARWIN_USER_TEMP_DIR, out->data,
					    size) != size)
					return hx_os_i_error(
						error, HX_OS_BUSY, 0, 0);
				out->size = size - 1;
				return hx_os_i_path_validate(out->data, error);
			}
		}
#endif
		return hx_string_assign_cstr(out, "/tmp", error);
	}
#if defined(__APPLE__)
	suffix = "/Library/Application Support";
#else
	status = hx_os_path_environment(kind == HX_OS_I_PATH_CONFIG_HOME
			? "XDG_CONFIG_HOME"
			: "XDG_DATA_HOME",
		out, error);
	if (status != HX_OS_NOT_FOUND)
		return status;
	suffix =
		kind == HX_OS_I_PATH_CONFIG_HOME ? "/.config" : "/.local/share";
#endif
	status = hx_os_path_user_home(out, error);
	if (status == HX_OS_OK) {
		while (out->size && out->data[out->size - 1] == '/')
			out->data[--out->size] = 0;
		status = hx_string_append_cstr(out, suffix, error);
	}
	return status;
}
