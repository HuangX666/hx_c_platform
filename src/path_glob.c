#include "file_utils_private.h"

#if HX_OS_ENABLE_CONTAINERS
typedef struct hx_os_glob_frame {
	hx_os_directory *directory;
	size_t path_bytes;
	size_t begin;
	size_t end;
	size_t next;
} hx_os_glob_frame;

static hx_os_status hx_os_glob_name(
	hx_os_directory *directory, hx_string *name, hx_os_error *error)
{
	size_t required;

	hx_os_status status = hx_string_reserve(name, 255, error);
	if (status != HX_OS_OK)
		return status;

	for (;;) {
		status = hx_os_directory_next(directory, name->data,
			name->capacity, &required, error);
		if (status != HX_OS_OUTPUT_FULL)
			break;

		if (required > 32768)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		status = hx_string_reserve(name, required - 1, error);
		if (status != HX_OS_OK)
			return status;
	}

	if (status == HX_OS_OK)
		name->size = required - 1;

	return status;
}

static void hx_os_glob_segment(hx_os_glob_frame *frame, const char *pattern,
	size_t begin, size_t path_bytes)
{
	frame->begin = begin;
	frame->end = begin;

	while (pattern[frame->end] && pattern[frame->end] != '/')
		++frame->end;

	frame->next = frame->end;

	while (pattern[frame->next] == '/')
		++frame->next;

	frame->path_bytes = path_bytes;
}

static hx_os_status hx_os_glob_add(hx_os_vector *matches, const hx_string *path,
	size_t *used, size_t maximum, hx_os_error *error)
{
	hx_string copy;
	size_t bytes;
	hx_os_status status;

	hx_string_init(&copy, path->context);

	status = hx_string_copy(&copy, path, error);
	if (status != HX_OS_OK) {
		hx_string_destroy(&copy);

		return status;
	}

	bytes = hx_os_i_allocation_bytes(copy.data);
	if (bytes > maximum - *used)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	else
		status = hx_os_sequence_push_back_take(matches, &copy, error);

	if (status == HX_OS_OK) {
		*used += bytes;
		memset(&copy, 0, sizeof(copy));
	}

	hx_string_destroy(&copy);

	return status;
}

static hx_os_status hx_os_glob_walk(hx_os_vector *matches, hx_string *path,
	const char *pattern, size_t begin, const hx_os_walk_options *options,
	hx_os_error *error)
{
	hx_os_glob_frame *frames;
	hx_string name;
	hx_os_file_info info;
	hx_os_status status;
	size_t depth = 0, entries = 0, text_bytes = 0;

	if (!pattern[begin]) {
		status =
			hx_os_path_stat(hx_string_c_str(path), 1, &info, error);
		if (status == HX_OS_NOT_FOUND)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);

		if (status == HX_OS_OK && begin && pattern[begin - 1] == '/' &&
			info.kind != HX_OS_FILE_DIRECTORY)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		return status == HX_OS_OK
			? hx_os_glob_add(matches, path, &text_bytes,
				  options->max_bytes, error)
			: status;
	}

	status = hx_os_path_stat(hx_string_c_str(path), 1, &info, error);
	if (status == HX_OS_NOT_FOUND ||
		(status == HX_OS_OK && info.kind != HX_OS_FILE_DIRECTORY))
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (status != HX_OS_OK)
		return status;

	if (options->max_depth * sizeof(*frames) > options->max_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	frames = hx_os_i_zero(
		path->context, options->max_depth * sizeof(*frames));
	if (!frames)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	hx_string_init(&name, path->context);
	hx_os_glob_segment(frames, pattern, begin, path->size);

	status = hx_os_directory_open(path->context, hx_string_c_str(path),
		&frames[0].directory, error);
	if (status == HX_OS_OK)
		depth = 1;

	while (depth && status == HX_OS_OK) {
		hx_os_glob_frame *frame = &frames[depth - 1];

		status = hx_os_glob_name(frame->directory, &name, error);
		if (status == HX_OS_EOF) {
			hx_os_directory_close(frame->directory);
			--depth;

			status = hx_os_i_error(error, HX_OS_OK, 0, 0);
			if (depth)
				(void)hx_string_erase(path,
					frames[depth - 1].path_bytes,
					path->size -
						frames[depth - 1].path_bytes,
					NULL);

			continue;
		}

		if (status != HX_OS_OK)
			break;

		if (entries++ == options->max_entries) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}

		if (!hx_os_i_glob_match(pattern + frame->begin,
			    frame->end - frame->begin, hx_string_c_str(&name)))
			continue;
		if (path->size && path->data[path->size - 1] != '/'
#if defined(_WIN32)
			&& path->data[path->size - 1] != ':'
#endif
		)
			status = hx_string_append_cstr(path, "/", error);
		if (status == HX_OS_OK)
			status = hx_string_append(
				path, hx_string_view_of(&name), error);

		if (status == HX_OS_OK && path->size > 32768)
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (status != HX_OS_OK)
			break;

		if (!pattern[frame->next]) {
			int add = 1;

			if (pattern[frame->end] == '/') {
				status = hx_os_path_stat(
					hx_string_c_str(path), 1, &info, error);
				add = status == HX_OS_OK &&
					info.kind == HX_OS_FILE_DIRECTORY;
			}

			if (status == HX_OS_OK && add)
				status = hx_os_glob_add(matches, path,
					&text_bytes, options->max_bytes, error);
		}
		else {
			status = hx_os_path_stat(
				hx_string_c_str(path), 1, &info, error);
			if (status == HX_OS_OK &&
				info.kind == HX_OS_FILE_DIRECTORY) {
				if (depth == options->max_depth) {
					status = hx_os_i_error(
						error, HX_OS_LIMIT, 0, 0);
					break;
				}

				hx_os_glob_segment(&frames[depth], pattern,
					frame->next, path->size);

				status = hx_os_directory_open(path->context,
					hx_string_c_str(path),
					&frames[depth].directory, error);
				if (status == HX_OS_OK) {
					++depth;
					continue;
				}
			}

			if (status == HX_OS_NOT_FOUND)
				status = hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

		(void)hx_string_erase(path, frame->path_bytes,
			path->size - frame->path_bytes, NULL);
	}

	while (depth)
		hx_os_directory_close(frames[--depth].directory);

	hx_string_destroy(&name);
	hx_os_memory_free(frames);

	return status;
}
#endif

