#include "archive_private.h"

#if HX_OS_HAVE_ARCHIVE
static int hx_ar_device(const char *name, size_t bytes)
{
	char base[9];
	size_t i = 0;

	while (i < bytes && name[i] != '.') {
		unsigned char value = (unsigned char)name[i];

		if (i == sizeof(base) - 1)
			return 0;
		base[i++] = (char)(value >= 'a' && value <= 'z'
				? value - ('a' - 'A')
				: value);
	}
	base[i] = 0;
	if (!strcmp(base, "CON") || !strcmp(base, "PRN") ||
		!strcmp(base, "AUX") || !strcmp(base, "NUL") ||
		!strcmp(base, "CONIN$") || !strcmp(base, "CONOUT$"))
		return 1;
	if (!strncmp(base, "COM", 3) || !strncmp(base, "LPT", 3)) {
		if (i == 4 && base[3] >= '1' && base[3] <= '9')
			return 1;
		if (i == 5 && (unsigned char)base[3] == 0xc2 &&
			((unsigned char)base[4] == 0xb9 ||
				(unsigned char)base[4] == 0xb2 ||
				(unsigned char)base[4] == 0xb3))
			return 1;
	}

	return 0;
}

hx_os_status hx_ar_i_path(
	hx_ar_job *job, const char *path, hx_string *out, int directory)
{
	size_t bytes, start = 0, i, components = 0, invalid;
	hx_os_error error = {0};
	hx_os_status status;

	if (!path)
		return hx_ar_i_fail(job, HX_OS_INVALID_ENCODING, NULL,
			"Missing UTF-8 archive path");
	bytes = strlen(path);
	if (!bytes)
		return hx_ar_i_fail(
			job, HX_OS_INVALID, NULL, "Empty archive path");
	if (bytes >= job->options.max_path_bytes)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Archive path length");
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	if (directory && (!*path || !strcmp(path, ".")))
		return hx_string_assign_cstr(out, ".", NULL);
	bytes = strlen(path);
	status = hx_os_text_validate_utf8(path, bytes, &invalid);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, HX_OS_INVALID_ENCODING, NULL,
			"Archive path must be UTF-8");
	if (path[0] == '/' || path[0] == '\\')
		return hx_ar_i_fail(
			job, HX_OS_INVALID, NULL, "Absolute archive path");
#if !defined(_WIN32)
	if (job->packing && strchr(path, '\\'))
		return hx_ar_i_fail(job, HX_OS_UNREPRESENTABLE, NULL,
			"A literal backslash cannot form a portable archive "
			"name");
#endif
	status = hx_string_assign_cstr(out, path, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, NULL);
	for (i = 0; i < bytes; ++i)
		if (out->data[i] == '\\')
			out->data[i] = '/';
	if (directory && bytes && out->data[bytes - 1] == '/') {
		--bytes;
		out->size = bytes;
		out->data[bytes] = 0;
	}
	for (i = 0; i <= bytes; ++i) {
		unsigned char value = (unsigned char)out->data[i];

		if (i < bytes && value != '/') {
			if (value < 32 || value == 127 ||
				strchr(":*?\"<>|", value))
				return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
					"Nonportable archive path component");
			continue;
		}
		if (i == start || (i - start == 1 && out->data[start] == '.') ||
			(i - start == 2 &&
				!memcmp(out->data + start, "..", 2)) ||
			out->data[i - 1] == '.' || out->data[i - 1] == ' ' ||
			hx_ar_device(out->data + start, i - start))
			return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Unsafe or reserved archive path component");
		if (++components > job->options.max_depth)
			return hx_ar_i_fail(
				job, HX_OS_LIMIT, NULL, "Archive path depth");
		start = i + 1;
	}

	return HX_OS_OK;
}

hx_os_status hx_ar_i_split(
	hx_ar_job *job, const char *path, hx_string *directory, hx_string *name)
{
	hx_string_view parent, leaf;
	hx_os_error error = {0};
	hx_os_status status;
#if defined(_WIN32)
	status = hx_os_path_dirname(path, &parent, &error);
	if (status == HX_OS_OK)
		status = hx_os_path_basename(path, &leaf, &error);
#else
	const char *last = strrchr(path, '/');

	parent.data = last ? path : ".";
	parent.size = last ? (size_t)(last - path) : 1;
	if (last == path)
		parent.size = 1;
	leaf.data = last ? last + 1 : path;
	leaf.size = strlen(leaf.data);
	status = HX_OS_OK;
#endif
	if (status == HX_OS_OK)
		status = hx_string_assign(directory, parent, &error);
	if (status == HX_OS_OK)
		status = hx_string_assign(name, leaf, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, NULL);

	return status;
}

