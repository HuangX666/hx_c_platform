#include "file_utils_private.h"

#if HX_OS_ENABLE_FILE_UTILS
static const char *hx_os_glob_next_point(const char *text)
{
	++text;
	while (((unsigned char)*text & 0xc0) == 0x80)
		++text;
	return text;
}

int hx_os_i_glob_match(const char *pattern, size_t bytes, const char *name)
{
	const char *end = pattern + bytes;
	const char *star = NULL, *retry = NULL;
	if (name[0] == '.' && (!bytes || pattern[0] != '.'))
		return 0;
	while (*name)
		if (pattern != end && *pattern == '*') {
			star = ++pattern;
			retry = name;
		}
		else if (pattern != end && *pattern == '?') {
			++pattern;
			name = hx_os_glob_next_point(name);
		}
		else if (pattern != end && *pattern == *name) {
			++pattern;
			++name;
		}
		else if (star) {
			retry = hx_os_glob_next_point(retry);
			name = retry;
			pattern = star;
		}
		else
			return 0;
	while (pattern != end && *pattern == '*')
		++pattern;
	return pattern == end;
}
#endif

int hx_os_i_path_separator(char value)
{
	return value == '/' || value == '\\';
}

size_t hx_os_i_path_root(const char *path)
{
	size_t index;
	if (!path[0])
		return 0;
	if (path[1] == ':')
		return hx_os_i_path_separator(path[2]) ? 3 : 2;
	if (!hx_os_i_path_separator(path[0]))
		return 0;
	if (!hx_os_i_path_separator(path[1]))
		return 1;
	index = 2;
	if (path[2] == '?' && hx_os_i_path_separator(path[3])) {
		index = 4;
		if (!strncmp(path + index, "UNC", 3) &&
			hx_os_i_path_separator(path[index + 3]))
			index += 4;
		else if (path[index] && path[index + 1] == ':')
			return hx_os_i_path_separator(path[index + 2])
				? index + 3
				: index + 2;
	}
	while (path[index] && !hx_os_i_path_separator(path[index]))
		++index;
	if (path[index])
		++index;
	while (path[index] && !hx_os_i_path_separator(path[index]))
		++index;
	return path[index] ? index + 1 : index;
}

static hx_os_status hx_os_path_parts(const char *path, hx_string_view *out,
	size_t *root, size_t *end, size_t *base, hx_os_error *error)
{
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	*root = hx_os_i_path_root(path);
	*end = strlen(path);
	while (*end > *root && hx_os_i_path_separator(path[*end - 1]))
		--*end;
	*base = *end;
	while (*base > *root && !hx_os_i_path_separator(path[*base - 1]))
		--*base;
	return HX_OS_OK;
}

hx_os_status hx_os_path_basename(
	const char *path, hx_string_view *out, hx_os_error *error)
{
	size_t root, end, base;
	hx_os_status status =
		hx_os_path_parts(path, out, &root, &end, &base, error);
	if (status != HX_OS_OK)
		return status;
	if (end == root)
		base = 0;
	out->data = path + base;
	out->size = end - base;
	return HX_OS_OK;
}

hx_os_status hx_os_path_dirname(
	const char *path, hx_string_view *out, hx_os_error *error)
{
	size_t root, end, base;
	hx_os_status status =
		hx_os_path_parts(path, out, &root, &end, &base, error);
	if (status != HX_OS_OK)
		return status;
	while (base > root && hx_os_i_path_separator(path[base - 1]))
		--base;
	out->data = base ? path : ".";
	out->size = base ? base : 1;
	return HX_OS_OK;
}

hx_os_status hx_os_path_extension(
	const char *path, hx_string_view *out, hx_os_error *error)
{
	hx_string_view base;
	size_t position;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_path_basename(path, &base, error);
	if (status != HX_OS_OK)
		return status;
	position = base.size;
	while (position && base.data[position - 1] != '.')
		--position;
	if (position > 1) {
		out->data = base.data + position - 1;
		out->size = base.size - position + 1;
	}
	else {
		out->data = base.data + base.size;
		out->size = 0;
	}
	return HX_OS_OK;
}

hx_os_status hx_os_i_walk_options(const hx_os_walk_options *input,
	hx_os_walk_options *out, hx_os_error *error)
{
	hx_os_walk_options defaults = HX_OS_WALK_OPTIONS_INIT;
	if (!input)
		input = &defaults;
	if (input->size < sizeof(*input) || input->version != 1 ||
		!input->max_depth || input->max_depth > 256 ||
		!input->max_entries || !input->max_results || !input->max_bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = *input;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
