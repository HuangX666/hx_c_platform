#include "shared_memory_private.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

hx_os_status hx_os_i_shm_native_open(hx_os_shared_memory *memory,
	const char *name, size_t bytes, int create, hx_os_error *error)
{
#ifndef __ANDROID__
	char native[80];
	int fd, code;
	struct stat info;
	void *mapping;
	size_t size = create ? bytes + 64 : 64;

	(void)snprintf(native, sizeof(native), "/hxcp_%s", name);
	fd = shm_open(native,
		O_RDWR | O_CLOEXEC | (create ? O_CREAT | O_EXCL : 0), 0600);
	if (fd < 0)
		return hx_os_i_errno(error, errno);
	if (create && ftruncate(fd, (off_t)size))
		goto failed;
	if (!create) {
		if (fstat(fd, &info))
			goto failed;
		if (info.st_size < 64) {
			(void)close(fd);
			return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
		}
	}
	mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED)
		goto failed;
	memory->native = fd;
	memory->mapping = mapping;
	memory->mapped_bytes = size;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
failed:
	code = errno;
	(void)close(fd);
	if (create)
		(void)shm_unlink(native);
	return hx_os_i_errno(error, code);
#else
	(void)memory;
	(void)name;
	(void)bytes;
	(void)create;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_i_shm_native_remap(
	hx_os_shared_memory *memory, size_t bytes, hx_os_error *error)
{
	void *mapping;
	struct stat info;
	int fd = (int)memory->native;

	if (fstat(fd, &info))
		return hx_os_i_errno(error, errno);
	if (info.st_size < 0 || (uint64_t)info.st_size != bytes)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	mapping = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED)
		return hx_os_i_errno(error, errno);
	if (munmap(memory->mapping, memory->mapped_bytes)) {
		int code = errno;

		(void)munmap(mapping, bytes);
		return hx_os_i_errno(error, code);
	}
	memory->mapping = mapping;
	memory->mapped_bytes = bytes;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_shm_native_close(
	hx_os_shared_memory *memory, hx_os_error *error)
{
	if (!memory->mapping)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (munmap(memory->mapping, memory->mapped_bytes))
		return hx_os_i_errno(error, errno);
	memory->mapping = NULL;
	return close((int)memory->native)
		? hx_os_i_errno(error, errno)
		: hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_shm_native_unlink(const char *name, hx_os_error *error)
{
#ifndef __ANDROID__
	char native[80];

	(void)snprintf(native, sizeof(native), "/hxcp_%s", name);
	return shm_unlink(native) ? hx_os_i_errno(error, errno)
				  : hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)name;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
