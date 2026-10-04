#include "duplex_private.h"

hx_os_status hx_os_pipe_pair_socket_until(hx_os_context *context,
	hx_os_deadline deadline, hx_os_file **first, hx_os_file **second,
	hx_os_error *error)
{
	if (!first || !second || first == second)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*first = NULL;
	*second = NULL;
#if HX_OS_ENABLE_DUPLEX_PIPE
	return hx_os_i_duplex_pair(context, deadline, first, second, error);
#else
	(void)context;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_pipe_pair_socket(hx_os_context *context, hx_os_file **first,
	hx_os_file **second, hx_os_error *error)
{
	return hx_os_pipe_pair_socket_until(
		context, hx_os_deadline_after_ms(5000), first, second, error);
}

hx_os_status hx_os_pipe_socket_shutdown(
	hx_os_file *file, int direction, hx_os_error *error)
{
#if HX_OS_ENABLE_DUPLEX_PIPE
	if (!file || file->pipe != HX_OS_I_FILE_SOCKET ||
		direction < HX_OS_SHUT_READ || direction > HX_OS_SHUT_BOTH)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_duplex_shutdown(file, direction, error);
#else
	(void)file;
	(void)direction;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
