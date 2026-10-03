#ifndef HX_C_PLATFORM_KCP_H
#define HX_C_PLATFORM_KCP_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_kcp hx_os_kcp;
typedef hx_os_status(HX_OS_CALL *hx_os_kcp_output_fn)(
	const void *packet, size_t bytes, void *user, hx_os_error *error);

typedef struct hx_os_kcp_options {
	uint32_t size;
	uint32_t version;
	size_t max_message_bytes;
	uint32_t max_send_segments;
	uint32_t receive_window;
	uint32_t mtu;
	uint32_t interval_ms;
} hx_os_kcp_options;

HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_options_init(
	hx_os_kcp_options *options, size_t capacity);
/* Pure protocol engine: no socket or thread is created. Each instance owns
 * a fixed allocation reserve, obtained from its context during creation.
 * All calls, including destruction, require external serialization. Output
 * runs inline, borrows packet bytes, must not block, and must not reenter
 * this instance. user is borrowed until destruction. No global allocator
 * configuration is changed. conv identifies a KCP conversation, not an
 * authenticated or restart-safe application session.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_create(hx_os_context *context,
	uint32_t conv, const hx_os_kcp_options *options,
	hx_os_kcp_output_fn output, void *user, hx_os_kcp **out,
	hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_kcp_destroy(hx_os_kcp *kcp);
/* Copies one whole message. LIMIT leaves the send queue unchanged.
 * Acceptance does not mean transmission, acknowledgement or execution.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_send(
	hx_os_kcp *kcp, const void *data, size_t bytes, hx_os_error *error);
/* Input is a complete KCP packet, with no application envelope. Malformed
 * input is rejected before the upstream parser is called. A receive reserve
 * shortage reports WOULD_BLOCK: drop that packet and let its peer retry.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_input(
	hx_os_kcp *kcp, const void *packet, size_t bytes, hx_os_error *error);
/* Whole-message receive. WOULD_BLOCK means no complete message. OUTPUT_FULL
 * sets *bytes to the required capacity and consumes nothing. On success,
 * *bytes is the actual length, including zero for an empty message.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_receive(hx_os_kcp *kcp, void *data,
	size_t capacity, size_t *bytes, hx_os_error *error);
/* now_ms is a nondecreasing monotonic millisecond count. The wrapper adapts
 * its low 32 bits to KCP. Drive an active engine at least once per 2^31 ms.
 * An output failure is reported; retained data can be retransmitted by a
 * later update. The caller supplies a finite session/request deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_update(
	hx_os_kcp *kcp, uint64_t now_ms, hx_os_error *error);
/* Flush after update, including protocol ACKs during orderly shutdown. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_flush(
	hx_os_kcp *kcp, hx_os_error *error);
HX_OS_API uint32_t HX_OS_CALL hx_os_kcp_next_update_ms(
	const hx_os_kcp *kcp, uint64_t now_ms);
HX_OS_API size_t HX_OS_CALL hx_os_kcp_pending_segments(const hx_os_kcp *kcp);
HX_OS_API size_t HX_OS_CALL hx_os_kcp_reserved_bytes(const hx_os_kcp *kcp);

#ifdef __cplusplus
}
#endif
#endif
