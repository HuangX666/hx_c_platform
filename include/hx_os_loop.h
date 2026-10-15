#ifndef HX_C_PLATFORM_LOOP_H
#define HX_C_PLATFORM_LOOP_H

#include "hx_os_net.h"
#include "hx_os_resolver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_loop hx_os_loop;
typedef struct hx_os_request hx_os_request;

enum {
	HX_OS_OP_POST,
	HX_OS_OP_TIMER,
	HX_OS_OP_CONNECT,
	HX_OS_OP_ACCEPT,
	HX_OS_OP_READ,
	HX_OS_OP_WRITE,
	HX_OS_OP_SEND_TO,
	HX_OS_OP_RECEIVE_FROM,
	HX_OS_OP_RESOLVE
};

enum { HX_OS_SEND_COPY, HX_OS_SEND_BORROW };

typedef struct hx_os_loop_options {
	uint32_t size;
	uint32_t version;
	size_t max_requests;
	size_t max_pending_bytes;
	size_t dns_threads;
	size_t dns_queue_capacity;
} hx_os_loop_options;

typedef struct hx_os_operation {
	uint32_t size;
	uint32_t version;
	int kind;
	int send_mode;
	hx_os_socket *socket;
	void *data;
	size_t bytes;
	hx_os_address address;
	const char *host;
	int protocol;
	hx_os_deadline deadline;
} hx_os_operation;

typedef struct hx_os_completion {
	hx_os_error error;
	size_t bytes;
	hx_os_address peer;
} hx_os_completion;

typedef void(HX_OS_CALL *hx_os_completion_fn)(
	hx_os_request *request, const hx_os_completion *completion, void *user);

HX_OS_API void HX_OS_CALL hx_os_loop_options_init(hx_os_loop_options *options);

HX_OS_API void HX_OS_CALL hx_os_operation_init(
	hx_os_operation *operation, int kind);

/**
 * \brief Creator thread owns polling, I/O submission, socket close and
 * destruction.
 *
 * \details That thread must remain alive until loop destruction. POST
 * submission and request cancel/retain/release/wait are thread-safe. One
 * read-side and one write-side operation may coexist per socket. CONNECT
 * occupies both directions. Socket ownership lasts until close.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_create(hx_os_network *network,
	const hx_os_loop_options *options, hx_os_loop **out,
	hx_os_error *error);

/**
 * \brief Context-only loop for posts, timers and local I/O.
 *
 * \details No network/DNS startup. Network operations require the original
 * network-backed factory.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_create_local(
	hx_os_context *context, const hx_os_loop_options *options,
	hx_os_loop **out, hx_os_error *error);

/**
 * \brief Accepted operations call completion once on the polling thread, never
 * inline in submit.
 *
 * \details Rejection transfers nothing and calls no callback. Writes copy bytes
 * by default; BORROW and read buffers live until callback. Addresses and DNS
 * names are copied. user is borrowed until callback ends. The deadline requests
 * cancellation; native retirement can finish later. A DNS job uses bounded
 * workers and cannot forcibly interrupt getaddrinfo.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_submit(hx_os_loop *loop,
	const hx_os_operation *operation, hx_os_completion_fn completion,
	void *user, hx_os_request **out, hx_os_error *error);

/**
 * \brief Submit cancellable c-ares DNS without assembling an operation.
 * \details NULL options selects resolver defaults. Host and options are copied.
 * Uses this loop's bounded DNS pool and request/byte budgets; no extra pool.
 * Deadline and request_cancel stop DNS network waiting. Completion runs once
 * on the polling thread after native callbacks retire. Rejection calls none.
 * Take the result with request_take_addresses, then release the request.
 * Disabled c-ares returns NOT_SUPPORTED; no fallback to blocking system DNS.
 * Existing OP_RESOLVE submissions keep the system-resolver contract.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_resolve(hx_os_loop *loop,
	const char *host, uint16_t port, int protocol,
	const hx_os_resolver_options *options, hx_os_deadline deadline,
	hx_os_completion_fn completion, void *user, hx_os_request **out,
	hx_os_error *error);

/**
 * \brief Poll performs one batch.
 *
 * \details run drains until idle. Neither starts a thread.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_poll_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_run_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_loop_pending(hx_os_loop *loop);

HX_OS_API const char *HX_OS_CALL hx_os_loop_backend(void);

HX_OS_API void HX_OS_CALL hx_os_request_cancel(hx_os_request *request);

HX_OS_API void HX_OS_CALL hx_os_request_retain(hx_os_request *request);

HX_OS_API void HX_OS_CALL hx_os_request_release(hx_os_request *request);

/**
 * \brief Success includes callback return; waiting does not drive the loop.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_request_wait_until(
	hx_os_request *request, hx_os_deadline deadline,
	hx_os_completion *completion, hx_os_error *error);

/**
 * \brief Take functions transfer ownership once, in the callback or after wait.
 *
 * \details Untaken results are released with the request. Returned sockets have
 * not yet been attached to a loop. Cross-thread use needs external exclusion.
 */
HX_OS_API hx_os_socket *HX_OS_CALL hx_os_request_take_socket(
	hx_os_request *request);

HX_OS_API hx_os_address_list *HX_OS_CALL hx_os_request_take_addresses(
	hx_os_request *request);

/**
 * \brief Close rejects new submissions and cancels outstanding operations.
 *
 * \details Timeout retains all live objects and can be retried. Close may
 * invoke callbacks. Close all attached sockets after their requests retire,
 * then destroy loop.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_close_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_destroy(
	hx_os_loop **loop, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
