#ifndef HX_C_PLATFORM_THREAD_H
#define HX_C_PLATFORM_THREAD_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_mutex hx_os_mutex;
typedef struct hx_os_condition hx_os_condition;
typedef struct hx_os_event hx_os_event;
typedef struct hx_os_thread hx_os_thread;
typedef struct hx_os_tls_key hx_os_tls_key;
typedef void *(HX_OS_CALL *hx_os_thread_fn)(void *user);
typedef void(HX_OS_CALL *hx_os_tls_destructor)(void *value, void *user);

HX_OS_API hx_os_status HX_OS_CALL hx_os_mutex_create(hx_os_context *context,
	int recursive, hx_os_mutex **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_mutex_lock(
	hx_os_mutex *mutex, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_mutex_try_lock(
	hx_os_mutex *mutex, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_mutex_unlock(
	hx_os_mutex *mutex, hx_os_error *error);
/* Destruction requires external exclusion of users, including waiters. */
HX_OS_API void HX_OS_CALL hx_os_mutex_destroy(hx_os_mutex *mutex);

HX_OS_API hx_os_status HX_OS_CALL hx_os_condition_create(
	hx_os_context *context, hx_os_condition **out, hx_os_error *error);
/*
 * Wait with a nonrecursive mutex locked once. Always recheck a predicate.
 * Returns with the mutex locked, including timeout. Reacquiring it can
 * exceed the deadline. Notifications are not retained.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_condition_wait_until(
	hx_os_condition *condition, hx_os_mutex *mutex, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_condition_notify_one(
	hx_os_condition *condition);
HX_OS_API void HX_OS_CALL hx_os_condition_notify_all(
	hx_os_condition *condition);
HX_OS_API void HX_OS_CALL hx_os_condition_destroy(hx_os_condition *condition);

/* Auto-reset retains one signal; manual-reset stays set until reset. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_event_create(hx_os_context *context,
	int manual_reset, int signaled, hx_os_event **out, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_event_set(hx_os_event *event);
HX_OS_API void HX_OS_CALL hx_os_event_reset(hx_os_event *event);
HX_OS_API hx_os_status HX_OS_CALL hx_os_event_wait_until(
	hx_os_event *event, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_event_destroy(hx_os_event *event);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_create(hx_os_context *context,
	hx_os_thread_fn function, void *user, hx_os_thread **out,
	hx_os_error *error);
/*
 * One joiner at a time. Timeout leaves thread/user alive. Success includes
 * native TLS cleanup and may be repeated. Returned result is caller-owned.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_join_until(hx_os_thread *thread,
	hx_os_deadline deadline, void **result, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_destroy(
	hx_os_thread **thread, hx_os_error *error);
HX_OS_API int HX_OS_CALL hx_os_thread_is_current(hx_os_thread *thread);
/* Native identifiers may be reused after thread exit; not globally unique. */
HX_OS_API uint64_t HX_OS_CALL hx_os_thread_id(void);

/*
 * Keys require quiescence before destruction. A key with live values returns
 * BUSY. Replacing a value does not destroy the old value. The destructor
 * runs on normal thread exit, must not reset this key, and must not throw.
 * Windows uses FLS; hosts switching native fibers observe fiber-local values.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_key_create(hx_os_context *context,
	hx_os_tls_destructor destructor, void *user, hx_os_tls_key **out,
	hx_os_error *error);
HX_OS_API void *HX_OS_CALL hx_os_tls_get(hx_os_tls_key *key);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_set(
	hx_os_tls_key *key, void *value, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_tls_clear(hx_os_tls_key *key);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_key_destroy(
	hx_os_tls_key **key, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
