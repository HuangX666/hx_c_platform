#include "file_utils_private.h"

#include <stdio.h>

static int hx_os_file_native_separator(char value)
{
#if defined(_WIN32)
	return hx_os_i_path_separator(value);
#else
	return value == '/';
#endif
}

static hx_os_status hx_os_file_paths(
	const char *first, const char *second, hx_os_error *error)
{
	hx_os_status status = hx_os_i_path_validate(first, error);

	return status == HX_OS_OK ? hx_os_i_path_validate(second, error)
				  : status;
}

hx_os_status hx_os_file_copy(
	const char *from, const char *to, int replace, hx_os_error *error)
{
	hx_os_status status = hx_os_file_paths(from, to, error);

	return status == HX_OS_OK
		? hx_os_i_file_copy(from, to, replace != 0, error)
		: status;
}

hx_os_status hx_os_file_exists(
	const char *path, int *exists, hx_os_error *error)
{
	hx_os_file_info info;
	hx_os_status status;

	if (!exists)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_path_stat(path, 1, &info, error);
	if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
		return status;

	*exists = status == HX_OS_OK;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_load(hx_os_context *context, const char *path,
	size_t max_bytes, void **data, size_t *bytes, hx_os_error *error)
{
	hx_os_file *file = NULL;
	unsigned char *memory;
	size_t used = 0, capacity = max_bytes < 4096 ? max_bytes : 4096;
	hx_os_status status;

	if (!data || !bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*data = NULL;
	*bytes = 0;

	status = hx_os_file_open(context, path, HX_OS_FILE_READ, &file, error);
	if (status != HX_OS_OK)
		return status;

	memory = hx_os_i_alloc(context, capacity, 0);
	if (!memory)
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	while (status == HX_OS_OK) {
		size_t completed = 0;

		if (used == max_bytes) {
			unsigned char extra;

			status = hx_os_file_read_some(
				file, &extra, 1, &completed, error);
			if (status == HX_OS_OK && completed)
				status =
					hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

			break;
		}

		if (used == capacity) {
			size_t growth = capacity > max_bytes - capacity
				? max_bytes
				: capacity * 2;

			unsigned char *next = hx_os_i_alloc(context, growth, 0);
			if (!next) {
				status = hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
				break;
			}

			memcpy(next, memory, used);

			hx_os_memory_free(memory);
			memory = next;
			capacity = growth;
		}

		status = hx_os_file_read_some(file, memory + used,
			capacity - used, &completed, error);
		used += completed;
		if (status == HX_OS_OK && !completed)
			status = HX_OS_EOF;
	}

	if (status == HX_OS_EOF)
		status = hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (status == HX_OS_OK)
		status = hx_os_file_close(&file, error);
	else
		(void)hx_os_file_close(&file, NULL);

	if (status != HX_OS_OK) {
		hx_os_memory_free(memory);

		return status;
	}

	*data = memory;
	*bytes = used;

	return HX_OS_OK;
}

hx_os_status hx_os_file_load_utf8(hx_os_context *context, const char *path,
	size_t max_bytes, hx_string *out, int strip_bom, hx_os_error *error)
{
	void *data;
	size_t bytes, prefix = 0;
	hx_string_view view;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status =
		hx_os_file_load(context, path, max_bytes, &data, &bytes, error);
	if (status != HX_OS_OK)
		return status;

	if (strip_bom && bytes >= 3 && !memcmp(data, "\xef\xbb\xbf", 3))
		prefix = 3;

	view.data = (const char *)data + prefix;
	view.size = bytes - prefix;

	status = hx_string_assign(out, view, error);

	hx_os_memory_free(data);

	return status;
}

hx_os_status hx_os_file_read_text(
	const char *path, hx_string *out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_file_load_utf8(
		out->context, path, 16 * 1024 * 1024, out, 1, error);
}

hx_os_status hx_os_file_save(const char *path, const void *data, size_t bytes,
	int replace, hx_os_error *error)
{
	hx_os_file *file = NULL;
	size_t completed;
	hx_os_status status;
	uint32_t flags = HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
		(replace ? HX_OS_FILE_TRUNCATE : HX_OS_FILE_EXCLUSIVE);

	if (!data && bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_file_open(NULL, path, flags, &file, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_file_write_all(file, data, bytes, &completed, error);
	if (status == HX_OS_OK)
		status = hx_os_file_close(&file, error);
	else
		(void)hx_os_file_close(&file, NULL);

	return status;
}

hx_os_status hx_os_file_save_atomic_ex(const char *path, const void *data,
	size_t bytes, hx_os_file_save_result *result, hx_os_error *error)
{
	hx_string temporary = HX_STRING_INIT;
	hx_os_file *file = NULL;
	hx_os_file_save_result value;
	hx_os_status status;
	size_t parent, completed;
	unsigned attempt;
	char suffix[96];

	static hx_os_atomic sequence = HX_OS_ATOMIC_INIT(0);
	memset(&value, 0, sizeof(value));
	if (result)
		*result = value;

	if (!data && bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;

	parent = strlen(path);

	while (parent && !hx_os_file_native_separator(path[parent - 1]))
		--parent;
#if defined(_WIN32)
	if (!parent && path[1] == ':')
		parent = 2;
#endif
	for (attempt = 0; attempt < 128; ++attempt) {
		hx_string_view directory = {path, parent};

		uint64_t count = (uint64_t)hx_os_atomic_fetch_add(
			&sequence, 1, HX_OS_RELAXED);
		(void)snprintf(suffix, sizeof(suffix), ".hx-tmp-%llx-%llx-%llx",
			(unsigned long long)hx_os_monotonic_ns(),
			(unsigned long long)hx_os_thread_id(),
			(unsigned long long)count);

		status = hx_string_assign(&temporary, directory, error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(
				&temporary, suffix, error);

		if (status != HX_OS_OK)
			break;

		status = hx_os_file_open(NULL, hx_string_c_str(&temporary),
			HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
				HX_OS_FILE_EXCLUSIVE,
			&file, error);
		if (status != HX_OS_EXISTS)
			break;
	}

	if (status != HX_OS_OK) {
		hx_string_destroy(&temporary);

		return status;
	}

	status = hx_os_file_write_all(file, data, bytes, &completed, error);
	if (status == HX_OS_OK)
		status = hx_os_file_sync(file, error);

	if (status == HX_OS_OK)
		status = hx_os_file_close(&file, error);
	else
		(void)hx_os_file_close(&file, NULL);

	if (status == HX_OS_OK)
		status = hx_os_file_move(
			hx_string_c_str(&temporary), path, 1, error);

	if (status == HX_OS_OK) {
		value.published = 1;
		value.directory_synced =
			hx_os_i_file_sync_parent(
				path, &value.durability_error) == HX_OS_OK;
	}
	else
		(void)hx_os_file_remove(hx_string_c_str(&temporary), NULL);

	hx_string_destroy(&temporary);
	if (result)
		*result = value;

	return status;
}

hx_os_status hx_os_file_save_atomic(
	const char *path, const void *data, size_t bytes, hx_os_error *error)
{
	return hx_os_file_save_atomic_ex(path, data, bytes, NULL, error);
}

hx_os_status hx_os_file_write_text(
	const char *path, const hx_string *text, hx_os_error *error)
{
	if (!text)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_file_save_atomic(
		path, hx_string_c_str(text), hx_string_size_bytes(text), error);
}

hx_os_status hx_os_file_replace(const char *target, const char *source,
	const char *backup, hx_os_error *error)
{
	hx_os_status status = hx_os_file_paths(target, source, error);
	if (status == HX_OS_OK && backup)
		status = hx_os_i_path_validate(backup, error);

	return status == HX_OS_OK
		? hx_os_i_file_replace(target, source, backup, error)
		: status;
}

hx_os_status hx_os_file_backup(const char *path, const char *backup_path,
	int replace, hx_os_error *error)
{
	return hx_os_file_copy(path, backup_path, replace, error);
}

hx_os_status hx_os_path_current_string(hx_string *out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_path_current(out, error);
}

hx_os_status hx_os_path_current(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
	hx_string value = HX_STRING_INIT;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_path_current(&value, error);
	if (status == HX_OS_OK)
		status = hx_os_i_text_copy(hx_string_c_str(&value), buffer,
			capacity, required, error);

	hx_string_destroy(&value);

	return status;
}

hx_os_status hx_os_path_change_current(const char *path, hx_os_error *error)
{
	hx_os_status status = hx_os_i_path_validate(path, error);

	return status == HX_OS_OK ? hx_os_i_path_change_current(path, error)
				  : status;
}

hx_os_status hx_os_path_absolute_string(
	const char *path, hx_string *out, hx_os_error *error)
{
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_path_validate(path, error);

	return status == HX_OS_OK ? hx_os_i_path_absolute(path, out, error)
				  : status;
}

hx_os_status hx_os_path_absolute(const char *path, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
	hx_string value = HX_STRING_INIT;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_path_absolute_string(path, &value, error);
	if (status == HX_OS_OK)
		status = hx_os_i_text_copy(hx_string_c_str(&value), buffer,
			capacity, required, error);

	hx_string_destroy(&value);

	return status;
}

hx_os_status hx_os_directory_remove_recursive_with_options(
	const char *path, const hx_os_walk_options *input, hx_os_error *error)
{
	hx_os_walk_options options;
	hx_string trimmed = HX_STRING_INIT;

	hx_os_status status = hx_os_i_path_validate(path, error);

	size_t length, root;
	const char *base;

	if (status == HX_OS_OK)
		status = hx_os_i_walk_options(input, &options, error);

	if (status != HX_OS_OK)
		return status;

	length = strlen(path);
#if defined(_WIN32)
	root = hx_os_i_path_root(path);
#else
	root = strspn(path, "/");
#endif
	while (length > root && hx_os_file_native_separator(path[length - 1]))
		--length;

	base = path + length;

	while (base > path && !hx_os_file_native_separator(base[-1]))
		--base;

	if (length == root || (path + length - base == 1 && base[0] == '.') ||
		(path + length - base == 2 && !memcmp(base, "..", 2)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	{
		hx_string_view view = {path, length};

		status = hx_string_assign(&trimmed, view, error);
	}

	if (status == HX_OS_OK)
		status = hx_os_i_remove_tree(
			hx_string_c_str(&trimmed), &options, error);

	hx_string_destroy(&trimmed);

	return status;
}

hx_os_status hx_os_directory_remove_recursive(
	const char *path, hx_os_error *error)
{
	return hx_os_directory_remove_recursive_with_options(path, NULL, error);
}
