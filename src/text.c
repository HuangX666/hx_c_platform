#include "internal.h"

#if HX_OS_HAVE_ICONV
#include <iconv.h>
#endif

struct hx_os_text_converter {
	hx_os_encoding source;
	hx_os_encoding target;
	int policy;
	int ended;
	uint64_t offset;
#if HX_OS_HAVE_ICONV
	iconv_t decoder;
	iconv_t encoder;
#endif
};

static int hx_os_text_builtin(hx_os_encoding encoding)
{
	return (encoding >= HX_OS_UTF8 && encoding <= HX_OS_ASCII) ||
		encoding == HX_OS_ISO_8859_1;
}

const char *hx_os_encoding_name(hx_os_encoding value)
{
	static const char *const names[] = {"UTF-8", "UTF-16LE", "UTF-16BE",
		"UTF-32LE", "UTF-32BE", "ASCII", "GB2312", "GBK",
		"GB18030:2005", "GB18030:2022", "BIG5", "SHIFT_JIS", "EUC-JP",
		"EUC-KR", "CP1252", "CP932", "CP936", "CP949", "CP950",
		"ISO-8859-1"};
	return (unsigned)value < sizeof(names) / sizeof(names[0]) ? names[value]
								  : NULL;
}

int hx_os_encoding_supported(hx_os_encoding value)
{
	if (!hx_os_encoding_name(value))
		return 0;
	if (hx_os_text_builtin(value))
		return 1;
#if HX_OS_HAVE_ICONV
	{
		iconv_t decoder =
			iconv_open("UTF-32LE", hx_os_encoding_name(value));
		iconv_t encoder =
			iconv_open(hx_os_encoding_name(value), "UTF-32LE");
		int supported =
			decoder != (iconv_t)-1 && encoder != (iconv_t)-1;
		if (decoder != (iconv_t)-1)
			(void)iconv_close(decoder);
		if (encoder != (iconv_t)-1)
			(void)iconv_close(encoder);
		return supported;
	}
#else
	return 0;
#endif
}

hx_os_status hx_os_text_converter_create(hx_os_context *context,
	hx_os_encoding source, hx_os_encoding target, int policy,
	hx_os_text_converter **out, hx_os_error *error)
{
	hx_os_text_converter *converter;
	if (!out || !hx_os_encoding_name(source) ||
		!hx_os_encoding_name(target) ||
		(policy != HX_OS_TEXT_STRICT && policy != HX_OS_TEXT_REPLACE))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_HAVE_ICONV &&
		(!hx_os_text_builtin(source) || !hx_os_text_builtin(target)))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	converter = hx_os_i_zero(context, sizeof(*converter));
	if (!converter)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	converter->source = source;
	converter->target = target;
	converter->policy = policy;
#if HX_OS_HAVE_ICONV
	converter->decoder = (iconv_t)-1;
	converter->encoder = (iconv_t)-1;
	if (!hx_os_text_builtin(source))
		converter->decoder =
			iconv_open("UTF-32LE", hx_os_encoding_name(source));
	if (!hx_os_text_builtin(target))
		converter->encoder =
			iconv_open(hx_os_encoding_name(target), "UTF-32LE");
	if ((!hx_os_text_builtin(source) &&
		    converter->decoder == (iconv_t)-1) ||
		(!hx_os_text_builtin(target) &&
			converter->encoder == (iconv_t)-1)) {
		int code = errno;
		hx_os_text_converter_destroy(converter);
		return hx_os_i_error(error,
			code == ENOMEM ? HX_OS_NO_MEMORY : HX_OS_NOT_SUPPORTED,
			HX_OS_ERROR_ERRNO, code);
	}
