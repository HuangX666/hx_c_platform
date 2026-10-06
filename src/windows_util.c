#include "windows_util_private.h"

int hx_os_windows_version_available(void)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_VERSION
	return 1;
#else
	return 0;
#endif
}

int hx_os_windows_desktop_available(void)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_DESKTOP
	return 1;
#else
	return 0;
#endif
}

hx_os_status hx_os_windows_version_query(const char *path,
	hx_os_windows_version_info *out, size_t capacity, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_VERSION
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity < sizeof(*out))
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	return hx_os_i_windows_version_query(path, out, error);
#else
	(void)path;
	(void)out;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_windows_version_string_ex(const char *path,
	const char *field, uint16_t language, uint16_t code_page,
	hx_string *out, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_VERSION
	size_t count = 0;

	if (!out || !field || !*field)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	while (field[count]) {
		unsigned char c = (unsigned char)field[count];

		if (++count > 64 ||
			!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_'))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	return hx_os_i_windows_version_string(
		path, field, language, code_page, out, error);
#else
	(void)path;
	(void)field;
	(void)language;
	(void)code_page;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_windows_version_string(
	const char *path, const char *field, hx_string *out, hx_os_error *error)
{
	return hx_os_windows_version_string_ex(path, field, 0, 0, out, error);
}

hx_os_status hx_os_windows_window_query(uintptr_t window,
	hx_os_windows_window_info *out, size_t capacity, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_DESKTOP
	if (!window || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity < sizeof(*out))
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	return hx_os_i_windows_window_query(window, out, error);
#else
	(void)window;
	(void)out;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_windows_window_text(
	uintptr_t window, int kind, hx_string *out, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_DESKTOP
	if (!window || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_windows_window_text(window, kind, out, error);
#else
	(void)window;
	(void)kind;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_windows_window_title(
	uintptr_t window, hx_string *out, hx_os_error *error)
{
	return hx_os_windows_window_text(window, 0, out, error);
}

hx_os_status hx_os_windows_window_class(
	uintptr_t window, hx_string *out, hx_os_error *error)
{
	return hx_os_windows_window_text(window, 1, out, error);
}

hx_os_status hx_os_windows_window_process_path(
	uintptr_t window, hx_string *out, hx_os_error *error)
{
	return hx_os_windows_window_text(window, 2, out, error);
}

hx_os_status hx_os_windows_monitor_query(uintptr_t monitor,
	hx_os_windows_monitor_info *out, size_t capacity, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_DESKTOP
	if (!monitor || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity < sizeof(*out))
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	return hx_os_i_windows_monitor_query(monitor, out, error);
#else
	(void)monitor;
	(void)out;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_windows_gpu_query_impl(uint32_t index,
	uintptr_t monitor, hx_os_windows_gpu_info *out, size_t capacity,
	hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_WINDOWS_DESKTOP
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity < sizeof(*out))
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	if (index > 255)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	return hx_os_i_windows_gpu_query(index, monitor, out, error);
#else
	(void)index;
	(void)monitor;
	(void)out;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_windows_gpu_query(uint32_t index,
	hx_os_windows_gpu_info *out, size_t capacity, hx_os_error *error)
{
	return hx_os_windows_gpu_query_impl(index, 0, out, capacity, error);
}

hx_os_status hx_os_windows_monitor_gpu_query(uintptr_t monitor,
	hx_os_windows_gpu_info *out, size_t capacity, hx_os_error *error)
{
	if (hx_os_windows_desktop_available() && !monitor)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_windows_gpu_query_impl(0, monitor, out, capacity, error);
}
