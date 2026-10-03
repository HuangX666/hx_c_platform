#ifndef HX_C_PLATFORM_COROUTINE_H
#define HX_C_PLATFORM_COROUTINE_H

#include "hx_container.h"
#include "hx_os_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_coro_runtime hx_os_coro_runtime;
typedef struct hx_os_coro hx_os_coro;
typedef struct hx_os_coro_channel hx_os_coro_channel;
typedef struct hx_os_coro_stream hx_os_coro_stream;

enum { HX_OS_CORO_STACKFUL, HX_OS_CORO_STACKLESS };
typedef hx_os_status(HX_OS_CALL *hx_os_coro_fn)(
	hx_os_coro *coroutine, void *user, hx_os_error *error);
typedef void(HX_OS_CALL *hx_os_coro_cleanup_fn)(void *user);
typedef hx_os_status(HX_OS_CALL *hx_os_coro_prepare_fn)(
	void *factory, void **user, hx_os_error *error);

typedef struct hx_os_coro_options {
	uint32_t size;
	uint32_t version;
	size_t workers;
	size_t max_coroutines;
	size_t stack_bytes;
	size_t max_stack_bytes;
	size_t max_channels;
	size_t max_channel_bytes;
	size_t max_io_handles;
	size_t max_io_bytes;
} hx_os_coro_options;
#define HX_OS_CORO_OPTIONS_INIT                                                \
	{                                                                      \
		sizeof(hx_os_coro_options), 1, 2, 1024, 128 * 1024,            \
			64 * 1024 * 1024, 128, 16 * 1024 * 1024, 1024,         \
			16 * 1024 * 1024                                       \
	}

/* Explicit creation starts a bounded set of workers. A started coroutine is
 * pinned to its worker. Nested go defaults to the current worker; external
 * submissions distribute across workers. No implicit process-global runtime.
 * Stack reservations include guard pages/native rounding, not only payload.
 * Handles/queued values may retain context allocations after successful close.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_options_init(
	hx_os_coro_options *options, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_runtime_create(
	hx_os_context *context, const hx_os_coro_options *options,
	hx_os_coro_runtime **out, hx_os_error *error);
/* Rejection transfers nothing. Accepted user data lives through cleanup,
 * which runs exactly once, including cancellation before the first call.
 * Stackless callbacks return WOULD_BLOCK only while yielding or waiting;
 * persistent state belongs in user, never in automatic local variables.
 * A stackful callback returns its final status; nested waits suspend its stack.
 * The callback's coroutine/current pointers are borrowed; retain before owning.
 * Lazy native stack creation failures arrive as the task result after
 * admission.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_go(hx_os_coro_runtime *runtime,
	int kind, hx_os_coro_fn function, void *user,
	hx_os_coro_cleanup_fn cleanup, hx_os_coro **out, hx_os_error *error);
/* Admission precedes prepare outside the scheduler lock. Successful prepare
 * rejected by a concurrent close is cleaned on the submitting thread.
 * Failed prepare releases its own partial construction.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_go_prepared(
	hx_os_coro_runtime *runtime, int kind, hx_os_coro_prepare_fn prepare,
	void *factory, hx_os_coro_fn function, hx_os_coro_cleanup_fn cleanup,
	hx_os_coro **out, hx_os_error *error);
HX_OS_API hx_os_context *HX_OS_CALL hx_os_coro_runtime_context(
	const hx_os_coro_runtime *runtime);
HX_OS_API hx_os_coro *HX_OS_CALL hx_os_coro_current(void);
HX_OS_API int HX_OS_CALL hx_os_coro_kind(const hx_os_coro *coroutine);
HX_OS_API int HX_OS_CALL hx_os_coro_wait_pending(void);
/* Set a terminal result, including WOULD_BLOCK as a final error, then return
 * from the callback. Completion is published only after callback and cleanup.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_complete(
	hx_os_status status, const hx_os_error *detail);
HX_OS_API void HX_OS_CALL hx_os_coro_retain(hx_os_coro *coroutine);
HX_OS_API void HX_OS_CALL hx_os_coro_release(hx_os_coro *coroutine);
HX_OS_API void HX_OS_CALL hx_os_coro_cancel(hx_os_coro *coroutine);
HX_OS_API int HX_OS_CALL hx_os_coro_cancel_requested(
	const hx_os_coro *coroutine);
HX_OS_API int HX_OS_CALL hx_os_coro_ready(const hx_os_coro *coroutine);
/* Native-thread callers block; stackful callers suspend. A stackless caller
 * polls the same wait again after WOULD_BLOCK, using persistent arguments.
 * Success includes cleanup; work_error separately reports the final result.
 * Self-wait returns DEADLOCK. Scheduler cleanup and I/O dispatchers cannot
 * block on unfinished tasks. Timeout retains the task and its resources.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_wait_until(hx_os_coro *coroutine,
	hx_os_deadline deadline, hx_os_error *work_error, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_yield(hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_sleep_until(
	hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_sleep_ms(
	uint64_t milliseconds, hx_os_error *error);
/* Closing rejects go, requests cooperative cancellation and wakes waits.
 * No forced unwinding/termination/detach. Retry the same owner after timeout.
 * Native TLS destruction is included in a successful worker join.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_runtime_close_until(
	hx_os_coro_runtime *runtime, hx_os_deadline deadline,
	hx_os_error *error);
/* Drain rejects new go calls but lets accepted work finish. A later close
 * may escalate drain to cancellation. Neither operation reverses closing.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_runtime_drain_until(
	hx_os_coro_runtime *runtime, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_runtime_destroy(
	hx_os_coro_runtime **runtime, hx_os_error *error);

/* Channels belong to one runtime and cross its workers. Zero capacity means
 * rendezvous. Values use the C container's relocatable copy/drop contract;
 * callbacks may not suspend or reenter this channel. receive initializes an
 * external output value and transfers its release obligation. close rejects
 * sends, drains already buffered values, then receive reports EOF. Last release
 * drops remaining buffered values; active operations hold their own reference.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_channel_create(
	hx_os_coro_runtime *runtime, const hx_os_container_options *values,
	size_t capacity, hx_os_coro_channel **out, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_coro_channel_retain(
	hx_os_coro_channel *channel);
HX_OS_API void HX_OS_CALL hx_os_coro_channel_release(
	hx_os_coro_channel *channel);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_channel_close(
	hx_os_coro_channel *channel, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_channel_send_until(
	hx_os_coro_channel *channel, const void *value, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_channel_receive_until(
	hx_os_coro_channel *channel, void *out, hx_os_deadline deadline,
	hx_os_error *error);
/* Wait on several sends/receives; exactly one commits. Case metadata and sent
 * values are copied; receive output storage stays live through completion.
 * The selected index is valid on a selected OK/EOF/CLOSED branch.
 * NOW polls and returns WOULD_BLOCK if no case is ready; other expirations
 * return TIMEOUT. At most 64 cases; ready cases rotate to avoid fixed priority.
 */
