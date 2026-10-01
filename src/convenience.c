#include "internal.h"
#include "hx_os_paths.h"
#include "hx_os_wait.h"
hx_os_status hx_os_thread_pool_shutdown_ex(hx_os_thread_pool **pool, int mode,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;
	if (!pool)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*pool)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_thread_pool_close_until(*pool, mode, deadline, error);
	return status == HX_OS_OK ? hx_os_thread_pool_destroy(pool, error)
				  : status;
}
hx_os_status hx_os_thread_pool_shutdown(
	hx_os_thread_pool **pool, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_thread_pool_shutdown_ex(
		pool, HX_OS_POOL_DRAIN, deadline, error);
}
hx_os_status hx_os_repeat_until(uint64_t interval_ms, size_t ticks,
	hx_os_repeat_fn function, void *user, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	uint64_t now, interval, due;
	size_t count = 0;
	hx_os_status status;
	if (completed)
		*completed = 0;
	if (!interval_ms || interval_ms > (UINT64_MAX - 1) / 1000000 ||
		!ticks || !function)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	interval = interval_ms * 1000000;
	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;
	if (now > UINT64_MAX - 1 - interval)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	due = now + interval;
	for (;;) {
		uint64_t steps;
		if (due >= deadline && deadline != HX_OS_DEADLINE_INFINITE) {
			status = hx_os_sleep_until(deadline, error);
			return status == HX_OS_OK
				? hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0)
				: status;
		}
		status = hx_os_sleep_until(due, error);
		if (status != HX_OS_OK)
			return status;
		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;
		if (now >= deadline && deadline != HX_OS_DEADLINE_INFINITE)
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
		{
			hx_os_error callback_error;
			hx_os_error_clear(&callback_error);
			status = function(user, &callback_error);
			if (error)
				*error = callback_error;
		}
		++count;
		if (completed)
			*completed = count;
		if (status == HX_OS_EOF ||
			(status == HX_OS_OK && count == ticks))
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (status != HX_OS_OK) {
			if (error && error->status != status)
				(void)hx_os_i_error(error, status, 0, 0);
			return status;
		}
		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;
		steps = (now - due) / interval + 1;
		if (steps > (UINT64_MAX - 1 - due) / interval)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		due += steps * interval;
	}
}
hx_os_status hx_os_repeat(uint64_t interval_ms, size_t ticks,
	hx_os_repeat_fn function, void *user, hx_os_error *error)
{
	return hx_os_repeat_until(interval_ms, ticks, function, user,
		HX_OS_DEADLINE_INFINITE, NULL, error);
}
hx_os_status hx_os_path_config_file(const char *application, const char *name,
	hx_string *out, hx_os_error *error)
{
	hx_string path;
	hx_os_status status;
	if (!out || !application || !*application || !name || !*name ||
		strpbrk(application, "/\\:") || strpbrk(name, "/\\:") ||
		!strcmp(application, ".") || !strcmp(application, "..") ||
		!strcmp(name, ".") || !strcmp(name, ".."))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&path, out->context);
	status = hx_os_path_config_home_string(&path, error);
	if (status == HX_OS_OK && path.size &&
		path.data[path.size - 1] != '/' &&
		path.data[path.size - 1] != '\\')
		status = hx_string_append_cstr(&path, "/", error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, application, error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, "/", error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, name, error);
	if (status == HX_OS_OK)
		hx_string_move(out, &path);
	hx_string_destroy(&path);
	return status;
}
hx_os_status hx_os_file_read_text_encoded(const char *path,
	hx_os_encoding encoding, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_UTILS
	void *bytes = NULL;
	size_t size = 0, skip = 0;
	hx_os_status status;
	hx_os_encoding bom;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_encoding_supported(encoding))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_file_load(
		out->context, path, 16u * 1024u * 1024u, &bytes, &size, error);
	if (status == HX_OS_OK) {
		unsigned char expected[4];
		size_t expected_bytes = 0;
		hx_os_status detected;
		/* Explicit UTF-16LE plus U+0000 shares the UTF-32LE signature.
		 * Prefer the caller's matching marker before generic detection.
		 */
		if (hx_os_text_bom_bytes(encoding, expected, &expected_bytes) ==
				HX_OS_OK &&
			expected_bytes <= size &&
			!memcmp(bytes, expected, expected_bytes)) {
			bom = encoding;
			skip = expected_bytes;
			detected = HX_OS_OK;
		}
		else
			detected =
				hx_os_text_bom_detect(bytes, size, &bom, &skip);
		if (detected == HX_OS_OK && bom != encoding)
			status = hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
		else if (detected != HX_OS_OK)
			skip = 0;
		if (status == HX_OS_OK)
			status = hx_string_assign_encoded(out, encoding,
				bytes ? (const unsigned char *)bytes + skip
				      : NULL,
				size - skip, error);
	}
	hx_os_memory_free(bytes);
	return status;
#else
	(void)path;
	(void)encoding;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
