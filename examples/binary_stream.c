#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_stream *stream = NULL;
	uint64_t position;
	uint32_t identifier = 0;
	double temperature = 0;
	hx_os_status status = hx_os_stream_memory(NULL, 64, &stream, NULL);

	if (status == HX_OS_OK)
		status = hx_os_stream_write_u32(
			stream, HX_OS_BIG_ENDIAN, 42, NULL);
	if (status == HX_OS_OK)
		status = hx_os_stream_write_f64(
			stream, HX_OS_BIG_ENDIAN, 23.5, NULL);
	if (status == HX_OS_OK)
		status = hx_os_stream_seek(
			stream, 0, HX_OS_SEEK_BEGIN, &position, NULL);

	if (status == HX_OS_OK)
		status = hx_os_stream_read_u32(
			stream, HX_OS_BIG_ENDIAN, &identifier, NULL);
	if (status == HX_OS_OK)
		status = hx_os_stream_read_f64(
			stream, HX_OS_BIG_ENDIAN, &temperature, NULL);
	if (status == HX_OS_OK)
		printf("device=%u temperature=%.1f\n", (unsigned)identifier,
			temperature);
	else
		fprintf(stderr, "Stream failed: %s\n",
			hx_os_status_name(status));

	hx_os_stream_close(&stream, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
