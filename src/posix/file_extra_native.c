#include "file_extra_private.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

hx_os_status hx_os_i_file_lock(hx_os_file *file, uint64_t offset,
	uint64_t bytes, int shared, int unlock, hx_os_error *error)
{
	struct flock lock;
	int result;

	memset(&lock, 0, sizeof(lock));
	lock.l_type = unlock ? F_UNLCK : shared ? F_RDLCK : F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = (off_t)offset;
	lock.l_len = (off_t)bytes;
	do {
		result = fcntl(file->descriptor, F_SETLK, &lock);
	} while (result != 0 && errno == EINTR);
	if (result != 0) {
		int code = errno;

		if (!unlock && (code == EACCES || code == EAGAIN))
			return hx_os_i_error(error, HX_OS_WOULD_BLOCK,
				HX_OS_ERROR_ERRNO, code);

		return hx_os_i_errno(error, code);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_file_set_times(const char *path,
	const hx_os_file_times *times, int follow, hx_os_error *error)
{
	struct timespec native[2];
	int result;

	memset(native, 0, sizeof(native));
	native[0].tv_nsec = UTIME_OMIT;
	native[1].tv_nsec = UTIME_OMIT;
	if (times->fields & HX_OS_TIME_ACCESSED) {
		native[0].tv_sec = (time_t)times->accessed_seconds;
		native[0].tv_nsec = (long)times->accessed_nanoseconds;
		if ((int64_t)native[0].tv_sec != times->accessed_seconds)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	if (times->fields & HX_OS_TIME_MODIFIED) {
		native[1].tv_sec = (time_t)times->modified_seconds;
		native[1].tv_nsec = (long)times->modified_nanoseconds;
		if ((int64_t)native[1].tv_sec != times->modified_seconds)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	do {
		result = utimensat(AT_FDCWD, path, native,
			follow ? 0 : AT_SYMLINK_NOFOLLOW);
	} while (result != 0 && errno == EINTR);
	if (result != 0)
		return hx_os_i_errno(error, errno);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_path_permissions(
	const char *path, unsigned permissions, int follow, hx_os_error *error)
{
	struct stat info;
	mode_t mode;
	int result;

	if (fstatat(AT_FDCWD, path, &info, follow ? 0 : AT_SYMLINK_NOFOLLOW) !=
		0)
		return hx_os_i_errno(error, errno);
	mode = info.st_mode & (mode_t)07777;
	mode &= (mode_t) ~(S_IRUSR | S_IWUSR | S_IXUSR);
	if (permissions & HX_OS_PERMISSION_OWNER_READ)
		mode |= S_IRUSR;

	if (permissions & HX_OS_PERMISSION_OWNER_WRITE)
		mode |= S_IWUSR;

	if (permissions & HX_OS_PERMISSION_OWNER_EXECUTE)
		mode |= S_IXUSR;

	do {
		result = fchmodat(
			AT_FDCWD, path, mode, follow ? 0 : AT_SYMLINK_NOFOLLOW);
	} while (result != 0 && errno == EINTR);
	if (result != 0) {
		int code = errno;

		if (code == ENOTSUP || code == EOPNOTSUPP)
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED,
				HX_OS_ERROR_ERRNO, code);

		return hx_os_i_errno(error, code);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
