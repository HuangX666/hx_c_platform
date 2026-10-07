#include "archive_private.h"

#if HX_OS_HAVE_ARCHIVE
#include <zlib.h>
#include <lzma.h>
#include <zstd.h>
#include <zstd_errors.h>

struct hx_ar_decoder {
	z_stream gzip;
	lzma_stream xz;
	ZSTD_DStream *zstd;
	size_t available, offset;
	uint64_t decoded;
	int eof, boundary, finished, initialized;
	unsigned char output[HX_AR_CHUNK];
};

hx_os_status hx_ar_i_decode_open(hx_ar_job *job)
{
	hx_ar_decoder *decoder;
	int success;

	if (!job->compression)
		return HX_OS_OK;
	decoder = hx_os_i_zero(job->context, sizeof(*decoder));
	if (!decoder)
		return hx_ar_i_fail(
			job, HX_OS_NO_MEMORY, NULL, "Archive decoder buffers");
	job->decoder = decoder;
	if (job->compression == HX_OS_ARCHIVE_GZIP)
		success = inflateInit2(&decoder->gzip, MAX_WBITS + 16) == Z_OK;
	else if (job->compression == HX_OS_ARCHIVE_XZ)
		success = lzma_stream_decoder(&decoder->xz, UINT64_MAX,
				  LZMA_CONCATENATED) == LZMA_OK;
	else {
		decoder->zstd = ZSTD_createDStream();
		success = decoder->zstd &&
			!ZSTD_isError(ZSTD_initDStream(decoder->zstd));
	}
	decoder->initialized = success;
	if (!success)
		return hx_ar_i_fail(job, HX_OS_NO_MEMORY, NULL,
			"Create native archive decoder");
	return HX_OS_OK;
}

static int hx_ar_decode_step(hx_ar_job *job, size_t *produced)
{
	hx_ar_decoder *decoder = job->decoder;
	const unsigned char *input = job->buffer + decoder->offset;
	size_t remaining = decoder->available - decoder->offset, consumed;
	int finished = 0;
	hx_os_status status = HX_OS_IO_ERROR;
	hx_os_error error = {0};
	uint64_t native_code = 0;
	const char *message = "Compressed stream is truncated or corrupt";

	if (decoder->eof && !remaining && decoder->boundary &&
		job->compression != HX_OS_ARCHIVE_XZ) {
		decoder->finished = 1;
		return 1;
	}
	if (job->compression == HX_OS_ARCHIVE_GZIP) {
		z_stream *stream = &decoder->gzip;
		int result;

		if (decoder->boundary && remaining) {
			result = inflateReset2(stream, MAX_WBITS + 16);
			if (result != Z_OK) {
				native_code = (uint64_t)(int64_t)result;
				message = zError(result);
				goto failed;
			}
			decoder->boundary = 0;
		}
		stream->next_in = (Bytef *)(uintptr_t)input;
		stream->avail_in = (uInt)remaining;
		stream->next_out = decoder->output;
		stream->avail_out = HX_AR_CHUNK;
		result = inflate(stream, Z_NO_FLUSH);
		consumed = remaining - stream->avail_in;
		*produced = HX_AR_CHUNK - stream->avail_out;
		decoder->boundary = result == Z_STREAM_END;
		if (result != Z_OK && result != Z_STREAM_END) {
			native_code = (uint64_t)(int64_t)result;
			message = zError(result);
			status = result == Z_MEM_ERROR	? HX_OS_NO_MEMORY
				: result == Z_BUF_ERROR ? HX_OS_TRUNCATED_INPUT
							: HX_OS_IO_ERROR;
			goto failed;
		}
	}
	else if (job->compression == HX_OS_ARCHIVE_XZ) {
		lzma_stream *stream = &decoder->xz;
		lzma_ret result;

		stream->next_in = input;
		stream->avail_in = remaining;
		stream->next_out = decoder->output;
		stream->avail_out = HX_AR_CHUNK;
		result = lzma_code(
			stream, decoder->eof ? LZMA_FINISH : LZMA_RUN);
		consumed = remaining - stream->avail_in;
		*produced = HX_AR_CHUNK - stream->avail_out;
		finished = result == LZMA_STREAM_END;
		if (result != LZMA_OK && !finished) {
			native_code = (uint64_t)result;
			status = result == LZMA_MEM_ERROR ? HX_OS_NO_MEMORY
							  : HX_OS_IO_ERROR;
			goto failed;
		}
	}
	else {
		ZSTD_inBuffer in = {input, remaining, 0};
		ZSTD_outBuffer out = {decoder->output, HX_AR_CHUNK, 0};
		size_t result = ZSTD_decompressStream(decoder->zstd, &out, &in);

		consumed = in.pos;
		*produced = out.pos;
		decoder->boundary = result == 0;
		if (ZSTD_isError(result)) {
			native_code = (uint64_t)ZSTD_getErrorCode(result);
			message = ZSTD_getErrorName(result);
			goto failed;
		}
	}
	decoder->offset += consumed;
	decoder->finished = finished;
	if (!finished && !consumed && !*produced && decoder->eof) {
		status = HX_OS_TRUNCATED_INPUT;
		goto failed;
	}
	return 1;
failed:
	(void)hx_os_i_error(&error, status, HX_OS_ERROR_ARCHIVE, native_code);
	error.cause_code = job->compression;
	(void)hx_ar_i_fail(job, status, &error, message);
	return 0;
}

