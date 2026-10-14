#include "internal.h"
#include "hx_os_temp.h"
#include "hx_os_paths.h"
#include "hx_os_util.h"

struct hx_os_temp {
	hx_string path;
	hx_os_file *file;
	int directory, keep;
};

int hx_os_temp_available(void)
{
	return HX_OS_HAVE_TEMP;
}

static hx_os_status hx_os_temp_create(hx_os_context *context,
	const char *parent, const char *prefix, int directory, hx_os_temp **out,
	hx_os_error *error)
{
	hx_os_temp *temp;
	hx_string root;
	hx_os_status status;
	unsigned attempt;
	size_t bytes, offset;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_HAVE_TEMP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!prefix)
		prefix = "hx";
	bytes = strlen(prefix);
	if (bytes > 64 || strchr(prefix, '/') || strchr(prefix, '\\') ||
		strchr(prefix, ':'))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_text_validate_utf8(prefix, bytes, &offset) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	hx_string_init(&root, context);
	status = parent ? hx_string_assign_cstr(&root, parent, error)
			: hx_os_path_temp_string(&root, error);
	if (status != HX_OS_OK) {
		hx_string_destroy(&root);
		return status;
	}
	temp = hx_os_i_zero(context, sizeof(*temp));
	if (!temp) {
		hx_string_destroy(&root);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	hx_string_init(&temp->path, context);
	temp->directory = directory;
	for (attempt = 0; attempt < 16; ++attempt) {
		unsigned char uuid[16];
		char suffix[37];

		status = hx_os_uuid_generate(uuid, error);
		if (status == HX_OS_OK)
			status = hx_os_uuid_format(uuid, suffix);
		if (status == HX_OS_OK)
			status =
				hx_string_format(&temp->path, error, "%s/%s-%s",
					hx_string_c_str(&root), prefix, suffix);
		if (status != HX_OS_OK)
			break;
		status = directory
			? hx_os_directory_create(
				  hx_string_c_str(&temp->path), 0, error)
			: hx_os_file_open(context, hx_string_c_str(&temp->path),
				  HX_OS_FILE_READ | HX_OS_FILE_WRITE |
					  HX_OS_FILE_CREATE |
					  HX_OS_FILE_EXCLUSIVE,
				  &temp->file, error);
		if (status != HX_OS_EXISTS)
			break;
	}
	hx_string_destroy(&root);
	if (status != HX_OS_OK) {
		hx_string_destroy(&temp->path);
		hx_os_memory_free(temp);
		return status;
	}
	*out = temp;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_temp_create_file(hx_os_context *context, const char *parent,
	const char *prefix, hx_os_temp **out, hx_os_error *error)
{
	return hx_os_temp_create(context, parent, prefix, 0, out, error);
}

hx_os_status hx_os_temp_create_directory(hx_os_context *context,
	const char *parent, const char *prefix, hx_os_temp **out,
	hx_os_error *error)
{
	return hx_os_temp_create(context, parent, prefix, 1, out, error);
}

const char *hx_os_temp_path(const hx_os_temp *temp)
{
	return temp ? hx_string_c_str(&temp->path) : NULL;
}

hx_os_file *hx_os_temp_file(const hx_os_temp *temp)
{
	return temp ? temp->file : NULL;
}

hx_os_status hx_os_temp_keep(hx_os_temp *temp, hx_os_error *error)
{
	if (!temp)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	temp->keep = 1;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_temp_publish(hx_os_temp *temp, const char *destination,
	int replace, hx_os_error *error)
{
	hx_string path;
	hx_os_status status;

	if (!temp || !destination || (replace != 0 && replace != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&path, temp->path.context);
	status = hx_string_assign_cstr(&path, destination, error);
	if (status == HX_OS_OK && temp->file)
		status = hx_os_file_close(&temp->file, error);
	if (status == HX_OS_OK)
		status = hx_os_file_move(hx_string_c_str(&temp->path),
			hx_string_c_str(&path), replace, error);
	if (status == HX_OS_OK) {
		hx_string_move(&temp->path, &path);
		temp->keep = 1;
	}
	hx_string_destroy(&path);
	return status;
}

hx_os_status hx_os_temp_close(hx_os_temp **temp, hx_os_error *error)
{
	hx_os_temp *value;
	hx_os_status status = HX_OS_OK;

	if (!temp)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *temp;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (value->file)
		status = hx_os_file_close(&value->file, error);
	if (status == HX_OS_OK && !value->keep) {
		status = value->directory
			? hx_os_directory_remove_recursive(
				  hx_string_c_str(&value->path), error)
			: hx_os_file_remove(
				  hx_string_c_str(&value->path), error);
		if (status == HX_OS_NOT_FOUND)
			status = HX_OS_OK;
	}
	if (status != HX_OS_OK)
		return status;
	*temp = NULL;
	hx_string_destroy(&value->path);
	hx_os_memory_free(value);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
