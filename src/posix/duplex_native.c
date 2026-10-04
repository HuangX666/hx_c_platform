#include "duplex_private.h"

hx_os_status hx_os_i_duplex_pair(hx_os_context *context,
	hx_os_deadline deadline, hx_os_file **first, hx_os_file **second,
	hx_os_error *error)
{
	int pair[2], type = SOCK_STREAM;
	hx_os_file *ends[2];
	hx_os_status status;

	(void)deadline; /* One local native operation, no connection wait. */
	ends[0] = hx_os_i_zero(context, sizeof(*ends[0]));
	ends[1] = hx_os_i_zero(context, sizeof(*ends[1]));
	if (!ends[0] || !ends[1]) {
		hx_os_memory_free(ends[0]);
		hx_os_memory_free(ends[1]);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
#if defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	if (socketpair(AF_UNIX, type, 0, pair)) {
		int code = errno;

		hx_os_memory_free(ends[0]);
		hx_os_memory_free(ends[1]);

		return hx_os_i_errno(error, code);
	}

	status = hx_os_i_socket_configure(pair[0], 0, error);
	if (status == HX_OS_OK)
		status = hx_os_i_socket_configure(pair[1], 0, error);

	if (status != HX_OS_OK) {
		(void)close(pair[0]);
		(void)close(pair[1]);
		hx_os_memory_free(ends[0]);
		hx_os_memory_free(ends[1]);

		return status;
	}

	ends[0]->descriptor = pair[0];
	ends[1]->descriptor = pair[1];
	ends[0]->flags = ends[1]->flags = HX_OS_FILE_READ | HX_OS_FILE_WRITE;
	ends[0]->pipe = ends[1]->pipe = HX_OS_I_FILE_SOCKET;
	*first = ends[0];
	*second = ends[1];

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_duplex_shutdown(
	hx_os_file *file, int direction, hx_os_error *error)
{
	int how = direction == HX_OS_SHUT_READ	? SHUT_RD
		: direction == HX_OS_SHUT_WRITE ? SHUT_WR
						: SHUT_RDWR;

	return hx_os_i_errno(
		error, shutdown(file->descriptor, how) ? errno : 0);
}
