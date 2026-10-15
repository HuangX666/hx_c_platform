#ifndef HX_C_PLATFORM_DIAL_H
#define HX_C_PLATFORM_DIAL_H

#include "hx_os_net.h"
#include "hx_os_coord.h"
#include "hx_os_resolver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_tcp_options {
	uint32_t size, version;
	int nodelay, keepalive, dual_stack;
	uint32_t keepalive_idle_seconds, keepalive_interval_seconds;
	uint32_t keepalive_probe_count;
	uint32_t send_buffer_bytes, receive_buffer_bytes;
} hx_os_tcp_options;

#define HX_OS_TCP_OPTIONS_INIT                                                 \
	{                                                                      \
		sizeof(hx_os_tcp_options), 1, -1, -1, -1, 0, 0, 0, 0, 0        \
	}

/**
 * \brief -1 preserves a switch, 0 disables it, 1 enables it.
 *
 * \details Positive keepalive durations require keepalive=1 and both values;
 * zero durations retain native timing. Probe count=0 retains the native count.
 * Buffer sizes=0 retain their current values. Dual-stack applies only to IPv6.
 * Apply before bind/connect or loop attachment, excluding concurrent callers.
 * Updates are sequential and can partially succeed; original ABI is unchanged.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tcp_options_init(
	hx_os_tcp_options *options, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_apply_tcp_options(
	hx_os_socket *socket, const hx_os_tcp_options *options,
	hx_os_error *error);

typedef struct hx_os_dial_options {
	uint32_t size, version;
	hx_os_tcp_options socket;
	hx_os_cancel_token *cancel;
	hx_os_resolver *resolver;
	uint32_t attempt_delay_ms;
} hx_os_dial_options;

#define HX_OS_DIAL_OPTIONS_INIT                                                \
	{                                                                      \
		sizeof(hx_os_dial_options), 1, HX_OS_TCP_OPTIONS_INIT, NULL,   \
			NULL, 250                                              \
	}

HX_OS_API hx_os_status HX_OS_CALL hx_os_dial_options_init(
	hx_os_dial_options *options, size_t capacity);

/**
 * \brief Connect TCP with at most two simultaneous, staggered candidates.
 *
 * \details Candidates alternate address families, preserving each family's
 * original order. A failure starts the next candidate immediately; otherwise
 * attempt_delay_ms (0..60000) separates starts. All candidates share deadline.
 * While untried addresses remain, a stalled candidate keeps its slot for at
 * most 2 s, shortened to share a finite remaining budget across candidates.
 * That window also caps the start delay. Ready candidates are checked before
 * retirement. Once all addresses start, live candidates keep the remaining
 * overall deadline. Slow candidates may be abandoned to let later ones run.
 * Inputs/cancel are borrowed for this call. No worker is started. Cancellation
 * is checked at most every 50 ms during connection waits, subject to native
 * scheduling. Failure leaves *out NULL and closes all unpublished candidates;
 * success returns an unattached socket owned by the caller. The network must
 * outlive it. Up to 256 candidates, each with a positive port, are accepted.
 * Socket-option failure is reported rather than silently weakening settings.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tcp_dial_addresses_until(
	hx_os_network *network, const hx_os_address *addresses, size_t count,
	const hx_os_dial_options *options, hx_os_deadline deadline,
	hx_os_socket **out, hx_os_error *error);

/**
 * \brief Resolve an ASCII/Punycode host, then use the same candidate dialer.
 *
 * \details options.resolver selects a borrowed c-ares resolver; resolution and
 * connection share deadline/cancel. NULL preserves blocking system DNS, which
 * cannot be forcibly cancelled. No connection starts after deadline/cancel.
 * Resolve externally and use addresses_until for a DNS-free connect budget.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tcp_dial_until(hx_os_network *network,
	const char *host, uint16_t port, const hx_os_dial_options *options,
	hx_os_deadline deadline, hx_os_socket **out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
