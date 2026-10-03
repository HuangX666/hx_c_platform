#include "file_utils_private.h"

#if HX_OS_ENABLE_FILE_UTILS
typedef struct hx_os_search_frame {
	hx_os_directory *directory;
	size_t path_bytes;
} hx_os_search_frame;

typedef struct hx_os_file_search {
	hx_os_context *context;
	hx_os_search_frame *frames;
	char *path;
	char *name;
	size_t path_capacity;
	size_t name_capacity;
	size_t allocated;
	size_t maximum;
} hx_os_file_search;

static hx_os_status hx_os_search_reserve(hx_os_file_search *search,
	char **buffer, size_t *capacity, size_t needed, hx_os_error *error)
{
	size_t next, cost;
	char *memory;
	if (needed <= *capacity)
		return HX_OS_OK;
	if (needed > 32769)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	next = *capacity ? *capacity * 2 : 256;
	if (next < needed)
		next = needed;
	if (next > 32769)
		next = 32769;
	cost = hx_os_i_allocation_cost(next, 0);
	if (cost > search->maximum - search->allocated) {
		next = needed;
		cost = hx_os_i_allocation_cost(next, 0);
	}
	if (cost > search->maximum - search->allocated)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	memory = hx_os_i_alloc(search->context, next, 0);
	if (!memory)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	search->allocated += cost;
	if (*buffer) {
		memcpy(memory, *buffer, strlen(*buffer) + 1);
		search->allocated -= hx_os_i_allocation_bytes(*buffer);
		hx_os_memory_free(*buffer);
	}
	else
		memory[0] = 0;
	*buffer = memory;
	*capacity = next;
	return HX_OS_OK;
}

static hx_os_status hx_os_search_name(hx_os_file_search *search,
	hx_os_directory *directory, unsigned *kind, size_t *bytes,
	hx_os_error *error)
{
	hx_os_status status;
	for (;;) {
#if HX_OS_ENABLE_FILE_INFO
		status = hx_os_directory_next_info(directory, search->name,
			search->name_capacity, bytes, kind, error);
#else
		(void)kind;
		status = hx_os_directory_next(directory, search->name,
			search->name_capacity, bytes, error);
#endif
		if (status != HX_OS_OUTPUT_FULL)
			return status;
		if (!*bytes)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		status = hx_os_search_reserve(search, &search->name,
			&search->name_capacity, *bytes, error);
		if (status != HX_OS_OK)
			return status;
	}
}

static hx_os_status hx_os_search_join(hx_os_file_search *search, size_t prefix,
	size_t name_bytes, size_t *length, hx_os_error *error)
{
	size_t separator = prefix && search->path[prefix - 1] != '/';
	hx_os_status status;
#if defined(_WIN32)
	if (prefix &&
		(search->path[prefix - 1] == ':' ||
			search->path[prefix - 1] == '\\'))
		separator = 0;
#endif
	if (!hx_os_i_add_size(prefix, separator, length) ||
		!hx_os_i_add_size(*length, name_bytes, length) ||
		*length > 32769)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	status = hx_os_search_reserve(
		search, &search->path, &search->path_capacity, *length, error);
	if (status != HX_OS_OK)
		return status;
	if (separator)
		search->path[prefix++] = '/';
	memcpy(search->path + prefix, search->name, name_bytes);
	--*length;
	return HX_OS_OK;
}

