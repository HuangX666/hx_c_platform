#include "serialization_schema.h"
#include <stdio.h>
#include <string.h>

/* Run with json, yaml, xml or toml. Only the encoded document goes to stdout.
 * INI/ENV/properties cannot represent this array; use a flat schema for them.
 */
int main(int argc, char **argv)
{
	sample samples[] = {{{"C", 1}, 23.5, 1}, {{"%", 1}, 60.0, 2}};
	device source = {{"客厅传感器", sizeof("客厅传感器") - 1}, {samples, 2},
		{-10.0, 80.0}, 1};
	hx_os_codec_result *decoded = NULL;
	hx_os_config_error error;
	hx_string text = HX_STRING_INIT;
	const device *result;
	hx_os_status status;
	int format = HX_OS_CONFIG_JSON, i;

	if (argc > 1) {
		for (i = HX_OS_CONFIG_JSON; i <= HX_OS_CONFIG_PROPERTIES; ++i)
			if (!strcmp(argv[1], hx_os_config_format_name(i)))
				break;

		if (i > HX_OS_CONFIG_PROPERTIES)
			return 2;

		format = i;
	}

	status = hx_os_codec_encode(
		&device_schema, &source, format, &text, NULL, &error);
	if (status == HX_OS_OK)
		status = hx_os_codec_decode(NULL, &device_schema, format,
			text.data, text.size, NULL, &decoded, &error);

	if (status != HX_OS_OK) {
		fprintf(stderr, "%s: %s\n", hx_os_status_name(status),
			error.message);

		hx_string_destroy(&text);

		return 1;
	}

	result = hx_os_codec_data(decoded);
	if (result->samples.count != 2 || !result->enabled) {
		hx_os_codec_release(decoded);
		hx_string_destroy(&text);

		return 1;
	}

	/* All decoded strings and sample elements stay valid until release. */
	(void)fwrite(text.data, 1, text.size, stdout);
	putchar('\n');

	hx_os_codec_release(decoded);
	hx_string_destroy(&text);

	return 0;
}
