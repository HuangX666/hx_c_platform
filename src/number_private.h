#ifndef HX_C_PLATFORM_NUMBER_PRIVATE_H
#define HX_C_PLATFORM_NUMBER_PRIVATE_H

#include "internal.h"

int hx_os_i_number_digit(unsigned char character);

hx_os_status hx_os_i_format_double(
	double value, char *buffer, size_t capacity, size_t *required);

int hx_os_i_number_valid(
	const char *text, size_t bytes, int extended, int *real);

hx_os_status hx_os_i_number_integer(
	const char *text, size_t bytes, uint64_t *magnitude, int *negative);

// Setup status is separate from strtod's conversion errno/partial end.
hx_os_status hx_os_i_strtod_c(
	const char *text, char **end, double *value, int *native_error);

hx_os_status hx_os_i_parse_double(hx_os_context *context, const char *text,
	size_t bytes, double *out, hx_os_error *error);
#endif
