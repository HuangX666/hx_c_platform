#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	const char *input = "#define PORT 8080\n"
			    "#define TITLE \"Camera\"\n"
			    "{\n"
			    "  \"title\": TITLE,\n"
			    "#if PORT >= 1024\n"
			    "  \"port\": PORT,\n"
			    "#else\n"
			    "  \"port\": 8080,\n"
			    "#endif\n"
			    "  \"enabled\": true\n"
			    "}\n";
	hx_string output = HX_STRING_INIT;
	hx_os_preprocess_diagnostic diagnostic =
		HX_OS_PREPROCESS_DIAGNOSTIC_INIT;
	hx_os_error error;

	hx_os_status status =
		hx_os_preprocess_text_ex(hx_string_view_from_cstr(input),
			"device.hxconf", NULL, &output, &diagnostic, &error);
	if (status == HX_OS_OK)
		fputs(hx_string_c_str(&output), stdout);
	else {
		fprintf(stderr, "%s:%zu:%zu: %s (%s)\n",
			hx_string_c_str(&diagnostic.source), diagnostic.line,
			diagnostic.column,
			diagnostic.description ? diagnostic.description
					       : "preprocessing failed",
			hx_os_status_name(status));
	}

	hx_string_destroy(&output);
	hx_os_preprocess_diagnostic_destroy(&diagnostic);

	return status == HX_OS_OK ? 0 : 1;
}
