#ifndef HX_C_PLATFORM_POOL_H
#define HX_C_PLATFORM_POOL_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_thread_pool hx_os_thread_pool;
typedef struct hx_os_task hx_os_task;
typedef hx_os_status(HX_OS_CALL *hx_os_task_fn)(hx_os_task *task, void *user);
typedef void(HX_OS_CALL *hx_os_task_cleanup)(void *user);
typedef hx_os_status(HX_OS_CALL *hx_os_task_prepare_fn)(
	void *factory, void **user);

typedef struct hx_os_thread_pool_options {
	uint32_t size;
	uint32_t version;
	size_t threads;
	size_t queue_capacity;
} hx_os_thread_pool_options;

enum { HX_OS_TASK_QUEUED, HX_OS_TASK_RUNNING, HX_OS_TASK_FINISHED };
enum { HX_OS_POOL_DRAIN, HX_OS_POOL_CANCEL_PENDING };

HX_OS_API void HX_OS_CALL hx_os_thread_pool_options_init(
	hx_os_thread_pool_options *options);
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_pool_create(
	hx_os_context *context, const hx_os_thread_pool_options *options,
	hx_os_thread_pool **out, hx_os_error *error);
/*
 * Success borrows user until cleanup completes. cleanup, if supplied, runs
 * exactly once on a worker, even for queued cancellation. Rejection does
 * not transfer user or call cleanup. Callbacks must not throw or longjmp.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_pool_submit(
	hx_os_thread_pool *pool, hx_os_task_fn function, void *user,
	hx_os_task_cleanup cleanup, hx_os_task **out, hx_os_error *error);
/*
 * Admission precedes prepare. prepare/cleanup run outside the pool mutex.
 * On prepare failure it must release partial resources and set *user=NULL.
 * After successful preparation, cleanup runs exactly once: on a worker for
 * accepted work, or on the submitting thread if close races preparation.
 * Rejection before prepare calls neither callback. factory is call-borrowed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_pool_submit_prepared(
	hx_os_thread_pool *pool, hx_os_task_prepare_fn prepare, void *factory,
	hx_os_task_fn function, hx_os_task_cleanup cleanup, hx_os_task **out,
	hx_os_error *error);
/* Borrowed context. NULL is the immutable default allocator. */
HX_OS_API hx_os_context *HX_OS_CALL hx_os_thread_pool_context(
	const hx_os_thread_pool *pool);
/*
 * Current thread is a worker of this live pool (also during task cleanup).
 * No allocation, lock or wait. Caller excludes pool destruction; invalid or
 * disabled calls leave *inside unchanged. A worker cannot close its own pool
 * or wait for an unfinished task from it (DEADLOCK). Finished task waits are
 * allowed. This query does not detect dependency cycles across other pools.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_pool_inside(
	const hx_os_thread_pool *pool, int *inside);
HX_OS_API int HX_OS_CALL hx_os_task_cancel_requested(hx_os_task *task);
HX_OS_API void HX_OS_CALL hx_os_task_cancel(hx_os_task *task);
HX_OS_API int HX_OS_CALL hx_os_task_state(hx_os_task *task);
/* Wait success includes cleanup. Work status is separate from wait status.
 * An unfinished task's own pool worker receives DEADLOCK, even with NOW.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_task_wait_until(hx_os_task *task,
	hx_os_deadline deadline, hx_os_status *work_status, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_task_retain(hx_os_task *task);
HX_OS_API void HX_OS_CALL hx_os_task_release(hx_os_task *task);
/* Timeout keeps pool and user data alive. No thread is killed or detached. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_pool_close_until(
	hx_os_thread_pool *pool, int mode, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_pool_destroy(
	hx_os_thread_pool **pool, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