typedef struct hx_ar_frame {
	hx_os_directory *directory;
	size_t prefix;
} hx_ar_frame;

static hx_os_status hx_ar_visit_source(hx_ar_job *job, const char *path,
	const hx_os_file_info *info, hx_ar_source_fn visit, void *user)
{
	hx_string normalized = HX_STRING_INIT;
	hx_os_archive_entry entry;
	hx_os_status status;

	if (info->kind != HX_OS_FILE_REGULAR &&
		info->kind != HX_OS_FILE_DIRECTORY)
		return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
			"Packing links and special files is not supported");
	if (job->result.entries == job->options.max_entries)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Archive entry count");
	if (info->kind == HX_OS_FILE_REGULAR) {
		if (info->size >
			job->options.max_content_bytes - job->source_bytes)
			return hx_ar_i_fail(
				job, HX_OS_LIMIT, NULL, "Packing source size");
		job->source_bytes += info->size;
	}
	hx_string_init(&normalized, job->context);
	status = hx_ar_i_path(
		job, path, &normalized, info->kind == HX_OS_FILE_DIRECTORY);
	if (status == HX_OS_OK) {
		memset(&entry, 0, sizeof(entry));
		entry.path = hx_string_view_of(&normalized);
		entry.bytes =
			info->kind == HX_OS_FILE_DIRECTORY ? 0 : info->size;
		entry.modified_seconds = info->modified_seconds;
		entry.modified_nanoseconds = info->modified_nanoseconds;
		entry.kind = info->kind == HX_OS_FILE_DIRECTORY
			? HX_OS_ARCHIVE_DIRECTORY
			: HX_OS_ARCHIVE_FILE;
		entry.flags =
			HX_OS_ARCHIVE_SIZE_KNOWN | HX_OS_ARCHIVE_MTIME_KNOWN;
		++job->result.entries;
		if (entry.kind == HX_OS_ARCHIVE_DIRECTORY)
			++job->result.directories;
		else
			++job->result.files;
		status = visit(job, &entry, user);
	}
	hx_string_destroy(&normalized);

	return status;
}

