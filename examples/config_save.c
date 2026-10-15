#include <hx_os.h>
#include <stdio.h>
#include "example_arguments.h"
#include <string.h>

static int hx_example_main(int argc, const char *const *argv)
{
	const char *source = "{\"name\":\"示例\",\"count\":9007199254740993}";
	hx_os_config_document *document = NULL;
	hx_os_config_error parse_error;
	hx_os_error error;
	hx_string text = HX_STRING_INIT;
	hx_os_status status;

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [output.json]\n", argv[0]);

		return 1;
	}

	status = hx_os_config_parse(NULL, HX_OS_CONFIG_JSON,
		source, strlen(source), NULL, &document, &parse_error);
	if (status == HX_OS_OK)
		status = argc > 1
			? hx_os_config_save(document, HX_OS_CONFIG_JSON,
				  argv[1], NULL, &error)
			: hx_os_config_serialize(document, HX_OS_CONFIG_JSON,
				  &text, NULL, &error);

	if (status == HX_OS_OK && argc == 1)
		puts(hx_string_c_str(&text));
	else if (status == HX_OS_OK)
		printf("Saved %s\n", argv[1]);
	else
		fprintf(stderr, "Save configuration: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&text);
	hx_os_config_release(document);

	return status != HX_OS_OK;
}