#endif
	*out = converter;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_text_decode(hx_os_text_converter *converter,
	const unsigned char *input, size_t bytes, uint32_t *point, size_t *used)
{
	if (hx_os_text_builtin(converter->source))
		return hx_os_i_utf_decode(
			converter->source, input, bytes, point, used);
#if HX_OS_HAVE_ICONV
	{
		HX_OS_ICONV_CONST char *source =
			(HX_OS_ICONV_CONST char *)input;
		char value[4];
		char *output = value;
		size_t left = bytes;
		size_t available = sizeof(value);
		size_t result;
		(void)iconv(converter->decoder, NULL, NULL, NULL, NULL);
		result = iconv(converter->decoder, &source, &left, &output,
			&available);
		*used = bytes - left;
		if (!available) {
			size_t decoded;
			return hx_os_i_utf_decode(HX_OS_UTF32LE,
				(const unsigned char *)value, 4, point,
				&decoded);
		}
		if (result == (size_t)-1 && errno == EINVAL)
			return HX_OS_NEED_INPUT;
		return HX_OS_INVALID_ENCODING;
	}
#else
	(void)input;
	(void)bytes;
	(void)point;
	(void)used;
	return HX_OS_NOT_SUPPORTED;
#endif
}

static hx_os_status hx_os_text_encode(hx_os_text_converter *converter,
	uint32_t point, unsigned char *output, size_t *used)
{
	if (hx_os_text_builtin(converter->target))
		return hx_os_i_utf_encode(
			converter->target, point, output, 16, used);
#if HX_OS_HAVE_ICONV
	{
		unsigned char value[4];
		HX_OS_ICONV_CONST char *input = (HX_OS_ICONV_CONST char *)value;
		char *destination = (char *)output;
		size_t left = 4;
		size_t capacity = 16;
		size_t encoded;
		size_t result;
		(void)hx_os_i_utf_encode(
			HX_OS_UTF32LE, point, value, 4, &encoded);
		(void)iconv(converter->encoder, NULL, NULL, NULL, NULL);
		result = iconv(converter->encoder, &input, &left, &destination,
			&capacity);
		if (result != 0 || left)
			return HX_OS_UNREPRESENTABLE;
		*used = 16 - capacity;
		return HX_OS_OK;
	}
#else
	(void)point;
	(void)output;
	(void)used;
	return HX_OS_NOT_SUPPORTED;
#endif
}

static size_t hx_os_text_invalid_unit(hx_os_encoding encoding, size_t left)
{
	size_t unit = (encoding == HX_OS_UTF16LE || encoding == HX_OS_UTF16BE)
		? 2
		: (encoding == HX_OS_UTF32LE || encoding == HX_OS_UTF32BE) ? 4
									   : 1;
	return unit < left ? unit : left;
}

