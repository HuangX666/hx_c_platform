#include "archive_private.h"

#if HX_OS_HAVE_ARCHIVE
#include <stdio.h>

hx_os_status hx_ar_i_native_error(
	hx_ar_job *job, struct archive *native, int result)
{
	hx_os_error error = {0};
	hx_os_status status = archive_errno(native) == ENOMEM ? HX_OS_NO_MEMORY
							      : HX_OS_IO_ERROR;

	(void)hx_os_i_error(&error, status, HX_OS_ERROR_ARCHIVE,
		(uint64_t)(int64_t)archive_errno(native));
	error.cause_code = (uint64_t)(int64_t)result;
	return hx_ar_i_fail(job, status, &error, archive_error_string(native));
}

hx_os_status hx_ar_i_open_input(hx_ar_job *job)
{
	unsigned char signature[6];
	size_t count = 0;
	uint64_t position;
	hx_os_error error = {0};
	hx_os_status status = hx_os_file_open(job->context, job->archive_path,
		HX_OS_FILE_READ, &job->input, &error);

	if (status == HX_OS_OK)
		status = hx_os_file_size(job->input, &job->input_size, &error);
	if (status == HX_OS_OK &&
		job->input_size > job->options.max_archive_bytes)
		return hx_ar_i_fail(
			job, HX_OS_LIMIT, NULL, "Archive input size");
	if (status == HX_OS_OK)
		status = hx_os_file_read_some(job->input, signature,
			sizeof(signature), &count, &error);
	if (status == HX_OS_EOF)
		status = HX_OS_OK;
	if (status == HX_OS_OK)
		status = hx_os_file_seek(
			job->input, 0, HX_OS_SEEK_BEGIN, &position, &error);
	if (status != HX_OS_OK)
		return hx_ar_i_fail(job, status, &error, "Open archive input");
	if (!job->options.format && count == sizeof(signature) &&
		!memcmp(signature, "7z\xbc\xaf\x27\x1c", sizeof(signature)))
		job->format = HX_OS_ARCHIVE_7ZIP;
	if (count >= 2 && signature[0] == 0x1f && signature[1] == 0x8b)
		job->compression = HX_OS_ARCHIVE_GZIP;
	else if (count == 6 &&
		!memcmp(signature,
			"\xfd"
			"7zXZ\0",
			6))
		job->compression = HX_OS_ARCHIVE_XZ;
	else if (count >= 4 &&
		((!memcmp(signature, "\x28\xb5\x2f\xfd", 4)) ||
			((signature[0] & 0xf0) == 0x50 &&
				signature[1] == 0x2a && signature[2] == 0x4d &&
				signature[3] == 0x18)))
		job->compression = HX_OS_ARCHIVE_ZSTD;

	return HX_OS_OK;
}

static la_ssize_t hx_ar_read_block(
	struct archive *native, void *user, const void **buffer)
{
	hx_ar_job *job = user;
	hx_os_error error = {0};
	hx_os_status status = hx_ar_i_check(job, 1);
	size_t count = 0;
	(void)native;

	*buffer = job->buffer;
	if (job->decoder)
		return hx_ar_i_decode_read(job, buffer);
	if (status == HX_OS_OK)
		status = hx_os_file_read_some(
			job->input, job->buffer, HX_AR_CHUNK, &count, &error);
	if (status == HX_OS_EOF)
		return 0;
	if (status != HX_OS_OK) {
		(void)hx_ar_i_fail(job, status, &error, "Read archive input");
		return -1;
	}
	job->input_position += count;
	if (hx_ar_i_progress(job, 0, job->input_position) != HX_OS_OK)
		return -1;

	return (la_ssize_t)count;
}

static la_int64_t hx_ar_seek_input(
	struct archive *native, void *user, la_int64_t offset, int origin)
{
	hx_ar_job *job = user;
	hx_os_error error = {0};
	uint64_t position;
	int where = origin == SEEK_SET ? HX_OS_SEEK_BEGIN
		: origin == SEEK_CUR   ? HX_OS_SEEK_CURRENT
				       : HX_OS_SEEK_END;
	hx_os_status status =
		hx_os_file_seek(job->input, offset, where, &position, &error);
	(void)native;

	if (status == HX_OS_OK && position > job->input_size)
		status = hx_os_i_error(&error, HX_OS_TRUNCATED_INPUT, 0, 0);
	if (status != HX_OS_OK) {
		(void)hx_ar_i_fail(job, status, &error, "Seek archive input");
		return -1;
	}
	job->input_position = position;

	return (la_int64_t)position;
}

hx_os_status hx_ar_i_reader_open(hx_ar_job *job, struct archive *native)
{
	int result = ARCHIVE_OK;
	hx_os_status status = hx_ar_i_decode_open(job);

	if (status != HX_OS_OK)
		return status;
	if (!job->decoder)
		result = archive_read_set_seek_callback(
			native, hx_ar_seek_input);

	if (result == ARCHIVE_OK)
		result = archive_read_open2(
			native, job, NULL, hx_ar_read_block, NULL, NULL);
	if (result != ARCHIVE_OK)
		return hx_ar_i_native_error(job, native, result);

	return HX_OS_OK;
}

static la_ssize_t hx_ar_write_block(
	struct archive *native, void *user, const void *data, size_t bytes)
{
	hx_ar_job *job = user;
	hx_os_error error = {0};
	hx_os_status status = hx_ar_i_check(job, 1);
	size_t count = 0;
	(void)native;

	if (status == HX_OS_OK &&
		bytes > job->options.max_archive_bytes - job->output_position)
		status = hx_os_i_error(&error, HX_OS_LIMIT, 0, 0);
	if (status == HX_OS_OK)
		status = hx_os_file_write_all(
			job->output, data, bytes, &count, &error);
	job->output_position += count;
	(void)hx_ar_i_progress(job, 0, job->output_position);
	if (status != HX_OS_OK) {
		(void)hx_ar_i_fail(job, status, &error, "Write archive output");
		// Retain the real failure but drain native finalization to a
		// discard sink. The pinned ZIP writer otherwise skips
		// deflateEnd when a client write callback makes it enter its
		// fatal state. The owner checks job.failed before publishing
		// the temporary.
		return (la_ssize_t)bytes;
	}

	return (la_ssize_t)count;
}

hx_os_status hx_ar_i_writer_open(hx_ar_job *job, struct archive *native)
{
	int result = archive_write_set_bytes_per_block(native, HX_AR_CHUNK);

	if (result == ARCHIVE_OK)
		result = archive_write_set_bytes_in_last_block(native, 1);
	if (result == ARCHIVE_OK)
		result = archive_write_open2(
			native, job, NULL, hx_ar_write_block, NULL, NULL);
	if (result != ARCHIVE_OK)
		return hx_ar_i_native_error(job, native, result);

	return HX_OS_OK;
}
#endif