static hx_os_status hx_os_search_walk(hx_os_file_search *search,
	size_t root_bytes, const char *pattern,
	const hx_os_walk_options *options, hx_os_directory_visit_fn visit,
	void *user, size_t *completed, hx_os_error *error)
{
	hx_os_status status;
	size_t depth = 0, scanned = 0, found = 0;
	size_t pattern_bytes = strlen(pattern);
	search->frames[0].path_bytes = root_bytes;
	status = hx_os_directory_open(search->context, search->path,
		&search->frames[0].directory, error);
	if (status == HX_OS_OK)
		depth = 1;
	while (depth && status == HX_OS_OK) {
		hx_os_search_frame *frame = &search->frames[depth - 1];
		size_t bytes = 0, length = 0;
		unsigned kind = HX_OS_FILE_OTHER;
		search->path[frame->path_bytes] = 0;
		status = hx_os_search_name(
			search, frame->directory, &kind, &bytes, error);
		if (status == HX_OS_EOF) {
			hx_os_directory_close(frame->directory);
			--depth;
			status = HX_OS_OK;
			continue;
		}
		if (status != HX_OS_OK)
			break;
		if (scanned == options->max_entries) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}
		++scanned;
#if !HX_OS_ENABLE_FILE_INFO
		{
			hx_os_file_info info;
			status = hx_os_search_join(search, frame->path_bytes,
				bytes, &length, error);
			if (status == HX_OS_OK)
				status = hx_os_path_stat(
					search->path, 0, &info, error);
			if (status != HX_OS_OK)
				break;
			kind = info.kind;
		}
#endif
		if (kind != HX_OS_FILE_DIRECTORY &&
			(kind != HX_OS_FILE_REGULAR ||
				!hx_os_i_glob_match(
					pattern, pattern_bytes, search->name)))
			continue;
#if HX_OS_ENABLE_FILE_INFO
		status = hx_os_search_join(
			search, frame->path_bytes, bytes, &length, error);
		if (status != HX_OS_OK)
			break;
#endif
		if (kind == HX_OS_FILE_DIRECTORY) {
			if (depth == options->max_depth) {
				status =
					hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
				break;
			}
			status = hx_os_directory_open(search->context,
				search->path, &search->frames[depth].directory,
				error);
			if (status == HX_OS_OK)
				search->frames[depth++].path_bytes = length;
		}
		else {
			hx_string_view path = {search->path, length};
			hx_os_error detail;
			if (found == options->max_results) {
				status =
					hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
				break;
			}
			++found;
			if (completed)
				*completed = found;
			hx_os_error_clear(&detail);
			status = visit(path, user, &detail);
			if (error) {
				*error = detail;
				error->status = status;
			}
		}
	}
	while (depth)
		hx_os_directory_close(search->frames[--depth].directory);
	return status == HX_OS_OK || status == HX_OS_EOF
		? hx_os_i_error(error, HX_OS_OK, 0, 0)
		: status;
}
#endif

hx_os_status hx_os_directory_find_files_ex(hx_os_context *context,
	const char *root, const char *pattern, const hx_os_walk_options *input,
	hx_os_directory_visit_fn visit, void *user, size_t *completed,
	hx_os_error *error)
{
	if (completed)
		*completed = 0;
#if HX_OS_ENABLE_FILE_UTILS
	{
		hx_os_file_search search = {0};
		hx_os_walk_options options;
		hx_os_status status;
		size_t bytes;
		if (!visit || !pattern || !pattern[0])
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		status = hx_os_i_path_validate(root, error);
		if (status == HX_OS_OK)
			status = hx_os_i_path_validate(pattern, error);
		if (status == HX_OS_OK)
			status = hx_os_i_walk_options(input, &options, error);
		if (status != HX_OS_OK)
			return status;
		if (strlen(pattern) > 4096 || strlen(root) > 32768)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		if (strpbrk(pattern, "/\\[]"))
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		search.context = context;
		search.maximum = options.max_bytes;
		bytes = options.max_depth * sizeof(*search.frames);
		search.allocated = hx_os_i_allocation_cost(bytes, 0);
		if (search.allocated > search.maximum)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		search.frames = hx_os_i_zero(context, bytes);
		if (!search.frames)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		bytes = strlen(root);
		status = hx_os_search_reserve(&search, &search.path,
			&search.path_capacity, bytes + 1, error);
		if (status == HX_OS_OK) {
			memcpy(search.path, root, bytes + 1);
			status = hx_os_search_walk(&search, bytes, pattern,
				&options, visit, user, completed, error);
		}
		hx_os_memory_free(search.name);
		hx_os_memory_free(search.path);
		hx_os_memory_free(search.frames);
		return status;
	}
#else
	(void)context;
	(void)root;
	(void)pattern;
	(void)input;
	(void)visit;
	(void)user;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_directory_find_files(const char *root, const char *pattern,
	hx_os_directory_visit_fn visit, void *user, hx_os_error *error)
{
	return hx_os_directory_find_files_ex(
		NULL, root, pattern, NULL, visit, user, NULL, error);
}

static hx_os_status HX_OS_CALL hx_os_search_first(
	hx_string_view path, void *user, hx_os_error *error)
{
	hx_os_status status = hx_string_assign(user, path, error);
	return status == HX_OS_OK ? HX_OS_EOF : status;
}
hx_os_status hx_os_directory_find_first_file_ex(const char *root,
	const char *pattern, const hx_os_walk_options *options, hx_string *out,
	hx_os_error *error)
{
	hx_os_status status;
	size_t completed = 0;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_directory_find_files_ex(out->context, root, pattern,
		options, hx_os_search_first, out, &completed, error);
	return status == HX_OS_OK && !completed
		? hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0)
		: status;
}
hx_os_status hx_os_directory_find_first_file(const char *root,
	const char *pattern, hx_string *out, hx_os_error *error)
{
	return hx_os_directory_find_first_file_ex(
		root, pattern, NULL, out, error);
}
