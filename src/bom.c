#include "internal.h"

#if HX_OS_ENABLE_BOM
static const unsigned char hx_os_bom_values[5][4] = {{0xef, 0xbb, 0xbf, 0},
	{0xff, 0xfe, 0, 0}, {0xfe, 0xff, 0, 0}, {0xff, 0xfe, 0, 0},
	{0, 0, 0xfe, 0xff}};
static const size_t hx_os_bom_sizes[5] = {3, 2, 2, 4, 4};
#endif

hx_os_status hx_os_text_bom_detect(const void *data, size_t bytes,
	hx_os_encoding *encoding, size_t *prefix_bytes)
{
#if HX_OS_ENABLE_BOM
	int i;

	if ((!data && bytes) || !encoding || !prefix_bytes)
		return HX_OS_INVALID;

	*prefix_bytes = 0;

	for (i = HX_OS_UTF32BE; i >= HX_OS_UTF8; --i)
		if (bytes >= hx_os_bom_sizes[i] &&
			!memcmp(data, hx_os_bom_values[i],
				hx_os_bom_sizes[i])) {
			*encoding = (hx_os_encoding)i;
			*prefix_bytes = hx_os_bom_sizes[i];

			return HX_OS_OK;
		}

	return HX_OS_NOT_FOUND;
#else
	(void)data;
	(void)bytes;
	(void)encoding;
	(void)prefix_bytes;

	return HX_OS_NOT_SUPPORTED;
#endif
}

hx_os_status hx_os_text_bom_bytes(
	hx_os_encoding encoding, unsigned char out[4], size_t *bytes)
{
#if HX_OS_ENABLE_BOM
	if (!out || !bytes)
		return HX_OS_INVALID;

	if (encoding < HX_OS_UTF8 || encoding > HX_OS_UTF32BE)
		return HX_OS_NOT_SUPPORTED;

	memcpy(out, hx_os_bom_values[encoding], hx_os_bom_sizes[encoding]);
	*bytes = hx_os_bom_sizes[encoding];

	return HX_OS_OK;
#else
	(void)encoding;
	(void)out;
	(void)bytes;

	return HX_OS_NOT_SUPPORTED;
#endif
}
