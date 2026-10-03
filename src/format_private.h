#ifndef HX_C_PLATFORM_FORMAT_PRIVATE_H
#define HX_C_PLATFORM_FORMAT_PRIVATE_H
#include "internal.h"
#include "hx_format.h"

typedef struct hx_os_format_state {
	const hx_format_arg *args;
	size_t count;
	size_t next;
	size_t limit;
	int indexing;
} hx_os_format_state;

typedef struct hx_os_format_spec {
	hx_string_view fill;
	size_t width;
	size_t precision;
	char align;
	char sign;
	char type;
	char grouping;
	int alternate;
	int zero;
	int has_precision;
} hx_os_format_spec;

hx_os_status hx_os_i_format_append(hx_string *out, const char *data,
	size_t bytes, size_t limit, hx_os_error *error);
hx_os_status hx_os_i_format_value(hx_string *out, const hx_format_arg *arg,
	const hx_os_format_spec *spec, size_t limit, hx_os_error *error);
hx_os_status hx_os_i_format_print(
	int stream, const char *text, size_t bytes, hx_os_error *error);
#endif