hx_os_status hx_os_path_glob_with_options(hx_os_context *context,
	const char *pattern, const hx_os_walk_options *input,
	hx_os_vector **matches, hx_os_error *error)
{
	if (!matches)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*matches = NULL;
#if HX_OS_ENABLE_CONTAINERS
	{
		hx_os_walk_options options;
		hx_os_container_options container;
		hx_os_vector *result = NULL;
		hx_string expression, path;

		hx_os_status status = hx_os_i_path_validate(pattern, error);

		size_t root, begin, prefix_bytes;

		if (status == HX_OS_OK)
			status = hx_os_i_walk_options(input, &options, error);

		if (status != HX_OS_OK)
			return status;

		if (strlen(pattern) > 32768)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (strpbrk(pattern, "[]"))
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

		hx_string_init(&expression, context);
		hx_string_init(&path, context);

		status = hx_string_assign_cstr(&expression, pattern, error);
		if (status != HX_OS_OK)
			goto finish;
#if defined(_WIN32)
		{
			size_t i;

			for (i = 0; i < expression.size; ++i)
				if (expression.data[i] == '\\')
					expression.data[i] = '/';
		}

		root = hx_os_i_path_root(expression.data);
#else
		root = strspn(expression.data, "/");
#endif
		begin = prefix_bytes = root;

		while (expression.data[begin]) {
			size_t end = begin;
			int wildcard = 0;

			while (expression.data[end] &&
				expression.data[end] != '/') {
				if (expression.data[end] == '*' ||
					expression.data[end] == '?')
					wildcard = 1;
				++end;
			}

			if (wildcard)
				break;

			prefix_bytes = end;
			begin = end;

			while (expression.data[begin] == '/')
				++begin;
		}

		{
			hx_string_view prefix = {
				prefix_bytes ? expression.data : ".",
				prefix_bytes ? prefix_bytes : 1};

			status = hx_string_assign(&path, prefix, error);
		}

		hx_os_string_container_options_init(&container, context);
		container.max_items = options.max_results;
		container.max_bytes = options.max_bytes;
		if (status == HX_OS_OK)
			status = hx_os_vector_create(
				context, &container, &result, error);

		if (status == HX_OS_OK)
			status = hx_os_glob_walk(result, &path, expression.data,
				begin, &options, error);

		if (status == HX_OS_NOT_FOUND)
			status = hx_os_i_error(error, HX_OS_OK, 0, 0);

	finish:
		hx_string_destroy(&path);
		hx_string_destroy(&expression);
		if (status == HX_OS_OK)
			*matches = result;
		else
			(void)hx_os_sequence_destroy(&result, NULL);

		return status;
	}
#else
	(void)context;
	(void)pattern;
	(void)input;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_path_glob(hx_os_context *context, const char *pattern,
	hx_os_vector **matches, hx_os_error *error)
{
	return hx_os_path_glob_with_options(
		context, pattern, NULL, matches, error);
}
