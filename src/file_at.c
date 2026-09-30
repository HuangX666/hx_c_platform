#include "file_private.h"

static hx_os_status hx_os_file_at(hx_os_file *file, int writing,
	uint64_t offset, void *data, size_t bytes, size_t *completed,
	hx_os_error *error)
{
	if (completed)
		*completed = 0;
#if HX_OS_ENABLE_POSITIONED_IO
	if (!file || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (file->pipe || (writing && (file->flags & HX_OS_FILE_APPEND)))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!(file->flags & (writing ? HX_OS_FILE_WRITE : HX_OS_FILE_READ)))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (bytes > INT_MAX)
		bytes = INT_MAX;
	if (offset > INT64_MAX || bytes > (uint64_t)INT64_MAX - offset)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	return hx_os_i_file_at(
		file, writing, offset, data, bytes, completed, error);
#else
	(void)file;
	(void)writing;
	(void)offset;
	(void)data;
	(void)bytes;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_file_read_at(hx_os_file *file, uint64_t offset, void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	return hx_os_file_at(file, 0, offset, data, bytes, completed, error);
}

hx_os_status hx_os_file_write_at(hx_os_file *file, uint64_t offset,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error)
{
	return hx_os_file_at(
		file, 1, offset, (void *)data, bytes, completed, error);
}
