#ifndef HX_C_PLATFORM_RESOLVER_H
#define HX_C_PLATFORM_RESOLVER_H

#include "hx_os_net.h"
#include "hx_os_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_resolver hx_os_resolver;

typedef struct hx_os_resolver_options {
	uint32_t size, version;
	const char *servers;
	uint32_t timeout_ms, tries;
	int use_hosts_file;
} hx_os_resolver_options;

#define HX_OS_RESOLVER_OPTIONS_INIT                                            \
	{                                                                      \
		sizeof(hx_os_resolver_options), 1, NULL, 1000, 2, 1            \
	}

HX_OS_API int HX_OS_CALL hx_os_resolver_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_resolver_options_init(
	hx_os_resolver_options *options, size_t capacity);

/**
 * \brief Optional c-ares resolver, driven on the caller without worker threads.
 *
 * \details One active query per object; concurrent/reentrant calls return
 * BUSY/DEADLOCK. servers=NULL reads system DNS configuration at creation;
 * otherwise use a numeric server CSV, e.g. "127.0.0.1:5353,[::1]:5353", at most
 * 16 servers/4096 bytes. Input is borrowed only during create. Explicitly set
 * use_hosts_file=0 for DNS-only lookups. Timeout per attempt is 250..60000 ms,
 * tries is 1..8; each query also has its own overall deadline and cancellation.
 * Native cache is disabled; sockets are capped at 64 and results at 256 unique
 * addresses. Native DNS allocations are outside the hx context allocation
 * budget. No event thread, process-global allocator replacement, or NSS plugin
 * emulation. Host/config file reads and scheduling are not hard real-time.
 *
 * The library balances its c-ares initialization leases. External native users
 * must coordinate global init/cleanup/allocator changes, including libcurl
 * instances sharing this c-ares binary. Do not create in DllMain. Recreate the
 * object after a system DNS configuration change; no hidden network watcher.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_resolver_create(hx_os_context *context,
	const hx_os_resolver_options *options, hx_os_resolver **out,
	hx_os_error *error);

/**
 * \brief Resolve ASCII/Punycode names without blocking native getaddrinfo.
 *
 * \details This convenience call drives asynchronous DNS until completion on
 * its calling thread. Cancellation/deadline stops the native query and retires
 * its callback before returning; there is no detached work or borrowed output.
 * Cancellation is checked at most every 50 ms during network waits, subject to
 * OS scheduling. Inputs/cancel are borrowed for the call. Positive port and
 * TCP/UDP protocol are required. *out is NULL on failure; destroy a successful
 * result with hx_os_address_list_destroy. Results outlive the resolver.
 * Native c-ares errors retain HX_OS_ERROR_CARES and their numeric code.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_resolver_resolve_until(
	hx_os_resolver *resolver, const char *host, uint16_t port, int protocol,
	hx_os_deadline deadline, hx_os_cancel_token *cancel,
	hx_os_address_list **out, hx_os_error *error);

/**
 * \brief Exclude new callers before destroy; an active query returns BUSY.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_resolver_destroy(
	hx_os_resolver **resolver, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
