#include "vm_private.h"

#if HX_OS_ENABLE_VIRTUAL_MEMORY
static hx_os_status hx_os_vm_protection(unsigned flags, hx_os_error *error)
{
	if (flags &
		~(unsigned)(HX_OS_PAGE_READ | HX_OS_PAGE_WRITE |
			HX_OS_PAGE_EXECUTE))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (flags && !(flags & HX_OS_PAGE_READ))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	return HX_OS_OK;
}
#endif

hx_os_status hx_os_pages_alloc(hx_os_context *context, size_t bytes,
	unsigned protection, hx_os_pages **out, hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	size_t page, granularity, rounded;
	hx_os_pages *value;
	hx_os_status status;
	if (!out || !bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_vm_protection(protection, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_vm_sizes(&page, &granularity, error);
	if (status != HX_OS_OK)
		return status;
	if (bytes > SIZE_MAX - (page - 1))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	rounded = (bytes + page - 1) / page * page;
	value = hx_os_i_zero(context, sizeof(*value));
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	value->bytes = rounded;
	status = hx_os_i_pages_alloc(value, protection, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(value);
		return status;
	}
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)bytes;
	(void)protection;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void *hx_os_pages_data(hx_os_pages *pages)
{
	return pages ? pages->base : NULL;
}

size_t hx_os_pages_size(const hx_os_pages *pages)
{
	return pages ? pages->bytes : 0;
}

hx_os_status hx_os_pages_protect(
	hx_os_pages *pages, unsigned protection, hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	hx_os_status status;
	if (!pages)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_vm_protection(protection, error);
	return status == HX_OS_OK
		? hx_os_i_pages_protect(pages, protection, error)
		: status;
#else
	(void)pages;
	(void)protection;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_pages_free(hx_os_pages **pages, hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	hx_os_status status;
	if (!pages)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*pages)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_i_pages_free(*pages, error);
	if (status == HX_OS_OK) {
		hx_os_memory_free(*pages);
		*pages = NULL;
	}
	return status;
#else
	(void)pages;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_file_map_create(hx_os_context *context, hx_os_file *file,
	uint64_t offset, uint64_t bytes, int mode, hx_os_file_map **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	hx_os_file_map *value;
	uint64_t size;
	size_t page, granularity, delta;
	hx_os_status status;
	if (!out || !file || mode < HX_OS_MAP_READ ||
		mode > HX_OS_MAP_PRIVATE_COPY)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (file->pipe)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!(file->flags & HX_OS_FILE_READ) ||
		(mode == HX_OS_MAP_SHARED_WRITE &&
			!(file->flags & HX_OS_FILE_WRITE)))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (mode == HX_OS_MAP_SHARED_WRITE && (file->flags & HX_OS_FILE_APPEND))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_file_size(file, &size, error);
	if (status != HX_OS_OK)
		return status;
	if (offset > size || offset > INT64_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!bytes)
		bytes = size - offset;
	if (bytes > size - offset || bytes > SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_EOF, 0, 0);
	status = hx_os_i_vm_sizes(&page, &granularity, error);
	if (status != HX_OS_OK)
		return status;
	delta = (size_t)(offset % granularity);
	if (bytes > SIZE_MAX - delta)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	value = hx_os_i_zero(context, sizeof(*value));
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	value->mapped_bytes = (size_t)bytes + delta;
	value->delta = delta;
	value->bytes = (size_t)bytes;
	value->mode = mode;
	status = hx_os_i_map_create(file, offset - delta, value, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(value);
		return status;
	}
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)file;
	(void)offset;
	(void)bytes;
	(void)mode;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_file_map_open(
	const char *path, int mode, hx_os_file_map **out, hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	hx_os_file *file = NULL;
	hx_os_status status, closed;
	hx_os_error close_error;
	uint32_t flags = HX_OS_FILE_READ;
	if (!out || mode < HX_OS_MAP_READ || mode > HX_OS_MAP_PRIVATE_COPY)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (mode == HX_OS_MAP_SHARED_WRITE)
		flags |= HX_OS_FILE_WRITE;
	status = hx_os_file_open(NULL, path, flags, &file, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_file_map_create(NULL, file, 0, 0, mode, out, error);
	closed = hx_os_file_close(&file, &close_error);
	if (status == HX_OS_OK && closed != HX_OS_OK) {
		(void)hx_os_file_map_destroy(out, NULL);
		if (error)
			*error = close_error;
		return closed;
	}
	return status;
#else
	(void)path;
	(void)mode;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

const void *hx_os_file_map_view(const hx_os_file_map *map)
{
	return map ? (const unsigned char *)map->base + map->delta : NULL;
}

void *hx_os_file_map_write_view(hx_os_file_map *map)
{
	return map && map->mode != HX_OS_MAP_READ
		? (unsigned char *)map->base + map->delta
		: NULL;
}

size_t hx_os_file_map_size(const hx_os_file_map *map)
{
	return map ? map->bytes : 0;
}

hx_os_status hx_os_file_map_flush(
	hx_os_file_map *map, int durable, hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	if (!map || (durable != 0 && durable != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (map->mode != HX_OS_MAP_SHARED_WRITE)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	return hx_os_i_map_flush(map, durable, error);
#else
	(void)map;
	(void)durable;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_file_map_destroy(hx_os_file_map **map, hx_os_error *error)
{
#if HX_OS_ENABLE_VIRTUAL_MEMORY
	hx_os_status status;
	if (!map)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*map)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_i_map_unmap(*map, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_i_map_close(*map, error);
	hx_os_memory_free(*map);
	*map = NULL;
	return status;
#else
	(void)map;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
