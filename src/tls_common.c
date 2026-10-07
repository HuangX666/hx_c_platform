#include "tls_private.h"

int hx_os_tls_available(void)
{
	return HX_OS_HAVE_TLS;
}

hx_os_status hx_os_tls_options_init(hx_os_tls_options *options, size_t capacity)
{
	const hx_os_tls_options defaults = HX_OS_TLS_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_tls_connect_until(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *options,
	hx_os_deadline deadline, hx_os_tls **out, hx_os_error *error)
{
	hx_os_status status = hx_os_tls_start_connect(
		context, host, port, options, out, error);

	if (status == HX_OS_OK)
		status = hx_os_tls_wait_ready_until(*out, deadline, error);

	return status;
}

hx_os_status hx_os_tls_listen_until(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *options,
	hx_os_deadline deadline, hx_os_tls **out, hx_os_error *error)
{
	hx_os_status status = hx_os_tls_start_listen(
		context, host, port, options, out, error);

	if (status == HX_OS_OK)
		status = hx_os_tls_wait_ready_until(*out, deadline, error);

	return status;
}

hx_os_status hx_os_tls_connect(const char *host, uint16_t port,
	const hx_os_tls_options *options, hx_os_tls **out, hx_os_error *error)
{
	return hx_os_tls_connect_until(NULL, host, port, options,
		hx_os_deadline_after_ms(5000), out, error);
}

hx_os_status hx_os_tls_listen(const char *host, uint16_t port,
	const hx_os_tls_options *options, hx_os_tls **out, hx_os_error *error)
{
	return hx_os_tls_listen_until(NULL, host, port, options,
		hx_os_deadline_after_ms(5000), out, error);
}
