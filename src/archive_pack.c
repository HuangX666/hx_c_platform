#include "archive_private.h"

#if HX_OS_HAVE_ARCHIVE
#include <stdio.h>

static int hx_ar_writer_options(hx_ar_job *job, struct archive *native)
{
	int result, filter = ARCHIVE_FILTER_NONE;
	const char *level = job->options.preset == HX_OS_ARCHIVE_FAST ? "1"
		: job->options.preset == HX_OS_ARCHIVE_SMALL	      ? "9"
								      : "6";
	char threads[16];

	if (job->options.flags & HX_OS_ARCHIVE_ENCRYPT_NAMES)
		return ARCHIVE_FAILED;
	if (job->options.password && job->format != HX_OS_ARCHIVE_ZIP)
		return ARCHIVE_FAILED;
	if (job->format == HX_OS_ARCHIVE_ZIP)
		result = archive_write_set_format_zip(native);
	else if (job->format == HX_OS_ARCHIVE_7ZIP)
		result = archive_write_set_format_7zip(native);
	else if (job->format >= HX_OS_ARCHIVE_GZIP)
		result = archive_write_set_format_raw(native);
	else if (job->format >= HX_OS_ARCHIVE_TAR &&
		job->format <= HX_OS_ARCHIVE_TAR_ZSTD)
		result = archive_write_set_format_pax_restricted(native);
	else
		return ARCHIVE_FAILED;
	if (result != ARCHIVE_OK)
		return result;
	if (job->format == HX_OS_ARCHIVE_ZIP) {
		result = archive_write_set_format_option(
			native, "zip", "hdrcharset", "UTF-8");
		if (result == ARCHIVE_OK)
			result = archive_write_set_format_option(
				native, "zip", "compression-level", level);
		if (result == ARCHIVE_OK && job->options.password)
			result = archive_write_set_passphrase(
				native, job->options.password);
		if (result == ARCHIVE_OK && job->options.password)
			result = archive_write_set_format_option(
				native, "zip", "encryption", "aes256");
	}
	if (job->format == HX_OS_ARCHIVE_7ZIP) {
		result = archive_write_set_format_option(
			native, "7zip", "compression-level", level);
		(void)snprintf(
			threads, sizeof(threads), "%u", job->options.workers);
		if (result == ARCHIVE_OK)
			result = archive_write_set_format_option(
				native, "7zip", "threads", threads);
	}
	if (job->format == HX_OS_ARCHIVE_GZIP ||
		job->format == HX_OS_ARCHIVE_TAR_GZIP)
		filter = ARCHIVE_FILTER_GZIP;
	if (job->format == HX_OS_ARCHIVE_XZ ||
		job->format == HX_OS_ARCHIVE_TAR_XZ)
		filter = ARCHIVE_FILTER_XZ;
	if (job->format == HX_OS_ARCHIVE_ZSTD ||
		job->format == HX_OS_ARCHIVE_TAR_ZSTD)
		filter = ARCHIVE_FILTER_ZSTD;
	if (result == ARCHIVE_OK)
		result = archive_write_add_filter(native, filter);
	/* WARN can select an external helper: do not open such a writer. */
	if (result != ARCHIVE_OK || filter == ARCHIVE_FILTER_NONE)
		return result;
	if (filter == ARCHIVE_FILTER_ZSTD)
		level = job->options.preset == HX_OS_ARCHIVE_FAST    ? "1"
			: job->options.preset == HX_OS_ARCHIVE_SMALL ? "19"
								     : "3";
	result = archive_write_set_filter_option(
		native, NULL, "compression-level", level);
	if (result == ARCHIVE_OK && filter != ARCHIVE_FILTER_GZIP) {
		unsigned workers = job->options.workers;

		if (filter == ARCHIVE_FILTER_ZSTD && workers == 1)
			workers = 0;
		(void)snprintf(threads, sizeof(threads), "%u", workers);
		result = archive_write_set_filter_option(
			native, NULL, "threads", threads);
	}

	return result;
}

static hx_os_status hx_ar_pack_data(hx_ar_job *job, struct archive *native,
	const hx_os_archive_entry *entry)
{
	hx_os_file *input = NULL;
	hx_os_error error = {0};
	hx_os_status status =
		hx_ar_i_input(job->source_root, entry->path.data, &input);
	uint64_t total = 0;

	while (status == HX_OS_OK) {
		size_t count = 0, offset = 0;

		status = hx_ar_i_check(job, 1);
		if (status != HX_OS_OK)
			break;
		status = hx_os_file_read_some(
			input, job->buffer, HX_AR_CHUNK, &count, &error);
		if (status == HX_OS_EOF) {
			status = total == entry->bytes ? HX_OS_OK
						       : HX_OS_TRUNCATED_INPUT;
			break;
		}
		if (status != HX_OS_OK)
			break;
		if (count > entry->bytes - total) {
			status = HX_OS_BUSY;
			break;
		}
		status = hx_ar_i_progress(job, count, job->output_position);
		while (status == HX_OS_OK && offset < count) {
			la_ssize_t written = archive_write_data(
				native, job->buffer + offset, count - offset);

			if (written <= 0) {
				status = hx_ar_i_native_error(
					job, native, ARCHIVE_FAILED);
				break;
			}
			offset += (size_t)written;
		}
		total += count;
	}
	if (status == HX_OS_OK)
		status = hx_os_file_close(&input, &error);
	else
		(void)hx_os_file_close(&input, NULL);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status,
			error.status == status ? &error : NULL,
			"Packing input changed or could not be read");

	return status;
}

