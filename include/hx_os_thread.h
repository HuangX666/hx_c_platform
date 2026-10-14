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
typedef struct hx_os_semaphore hx_os_semaphore;
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

/**
 * \brief Destruction requires external exclusion of users, including waiters.
 */
HX_OS_API void HX_OS_CALL hx_os_mutex_destroy(hx_os_mutex *mutex);

HX_OS_API hx_os_status HX_OS_CALL hx_os_condition_create(
	hx_os_context *context, hx_os_condition **out, hx_os_error *error);

/**
 * \brief Wait with a nonrecursive mutex locked once.
 *
 * \details Always recheck a predicate. Returns with the mutex locked, including
 * timeout. Reacquiring it can exceed the deadline. Notifications are not
 * retained.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_condition_wait_until(
	hx_os_condition *condition, hx_os_mutex *mutex, hx_os_deadline deadline,
	hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_condition_notify_one(
	hx_os_condition *condition);

HX_OS_API void HX_OS_CALL hx_os_condition_notify_all(
	hx_os_condition *condition);

HX_OS_API void HX_OS_CALL hx_os_condition_destroy(hx_os_condition *condition);

/**
 * \brief Auto-reset retains one signal; manual-reset stays set until reset.
 */
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

typedef struct hx_os_thread_options {
	uint32_t size;
	uint32_t version;
	size_t stack_size;
} hx_os_thread_options;
#define HX_OS_THREAD_OPTIONS_INIT                                              \
	{                                                                      \
		sizeof(hx_os_thread_options), 1, 0                             \
	}
/**
 * \brief Only the known prefix is initialized; smaller storage returns INVALID.
 *
 * \details stack_size is a reservation in bytes; 0 keeps the native default.
 * The OS may round up. Invalid/unsupported sizes fail without a thread.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_options_init(
	hx_os_thread_options *options, size_t capacity);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_create_with_options(
	hx_os_context *context, const hx_os_thread_options *options,
	hx_os_thread_fn function, void *user, hx_os_thread **out,
	hx_os_error *error);

/**
 * \brief Names are borrowed UTF-8 and copied by the native call.
 *
 * \details Linux/macOS names truncate to complete codepoints within 15/63
 * bytes. Windows 7 returns NOT_SUPPORTED. macOS can only name the current
 * thread. The owner must exclude concurrent join/destroy when using a thread
 * object.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_set_name(
	hx_os_thread *thread, const char *name, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_set_current_name(
	const char *name, hx_os_error *error);
enum {
	HX_OS_THREAD_PRIORITY_IDLE,
	HX_OS_THREAD_PRIORITY_NORMAL,
	HX_OS_THREAD_PRIORITY_TIME_CRITICAL
};

/**
 * \brief Scheduling requests, not hard real-time guarantees.
 *
 * \details Linux uses SCHED_IDLE / SCHED_OTHER / maximum SCHED_RR; privilege
 * failures are preserved. Windows priorities remain relative to the process
 * class. Exclude join/destroy and concurrent scheduling changes on the target.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_set_priority(
	hx_os_thread *thread, int level, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_set_current_priority(
	int level, hx_os_error *error);

/**
 * \brief Nonzero mask: Linux CPU IDs 0..63; Windows current processor group,
 * native pointer-width bits (higher bits return NOT_SUPPORTED on x86).
 *
 * \details Apple cannot provide strict binding and returns NOT_SUPPORTED. Linux
 * verifies readback; mismatch returns NOT_SUPPORTED and attempts to restore the
 * old mask. Restoration failure is error.cause_code. Failure can have changed
 * affinity; topology/cpuset changes can occur after verification. This is not a
 * reservation of CPUs.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_set_affinity(
	hx_os_thread *thread, uint64_t mask, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_set_current_affinity(
	uint64_t mask, hx_os_error *error);

/**
 * \brief One joiner at a time.
 *
 * \details Timeout leaves thread/user alive. Success includes native TLS
 * cleanup and may be repeated. Returned result is caller-owned.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_join_until(hx_os_thread *thread,
	hx_os_deadline deadline, void **result, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_thread_destroy(
	hx_os_thread **thread, hx_os_error *error);

HX_OS_API int HX_OS_CALL hx_os_thread_is_current(hx_os_thread *thread);

/**
 * \brief Native identifiers may be reused after thread exit; not globally
 * unique.
 */
HX_OS_API uint64_t HX_OS_CALL hx_os_thread_id(void);

/**
 * \brief Bounded counting semaphore, 0 <= initial <= maximum and maximum > 0.
 *
 * \details post(count>0) is all-or-nothing; exceeding maximum returns LIMIT.
 * wait consumes one token, including at NOW if immediately available. No token
 * at NOW returns TIMEOUT. INFINITE is allowed. Timeout retains the object for
 * retry. No FIFO guarantee. Destruction requires external exclusion of all
 * operations/waiters, like event destruction.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_semaphore_create(hx_os_context *context,
	uint32_t initial, uint32_t maximum, hx_os_semaphore **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_semaphore_post(
	hx_os_semaphore *semaphore, uint32_t count, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_semaphore_wait_until(
	hx_os_semaphore *semaphore, hx_os_deadline deadline,
	hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_semaphore_destroy(hx_os_semaphore *semaphore);

/**
 * \brief Keys require quiescence before destruction.
 *
 * \details A key with live values returns BUSY. Replacing a value does not
 * destroy the old value. The destructor runs on normal thread exit, must not
 * reset this key, and must not throw. Windows uses FLS; hosts switching native
 * fibers observe fiber-local values.
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