static hx_os_status hx_ar_walk_directory(
	hx_ar_job *job, hx_ar_source_fn visit, void *user)
{
	hx_ar_frame *frames = hx_os_i_zero(
		job->context, (job->options.max_depth + 1) * sizeof(*frames));
	hx_string full = HX_STRING_INIT, relative = HX_STRING_INIT;
	char *name =
		hx_os_i_alloc(job->context, job->options.max_path_bytes, 0);
	size_t depth = 0;
	hx_os_error error = {0};
	hx_os_status status;

	hx_string_init(&full, job->context);
	hx_string_init(&relative, job->context);
	if (!frames || !name) {
		status = HX_OS_NO_MEMORY;
		goto done;
	}
	status = hx_os_directory_open(job->context,
		hx_ar_i_root_path(job->source_root), &frames[0].directory,
		&error);
	if (status == HX_OS_OK)
		depth = 1;
	while (depth && status == HX_OS_OK) {
		hx_os_file_info info;
		size_t required;

		status = hx_ar_i_check(job, 1);
		if (status != HX_OS_OK)
			break;
		status = hx_os_directory_next(frames[depth - 1].directory, name,
			job->options.max_path_bytes, &required, &error);
		if (status == HX_OS_EOF) {
			hx_os_directory_close(frames[--depth].directory);
			frames[depth].directory = NULL;
			status = HX_OS_OK;
			continue;
		}
		if (status != HX_OS_OK)
			break;
		if (relative.size > frames[depth - 1].prefix)
			status = hx_string_erase(&relative,
				frames[depth - 1].prefix,
				relative.size - frames[depth - 1].prefix,
				&error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(&relative, name, &error);
		if (status == HX_OS_OK)
			status = hx_string_assign_cstr(&full,
				hx_ar_i_root_path(job->source_root), &error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(&full, "/", &error);
		if (status == HX_OS_OK)
			status = hx_string_append(
				&full, hx_string_view_of(&relative), &error);
		if (status == HX_OS_OK)
			status = hx_os_path_stat(
				hx_string_c_str(&full), 0, &info, &error);
		if (status == HX_OS_OK)
			status = hx_ar_visit_source(job,
				hx_string_c_str(&relative), &info, visit, user);
		if (status == HX_OS_OK && info.kind == HX_OS_FILE_DIRECTORY) {
			if (depth > job->options.max_depth) {
				status = HX_OS_LIMIT;
				break;
			}
			status = hx_os_directory_open(job->context,
				hx_string_c_str(&full),
				&frames[depth].directory, &error);
			if (status == HX_OS_OK) {
				status = hx_string_append_cstr(
					&relative, "/", &error);
				frames[depth].prefix = relative.size;
				++depth;
			}
		}
	}
done:
	while (depth)
		hx_os_directory_close(frames[--depth].directory);
	hx_os_memory_free(name);
	hx_os_memory_free(frames);
	hx_string_destroy(&relative);
	hx_string_destroy(&full);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status,
			status == error.status ? &error : NULL,
			"Source traversal");

	return status;
}

hx_os_status hx_ar_i_walk(hx_ar_job *job, hx_ar_source_fn visit, void *user)
{
	hx_string source = HX_STRING_INIT, parent = HX_STRING_INIT;
	hx_string name = HX_STRING_INIT;
	hx_os_file_info info;
	hx_os_error error = {0};
	hx_os_status status;
	int inside = 0;

	hx_string_init(&source, job->context);
	hx_string_init(&parent, job->context);
	hx_string_init(&name, job->context);
	status = hx_os_path_stat(job->source, 0, &info, &error);
	if (status == HX_OS_OK && info.kind != HX_OS_FILE_REGULAR &&
		info.kind != HX_OS_FILE_DIRECTORY)
		status = hx_os_i_error(&error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_i_path_absolute(job->source, &source, &error);
	if (status == HX_OS_OK) {
		job->source_directory = info.kind == HX_OS_FILE_DIRECTORY;
		if (job->source_directory)
			status = hx_string_assign(
				&parent, hx_string_view_of(&source), &error);
		else
			status = hx_ar_i_split(
				job, hx_string_c_str(&source), &parent, &name);
	}
	if (status == HX_OS_OK)
		status = hx_ar_i_root_open(
			job, hx_string_c_str(&parent), 0, &job->source_root);
	if (status == HX_OS_OK && job->source_directory) {
		hx_string unused = HX_STRING_INIT;

		hx_string_init(&unused, job->context);
		status =
			hx_ar_i_split(job, job->archive_path, &parent, &unused);
		if (status == HX_OS_OK)
			status = hx_ar_i_contains(job->source_root,
				hx_string_c_str(&parent), &inside);
		if (status == HX_OS_OK && inside)
			status = hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Archive output is inside the source "
				"directory");
		hx_string_destroy(&unused);
	}
	if (status == HX_OS_OK)
		status = job->source_directory
			? hx_ar_walk_directory(job, visit, user)
			: hx_ar_visit_source(job, hx_string_c_str(&name), &info,
				  visit, user);
	hx_string_destroy(&name);
	hx_string_destroy(&parent);
	hx_string_destroy(&source);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status,
			status == error.status ? &error : NULL,
			"Packing source");

	return status;
}

static hx_os_status hx_ar_collect_item(
	hx_ar_job *job, const hx_os_archive_entry *entry, void *user)
{
	hx_ar_item *item;
	hx_os_error error = {0};
	hx_os_status status;
	(void)user;

	item = hx_os_i_zero(job->context, sizeof(*item) + entry->path.size + 1);
	if (!item)
		return hx_ar_i_fail(
			job, HX_OS_NO_MEMORY, NULL, "Archive metadata");
	item->entry = *entry;
	memcpy(item->path, entry->path.data, entry->path.size);
	item->entry.path.data = item->path;
	status = hx_os_vector_push_back(job->items, &item, &error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(item);
		return hx_ar_i_fail(job, status, &error, "Archive metadata");
	}

	return HX_OS_OK;
}

hx_os_status hx_ar_i_collect(hx_ar_job *job)
{
	hx_os_container_options options;
	hx_os_error error = {0};
	hx_os_status status;

	hx_os_container_options_init(&options, sizeof(hx_ar_item *));
	options.max_items = job->options.max_entries;
	options.max_bytes = job->options.max_memory_bytes;
	status = hx_os_vector_create(
		job->context, &options, &job->items, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, "Archive metadata");

	return hx_ar_i_walk(job, hx_ar_collect_item, NULL);
}

void hx_ar_i_items_free(hx_ar_job *job)
{
	size_t i;

	if (!job->items)
		return;
	for (i = 0; i < hx_os_sequence_size(job->items); ++i)
		hx_os_memory_free(
			*(hx_ar_item *const *)hx_os_sequence_at(job->items, i));
	(void)hx_os_sequence_destroy(&job->items, NULL);
}
#endif
