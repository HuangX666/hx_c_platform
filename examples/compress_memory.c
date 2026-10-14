#include <hx_os.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	const char json[] = "{\"device\":\"door-1\",\"records\":[1,2,3]}";
	hx_bytes compressed = HX_BYTES_INIT, restored = HX_BYTES_INIT;
	hx_os_status status;

	status = hx_os_compress(HX_OS_COMPRESS_ZSTD,
		(hx_os_bytes_view){json, sizeof(json) - 1}, NULL, &compressed,
		NULL);
	if (status == HX_OS_OK)
		status = hx_os_decompress(HX_OS_COMPRESS_ZSTD,
			hx_bytes_view(&compressed), NULL, &restored, NULL);
	if (status == HX_OS_OK) {
		printf("%zu bytes -> %zu bytes -> %zu bytes\n",
			sizeof(json) - 1, compressed.size, restored.size);
		printf("%.*s\n", (int)restored.size,
			(const char *)restored.data);
	}
	else
		fprintf(stderr, "Compression: %s\n", hx_os_status_name(status));

	hx_bytes_destroy(&restored);
	hx_bytes_destroy(&compressed);
	return status == HX_OS_OK ? 0 : 1;
}
