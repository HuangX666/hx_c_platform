#include "internal.h"

hx_os_status hx_os_file_write_text(
	const char *path, const hx_string *text, hx_os_error *error)
{
	(void)path;
	(void)text;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_copy(
	const char *from, const char *to, int replace, hx_os_error *error)
{
	(void)from;
	(void)to;
	(void)replace;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_exists(
	const char *path, int *exists, hx_os_error *error)
{
	(void)path;
	(void)exists;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_load(hx_os_context *context, const char *path,
	size_t max_bytes, void **data, size_t *bytes, hx_os_error *error)
{
	(void)context;
	(void)path;
	(void)max_bytes;
	(void)data;
	(void)bytes;
	if (data)
		*data = NULL;
	if (bytes)
		*bytes = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_load_utf8(hx_os_context *context, const char *path,
	size_t max_bytes, hx_string *out, int strip_bom, hx_os_error *error)
{
	(void)context;
	(void)path;
	(void)max_bytes;
	(void)out;
	(void)strip_bom;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_read_text(
	const char *path, hx_string *out, hx_os_error *error)
{
	(void)path;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_save(const char *path, const void *data, size_t bytes,
	int replace, hx_os_error *error)
{
	(void)path;
	(void)data;
	(void)bytes;
	(void)replace;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_save_atomic(
	const char *path, const void *data, size_t bytes, hx_os_error *error)
{
	(void)path;
	(void)data;
	(void)bytes;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_save_atomic_ex(const char *path, const void *data,
	size_t bytes, hx_os_file_save_result *result, hx_os_error *error)
{
	(void)path;
	(void)data;
	(void)bytes;
	(void)result;
	if (result)
		memset(result, 0, sizeof(*result));
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_replace(const char *target, const char *source,
	const char *backup, hx_os_error *error)
{
	(void)target;
	(void)source;
	(void)backup;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_file_backup(const char *path, const char *backup_path,
	int replace, hx_os_error *error)
{
	(void)path;
	(void)backup_path;
	(void)replace;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_current(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
	(void)buffer;
	(void)capacity;
	(void)required;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_current_string(hx_string *out, hx_os_error *error)
{
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_change_current(const char *path, hx_os_error *error)
{
	(void)path;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_absolute(const char *path, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
	(void)path;
	(void)buffer;
	(void)capacity;
	(void)required;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_absolute_string(
	const char *path, hx_string *out, hx_os_error *error)
{
	(void)path;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_glob(hx_os_context *context, const char *pattern,
	hx_os_vector **matches, hx_os_error *error)
{
	(void)context;
	(void)pattern;
	(void)matches;
	if (matches)
		*matches = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_path_glob_with_options(hx_os_context *context,
	const char *pattern, const hx_os_walk_options *options,
	hx_os_vector **matches, hx_os_error *error)
{
	(void)context;
	(void)pattern;
	(void)options;
	(void)matches;
	if (matches)
		*matches = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_directory_remove_recursive(
	const char *path, hx_os_error *error)
{
	(void)path;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_directory_remove_recursive_with_options(
	const char *path, const hx_os_walk_options *options, hx_os_error *error)
{
	(void)path;
	(void)options;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
