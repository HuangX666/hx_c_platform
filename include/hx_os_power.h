#ifndef HX_C_PLATFORM_POWER_H
#define HX_C_PLATFORM_POWER_H

#include "hx_os_base.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_sleep_inhibit hx_os_sleep_inhibit;
typedef struct hx_os_performance_token hx_os_performance_token;

/* Explicit active request, with a copied UTF-8 reason (1..4096 bytes).
 * Each object owns only its own native request; multiple consumers compose.
 * Caller serializes operations; transfer between threads is allowed after
 * synchronization. No hidden thread, timer, process-global configuration or
 * frequency guarantee. OS/user power policies can override these requests.
 * Linux desktop optionally uses the installed libsystemd/libelogind sd-bus
 * runtime and a local session bus. It tries freedesktop PowerManagement then
 * GNOME SessionManager (suspend flag only, not idle/lock inhibition). Missing
 * runtime/service returns NOT_SUPPORTED. Method calls share a 1 s monotonic
 * budget; native setup has no hard deadline. Activation does not autostart a
 * service or request interactive authorization. Each active object holds a
 * private bus connection; deactivation/destruction closes it without waiting
 * for a release reply. The desktop observes disconnect and releases the lease.
 * No global system power configuration is changed. Do not mutate the process
 * environment concurrently with creation/reactivation. Native library mapping
 * may remain loaded to protect its internal TLS through plugin unload.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_inhibit_create(
	hx_os_context *context, const char *reason, hx_os_sleep_inhibit **out,
	hx_os_error *error);

/* Idempotent requested state; no native reference is added by repeated true.
 * active() records this state, not a live desktop-service health check. A bus
 * or desktop restart can lose a Linux lease; deactivate then activate again
 * (or recreate) to request a new one. No hidden reconnect worker is started.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_inhibit_set_active(
	hx_os_sleep_inhibit *inhibit, int active, hx_os_error *error);

HX_OS_API int HX_OS_CALL hx_os_sleep_inhibit_active(
	const hx_os_sleep_inhibit *inhibit);

/* Release consumes the object even when native cleanup reports an error.
 * No outstanding user callback or background operation borrows its storage.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_inhibit_destroy(
	hx_os_sleep_inhibit **inhibit, hx_os_error *error);

/* Windows execution-required request (Win8+, Win7 returns NOT_SUPPORTED);
 * Apple user-initiated/latency-critical activity, confined to native code.
 * Duration is caller-controlled; no CPU clock or scheduling guarantee.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_performance_request(
	hx_os_context *context, const char *reason,
	hx_os_performance_token **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_performance_release(
	hx_os_performance_token **token, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
