#include "internal.h"

hx_os_status hx_os_file_write_all(hx_os_file *file, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	const unsigned char *input = data;
	hx_os_status status = HX_OS_OK;

	if (!completed || (!data && bytes) || !file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = 0;

	while (*completed < bytes) {
		size_t written = 0;

		status = hx_os_file_write_some(file, input + *completed,
			bytes - *completed, &written, error);
		*completed += written;
		if (status != HX_OS_OK)
			return status;

		if (!written)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_i_file_flags_valid(uint32_t flags)
{
	return !(flags & ~63u) &&
		(flags & (HX_OS_FILE_READ | HX_OS_FILE_WRITE)) &&
		(!(flags & (HX_OS_FILE_TRUNCATE | HX_OS_FILE_APPEND)) ||
			(flags & HX_OS_FILE_WRITE)) &&
		(!(flags & HX_OS_FILE_EXCLUSIVE) ||
			(flags & HX_OS_FILE_CREATE));
}

hx_os_status hx_os_i_directory_create_one(const char *path, hx_os_error *error);

static hx_os_status hx_os_directory_ensure(const char *path, hx_os_error *error)
{
	hx_os_status status = hx_os_i_directory_create_one(path, error);
	if (status == HX_OS_EXISTS) {
		hx_os_file_info info;

		status = hx_os_path_stat(path, 1, &info, error);
		if (status == HX_OS_OK && info.kind != HX_OS_FILE_DIRECTORY)
			status = hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
	}

	return status;
}

hx_os_status hx_os_directory_create(
	const char *path, int parents, hx_os_error *error)
{
	size_t length;
	size_t i;
	size_t start = 1;
	char *copy;

	hx_os_status status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;

	if (!parents)
		return hx_os_i_directory_create_one(path, error);

	length = strlen(path);
	copy = hx_os_i_alloc(NULL, length + 1, 0);
	if (!copy)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	memcpy(copy, path, length + 1);
#if defined(_WIN32)
	for (i = 0; i < length; ++i)
		if (copy[i] == '\\')
			copy[i] = '/';

	if (length >= 3 && copy[1] == ':')
		start = 3;
	else if (length >= 2 && copy[0] == '/' && copy[1] == '/') {
		size_t separators = 0;

		start = length;

		for (i = 2; i < length; ++i) {
			if (copy[i] == '/' && ++separators == 2) {
				start = i + 1;
				break;
			}
		}
	}
#endif
	for (i = start; i < length; ++i) {
		if (copy[i] != '/' || copy[i - 1] == '/')
			continue;

		copy[i] = 0;

		status = hx_os_directory_ensure(copy, error);
		copy[i] = '/';
		if (status != HX_OS_OK)
			break;
	}

	if (status == HX_OS_OK)
		status = hx_os_directory_ensure(copy, error);

	hx_os_memory_free(copy);

	return status;
}
