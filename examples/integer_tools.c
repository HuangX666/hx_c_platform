#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	unsigned char data[10];
	size_t written = 0, consumed = 0;
	uint64_t value = 0;
	hx_string uuid = HX_STRING_INIT;
	hx_os_error error;

	hx_os_status status =
		hx_os_varint_encode(300, data, sizeof(data), &written, &error);
	if (status == HX_OS_OK)
		status = hx_os_varint_decode(
			data, written, &value, &consumed, &error);
	if (status == HX_OS_OK) {
		printf("300 uses %lu bytes; decoded: %lu\n",
			(unsigned long)written, (unsigned long)value);
		printf("ZigZag -1: %lu\n",
			(unsigned long)hx_os_zigzag_encode(-1));
		status = hx_os_uuid_v7_generate_string(&uuid, &error);
	}
	if (status == HX_OS_OK)
		printf("UUID v7: %s\n", hx_string_c_str(&uuid));
	else
		fprintf(stderr, "Integer tools failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&uuid);
	return status == HX_OS_OK ? 0 : 1;
}
