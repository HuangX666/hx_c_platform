#include "file_utils_private.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/sendfile.h>
#include <sys/syscall.h>
#endif

static int hx_os_file_open_native(const char *path, int flags)
{
	int descriptor;

	do {
		descriptor = open(path, flags | O_CLOEXEC | O_NONBLOCK, 0600);
	} while (descriptor < 0 && errno == EINTR);

	return descriptor;
}

static hx_os_status hx_os_file_copy_bytes(
	int input, int output, int regular_size, hx_os_error *error)
{
	unsigned char *buffer;
	hx_os_status status = HX_OS_OK;
#if defined(__linux__)
	if (regular_size) {
		ssize_t count;
#if defined(SYS_copy_file_range)
		for (;;) {
			count = (ssize_t)syscall(SYS_copy_file_range, input,
				NULL, output, NULL, (size_t)65536, 0);
			if (count > 0 || (count < 0 && errno == EINTR))
				continue;

			if (!count)
				return hx_os_i_error(error, HX_OS_OK, 0, 0);

			if (count < 0 && errno != ENOSYS && errno != EINVAL &&
				errno != EXDEV && errno != EOPNOTSUPP)
				return hx_os_i_errno(error, errno);
			break;
		}
#endif
		for (;;) {
			count = sendfile(output, input, NULL, 65536);
			if (count > 0 || (count < 0 && errno == EINTR))
				continue;

			if (!count)
				return hx_os_i_error(error, HX_OS_OK, 0, 0);

			if (count < 0 && errno != ENOSYS && errno != EINVAL &&
				errno != EXDEV && errno != EOPNOTSUPP)
				return hx_os_i_errno(error, errno);
			break;
		}
	}
#else
	(void)regular_size;
#endif
	buffer = hx_os_i_alloc(NULL, 65536, 0);
	if (!buffer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	for (;;) {
		ssize_t read_bytes;
		size_t used = 0;

		do {
			read_bytes = read(input, buffer, 65536);
		} while (read_bytes < 0 && errno == EINTR);
		if (read_bytes < 0) {
			status = hx_os_i_errno(error, errno);
			break;
		}

		if (!read_bytes)
			break;

		while (used < (size_t)read_bytes) {
			ssize_t written;

			do {
				written = write(output, buffer + used,
					(size_t)read_bytes - used);
			} while (written < 0 && errno == EINTR);
			if (written <= 0) {
				status = written
					? hx_os_i_errno(error, errno)
					: hx_os_i_error(
						  error, HX_OS_IO_ERROR, 0, 0);
				break;
			}

			used += (size_t)written;
		}

		if (status != HX_OS_OK)
			break;
	}

	hx_os_memory_free(buffer);

	return status;
}

hx_os_status hx_os_i_file_copy(
	const char *from, const char *to, int replace, hx_os_error *error)
{
	struct stat source, destination;

	int input = hx_os_file_open_native(from, O_RDONLY);

	int output = -1, code = 0, result;
	hx_os_status status = HX_OS_OK;

	if (input < 0)
		return hx_os_i_errno(error, errno);

	if (fstat(input, &source))
		status = hx_os_i_errno(error, errno);
	else if (!S_ISREG(source.st_mode))
		status = hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	if (status == HX_OS_OK) {
		output = hx_os_file_open_native(
			to, O_WRONLY | O_CREAT | (replace ? 0 : O_EXCL));
		if (output < 0)
			status = hx_os_i_errno(error, errno);
		else if (fstat(output, &destination))
			status = hx_os_i_errno(error, errno);
		else if (!S_ISREG(destination.st_mode))
			status =
				hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		else if (source.st_dev == destination.st_dev &&
			source.st_ino == destination.st_ino)
			status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	if (status == HX_OS_OK) {
		do {
			result = ftruncate(output, 0);
		} while (result && errno == EINTR);

		status = result ? hx_os_i_errno(error, errno)
				: hx_os_file_copy_bytes(input, output,
					  source.st_size != 0, error);
	}

	if (output >= 0 && close(output))
		code = errno;

	if (close(input) && !code)
		code = errno;

	return status == HX_OS_OK ? hx_os_i_errno(error, code) : status;
}

hx_os_status hx_os_i_file_replace(const char *target, const char *source,
	const char *backup, hx_os_error *error)
{
	struct stat from, to, saved;
	hx_os_status status;
	int exists;

	if (lstat(source, &from))
		return hx_os_i_errno(error, errno);

	if (!S_ISREG(from.st_mode))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	exists = lstat(target, &to) == 0;
	if (!exists && errno != ENOENT)
		return hx_os_i_errno(error, errno);

	if (exists &&
		(!S_ISREG(to.st_mode) ||
			(from.st_dev == to.st_dev && from.st_ino == to.st_ino)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (exists && from.st_dev != to.st_dev)
		return hx_os_i_error(
			error, HX_OS_CROSS_DEVICE, HX_OS_ERROR_ERRNO, EXDEV);

	if (backup) {
		if (!lstat(backup, &saved))
			return hx_os_i_error(
				error, HX_OS_EXISTS, HX_OS_ERROR_ERRNO, EEXIST);

		if (errno != ENOENT)
			return hx_os_i_errno(error, errno);

		if (exists) {
			/* The caller excludes competing mutations of all three
			 * paths. */
			status = hx_os_file_move(target, backup, 1, error);
			if (status != HX_OS_OK)
				return status;
		}
	}

	return hx_os_file_move(source, target, 1, error);
}

hx_os_status hx_os_i_file_sync_parent(const char *path, hx_os_error *error)
{
	const char *slash = strrchr(path, '/');

	hx_string parent = HX_STRING_INIT;
	hx_os_status status;
	int descriptor, result, code;
	hx_string_view view = {
		slash ? path : ".", slash ? (size_t)(slash - path) : 1};

	if (slash == path)
		view.size = 1;

	status = hx_string_assign(&parent, view, error);
	if (status != HX_OS_OK)
		return status;

	descriptor = hx_os_file_open_native(
		hx_string_c_str(&parent), O_RDONLY | O_DIRECTORY);
	code = descriptor < 0 ? errno : 0;

	hx_string_destroy(&parent);
	if (code)
		return hx_os_i_errno(error, code);

	do {
		result = fsync(descriptor);
	} while (result && errno == EINTR);
	code = result ? errno : 0;
	if (close(descriptor) && !code)
		code = errno;

	return hx_os_i_errno(error, code);
}

hx_os_status hx_os_i_path_current(hx_string *out, hx_os_error *error)
{
	size_t capacity = 256;

	for (;;) {
		char *buffer = hx_os_i_alloc(out->context, capacity, 0);

		hx_os_status status;
		int code;

		if (!buffer)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		code = getcwd(buffer, capacity) ? 0 : errno;

		status = code ? hx_os_i_errno(error, code)
			      : hx_string_assign_cstr(out, buffer, error);

		hx_os_memory_free(buffer);
		if (code != ERANGE)
			return status;

		if (capacity >= 1024 * 1024)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		capacity *= 2;
	}
}

hx_os_status hx_os_i_path_absolute(
	const char *path, hx_string *out, hx_os_error *error)
{
	char *resolved = realpath(path, NULL);

	hx_os_status status;

	if (!resolved)
		return hx_os_i_errno(error, errno);

	status = hx_string_assign_cstr(out, resolved, error);

	free(resolved);

	return status;
}

hx_os_status hx_os_i_path_change_current(const char *path, hx_os_error *error)
{
	int result;

	do {
		result = chdir(path);
	} while (result && errno == EINTR);

	return hx_os_i_errno(error, result ? errno : 0);
}

hx_os_status hx_os_i_remove_tree(
	const char *path, const hx_os_walk_options *options, hx_os_error *error)
{
	struct hx_os_remove_frame {
		DIR *directory;
		const char *name;
	} *frames;

	struct stat info;
	size_t depth = 0, entries = 0, capacity = options->max_depth;
	int descriptor, code = 0;
	hx_os_status status = HX_OS_OK;

	if (lstat(path, &info))
		return hx_os_i_errno(error, errno);

	if (!S_ISDIR(info.st_mode))
		return hx_os_i_errno(error, unlink(path) ? errno : 0);

	if (capacity * sizeof(*frames) > options->max_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	frames = hx_os_i_zero(NULL, capacity * sizeof(*frames));
	if (!frames)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	descriptor = hx_os_file_open_native(
		path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (descriptor < 0)
		code = errno;
	else {
		frames[0].directory = fdopendir(descriptor);
		if (!frames[0].directory) {
			code = errno;

			(void)close(descriptor);
		}
		else {
			frames[0].name = path;
			depth = 1;
		}
	}

	while (depth && !code && status == HX_OS_OK) {
		struct dirent *entry;

		int parent = dirfd(frames[depth - 1].directory);
		errno = 0;
		entry = readdir(frames[depth - 1].directory);
		if (!entry) {
			const char *name = frames[depth - 1].name;

			code = errno;
			if (code)
				break;

			if (closedir(frames[depth - 1].directory))
				code = errno;

			frames[--depth].directory = NULL;
			if (!code &&
				unlinkat(depth ? dirfd(frames[depth - 1]
								 .directory)
					       : AT_FDCWD,
					name, AT_REMOVEDIR))
				code = errno;
			continue;
		}

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;

		if (entries++ == options->max_entries) {
			status = HX_OS_LIMIT;
			break;
		}

		{
			size_t invalid;

			status = hx_os_text_validate_utf8(
				entry->d_name, strlen(entry->d_name), &invalid);
			if (status != HX_OS_OK)
				break;
		}

		if (fstatat(parent, entry->d_name, &info,
			    AT_SYMLINK_NOFOLLOW)) {
			code = errno;
			break;
		}

		if (!S_ISDIR(info.st_mode)) {
			if (unlinkat(parent, entry->d_name, 0))
				code = errno;

			continue;
		}

		if (depth == capacity) {
			status = HX_OS_LIMIT;
			break;
		}

		do {
			descriptor = openat(parent, entry->d_name,
				O_RDONLY | O_DIRECTORY | O_CLOEXEC |
					O_NOFOLLOW);
		} while (descriptor < 0 && errno == EINTR);
		if (descriptor < 0) {
			code = errno;
			break;
		}

		frames[depth].directory = fdopendir(descriptor);
		if (!frames[depth].directory) {
			code = errno;

			(void)close(descriptor);
			break;
		}

		frames[depth++].name = entry->d_name;
	}

	while (depth)
		(void)closedir(frames[--depth].directory);

	hx_os_memory_free(frames);

	return code ? hx_os_i_errno(error, code)
		    : hx_os_i_error(error, status, 0, 0);
}
