#ifndef HX_C_PLATFORM_QUEUE_H
#define HX_C_PLATFORM_QUEUE_H

#include "hx_os_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_message_queue hx_os_message_queue;
typedef struct hx_os_ring_queue hx_os_ring_queue;

enum { HX_OS_QUEUE_SPSC, HX_OS_QUEUE_MPSC, HX_OS_QUEUE_MPMC };

enum { HX_OS_QUEUE_LOCKED, HX_OS_QUEUE_ATOMIC };

HX_OS_API int HX_OS_CALL hx_os_queues_available(void);

/**
 * \brief Fixed-size messages, copied by value.
 *
 * \details All storage is allocated at creation; send/receive do not allocate
 * or invoke callbacks. Pointer fields are borrowed, not cloned or freed. No
 * worker or coroutine runtime is created. Capacity and message_bytes must be
 * nonzero. Supports multiple senders and receivers.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_create(
	hx_os_context *context, size_t message_bytes, size_t capacity,
	hx_os_message_queue **out, hx_os_error *error);

/**
 * \brief Complete messages only; failure preserves input/output and queue
 * contents.
 *
 * \details NOW polls, returning WOULD_BLOCK if progress is unavailable. Finite
 * waits use a monotonic deadline; expiry returns TIMEOUT. INFINITE is
 * permitted. Mutex acquisition/reacquisition may exceed a deadline; this is not
 * a hard timeout. Waiting callers have no FIFO scheduling guarantee.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_send_until(
	hx_os_message_queue *queue, const void *message,
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_receive_until(
	hx_os_message_queue *queue, void *message, hx_os_deadline deadline,
	hx_os_error *error);

/**
 * \brief Batches contain contiguous, fixed-size messages.
 *
 * \details completed is required and is set on every return, including errors.
 * A zero count accepts NULL messages and succeeds even after close. The queue
 * must still be valid. send_many attempts all count messages using one
 * deadline. On TIMEOUT, WOULD_BLOCK, or CLOSED, the completed prefix stays
 * accepted; retry only the remainder. Other senders may interleave while this
 * call waits for space. receive_many waits for at least one message, then
 * returns up to maximum currently available messages with OK; it never waits to
 * fill the array. Only the completed output prefix is modified. Closed queues
 * drain before EOF. No whole-batch atomicity or allocation is implied.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_send_many_until(
	hx_os_message_queue *queue, const void *messages, size_t count,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_receive_many_until(
	hx_os_message_queue *queue, void *messages, size_t maximum,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

/**
 * \brief Optional shared cancellation token.
 *
 * \details NULL preserves the ordinary behavior. One token callback slot is
 * borrowed through the call, including callback retirement. CANCELLED preserves
 * the accepted send prefix. Other callers and the queue remain usable.
 * Cancellation is observed before moving the next chunk; racing completion can
 * still succeed. Zero count remains a no-op.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_send_cancellable_until(
	hx_os_message_queue *queue, const void *message,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_receive_cancellable_until(
	hx_os_message_queue *queue, void *message, hx_os_deadline deadline,
	hx_os_cancel_token *token, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL
hx_os_message_queue_send_many_cancellable_until(hx_os_message_queue *queue,
	const void *messages, size_t count, hx_os_deadline deadline,
	hx_os_cancel_token *token, size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL
hx_os_message_queue_receive_many_cancellable_until(hx_os_message_queue *queue,
	void *messages, size_t maximum, hx_os_deadline deadline,
	hx_os_cancel_token *token, size_t *completed, hx_os_error *error);

/**
 * \brief Idempotent.
 *
 * \details Rejects sends with CLOSED, wakes all waiters, and lets receivers
 * drain accepted messages before EOF. May run concurrently with send/receive.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_close(
	hx_os_message_queue *queue, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_size(
	hx_os_message_queue *queue, size_t *out, hx_os_error *error);

/**
 * \brief First close, join callers, then destroy.
 *
 * \details External exclusion of all calls, including callers entering the
 * mutex, is required. Remaining copied bytes are discarded; pointed-to
 * resources still belong to the application. A detected active wait returns
 * BUSY and retains the owner.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_message_queue_destroy(
	hx_os_message_queue **queue, hx_os_error *error);

/**
 * \brief Fixed-capacity element ring.
 *
 * \details SPSC requires one producer and one consumer; MPSC requires one
 * consumer; MPMC permits multiple producers/consumers. Both backends copy
 * fixed-size bytes; no callbacks or per-operation allocation. LOCKED uses a
 * mutex and permits any nonzero capacity. ATOMIC requires a power of two >= 2
 * and lock-free 64-bit platform atomics; otherwise NOT_SUPPORTED (an invalid
 * capacity returns INVALID). Never silently falls back to locks. MPSC/MPMC
 * share a sequence-tagged reservation algorithm. It uses no mutex but is NOT
 * strictly lock-free: a paused slot owner can delay other operations. Only the
 * SPSC atomic algorithm has the lock-free progress guarantee.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_queue_create(
	hx_os_context *context, size_t message_bytes, size_t capacity, int mode,
	int backend, hx_os_ring_queue **out, hx_os_error *error);

/**
 * \brief WOULD_BLOCK means full/empty, a locked backend's mutex is busy, or the
 * next atomic slot is still reserved.
 *
 * \details Output is unchanged on failure. Multi-party CAS retries can contend;
 * try does not promise a bounded execution time. Atomic counters may wrap, but
 * a call must not remain suspended through a full 2^64 position cycle. No
 * global ordering of overlapping producers beyond reservation order is
 * promised. Do not destroy while any caller is active.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_queue_try_push(
	hx_os_ring_queue *queue, const void *message, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_queue_try_pop(
	hx_os_ring_queue *queue, void *message, hx_os_error *error);

/**
 * \brief Nonblocking batches: a nonzero completed prefix returns OK, even if
 * fewer than requested messages moved.
 *
 * \details WOULD_BLOCK has zero progress. completed is required and set on
 * every return. Zero count is a successful no-op with a valid queue; messages
 * may then be NULL. Unfilled output elements are intact. MPSC/MPMC atomic
 * batches publish individual messages and can interleave.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_queue_try_push_many(
	hx_os_ring_queue *queue, const void *messages, size_t count,
	size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_queue_try_pop_many(
	hx_os_ring_queue *queue, void *messages, size_t maximum,
	size_t *completed, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_ring_queue_capacity(
	const hx_os_ring_queue *queue);

HX_OS_API int HX_OS_CALL hx_os_ring_queue_is_lock_free(
	const hx_os_ring_queue *queue);

/**
 * \brief Requires external quiescence; no implicit close, wait or pointer
 * cleanup.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_ring_queue_destroy(
	hx_os_ring_queue **queue, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
