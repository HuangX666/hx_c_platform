#include "internal.h"
#include "hx_os_compression.h"

#if HX_OS_HAVE_COMPRESSION
#include <zlib.h>
#include <lzma.h>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zstd_errors.h>

static hx_os_status hx_os_compression_append(hx_bytes *out, const void *data,
	size_t bytes, size_t maximum, hx_os_error *error)
{
	if (bytes > maximum - out->size)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	return hx_bytes_append(out, (hx_os_bytes_view){data, bytes}, error);
}

static hx_os_status hx_os_compression_zlib(unsigned format,
	hx_os_bytes_view input, const hx_os_compression_options *options,
	hx_bytes *out, int decode, hx_os_error *error)
{
	z_stream stream;
	unsigned char buffer[32768];
	int code, bits = format == HX_OS_COMPRESS_GZIP ? 31 : 15;
	hx_os_status status = HX_OS_OK;

	if (options->max_codec_bytes < 1024 * 1024)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	memset(&stream, 0, sizeof(stream));
	code = decode ? inflateInit2(&stream, bits)
		      : deflateInit2(&stream, options->level, Z_DEFLATED, bits,
				8, Z_DEFAULT_STRATEGY);
	if (code != Z_OK)
		return hx_os_i_error(error,
			code == Z_MEM_ERROR ? HX_OS_NO_MEMORY : HX_OS_INVALID,
			HX_OS_ERROR_ZLIB, code);
	stream.next_in = (Bytef *)(uintptr_t)input.data;
	stream.avail_in = (uInt)input.size;
	for (;;) {
		size_t produced;

		stream.next_out = buffer;
		stream.avail_out = sizeof(buffer);
		code = decode ? inflate(&stream, Z_FINISH)
			      : deflate(&stream, Z_FINISH);
		produced = sizeof(buffer) - stream.avail_out;
		if (code != Z_OK && code != Z_STREAM_END &&
			code != Z_BUF_ERROR) {
			status = hx_os_i_error(error,
				code == Z_MEM_ERROR ? HX_OS_NO_MEMORY
						    : HX_OS_SYNTAX,
				HX_OS_ERROR_ZLIB, code);
			break;
		}
		status = hx_os_compression_append(out, buffer, produced,
			options->max_output_bytes, error);
		if (status != HX_OS_OK)
			break;
		if (code == Z_STREAM_END) {
			if (stream.avail_in)
				status = hx_os_i_error(error, HX_OS_SYNTAX,
					HX_OS_ERROR_ZLIB, Z_DATA_ERROR);
			break;
		}
		if (!produced) {
			status = hx_os_i_error(error, HX_OS_TRUNCATED_INPUT,
				HX_OS_ERROR_ZLIB, code);
			break;
		}
	}
	if (decode)
		(void)inflateEnd(&stream);
	else
		(void)deflateEnd(&stream);
	return status;
}

