#include <hx_codec_macros.h>
#include <stdio.h>

HX_CODEC_STRUCT(connection, HX_CODEC_STRING(host), HX_CODEC_I32(port));

int main(void)
{
	connection source = {{"localhost", 9}, 8080};
	hx_string json = HX_STRING_INIT;
	hx_os_codec_result *owner = NULL;
	hx_os_config_error error;
	hx_os_status status;
	const connection *decoded;
	status = hx_os_codec_encode(&connection_schema, &source,
		HX_OS_CONFIG_JSON, &json, NULL, &error);
	if (status == HX_OS_OK)
		status = hx_os_codec_decode(NULL, &connection_schema,
			HX_OS_CONFIG_JSON, json.data, json.size, NULL, &owner,
			&error);
	if (status != HX_OS_OK) {
		fprintf(stderr, "%s: %s\n", hx_os_status_name(status),
			error.message);
		hx_string_destroy(&json);
		return 1;
	}
	puts(hx_string_c_str(&json));
	decoded = hx_os_codec_data(owner);
	printf("port = %d\n", (int)decoded->port);
	hx_os_codec_release(owner);
	hx_string_destroy(&json);
	return 0;
}
