#include "archive_private.h"

#if HX_OS_HAVE_ARCHIVE
static int hx_ar_reader_options(hx_ar_job *job, struct archive *native)
{
	int result = archive_read_support_filter_none(native);
	if (result == ARCHIVE_OK && job->format >= HX_OS_ARCHIVE_GZIP)
		result = archive_read_set_format(native, ARCHIVE_FORMAT_RAW);
	else if (result == ARCHIVE_OK) {
		result = archive_read_support_format_zip(native);
		if (result == ARCHIVE_OK)
			result = archive_read_support_format_tar(native);
		if (result == ARCHIVE_OK)
			result = archive_read_support_format_7zip(native);
	}
	if (result == ARCHIVE_OK && job->options.password)
		result = archive_read_add_passphrase(
			native, job->options.password);

	return result;
}

hx_os_status hx_ar_i_destination(
	hx_ar_job *job, hx_string *selection, hx_string *name)
{
	hx_string directory = HX_STRING_INIT;
	hx_os_status status = HX_OS_OK;

	if (!job->destination)
		return HX_OS_OK;
	hx_string_init(&directory, job->context);
	if (job->selection) {
		status = hx_ar_i_path(job, job->selection, selection, 0);
		if (status == HX_OS_OK)
			status = hx_ar_i_split(
				job, job->destination, &directory, name);
		if (status == HX_OS_OK)
			status = hx_ar_i_path(
				job, hx_string_c_str(name), name, 0);
		if (status == HX_OS_OK && strchr(hx_string_c_str(name), '/'))
			status = hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Extraction destination must name one file");
	}
	if (status == HX_OS_OK)
		status = hx_ar_i_root_open(job,
			job->selection ? hx_string_c_str(&directory)
				       : job->destination,
			1, &job->output_root);
	hx_string_destroy(&directory);

	return status;
}

hx_os_status hx_ar_i_entry(hx_ar_job *job, const hx_os_archive_entry *entry)
{
	if (job->result.entries == job->options.max_entries)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Archive entry count");
	if ((entry->flags & HX_OS_ARCHIVE_SIZE_KNOWN) &&
		entry->bytes > job->options.max_content_bytes)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Archive entry size");
	(void)hx_os_mutex_lock(job->mutex, NULL);
	++job->result.entries;
	job->result.files += entry->kind == HX_OS_ARCHIVE_FILE;
	job->result.directories += entry->kind == HX_OS_ARCHIVE_DIRECTORY;
	(void)hx_os_mutex_unlock(job->mutex, NULL);

	return hx_ar_i_check(job, 1);
}

static hx_os_status hx_ar_raw_name(hx_ar_job *job, hx_string *name)
{
	hx_string directory = HX_STRING_INIT;
	hx_os_error error = {0};
	hx_os_status status;
	const char *dot;

	hx_string_init(&directory, job->context);
	status = hx_ar_i_split(job, job->archive_path, &directory, name);
	if (status == HX_OS_OK) {
		dot = strrchr(hx_string_c_str(name), '.');
		if (dot && dot != hx_string_c_str(name))
			status = hx_string_erase(name,
				(size_t)(dot - name->data), strlen(dot),
				&error);
		else
			status = hx_string_assign_cstr(name, "data", &error);
	}
	hx_string_destroy(&directory);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, "Raw stream filename");

	return status;
}

