#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int measure(const char *name, hx_string_view input, int operation,
	size_t iterations)
{
	hx_string output = HX_STRING_INIT;
	hx_os_config_document *document = NULL;
	hx_format_arg argument = hx_fmt_view(input);
	hx_os_status status = HX_OS_OK;
	uint64_t begin = 0, end;
	size_t i, offset = 0;
	char *json = NULL;

	if (operation == 2) {
		// These fixtures contain no JSON quotes or control characters.
		json = malloc(input.size + 2);
		if (!json)
			return 1;
		json[0] = '"';
		memcpy(json + 1, input.data, input.size);
		json[input.size + 1] = '"';
		status = hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, json,
			input.size + 2, NULL, &document, NULL);
	}

	// Setup and the first warm-up are outside the measured interval.
	for (i = 0; i <= iterations && status == HX_OS_OK; ++i) {
		if (i == 1)
			begin = hx_os_monotonic_ns();
		if (operation == 0)
			status = hx_os_text_validate_utf8(
				input.data, input.size, &offset);
		else if (operation == 1)
			status = hx_format(&output,
				hx_string_view_from_cstr("{:?}"), &argument, 1,
				NULL);
		else
			status = hx_os_config_serialize(document,
				HX_OS_CONFIG_JSON, &output, NULL, NULL);
	}
	end = hx_os_monotonic_ns();
	if (status == HX_OS_OK &&
		(operation ? output.size != input.size + 2
			   : offset != input.size))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK)
		printf("case=%s bytes=%zu iterations=%zu ns_per_op=%.2f\n",
			name, input.size, iterations,
			(double)(end - begin) / iterations);
	else
		fprintf(stderr, "%s: %s\n", name, hx_os_status_name(status));

	hx_os_config_release(document);
	hx_string_destroy(&output);
	free(json);

	return status != HX_OS_OK;
}

int main(void)
{
	const size_t bytes = 1024 * 1024;
	char *data = malloc(bytes);
	hx_string_view input;
	uint64_t capabilities = hx_os_capabilities();
	size_t i;
	int failed = 0, mixed;

	if (!data)
		return 1;
	memset(data, 'a', bytes);
	input.data = data;
	input.size = 1;
	failed |= measure("utf8_1_ascii", input, 0, 200000);
	input.size = 32;
	failed |= measure("utf8_32_ascii", input, 0, 200000);
	failed |= measure("utf8_3_chinese",
		hx_string_view_from_cstr("\xe4\xb8\xad"), 0, 200000);
	for (mixed = 0; mixed < 2; ++mixed) {
		if (mixed)
			for (i = 16; i + 6 < bytes; i += 64)
				memcpy(data + i, "\xe4\xb8\xad\xe6\x96\x87", 6);
		input.size = bytes;
		failed |= measure(mixed ? "utf8_1m_mixed" : "utf8_1m_ascii",
			input, 0, 64);
		input.size = bytes / 4;
		if (capabilities & HX_OS_CAP_FORMAT)
			failed |= measure(
				mixed ? "quote_256k_mixed" : "quote_256k_ascii",
				input, 1, 16);
		if (capabilities & HX_OS_CAP_CONFIG_SERIALIZE)
			failed |= measure(
				mixed ? "json_256k_mixed" : "json_256k_ascii",
				input, 2, 32);
	}
	memset(data, 'a', bytes);
	for (i = 0; i + 3 <= bytes; i += 3)
		memcpy(data + i, "\xe4\xb8\xad", 3);
	input.size = bytes;
	failed |= measure("utf8_1m_chinese", input, 0, 64);
	free(data);

	return failed ? 1 : 0;
}
