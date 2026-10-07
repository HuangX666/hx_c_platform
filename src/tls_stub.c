#include "tls_private.h"

#if !HX_OS_HAVE_TLS
hx_os_status hx_os_tls_start_connect(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error)
{
	(void)context;
	(void)host;
	(void)port;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_start_listen(hx_os_context *context,
	const char *numeric_host, uint16_t port,
	const hx_os_tls_options *options, hx_os_tls **out, hx_os_error *error)
{
	(void)context;
	(void)numeric_host;
	(void)port;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_wait_ready_until(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error)
{
	(void)tls;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_local_address(
	hx_os_tls *tls, hx_os_address *out, hx_os_error *error)
{
	(void)tls;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_accept_until(hx_os_tls *tls, hx_os_deadline deadline,
	uint64_t *peer, hx_os_error *error)
{
	(void)tls;
	(void)deadline;
	(void)peer;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_send_until(hx_os_tls *tls, uint64_t peer,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *accepted, hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)data;
	(void)bytes;
	(void)deadline;
	if (accepted)
		*accepted = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_flush_until(hx_os_tls *tls, uint64_t peer,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_receive_until(hx_os_tls *tls, uint64_t peer, void *data,
	size_t capacity, hx_os_deadline deadline, size_t *received,
	hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)data;
	(void)capacity;
	(void)deadline;
	if (received)
		*received = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_receive_exact_until(hx_os_tls *tls, uint64_t peer,
	void *data, size_t bytes, hx_os_deadline deadline, size_t *received,
	hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)data;
	(void)bytes;
	(void)deadline;
	if (received)
		*received = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_cancel_waits(hx_os_tls *tls, hx_os_error *error)
{
	(void)tls;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_peer_close_until(hx_os_tls *tls, uint64_t peer,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_peer_abort_until(hx_os_tls *tls, uint64_t peer,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_close_until(
	hx_os_tls **tls, hx_os_deadline deadline, hx_os_error *error)
{
	(void)tls;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_abort_until(
	hx_os_tls **tls, hx_os_deadline deadline, hx_os_error *error)
{
	(void)tls;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_tls_get_peer_info(hx_os_tls *tls, uint64_t peer,
	hx_os_tls_peer_info *out, size_t capacity, hx_os_error *error)
{
	(void)tls;
	(void)peer;
	(void)out;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

#endif
