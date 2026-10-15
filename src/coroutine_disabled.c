#include "internal.h"
#include "hx_os_coroutine.h"

hx_os_status hx_os_coro_binding_create(hx_os_coro_runtime *runtime,
	hx_os_coro_binding **out, hx_os_error *error)
{
	(void)runtime;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_binding_close(
	hx_os_coro_binding **binding, hx_os_error *error)
{
	if (binding && !*binding)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_value_alloc(hx_os_coro_channel *channel, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	(void)channel;
	(void)bytes;
	(void)alignment;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_coro_value_free(void *memory)
{
	(void)memory;
}

hx_os_status hx_os_coro_options_init(
	hx_os_coro_options *options, size_t capacity)
{
	const hx_os_coro_options defaults = HX_OS_CORO_OPTIONS_INIT;

	if (!options || capacity < sizeof(defaults))
		return HX_OS_INVALID;

	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_coro_runtime_create(hx_os_context *context,
	const hx_os_coro_options *options, hx_os_coro_runtime **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_go(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_fn function, void *user, hx_os_coro_cleanup_fn cleanup,
	hx_os_coro **out, hx_os_error *error)
{
	(void)runtime;
	(void)kind;
	(void)function;
	(void)user;
	(void)cleanup;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_go_prepared(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_prepare_fn prepare, void *factory, hx_os_coro_fn function,
	hx_os_coro_cleanup_fn cleanup, hx_os_coro **out, hx_os_error *error)
{
	(void)runtime;
	(void)kind;
	(void)prepare;
	(void)factory;
	(void)function;
	(void)cleanup;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_context *hx_os_coro_runtime_context(const hx_os_coro_runtime *runtime)
{
	(void)runtime;

	return NULL;
}

hx_os_coro *hx_os_coro_current(void)
{
	return NULL;
}

int hx_os_coro_kind(const hx_os_coro *coroutine)
{
	(void)coroutine;

	return -1;
}

int hx_os_coro_wait_pending(void)
{
	return 0;
}

hx_os_status hx_os_coro_complete(hx_os_status status, const hx_os_error *detail)
{
	(void)status;
	(void)detail;

	return HX_OS_NOT_SUPPORTED;
}

void hx_os_coro_retain(hx_os_coro *coroutine)
{
	(void)coroutine;
}

void hx_os_coro_release(hx_os_coro *coroutine)
{
	(void)coroutine;
}

void hx_os_coro_cancel(hx_os_coro *coroutine)
{
	(void)coroutine;
}

int hx_os_coro_cancel_requested(const hx_os_coro *coroutine)
{
	(void)coroutine;

	return 0;
}

int hx_os_coro_ready(const hx_os_coro *coroutine)
{
	(void)coroutine;

	return 0;
}

hx_os_status hx_os_coro_wait_until(hx_os_coro *coroutine,
	hx_os_deadline deadline, hx_os_error *work_error, hx_os_error *error)
{
	(void)coroutine;
	(void)deadline;
	(void)work_error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_yield(hx_os_error *error)
{
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_sleep_until(hx_os_deadline deadline, hx_os_error *error)
{
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_sleep_ms(uint64_t milliseconds, hx_os_error *error)
{
	(void)milliseconds;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_runtime_close_until(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)runtime;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_runtime_drain_until(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)runtime;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_runtime_destroy(
	hx_os_coro_runtime **runtime, hx_os_error *error)
{
	(void)runtime;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_channel_create(hx_os_coro_runtime *runtime,
	const hx_os_container_options *values, size_t capacity,
	hx_os_coro_channel **out, hx_os_error *error)
{
	(void)runtime;
	(void)values;
	(void)capacity;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_coro_channel_retain(hx_os_coro_channel *channel)
{
	(void)channel;
}

void hx_os_coro_channel_release(hx_os_coro_channel *channel)
{
	(void)channel;
}

hx_os_status hx_os_coro_channel_close(
	hx_os_coro_channel *channel, hx_os_error *error)
{
	(void)channel;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_channel_send_until(hx_os_coro_channel *channel,
	const void *value, hx_os_deadline deadline, hx_os_error *error)
{
	(void)channel;
	(void)value;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_channel_receive_until(hx_os_coro_channel *channel,
	void *out, hx_os_deadline deadline, hx_os_error *error)
{
	(void)channel;
	(void)out;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_select(const hx_os_coro_case *cases, size_t count,
	hx_os_deadline deadline, size_t *selected, hx_os_error *error)
{
	(void)cases;
	(void)count;
	(void)deadline;
	(void)selected;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_coro_stream_retain(hx_os_coro_stream *stream)
{
	(void)stream;
}

void hx_os_coro_stream_release(hx_os_coro_stream *stream)
{
	(void)stream;
}

hx_os_status hx_os_coro_dial(const hx_os_address *address,
	hx_os_deadline deadline, hx_os_coro_stream **out, hx_os_error *error)
{
	(void)address;
	(void)deadline;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_listen(const hx_os_address *address, int backlog,
	int reuse_address, hx_os_coro_stream **out, hx_os_address *bound,
	hx_os_error *error)
{
	(void)address;
	(void)backlog;
	(void)reuse_address;
	if (out)
		*out = NULL;

	(void)bound;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_accept(hx_os_coro_stream *listener,
	hx_os_deadline deadline, hx_os_coro_stream **out, hx_os_address *peer,
	hx_os_error *error)
{
	(void)listener;
	(void)deadline;
	if (out)
		*out = NULL;

	(void)peer;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_read(hx_os_coro_stream *stream, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	(void)stream;
	(void)data;
	(void)bytes;
	(void)deadline;
	(void)completed;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_write(hx_os_coro_stream *stream, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	(void)stream;
	(void)data;
	(void)bytes;
	(void)deadline;
	(void)completed;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_resolve(const char *host, uint16_t port,
	hx_os_deadline deadline, hx_os_address_list **out, hx_os_error *error)
{
	(void)host;
	(void)port;
	(void)deadline;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_udp_bind(const hx_os_address *address,
	int reuse_address, hx_os_coro_stream **out, hx_os_address *bound,
	hx_os_error *error)
{
	(void)address;
	(void)reuse_address;
	(void)bound;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_send_to(hx_os_coro_stream *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	(void)socket;
	(void)peer;
	(void)data;
	(void)bytes;
	(void)deadline;
	if (completed)
		*completed = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_receive_from(hx_os_coro_stream *socket,
	hx_os_address *peer, void *data, size_t capacity,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	(void)socket;
	(void)data;
	(void)capacity;
	(void)deadline;
	if (completed)
		*completed = 0;

	if (peer)
		memset(peer, 0, sizeof(*peer));

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_pipe_pair(hx_os_coro_stream **reader,
	hx_os_coro_stream **writer, hx_os_error *error)
{
	if (reader)
		*reader = NULL;

	if (writer)
		*writer = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_spawn(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_fn function, void *user, hx_os_coro_cleanup_fn cleanup,
	hx_os_error *error)
{
	hx_os_coro *out;

	return hx_os_coro_go(
		runtime, kind, function, user, cleanup, &out, error);
}

hx_os_status hx_os_coro_spawn_prepared(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_prepare_fn prepare, void *factory, hx_os_coro_fn function,
	hx_os_coro_cleanup_fn cleanup, hx_os_error *error)
{
	hx_os_coro *out;

	return hx_os_coro_go_prepared(runtime, kind, prepare, factory, function,
		cleanup, &out, error);
}

hx_os_status hx_os_coro_errors_read(hx_os_coro_runtime *runtime, int clear,
	hx_os_coro_errors *out, hx_os_error *error)
{
	(void)runtime;
	(void)clear;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_runtime_wait_until(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)runtime;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_coro_runtime *hx_os_coro_current_runtime(void)
{
	return NULL;
}

hx_os_status hx_os_coro_local_key_create(hx_os_context *context,
	hx_os_tls_destructor cleanup, void *user, hx_os_coro_local_key **out,
	hx_os_error *error)
{
	(void)context;
	(void)cleanup;
	(void)user;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_coro_local_key_retain(hx_os_coro_local_key *key)
{
	(void)key;
}

void hx_os_coro_local_key_release(hx_os_coro_local_key *key)
{
	(void)key;
}

void *hx_os_coro_local_get(hx_os_coro_local_key *key)
{
	(void)key;

	return NULL;
}

hx_os_status hx_os_coro_local_set(
	hx_os_coro_local_key *key, void *value, hx_os_error *error)
{
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_coro_sync_retain(hx_os_coro_sync *sync)
{
	(void)sync;
}

void hx_os_coro_sync_release(hx_os_coro_sync *sync)
{
	(void)sync;
}

hx_os_status hx_os_coro_sync_close(hx_os_coro_sync *sync, hx_os_error *error)
{
	(void)sync;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_mutex_create(
	hx_os_coro_runtime *runtime, hx_os_coro_mutex **out, hx_os_error *error)
{
	(void)runtime;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_mutex_lock_until(
	hx_os_coro_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	(void)mutex;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_mutex_unlock(
	hx_os_coro_mutex *mutex, hx_os_error *error)
{
	(void)mutex;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_semaphore_create(hx_os_coro_runtime *runtime,
	size_t initial, size_t maximum, hx_os_coro_semaphore **out,
	hx_os_error *error)
{
	(void)runtime;
	(void)initial;
	(void)maximum;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_semaphore_wait_until(hx_os_coro_semaphore *semaphore,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)semaphore;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_semaphore_post(
	hx_os_coro_semaphore *semaphore, size_t count, hx_os_error *error)
{
	(void)semaphore;
	(void)count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_event_create(hx_os_coro_runtime *runtime,
	int manual_reset, int initially_set, hx_os_coro_event **out,
	hx_os_error *error)
{
	(void)runtime;
	(void)manual_reset;
	(void)initially_set;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_event_wait_until(
	hx_os_coro_event *event, hx_os_deadline deadline, hx_os_error *error)
{
	(void)event;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_event_set(hx_os_coro_event *event, hx_os_error *error)
{
	(void)event;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_event_reset(hx_os_coro_event *event, hx_os_error *error)
{
	(void)event;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_rwlock_create(hx_os_coro_runtime *runtime,
	hx_os_coro_rwlock **out, hx_os_error *error)
{
	(void)runtime;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_rwlock_read_until(
	hx_os_coro_rwlock *lock, hx_os_deadline deadline, hx_os_error *error)
{
	(void)lock;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_rwlock_write_until(
	hx_os_coro_rwlock *lock, hx_os_deadline deadline, hx_os_error *error)
{
	(void)lock;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_rwlock_read_unlock(
	hx_os_coro_rwlock *lock, hx_os_error *error)
{
	(void)lock;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_rwlock_write_unlock(
	hx_os_coro_rwlock *lock, hx_os_error *error)
{
	(void)lock;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_condition_create(hx_os_coro_runtime *runtime,
	hx_os_coro_condition **out, hx_os_error *error)
{
	(void)runtime;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_condition_wait_until(hx_os_coro_condition *condition,
	hx_os_coro_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	(void)condition;
	(void)mutex;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_condition_notify_one(
	hx_os_coro_condition *condition, hx_os_error *error)
{
	(void)condition;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_coro_condition_notify_all(
	hx_os_coro_condition *condition, hx_os_error *error)
{
	(void)condition;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
