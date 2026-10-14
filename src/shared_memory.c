#include "shared_memory_private.h"

int hx_os_shared_memory_available(void)
{
	return HX_OS_HAVE_SHARED_MEMORY && hx_os_atomic32_is_lock_free();
}

int hx_os_shared_memory_native_available(void)
{
#ifdef __ANDROID__
	return 0;
#else
	return hx_os_shared_memory_available();
#endif
}

static hx_os_status hx_os_shm_name(const char *name, hx_os_error *error)
{
	size_t i;

	if (!name || !*name)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (i = 0; name[i]; ++i) {
		unsigned char ch = (unsigned char)name[i];

		if (i >= 64 ||
			!((ch >= 'a' && ch <= 'z') ||
				(ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') || ch == '_' ||
				ch == '-' || ch == '.'))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
	return HX_OS_OK;
}

static hx_os_status hx_os_shm_path(const char *directory, const char *name,
	hx_string *out, hx_os_error *error)
{
	hx_os_status status = hx_os_shm_name(name, error);

	if (status != HX_OS_OK)
		return status;
	if (!directory || !*directory)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_string_format(out, error, "%s/hxcp_%s.shm", directory, name);
}

static hx_os_status hx_os_shm_file_open(hx_os_shared_memory *memory,
	const char *directory, const char *name, size_t bytes, int create,
	hx_os_error *error)
{
	hx_string path;
	uint64_t size = 0;
	hx_os_status status;
	uint32_t flags = HX_OS_FILE_READ | HX_OS_FILE_WRITE;

	hx_string_init(&path, memory->context);
	status = hx_os_shm_path(directory, name, &path, error);
	if (create)
		flags |= HX_OS_FILE_CREATE | HX_OS_FILE_EXCLUSIVE;
	if (status == HX_OS_OK)
		status = hx_os_file_open(memory->context,
			hx_string_c_str(&path), flags, &memory->file, error);
	if (status == HX_OS_OK && create)
		status = hx_os_file_truncate(memory->file, bytes + 64, error);
	if (status == HX_OS_OK && !create) {
		status = hx_os_file_size(memory->file, &size, error);
		if (status == HX_OS_OK && size < 64)
			status = hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	}
	if (status == HX_OS_OK)
		status = hx_os_file_map_create(memory->context, memory->file, 0,
			create ? bytes + 64 : 64, HX_OS_MAP_SHARED_WRITE,
			&memory->file_map, error);
	if (status == HX_OS_OK) {
		memory->mapping = hx_os_file_map_write_view(memory->file_map);
		memory->mapped_bytes = create ? bytes + 64 : 64;
	}
	else if (create && memory->file) {
		(void)hx_os_file_close(&memory->file, NULL);
		(void)hx_os_file_remove(hx_string_c_str(&path), NULL);
	}
	hx_string_destroy(&path);
	return status;
}

static hx_os_status hx_os_shm_remap(
	hx_os_shared_memory *memory, size_t bytes, hx_os_error *error)
{
	hx_os_file_map *map = NULL;
	hx_os_status status;
	uint64_t size;

	if (!memory->file_map)
		return hx_os_i_shm_native_remap(memory, bytes, error);
	status = hx_os_file_size(memory->file, &size, error);
	if (status == HX_OS_OK && size != bytes)
		status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_file_map_create(memory->context, memory->file, 0,
			bytes, HX_OS_MAP_SHARED_WRITE, &map, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_file_map_destroy(&memory->file_map, error);
	if (status != HX_OS_OK) {
		(void)hx_os_file_map_destroy(&map, NULL);
		if (!memory->file_map)
			memory->mapping = NULL;
		return status;
	}
	memory->file_map = map;
	memory->mapping = hx_os_file_map_write_view(map);
	memory->mapped_bytes = bytes;
	return HX_OS_OK;
}

static hx_os_status hx_os_shm_open(hx_os_context *context,
	const char *directory, const char *name, size_t bytes, int create,
	hx_os_shared_memory **out, hx_os_error *error)
{
	hx_os_shared_memory *memory;
	hx_os_shm_header *header;
	hx_os_status status;
	size_t i;
	uint64_t count = 0;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!hx_os_shared_memory_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_shm_name(name, error);
	if (status != HX_OS_OK)
		return status;
	if (create && (!bytes || bytes > 64 * 1024 * 1024))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	memory = hx_os_i_zero(context, sizeof(*memory));
	if (!memory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	memory->context = context;
	status = directory
		? hx_os_shm_file_open(
			  memory, directory, name, bytes, create, error)
		: hx_os_i_shm_native_open(memory, name, bytes, create, error);
	if (status != HX_OS_OK)
		goto failed;
	header = memory->mapping;
	if (create) {
		hx_os_atomic32_store(&header->ready, 0, HX_OS_RELAXED);
		memcpy(header->fields, "HXSM", 4);
		header->fields[4] = 1;
		for (i = 0; i < 8; ++i)
			header->fields[12 + i] =
				(unsigned char)((uint64_t)bytes >> (i * 8));
		hx_os_atomic32_store(&header->ready, 1, HX_OS_RELEASE);
	}
	else {
		int32_t ready =
			hx_os_atomic32_load(&header->ready, HX_OS_ACQUIRE);

		if (!ready) {
			status = hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
			goto failed;
		}
		if (ready != 1 || memcmp(header->fields, "HXSM", 4) ||
			header->fields[4] != 1) {
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			goto failed;
		}
		for (i = 0; i < 8; ++i)
			count |= (uint64_t)header->fields[12 + i] << (i * 8);
		if (!count || count > 64 * 1024 * 1024) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			goto failed;
		}
		bytes = (size_t)count;
		status = hx_os_shm_remap(memory, bytes + 64, error);
		if (status != HX_OS_OK)
			goto failed;
	}
	memory->bytes = bytes;
	*out = memory;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
failed:
	(void)hx_os_shared_memory_close(&memory, NULL);
	return status;
}

hx_os_status hx_os_shared_memory_create(hx_os_context *context,
	const char *name, size_t bytes, hx_os_shared_memory **out,
	hx_os_error *error)
{
	return hx_os_shm_open(context, NULL, name, bytes, 1, out, error);
}

hx_os_status hx_os_shared_memory_open(hx_os_context *context, const char *name,
	hx_os_shared_memory **out, hx_os_error *error)
{
	return hx_os_shm_open(context, NULL, name, 0, 0, out, error);
}

hx_os_status hx_os_shared_memory_create_at(hx_os_context *context,
	const char *directory, const char *name, size_t bytes,
	hx_os_shared_memory **out, hx_os_error *error)
{
	if (!directory)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_shm_open(context, directory, name, bytes, 1, out, error);
}

hx_os_status hx_os_shared_memory_open_at(hx_os_context *context,
	const char *directory, const char *name, hx_os_shared_memory **out,
	hx_os_error *error)
{
	if (!directory)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_shm_open(context, directory, name, 0, 0, out, error);
}

void *hx_os_shared_memory_data(hx_os_shared_memory *memory)
{
	return memory && memory->mapping ? (unsigned char *)memory->mapping + 64
					 : NULL;
}

size_t hx_os_shared_memory_size(const hx_os_shared_memory *memory)
{
	return memory ? memory->bytes : 0;
}

hx_os_status hx_os_shared_memory_unlink(const char *name, hx_os_error *error)
{
	hx_os_status status = hx_os_shm_name(name, error);

	if (status != HX_OS_OK)
		return status;
	if (!hx_os_shared_memory_native_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	return hx_os_i_shm_native_unlink(name, error);
}

hx_os_status hx_os_shared_memory_unlink_at(
	const char *directory, const char *name, hx_os_error *error)
{
	hx_string path = HX_STRING_INIT;
	hx_os_status status;

	if (!hx_os_shared_memory_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_shm_path(directory, name, &path, error);
	if (status == HX_OS_OK)
		status = hx_os_file_remove(hx_string_c_str(&path), error);
	hx_string_destroy(&path);
	return status;
}

hx_os_status hx_os_shared_memory_close(
	hx_os_shared_memory **memory, hx_os_error *error)
{
	hx_os_shared_memory *value;
	hx_os_status status;

	if (!memory)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *memory;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (value->file_map || value->file) {
		status = hx_os_file_map_destroy(&value->file_map, error);
		if (value->file_map)
			return status;
		value->mapping = NULL;
		if (status == HX_OS_OK)
			status = hx_os_file_close(&value->file, error);
		else
			(void)hx_os_file_close(&value->file, NULL);
	}
	else {
		status = hx_os_i_shm_native_close(value, error);
		if (value->mapping)
			return status;
	}
	*memory = NULL;
	hx_os_memory_free(value);
	return status;
}
