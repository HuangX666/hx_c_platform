#include "internal.h"
#include "hx_os_util.h"

static int hx_os_uuid_hex(unsigned char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;

	return -1;
}

hx_os_status hx_os_uuid_parse(hx_string_view text, unsigned char out[16],
	hx_os_error *error)
{
	unsigned char value[16];
	size_t i, position = 0;

	if (!out || (!text.data && text.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (text.size != 36)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	for (i = 0; i < sizeof(value); ++i) {
		int high, low;

		if (i == 4 || i == 6 || i == 8 || i == 10) {
			if (text.data[position++] != '-')
				return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		}
		high = hx_os_uuid_hex((unsigned char)text.data[position++]);
		low = hx_os_uuid_hex((unsigned char)text.data[position++]);
		if (high < 0 || low < 0)
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		value[i] = (unsigned char)((high << 4) | low);
	}

	memcpy(out, value, sizeof(value));
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_uuid_v7_generate_at(
	uint64_t milliseconds, unsigned char out[16], hx_os_error *error)
{
	unsigned char value[16];
	unsigned i;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (milliseconds >> 48)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	status = hx_os_random_bytes(value, sizeof(value), error);
	if (status != HX_OS_OK)
		return status;

	for (i = 0; i < 6; ++i)
		value[i] = (unsigned char)(milliseconds >> (40 - 8 * i));
	value[6] = (unsigned char)((value[6] & 15) | 0x70);
	value[8] = (unsigned char)((value[8] & 63) | 0x80);
	memcpy(out, value, sizeof(value));
	hx_os_secure_zero(value, sizeof(value));

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_uuid_v7_generate(
	unsigned char out[16], hx_os_error *error)
{
	int64_t milliseconds;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_wall_time_read(&milliseconds, error);
	if (status != HX_OS_OK)
		return status;
	if (milliseconds < 0)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	return hx_os_uuid_v7_generate_at((uint64_t)milliseconds, out, error);
}

hx_os_status hx_os_uuid_v7_generate_string(
	hx_string *out, hx_os_error *error)
{
	unsigned char uuid[16];
	char text[37];
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_uuid_v7_generate(uuid, error);
	if (status != HX_OS_OK)
		return status;

	(void)hx_os_uuid_format(uuid, text);
	return hx_string_assign_cstr(out, text, error);
}

hx_os_status hx_os_uuid_v7_time(const unsigned char uuid[16],
	uint64_t *milliseconds, hx_os_error *error)
{
	uint64_t value = 0;
	unsigned i;

	if (!uuid || !milliseconds)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if ((uuid[6] >> 4) != 7 || (uuid[8] & 0xc0) != 0x80)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);

	for (i = 0; i < 6; ++i)
		value = (value << 8) | uuid[i];
	*milliseconds = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