hx_os_status hx_os_text_converter_update(hx_os_text_converter *converter,
	const void *input, size_t bytes, void *output, size_t capacity,
	int final_input, hx_os_text_result *result, hx_os_error *error)
{
	const unsigned char *source = input;
	unsigned char *destination = output;
	hx_os_status status = HX_OS_OK;
	size_t in = 0;
	size_t out = 0;
	if (!converter || !result || (!input && bytes) || (!output && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	memset(result, 0, sizeof(*result));
	result->error_offset = UINT64_MAX;
	if (converter->ended)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (bytes && capacity &&
		((uintptr_t)input <= (uintptr_t)output
				? (uintptr_t)output - (uintptr_t)input < bytes
				: (uintptr_t)input - (uintptr_t)output <
					capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > UINT64_MAX - converter->offset)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	while (in < bytes) {
		uint32_t point = 0;
		size_t consumed = 0;
		size_t produced = 0;
		unsigned char encoded[16];
		int replaced = 0;
		status = hx_os_text_decode(
			converter, source + in, bytes - in, &point, &consumed);
		if (status == HX_OS_NEED_INPUT && !final_input)
			break;
		if (status == HX_OS_NEED_INPUT)
			status = HX_OS_TRUNCATED_INPUT;
		if (status != HX_OS_OK) {
			if (converter->policy == HX_OS_TEXT_STRICT ||
				(status != HX_OS_INVALID_ENCODING &&
					status != HX_OS_TRUNCATED_INPUT))
				break;
			consumed = status == HX_OS_TRUNCATED_INPUT
				? bytes - in
				: hx_os_text_invalid_unit(
					  converter->source, bytes - in);
			point = 0xfffd;
			replaced = 1;
		}
		status =
			hx_os_text_encode(converter, point, encoded, &produced);
		if (status == HX_OS_UNREPRESENTABLE &&
			converter->policy == HX_OS_TEXT_REPLACE) {
			status = hx_os_text_encode(
				converter, 0xfffd, encoded, &produced);
			if (status != HX_OS_OK)
				status = hx_os_text_encode(
					converter, '?', encoded, &produced);
			replaced = 1;
		}
		if (status != HX_OS_OK)
			break;
		if (produced > capacity - out) {
			status = HX_OS_OUTPUT_FULL;
			break;
		}
		memcpy(destination + out, encoded, produced);
		in += consumed;
		out += produced;
		result->replacements += (size_t)replaced;
	}
	result->input_used = in;
	result->output_used = out;
	if (status < 0) {
		result->error_offset = converter->offset + in;
		converter->ended = 1;
	}
	converter->offset += in;
	if (status == HX_OS_OK && final_input)
		converter->ended = 1;
	return hx_os_i_error(error, status, 0, 0);
}

void hx_os_text_converter_reset(hx_os_text_converter *converter)
{
	converter->offset = 0;
	converter->ended = 0;
}

void hx_os_text_converter_destroy(hx_os_text_converter *converter)
{
	if (!converter)
		return;
#if HX_OS_HAVE_ICONV
	if (converter->decoder != (iconv_t)-1)
		(void)iconv_close(converter->decoder);
	if (converter->encoder != (iconv_t)-1)
		(void)iconv_close(converter->encoder);
#endif
	hx_os_memory_free(converter);
}

hx_os_status hx_os_text_convert(hx_os_encoding source, hx_os_encoding target,
	const void *input, size_t bytes, void *output, size_t capacity,
	hx_os_text_result *result, hx_os_error *error)
{
	hx_os_text_converter *converter;
	hx_os_status status;
	if (!result)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	memset(result, 0, sizeof(*result));
	result->error_offset = UINT64_MAX;
	status = hx_os_text_converter_create(
		NULL, source, target, HX_OS_TEXT_STRICT, &converter, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_text_converter_update(
		converter, input, bytes, output, capacity, 1, result, error);
	hx_os_text_converter_destroy(converter);
	return status;
}

hx_os_status hx_os_text_measure(hx_os_encoding source, hx_os_encoding target,
	const void *input, size_t bytes, size_t *required, hx_os_error *error)
{
	hx_os_text_converter *converter;
	const unsigned char *data = input;
	size_t offset = 0;
	size_t total = 0;
	hx_os_status status;
	if (!required || (!input && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*required = 0;
	status = hx_os_text_converter_create(
		NULL, source, target, HX_OS_TEXT_STRICT, &converter, error);
	if (status != HX_OS_OK)
		return status;
	do {
		unsigned char output[256];
		hx_os_text_result result;
		status = hx_os_text_converter_update(converter,
			bytes ? data + offset : NULL, bytes - offset, output,
			sizeof(output), 1, &result, error);
		offset += result.input_used;
		if (!hx_os_i_add_size(total, result.output_used, &total)) {
			status = hx_os_i_error(error, HX_OS_RANGE, 0, 0);
			break;
		}
		if (status == HX_OS_OUTPUT_FULL && !result.input_used) {
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
			break;
		}
	} while (status == HX_OS_OUTPUT_FULL);
	hx_os_text_converter_destroy(converter);
	if (status == HX_OS_OK)
		*required = total;
	return status;
}