static hx_os_status hx_ar_read_entry(hx_ar_job *job, struct archive *native,
	struct archive_entry *value, hx_string *path,
	hx_os_archive_entry *entry)
{
	hx_os_status status;
	const char *name = archive_entry_pathname_utf8(value);
	int kind = archive_entry_filetype(value);
	la_int64_t bytes = archive_entry_size(value);
	long nanos = archive_entry_mtime_nsec(value);
	uint32_t expected = job->options.format;
	int actual = archive_format(native) & ARCHIVE_FORMAT_BASE_MASK;

	if (expected && expected < HX_OS_ARCHIVE_GZIP) {
		int wanted = expected == HX_OS_ARCHIVE_ZIP ? ARCHIVE_FORMAT_ZIP
			: expected == HX_OS_ARCHIVE_7ZIP   ? ARCHIVE_FORMAT_7ZIP
							   : ARCHIVE_FORMAT_TAR;
		uint32_t compression = expected == HX_OS_ARCHIVE_TAR_GZIP
			? HX_OS_ARCHIVE_GZIP
			: expected == HX_OS_ARCHIVE_TAR_XZ ? HX_OS_ARCHIVE_XZ
			: expected == HX_OS_ARCHIVE_TAR_ZSTD
			? HX_OS_ARCHIVE_ZSTD
			: 0;

		if (actual != wanted || job->compression != compression)
			return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Archive content does not match the explicit "
				"format");
	}

	memset(entry, 0, sizeof(*entry));
	if (!name && archive_entry_pathname_w(value)) {
		const wchar_t *wide = archive_entry_pathname_w(value);
		hx_os_error error = {0};

		status =
			hx_string_assign_wide(path, wide, wcslen(wide), &error);
		if (status != HX_OS_OK)
			return hx_ar_i_fail(
				job, status, &error, "Archive wide path");
		name = hx_string_c_str(path);
	}
	entry->kind = kind == AE_IFREG ? HX_OS_ARCHIVE_FILE
		: kind == AE_IFDIR     ? HX_OS_ARCHIVE_DIRECTORY
				       : HX_OS_ARCHIVE_OTHER;
	if (kind == AE_IFLNK || archive_entry_hardlink(value) ||
		archive_entry_symlink(value))
		entry->kind = HX_OS_ARCHIVE_LINK;
	if (job->format >= HX_OS_ARCHIVE_GZIP) {
		if (job->compression != job->format)
			return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Raw stream compression does not match its "
				"format");
		status = hx_ar_raw_name(job, path);
		if (status != HX_OS_OK)
			return status;
		name = hx_string_c_str(path);
	}
	status = hx_ar_i_path(
		job, name, path, entry->kind == HX_OS_ARCHIVE_DIRECTORY);
	if (status != HX_OS_OK)
		return status;
	entry->path = hx_string_view_of(path);
	if (archive_entry_size_is_set(value)) {
		if (bytes < 0)
			return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Negative archive entry size");
		entry->bytes = (uint64_t)bytes;
		entry->flags |= HX_OS_ARCHIVE_SIZE_KNOWN;
	}
	if (archive_entry_is_encrypted(value) == 1)
		entry->flags |= HX_OS_ARCHIVE_ENCRYPTED;
	if (archive_entry_mtime_is_set(value)) {
		if (nanos < 0 || nanos >= 1000000000L)
			return hx_ar_i_fail(job, HX_OS_INVALID, NULL,
				"Invalid archive modification time");
		entry->flags |= HX_OS_ARCHIVE_MTIME_KNOWN;
		entry->modified_seconds = (int64_t)archive_entry_mtime(value);
		entry->modified_nanoseconds = (uint32_t)nanos;
	}

	return hx_ar_i_entry(job, entry);
}

static hx_os_status hx_ar_unpack_data(hx_ar_job *job, struct archive *native,
	const hx_os_archive_entry *entry, hx_os_file *output, void *buffer)
{
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	uint64_t total = 0;

	while (status == HX_OS_OK) {
		la_ssize_t count;
		size_t written;

		status = hx_ar_i_check(job, 1);
		if (status != HX_OS_OK)
			break;
		count = archive_read_data(native, buffer, HX_AR_CHUNK);
		if (count < 0)
			return hx_ar_i_native_error(job, native, (int)count);
		if (!count) {
			if ((entry->flags & HX_OS_ARCHIVE_SIZE_KNOWN) &&
				total != entry->bytes)
				status = hx_os_i_error(
					&error, HX_OS_TRUNCATED_INPUT, 0, 0);
			break;
		}
		status = hx_ar_i_progress(
			job, (uint64_t)count, job->input_position);
		if (status == HX_OS_OK)
			status = hx_os_file_write_all(output, buffer,
				(size_t)count, &written, &error);
		total += (uint64_t)count;
	}
	if (status != HX_OS_OK)
		return hx_ar_i_fail(
			job, status, &error, "Extract archive contents");

	return status;
}

