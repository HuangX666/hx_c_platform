#include "archive_private.h"
#include "archive_bridge.h"
#include "hx_os_library.h"
#include "hx_os_paths.h"

#if HX_OS_HAVE_7ZIP
typedef struct hx_ar_seven hx_ar_seven;

typedef struct hx_ar_seven_stream {
	hx_ar_seven *owner;
	hx_os_file *file;
	hx_ar_sink *sink;
	hx_os_atomic references;
	uint64_t expected, position;
	int primary, source, size_known;
} hx_ar_seven_stream;

struct hx_ar_seven {
	hx_ar_job *job;
	hx_ar_bridge bridge;
	hx_ar_bridge_run run;
	hx_ar_seven_stream primary, *active;
	hx_string selection, output_name;
	int matched;
};

static void *hx_ar_seven_allocate(void *user, size_t bytes)
{
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	void *memory = hx_os_i_zero(job->context, bytes);

	if (!memory)
		(void)hx_ar_i_fail(
			job, HX_OS_NO_MEMORY, NULL, "7z adapter memory");
	return memory;
}

static hx_os_status hx_ar_seven_check(void *user)
{
	return hx_ar_i_check(((hx_ar_seven *)user)->job, 1);
}

static hx_os_status hx_ar_seven_fail(
	void *user, hx_os_status status, uint64_t native, const char *message)
{
	hx_os_error error = {0};

	(void)hx_os_i_error(&error, status, HX_OS_ERROR_ARCHIVE, native);
	return hx_ar_i_fail(
		((hx_ar_seven *)user)->job, status, &error, message);
}

/**
 * \brief The pinned 7-Zip COM ABI stores UTF-16 units in wchar_t on POSIX too;
 * using the platform's native UTF-32 convention would lose non-BMP names.
 */
static hx_os_status hx_ar_seven_to_wide(
	void *user, const char *text, wchar_t **out)
{
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	size_t bytes = strlen(text), offset = 0, written = 0;

	*out = NULL;
	if (bytes >= job->options.max_path_bytes)
		return hx_ar_i_fail(job, HX_OS_LIMIT, NULL, "7z string length");
	*out = hx_ar_seven_allocate(user, (bytes + 1) * sizeof(wchar_t));
	if (!*out)
		return HX_OS_NO_MEMORY;
	while (offset < bytes) {
		uint32_t point;
		size_t used;
		hx_os_status status = hx_os_i_utf_decode(HX_OS_UTF8,
			(const unsigned char *)text + offset, bytes - offset,
			&point, &used);

		if (status != HX_OS_OK) {
			hx_os_memory_free(*out);
			*out = NULL;
			return hx_ar_i_fail(job, HX_OS_INVALID_ENCODING, NULL,
				"7z UTF-8 conversion");
		}
		offset += used;
		if (point >= 0x10000) {
			point -= 0x10000;
			(*out)[written++] = (wchar_t)(0xd800 + (point >> 10));
			point = 0xdc00 + (point & 0x3ff);
		}
		(*out)[written++] = (wchar_t)point;
	}
	(*out)[written] = 0;
	return HX_OS_OK;
}

static hx_os_status hx_ar_seven_from_wide(
	void *user, const wchar_t *wide, size_t units, char **out)
{
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	hx_string text = HX_STRING_INIT;
	hx_os_status status;
	hx_os_error error = {0};
	unsigned char *utf16;
	size_t i;

	*out = NULL;
	if (units > job->options.max_path_bytes)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "7z filename length");
	utf16 = hx_ar_seven_allocate(user, units * 2);
	if (!utf16)
		return HX_OS_NO_MEMORY;
	for (i = 0; i < units; ++i) {
		if ((uint32_t)wide[i] > 0xffff) {
			hx_os_memory_free(utf16);
			return hx_ar_i_fail(job, HX_OS_INVALID_ENCODING, NULL,
				"7-Zip returned an invalid UTF-16 unit");
		}
		utf16[i * 2] = (unsigned char)wide[i];
		utf16[i * 2 + 1] = (unsigned char)((uint32_t)wide[i] >> 8);
	}
	hx_string_init(&text, job->context);
	status = hx_string_assign_encoded(
		&text, HX_OS_UTF16LE, utf16, units * 2, &error);
	hx_os_memory_free(utf16);
	if (status == HX_OS_OK && text.size >= job->options.max_path_bytes)
		status = HX_OS_LIMIT;
	if (status == HX_OS_OK) {
		*out = hx_ar_seven_allocate(user, text.size + 1);
		if (!*out)
			status = HX_OS_NO_MEMORY;
		else
			memcpy(*out, hx_string_c_str(&text), text.size + 1);
	}
	hx_string_destroy(&text);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status,
			error.status == status ? &error : NULL,
			"7z filename conversion");
	return status;
}

