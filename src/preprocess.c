#include "preprocess_private.h"

int hx_os_preprocess_available(void)
{
	return HX_OS_HAVE_PREPROCESSOR;
}

hx_os_status hx_os_preprocess_options_init(
	hx_os_preprocess_options *options, size_t capacity)
{
	const hx_os_preprocess_options defaults = HX_OS_PREPROCESS_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;
	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;
	*options = defaults;

	return HX_OS_OK;
}

void hx_os_preprocess_diagnostic_init(
	hx_os_preprocess_diagnostic *diagnostic, hx_os_context *context)
{
	if (!diagnostic)
		return;
	hx_string_init(&diagnostic->source, context);
	diagnostic->description = NULL;
	diagnostic->line = diagnostic->column = 0;
}

void hx_os_preprocess_diagnostic_destroy(
	hx_os_preprocess_diagnostic *diagnostic)
{
	if (!diagnostic)
		return;
	hx_string_destroy(&diagnostic->source);
	diagnostic->description = NULL;
	diagnostic->line = diagnostic->column = 0;
}

static void hx_os_pp_diagnostic_clear(hx_os_preprocess_diagnostic *diagnostic)
{
	if (!diagnostic)
		return;
	hx_string_clear(&diagnostic->source);
	diagnostic->description = NULL;
	diagnostic->line = diagnostic->column = 0;
}

static hx_os_status hx_os_pp_options(const hx_os_preprocess_options *options,
	hx_os_preprocess_options *copy, hx_os_error *error)
{
	const hx_os_preprocess_options defaults = HX_OS_PREPROCESS_OPTIONS_INIT;
	const size_t byte_limit = 64u * 1024u * 1024u;
	const size_t count_limit = 1024u * 1024u;

	if (!options)
		options = &defaults;
	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_input_bytes ||
		options->max_input_bytes > byte_limit ||
		!options->max_total_input_bytes ||
		options->max_total_input_bytes > byte_limit ||
		!options->max_output_bytes ||
		options->max_output_bytes > byte_limit ||
		!options->max_macro_bytes ||
		options->max_macro_bytes > byte_limit ||
		!options->max_expanded_bytes ||
		options->max_expanded_bytes > byte_limit ||
		!options->max_defines || options->max_defines > 4096 ||
		!options->max_expansions ||
		options->max_expansions > count_limit ||
		!options->max_directives ||
		options->max_directives > count_limit ||
		!options->max_include_depth ||
		options->max_include_depth > 64 ||
		!options->max_expansion_depth ||
		options->max_expansion_depth > 64 ||
		!options->max_condition_depth ||
		options->max_condition_depth > 64 ||
		options->define_count > options->max_defines ||
		(options->define_count && !options->defines) ||
		(options->flags & ~(uint32_t)HX_OS_PREPROCESS_NO_INCLUDES))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*copy = *options;

	return HX_OS_OK;
}

static hx_os_status hx_os_pp_source_name(const char *name, hx_os_error *error)
{
	size_t bytes = 0, invalid;

	if (!name || !name[0])
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	while (bytes <= 4096 && name[bytes])
		++bytes;
	if (bytes > 4096)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	return hx_os_i_error(
		error, hx_os_text_validate_utf8(name, bytes, &invalid), 0, 0);
}

