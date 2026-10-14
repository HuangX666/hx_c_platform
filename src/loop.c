#include "loop_private.h"

static int hx_os_loop_owner(hx_os_loop *loop)
{
	return loop->owner == hx_os_thread_id();
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

static hx_os_status hx_os_loop_create_internal(hx_os_context *context,
	hx_os_network *network, const hx_os_loop_options *input,
	hx_os_loop **out, hx_os_error *error)
{
	hx_os_loop_options options;
	hx_os_loop *loop;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	hx_os_loop_options_init(&options);
	if (input) {
		if (input->size < sizeof(*input) ||
			input->version != HX_OS_ABI_VERSION)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		options = *input;
	}

	if (!options.max_requests || !options.max_pending_bytes ||
		options.dns_threads > 64 ||
		(options.dns_threads && !options.dns_queue_capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	loop = hx_os_i_zero(context, sizeof(*loop));
	if (!loop)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	loop->network = network;
	loop->context = context;
	loop->options = options;
	loop->owner = hx_os_thread_id();

	status = hx_os_mutex_create(loop->context, 0, &loop->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_i_loop_native_create(loop, error);

	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(loop->mutex);
		hx_os_memory_free(loop);

		return status;
	}

	if (network)
		(void)hx_os_atomic_fetch_add(
			&network->objects, 1, HX_OS_RELAXED);

	*out = loop;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_loop_create(hx_os_network *network,
	const hx_os_loop_options *options, hx_os_loop **out, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK
	if (!network)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_loop_create_internal(
		network->context, network, options, out, error);
#else
	(void)network;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_loop_create_local(hx_os_context *context,
	const hx_os_loop_options *options, hx_os_loop **out, hx_os_error *error)
{
	return hx_os_loop_create_internal(context, NULL, options, out, error);
}

void hx_os_request_retain(hx_os_request *request)
{
	(void)hx_os_atomic_fetch_add(&request->references, 1, HX_OS_RELAXED);
}

void hx_os_request_release(hx_os_request *request)
{
	if (!request)
		return;

	if (hx_os_atomic_fetch_add(&request->references, -1, HX_OS_ACQ_REL) !=
		1)
		return;
	(void)hx_os_socket_close(&request->accepted, NULL);
	hx_os_address_list_destroy(request->addresses);
	hx_os_memory_free(request->owned_data);
	hx_os_memory_free(request->owned_host);
	hx_os_event_destroy(request->finished);
	hx_os_mutex_destroy(request->mutex);
	hx_os_memory_free(request);
}

static hx_os_status hx_os_loop_dns_worker(hx_os_task *task, void *user)
{
	hx_os_request *request = user;
	hx_os_operation *op = &request->operation;

	(void)task;
	if (hx_os_atomic_load(&request->cancelled, HX_OS_ACQUIRE))
		return hx_os_i_error(
			&request->completion.error, HX_OS_CANCELLED, 0, 0);

	if (hx_os_deadline_expired(op->deadline))
		return hx_os_i_error(
			&request->completion.error, HX_OS_TIMEOUT, 0, 0);

	return hx_os_resolve(request->loop->network, op->host, op->address.port,
		op->protocol, &request->addresses, &request->completion.error);
}

static void hx_os_loop_dns_cleanup(void *user)
{
	hx_os_request *request = user;

	(void)hx_os_mutex_lock(request->mutex, NULL);
	hx_os_atomic_store(&request->dns_finished, 1, HX_OS_RELEASE);
	hx_os_i_loop_wake(request->loop);
	(void)hx_os_mutex_unlock(request->mutex, NULL);

	hx_os_request_release(request);
}

static hx_os_status hx_os_loop_start_dns(
	hx_os_loop *loop, hx_os_request *request, hx_os_error *error)
{
	hx_os_task *task;
	hx_os_status status;

	if (!loop->options.dns_threads)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	if (!loop->dns_pool) {
		hx_os_thread_pool_options options;

		hx_os_thread_pool_options_init(&options);
		options.threads = loop->options.dns_threads;
		options.queue_capacity = loop->options.dns_queue_capacity;

		status = hx_os_thread_pool_create(
			loop->context, &options, &loop->dns_pool, error);
		if (status != HX_OS_OK)
			return status;
	}

	hx_os_request_retain(request);

	status = hx_os_thread_pool_submit(loop->dns_pool, hx_os_loop_dns_worker,
		request, hx_os_loop_dns_cleanup, &task, error);
	if (status == HX_OS_OK)
		hx_os_task_release(task);
	else
		hx_os_request_release(request);

	return status;
}

static hx_os_status hx_os_loop_validate(hx_os_loop *loop,
	const hx_os_operation *op, size_t *budget, int *directions)
{
	int kind = op->kind;

	*budget = 0;
	*directions = 0;
	if (op->size < sizeof(*op) || op->version != HX_OS_ABI_VERSION ||
		kind < HX_OS_OP_POST || kind > HX_OS_OP_RESOLVE)
		return HX_OS_INVALID;
	if (kind != HX_OS_OP_POST && !hx_os_loop_owner(loop))
		return HX_OS_BUSY;

	if (!loop->network && kind >= HX_OS_OP_CONNECT)
		return HX_OS_NOT_SUPPORTED;

	if (kind == HX_OS_OP_RESOLVE) {
		size_t length = 0;

		if (!op->host || !op->host[0] ||
			(op->protocol != HX_OS_TCP &&
				op->protocol != HX_OS_UDP))
			return HX_OS_INVALID;
		while (op->host[length] && length < 1024)
			++length;

		if (length == 1024)
			return HX_OS_LIMIT;

		*budget = length + 1;

		return HX_OS_OK;
	}

	if (kind == HX_OS_OP_POST || kind == HX_OS_OP_TIMER)
		return HX_OS_OK;

	if (!op->socket || op->socket->network != loop->network ||
		(op->socket->loop && op->socket->loop != loop))
		return HX_OS_INVALID;
	if (op->socket->family == HX_OS_LOCAL &&
		(kind == HX_OS_OP_CONNECT || kind == HX_OS_OP_ACCEPT))
		return HX_OS_NOT_SUPPORTED;
	*directions = kind == HX_OS_OP_CONNECT ? 3
		: kind == HX_OS_OP_READ || kind == HX_OS_OP_RECEIVE_FROM ||
			kind == HX_OS_OP_ACCEPT
		? HX_OS_WAIT_READ
		: HX_OS_WAIT_WRITE;
	if (hx_os_atomic_load(&op->socket->pending, HX_OS_ACQUIRE) &
		*directions)
		return HX_OS_BUSY;
	if ((kind == HX_OS_OP_ACCEPT || kind == HX_OS_OP_CONNECT ||
		    kind == HX_OS_OP_WRITE || kind == HX_OS_OP_READ) &&
		!hx_os_i_socket_stream(op->socket))
		return HX_OS_INVALID;
	if ((kind == HX_OS_OP_SEND_TO || kind == HX_OS_OP_RECEIVE_FROM) &&
		op->socket->protocol != HX_OS_UDP)
		return HX_OS_INVALID;
	if (kind == HX_OS_OP_CONNECT || kind == HX_OS_OP_SEND_TO) {
		if (op->address.family != op->socket->family)
			return HX_OS_INVALID;
	}

	if (kind >= HX_OS_OP_READ && kind <= HX_OS_OP_RECEIVE_FROM) {
		if ((!op->data && op->bytes) ||
			(op->send_mode != HX_OS_SEND_COPY &&
				op->send_mode != HX_OS_SEND_BORROW))
			return HX_OS_INVALID;
		if ((kind == HX_OS_OP_SEND_TO ||
			    kind == HX_OS_OP_RECEIVE_FROM) &&
			op->bytes > INT_MAX)
			return HX_OS_LIMIT;
		*budget = op->bytes;
	}

	return HX_OS_OK;
}

hx_os_status hx_os_i_loop_submit(hx_os_loop *loop, const hx_os_operation *op,
	hx_os_pipe *pipe, hx_os_loop_watch *watch,
	hx_os_completion_fn completion, void *user, hx_os_request **out,
	hx_os_error *error)
{
	hx_os_request *request = NULL;
	hx_os_status status;
	size_t budget;
	int directions;

	if (!loop || !op || !completion || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_os_error_clear(error);
	*out = NULL;
	if (pipe) {
		budget = op->bytes;
		directions = op->kind != HX_OS_I_OP_PIPE_WRITE
			? HX_OS_WAIT_READ
			: HX_OS_WAIT_WRITE;

		status = !hx_os_loop_owner(loop) ? HX_OS_BUSY
			: pipe->closing		 ? HX_OS_CLOSED
			: (pipe->loop && pipe->loop != loop) ||
				pipe->direction != directions
			? HX_OS_INVALID
			: pipe->request ? HX_OS_BUSY
					: HX_OS_OK;
	}
	else
		status = hx_os_loop_validate(loop, op, &budget, &directions);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	(void)hx_os_mutex_lock(loop->mutex, NULL);
	if (loop->closing)
		status = HX_OS_CLOSED;
	else if (loop->pending + loop->reserving >=
			loop->options.max_requests ||
		budget > loop->options.max_pending_bytes - loop->bytes)
		status = HX_OS_LIMIT;
	else {
		++loop->reserving;
		loop->bytes += budget;
	}

	(void)hx_os_mutex_unlock(loop->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	request = hx_os_i_zero(loop->context, sizeof(*request));
	if (!request) {
		status = HX_OS_NO_MEMORY;
		goto rejected;
	}

	hx_os_atomic_store(&request->references, 1, HX_OS_RELAXED);
	request->operation = *op;
	request->pipe = pipe;
	request->fd_watch = watch;
	request->loop = loop;
	request->budget = budget;
	request->directions = directions;
	request->callback = completion;
	request->user = user;

	status = hx_os_mutex_create(loop->context, 0, &request->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_event_create(
			loop->context, 1, 0, &request->finished, error);

	if (status != HX_OS_OK)
		goto rejected;

	if ((op->kind == HX_OS_OP_WRITE || op->kind == HX_OS_OP_SEND_TO ||
		    op->kind == HX_OS_I_OP_PIPE_WRITE) &&
		op->send_mode == HX_OS_SEND_COPY && op->bytes) {
		request->owned_data =
			hx_os_i_alloc(loop->context, op->bytes, 0);
		if (!request->owned_data) {
			status = HX_OS_NO_MEMORY;
			goto rejected;
		}

		memcpy(request->owned_data, op->data, op->bytes);
		request->operation.data = request->owned_data;
	}

	if (op->kind == HX_OS_OP_RESOLVE) {
		request->owned_host = hx_os_i_alloc(loop->context, budget, 0);
		if (!request->owned_host) {
			status = HX_OS_NO_MEMORY;
			goto rejected;
		}

		memcpy(request->owned_host, op->host, budget);
		request->operation.host = request->owned_host;

		status = hx_os_loop_start_dns(loop, request, error);
		if (status != HX_OS_OK)
			goto rejected;
	}

	if (watch) {
		status = hx_os_i_loop_fd_register(watch, error);
		if (status != HX_OS_OK)
			goto rejected;
	}

	if (pipe) {
		if (!pipe->loop) {
			status = hx_os_i_pipe_attach(loop, pipe, error);
			if (status != HX_OS_OK)
				goto rejected;

			pipe->loop = loop;
			pipe->next = loop->pipes;
			loop->pipes = pipe;
		}

		pipe->request = request;
	}
	else if (directions) {
		if (!op->socket->loop) {
			status = hx_os_i_loop_attach(loop, op->socket, error);
			if (status != HX_OS_OK)
				goto rejected;

			op->socket->loop = loop;
			op->socket->loop_next = loop->sockets;
			loop->sockets = op->socket;
		}

		(void)hx_os_atomic_fetch_add(
			&op->socket->pending, directions, HX_OS_RELEASE);
	}

	hx_os_request_retain(request);
	(void)hx_os_mutex_lock(loop->mutex, NULL);
	--loop->reserving;
	++loop->pending;
	if (loop->inbox_last)
		loop->inbox_last->next = request;
	else
		loop->inbox = request;

	loop->inbox_last = request;
	hx_os_i_loop_wake(loop);
	(void)hx_os_mutex_unlock(loop->mutex, NULL);
	*out = request;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);

rejected:
	hx_os_request_release(request);
	(void)hx_os_mutex_lock(loop->mutex, NULL);
	--loop->reserving;
	loop->bytes -= budget;
	hx_os_i_loop_wake(loop);
	(void)hx_os_mutex_unlock(loop->mutex, NULL);
	if (error && error->status == status)
		return status;

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_loop_submit(hx_os_loop *loop, const hx_os_operation *op,
	hx_os_completion_fn completion, void *user, hx_os_request **out,
	hx_os_error *error)
{
	return hx_os_i_loop_submit(
		loop, op, NULL, NULL, completion, user, out, error);
}

void hx_os_request_cancel(hx_os_request *request)
{
	if (!request)
		return;

	(void)hx_os_mutex_lock(request->mutex, NULL);
	if (request->loop) {
		hx_os_atomic_store(&request->cancelled, 1, HX_OS_RELEASE);
		hx_os_i_loop_wake(request->loop);
	}

	(void)hx_os_mutex_unlock(request->mutex, NULL);
}

hx_os_status hx_os_request_wait_until(hx_os_request *request,
	hx_os_deadline deadline, hx_os_completion *completion,
	hx_os_error *error)
{
	hx_os_status status;
	int owner = 0;

	if (!request)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	(void)hx_os_mutex_lock(request->mutex, NULL);
	if (request->loop)
		owner = hx_os_loop_owner(request->loop);

	(void)hx_os_mutex_unlock(request->mutex, NULL);
	if (hx_os_atomic_load(&request->state, HX_OS_ACQUIRE) != 2) {
		if (owner)
			return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

		status = hx_os_event_wait_until(
			request->finished, deadline, error);
		if (status != HX_OS_OK)
			return status;
	}

	if (completion)
		*completion = request->completion;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_socket *hx_os_request_take_socket(hx_os_request *request)
{
	hx_os_socket *socket = NULL;

	(void)hx_os_mutex_lock(request->mutex, NULL);
	if (hx_os_atomic_load(&request->state, HX_OS_ACQUIRE)) {
		socket = request->accepted;
		request->accepted = NULL;
	}

	(void)hx_os_mutex_unlock(request->mutex, NULL);

	return socket;
}

hx_os_address_list *hx_os_request_take_addresses(hx_os_request *request)
{
	hx_os_address_list *list = NULL;

	(void)hx_os_mutex_lock(request->mutex, NULL);
	if (hx_os_atomic_load(&request->state, HX_OS_ACQUIRE)) {
		list = request->addresses;
		request->addresses = NULL;
	}

	(void)hx_os_mutex_unlock(request->mutex, NULL);

	return list;
}

size_t hx_os_loop_pending(hx_os_loop *loop)
{
	size_t count;

	(void)hx_os_mutex_lock(loop->mutex, NULL);
	count = loop->pending + loop->reserving;
	(void)hx_os_mutex_unlock(loop->mutex, NULL);

	return count;
}

static void hx_os_loop_finish(hx_os_loop *loop, hx_os_request *request)
{
	if (request->stop_status != HX_OS_OK) {
		(void)hx_os_i_error(&request->completion.error,
			request->stop_status, request->completion.error.domain,
			request->completion.error.native_code);

		(void)hx_os_socket_close(&request->accepted, NULL);
		hx_os_address_list_destroy(request->addresses);
		request->addresses = NULL;
	}

	if (request->pipe) {
		request->pipe->request = NULL;
		(void)hx_os_i_pipe_refresh(request->pipe, NULL);
		request->pipe = NULL;
	}
	else if (request->directions) {
		(void)hx_os_atomic_fetch_add(
			&request->operation.socket->pending,
			-request->directions, HX_OS_RELEASE);
		(void)hx_os_i_loop_refresh(request->operation.socket, NULL);
	}

	hx_os_atomic_store(&request->state, 1, HX_OS_RELEASE);
	request->callback(request, &request->completion, request->user);
	(void)hx_os_mutex_lock(request->mutex, NULL);
	request->loop = NULL;
	hx_os_atomic_store(&request->state, 2, HX_OS_RELEASE);
	(void)hx_os_mutex_unlock(request->mutex, NULL);
	hx_os_event_set(request->finished);
	(void)hx_os_mutex_lock(loop->mutex, NULL);
	--loop->pending;
	loop->bytes -= request->budget;
	(void)hx_os_mutex_unlock(loop->mutex, NULL);

	hx_os_request_release(request);
}

static size_t hx_os_loop_pump(hx_os_loop *loop, hx_os_deadline *next)
{
	hx_os_request **cursor;
	size_t completed = 0;

	(void)hx_os_mutex_lock(loop->mutex, NULL);
	if (loop->inbox_last) {
		loop->inbox_last->next = loop->active;
		loop->active = loop->inbox;
		loop->inbox = NULL;
		loop->inbox_last = NULL;
	}

	(void)hx_os_mutex_unlock(loop->mutex, NULL);
	cursor = &loop->active;

	while (*cursor) {
		hx_os_request *request = *cursor;
		int kind = request->operation.kind;
		hx_os_deadline deadline = request->operation.deadline;

		if (request->stop_status == HX_OS_OK) {
			if (loop->closing ||
				hx_os_atomic_load(
					&request->cancelled, HX_OS_ACQUIRE))
				request->stop_status = HX_OS_CANCELLED;
			else if (kind != HX_OS_OP_TIMER &&
				hx_os_deadline_expired(deadline))
				request->stop_status = HX_OS_TIMEOUT;
		}

		if (kind == HX_OS_OP_RESOLVE)
			request->ready = (int)hx_os_atomic_load(
				&request->dns_finished, HX_OS_ACQUIRE);
		else if (kind == HX_OS_OP_POST)
			request->ready = 1;
		else if (request->timer)
			completed += hx_os_i_timer_step(request);
		else if (request->fd_watch)
			completed += hx_os_i_loop_fd_step(request);
		else if (kind == HX_OS_OP_TIMER)
			request->ready = request->stop_status ||
				hx_os_deadline_expired(deadline);
		else if (request->pipe)
			hx_os_i_pipe_step(request);
		else
			hx_os_i_loop_step(request);

		if (request->ready) {
			*cursor = request->next;
			hx_os_loop_finish(loop, request);
			++completed;
		}
		else {
			deadline = request->operation.deadline;
			if (!request->stop_status && deadline < *next)
				*next = deadline;

			cursor = &request->next;
		}
	}

	return completed;
}

// Caller retains request and owns the reentry flag through this call.
hx_os_status hx_os_i_loop_cancel_until(hx_os_loop *loop, hx_os_request *request,
	hx_os_deadline deadline, int *close_running, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;

	if (!hx_os_loop_owner(loop))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (*close_running)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	hx_os_request_cancel(request);
	if (hx_os_atomic_load(&request->state, HX_OS_ACQUIRE) == 2)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (loop->running)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	*close_running = 1;

	while (hx_os_atomic_load(&request->state, HX_OS_ACQUIRE) != 2) {
		status = hx_os_loop_poll_until(loop, deadline, error);
		if (status != HX_OS_OK)
			break;

		if (hx_os_atomic_load(&request->state, HX_OS_ACQUIRE) != 2 &&
			hx_os_deadline_expired(deadline)) {
			status = hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
			break;
		}
	}

	*close_running = 0;

	return status;
}

hx_os_status hx_os_loop_poll_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_deadline next = deadline;
	hx_os_status status = HX_OS_OK;
	size_t completed;

	if (!loop)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!hx_os_loop_owner(loop))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (loop->running)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	if (loop->closed)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

	loop->running = 1;

	completed = hx_os_loop_pump(loop, &next);
	if (!completed) {
		status = hx_os_i_loop_wait(loop, next, error);
		if (status == HX_OS_OK || status == HX_OS_TIMEOUT) {
			completed = hx_os_loop_pump(loop, &next);

			status = !completed && hx_os_deadline_expired(deadline)
				? HX_OS_TIMEOUT
				: HX_OS_OK;
		}
	}

	loop->running = 0;
	if (status == HX_OS_OK || status == HX_OS_TIMEOUT)
		return hx_os_i_error(error, status, 0, 0);

	return status;
}

hx_os_status hx_os_loop_run_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (!loop)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!hx_os_loop_owner(loop))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (loop->running)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	while (hx_os_loop_pending(loop)) {
		status = hx_os_loop_poll_until(loop, deadline, error);
		if (status != HX_OS_OK)
			return status;

		if (hx_os_loop_pending(loop) &&
			hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_loop_close_until(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (!loop)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!hx_os_loop_owner(loop))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (loop->running)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	(void)hx_os_mutex_lock(loop->mutex, NULL);
	loop->closing = 1;
	(void)hx_os_mutex_unlock(loop->mutex, NULL);
	if (loop->closed)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_loop_run_until(loop, deadline, error);
	if (status != HX_OS_OK)
		return status;

	if (loop->dns_pool) {
		status = hx_os_thread_pool_close_until(
			loop->dns_pool, HX_OS_POOL_DRAIN, deadline, error);
		if (status != HX_OS_OK)
			return status;

		(void)hx_os_thread_pool_destroy(&loop->dns_pool, NULL);
	}

	loop->closed = 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_loop_detach(hx_os_socket *socket, hx_os_error *error)
{
	hx_os_loop *loop = socket->loop;
	hx_os_socket **cursor;
	hx_os_status status;

	if (!hx_os_loop_owner(loop))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_i_loop_refresh(socket, error);
	if (status != HX_OS_OK)
		return status;

	for (cursor = &loop->sockets; *cursor; cursor = &(*cursor)->loop_next) {
		if (*cursor == socket) {
			*cursor = socket->loop_next;
			socket->loop = NULL;
			break;
		}
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_loop_destroy(hx_os_loop **loop, hx_os_error *error)
{
	hx_os_loop *value;
	hx_os_network *network;

	if (!loop)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*loop)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	value = *loop;
	if (!hx_os_loop_owner(value) || value->running || !value->closed ||
		value->sockets || value->pipes || value->timers ||
		value->fd_watches || hx_os_loop_pending(value))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	network = value->network;

	hx_os_i_loop_native_destroy(value);
	hx_os_mutex_destroy(value->mutex);
	hx_os_memory_free(value);
	*loop = NULL;
	if (network)
		(void)hx_os_atomic_fetch_add(
			&network->objects, -1, HX_OS_RELEASE);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