static void hx_ar_seven_release(void *opaque)
{
	hx_ar_seven_stream *stream = opaque;
	hx_ar_job *job;
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	uint64_t size;

	if (!stream || stream->primary ||
		hx_os_atomic_fetch_add(
			&stream->references, -1, HX_OS_ACQ_REL) != 1)
		return;
	job = stream->owner->job;
	if (stream->source && !hx_os_atomic_load(&job->failed, HX_OS_ACQUIRE)) {
		status = hx_os_file_size(stream->file, &size, &error);
		if (status == HX_OS_OK &&
			(size != stream->expected ||
				stream->position != stream->expected))
			status = HX_OS_TRUNCATED_INPUT;
	}
	if (stream->sink) {
		hx_ar_i_sink_free(&stream->sink);
		stream->file = NULL;
	}
	if (status == HX_OS_OK)
		status = hx_os_file_close(&stream->file, &error);
	else
		(void)hx_os_file_close(&stream->file, NULL);
	if (status != HX_OS_OK)
		(void)hx_ar_i_fail(job, status,
			error.status == status ? &error : NULL,
			"7z source changed or stream close failed");
	hx_os_memory_free(stream);
}

static hx_os_status hx_ar_seven_read(
	void *user, void *opaque, void *data, uint32_t bytes, uint32_t *done)
{
	hx_ar_seven_stream *stream = opaque;
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	hx_os_error error = {0};
	hx_os_status status = hx_ar_i_check(job, 1);
	size_t count = 0, request = bytes < HX_AR_CHUNK ? bytes : HX_AR_CHUNK;

	*done = 0;
	if (status != HX_OS_OK)
		return status;
	if (stream->position >= stream->expected)
		return HX_OS_EOF;
	if (request > stream->expected - stream->position)
		request = (size_t)(stream->expected - stream->position);
	status = hx_os_file_read_some(
		stream->file, data, request, &count, &error);
	stream->position += count;
	*done = (uint32_t)count;
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return hx_ar_i_fail(job, status, &error, "Read 7z input");
	if (stream->primary)
		job->input_position = stream->position;
	return hx_ar_i_progress(job, stream->primary ? 0 : count,
		stream->primary ? stream->position : 0);
}

static hx_os_status hx_ar_seven_write(void *user, void *opaque,
	const void *data, uint32_t bytes, uint32_t *done)
{
	hx_ar_seven_stream *stream = opaque;
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	hx_os_error error = {0};
	hx_os_status status = hx_ar_i_check(job, 1);
	size_t count = 0, request = bytes < HX_AR_CHUNK ? bytes : HX_AR_CHUNK;
	uint64_t limit = stream->primary ? job->options.max_archive_bytes
					 : stream->expected;

	*done = 0;
	if (status != HX_OS_OK)
		return status;
	if (stream->position > limit || request > limit - stream->position)
		return hx_ar_i_fail(job, HX_OS_LIMIT, NULL, "7z stream size");
	if (!stream->primary) {
		status = hx_ar_i_progress(job, request, 0);
		if (status != HX_OS_OK)
			return status;
	}
	status = hx_os_file_write_all(
		stream->file, data, request, &count, &error);
	stream->position += count;
	*done = (uint32_t)count;
	if (stream->primary)
		(void)hx_ar_i_progress(job, 0, stream->position);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, "Write 7z output");
	return status;
}

static hx_os_status hx_ar_seven_seek(void *user, void *opaque, int64_t offset,
	uint32_t origin, uint64_t *position)
{
	hx_ar_seven_stream *stream = opaque;
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	hx_os_error error = {0};
	hx_os_status status;

	if (!stream->primary || origin > 2)
		return HX_OS_NOT_SUPPORTED;
	status = hx_os_file_seek(
		stream->file, offset, (int)origin, position, &error);
	if (status == HX_OS_OK && *position > job->options.max_archive_bytes)
		status = hx_os_i_error(&error, HX_OS_LIMIT, 0, 0);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, "Seek 7z stream");
	stream->position = *position;
	return status;
}