la_ssize_t hx_ar_i_decode_read(hx_ar_job *job, const void **buffer)
{
	hx_ar_decoder *decoder = job->decoder;
	hx_os_error error = {0};
	hx_os_status status;

	*buffer = decoder->output;
	while (!decoder->finished) {
		size_t produced = 0;

		if (hx_ar_i_check(job, 1) != HX_OS_OK)
			return -1;
		if (decoder->offset == decoder->available && !decoder->eof) {
			status = hx_os_file_read_some(job->input, job->buffer,
				HX_AR_CHUNK, &decoder->available, &error);
			decoder->offset = 0;
			decoder->eof = status == HX_OS_EOF;
			if (status != HX_OS_OK && !decoder->eof) {
				(void)hx_ar_i_fail(job, status, &error,
					"Read compressed stream");
				return -1;
			}
			job->input_position += decoder->available;
			if (hx_ar_i_progress(job, 0, job->input_position) !=
				HX_OS_OK)
				return -1;
		}
		if (!hx_ar_decode_step(job, &produced))
			return -1;
		if (produced >
			job->options.max_decoded_bytes - decoder->decoded) {
			(void)hx_ar_i_fail(job, HX_OS_LIMIT, NULL,
				"Decoded container byte limit (including "
				"headers and padding)");
			return -1;
		}
		decoder->decoded += produced;
		if (produced)
			return (la_ssize_t)produced;
	}
	return 0;
}

hx_os_status hx_ar_i_decode_finish(hx_ar_job *job)
{
	const void *buffer;
	la_ssize_t result;

	if (!job->decoder)
		return HX_OS_OK;
	do {
		result = hx_ar_i_decode_read(job, &buffer);
	} while (result > 0);
	return result < 0 ? HX_OS_IO_ERROR : HX_OS_OK;
}

void hx_ar_i_decode_close(hx_ar_job *job)
{
	hx_ar_decoder *decoder = job->decoder;

	if (!decoder)
		return;
	if (decoder->initialized && job->compression == HX_OS_ARCHIVE_GZIP)
		(void)inflateEnd(&decoder->gzip);
	if (job->compression == HX_OS_ARCHIVE_XZ)
		lzma_end(&decoder->xz);
	(void)ZSTD_freeDStream(decoder->zstd);
	hx_os_secure_zero(decoder->output, HX_AR_CHUNK);
	hx_os_memory_free(decoder);
	job->decoder = NULL;
}
#endif