static hx_os_status hx_ar_pack_entry(
	hx_ar_job *job, const hx_os_archive_entry *entry, void *user)
{
	struct archive *native = user;
	struct archive_entry *value;
	hx_os_status status = HX_OS_OK;
	int result;

	if (job->format >= HX_OS_ARCHIVE_GZIP &&
		(job->source_directory || job->result.entries > 1))
		return hx_ar_i_fail(job, HX_OS_UNREPRESENTABLE, NULL,
			"Raw compression needs one file; use a TAR variant for "
			"directories");
	if (entry->bytes >
		job->options.max_content_bytes - job->result.content_bytes)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Packing content size");
	value = archive_entry_new();
	if (!value)
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
#if defined(_WIN32)
	{
		size_t units = entry->path.size + 1, required;
		wchar_t *wide =
			hx_os_i_alloc(job->context, units * sizeof(*wide), 0);
		hx_os_error error = {0};

		status = wide
			? hx_os_text_to_wide(entry->path.data, entry->path.size,
				  wide, units, &required, &error)
			: HX_OS_NO_MEMORY;
		if (status == HX_OS_OK)
			archive_entry_copy_pathname_w(value, wide);
		hx_os_memory_free(wide);
		if (status != HX_OS_OK) {
			archive_entry_free(value);
			return hx_ar_i_fail(
				job, status, &error, "Archive wide name");
		}
	}
#else
	archive_entry_set_pathname_utf8(value, entry->path.data);
#endif
	archive_entry_set_filetype(value,
		entry->kind == HX_OS_ARCHIVE_DIRECTORY ? AE_IFDIR : AE_IFREG);
	archive_entry_set_perm(
		value, entry->kind == HX_OS_ARCHIVE_DIRECTORY ? 0700 : 0600);
	archive_entry_set_size(value, (la_int64_t)entry->bytes);
	archive_entry_set_mtime(value, (time_t)entry->modified_seconds,
		(long)entry->modified_nanoseconds);
	result = archive_write_header(native, value);
	if (result != ARCHIVE_OK)
		status = hx_ar_i_native_error(job, native, result);
	if (status == HX_OS_OK && entry->kind == HX_OS_ARCHIVE_FILE)
		status = hx_ar_pack_data(job, native, entry);
	if (status == HX_OS_OK) {
		result = archive_write_finish_entry(native);
		if (result != ARCHIVE_OK)
			status = hx_ar_i_native_error(job, native, result);
	}
	archive_entry_free(value);

	return status;
}

static hx_os_status hx_ar_pack_write(
	hx_os_file *file, void *user, hx_os_error *error)
{
	hx_ar_job *job = user;
	struct archive *native = archive_write_new();
	hx_os_status status;
	int result, opened;
	(void)error;

	if (!native)
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL, NULL);
	job->output = file;
	result = hx_ar_writer_options(job, native);
	status = result == ARCHIVE_OK
		? hx_ar_i_writer_open(job, native)
		: hx_ar_i_native_error(job, native, result);
	opened = status == HX_OS_OK;
	if (status == HX_OS_OK)
		status = hx_ar_i_walk(job, hx_ar_pack_entry, native);
	if (status == HX_OS_OK && job->source_directory &&
		job->format >= HX_OS_ARCHIVE_GZIP)
		status = hx_ar_i_fail(job, HX_OS_UNREPRESENTABLE, NULL,
			"A directory requires a container format");
	if (status == HX_OS_OK) {
		result = archive_write_close(native);
		if (result != ARCHIVE_OK)
			status = hx_ar_i_native_error(job, native, result);
	}
	else {
		if (opened && job->format == HX_OS_ARCHIVE_ZIP)
			(void)archive_write_finish_entry(native);
		(void)archive_write_fail(native);
		/* FATAL skips unfinished entry padding. Close still releases
		 * filters and joins the 7z empty/directory lists to the list
		 * that format_free owns. Failed output is discarded. */
		if (opened)
			(void)archive_write_close(native);
	}
	result = archive_write_free(native);
	if (status == HX_OS_OK && result != ARCHIVE_OK)
		status = hx_ar_i_fail(
			job, HX_OS_IO_ERROR, NULL, "Release archive writer");
	job->output = NULL;
	if (status == HX_OS_OK)
		status = hx_ar_i_check(job, 0);

	return status;
}

hx_os_status hx_ar_i_pack(hx_ar_job *job)
{
	hx_os_error error = {0};
	hx_os_file_save_result result;
	hx_os_status status;

	if (!job->format ||
		(job->options.flags & HX_OS_ARCHIVE_ENCRYPT_NAMES) ||
		(job->options.password && job->format != HX_OS_ARCHIVE_ZIP))
		return hx_ar_i_fail(job, HX_OS_NOT_SUPPORTED, NULL,
			"Unsupported archive format or encryption combination");
	status = hx_os_i_file_save_callback(job->context, job->archive_path,
		(job->options.flags & HX_OS_ARCHIVE_OVERWRITE) != 0, 0,
		hx_ar_pack_write, job, &result, &error);
	job->result.published = (uint32_t)result.published;
	if (status != HX_OS_OK)
		return hx_ar_i_fail(
			job, status, &error, "Publish archive output");

	return HX_OS_OK;
}
#endif