static hx_os_status hx_ar_seven_resize(void *user, void *opaque, uint64_t bytes)
{
	hx_ar_seven_stream *stream = opaque;
	hx_ar_job *job = ((hx_ar_seven *)user)->job;
	hx_os_error error = {0};
	hx_os_status status;

	if (!stream->primary || !job->packing)
		return HX_OS_NOT_SUPPORTED;
	if (bytes > job->options.max_archive_bytes)
		return hx_ar_i_fail(job, HX_OS_LIMIT, NULL, "7z archive size");
	status = hx_os_file_truncate(stream->file, bytes, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, "Resize 7z output");
	return status;
}

static hx_os_status hx_ar_seven_source(
	void *user, uint32_t index, hx_os_archive_entry *entry, void **out)
{
	hx_ar_seven *seven = user;
	hx_ar_job *job = seven->job;
	hx_ar_item *const *item = hx_os_sequence_at(job->items, index);
	hx_ar_seven_stream *stream;
	hx_os_status status;

	if (!item)
		return hx_ar_i_fail(
			job, HX_OS_INVALID, NULL, "7z source index");
	*entry = (*item)->entry;
	if (!out)
		return HX_OS_OK;
	*out = NULL;
	if (entry->kind == HX_OS_ARCHIVE_DIRECTORY)
		return HX_OS_OK;
	stream = hx_ar_seven_allocate(user, sizeof(*stream));
	if (!stream)
		return HX_OS_NO_MEMORY;
	stream->owner = seven;
	stream->expected = entry->bytes;
	stream->source = 1;
	hx_os_atomic_store(&stream->references, 1, HX_OS_RELAXED);
	status = hx_ar_i_input(
		job->source_root, entry->path.data, &stream->file);
	if (status != HX_OS_OK) {
		hx_ar_seven_release(stream);
		return status;
	}
	*out = stream;
	return HX_OS_OK;
}

static hx_os_status hx_ar_seven_inspect(void *user, uint32_t index,
	const hx_os_archive_entry *input, int *selected)
{
	hx_ar_seven *seven = user;
	hx_ar_job *job = seven->job;
	hx_string path = HX_STRING_INIT;
	hx_os_archive_entry entry = *input;
	hx_os_error error = {0};
	hx_os_status status;
	(void)index;

	*selected = 0;
	hx_string_init(&path, job->context);
	status = hx_ar_i_path(job, entry.path.data, &path,
		entry.kind == HX_OS_ARCHIVE_DIRECTORY);
	if (status == HX_OS_OK) {
		entry.path = hx_string_view_of(&path);
		status = hx_ar_i_entry(job, &entry);
	}
	if (status == HX_OS_OK && job->visit) {
		status = job->visit(&entry, job->visit_user, &error);
		if (status != HX_OS_OK)
			(void)hx_ar_i_fail(
				job, status, &error, "7z visitor stopped");
	}
	if (status == HX_OS_OK && job->selection &&
		!strcmp(hx_string_c_str(&path),
			hx_string_c_str(&seven->selection))) {
		if (seven->matched)
			status = hx_ar_i_fail(
				job, HX_OS_EXISTS, NULL, "Ambiguous 7z entry");
		else if (entry.kind != HX_OS_ARCHIVE_FILE)
			status = hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
				"Selected 7z entry is not a regular file");
		else {
			seven->matched = 1;
			*selected = 1;
		}
	}
	hx_string_destroy(&path);
	return status;
}