hx_os_status hx_os_preprocess_text_ex(hx_string_view input,
	const char *source_name, const hx_os_preprocess_options *options,
	hx_string *out, hx_os_preprocess_diagnostic *diagnostic,
	hx_os_error *error)
{
	hx_os_preprocess_options limits;
	hx_os_status status;

	if (!out || (diagnostic && out == &diagnostic->source))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_os_pp_diagnostic_clear(diagnostic);
	(void)hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (!hx_os_preprocess_available())
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_pp_options(options, &limits, error);
	if (status != HX_OS_OK)
		return status;
	if (!source_name)
		source_name = "<input>";
	status = hx_os_pp_source_name(source_name, error);
	if (status != HX_OS_OK)
		return status;
	if (!input.data && input.size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (input.size > limits.max_input_bytes ||
		input.size > limits.max_total_input_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

#if HX_OS_HAVE_PREPROCESSOR
	return hx_os_i_pp_run(
		input, source_name, &limits, out, diagnostic, error);
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_preprocess_text(
	const char *input, hx_string *out, hx_os_error *error)
{
	hx_string_view view = {input, 0};

	if (!input)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	while (view.size <= 16u * 1024u * 1024u && input[view.size])
		++view.size;

	return hx_os_preprocess_text_ex(view, NULL, NULL, out, NULL, error);
}

#if HX_OS_HAVE_PREPROCESSOR && HX_OS_ENABLE_FILE_UTILS
static int hx_os_pp_relative_path(const char *name)
{
	const char *part = name, *cursor;

	if (!name[0] || name[0] == '/' || name[0] == '\\')
		return 0;
	for (cursor = name;; ++cursor) {
		if (*cursor == ':')
			return 0;
		if (!*cursor || *cursor == '/' || *cursor == '\\') {
			if (cursor - part == 2 && part[0] == '.' &&
				part[1] == '.')
				return 0;
			part = cursor + 1;
		}
		if (!*cursor)
			break;
	}

	return 1;
}

static hx_os_status HX_OS_CALL hx_os_pp_file_include(const char *parent,
	const char *requested, size_t max_bytes, hx_string *text,
	hx_string *resolved_name, void *user, hx_os_error *error)
{
	hx_string path;
	hx_string_view directory;
	hx_os_status status;
	size_t i;
#if defined(_WIN32)
	const char separator = '\\';
#else
	const char separator = '/';
#endif

	(void)user;
	if (!hx_os_pp_relative_path(requested))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&path, text->context);
	status = hx_os_path_dirname(parent, &directory, error);
	if (status == HX_OS_OK)
		status = hx_string_assign(&path, directory, error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, "/", error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, requested, error);
	if (status == HX_OS_OK) {
		for (i = 0; i < path.size; ++i)
			if (path.data[i] == '\\' || path.data[i] == '/')
				path.data[i] = separator;
		status = hx_os_path_absolute_string(
			path.data, resolved_name, error);
	}
	if (status == HX_OS_OK)
		status = hx_os_pp_source_name(resolved_name->data, error);
	if (status == HX_OS_OK)
		status = hx_os_file_load_utf8(text->context,
			resolved_name->data, max_bytes, text, 0, error);
	hx_string_destroy(&path);

	return status;
}
#endif

hx_os_status hx_os_preprocess_file_ex(const char *path,
	const hx_os_preprocess_options *options, hx_string *out,
	hx_os_preprocess_diagnostic *diagnostic, hx_os_error *error)
{
#if HX_OS_HAVE_PREPROCESSOR && HX_OS_ENABLE_FILE_UTILS
	hx_os_preprocess_options limits;
	hx_string text, absolute;
	hx_os_status status;
	size_t maximum;

	if (!out || (diagnostic && out == &diagnostic->source))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_os_pp_diagnostic_clear(diagnostic);
	status = hx_os_pp_options(options, &limits, error);
	if (status != HX_OS_OK)
		return status;
	if (!limits.include)
		limits.include = hx_os_pp_file_include;
	maximum = limits.max_input_bytes < limits.max_total_input_bytes
		? limits.max_input_bytes
		: limits.max_total_input_bytes;
	hx_string_init(&text, out->context);
	hx_string_init(&absolute, out->context);
	status = hx_os_path_absolute_string(path, &absolute, error);
	if (status == HX_OS_OK)
		status = hx_os_pp_source_name(absolute.data, error);
	if (status == HX_OS_OK)
		status = hx_os_file_load_utf8(
			out->context, absolute.data, maximum, &text, 0, error);
	if (status == HX_OS_OK)
		status = hx_os_preprocess_text_ex(hx_string_view_of(&text),
			absolute.data, &limits, out, diagnostic, error);
	else if (diagnostic) {
		diagnostic->description = "cannot read preprocessing source";
		(void)hx_string_assign_cstr(&diagnostic->source,
			absolute.size ? absolute.data : (path ? path : ""),
			NULL);
	}
	hx_string_destroy(&text);
	hx_string_destroy(&absolute);

	return status;
#else
	(void)path;
	(void)options;
	(void)out;
	hx_os_pp_diagnostic_clear(diagnostic);

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_preprocess_file(
	const char *path, hx_string *out, hx_os_error *error)
{
	return hx_os_preprocess_file_ex(path, NULL, out, NULL, error);
}
