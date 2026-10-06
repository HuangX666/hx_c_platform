#include "config_private.h"
#include "hx_config_file.h"

#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_FILE_UTILS
static hx_os_status hx_os_config_file_error(hx_os_config_error *error,
	const char *path, hx_os_status status, const hx_os_error *native)
{
	hx_os_cfg_parser parser;

	memset(&parser, 0, sizeof(parser));
	parser.error = error;
	hx_os_cfg_error_init(error, path);
	hx_os_cfg_fail(&parser, status, SIZE_MAX,
		"Configuration file operation failed");
	if (error && native) {
		error->detail = *native;
		error->detail.status = status;
	}

	return status;
}

static hx_os_status hx_os_config_backup_name(
	const char *path, hx_string *name, hx_os_config_error *error)
{
	hx_os_error native;
	hx_os_status status = hx_os_i_path_validate(path, &native);

	if (status == HX_OS_OK)
		status = hx_string_assign_cstr(name, path, &native);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(name, ".bak", &native);

	return status == HX_OS_OK
		? status
		: hx_os_config_file_error(error, path, status, &native);
}

static hx_os_status hx_os_config_read_valid(hx_os_context *context, int format,
	const char *path, hx_os_config_options *options, void **data,
	size_t *bytes, hx_os_config_document **out, hx_os_config_error *error)
{
	hx_os_error native;
	hx_os_status status = hx_os_file_load(
		context, path, options->max_input_bytes, data, bytes, &native);

	if (status != HX_OS_OK)
		return hx_os_config_file_error(error, path, status, &native);
	options->source_name = path;

	return hx_os_config_parse(
		context, format, *data, *bytes, options, out, error);
}
#endif

hx_os_status hx_os_config_save_backup(const hx_os_config_document *document,
	int format, const char *path, const hx_os_config_options *input,
	hx_os_config_backup_result *result, hx_os_config_error *error)
{
	hx_os_config_backup_result progress;
	hx_os_cfg_parser parser;

	memset(&progress, 0, sizeof(progress));
	if (result)
		*result = progress;
	memset(&parser, 0, sizeof(parser));
	parser.error = error;
	hx_os_cfg_error_init(error, path);
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE &&                    \
	HX_OS_ENABLE_FILE_UTILS
	{
		hx_os_config_options options;
		hx_os_config_document *previous = NULL;
		hx_string text, backup;
		void *data = NULL;
		size_t bytes = 0;
		hx_os_error native = {0, 0, 0, 0};
		hx_os_status status;

		if (!document)
			return hx_os_cfg_fail(&parser, HX_OS_INVALID, SIZE_MAX,
				"Missing configuration document");
		if (!hx_os_cfg_options(&parser, input, &options))
			return parser.status;
		hx_string_init(&text, document->context);
		hx_string_init(&backup, document->context);
		status = hx_os_config_backup_name(path, &backup, error);
		if (status == HX_OS_OK) {
			status = hx_os_config_serialize(
				document, format, &text, &options, &native);
			if (status != HX_OS_OK)
				hx_os_config_file_error(
					error, path, status, &native);
		}
		if (status == HX_OS_OK) {
			status = hx_os_config_read_valid(document->context,
				format, path, &options, &data, &bytes,
				&previous, error);
			if (status == HX_OS_NOT_FOUND)
				status = HX_OS_OK;
		}
		if (status == HX_OS_OK && previous) {
			status = hx_os_file_save_atomic_ex(
				hx_string_c_str(&backup), data, bytes,
				&progress.backup, &native);
			if (status != HX_OS_OK)
				hx_os_config_file_error(error,
					hx_string_c_str(&backup), status,
					&native);
		}
		if (status == HX_OS_OK) {
			status = hx_os_file_save_atomic_ex(path,
				hx_string_c_str(&text), text.size,
				&progress.primary, &native);
			if (status != HX_OS_OK)
				hx_os_config_file_error(
					error, path, status, &native);
		}
		if (result)
			*result = progress;
		hx_os_config_release(previous);
		hx_os_memory_free(data);
		hx_string_destroy(&text);
		hx_string_destroy(&backup);
		if (status == HX_OS_OK)
			hx_os_cfg_error_init(error, path);

		return status;
	}
#else
	(void)document;
	(void)format;
	(void)input;
	return hx_os_cfg_fail(&parser, HX_OS_NOT_SUPPORTED, SIZE_MAX,
		"Configuration backup support is disabled");
#endif
}

hx_os_status hx_os_config_restore_backup(hx_os_context *context, int format,
	const char *path, const hx_os_config_options *input,
	hx_os_config_document **out, hx_os_file_save_result *result,
	hx_os_config_error *error)
{
	hx_os_cfg_parser parser;

	if (out)
		*out = NULL;
	if (result)
		memset(result, 0, sizeof(*result));
	memset(&parser, 0, sizeof(parser));
	parser.error = error;
	hx_os_cfg_error_init(error, path);
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_FILE_UTILS
	{
		hx_os_config_options options;
		hx_os_config_document *document = NULL;
		hx_string backup;
		void *data = NULL;
		size_t bytes = 0;
		hx_os_error native;
		hx_os_status status;

		if (!out)
			return hx_os_cfg_fail(&parser, HX_OS_INVALID, SIZE_MAX,
				"Missing document output");
		if (!hx_os_cfg_options(&parser, input, &options))
			return parser.status;
		hx_string_init(&backup, context);
		status = hx_os_config_backup_name(path, &backup, error);
		if (status == HX_OS_OK)
			status = hx_os_config_read_valid(context, format,
				hx_string_c_str(&backup), &options, &data,
				&bytes, &document, error);
		if (status == HX_OS_OK) {
			status = hx_os_file_save_atomic_ex(
				path, data, bytes, result, &native);
			if (status != HX_OS_OK)
				hx_os_config_file_error(
					error, path, status, &native);
		}
		if (status == HX_OS_OK) {
			*out = document;
			document = NULL;
			hx_os_cfg_error_init(error, path);
		}
		hx_os_config_release(document);
		hx_os_memory_free(data);
		hx_string_destroy(&backup);

		return status;
	}
#else
	(void)context;
	(void)format;
	(void)input;
	return hx_os_cfg_fail(&parser, HX_OS_NOT_SUPPORTED, SIZE_MAX,
		"Configuration recovery support is disabled");
#endif
}
