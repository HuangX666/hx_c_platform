#ifndef HX_C_PLATFORM_COORD_H
#define HX_C_PLATFORM_COORD_H

#include "hx_os_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_cancel_token hx_os_cancel_token;
typedef struct hx_os_wait_group hx_os_wait_group;
typedef struct hx_os_rwlock hx_os_rwlock;
typedef void(HX_OS_CALL *hx_os_cancel_fn)(void *user);

/**
 * \brief THREAD_EX enables this module.
 *
 * \details Tokens are sticky: create a new token for a new cancellation scope.
 * No worker, polling loop, or forced interruption. max_callbacks=0 uses 64
 * preallocated slots. No allocation on cancellation. The owner must keep the
 * token alive through every operation/registration.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_create(hx_os_context *context,
	size_t max_callbacks, hx_os_cancel_token **out, hx_os_error *error);
HX_OS_API int HX_OS_CALL hx_os_cancel_requested(
	const hx_os_cancel_token *token);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_request(
	hx_os_cancel_token *token, hx_os_error *error);

/**
 * \brief Callbacks run synchronously on the first requesting thread, outside
 * locks.
 *
 * \details They must be short, nonblocking wake/request operations; do not
 * throw or longjmp. Already cancelled subscription returns CANCELLED without
 * calling function or adopting user. Successful subscription borrows user until
 * unsubscribe succeeds. Registration IDs are never reused by a token.
 * Unsubscribe prevents future starts; an in-flight callback is waited for. A
 * callback waiting for itself returns DEADLOCK. Timeout retains the ID and user
 * lifetime obligation. Other request callers may return before dispatch
 * finishes. request is not a wait for task/I/O completion.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_subscribe(
	hx_os_cancel_token *token, hx_os_cancel_fn function, void *user,
	uint64_t *id, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_unsubscribe_until(
	hx_os_cancel_token *token, uint64_t id, hx_os_deadline deadline,
	hx_os_error *error);

/**
 * \brief Success means cancellation was requested.
 *
 * \details A timeout leaves token intact. sleep returns CANCELLED on a
 * cancellation, OK on deadline expiry. Passing NULL to sleep is an ordinary
 * sleep; INFINITE requires a non-NULL token.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_wait_until(
	hx_os_cancel_token *token, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_cancellable_until(
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error);

/**
 * \brief These bindings retain the target until successful unsubscribe.
 *
 * \details Rejected binding retains nothing, including when token was already
 * cancelled. Cancellation follows each target's existing cooperative/native
 * contract.
 */
struct hx_os_task;
struct hx_os_request;
struct hx_os_coro;
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_bind_task(
	hx_os_cancel_token *token, struct hx_os_task *task, uint64_t *id,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_bind_request(
	hx_os_cancel_token *token, struct hx_os_request *request, uint64_t *id,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_bind_coroutine(
	hx_os_cancel_token *token, struct hx_os_coro *coroutine, uint64_t *id,
	hx_os_error *error);

/**
 * \brief External exclusion of new callers required.
 *
 * \details BUSY retains the owner while any subscription, waiter, or request
 * dispatch is active.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cancel_destroy(
	hx_os_cancel_token **token, hx_os_error *error);

/**
 * \brief Go-style task counting, not a thread launcher.
 *
 * \details Add before publishing work. While count>0, tasks may add children
 * before completing themselves. Starting a new round at zero requires all
 * previous waits to have returned. Waiting does not cancel tasks;
 * timeout/cancellation preserves the count. count may not underflow or
 * overflow. All waits share the current round, not a snapshot.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_wait_group_create(
	hx_os_context *context, size_t initial, hx_os_wait_group **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_wait_group_add(
	hx_os_wait_group *group, size_t count, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_wait_group_done(
	hx_os_wait_group *group, size_t count, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_wait_group_wait_until(
	hx_os_wait_group *group, hx_os_deadline deadline,
	hx_os_cancel_token *token, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_wait_group_destroy(
	hx_os_wait_group **group, hx_os_error *error);

/**
 * \brief Writer preference: queued writers block new readers, not strict FIFO
 * or starvation freedom.
 *
 * \details Nonrecursive; read-to-write upgrades are unsupported. Each
 * successful acquisition must be released by that thread in that mode. Read
 * ownership is a caller contract, not a per-thread allocation. These are
 * native-thread waits, not coroutine waits. Mutex reacquisition/cancellation
 * retirement may exceed deadlines; no hard timeout is promised.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_rwlock_create(
	hx_os_context *context, hx_os_rwlock **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_rwlock_read_until(hx_os_rwlock *lock,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_rwlock_write_until(hx_os_rwlock *lock,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_rwlock_read_unlock(
	hx_os_rwlock *lock, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_rwlock_write_unlock(
	hx_os_rwlock *lock, hx_os_error *error);
/**
 * \brief Quiescence is required.
 *
 * \details Held locks/waits return BUSY without destroying.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_rwlock_destroy(
	hx_os_rwlock **lock, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