enum { HX_OS_CORO_SEND, HX_OS_CORO_RECEIVE };
typedef struct hx_os_coro_case {
	hx_os_coro_channel *channel;
	void *value;
	int operation;
} hx_os_coro_case;
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_select(
	const hx_os_coro_case *cases, size_t count, hx_os_deadline deadline,
	size_t *selected, hx_os_error *error);

/* I/O is marshalled to one bounded dispatcher per runtime. Streams can cross
 * its workers; one read and one write may coexist (a pipe has one direction).
 * Buffers live until completion, even after cancellation requests. Writes
 * attempt all bytes and report partial progress on failure. No syscall hooks.
 * These calls require a current coroutine. Stackless callers repeat the same
 * call after WOULD_BLOCK; arguments and outputs must live in persistent state.
 * Stream release queues native close without waiting for I/O retirement.
 * Runtime close retires native resources; retained handles remain releasable.
 */
HX_OS_API void HX_OS_CALL hx_os_coro_stream_retain(hx_os_coro_stream *stream);
HX_OS_API void HX_OS_CALL hx_os_coro_stream_release(hx_os_coro_stream *stream);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_dial(const hx_os_address *address,
	hx_os_deadline deadline, hx_os_coro_stream **out, hx_os_error *error);
/* reuse_address is explicit; bound receives the actual address/ephemeral port.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_listen(
	const hx_os_address *address, int backlog, int reuse_address,
	hx_os_coro_stream **out, hx_os_address *bound, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_accept(hx_os_coro_stream *listener,
	hx_os_deadline deadline, hx_os_coro_stream **out, hx_os_address *peer,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_read(hx_os_coro_stream *stream,
	void *data, size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_write(hx_os_coro_stream *stream,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_resolve(const char *host,
	uint16_t port, hx_os_deadline deadline, hx_os_address_list **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_pipe_pair(
	hx_os_coro_stream **reader, hx_os_coro_stream **writer,
	hx_os_error *error);

/* Explicit C99 state machines. state is a persistent int initially zero.
 * Put each AWAIT on its own source line; keep cross-wait values in user state,
 * not automatic locals. No nested switch/VLA across these case labels.
 * AWAIT only parks; the caller still handles errors in result explicitly.
 */
#define HX_OS_CORO_BEGIN(state)                                                \
	switch (state) {                                                       \
	case 0:
#define HX_OS_CORO_AWAIT(state, result, expression)                            \
	do {                                                                   \
		(state) = __LINE__;                                            \
	/* fall through */                                                     \
	case __LINE__:                                                         \
		(result) = (expression);                                       \
		if ((result) == HX_OS_WOULD_BLOCK &&                           \
			hx_os_coro_wait_pending())                             \
			return (result);                                       \
	} while (0)
#define HX_OS_CORO_END(state)                                                  \
	}                                                                      \
	(state) = -1

#ifdef __cplusplus
}
#endif
#endif
