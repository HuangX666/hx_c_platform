#include "file_private.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

struct hx_os_directory {
	DIR *native;
	struct dirent *pending;
};
int hx_os_i_file_flags_valid(uint32_t flags);

hx_os_status hx_os_file_open(hx_os_context *context, const char *path,
	uint32_t flags, hx_os_file **out, hx_os_error *error)
{
	hx_os_file *file;
	int native_flags = O_CLOEXEC | O_NONBLOCK;
	int descriptor;
	int code;
	struct stat info;
	hx_os_status status;
	if (!out || !hx_os_i_file_flags_valid(flags))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	native_flags |= (flags & HX_OS_FILE_WRITE)
		? ((flags & HX_OS_FILE_READ) ? O_RDWR : O_WRONLY)
		: O_RDONLY;
	if (flags & HX_OS_FILE_CREATE)
		native_flags |= O_CREAT;
	if (flags & HX_OS_FILE_TRUNCATE)
		native_flags |= O_TRUNC;
	if (flags & HX_OS_FILE_APPEND)
		native_flags |= O_APPEND;
	if (flags & HX_OS_FILE_EXCLUSIVE)
		native_flags |= O_EXCL;
	file = hx_os_i_zero(context, sizeof(*file));
	if (!file)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	do {
		descriptor = open(path, native_flags, 0600);
	} while (descriptor < 0 && errno == EINTR);
	code = descriptor < 0 ? errno : 0;
	if (!code && fstat(descriptor, &info))
		code = errno;
	if (code || !S_ISREG(info.st_mode)) {
		if (descriptor >= 0)
			(void)close(descriptor);
		hx_os_memory_free(file);
		return code ? hx_os_i_errno(error, code)
			    : hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	file->descriptor = descriptor;
	file->flags = flags;
	*out = file;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_read_some(hx_os_file *file, void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	ssize_t count;
	if (!file || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (bytes > INT_MAX)
		bytes = INT_MAX;
	do {
		count = read(file->descriptor, data, bytes);
	} while (count < 0 && errno == EINTR);
	if (count < 0)
		return hx_os_i_errno(error, errno);
	*completed = (size_t)count;
	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_EOF, 0, 0);
}

hx_os_status hx_os_file_write_some(hx_os_file *file, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	ssize_t count;
	if (!file || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (bytes > INT_MAX)
		bytes = INT_MAX;
#if HX_OS_ENABLE_PIPE
	if (file->pipe)
		return hx_os_i_pipe_write(
			file->descriptor, data, bytes, completed, error);
#endif
	do {
		count = write(file->descriptor, data, bytes);
	} while (count < 0 && errno == EINTR);
	if (count < 0)
		return hx_os_i_errno(error, errno);
	*completed = (size_t)count;
	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_IO_ERROR, 0, 0);
}

hx_os_status hx_os_file_sync(hx_os_file *file, hx_os_error *error)
{
	int result;
	if (!file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (file->pipe)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	do {
		result = fsync(file->descriptor);
	} while (result && errno == EINTR);
	return hx_os_i_errno(error, result ? errno : 0);
}

hx_os_status hx_os_file_close(hx_os_file **file, hx_os_error *error)
{
	int code;
	if (!file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*file)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	code = close((*file)->descriptor) ? errno : 0;
	hx_os_memory_free(*file);
	*file = NULL;
	return hx_os_i_errno(error, code);
}

hx_os_status hx_os_file_size(
	hx_os_file *file, uint64_t *size, hx_os_error *error)
{
	struct stat info;
	if (!file || !size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (file->pipe)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (fstat(file->descriptor, &info))
		return hx_os_i_errno(error, errno);
	if (info.st_size < 0)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*size = (uint64_t)info.st_size;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_seek(hx_os_file *file, int64_t offset, int origin,
	uint64_t *position, hx_os_error *error)
{
	off_t result;
	int native_origin;
	if (!file || !position || origin < 0 || origin > 2)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (file->pipe)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if ((int64_t)(off_t)offset != offset)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	native_origin = origin == 0 ? SEEK_SET
		: origin == 1	    ? SEEK_CUR
				    : SEEK_END;
	result = lseek(file->descriptor, (off_t)offset, native_origin);
	if (result < 0)
		return hx_os_i_errno(error, errno);
	*position = (uint64_t)result;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_truncate(
	hx_os_file *file, uint64_t size, hx_os_error *error)
{
	int result;
	if (!file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (file->pipe)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (size > INT64_MAX || (off_t)size < 0 ||
		(uint64_t)(off_t)size != size)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	do {
		result = ftruncate(file->descriptor, (off_t)size);
	} while (result && errno == EINTR);
	return hx_os_i_errno(error, result ? errno : 0);
}

hx_os_status hx_os_path_stat(const char *path, int follow_links,
	hx_os_file_info *info, hx_os_error *error)
{
	struct stat native;
	int result;
	hx_os_status status;
	if (!info)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	result = follow_links ? stat(path, &native) : lstat(path, &native);
	if (result)
		return hx_os_i_errno(error, errno);
	memset(info, 0, sizeof(*info));
	info->size = native.st_size < 0 ? 0 : (uint64_t)native.st_size;
	info->kind = S_ISREG(native.st_mode) ? HX_OS_FILE_REGULAR
		: S_ISDIR(native.st_mode)    ? HX_OS_FILE_DIRECTORY
		: S_ISLNK(native.st_mode)    ? HX_OS_FILE_LINK
					     : HX_OS_FILE_OTHER;
#if defined(__APPLE__)
	info->modified_seconds = native.st_mtimespec.tv_sec;
	info->modified_nanoseconds = (uint32_t)native.st_mtimespec.tv_nsec;
#else
	info->modified_seconds = native.st_mtim.tv_sec;
	info->modified_nanoseconds = (uint32_t)native.st_mtim.tv_nsec;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_remove(const char *path, hx_os_error *error)
{
	hx_os_status status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	return hx_os_i_errno(error, unlink(path) ? errno : 0);
}

hx_os_status hx_os_file_move(
	const char *from, const char *to, int replace, hx_os_error *error)
{
	int result;
	hx_os_status status = hx_os_i_path_validate(from, error);
	if (status == HX_OS_OK)
		status = hx_os_i_path_validate(to, error);
	if (status != HX_OS_OK)
		return status;
	if (replace)
		result = rename(from, to);
	else {
#if defined(__linux__) && defined(SYS_renameat2)
		result = (int)syscall(
			SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to, 1u);
#elif defined(__APPLE__) && defined(RENAME_EXCL)
		result = renamex_np(from, to, RENAME_EXCL);
#else
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
		if (result &&
			(errno == ENOSYS || errno == EINVAL ||
				errno == EOPNOTSUPP))
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED,
				HX_OS_ERROR_ERRNO, errno);
	}
	return hx_os_i_errno(error, result ? errno : 0);
}

hx_os_status hx_os_i_directory_create_one(const char *path, hx_os_error *error)
{
	return hx_os_i_errno(error, mkdir(path, 0700) ? errno : 0);
}

hx_os_status hx_os_directory_remove(const char *path, hx_os_error *error)
{
	hx_os_status status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	return hx_os_i_errno(error, rmdir(path) ? errno : 0);
}

hx_os_status hx_os_directory_open(hx_os_context *context, const char *path,
	hx_os_directory **out, hx_os_error *error)
{
	hx_os_directory *directory;
	hx_os_status status;
	int code;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	directory = hx_os_i_zero(context, sizeof(*directory));
	if (!directory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	directory->native = opendir(path);
	code = directory->native ? 0 : errno;
	if (code) {
		hx_os_memory_free(directory);
		return hx_os_i_errno(error, code);
	}
	*out = directory;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_directory_next(hx_os_directory *directory, char *name,
	size_t capacity, size_t *required, hx_os_error *error)
{
	const char *text;
	size_t bytes;
	size_t offset;
	hx_os_status status;
	if (!directory || !required || (!name && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	while (!directory->pending) {
		errno = 0;
		directory->pending = readdir(directory->native);
		if (!directory->pending)
			return errno ? hx_os_i_errno(error, errno)
				     : hx_os_i_error(error, HX_OS_EOF, 0, 0);
		text = directory->pending->d_name;
		if (!strcmp(text, ".") || !strcmp(text, ".."))
			directory->pending = NULL;
	}
	text = directory->pending->d_name;
	bytes = strlen(text);
	status = hx_os_text_validate_utf8(text, bytes, &offset);
	if (status != HX_OS_OK) {
		directory->pending = NULL;
		return hx_os_i_error(error, status, 0, 0);
	}
	*required = bytes + 1;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	memcpy(name, text, *required);
	directory->pending = NULL;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_directory_close(hx_os_directory *directory)
{
	if (!directory)
		return;
	(void)closedir(directory->native);
	hx_os_memory_free(directory);
}
