#include "internal.h"
#include "hx_os_util.h"
#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static int hx_os_random_device(const char *path, unsigned int minor_number)
{
	struct stat info;
	int descriptor;
	do {
		descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	} while (descriptor < 0 && errno == EINTR);
	if (descriptor < 0)
		return -1;
	if (fstat(descriptor, &info) != 0) {
		int code = errno;
		(void)close(descriptor);
		errno = code;
		return -1;
	}
	if (!S_ISCHR(info.st_mode) || major(info.st_rdev) != 1 ||
		minor(info.st_rdev) != minor_number) {
		(void)close(descriptor);
		errno = ENODEV;
		return -1;
	}
	return descriptor;
}

static hx_os_status hx_os_random_read(
	int descriptor, void *buffer, size_t bytes, hx_os_error *error)
{
	unsigned char *output = buffer;
	while (bytes) {
		size_t chunk = bytes > 262144 ? 262144 : bytes;
		ssize_t got = read(descriptor, output, chunk);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return hx_os_i_errno(error, errno);
		}
		if (!got)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		output += (size_t)got;
		bytes -= (size_t)got;
	}
	return HX_OS_OK;
}

static hx_os_status hx_os_random_legacy(
	void *buffer, size_t bytes, hx_os_error *error)
{
	unsigned char seed[32];
	hx_os_status status;
	int descriptor = hx_os_random_device("/dev/random", 8);
	if (descriptor < 0)
		return hx_os_i_errno(error, errno);
	/* Old kernels lack getrandom's boot-time readiness guarantee. */
	status = hx_os_random_read(descriptor, seed, sizeof(seed), error);
	(void)close(descriptor);
	hx_os_secure_zero(seed, sizeof(seed));
	if (status != HX_OS_OK)
		return status;
	descriptor = hx_os_random_device("/dev/urandom", 9);
	if (descriptor < 0)
		return hx_os_i_errno(error, errno);
	status = hx_os_random_read(descriptor, buffer, bytes, error);
	(void)close(descriptor);
	return status;
}
#endif

hx_os_status hx_os_i_random_bytes(
	void *buffer, size_t bytes, hx_os_error *error)
{
#if defined(__APPLE__)
	arc4random_buf(buffer, bytes);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#elif defined(__linux__)
	unsigned char *output = buffer;
#if defined(SYS_getrandom)
	while (bytes) {
		size_t chunk = bytes > 262144 ? 262144 : bytes;
		long got = syscall(SYS_getrandom, output, chunk, 0);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			if (errno == ENOSYS)
				break;
			return hx_os_i_errno(error, errno);
		}
		if (!got)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		output += (size_t)got;
		bytes -= (size_t)got;
	}
#endif
	if (bytes) {
		hx_os_status status = hx_os_random_legacy(output, bytes, error);
		if (status != HX_OS_OK)
			return status;
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
#error Random backend not adapted
#endif
}