static hx_os_status hx_ar_seven_extract(
	void *user, const hx_os_archive_entry *entry, void **out)
{
	hx_ar_seven *seven = user;
	hx_ar_job *job = seven->job;
	hx_string path = HX_STRING_INIT;
	hx_ar_seven_stream *stream;
	hx_os_status status;

	*out = NULL;
	if (seven->active)
		return hx_ar_i_fail(
			job, HX_OS_BUSY, NULL, "Unfinished 7z entry");
	hx_string_init(&path, job->context);
	status = hx_ar_i_path(job, entry->path.data, &path,
		entry->kind == HX_OS_ARCHIVE_DIRECTORY);
	if (status == HX_OS_OK && entry->kind == HX_OS_ARCHIVE_DIRECTORY)
		status = hx_ar_i_directory(
			job->output_root, hx_string_c_str(&path));
	else if (status == HX_OS_OK && entry->kind != HX_OS_ARCHIVE_FILE)
		status = hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
			"7z links and special files cannot be extracted");
	else if (status == HX_OS_OK) {
		stream = hx_ar_seven_allocate(user, sizeof(*stream));
		if (!stream)
			status = HX_OS_NO_MEMORY;
		else {
			stream->owner = seven;
			stream->size_known =
				!!(entry->flags & HX_OS_ARCHIVE_SIZE_KNOWN);
			stream->expected = stream->size_known
				? entry->bytes
				: job->options.max_content_bytes;
			hx_os_atomic_store(
				&stream->references, 1, HX_OS_RELAXED);
			status = hx_ar_i_sink(job->output_root,
				job->selection
					? hx_string_c_str(&seven->output_name)
					: hx_string_c_str(&path),
				(entry->flags & HX_OS_ARCHIVE_ENCRYPTED) != 0,
				&stream->sink, &stream->file);
			if (status == HX_OS_OK) {
				seven->active = stream;
				(void)hx_os_atomic_fetch_add(
					&stream->references, 1, HX_OS_RELAXED);
				*out = stream;
			}
			else
				hx_ar_seven_release(stream);
		}
	}
	hx_string_destroy(&path);
	return status;
}

static hx_os_status hx_ar_seven_finish(void *user)
{
	hx_ar_seven *seven = user;
	hx_ar_seven_stream *stream = seven->active;
	hx_os_status status;

	if (!stream)
		return HX_OS_OK;
	status = !stream->size_known || stream->position == stream->expected
		? hx_ar_i_publish(stream->sink)
		: hx_ar_i_fail(seven->job, HX_OS_TRUNCATED_INPUT, NULL,
			  "7z extracted size differs");
	seven->active = NULL;
	hx_ar_seven_release(stream);
	return status;
}

static void hx_ar_seven_setup(hx_ar_seven *seven, hx_ar_job *job)
{
	hx_ar_bridge *host = &seven->bridge;

	memset(seven, 0, sizeof(*seven));
	seven->job = job;
	seven->primary.owner = seven;
	seven->primary.primary = 1;
	hx_string_init(&seven->selection, job->context);
	hx_string_init(&seven->output_name, job->context);
	host->size = sizeof(*host);
	host->version = 1;
	host->user = seven;
	host->stream = &seven->primary;
	host->password = job->options.password;
	host->workers = job->options.workers;
	host->preset = job->options.preset;
	host->encrypt_names =
		!!(job->options.flags & HX_OS_ARCHIVE_ENCRYPT_NAMES);
	host->packing = (uint32_t)job->packing;
	host->listing = job->visit ? 1 : job->selection ? 2 : 0;
	host->max_entries = job->options.max_entries;
	host->max_path_bytes = job->options.max_path_bytes;
	host->allocate = hx_ar_seven_allocate;
	host->free = hx_os_memory_free;
	host->check = hx_ar_seven_check;
	host->fail = hx_ar_seven_fail;
	host->to_wide = hx_ar_seven_to_wide;
	host->from_wide = hx_ar_seven_from_wide;
	host->read = hx_ar_seven_read;
	host->write = hx_ar_seven_write;
	host->seek = hx_ar_seven_seek;
	host->resize = hx_ar_seven_resize;
	host->release = hx_ar_seven_release;
	host->source = hx_ar_seven_source;
	host->inspect = hx_ar_seven_inspect;
	host->extract = hx_ar_seven_extract;
	host->finish = hx_ar_seven_finish;
}

static int hx_ar_seven_absolute(const char *path)
{
	if (!path || !*path)
		return 0;
#if defined(_WIN32)
	return (path[0] == '\\' && path[1] == '\\') ||
		(path[0] && path[1] == ':' &&
			(path[2] == '\\' || path[2] == '/'));
#else
	return path[0] == '/';
#endif
}

static hx_os_status hx_ar_seven_pack(
	hx_os_file *file, void *user, hx_os_error *error)
{
	hx_ar_seven *seven = user;
	(void)error;

	seven->primary.file = file;
	return seven->run(&seven->bridge);
}
#endif

