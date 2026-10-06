#include "internal.h"
#include "hx_os_util.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <signal.h>
#endif

#if HX_OS_ENABLE_RANDOM
hx_os_status hx_os_i_random_bytes(
	void *buffer, size_t bytes, hx_os_error *error);
#endif

hx_os_status hx_os_random_bytes(void *buffer, size_t bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_RANDOM
	hx_os_status status;

	if (!buffer && bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_i_random_bytes(buffer, bytes, error);
	if (status != HX_OS_OK)
		hx_os_secure_zero(buffer, bytes);

	return status;
#else
	(void)buffer;
	(void)bytes;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_uuid_generate(unsigned char out[16], hx_os_error *error)
{
	unsigned char value[16];
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_random_bytes(value, sizeof(value), error);
	if (status != HX_OS_OK)
		return status;

	value[6] = (unsigned char)((value[6] & 0x0f) | 0x40);
	value[8] = (unsigned char)((value[8] & 0x3f) | 0x80);
	memcpy(out, value, sizeof(value));
	hx_os_secure_zero(value, sizeof(value));

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_uuid_format(const unsigned char uuid[16], char out[37])
{
	static const char digits[] = "0123456789abcdef";
	char text[37];
	size_t i, position = 0;

	if (!uuid || !out)
		return HX_OS_INVALID;

	for (i = 0; i < 16; ++i) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			text[position++] = '-';

		text[position++] = digits[uuid[i] >> 4];
		text[position++] = digits[uuid[i] & 15];
	}

	text[position] = '\0';
	memcpy(out, text, sizeof(text));

	return HX_OS_OK;
}

hx_os_status hx_os_uuid_generate_string(hx_string *out, hx_os_error *error)
{
	unsigned char uuid[16];
	char text[37];
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_uuid_generate(uuid, error);
	if (status != HX_OS_OK)
		return status;

	(void)hx_os_uuid_format(uuid, text);

	return hx_string_assign_cstr(out, text, error);
}

uint32_t hx_os_crc32(uint32_t seed, const void *data, size_t bytes)
{
	/* Four-bit table avoids runtime setup and uses 64 bytes of read-only
	 * data. */
	static const uint32_t table[16] = {0x00000000, 0x1db71064, 0x3b6e20c8,
		0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0,
		0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
	const unsigned char *input = data;
	uint32_t crc = ~seed;
	size_t i;

	for (i = 0; i < bytes; ++i) {
		crc ^= input[i];
		crc = (crc >> 4) ^ table[crc & 15];
		crc = (crc >> 4) ^ table[crc & 15];
	}

	return ~crc;
}

void hx_os_secure_zero(void *buffer, size_t bytes)
{
	if (!bytes)
		return;
#if defined(_WIN32)
	SecureZeroMemory(buffer, bytes);
#elif HX_OS_HAVE_EXPLICIT_BZERO
	explicit_bzero(buffer, bytes);
#else
	/* Volatile applies only to required writes, never to thread ordering.
	 */
	volatile unsigned char *output = buffer;

	while (bytes--)
		*output++ = 0;
#endif
}

void hx_os_debug_break(void)
{
#if defined(_WIN32)
	DebugBreak();
#else
	(void)raise(SIGTRAP);
#endif
}
