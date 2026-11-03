#include "local_dumps_private.h"

int hx_os_local_dumps_available(void)
{
#if defined(_WIN32) && HX_OS_ENABLE_LOCAL_DUMPS
	return 1;
#else
	return 0;
#endif
}

hx_os_status hx_os_local_dumps_options_init(
	hx_os_local_dumps_options *options, size_t capacity)
{
	hx_os_local_dumps_options value;

	if (!options || capacity < sizeof(value))
		return HX_OS_INVALID;

	memset(&value, 0, sizeof(value));
	value.size = sizeof(value);
	value.version = 1;
	value.dump_type = HX_OS_LOCAL_DUMPS_FULL;
	value.dump_count = 3;
	memcpy(options, &value, sizeof(value));

	return HX_OS_OK;
}

static hx_os_status hx_os_local_dumps_change(const char *application,
	const hx_os_local_dumps_options *input, int remove, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_LOCAL_DUMPS
	hx_os_local_dumps_options options;

	(void)hx_os_local_dumps_options_init(&options, sizeof(options));
	if (input) {
		if (input->size < sizeof(*input) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		options = *input;
	}

	if ((options.dump_type != HX_OS_LOCAL_DUMPS_MINI &&
		    options.dump_type != HX_OS_LOCAL_DUMPS_FULL) ||
		!options.dump_count ||
		options.registry_view > HX_OS_LOCAL_DUMPS_VIEW64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_local_dumps_change(application, &options, remove, error);
#else
	(void)application;
	(void)input;
	(void)remove;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_local_dumps_enable(const char *application,
	const hx_os_local_dumps_options *options, hx_os_error *error)
{
	return hx_os_local_dumps_change(application, options, 0, error);
}

hx_os_status hx_os_local_dumps_remove(const char *application,
	const hx_os_local_dumps_options *expected, hx_os_error *error)
{
	return hx_os_local_dumps_change(application, expected, 1, error);
}

hx_os_status hx_os_local_dumps_query(const char *application,
	unsigned registry_view, hx_os_local_dumps_info *out, size_t capacity,
	hx_string *folder, hx_os_error *error)
{
#if defined(_WIN32) && HX_OS_ENABLE_LOCAL_DUMPS
	if (!out || !folder || registry_view > HX_OS_LOCAL_DUMPS_VIEW64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity < sizeof(*out))
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	return hx_os_i_local_dumps_query(
		application, registry_view, out, folder, error);
#else
	(void)application;
	(void)registry_view;
	(void)out;
	(void)capacity;
	(void)folder;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