hx_os_status hx_ar_i_7zip(hx_ar_job *job)
{
#if HX_OS_HAVE_7ZIP
	hx_ar_seven seven;
	hx_os_library *adapter = NULL, *vendor = NULL;
	hx_string path = HX_STRING_INIT, diagnostic = HX_STRING_INIT;
	hx_os_error error = {0};
	hx_os_status status;
	hx_os_file_save_result saved;

	if (!hx_ar_seven_absolute(job->options.seven_zip_library) ||
		(job->options.seven_zip_adapter &&
			!hx_ar_seven_absolute(job->options.seven_zip_adapter)))
		return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
			"Set seven_zip_library to an absolute trusted 7-Zip "
			"DLL/so path");
	if ((job->options.flags & HX_OS_ARCHIVE_ENCRYPT_NAMES) &&
		!job->options.password)
		return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
			"Filename encryption needs a password");
	hx_ar_seven_setup(&seven, job);
	hx_string_init(&path, job->context);
	hx_string_init(&diagnostic, job->context);
	if (job->options.seven_zip_adapter)
		status = hx_string_assign_cstr(
			&path, job->options.seven_zip_adapter, &error);
	else {
		status = hx_os_path_module_string(&path, &error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(&path,
#if defined(_WIN32)
				"/hx_archive_7zip.dll",
#elif defined(__APPLE__)
				"/libhx_archive_7zip.dylib",
#else
				"/libhx_archive_7zip.so",
#endif
				&error);
	}
	if (status == HX_OS_OK)
		status = hx_os_library_open_ex(job->context,
			hx_string_c_str(&path), HX_OS_LIBRARY_LOCAL, &adapter,
			&diagnostic, &error);
	if (status == HX_OS_OK)
		status = hx_os_library_bind_ex(adapter, "hx_archive_7zip_run",
			&seven.run, sizeof(seven.run), &diagnostic, &error);
	if (status == HX_OS_OK)
		status = hx_os_library_open_ex(job->context,
			job->options.seven_zip_library, HX_OS_LIBRARY_LOCAL,
			&vendor, &diagnostic, &error);
	if (status == HX_OS_OK)
		status = hx_os_library_symbol_ex(vendor, "CreateObject",
			&seven.bridge.create_object, &diagnostic, &error);
	if (status == HX_OS_OK) {
		status = hx_os_library_symbol_ex(vendor, "GetModuleProp",
			&seven.bridge.module_property, &diagnostic, &error);
		if (status == HX_OS_NOT_FOUND)
			status = HX_OS_NOT_SUPPORTED;
	}
	if (status != HX_OS_OK)
		(void)hx_ar_i_fail(
			job, status, &error, hx_string_c_str(&diagnostic));
	if (status == HX_OS_OK && job->packing) {
		status = hx_ar_i_collect(job);
		seven.bridge.count = (uint32_t)hx_os_sequence_size(job->items);
		if (status == HX_OS_OK) {
			status = hx_os_i_file_save_callback(job->context,
				job->archive_path,
				(job->options.flags &
					HX_OS_ARCHIVE_OVERWRITE) != 0,
				0, hx_ar_seven_pack, &seven, &saved, &error);
			job->result.published = (uint32_t)saved.published;
		}
	}
	else if (status == HX_OS_OK) {
		seven.primary.file = job->input;
		seven.primary.expected = job->input_size;
		status = hx_ar_i_destination(
			job, &seven.selection, &seven.output_name);
		if (status == HX_OS_OK)
			status = seven.run(&seven.bridge);
		if (status == HX_OS_OK && job->selection && !seven.matched)
			status = hx_ar_i_fail(job, HX_OS_NOT_FOUND, NULL,
				"7z entry not found");
	}
	if (status != HX_OS_OK)
		(void)hx_ar_i_fail(job, status,
			error.status == status ? &error : NULL,
			"7z operation failed");
	hx_ar_seven_release(seven.active);
	if (status == HX_OS_OK)
		status = hx_os_i_library_close_ordered(
			&vendor, &diagnostic, &error);
	else
		(void)hx_os_i_library_close_ordered(&vendor, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_i_library_close_ordered(
			&adapter, &diagnostic, &error);
	else
		(void)hx_os_i_library_close_ordered(&adapter, NULL, NULL);
	if (status != HX_OS_OK)
		(void)hx_ar_i_fail(job, status,
			error.status == status ? &error : NULL,
			hx_string_c_str(&diagnostic));
	hx_string_destroy(&seven.output_name);
	hx_string_destroy(&seven.selection);
	hx_string_destroy(&diagnostic);
	hx_string_destroy(&path);
	return status;
#elif HX_OS_HAVE_ARCHIVE
	return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
		"The optional 7-Zip adapter is not enabled");
#else
	(void)job;
	return HX_OS_NOT_SUPPORTED;
#endif
}