static hx_os_status hx_os_compression_xz(hx_os_bytes_view input,
	const hx_os_compression_options *options, hx_bytes *out, int decode,
	hx_os_error *error)
{
	lzma_stream stream = LZMA_STREAM_INIT;
	unsigned char buffer[32768];
	lzma_ret code;
	hx_os_status status = HX_OS_OK;

	if (!decode &&
		lzma_easy_encoder_memusage((uint32_t)options->level) >
			options->max_codec_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	code = decode
		? lzma_stream_decoder(&stream, options->max_codec_bytes, 0)
		: lzma_easy_encoder(
			  &stream, (uint32_t)options->level, LZMA_CHECK_CRC64);
	if (code != LZMA_OK)
		return hx_os_i_error(error,
			code == LZMA_MEM_ERROR ? HX_OS_NO_MEMORY
					       : HX_OS_INVALID,
			HX_OS_ERROR_LZMA, code);
	stream.next_in = input.data;
	stream.avail_in = input.size;
	for (;;) {
		size_t produced;

		stream.next_out = buffer;
		stream.avail_out = sizeof(buffer);
		code = lzma_code(&stream, LZMA_FINISH);
		produced = sizeof(buffer) - stream.avail_out;
		if (code != LZMA_OK && code != LZMA_STREAM_END) {
			status = hx_os_i_error(error,
				code == LZMA_MEMLIMIT_ERROR ? HX_OS_LIMIT
					: code == LZMA_MEM_ERROR
					? HX_OS_NO_MEMORY
					: code == LZMA_BUF_ERROR
					? HX_OS_TRUNCATED_INPUT
					: HX_OS_SYNTAX,
				HX_OS_ERROR_LZMA, code);
			break;
		}
		status = hx_os_compression_append(out, buffer, produced,
			options->max_output_bytes, error);
		if (status != HX_OS_OK)
			break;
		if (code == LZMA_STREAM_END) {
			if (stream.avail_in)
				status = hx_os_i_error(error, HX_OS_SYNTAX,
					HX_OS_ERROR_LZMA, LZMA_DATA_ERROR);
			break;
		}
	}
	lzma_end(&stream);
	return status;
}

static hx_os_status hx_os_compression_zstd(hx_os_bytes_view input,
	const hx_os_compression_options *options, hx_bytes *out, int decode,
	hx_os_error *error)
{
	ZSTD_CStream *encoder = NULL;
	ZSTD_DStream *decoder = NULL;
	ZSTD_inBuffer source = {input.data, input.size, 0};
	unsigned char buffer[32768];
	size_t code;
	hx_os_status status = HX_OS_OK;

	if (decode) {
		decoder = ZSTD_createDStream();
		if (!decoder)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		{
			size_t window = options->max_codec_bytes / 4;

			while (window >= 1024 &&
				ZSTD_estimateDStreamSize(window) >
					options->max_codec_bytes)
				window /= 2;
			code = ZSTD_DCtx_setMaxWindowSize(decoder, window);
		}
	}
	else {
		if (ZSTD_estimateCStreamSize(options->level) >
			options->max_codec_bytes)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		encoder = ZSTD_createCStream();
		if (!encoder)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		code = ZSTD_initCStream(encoder, options->level);
	}
	while (!ZSTD_isError(code)) {
		ZSTD_outBuffer destination = {buffer, sizeof(buffer), 0};
		size_t before = source.pos;

		code = decode
			? ZSTD_decompressStream(decoder, &destination, &source)
			: ZSTD_compressStream2(
				  encoder, &destination, &source, ZSTD_e_end);
		if (ZSTD_isError(code))
			break;
		status = hx_os_compression_append(out, buffer, destination.pos,
			options->max_output_bytes, error);
		if (status != HX_OS_OK)
			break;
		if (!code) {
			if (source.pos != input.size)
				status = hx_os_i_error(
					error, HX_OS_SYNTAX, 0, 0);
			break;
		}
		if (source.pos == before && !destination.pos) {
			status = hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
			break;
		}
	}
	if (ZSTD_isError(code)) {
		ZSTD_ErrorCode detail = ZSTD_getErrorCode(code);

		status = hx_os_i_error(error,
			detail == ZSTD_error_memory_allocation ? HX_OS_NO_MEMORY
				: detail ==
					ZSTD_error_frameParameter_windowTooLarge
				? HX_OS_LIMIT
				: HX_OS_SYNTAX,
			HX_OS_ERROR_ZSTD, detail);
	}
	ZSTD_freeCStream(encoder);
	ZSTD_freeDStream(decoder);
	return status;
}
#endif

int hx_os_compression_available(unsigned format)
{
	return HX_OS_HAVE_COMPRESSION && format >= HX_OS_COMPRESS_ZLIB &&
		format <= HX_OS_COMPRESS_ZSTD;
}

hx_os_status hx_os_compression_options_init(
	hx_os_compression_options *options, size_t capacity)
{
	const hx_os_compression_options defaults =
		HX_OS_COMPRESSION_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;
	return HX_OS_OK;
}

static hx_os_status hx_os_compression_run(unsigned format,
	hx_os_bytes_view input, const hx_os_compression_options *options,
	hx_bytes *out, int decode, hx_os_error *error)
{
#if HX_OS_HAVE_COMPRESSION
	const hx_os_compression_options defaults =
		HX_OS_COMPRESSION_OPTIONS_INIT;
	hx_bytes result;
	hx_os_status status;

	if (!options)
		options = &defaults;
	if (!out || (!input.data && input.size) ||
		options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_compression_available(format))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (input.size > HX_BYTES_MAX_SIZE || !options->max_output_bytes ||
		options->max_output_bytes > HX_BYTES_MAX_SIZE ||
		options->max_codec_bytes < 1024 * 1024 ||
		options->max_codec_bytes > 1024 * 1024 * 1024)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (!decode &&
		(options->level < (format == HX_OS_COMPRESS_ZSTD) ||
			options->level >
				(format == HX_OS_COMPRESS_ZSTD ? 19 : 9)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_bytes_init(&result, out->context);
	if (format == HX_OS_COMPRESS_XZ)
		status = hx_os_compression_xz(
			input, options, &result, decode, error);
	else if (format == HX_OS_COMPRESS_ZSTD)
		status = hx_os_compression_zstd(
			input, options, &result, decode, error);
	else
		status = hx_os_compression_zlib(
			format, input, options, &result, decode, error);
	if (status == HX_OS_OK)
		hx_bytes_move(out, &result);
	hx_bytes_destroy(&result);
	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
#else
	(void)format;
	(void)input;
	(void)options;
	(void)out;
	(void)decode;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_compress(unsigned format, hx_os_bytes_view input,
	const hx_os_compression_options *options, hx_bytes *out,
	hx_os_error *error)
{
	return hx_os_compression_run(format, input, options, out, 0, error);
}

hx_os_status hx_os_decompress(unsigned format, hx_os_bytes_view input,
	const hx_os_compression_options *options, hx_bytes *out,
	hx_os_error *error)
{
	return hx_os_compression_run(format, input, options, out, 1, error);
}

const char *hx_os_i_compression_message(uint32_t domain, int64_t code)
{
#if HX_OS_HAVE_COMPRESSION
	static const char *const xz_messages[] = {"ok", "stream end",
		"no integrity check", "unsupported integrity check", "check",
		"allocation failed", "memory limit", "unrecognized format",
		"unsupported options", "damaged compressed data", "no progress",
		"invalid codec usage", "operation timed out"};

	if (domain == HX_OS_ERROR_ZLIB && code >= INT_MIN && code <= INT_MAX)
		return zError((int)code);
	if (domain == HX_OS_ERROR_LZMA && code >= 0 &&
		(size_t)code < sizeof(xz_messages) / sizeof(xz_messages[0]))
		return xz_messages[code];
	if (domain == HX_OS_ERROR_ZSTD && code >= 0 &&
		code < ZSTD_error_maxCode)
		return ZSTD_getErrorString((ZSTD_ErrorCode)code);
#else
	(void)domain;
	(void)code;
#endif
	return "compression failure";
}
