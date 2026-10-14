#include <hx_encoding.h>
#include <stdio.h>

int main(void)
{
	const unsigned char input[] = {0, 1, 254, 255};
	unsigned char decoded[sizeof(input)];
	hx_string text = HX_STRING_INIT;
	size_t bytes = 0;
	hx_os_status status;

	status = hx_os_base64_encode_string(
		input, sizeof(input), 0, &text, NULL);
	if (status == HX_OS_OK) {
		printf("Base64: %s\n", hx_string_c_str(&text));
		status = hx_os_base64_decode(hx_string_c_str(&text),
			hx_string_size_bytes(&text), 0, decoded,
			sizeof(decoded), &bytes, NULL);
	}
	if (status == HX_OS_OK)
		printf("Decoded %zu bytes; last byte: %u\n", bytes,
			(unsigned)decoded[bytes - 1]);
	else
		fprintf(stderr, "Encoding failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
