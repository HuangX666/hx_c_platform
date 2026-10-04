#include "file_extra_private.h"

#if HX_OS_ENABLE_FILE_EXTRA
static hx_os_status hx_os_lock_validate(
	hx_os_file *file, uint64_t offset, uint64_t bytes, hx_os_error *error)
{
	if (!file || !bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (offset > INT64_MAX || bytes > INT64_MAX - offset)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	if (file->pipe)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	return HX_OS_OK;
}
#endif

hx_os_status hx_os_file_lock_at(hx_os_file *file, uint64_t offset,
	uint64_t bytes, int shared, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_EXTRA
	hx_os_error native;

	hx_os_status status = hx_os_lock_validate(file, offset, bytes, error);
	if (status != HX_OS_OK)
		return status;

	if (shared != 0 && shared != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!(file->flags & (shared ? HX_OS_FILE_READ : HX_OS_FILE_WRITE)))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);

	for (;;) {
		uint64_t now, wake;

		status = hx_os_i_file_lock(
			file, offset, bytes, shared, 0, &native);
		if (status != HX_OS_WOULD_BLOCK) {
			if (error)
				*error = native;

			return status;
		}

		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		if (deadline != HX_OS_DEADLINE_INFINITE && now >= deadline)
			return hx_os_i_error(error, HX_OS_TIMEOUT,
				native.domain, native.native_code);

		wake = now > UINT64_MAX - 1 - UINT64_C(10000000)
			? UINT64_MAX - 1
			: now + UINT64_C(10000000);

		if (deadline < wake)
			wake = deadline;

		status = hx_os_sleep_until(wake, error);
		if (status != HX_OS_OK)
			return status;

		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		if (deadline != HX_OS_DEADLINE_INFINITE && now >= deadline)
			return hx_os_i_error(error, HX_OS_TIMEOUT,
				native.domain, native.native_code);
	}
#else
	(void)file;
	(void)offset;
	(void)bytes;
	(void)shared;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_file_unlock_at(
	hx_os_file *file, uint64_t offset, uint64_t bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_EXTRA
	hx_os_status status = hx_os_lock_validate(file, offset, bytes, error);

	return status == HX_OS_OK
		? hx_os_i_file_lock(file, offset, bytes, 0, 1, error)
		: status;
#else
	(void)file;
	(void)offset;
	(void)bytes;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_file_times_init(hx_os_file_times *times, size_t capacity)
{
	hx_os_file_times value = HX_OS_FILE_TIMES_INIT;

	if (!times || capacity < sizeof(value))
		return HX_OS_INVALID;

	memcpy(times, &value, sizeof(value));

	return HX_OS_OK;
}

hx_os_status hx_os_file_set_times(const char *path,
	const hx_os_file_times *times, int follow_links, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_EXTRA
	hx_os_status status;

	if (!times || times->size < sizeof(*times) || times->version != 1 ||
		times->reserved || (times->fields & ~3u) ||
		(follow_links != 0 && follow_links != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (((times->fields & HX_OS_TIME_ACCESSED) &&
		    times->accessed_nanoseconds >= 1000000000) ||
		((times->fields & HX_OS_TIME_MODIFIED) &&
			times->modified_nanoseconds >= 1000000000))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;

	if (!times->fields)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	return hx_os_i_file_set_times(path, times, follow_links, error);
#else
	(void)path;
	(void)times;
	(void)follow_links;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_path_set_permissions(const char *path, unsigned permissions,
	int follow_links, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_EXTRA
	hx_os_status status;

	if ((permissions & ~7u) || (follow_links != 0 && follow_links != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_i_path_permissions(path, permissions, follow_links, error);
#else
	(void)path;
	(void)permissions;
	(void)follow_links;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
