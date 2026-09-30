#include "internal.h"
#include "hx_os_net.h"
#include "hx_os_loop.h"

/* Disabled modules keep ABI entry points and reject resource creation. */

#if !HX_OS_ENABLE_MEMORY_POOLS

void hx_os_memory_pool_options_init(hx_os_memory_pool_options *options)
{
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->block_bytes = 65536;
	options->cached_bytes = 262144;
}

hx_os_status hx_os_memory_pool_create(hx_os_context *context,
	const hx_os_memory_pool_options *options, hx_os_memory_pool **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_memory_pool_alloc(hx_os_memory_pool *pool, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	(void)pool;
	(void)bytes;
	(void)alignment;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_memory_pool_realloc(hx_os_memory_pool *pool, void **memory,
	size_t bytes, hx_os_error *error)
{
	(void)pool;
	(void)memory;
	(void)bytes;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_memory_pool_free(hx_os_memory_pool *pool, void *memory)
{
	(void)pool;
	(void)memory;
}

void hx_os_memory_pool_collect(hx_os_memory_pool *pool)
{
	(void)pool;
}

void hx_os_memory_pool_stats(hx_os_memory_pool *pool, hx_os_memory_stats *stats)
{
	(void)pool;
	if (stats)
		memset(stats, 0, sizeof(*stats));
}

hx_os_status hx_os_memory_pool_destroy(
	hx_os_memory_pool **pool, hx_os_error *error)
{
	(void)pool;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_object_pool_create(hx_os_context *context,
	size_t object_bytes, size_t alignment, size_t maximum_objects,
	hx_os_object_pool **out, hx_os_error *error)
{
	(void)context;
	(void)object_bytes;
	(void)alignment;
	(void)maximum_objects;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_object_pool_acquire(
	hx_os_object_pool *pool, void **out, hx_os_error *error)
{
	(void)pool;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_object_pool_release(hx_os_object_pool *pool, void *object)
{
	(void)pool;
	(void)object;
}

hx_os_status hx_os_object_pool_destroy(
	hx_os_object_pool **pool, hx_os_error *error)
{
	(void)pool;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_arena_create(hx_os_context *context, size_t block_bytes,
	size_t maximum_bytes, hx_os_arena **out, hx_os_error *error)
{
	(void)context;
	(void)block_bytes;
	(void)maximum_bytes;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_arena_alloc(hx_os_arena *arena, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	(void)arena;
	(void)bytes;
	(void)alignment;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_arena_reset(hx_os_arena *arena)
{
	(void)arena;
}

void hx_os_arena_destroy(hx_os_arena *arena)
{
	(void)arena;
}
#endif

#if !HX_OS_ENABLE_THREAD_POOL

hx_os_context *hx_os_thread_pool_context(const hx_os_thread_pool *pool)
{
	(void)pool;
	return NULL;
}

hx_os_status hx_os_thread_pool_inside(
	const hx_os_thread_pool *pool, int *inside)
{
	(void)pool;
	(void)inside;
	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_thread_pool_submit_prepared(hx_os_thread_pool *pool,
	hx_os_task_prepare_fn prepare, void *factory, hx_os_task_fn function,
	hx_os_task_cleanup cleanup, hx_os_task **out, hx_os_error *error)
{
	(void)pool;
	(void)prepare;
	(void)factory;
	(void)function;
	(void)cleanup;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_thread_pool_options_init(hx_os_thread_pool_options *options)
{
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->threads = 2;
	options->queue_capacity = 256;
}

hx_os_status hx_os_thread_pool_create(hx_os_context *context,
	const hx_os_thread_pool_options *options, hx_os_thread_pool **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_thread_pool_submit(hx_os_thread_pool *pool,
	hx_os_task_fn function, void *user, hx_os_task_cleanup cleanup,
	hx_os_task **out, hx_os_error *error)
{
	(void)pool;
	(void)function;
	(void)user;
	(void)cleanup;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

int hx_os_task_cancel_requested(hx_os_task *task)
{
	(void)task;
	return 0;
}

void hx_os_task_cancel(hx_os_task *task)
{
	(void)task;
}

int hx_os_task_state(hx_os_task *task)
{
	(void)task;
	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_task_wait_until(hx_os_task *task, hx_os_deadline deadline,
	hx_os_status *work_status, hx_os_error *error)
{
	(void)task;
	(void)deadline;
	(void)work_status;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_task_retain(hx_os_task *task)
{
	(void)task;
}

void hx_os_task_release(hx_os_task *task)
{
	(void)task;
}

hx_os_status hx_os_thread_pool_close_until(hx_os_thread_pool *pool, int mode,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)pool;
	(void)mode;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_thread_pool_destroy(
	hx_os_thread_pool **pool, hx_os_error *error)
{
	(void)pool;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif

#if !HX_OS_ENABLE_NETWORK

hx_os_status hx_os_network_create(
	hx_os_context *context, hx_os_network **out, hx_os_error *error)
{
	(void)context;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_network_destroy(hx_os_network **network, hx_os_error *error)
{
	(void)network;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_address_parse(const char *numeric_host, uint16_t port,
	hx_os_address *out, hx_os_error *error)
{
	(void)numeric_host;
	(void)port;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_address_format(const hx_os_address *address, char *buffer,
	size_t capacity, hx_os_error *error)
{
	(void)address;
	(void)buffer;
	(void)capacity;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_resolve(hx_os_network *network, const char *host,
	uint16_t port, int protocol, hx_os_address_list **out,
	hx_os_error *error)
{
	(void)network;
	(void)host;
	(void)port;
	(void)protocol;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

size_t hx_os_address_list_count(const hx_os_address_list *list)
{
	(void)list;
	return 0;
}

const hx_os_address *hx_os_address_list_get(
	const hx_os_address_list *list, size_t index)
{
	(void)list;
	(void)index;
	return NULL;
}

void hx_os_address_list_destroy(hx_os_address_list *list)
{
	(void)list;
}

hx_os_status hx_os_socket_create(hx_os_network *network, int family,
	int protocol, hx_os_socket **out, hx_os_error *error)
{
	(void)network;
	(void)family;
	(void)protocol;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_bind(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error)
{
	(void)socket;
	(void)address;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_listen(
	hx_os_socket *socket, int backlog, hx_os_error *error)
{
	(void)socket;
	(void)backlog;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_local_address(
	hx_os_socket *socket, hx_os_address *address, hx_os_error *error)
{
	(void)socket;
	(void)address;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_accept(hx_os_socket *listener, hx_os_socket **out,
	hx_os_address *peer, hx_os_error *error)
{
	(void)listener;
	if (out)
		*out = NULL;
	(void)peer;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_connect(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error)
{
	(void)socket;
	(void)address;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_connect_until(hx_os_socket *socket,
	const hx_os_address *address, hx_os_deadline deadline,
	hx_os_error *error)
{
	(void)socket;
	(void)address;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_wait_until(hx_os_socket *socket, int events,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)socket;
	(void)events;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_send_some(hx_os_socket *socket, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	(void)socket;
	(void)data;
	(void)bytes;
	if (completed)
		*completed = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_receive_some(hx_os_socket *socket, void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	(void)socket;
	(void)data;
	(void)bytes;
	if (completed)
		*completed = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_send_until(hx_os_socket *socket, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	(void)socket;
	(void)data;
	(void)bytes;
	(void)deadline;
	if (completed)
		*completed = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_send_to(hx_os_socket *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	(void)socket;
	(void)peer;
	(void)data;
	(void)bytes;
	if (completed)
		*completed = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_receive_from(hx_os_socket *socket,
	hx_os_address *peer, void *data, size_t bytes, size_t *completed,
	hx_os_error *error)
{
	(void)socket;
	(void)peer;
	(void)data;
	(void)bytes;
	if (completed)
		*completed = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_shutdown(
	hx_os_socket *socket, int direction, hx_os_error *error)
{
	(void)socket;
	(void)direction;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_socket_close(hx_os_socket **socket, hx_os_error *error)
{
	(void)socket;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif

#if !HX_OS_ENABLE_LOOP

hx_os_status hx_os_loop_create_local(hx_os_context *context,
	const hx_os_loop_options *options, hx_os_loop **out, hx_os_error *error)
{
	(void)context;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_loop_options_init(hx_os_loop_options *options)
{
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->max_requests = 1024;
	options->max_pending_bytes = 4 * 1024 * 1024;
	options->dns_threads = 2;
	options->dns_queue_capacity = 16;
}

void hx_os_operation_init(hx_os_operation *operation, int kind)
{
	memset(operation, 0, sizeof(*operation));
	operation->size = sizeof(*operation);
	operation->version = HX_OS_ABI_VERSION;
	operation->kind = kind;
	operation->protocol = HX_OS_TCP;
	operation->deadline = HX_OS_DEADLINE_INFINITE;
}

hx_os_status hx_os_loop_create(hx_os_network *network,
	const hx_os_loop_options *options, hx_os_loop **out, hx_os_error *error)
{
	(void)network;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_loop_submit(hx_os_loop *loop,
	const hx_os_operation *operation, hx_os_completion_fn completion,
	void *user, hx_os_request **out, hx_os_error *error)
{
	(void)loop;
	(void)operation;
	(void)completion;
	(void)user;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_loop_poll_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	(void)loop;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_loop_run_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	(void)loop;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

size_t hx_os_loop_pending(hx_os_loop *loop)
{
	(void)loop;
	return 0;
}

const char *hx_os_loop_backend(void)
{
	return "disabled";
}

void hx_os_request_cancel(hx_os_request *request)
{
	(void)request;
}

void hx_os_request_retain(hx_os_request *request)
{
	(void)request;
}

void hx_os_request_release(hx_os_request *request)
{
	(void)request;
}

hx_os_status hx_os_request_wait_until(hx_os_request *request,
	hx_os_deadline deadline, hx_os_completion *completion,
	hx_os_error *error)
{
	(void)request;
	(void)deadline;
	(void)completion;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_socket *hx_os_request_take_socket(hx_os_request *request)
{
	(void)request;
	return NULL;
}

hx_os_address_list *hx_os_request_take_addresses(hx_os_request *request)
{
	(void)request;
	return NULL;
}

hx_os_status hx_os_loop_close_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	(void)loop;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_loop_destroy(hx_os_loop **loop, hx_os_error *error)
{
	(void)loop;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif

#if !HX_OS_ENABLE_LOOP && HX_OS_ENABLE_NETWORK
#include "net_private.h"

hx_os_status hx_os_i_loop_detach(hx_os_socket *socket, hx_os_error *error)
{
	(void)socket;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif
