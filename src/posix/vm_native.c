#include "vm_private.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static int hx_os_vm_native_protection(unsigned flags)
{
	int value = PROT_NONE;

	if (flags & HX_OS_PAGE_READ)
		value |= PROT_READ;

	if (flags & HX_OS_PAGE_WRITE)
		value |= PROT_WRITE;

	if (flags & HX_OS_PAGE_EXECUTE)
		value |= PROT_EXEC;

	return value;
}

hx_os_status hx_os_i_vm_sizes(
	size_t *page, size_t *granularity, hx_os_error *error)
{
	long value = sysconf(_SC_PAGESIZE);
	if (value <= 0)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	*page = *granularity = (size_t)value;

	return HX_OS_OK;
}

hx_os_status hx_os_i_pages_alloc(
	hx_os_pages *pages, unsigned protection, hx_os_error *error)
{
	void *memory =
		mmap(NULL, pages->bytes, hx_os_vm_native_protection(protection),
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (memory == MAP_FAILED)
		return hx_os_i_errno(error, errno);

	pages->base = memory;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pages_protect(
	hx_os_pages *pages, unsigned protection, hx_os_error *error)
{
	if (mprotect(pages->base, pages->bytes,
		    hx_os_vm_native_protection(protection)) != 0)
		return hx_os_i_errno(error, errno);
	if (protection & HX_OS_PAGE_EXECUTE)
		__builtin___clear_cache((char *)pages->base,
			(char *)pages->base + pages->bytes);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pages_free(hx_os_pages *pages, hx_os_error *error)
{
	return munmap(pages->base, pages->bytes)
		? hx_os_i_errno(error, errno)
		: hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_map_create(hx_os_file *file, uint64_t offset,
	hx_os_file_map *map, hx_os_error *error)
{
	int code, protection = PROT_READ;
	int flags =
		map->mode == HX_OS_MAP_PRIVATE_COPY ? MAP_PRIVATE : MAP_SHARED;

	if (map->mode != HX_OS_MAP_READ)
		protection |= PROT_WRITE;

	do {
		map->descriptor = fcntl(file->descriptor, F_DUPFD_CLOEXEC, 0);
	} while (map->descriptor < 0 && errno == EINTR);
	if (map->descriptor < 0)
		return hx_os_i_errno(error, errno);

	map->base = mmap(NULL, map->mapped_bytes, protection, flags,
		map->descriptor, (off_t)offset);
	if (map->base == MAP_FAILED) {
		code = errno;

		(void)close(map->descriptor);

		return hx_os_i_errno(error, code);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_map_flush(
	hx_os_file_map *map, int durable, hx_os_error *error)
{
	int result;

	do {
		result = msync(map->base, map->mapped_bytes, MS_SYNC);
	} while (result != 0 && errno == EINTR);
	if (result != 0)
		return hx_os_i_errno(error, errno);

	if (durable) {
		do {
			result = fsync(map->descriptor);
		} while (result != 0 && errno == EINTR);
		if (result != 0)
			return hx_os_i_errno(error, errno);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_map_unmap(hx_os_file_map *map, hx_os_error *error)
{
	return munmap(map->base, map->mapped_bytes)
		? hx_os_i_errno(error, errno)
		: hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_map_close(hx_os_file_map *map, hx_os_error *error)
{
	return close(map->descriptor) ? hx_os_i_errno(error, errno)
				      : hx_os_i_error(error, HX_OS_OK, 0, 0);
}
