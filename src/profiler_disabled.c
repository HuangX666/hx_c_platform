#include "profiler_private.h"

hx_os_status hx_os_profiler_create(
	hx_os_context *context, hx_os_profiler **out, hx_os_error *error)
{
	(void)context;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_create_with_options(hx_os_context *context,
	const hx_os_profiler_options *options, hx_os_profiler **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_start_root(
	hx_os_profiler *profiler, const char *name, hx_os_error *error)
{
	(void)profiler;
	(void)name;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_reach(
	hx_os_profiler *profiler, const char *name, hx_os_error *error)
{
	(void)profiler;
	(void)name;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_leave(hx_os_profiler *profiler, hx_os_error *error)
{
	(void)profiler;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_restart_thread(
	hx_os_profiler *profiler, hx_os_error *error)
{
	(void)profiler;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_profiler_stop(hx_os_profiler *profiler)
{
	(void)profiler;
}

hx_os_status hx_os_profiler_close_until(
	hx_os_profiler **profiler, hx_os_deadline deadline, hx_os_error *error)
{
	(void)profiler;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_destroy(
	hx_os_profiler **profiler, hx_os_error *error)
{
	(void)profiler;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_dump(hx_os_profiler *profiler, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
	(void)profiler;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_dump_string(
	hx_os_profiler *profiler, hx_string *out, hx_os_error *error)
{
	(void)profiler;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_save(
	hx_os_profiler *profiler, const char *path, hx_os_error *error)
{
	(void)profiler;
	(void)path;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
