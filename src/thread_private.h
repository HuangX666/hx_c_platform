#ifndef HX_C_PLATFORM_THREAD_PRIVATE_H
#define HX_C_PLATFORM_THREAD_PRIVATE_H

#include "internal.h"
#include "native_thread.h"

struct hx_os_mutex {
	hx_os_i_mutex_native native;
	int recursive;
};
struct hx_os_condition {
	hx_os_i_condition_native native;
};
struct hx_os_thread {
	hx_os_thread_fn function;
	void *user;
	void *result;
	hx_os_atomic joining;
	hx_os_atomic joined;
	hx_os_atomic started;
	hx_os_i_thread_native native;
};
struct hx_os_tls_key {
	hx_os_atomic values;
	hx_os_context *context;
	hx_os_tls_destructor destructor;
	void *user;
	hx_os_i_tls_native native;
};

hx_os_status hx_os_i_mutex_init(hx_os_mutex *mutex, hx_os_error *error);
hx_os_status hx_os_i_condition_init(
	hx_os_condition *condition, hx_os_error *error);
hx_os_status hx_os_i_thread_start(
	hx_os_thread *thread, size_t stack_size, hx_os_error *error);
hx_os_status hx_os_i_thread_name(
	hx_os_thread *thread, const char *name, hx_os_error *error);
hx_os_status hx_os_i_thread_priority(
	hx_os_thread *thread, int level, hx_os_error *error);
hx_os_status hx_os_i_thread_affinity(
	hx_os_thread *thread, uint64_t mask, hx_os_error *error);
hx_os_status hx_os_i_thread_join(
	hx_os_thread *thread, hx_os_deadline deadline, hx_os_error *error);
hx_os_status hx_os_i_thread_dispose(hx_os_thread *thread, hx_os_error *error);
hx_os_status hx_os_i_tls_init(hx_os_tls_key *key, hx_os_error *error);
void *hx_os_i_tls_get_native(hx_os_tls_key *key);
hx_os_status hx_os_i_tls_set_native(
	hx_os_tls_key *key, void *value, hx_os_error *error);
hx_os_status hx_os_i_tls_dispose(hx_os_tls_key *key, hx_os_error *error);
void hx_os_i_tls_cleanup(void *value);

#endif