hx_os_status hx_ar_i_read(hx_ar_job *job)
{
	struct archive *native = archive_read_new();
	struct archive_entry *value;
	hx_string path = HX_STRING_INIT, selection = HX_STRING_INIT;
	hx_string output_name = HX_STRING_INIT;
	hx_ar_sink *pending = NULL;
	void *plain = NULL;
	hx_os_status status;
	int result, matched = 0;

	hx_string_init(&path, job->context);
	hx_string_init(&selection, job->context);
	hx_string_init(&output_name, job->context);
	if (!native) {
		status = hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
		goto done;
	}
	result = hx_ar_reader_options(job, native);
	status = result == ARCHIVE_OK
		? hx_ar_i_reader_open(job, native)
		: hx_ar_i_native_error(job, native, result);
	if (status == HX_OS_OK)
		status = hx_ar_i_destination(job, &selection, &output_name);
	if (status == HX_OS_OK && !job->visit) {
		plain = hx_os_i_alloc(job->context, HX_AR_CHUNK, 0);
		if (!plain)
			status = hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
	}
	while (status == HX_OS_OK) {
		hx_os_archive_entry entry;
		hx_os_error callback_error = {0};
		hx_os_file *file = NULL;

		status = hx_ar_i_check(job, 1);
		if (status != HX_OS_OK)
			break;
		result = archive_read_next_header(native, &value);
		if (result != ARCHIVE_OK && result != ARCHIVE_EOF) {
			status = hx_ar_i_native_error(job, native, result);
			break;
		}
		if (result == ARCHIVE_EOF)
			break;
		if (pending) {
			status = hx_ar_i_publish(pending);
			hx_ar_i_sink_free(&pending);
			if (status != HX_OS_OK)
				break;
		}
		status = hx_ar_read_entry(job, native, value, &path, &entry);
		if (status != HX_OS_OK)
			break;
		if (job->visit) {
			void *locale = hx_ar_i_callback_begin(job);

			status = job->visit(
				&entry, job->visit_user, &callback_error);
			hx_ar_i_callback_end(locale);
			if (status != HX_OS_OK)
				(void)hx_ar_i_fail(job, status, &callback_error,
					"Archive visitor stopped");
		}
		else if (job->selection &&
			strcmp(hx_string_c_str(&selection),
				hx_string_c_str(&path)))
			continue;
		else if (entry.kind == HX_OS_ARCHIVE_DIRECTORY &&
			!job->selection)
			status = hx_ar_i_directory(
				job->output_root, hx_string_c_str(&path));
		else if (entry.kind != HX_OS_ARCHIVE_FILE)
			status = hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
				"Only regular files and directories can be "
				"extracted");
		else {
			matched = 1;
			status = hx_ar_i_sink(job->output_root,
				job->selection ? hx_string_c_str(&output_name)
					       : hx_string_c_str(&path),
				(entry.flags & HX_OS_ARCHIVE_ENCRYPTED) != 0,
				&pending, &file);
			if (status == HX_OS_OK)
				status = hx_ar_unpack_data(
					job, native, &entry, file, plain);
		}
	}
	if (status == HX_OS_OK && job->selection && !matched)
		status = hx_ar_i_fail(job, HX_OS_NOT_FOUND, NULL,
			"Archive entry was not found");
	if (status == HX_OS_OK)
		status = hx_ar_i_decode_finish(job);
	if (status == HX_OS_OK) {
		result = archive_read_close(native);
		if (result != ARCHIVE_OK)
			status = hx_ar_i_native_error(job, native, result);
	}
	if (status == HX_OS_OK && pending)
		status = hx_ar_i_publish(pending);
done:
	hx_ar_i_sink_free(&pending);
	if (native)
		(void)archive_read_free(native);
	if (plain)
		hx_os_secure_zero(plain, HX_AR_CHUNK);
	hx_os_memory_free(plain);
	hx_string_destroy(&output_name);
	hx_string_destroy(&selection);
	hx_string_destroy(&path);

	return status;
}
#endif
